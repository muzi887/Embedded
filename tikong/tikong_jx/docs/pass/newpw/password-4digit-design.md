# 访客四位通行码方案：HH-TOTP-4

更新时间：2026-07-19

> 设计 Prompt：[password-4digit-design-prompt.md](./password-4digit-design-prompt.md)  
> 简单易懂（串流程）：[password-4digit-flow-simple.md](./password-4digit-flow-simple.md)  
> **落地剖面（选单元+RateLimit，软硬件对照）**：[password-4digit-eng-unit-ratelimit.md](./password-4digit-eng-unit-ratelimit.md)  
> 统一设备编号版（无种类分支）：[password-4digit-design-deviceuid.md](./password-4digit-design-deviceuid.md) · [流程简版](./password-4digit-flow-deviceuid-simple.md)  
> 背景问题：[password-4digit-unacceptable.md](../password-4digit-unacceptable.md)、[password-4digit-remediation.md](../password-4digit-remediation.md)

一套可落地的**分层密钥 + 截断 HMAC 口令**方案。核心判断：**4 位空间无法同时对「园区×单元×楼层×时间」做密码学级硬绑定**；正确做法是分层密钥隔离越权，口令只做短时效认证，设备按角色做有界搜索/校验，并用尝试次数限制压低猜解。

**推荐方案名称**：HH-TOTP-4（Hierarchical-HMAC Truncated OTP，4 位）

---

## 1. 设计目标与威胁模型

### 1.1 目标

- 同一串 4 位码，依次过：园区门 → 单元门 → 电梯（仅呼授权层）。
- 设备离线自洽：只用本机密钥 + 本机时钟 + 收到的 4 位码。
- 防串园区 / 串单元 / 串楼层；防改码位撞库式误开；密钥按园区/单元隔离。

### 1.2 威胁（本方案覆盖）


| 威胁        | 说明                     |
| --------- | ---------------------- |
| T1 改码     | 改 1～2 位仍想开门/呼错层        |
| T2 跨园区    | A 园区码在 B 园区门使用         |
| T3 错门类型   | 单元/电梯材料在错误角色上误开        |
| T4 同密钥串门  | 多设备共用一把密钥导致跨楼互开        |
| T5 同单元不同层 | 访 1801 的码呼到 1802 / 其它层 |
| T6 时钟漂移   | 生成端与现场差几分钟导致误拒/误接受窗过大  |
| T7 盲猜     | 在门禁键盘上试 0000–9999      |


### 1.3 非目标

- 抗国家级离线破解。
- 抗有线窃听后的长期伪造成批（应用层另议）。
- 单码单次核销（可做增强，非默认）。

---

## 2. 时间模型


| 参数   | 符号         | 推荐值               | 含义                                  |
| ---- | ---------- | ----------------- | ----------------------------------- |
| 槽宽   | `SLOT_SEC` | **600 s（10 min）** | `slot = floor(unix_utc / SLOT_SEC)` |
| 容差   | `SKEW`     | **±1 槽**          | 校验尝试 `slot-1, slot, slot+1`         |
| 有效寿命 | —          | 约 **20～30 min**   | 相对生成时刻，跨最多 3 个槽                     |
| 时区   | —          | **一律 UTC**        | 平台与设备只存 Unix 秒                      |


**失效条件（满足任一即拒）**

1. 对 `δ ∈ [-SKEW, +SKEW]`，用本机 `now` 算出的 `slot+δ` 均无法使 HMAC 截断匹配；
2. 本机配置的 `MAX_SKEW_ABS_SEC`（如 1800）以外的墙钟异常可告警（可选）；
3. 可选：本地「已使用码」缓存命中且策略为单次核销（默认关闭）。

**选型理由**：10 分钟槽在「访客从到门口走到电梯」与「旧码尽快作废」之间折中；±1 槽覆盖常见 RTC 漂移与跨槽边界。

---

## 3. 密码编码格式

**不采用「可见字段拆分」**（如高 2 位=楼层、低 2 位=校验）。4 位全空间用于：

```text
code ∈ {0000…9999}
code = TruncDec4( HMAC(K_u, msg) )
```

**消息绑定（必须字节级一致）**

```text
msg = ver || campus_id || unit_id || floor || slot
```


| 字段          | 类型       | 说明                     |
| ----------- | -------- | ---------------------- |
| `ver`       | `u8`     | 协议版本，推荐 `0x01`         |
| `campus_id` | `u32` BE | 园区号                    |
| `unit_id`   | `u16` BE | 单元号                    |
| `floor`     | `u8`     | 目标楼层 1…`F_MAX`（如 1…60） |
| `slot`      | `u32` BE | 时间槽                    |


**截断（与 HOTP 同类，便于 MCU）**

```text
TruncDec4(h) = ( DynTrunc(HMAC-SHA1) ) mod 10000
```

`DynTrunc`：取 SHA1 摘要按 RFC 4226 动态截断得 31-bit 整数，再 `mod 10000`，十进制零填充为 4 位。

**为何不把园区/单元明文塞进 4 位**：明文可改；真隔离靠**不同 `K_u`**，不靠可见数字段。

---

## 4. 密钥与材料

### 4.1 层级

```text
K_c   = 园区根密钥（128 bit 随机，云端生成）
K_u   = HMAC-SHA256(K_c, "unit" || unit_id)[0:16]   // 16 字节工作密钥
```


| 角色      | 预置秘密              | 本机身份                        | 不持有              |
| ------- | ----------------- | --------------------------- | ---------------- |
| 云 / App | 全部 `K_c`（或 HSM）   | —                           | —                |
| 园区门机    | **仅本园 `K_c`**     | `campus_id`，单元列表            | 其它园 `K_c`        |
| 单元门机    | **仅本单元 `K_u`**    | `campus_id, unit_id`        | `K_c`、其它单元 `K_u` |
| 电梯控     | **与本单元门相同 `K_u`** | `campus_id, unit_id, F_MAX` | 其它单元密钥           |


电梯与单元门同 `K_u`：同一授权路径；**楼层约束在校验消息的 `floor` 字段 + 呼梯执行**，不靠另一把「层密钥」。

### 4.2 下发与轮换

- 出厂/开通：云按园区下发 `K_c`→门机；按单元派生 `K_u`→单元门与电梯（密信道 / 工装）。
- 轮换：`key_id`（`u8`）写入设备；`msg` 增补 `key_id`；云双窗口签发（旧+新各有效 N 天）；设备先试当前 `key_id` 再试上一版。
- **禁止**：全小区共用一把 `K`；禁止「所有电梯同一密钥」。

### 4.3 算法选型


| 算法          | 用途           | 理由                                 |
| ----------- | ------------ | ---------------------------------- |
| HMAC-SHA1   | 口令截断         | MCU（如 F103）软件实现成熟、耗时可控；与 HOTP 生态一致 |
| HMAC-SHA256 | 仅用于 `K_u` 派生 | 派生次数少；口令路径仍用 SHA1 截断即可             |


不强制 AES：无加密载荷，只需完整性 MAC。

---

## 5. 平台生成算法

**输入**：`campus_id, unit_id, floor, t_gen`（Unix），以及该园 `K_c` / 直接 `K_u`  
**输出**：4 位十进制字符串

```text
function GenerateVisitorCode(campus_id, unit_id, floor, t_gen, K_c):
    assert 1 <= floor <= F_MAX
    K_u  = HMAC_SHA256(K_c, "unit" || U16BE(unit_id))[0:16]
    slot = floor(t_gen / SLOT_SEC)
    msg  = U8(VER) || U32BE(campus_id) || U16BE(unit_id)
           || U8(floor) || U32BE(slot)
    h    = HMAC_SHA1(K_u, msg)
    code = DynTrunc31(h) % 10000
    return format("%04d", code)
```

平台可同时展示：建议有效至 `(slot + SKEW + 1) * SLOT_SEC`（对访客友好的「约 20～30 分钟内有效」）。

---

## 6. 三类设备校验算法

公共例程：

```text
function CodeMatches(K_u, campus_id, unit_id, floor, slot, code):
    msg = U8(VER) || U32BE(campus_id) || U16BE(unit_id)
          || U8(floor) || U32BE(slot)
    return (DynTrunc31(HMAC_SHA1(K_u, msg)) % 10000) == code

function RateLimitAllow(device):  // 例：5 次 / 300s，失败计数
    ...
```

### 6.1 单元门机（本机已知 `campus_id, unit_id, K_u`）

单元门目标：确认「本单元、当前时间窗内的合法访客」。  
**默认不强制访客已选楼层**（进单元后由电梯约束层）。若门口 UI 已选楼层，则只验该层（更严）。

```text
function VerifyUnitDoor(code, now):
    if not RateLimitAllow(): return REJECT
    slot0 = floor(now / SLOT_SEC)
    for δ in [-SKEW .. +SKEW]:
        slot = slot0 + δ
        for f in 1 .. F_MAX:                    // 或仅 f = selected_floor
            if CodeMatches(K_u, campus_id, unit_id, f, slot, code):
                RateLimitReset()
                return ACCEPT                 // 开门；不必记录 f
    RateLimitFail()
    return REJECT
```

**误接受率（单次尝试）** ≈ `F_MAX × (2·SKEW+1) / 10000`  
例：`F_MAX=40, SKEW=1` → `120/10000 = 1.2%`（再叠加 RateLimit）。

### 6.2 电梯控制器（本机已知同上 + 执行呼梯）

必须解出唯一授权层；多匹配则拒绝（防歧义）。

```text
function VerifyElevator(code, now):
    if not RateLimitAllow(): return REJECT
    slot0 = floor(now / SLOT_SEC)
    hits = []
    for δ in [-SKEW .. +SKEW]:
        for f in 1 .. F_MAX:
            if CodeMatches(K_u, campus_id, unit_id, f, slot0+δ, code):
                hits.append(f)
    hits = unique(hits)
    if len(hits) == 1:
        RateLimitReset()
        CallFloor(hits[0])
        return ACCEPT
    RateLimitFail()
    return REJECT
```

**楼层隔离**：授权写在 `msg.floor` 里；呼错层必须让另一 `floor` 的 HMAC 碰巧相等，概率同上量级；改码位等价于重猜，见 §8。

### 6.3 园区门机（本机已知 `campus_id, K_c`，单元列表 `U[]`）

园区门**不知道**访客要去的单元/楼层。4 位下若对「全部单元 × 全部楼层 × 3 槽」穷举，误开率会到两位数百分比，**不可接受**。

**推荐（可落地）——园区门需选定单元（或扫楼栋二维码带 unit_id）**：

```text
function VerifyCampusGate(code, now, selected_unit_id):
    if selected_unit_id not in U: return REJECT
    if not RateLimitAllow(): return REJECT
    K_u = HMAC_SHA256(K_c, "unit" || U16BE(selected_unit_id))[0:16]
    // 以下与单元门相同：对 f、δ 搜索
    ... // CodeMatches(K_u, campus_id, selected_unit_id, f, slot, code)
```

这样园区门与单元门搜索空间同阶（~1% 量级误匹配，靠 RateLimit）。

**若产品坚持「只输 4 位、不选单元」**：见 §12 变体 V1（弱园区绑定）——只保证「像本园时段访客」，**不能**在园区门密码学保证单元；单元/电梯仍强约束。需在需求上明确接受。

---

## 7. 时差与跨槽处理

```text
校验端 now_dev
生成端 t_gen → slot_g = floor(t_gen / SLOT_SEC)

正常接受当且仅当 ∃ δ∈[-SKEW,+SKEW], ∃ 合法绑定:
    CodeMatches(..., slot = slot_g 的邻近)
即设备用 slot0+δ 重算，覆盖：
  - 设备慢/快不到约一个槽
  - 访客在槽边界附近使用
```


| 场景             | 行为                     |
| -------------- | ---------------------- |
| 差 < ~10 min    | ±1 槽覆盖                 |
| 差 > ~20–30 min | 全部 δ 失败 → 失效，须重新生成     |
| 生成后立刻用         | `slot` 与设备当前槽一致或差 1，通过 |
| 禁止「向未来扩很多槽」    | `SKEW` 固定为 1，防止旧码长期有效  |


设备 RTC 与平台 NTP 应对齐；门机可在联网时校时，离线仍用本地钟 + 容差。

---

## 8. 安全性分析

### 8.1 攻击是否成立


| 攻击             | 结论              | 机理                                                                   |
| -------------- | --------------- | -------------------------------------------------------------------- |
| 改动码中某一段仍开门/错层  | **基本不成立**       | 无明文段；改 1 位 ≈ 随机新码，需再命中某 `(f,δ)`；单次≈1.2%，连续试触发 RateLimit              |
| A 园码开 B 园门     | **不成立**         | B 无 `K_c^A`；`campus_id` 不同 → msg 不同；密钥隔离                             |
| 单元/电梯材料在错门类型误开 | **不成立**         | 园区门若无对应 `K_u`/`K_c` 派生链则失败；他园单元 `K_u` 不同；角色上电梯多「呼层」语义，门机只给 DOOR_OPEN |
| 多设备同密钥跨楼互开     | **不成立（若按层级部署）** | 每单元独立 `K_u`；禁止小区万能钥；同单元多梯共享 `K_u` 可接受（同授权域）                          |
| 同园同单元不同楼层串层    | **电梯侧成立约束**     | `floor` 进 MAC；呼梯只接受唯一命中层。单元门默认「本单元任一授权层访客可进单元」——与物理动线一致              |


### 8.2 4 位空间：碰撞与猜测

- 全空间：`10^4`。
- 在已知密钥下，合法码对给定 `(campus,unit,floor,slot)` 唯一确定；不同楼层/槽截断碰撞概率约 `1/10000`。
- **在线猜解**：无 RateLimit 时，期望约 `10000/(F_MAX×3)` 次可碰到「某层某槽」——故 **必须 RateLimit**（如 5 次/5 分钟 + 锁定）。
- **同单元多住户**：隔离靠 `floor`（及业务侧不同订单）；不是靠每人一把设备密钥。4 位无法为「每户长期唯一口令」；本方案是**短时访客票**，不是住户固定密码。

### 8.3 权衡（必须写清）


| 维度      | 完美隔离？             | 本方案                 |
| ------- | ----------------- | ------------------- |
| 跨园区     | 是                 | `K_c` / `campus_id` |
| 跨单元     | 是                 | 独立 `K_u`            |
| 跨楼层     | 电梯：强；单元门：弱（进单元即可） | 推荐折中                |
| 园区门不选单元 | 否（4 位不够）          | 推荐 UI 选单元；或变体弱校验    |
| 抗盲猜     | 否（仅 1e4）          | RateLimit + 短时效     |


---

## 9. 与「仅限层位图兜底、口令不绑楼层」旧方案对比


|      | 旧：口令∉楼层，位图另控         | 本方案 HH-TOTP-4         |
| ---- | -------------------- | --------------------- |
| 口令与层 | 口令对任意层可能相同，层靠配置位图    | `floor` 进入 HMAC，错层须碰撞 |
| 时效   | 常为静态或粗粒度             | 槽 + ±SKEW，过期作废        |
| 跨单元  | 易因共享密钥/共享口令串门        | `K_u` 按单元派生           |
| 改码   | 若口令与校验分离，改码可能仍碰有效集合  | 均匀随机，改码≈重猜            |
| 电梯   | 依赖「先认证再应用位图」，位图配错即越权 | 认证成功同时给出唯一 `floor`    |


本方案更好之处：**授权语义集中在一票（园区+单元+层+时）**，设备离线可验；旧方案把「是谁的票」和「能去哪层」拆开，配置漂移时更容易静默越权。

---

## 10. 实现注意：平台与固件必须一致的配置项


| 配置项                          | 推荐                                         |
| ---------------------------- | ------------------------------------------ |
| `VER`                        | `0x01`                                     |
| `SLOT_SEC`                   | `600`                                      |
| `SKEW`                       | `1`                                        |
| `F_MAX`                      | 按单元配置，两端相同                                 |
| 字节序                          | 全部整数 **大端** 入 msg                          |
| 字符串盐                         | 派生用字面量 `"unit"`（编码 ASCII）                  |
| HMAC 口令                      | HMAC-SHA1 + RFC4226 DynTrunc + `mod 10000` |
| 派生                           | HMAC-SHA256，取前 16 字节为 `K_u`                |
| 时间                           | Unix UTC 秒；禁止本地时区参与 `slot`                 |
| `campus_id` / `unit_id` 类型宽度 | 与上表 `u32`/`u16` 一致                         |
| RateLimit                    | 两端策略可不同，但现场必须启用                            |
| `key_id`（若轮换）                | 写入 msg 位置与长度一致                             |


联调建议：固定向量 `(K_u, msg) → code` 写入测试用例，平台与 MCU 对照。

---

## 11. 推荐方案小结

```text
云: K_c --HMAC--> K_u
    code = Trunc4( HMAC-SHA1(K_u, ver|campus|unit|floor|slot) )

园区门: 选 unit → 派生 K_u → 搜 (floor × δ) → 开门
单元门: 持 K_u → 搜 (floor × δ) → 开门
电梯:   持 K_u → 搜 (floor × δ) → 唯一命中则 CallFloor(f)
```

**默认落地选择**：HH-TOTP-4 + 10 分钟槽 + ±1 容差 + 单元级 `K_u` + 电梯唯一楼层命中 + 园区门选单元 + RateLimit。

---

## 12. 可选变体与代价

### V1 — 园区门「只输 4 位、不选单元」

- 做法：园区门用 `HMAC(K_c, ver|campus|slot) % 10000` 与访客码比对（**消息不含 unit/floor**），同时平台生成两码或把园区因子编进同一截断（难，易冲突）。
- 更干净的弱方案：园区门接受「任意本园单元票」——对所有 `unit∈U` 派生 `K_u` 做搜索；仅当 `|U|×F_MAX×3 ≪ 10000`（如 3 单元×20 层）才可考虑，否则 FAR 过高。
- **代价**：园区门越权面变大，或仅适超小园区。

### V2 — 明楼层 + 2 位 MAC（`code = (floor-1)%100*100 + auth`）

- `auth = HMAC(...) % 100`。
- **代价**：楼层从码面可读可改；改层后盲猜 auth 成功率为 1/100；电梯实现更简单，安全性弱于纯截断。

### V3 — 单次核销

- 设备将接受过的 `code|slot` 写入短 FIFO。
- **代价**：同伴无法共用一张码；断电丢失策略要定义。

### V4 — 升到 5～6 位

- 搜索 FAR 与猜解难度明显下降；若产品可改输入长度，这是安全性价比最高的演进。参见：[五位密码编码方案_10分钟单位.md](../五位密码编码方案_10分钟单位.md)。

