# 访客四位通行码方案：HH-TOTP-4-U（统一设备编号）

更新时间：2026-07-19

> **相对 HH-TOTP-4 的变更**：现场设备**一律按 `device_id` 编号**，算法与固件**不再按「园区门 / 单元门 / 电梯」种类分支**。  
> 业务路径不变：同一串 4 位码依次经过授权路径上的多台设备，最终到达指定楼层。  
> 简单易懂版：[password-4digit-flow-deviceuid-simple.md](./password-4digit-flow-deviceuid-simple.md)  
> 按种类区分的前一版：[password-4digit-design.md](./password-4digit-design.md)

**方案名称**：HH-TOTP-4-U（Unified device identity）

---

## 0. 与前一版的核心差别


|       | HH-TOTP-4（按种类）             | HH-TOTP-4-U（本方案）                              |
| ----- | -------------------------- | --------------------------------------------- |
| 设备身份  | `g_device_type` = 园门/楼门/电梯 | 每台一个 `**device_id`**，无种类枚举                    |
| 校验算法  | 三类设备三套叙述（实质相近）             | **全网同一套** `VerifyVisitorCode`                 |
| 成功后动作 | 写死在「门开 / 呼梯」类型里            | 看本机 **能力标志** `caps`，不看种类名                     |
| 密钥    | 仍 `K_c` / `K_u`            | 相同；下发时按 `device_id` 寻址装钥                      |
| 口令绑定  | 园+单元+层+时                   | **相同**（不把 `device_id` 编进 4 位票，否则一票无法过路径上多台设备） |


**不变**：4 位、离线验、时间槽、分层密钥、电梯侧解出唯一楼层、RateLimit、园级设备建议先选定单元。

---

## 1. 设计目标与威胁模型

### 1.1 目标

- 访客持**同一串** 4 位码，沿授权路径通过多台门禁设备，并到达指定楼层。
- 每台设备只凭：本机配置 + 本机密钥 + 本机时钟 + 收到的 4 位码（可另有 UI 选定的 `unit_id`）。
- 不依赖「这是不是电梯」这类种类字段做密码学分支；运维用 `device_id` 管理设备。

### 1.2 威胁


| 威胁       | 说明                    |
| -------- | --------------------- |
| T1 改码    | 改若干位仍想通过或呼错层          |
| T2 跨园区   | A 园票在 B 园设备通过         |
| T3 跨作用域  | 配错园/单元的设备、或他单元票在本机通过  |
| T4 同密钥串门 | 多设备误配同一 `K_u` / 误用万能钥 |
| T5 串层    | 票为 5 楼却呼到其它层          |
| T6 时钟漂移  | 生成与校验时差               |
| T7 盲猜    | 键盘穷举 0000–9999        |


### 1.3 非目标

同 HH-TOTP-4：不抗国家级分析；默认不强制单次核销。

---

## 2. 设备模型

每台门机/梯控在云与 EEPROM 中统一为：

```text
Device {
  device_id:   u32          // 全局唯一编号（运维主键）
  campus_id:   u32          // 本机所属园区
  unit_id:     u16          // 本机所属单元；园级入口设备用 UNIT_WILDCARD=0
  F_MAX:       u8           // 本机允许搜索/呼梯的最大层（无楼层能力可仍填，供搜索用）
  caps:        u8           // 能力位，见下
  key_id:      u8           // 密钥版本（可选）
  // 密钥材料：见 §4（不按种类存，按作用域存）
}
```

### 2.1 能力标志 `caps`


| 位    | 符号                 | 含义                                                     |
| ---- | ------------------ | ------------------------------------------------------ |
| bit0 | `CAP_OPEN`         | 校验成功可驱动开门继电器                                           |
| bit1 | `CAP_CALL_FLOOR`   | 校验成功须解出唯一 `floor` 并呼梯                                  |
| bit2 | `CAP_NEED_UNIT_UI` | 校验前必须从 UI/扫码得到 `selected_unit_id`（典型：园级入口，`unit_id=0`） |


一台设备可同时 `CAP_OPEN | CAP_CALL_FLOOR`（少见）；常见配置：


| 现场角色（仅运维称呼） | 建议配置                                               |
| ----------- | -------------------------------------------------- |
| 园区人行入口      | `unit_id=0`, `CAP_OPEN | CAP_NEED_UNIT_UI`，持 `K_c` |
| 单元入口        | `unit_id=N`, `CAP_OPEN`，持 `K_u(N)`                 |
| 电梯          | `unit_id=N`, `CAP_CALL_FLOOR`，持 `K_u(N)`           |


算法**只读 `caps` 与作用域**，不读「种类枚举」。

### 2.2 为何票面不绑 `device_id`

路径上有多台 `device_id`（如 1001→2003→3008）。若  
`code = HMAC(K, device_id || …)`，则一票无法在三台机器上同时成立。  
因此口令仍绑定**授权作用域**：`campus + unit + floor + slot`；`device_id` 用于装钥、审计、上报，不进入 4 位截断输入（除非做「单机票」变体，见 §12）。

---

## 3. 时间模型

与 HH-TOTP-4 相同：


| 参数         | 推荐          |
| ---------- | ----------- |
| `SLOT_SEC` | 600（10 min） |
| `SKEW`     | ±1 槽        |
| 时间         | Unix UTC    |
| 有效约        | 20～30 min   |


`slot = floor(unix_utc / SLOT_SEC)`。

---

## 4. 密钥与材料

```text
K_c = 园区根密钥（128-bit）
K_u = HMAC-SHA256(K_c, "unit" || U16BE(unit_id))[0:16]
```


| 设备作用域              | 预置密钥     | 说明                                    |
| ------------------ | -------- | ------------------------------------- |
| `unit_id == 0`（园级） | `K_c`    | 验票时用 UI 的 `selected_unit_id` 派生 `K_u` |
| `unit_id == N`     | `K_u(N)` | 不持有 `K_c`                             |


云侧按 `**device_id**` 下发对应密钥与配置（工装/密信道）。轮换用 `key_id` 写入 msg（可选，与前版一致）。

**禁止**：全小区共用一把钥；禁止「所有 device_id 共用同一 `K_u`」除非它们确属同一单元授权域。

口令算法：HMAC-SHA1 + RFC4226 DynTrunc + `mod 10000`（MCU 可行）。派生仍用 HMAC-SHA256。

---

## 5. 密码编码（与前版相同）

```text
msg  = U8(VER) || U32BE(campus_id) || U16BE(unit_id)
       || U8(floor) || U32BE(slot)
       [ || U8(key_id) ]   // 若启用轮换
code = DynTrunc31(HMAC-SHA1(K_u, msg)) % 10000   // 显示为 4 位十进制
```

不拆可见字段；不出现 device_type / device_id。

---

## 6. 平台生成算法

输入：`campus_id, unit_id, floor, t_gen, K_c`（及授权路径上的 `device_id` 列表仅作业务记录，**不参与** code 计算）。

```text
function GenerateVisitorCode(campus_id, unit_id, floor, t_gen, K_c):
    assert unit_id != 0 and 1 <= floor <= F_MAX
    K_u  = HMAC_SHA256(K_c, "unit" || U16BE(unit_id))[0:16]
    slot = floor(t_gen / SLOT_SEC)
    msg  = U8(VER) || U32BE(campus_id) || U16BE(unit_id)
           || U8(floor) || U32BE(slot)
    return format("%04d", DynTrunc31(HMAC_SHA1(K_u, msg)) % 10000)
```

云可同时记录：本票允许出现在哪些 `device_id` 上（审计/风控），固件离线验票**不依赖**该列表。

---

## 7. 统一校验算法（所有 device_id 共用）

```text
function ResolveWorkingKey(dev, selected_unit_id):
    if dev.unit_id != 0:
        return (OK, K_u_stored, dev.unit_id)      // 本机已是单元域
    // 园级入口
    if not (dev.caps & CAP_NEED_UNIT_UI):
        return (FAIL, —, —)                       // 配置错误
    if selected_unit_id == 0 or selected_unit_id not in allowed_units:
        return (FAIL, —, —)
    K_u = HMAC_SHA256(K_c, "unit" || U16BE(selected_unit_id))[0:16]
    return (OK, K_u, selected_unit_id)

function CodeMatches(K_u, campus_id, unit_id, floor, slot, code):
    msg = U8(VER) || U32BE(campus_id) || U16BE(unit_id)
          || U8(floor) || U32BE(slot)
    return (DynTrunc31(HMAC_SHA1(K_u, msg)) % 10000) == code

function VerifyVisitorCode(dev, code, now, selected_unit_id):
    if not RateLimitAllow(dev.device_id):
        return REJECT

    st, K_u, uid = ResolveWorkingKey(dev, selected_unit_id)
    if st != OK:
        RateLimitFail(); return REJECT

    slot0 = floor(now / SLOT_SEC)
    hits = []   // list of floor

    for δ in [-SKEW .. +SKEW]:
        for f in 1 .. dev.F_MAX:
            if CodeMatches(K_u, dev.campus_id, uid, f, slot0+δ, code):
                hits.append(f)

    hits = unique(hits)

    if (dev.caps & CAP_CALL_FLOOR):
        if len(hits) != 1:
            RateLimitFail(); return REJECT
        RateLimitReset()
        CallFloor(hits[0])
        if (dev.caps & CAP_OPEN): OpenDoor()    // 若兼有
        return ACCEPT

    // 仅开门：任一楼层命中即可（证明本单元本时间窗合法访客）
    if len(hits) >= 1:
        RateLimitReset()
        if (dev.caps & CAP_OPEN): OpenDoor()
        return ACCEPT

    RateLimitFail()
    return REJECT
```

**要点**：`device_id` 为 1001 还是 3008，走的是**同一函数**；差别只来自 `campus_id/unit_id/caps/F_MAX/密钥`。

---

## 8. 时差与跨槽

同前版：设备用 `slot0+δ`，`δ∈[-SKEW,+SKEW]`；超时须重新出票。

---

## 9. 安全性分析（相对「种类方案」）


| 攻击    | 结论                                             |
| ----- | ---------------------------------------------- |
| 改码    | 整票截断，改位≈重猜；叠 RateLimit                         |
| 跨园区   | `campus_id` + `K_c` 不同                         |
| 「错种类」 | 种类已取消；错作用域（他单元 `K_u`）仍失败                       |
| 同钥串门  | 按单元装 `K_u`；`device_id` 只寻址，不共享钥除非同单元           |
| 串层    | 仅 `CAP_CALL_FLOOR` 设备在唯一命中时呼层；纯 `CAP_OPEN` 不呼层 |
| 盲猜    | 必须 RateLimit                                   |


**权衡（与前版相同）**：4 位无法完美硬隔离一切；园级设备建议 `CAP_NEED_UNIT_UI`；开门设备不强制锁层，锁层靠带 `CAP_CALL_FLOOR` 的设备。

---

## 10. 与旧固件「按 type 拆位」及 HH-TOTP-4 的对比


|        | 旧固件 type=1/2/3 拆位 | HH-TOTP-4    | HH-TOTP-4-U                  |
| ------ | ----------------- | ------------ | ---------------------------- |
| 身份     | 种类枚举              | 种类叙述 + 作用域密钥 | **仅 device_id + 作用域 + caps** |
| 楼层     | 明文段，电梯可不验         | HMAC 绑定      | **同左**                       |
| 校验代码路径 | 三分支拆位             | 三节伪代码        | **单函数**                      |
| 运维     | 改类型易改行为           | 改类型文档        | 改 `caps`/作用域，编号不变则逻辑清晰       |


---

## 11. 平台与固件必须一致的配置项


| 项                           | 推荐                            |
| --------------------------- | ----------------------------- |
| `VER`                       | `0x01`                        |
| `SLOT_SEC` / `SKEW`         | 600 / 1                       |
| msg 字段与端序                   | 同 §5，大端                       |
| `UNIT_WILDCARD`             | `0`                           |
| `caps` 位定义                  | 同 §2.1                        |
| HMAC / DynTrunc / mod 10000 | 同前版                           |
| `K_u` 派生盐                   | ASCII `"unit"`                |
| RateLimit                   | 每 `device_id` 独立计数，必须启用       |
| 上报                          | 带 `device_id`（及命中 `floor` 若有） |


---

## 12. 可选变体


| 变体        | 做法                                  | 代价               |
| --------- | ----------------------------------- | ---------------- |
| V1 园级不选单元 | 穷举本园所有 `unit`                       | FAR 高，仅极小园区      |
| V2 单机票    | `msg` 含 `device_id`，一票一机            | **不能**一票走完全路径    |
| V3 路径组密钥  | `K_path=HMAC(MK, sort(device_ids))` | 路径变更要重装钥；与单元模型重复 |
| V4 更长位数   | 5～6 位                               | 产品改输入；安全性价比高     |


**默认推荐**：HH-TOTP-4-U 主方案（作用域密钥 + 统一 Verify + caps），不采用 V2 作主路径票。

---

## 13. 小结

```text
云: 按目的地 (campus, unit, floor, time) 用 K_u 出 4 位码
    按 device_id 向各机下发：作用域 + caps + K_c 或 K_u

任意设备:
    VerifyVisitorCode(本机配置, code, now [, selected_unit])
        → OPEN 和/或 CallFloor(唯一层)
```

业务上仍可称呼「园门 / 单元门 / 电梯」，但那只是**部署模板**（预置不同 `caps` 与 `unit_id`），不是算法分支条件。