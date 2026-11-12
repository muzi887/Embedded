# 访客四位通行码方案：HH-TOTP-4-S

更新时间：2026-07-21

同一串 **4 位数字**，依次通过园区门 → 单元门 → 电梯（呼到授权层）。设备离线校验：本机密钥 + 本机时钟 + 收到的码。

**方案名称**：HH-TOTP-4-S（Hierarchical-HMAC Truncated OTP，4 位，Simple / Offline）

**产品约定**

| 项 | 约定 |
| -- | ---- |
| 园区门 | 不选单元，只输 4 位 |
| 尝试次数 | 不限制 |
| 时间槽 | 20 分钟一档；校验当前 ±1 档（共 3 档） |
| 有效时间 | 约 1 小时 |
| 漂移字段 | 无 |
| 校时 | 约每 3 个月现场手校 |

---

## 1. 做什么、不做什么

**做得到**

- 一票通行：同一码过园门、单元门、电梯。
- 离线自洽：不依赖设备在线。
- 隔离：跨园区、跨单元（单元门/电梯）、电梯错层（须唯一命中）。

**做不到 / 有意放宽**

- 园区门不保证「只开去某单元的人」（不选单元带来的代价）。
- 不限制试错次数；4 位空间下盲猜与重码率见 §8。
- 不抗国家级破解；默认不单次核销。

---

## 2. 业务输入（平台出码）

平台生成码时只需：

| 输入 | 符号 | 取值 | 含义 |
| ---- | ---- | ---- | ---- |
| 园区号 | `campus_id` | 1～300 | 哪个园区 |
| 单元号 | `unit_id` | 1～300 | 哪个单元 |
| 楼层号 | `floor` | 1～`F_MAX` | 呼梯层（例：访 1801 → `floor = 18`） |
| 生成时刻 | `t_gen` | Unix UTC 秒 | 用于算时间槽 |
| 园区密钥 | `K_c` | 128 bit | 不出给访客 |

约定：

1. `campus_id`、`unit_id`、`floor` 均为整数；协议编号上限 `ID_MAX = 300`。
2. `F_MAX` = 该单元**实际最高呼梯层**，按楼配置（`1 ≤ F_MAX ≤ 300`）。平台与该单元门、电梯必须一致。
3. 出码时必须 `1 ≤ floor ≤ F_MAX`。`F_MAX` 按真实楼层配（常见几十层），不要无故配成 300，否则重码率会升得很高（§8）。
4. 平台与现场对同一园区 / 单元 / 楼层用同一套编号。

---

## 3. 时间

| 参数 | 取值 | 含义 |
| ---- | ---- | ---- |
| `SLOT_SEC` | 1200（20 min） | `slot = ⌊unix_utc / 1200⌋` |
| `SKEW` | ±1 | 校验试 `slot-1, slot, slot+1` |
| 有效期话术 | 约 1 小时 | 相对生成时刻的三槽尺度 |
| 时区 | UTC | 只存 Unix 秒；本地时区不参与算槽 |

码失效：三档都对不上 → 须重新生成。

不设漂移时间字段。钟差靠约每 3 个月手校（§9）；不要靠加大 `SKEW` 代替校时。

---

## 4. 码怎么算出来

4 位不是「高位=楼层、低位=校验」的明文拼盘，而是：

```text
code = TruncDec4( HMAC-SHA1(K_u, msg) )   // 0000～9999

K_u = HMAC-SHA256(K_c, "unit" || U16BE(unit_id)) 的前 16 字节

msg = campus_id || unit_id || floor || slot     // 共 10 字节
```

| 字段 | 类型 | 取值 |
| ---- | ---- | ---- |
| `campus_id` | `u16` 大端 | 1～300 |
| `unit_id` | `u16` 大端 | 1～300 |
| `floor` | `u16` 大端 | 1～`F_MAX` |
| `slot` | `u32` 大端 | `⌊t / 1200⌋` |

`TruncDec4`：RFC 4226 动态截断得 31-bit，再 `mod 10000`，十进制补成 4 位。

---

## 5. 密钥谁拿什么

```text
K_c = 园区根密钥（云生成）
K_u = 由 K_c 与 unit_id 派生（每单元一把）
```

| 角色 | 持有 | 本机还需知道 |
| ---- | ---- | ------------ |
| 云 / App | `K_c`（可算任意 `K_u`） | — |
| 园区门 | 本园 `K_c` | `campus_id`、本园单元列表 `U[]` |
| 单元门 | 本单元 `K_u` | `campus_id`、`unit_id`、`F_MAX` |
| 电梯 | 与本单元门相同 `K_u` | `campus_id`、`unit_id`、`F_MAX` |

不同单元不得共用同一 `K_u`。电梯与同单元门同钥：楼层约束靠 `msg` 里的 `floor`，不靠另一把「层密钥」。

---

## 6. 平台生成（伪代码）

```text
function GenerateVisitorCode(campus_id, unit_id, floor_no, t_gen, K_c, F_MAX):
    assert 1 <= campus_id <= 300
    assert 1 <= unit_id   <= 300
    assert 1 <= floor_no  <= F_MAX
    K_u  = HMAC_SHA256(K_c, "unit" || U16BE(unit_id))[0:16]
    slot = ⌊t_gen / 1200⌋
    msg  = U16BE(campus_id) || U16BE(unit_id)
           || U16BE(floor_no) || U32BE(slot)
    return format("%04d", DynTrunc31(HMAC_SHA1(K_u, msg)) % 10000)
```

对访客展示：约 **1 小时内**有效。

---

## 7. 设备如何校验

公共判断（无次数限制）：

```text
function CodeMatches(K_u, campus_id, unit_id, floor_no, slot, code):
    msg = U16BE(campus_id) || U16BE(unit_id)
          || U16BE(floor_no) || U32BE(slot)
    return (DynTrunc31(HMAC_SHA1(K_u, msg)) % 10000) == code
```

### 7.1 单元门

已知本机 `campus_id, unit_id, K_u, F_MAX`。不要求访客先选层：在本单元所有层 × 三档上试。

```text
function VerifyUnitDoor(code, now):
    slot0 = ⌊now / 1200⌋
    for δ in [-1, 0, 1]:
        for f in 1 .. F_MAX:
            if CodeMatches(K_u, campus_id, unit_id, f, slot0+δ, code):
                return ACCEPT   // 开门
    return REJECT
```

### 7.2 电梯

同样搜索；但必须得到**唯一**楼层才呼梯，多命中则拒绝（避免歧义）。

```text
function VerifyElevator(code, now):
    slot0 = ⌊now / 1200⌋
    hits = []
    for δ in [-1, 0, 1]:
        for f in 1 .. F_MAX:
            if CodeMatches(K_u, campus_id, unit_id, f, slot0+δ, code):
                hits.append(f)
    hits = unique(hits)
    if len(hits) == 1:
        CallFloor(hits[0])
        return ACCEPT
    return REJECT
```

### 7.3 园区门（不选单元）

持 `K_c` 与本园 `U[]`。访客只输 4 位。对每个单元派生 `K_u`，再按单元门同款搜索；任一命中即开门。

```text
function VerifyCampusGate(code, now):
    slot0 = ⌊now / 1200⌋
    for u in U:
        K_u = HMAC_SHA256(K_c, "unit" || U16BE(u))[0:16]
        for δ in [-1, 0, 1]:
            for f in 1 .. F_MAX(u):
                if CodeMatches(K_u, campus_id, u, f, slot0+δ, code):
                    return ACCEPT   // 开门；不必记下 u / f
    return REJECT
```

语义：园区门只说明「像本园当前时间窗内的合法单元票」；真正绑单元、绑楼层靠后面的单元门与电梯。

---

## 8. 重码率

口令只有 **10000** 种。设备会在多个「（单元,）层 × 时间档」上试算，组合越多，**随便输一个 4 位数碰巧通过**的概率越高。称此为**重码率**（单次尝试的误放行率）。

| 设备 | 重码率（约） |
| ---- | ------------ |
| 单元门 | `F_MAX × 3 / 10000` |
| 电梯 | 试算同量级；须唯一层，略严 |
| 园区门 | `|U| × F_MAX × 3 / 10000` |

| 例子 | 约重码率 |
| ---- | -------- |
| 单元门，`F_MAX = 40` | 1.2% |
| 园区门，3 单元 × 20 层 | 1.8% |
| 园区门，10 单元 × 40 层 | 12% |

说明：

- `|U|`、`F_MAX` 越大，园区门越不宜用「不选单元」。
- 本方案不限制试错次数，可反复试。
- `F_MAX` 必须按真实楼层配置，才能把重码率压在可接受范围。

---

## 9. 校时（无漂移列）

```text
二维码 CSV：6,yyyyMMddHHmmss,签名10位
签名 = SHA1("6," + 时间14位 + "," + 设备密钥) 的十六进制前 10 位
```

验签通过后写 RTC。约每 3 个月现场校一次。

---

## 10. 配置对齐清单

平台与固件必须一致：

| 项 | 值 |
| -- | -- |
| `SLOT_SEC` | `1200` |
| `SKEW` | `1` |
| `ID_MAX` | `300` |
| `campus_id` / `unit_id` / `floor` | `u16` 大端 |
| `F_MAX` | 按单元实配，`≤ 300` |
| `msg` | 10 字节（无版本字段） |
| 派生盐 | ASCII `"unit"` |
| 口令 | HMAC-SHA1 + DynTrunc + `mod 10000` |
| 次数限制 | 关 |
| 园区门选单元 | 关 |
| 漂移字段 | 无 |

联调：固定 `(K_u, msg) → code` 向量，平台与 MCU 对照。

---

## 11. 一句话流程

```text
云用 (园区, 单元, 楼层, 时间) + K_u 算出 4 位码，约 1 小时有效

园区门：不选单元，穷举本园单元票 → 开门
单元门：本单元钥，搜层×三档 → 开门
电梯：  本单元钥，搜层×三档 → 唯一层则呼梯

离线验；无漂移字段；约 3 个月手校
```
