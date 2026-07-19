# 四位口令校验（固件解码）说明

更新时间：2026-07-18

> **源码**：`App/Pass/qr_comm.c`  
> **入口函数**：`qr_handle_password_input`（static）  
> **摘要函数**：`qr_build_password_auth_digest`（static）  
> **上游**：读头 JSON `type == '2'`，见 [qr-process-uart45.md](./qr-process-uart45.md)  
> **上报**：`pwd_Reply`（`App/Cloud/Live_data.c`）

本文只讲 **固件如何校验读头送来的 4 位数字口令**，不覆盖 App 侧如何生成该口令。

---

## 一、在系统中的位置

```text
读头 JSON：{"type":"2","data":"1234",...}
        │
        ▼
QRProcessUart4 / QRProcessUart5
        │ type == '2'
        ▼
qr_handle_password_input(data, uart_port)
        ├─ 格式校验 → pwd4_value
        ├─ RTC 三时刻摘要 → sum / sum2 / sum3
        ├─ 按 g_device_type 拆位比对
        └─ 成功：继电器 / 楼层呼叫；失败：蜂鸣；最后 pwd_Reply
```

依赖的设备参数（EEPROM 上电读入，见 `data_handler` / `eeprom`）：

| 变量 | 用途 |
| --- | --- |
| `g_device_code` | 拼进摘要源串 |
| `g_device_public_Key` | 拼进摘要源串 |
| `g_device_drift_time` | 分钟漂移窗口（取模与前后偏移） |
| `g_device_type` | `"1"` 园区门 / `"2"` 楼门禁 / `"3"` 电梯 |

---

## 二、格式校验

`data` 必须是 **恰好 4 个 ASCII 数字**（`'0'`～`'9'`），否则：

- `g_result.code = 400`，`error = "password format invalid, need 4 digits"`
- `Bsp_SetBeep(2)` 后直接返回（不上报口令成功路径）

合法时转为整数：

```text
pwd4_value = d0*1000 + d1*100 + d2*10 + d3
```

例如 `"2741"` → `2741`。

---

## 三、时间窗口（三时刻容差）

口令与 **当前 RTC 时间槽** 绑定。为容忍时钟偏差，固件对三个时刻各算一次摘要，**任一命中即通过**。

步骤：

1. `Refresh_CurrentTime()` → `ct = Get_CurrentTime()`
2. `drift_min = atoi(g_device_drift_time)`  
   - `Rtc_Time_ZeroSecondsMinuteMod` 只接受 **10 / 15 / 20 / 30 / 60**；其它值取模失败，后续摘要可能异常（联调时注意配置）
3. 秒清零，分钟向下对齐到 `drift_min` 的整数倍 → 得到「当前槽」`ct`
4. 再生成：
   - `ct_ahead`  = `ct` 往前 `drift_min` 分钟
   - `ct_behind` = `ct` 往后 `drift_min` 分钟
5. 对 `ct` / `ct_ahead` / `ct_behind` 各调用一次 `qr_build_password_auth_digest`，得到  
   `auth_hash_ascii_sum` / `sum2` / `sum3`

时间串格式由 `creatYMDHM` 生成：**`YYYYMMDDhhmm`**（12 位，无秒）。

---

## 四、摘要算法（`qr_build_password_auth_digest`）

对某个时刻 `t`：

```text
now_ymdh = creatYMDHM(t)                          // YYYYMMDDhhmm
auth_src = "{g_device_code},{g_device_public_Key},{now_ymdh}"
auth_dic_hash = SHA1(auth_src) 再转小写（40 hex）
auth_hash_left10 = auth_dic_hash 前 10 个字符
sum = left10 各字节 ASCII 值之和
返回 sum
```

要点：

- SHA1 实现：`Middlewares/sha1`（`sha1_hash`）
- 参与摘要的是 **设备编码 + 公钥 + 时间到分钟**，不是用户输入的四位数字本身
- 四位数字是对 `sum` 做模运算后的 **编码结果**；固件侧「解码」即按设备类型拆位再与 `sum % N` 比对

---

## 五、四位口令的拆解含义

把 `pwd4_value`（0～9999）拆成三段（源码调试打印与此一致）：

| 分量 | 计算 | 含义（业务） |
| --- | --- | --- |
| 园区门位（千位） | `pwd4_value / 1000` | 0～9，对应 `sum % 10` |
| 单元位 | `(pwd4_value % 1000) / 40` | 0～24，对应 `sum % 25` |
| 电梯楼层位 | `pwd4_value % 40` | 0～39，电梯呼叫用 |

数值关系示意（不必在固件里再算一遍，便于对照）：

```text
pwd4 = gate*1000 + unit*40 + floor
其中 gate ∈ [0,9]，unit ∈ [0,24]，floor ∈ [0,39]
```

同一四位数在不同 `g_device_type` 下 **只用其中一段** 做鉴权。

---

## 六、按设备类型校验与授权

三时刻摘要记为 `S0/S1/S2`（即 sum / sum2 / sum3）。下列条件中 **任一时刻满足** 即鉴权成功。

### 1. 园区门 — `g_device_type == "1"`

```text
S % 10 == pwd4_value / 1000
```

成功：

- UART4 → `Rly_Set1(1)` + `TIM1_ClearPendingAndEnable()`
- UART5 → `Rly_Set2(1)` + `TIM2_ClearPendingAndEnable()`
- `g_result.code = 200`，`Bsp_SetBeep(1)`

失败：`code = 400`，`Bsp_SetBeep(2)`。

### 2. 楼门禁 — `g_device_type == "2"`

```text
S % 25 == (pwd4_value % 1000) / 40
```

成功动作同园区门（对应读头继电器开闸）。失败同上。

### 3. 电梯 — `g_device_type == "3"`

鉴权条件与楼门禁相同（比对单元位）：

```text
S % 25 == (pwd4_value % 1000) / 40
```

成功后 **不走继电器**，而是：

1. `FloorCtrl_GetLimit` 取限层
2. `Floor_AuthCheck_limit_off` → `Floor_CallOne(pwd4_value % 40)` → `Floor_AuthCheck_limit`  
   - 呼叫楼层为 `pwd4_value % 40`  
   - 源码注释：与 App 楼层起点可能不一致（App 起点不一定为 1）
3. `Bsp_SetBeep(1)`

失败：`code = 400`，`Bsp_SetBeep(2)`。

---

## 七、流程简图

```text
data（字符串）
  │
  ├─ 非 4 位数字 ──► beep×2，return
  │
  ▼
pwd4_value
  │
  ▼
RTC 对齐 drift → 三时刻
  │
  ▼
对每个时刻：
  code,publicKey,YYYYMMDDhhmm → SHA1 → 左10 ASCII 和 S
  │
  ▼
type==1：千位 == S%10 ？
type==2/3：单元位 == S%25 ？
  │
  ├─ 是 → 授权（继电器或 Floor_CallOne）+ beep×1
  └─ 否 → beep×2
  │
  ▼
pwd_Reply(ts, ts, pwd4_value, g_result)
```

---

## 八、源码锚点（对照阅读）

| 逻辑 | 位置（约） |
| --- | --- |
| 时间串 `YYYYMMDDhhmm` | `creatYMDHM` |
| 摘要 SHA1 + 左 10 ASCII 和 | `qr_build_password_auth_digest` |
| 4 位格式与 `pwd4_value` | `qr_handle_password_input` 前半 |
| 三时刻漂移 | 同函数内 `Rtc_Time_ZeroSecondsMinuteMod` / `ds3231_shift_minutes` |
| 拆位调试打印 | `gate num` / `unit num` / `elevator num` |
| type 1/2/3 分支 | 同函数后半 |
| 读头分发 | `QRProcessUart4` / `QRProcessUart5` 中 `type == '2'` |

---

## 九、接手联调注意

- 先确认 EEPROM 里 `g_device_type`、`g_device_code`、`g_device_public_Key`、`g_device_drift_time` 与现场一致；摘要串错一位则永远校验失败。
- USART1 会打印 `auth source str`、`auth dic hash`、`ascii sum` 以及 `%10` / `%25` 与拆位值，优先用这些日志对口令。
- 时钟未同步或漂移配置非法时，三时刻窗口也对不上 App 生成槽。
- 电梯成功路径里限层开闭与呼叫顺序敏感；源码有 TODO（常开/人工按键与安检交互），改楼层控制时需单独验证。
- 入口与 JSON 切片问题见 [qr-process-uart45.md](./qr-process-uart45.md)，本文从「已拿到 `data` 字符串」之后写起。
