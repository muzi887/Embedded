# gate_kaidu_count 逻辑梳理与问题建议

## 一、数据流概览

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  gate_kaidu_count：EEPROM 持久化的计数值（0 ~ gate_maxturns）                │
│  Num_count：运行时实时计数值，由光电编码器 CODE1/CODE2 更新                   │
│  gate_kaidu：开度百分比 = count * 100 / gate_maxturns                        │
└─────────────────────────────────────────────────────────────────────────────┘

  上电初始化                    运行中                          停止后
  para_read()              ChangeReality()                  JudeKaidu()
       │                         │                              │
       ▼                         ▼                              ▼
  gate_kaidu_count ──→ Num_count   Num_count ←── 编码器脉冲    Num_count 稳定
       │                   │              │                         │
       └──→ Num_count      └──→ gate_kaidu (实时)                  │
       └──→ gate_kaidu           │                                ▼
                                 │                    gate_kaidu_count = Num_count
                                 │                              │
                                 ▼                              ▼
                          dcmotor_ctrl()               EEPROM 写入 (EepromAddr=0x0006)
                          t3/t4 用 gate_kaidu 与 range_target 比较
```

## 二、各模块中的 gate_kaidu_count 逻辑

| 模块 | 位置 | 逻辑 |
|------|------|------|
| **reg.c** | `eepromToPara()` | 从 EEPROM 读 `gate_kaidu_count` → 赋给 `Num_count`，并计算 `gate_kaidu` |
| **reg.c** | `Eeprom_Wpara()` | 将 `gate_kaidu_count` 写入 EEPROM (0x000C) |
| **switch.c** | `ChangeReality()` | 用 `Num_count` 更新 `gate_kaidu`，不直接改 `gate_kaidu_count` |
| **switch.c** | `JudeKaidu()` | 电机停止且 `Num_count` 稳定后，`gate_kaidu_count = Num_count`，并触发 EEPROM 写入 |
| **dc_motor.c** | `dcmotor_ctrl()` | 用 `gate_kaidu` 与 `range_target` 比较，不直接使用 `gate_kaidu_count` |

## 三、发现的问题

### 3.1 reg.c 第 65 行：gate_kaidu 读取后被覆盖（冗余）

```c
armReg.gate_kaidu = eeprom_buf[GATE_KAIDU_ADDR];           // 第65行
armReg.gate_kaidu_count = eeprom_buf[GATE_KAIDU_COUNT_ADDR];
armReg.gate_kaidu = armReg.gate_kaidu_count * 100 / armReg.gate_maxturns;  // 第71行覆盖
```

第 65 行读取的 `gate_kaidu` 被第 71 行覆盖，实际生效的是 `gate_kaidu_count` 推导值。建议删除第 65 行或加注释说明。

### 3.2 gate_maxturns 为 0 时除零风险

- **reg.c** 第 71 行：`armReg.gate_kaidu = armReg.gate_kaidu_count * 100 / armReg.gate_maxturns`
- **switch.c** 第 121 行：`armReg.gate_kaidu = Num_count * 100 / armReg.gate_maxturns`

若 `gate_maxturns == 0` 会除零。建议增加 `gate_maxturns > 0` 判断。

### 3.3 JudeKaidu：限位停止时可能不写入 gate_kaidu_count

`JudeKaidu()` 仅在 `changeFlag == 1` 时进入写入逻辑，而 `changeFlag` 只在 `ChangeReality()` 中置 1，`ChangeReality()` 仅在编码器脉冲时调用。

**场景**：闸门已在限位附近，用户按下降键后几乎无脉冲就到达下限位，`ChangeReality()` 从未被调用，`changeFlag` 一直为 0，`JudeKaidu()` 不会更新 `gate_kaidu_count`。

**建议**：在 `JudeDir()` 中修改 `Num_count` 时（如 `Num_count = 0` 或 `Num_count = gate_maxturns`）同时置 `changeFlag = 1` 并更新 `gate_kaidu`。

### 3.4 dc_motor.c：range_target==0 时 t4 永远为假

```c
t4 = sys_moter_state == MOTOR_DOWN && armReg.gate_kaidu <= armReg.range_target 
     && armReg.range_target != 0 && remoteflag;
```

`range_target != 0` 导致远程下发“全关”(0) 时 t4 永远为假，只能靠下限位 t1 停止；若下限位故障，电机无法在 0 处停止。

**建议**：去掉 `range_target != 0`，或改为：
```c
t4 = sys_moter_state == MOTOR_DOWN && armReg.gate_kaidu <= armReg.range_target && remoteflag;
```

### 3.5 gate_kaidu_count 与 gate_kaidu 的 EEPROM 地址映射

- `para_read` 从 `eeprom_buf` 按索引读取
- `Eeprom_Wpara` 中 `gate_kaidu` 写 0x000A，`gate_kaidu_count` 写 0x000C

需确认 `AT24CXX_Readdatas(0x0000, eeprom_buf, 10)` 的第三个参数（字节数还是字数）及 `eeprom_buf` 索引与物理地址的对应关系。

## 四、当前正确的部分

1. **Num_count 初始化**：`Num_count = armReg.gate_kaidu_count` 正确，用计数值初始化。
2. **ChangeReality 的 FLAG 判断**：`!FLAG_OPENALL && !FLAG_CLOSEALL` 正确。
3. **gate_kaidu_count 与 Num_count 关系**：`gate_kaidu_count` 为持久化计数值，`Num_count` 为运行时计数值，关系清晰。
4. **JudeKaidu 写入时机**：电机停止且 `Num_count` 连续两次 500ms 不变后写入，逻辑合理。

## 五、修复优先级建议

| 优先级 | 问题 | 建议 |
|--------|------|------|
| P0 | range_target==0 无法停止 | 去掉 t4 中的 `range_target != 0` |
| P1 | gate_maxturns 除零 | 计算前判断 `gate_maxturns > 0` |
| P1 | 限位停止时可能不写入 | 在 JudeDir 修改 Num_count 时置 `changeFlag = 1` |
| P2 | reg.c 第 65 行冗余 | 删除或加注释说明 |
