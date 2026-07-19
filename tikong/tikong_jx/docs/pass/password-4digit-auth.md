# 四位口令固件校验

更新时间：2026-07-19

> **范围**：固件如何校验读头送来的 4 位数字口令（不含 App 如何生成）。  
> **入口**：读头 JSON `type == '2'` → `qr_handle_password_input`，见 [qr-process-uart45.md](./qr-process-uart45.md)  
> **源码**：`App/Pass/qr_comm.c`（`qr_handle_password_input`、`qr_build_password_auth_digest`、`creatYMDHM`、`ds1302_shift_minutes`）  
> **上报**：`pwd_Reply`（`App/Cloud/Live_data.c`）  
> **重复/碰撞局限**：[password-4digit-collision.md](./password-4digit-collision.md)  
> **场景下不可接受的重复**：[password-4digit-unacceptable.md](./password-4digit-unacceptable.md)

---

## 1. 先建立整体图景

口令路径可以想成四件事：

1. 读头送来 4 个数字字符  
2. 用设备 RTC 算出「当前该用哪几个时间」  
3. 用「设备码 + 公钥 + 时间」算出一个整数 `sum`（摘要）  
4. 按设备类型，从 4 位数字里拆出一段，与 `sum` 比对；对上就开门/呼梯

```text
读头 JSON {"type":"2","data":"2741",...}
        │
        ▼
  格式校验 → pwd4_value = 2741
        │
        ▼
  RTC → 对齐成时间槽 → 当前 / 前一 / 后一 三个槽
        │
        ▼
  每个槽：拼源串 → SHA1 → 加工成 sum（共三个 sum）
        │
        ▼
  按 g_device_type 拆位比对（任一 sum 命中即通过）
        │
        ├─ 成功 → 继电器或呼梯 + beep×1
        └─ 失败 → beep×2
        │
        ▼
  pwd_Reply(...)
```

依赖的 EEPROM 参数（上电读入）：

| 变量 | 用途 |
| --- | --- |
| `g_device_code` | 拼进摘要源串 |
| `g_device_public_Key` | 拼进摘要源串 |
| `g_device_drift_time` | 时间槽宽度 D（分钟），以及邻槽偏移 |
| `g_device_type` | `"1"` 园区门 / `"2"` 楼门禁 / `"3"` 电梯 |

---

## 2. 必懂概念

### 2.1 四位数字不是「设备登录密码」

用户输入的 `"2741"` **不会**拿去和 EEPROM 里存的明文密码字符串比对。  
它是 App 按同一套规则算出来的**校验码**；固件用同样规则算出 `sum`，再看拆位后是否相等。

### 2.2 SHA1 和「摘要」分别是什么

| 名称 | 是什么 |
| --- | --- |
| **SHA1** | 一种哈希**算法**：任意字符串进去，固定得到 40 个十六进制字符。相同输入结果相同；改一个字符结果几乎全变。本工程入口 [`sha1_hash`](./sha1-hash.md)（[sha1.c](../../Middlewares/sha1.c)） |
| **摘要（本工程口令）** | **整条加工流程**，以及它产出的整数 `sum`。SHA1 只是其中一步 |

关系：

```text
设备码,公钥,时间串          ← 源串（材料）
        │
        ▼
     SHA1（算法/工具）
        │
        ▼
  40 个十六进制字符
        │
        ▼
  取前 10 个字符，把每个字符的 ASCII 码加起来
        │
        ▼
   整数 sum  ← 口令比对用的「摘要结果」
```

### 2.3 为什么口令要绑「时间槽」，而不是墙上的真实时刻

源串里有时间，例如：

```text
设备码,公钥,202607191430
                 └─ 年月日时分（没有秒）
```

若 App 用 `14:37`、设备用 `14:38`，源串不同 → SHA1 不同 → `sum` 不同 → 口令永远对不上。  
所以不能绑「精确到分钟的墙钟」，要绑 **时间槽**：在一段固定长度的时间内，两边都用**同一个代表时刻**。

类比公交车：每 D 分钟一班。你 14:37 上车，票面统一打成 **14:30**；14:30～14:39 上车的人票面都一样。

- **D（槽宽）** = `atoi(g_device_drift_time)`，单位分钟  
- 合法 D：**10 / 15 / 20 / 30 / 60**（`Rtc_Time_ZeroSecondsMinuteMod` 白名单；其它值对齐失败）

### 2.4 为什么要算三个时间（容差窗口）

即使已经对齐到槽，App 与设备仍可能差「一格」（刚换班、两边钟快慢）。  
固件因此对 **当前槽、前一槽、后一槽** 各算一次摘要，**任一命中即通过**。  
大约能盖住「差一格」；差两格通常仍失败。

```text
假设 D = 10

14:20          14:30          14:40          14:50
  |--- 前一槽 ---|--- 当前槽 ---|--- 后一槽 ---|
     ct_ahead         ct          ct_behind

墙钟 14:37 → 对齐后落在当前槽 → 代表时刻 14:30
```

| 情况 | 设备怎么兜住 |
| --- | --- |
| 两边同槽 | 当前槽 `ct` 对上 |
| App 还在上一格、设备已进本格 | 设备的前一槽 `ct_ahead` 对上 |
| App 已进下一格、设备仍在本格 | 设备的后一槽 `ct_behind` 对上 |

注意：同一个 `g_device_drift_time` 在权限有效期检查里是「把 begin/end 边界放宽几分钟」，与口令时间槽**不是同一算法**。见 [storage-logic.md](../store/storage-logic.md)。

---

## 3. 格式校验

`data` 必须恰好 4 个 ASCII 数字 `'0'`～`'9'`，否则：

- `g_result.code = 400`，`error = "password format invalid, need 4 digits"`
- `Bsp_SetBeep(2)` 后返回

合法时：

```text
pwd4_value = d0*1000 + d1*100 + d2*10 + d3
```

例：`"2741"` → `2741`。

---

## 4. 时间怎么变成三个时间串

对应源码约 275～316 行。

### 4.1 四步

```text
① 读 RTC
   Refresh_CurrentTime() → ct = Get_CurrentTime()
   例：2026-07-19 14:37:42

② 对齐到当前槽起点
   Rtc_Time_ZeroSecondsMinuteMod(&ct, &ct, D)
   · 秒 ← 0
   · 分钟 ← (分钟 ÷ D) 取整 × D   （向下取整，不是四舍五入）
   例 D=10：14:37:42 → 14:30:00  （这就是当前槽 ct）

③ 生成邻槽
   ct_ahead  = ct − D 分钟   （ds1302_shift_minutes，软件日历加减，不访问芯片）
   ct_behind = ct + D 分钟
   例：14:20:00 / 14:30:00 / 14:40:00

④ 格式化成时间串（无秒）
   creatYMDHM → YYYYMMDDhhmm（12 字符）
   例：202607191420 / 202607191430 / 202607191440
```

同一槽内任意墙钟，对齐结果相同，所以槽内摘要稳定。  
跨日：例如 `00:00` 且 D=10 时，前一槽为前一天 `23:50`。

### 4.2 手算例（联调可对日志）

| 已知 | 值 |
| --- | --- |
| D | 15 |
| RTC | 2026-07-19 09:22:08 |

| 步骤 | 结果 |
| --- | --- |
| 对齐 | 09:15:00（22→15，秒清 0） |
| 前一槽 | 09:00:00 |
| 后一槽 | 09:30:00 |
| 三串 | `202607190900` / `202607190915` / `202607190930` |

USART1 关键字：`before/after ... minute mod`、`ahead/behind by drift`、`current time ymdh:`。

`creatYMDHM` 使用**静态缓冲**，不要嵌套调用后仍拿旧指针当结果。

---

## 5. 摘要算法（每个时间串算一次）

函数：`qr_build_password_auth_digest`。对某个槽时刻 `t`：

```text
now_ymdh = creatYMDHM(t)                              // YYYYMMDDhhmm
auth_src = "{g_device_code},{g_device_public_Key},{now_ymdh}"
auth_dic_hash = sha1_hash(auth_src) 再转小写          // 40 hex
auth_hash_left10 = 前 10 个字符
sum = left10 各字符的 ASCII 值之和
返回 sum
```

要点：

- 参与计算的是 **设备码 + 公钥 + 时间**，不是用户输入的四位数字本身  
- 三个槽 → 三个 `sum`（源码里 `auth_hash_ascii_sum1` / `sum2` / `sum3`）  
- 四位数字是对 `sum` 做模运算后的编码；固件「解码」= 拆位再与 `sum % N` 比对  

---

## 6. 四位数字怎么拆

`pwd4_value`（0～9999）在业务上拆成三段（与调试打印一致）：

| 分量 | 计算 | 取值 | 用途 |
| --- | --- | --- | --- |
| 园区门位（千位） | `pwd4_value / 1000` | 0～9 | 对 `sum % 10` |
| 单元位 | `(pwd4_value % 1000) / 40` | 0～24 | 对 `sum % 25` |
| 电梯楼层位 | `pwd4_value % 40` | 0～39 | 呼梯楼层 |

关系示意：

```text
pwd4 ≈ gate*1000 + unit*40 + floor
```

同一四位数在不同 `g_device_type` 下**只用其中一段**做鉴权（电梯额外用楼层位呼叫）。

---

## 7. 按设备类型比对与授权

记三个摘要为 `S0/S1/S2`。下列条件中**任一**满足即成功。

### 园区门 — `g_device_type == "1"`

```text
S % 10 == pwd4_value / 1000
```

成功：UART4 → `Rly_Set1` + TIM1；UART5 → `Rly_Set2` + TIM2；`code=200`，beep×1。  
失败：`code=400`，beep×2。

### 楼门禁 — `g_device_type == "2"`

```text
S % 25 == (pwd4_value % 1000) / 40
```

成功动作同园区门（对应读头开闸）。

### 电梯 — `g_device_type == "3"`

鉴权与楼门禁相同（比对单元位）。成功后**不走继电器**，而是：

1. `FloorCtrl_GetLimit` 取限层  
2. `Floor_AuthCheck_limit_off` → `Floor_CallOne(pwd4_value % 40)` → `Floor_AuthCheck_limit`  
3. beep×1  

源码注释：App 楼层起点可能不为 1，与固件楼层号可能有偏差。限层开闭顺序敏感，有 TODO（常开/人工按键与安检）。

---

## 8. 端到端流程（对照用）

```text
data
  ├─ 非 4 位数字 → beep×2，return
  ▼
pwd4_value
  ▼
RTC → 对齐 D → ct / ct_ahead / ct_behind
  ▼
每个槽：code,publicKey,YYYYMMDDhhmm → SHA1 → 左10 ASCII 和 → S
  ▼
type 1：千位 == S%10 ？
type 2/3：单元位 == S%25 ？
  ├─ 是 → 授权 + beep×1
  └─ 否 → beep×2
  ▼
pwd_Reply(ts, ts, pwd4_value, g_result)
```

---

## 9. 源码锚点

| 逻辑 | 符号 / 文件 |
| --- | --- |
| 读头分发 `type=='2'` | `QRProcessUart4` / `QRProcessUart5` |
| 主流程 | `qr_handle_password_input` |
| 对齐 | `Rtc_Time_ZeroSecondsMinuteMod`（[RTC.c](../../Hardware/Time/RTC.c)） |
| ±D 分钟 | `ds1302_shift_minutes` |
| 时间串 | `creatYMDHM` |
| 摘要 | `qr_build_password_auth_digest` |
| 拆位调试打印 | `gate num` / `unit num` / `elevator num` |
| type 1/2/3 分支 | `qr_handle_password_input` 后半 |

---

## 10. 联调注意

- 先确认 `g_device_type`、`g_device_code`、`g_device_public_Key`、`g_device_drift_time` 与现场及 App 一致；源串错一位则永远失败。  
- D 必须在合法列表内，且与 App 槽宽一致。  
- 优先看 USART1：`auth source str`、`auth dic hash`、`ascii sum`、`%10`/`%25`、拆位值。  
- 时钟未同步或对齐失败时，三时刻也对不上 App。  
- 口令重复/碰撞（平台源串简单、`%10`/`%25` 空间小）见 [password-4digit-collision.md](./password-4digit-collision.md)。  
- JSON 切片问题见 [qr-process-uart45.md](./qr-process-uart45.md)；本文从已拿到 `data` 之后写起。
