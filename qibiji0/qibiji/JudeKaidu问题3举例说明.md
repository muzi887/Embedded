# JudeKaidu 问题 3 举例说明

## 问题 3 回顾

**现象**：`changeFlag` 仅在 `ChangeReality()` 中置 1，而 `ChangeReality()` 仅在 `JudeCount()` 检测到 CODE1/CODE2 脉冲时调用。若电机在限位处停止且此前一段时间无脉冲，`changeFlag` 可能仍为 0，导致 `JudeKaidu` 不进入写入逻辑，开度无法更新。

---

## 场景一：闸门已贴住下限位后再次“下降”

### 初始状态

- 闸门已全关，`FLAG_CLOSEALL = 1`，`Num_count = 0`
- 上次停止时已写入 `gate_kaidu = 0`，`changeFlag = 0`
- 用户按下“下降”键

### 执行过程

1. **dcmotor_keycheck**：检测到 KEY_DOWN，`sys_moter_state = MOTOR_DOWN`，`armReg.motor_on_off = MOTOR_DOWN`
2. **dcmotor_ctrl**：电机开始下降
3. **JudeDir**：`!DN_LIMIT && !FLAG_CLOSEALL` 为真（已贴住下限位，DN_LIMIT 为低），`FLAG_CLOSEALL = 1`，`Num_count = 0`
4. **dcmotor_ctrl**：`t1 = FLAG_CLOSEALL==1 && sys_moter_state==MOTOR_DOWN` 为真，`sys_moter_state = MOTOR_STOP`
5. **dcmotor_stop**：电机立即停止，`armReg.motor_on_off = MOTOR_STOP`

### 关键点

从按下到停止，闸门几乎没动，光电编码器没有产生脉冲，`ChangeReality()` 从未被调用，`changeFlag` 一直为 0。

### JudeKaidu 行为

```c
if ((armReg.motor_on_off == MOTOR_STOP) && (changeFlag))  // changeFlag == 0
{
    // 条件不成立，整个 if 块不执行
}
```

因此不会进入 `if`，不会更新 `LastNumCount`，也不会写入 `gate_kaidu`。虽然当前开度仍是 0，逻辑上不需要更新，但若之前 `gate_kaidu` 因某种原因被错误修改，这次也无法被纠正。

---

## 场景二：从中间位置下降到下限位（对比）

### 初始状态

- 闸门在 50% 开度，`Num_count = 40`，`gate_kaidu = 50`
- 用户远程下发“全关”，`range_target = 0`，电机开始下降

### 执行过程

1. 下降过程中：每次脉冲触发 `ChangeReality()`，`Num_count--`，`changeFlag = 1`
2. 接近下限位：最后一次脉冲时 `Num_count` 从 1 变为 0，`kaival = 0`，`changeFlag = 1`
3. 同一 TIM2 周期内：`JudeDir` 检测到下限位，`Num_count = 0`，`FLAG_CLOSEALL = 1`
4. 电机停止：`armReg.motor_on_off = MOTOR_STOP`

此场景下 `changeFlag` 为 1，`JudeKaidu` 会进入写入逻辑。问题 3 主要出现在“几乎无脉冲”的情况。

---

## 问题 3 的典型触发条件

| 条件 | 说明 |
|------|------|
| 电机在限位处停止 | 全开或全关 |
| 停止前几乎无脉冲 | 闸门未动或只动很少 |
| `changeFlag` 仍为 0 | 上次停止后未再产生脉冲 |

### 典型情况

1. 已贴住下限位，用户再按“下降” → 电机立刻停，无脉冲
2. 已贴住上限位，用户再按“上升” → 电机立刻停，无脉冲
3. 自动校准时，从下限位开始下降，闸门卡住或未动 → 无脉冲
4. 上电后首次运行，从限位启动并立即被限位停止 → 可能无脉冲

---

## 后果

- **正常情况**：开度本身正确，只是不会再次写入 EEPROM，影响较小
- **异常情况**：若 `gate_kaidu` 与真实位置不一致（如 EEPROM 损坏、初始化错误），本应借限位停止时修正，却因 `changeFlag = 0` 无法写入，错误会一直保留

---

## 可选修复思路

在 `JudeDir` 中，当直接修改 `Num_count` 时，同时更新 `kaival` 并置位 `changeFlag`，例如：

```c
// 在 JudeDir 中，当设置 Num_count = 0 或 Num_count = gate_maxturns 时：
changeFlag = 1;
if (armReg.gate_maxturns > 0)
    kaival = Num_count * 100 / armReg.gate_maxturns;
else
    kaival = 0;
```

这样即使没有脉冲，限位触发时也会让 `JudeKaidu` 有机会进入写入逻辑并更新开度。
