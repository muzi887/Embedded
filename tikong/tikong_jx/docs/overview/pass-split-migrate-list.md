# Pass 域拆分 — 迁出了什么、变成了什么

更新时间：2026-07-20

> **对齐**：[optimization-directions.md](./optimization-directions.md) §P0  
> **状态**：P0 Pass 主体拆分 **已落地**；下文「进度」以当前树为准。  
> **原则**：`app_poll` 仍只调 `QRProcessUart4/5`；拆的是文件内部职责，不是多挂入口。

---

## 1. 当前文件树（已拆完主体）

```text
App/Pass/
  qr_comm.c / .h           ~412 行  读头轮询 + 组帧 + type 分发
  pass_password.c / .h     ~231 行  四位口令
  cmd.c / .h               ~161 行  CommControl 薄门面 + 黑名单命令残留
  pass_permission.c / .h   ~529 行  院/楼/梯权限链
  pass_setting.c / .h      ~548 行  设置 / 校时 / 限层 / AT 后写 EEPROM
  pass_crypto.c / .h       ~249 行  验签 + 时间窗等
  data_handler.c / .h      ~700 行  g_device_*、凭证、黑名单包、组帧杂项

Hardware/Time/
  rtc_util.c / .h          ~109 行  ds1302_shift_minutes、create_ymdhm
  RTC.c / ds1302.c         芯片驱动（未变）
```

### 1.1 调用关系（当前）

```text
app_poll
  → QRProcessUart4/5          （qr_comm.c：组帧 + switch(type)）
       → type '2'  → qr_handle_password_input   （pass_password.c）
       → type 其它 → CommControl                （cmd.c 薄）
                        → '1'/'6'/'7' → pass_setting.c
                        → '2'         → pass_permission.c
```

`app_poll` **未改调用列表**；变的是函数所在 `.c`。

---

## 2. 对照表：迁出了什么 → 变成了什么

### 2.1 从 `qr_comm.c` 出去

| 迁出了什么（原） | 变成了什么（目标） | 进度 |
|------------------|--------------------|------|
| `qr_handle_password_input` | `pass_password.c`（导出，非 static） | **已完成** |
| `qr_build_password_auth_digest` | `pass_password.c` 内 static | **已完成** |
| `ds1302_shift_minutes` | `Hardware/Time/rtc_util.c`；`qr_comm.h` 已不再导出 | **已完成** |
| `ds1302_days_in_month` | `rtc_util.c` 内 static | **已完成** |
| `create_ymdhm` | `rtc_util.c` / `rtc_util.h`（对外非 static） | **已完成** |
| `uart4_find_sub` + `uart5_find_sub` | **仍在** `qr_comm.c`（双份） | **待合并**（可选） |

| 留下什么 | 说明 |
|----------|------|
| `QRProcessUart4` / `QRProcessUart5` | 仍在 `qr_comm`，变薄 |
| UART 缓冲、`order_type_*`、`card_Number_*` | 读头通道私有 |

---

### 2.2 从 `cmd.c` 出去

**A. → `pass_permission.c`**

| 迁出了什么 | 变成了什么 | 进度 |
|------------|------------|------|
| `Cmd_Permission` | 导出（`cmd.c` 调用） | **已完成** |
| `Cmd_Permission_VerifySignature` / `CheckBlacklist` / `CheckTimeWindow` | 文件内 static | **已完成** |
| `Cmd_Permission_ProcessGate` / `Unit` / `Elevator` | 文件内 static | **已完成** |

**B. → `pass_setting.c`**

| 迁出了什么 | 变成了什么 | 进度 |
|------------|------------|------|
| `Cmd_Setting` / `Cmd_Set_Time` / `Cmd_Set_OpenLimit` | 导出 | **已完成** |
| `Cmd_Setting_OnAtSequenceDone` / `Abort` | 导出（`main` 调用） | **已完成** |
| `Cmd_Setting_Snapshot*` / `WriteKeys*` | 文件内 static | **已完成** |
| `copy_field_snap` / `qr_hex_*` / `qr_split_*` / `parseYMDHMS` | setting 侧（`parseYMDHMS` 导出） | **已完成** |
| `g_mqtt_*` / `g_floors_limit_time` | 定义在 `pass_setting.c`；头文件 `extern` | **已完成** |

**C. `cmd.c` 现在留下什么**

| 留下 | 说明 |
|------|------|
| `CommControl` | 薄：按 `cmd_ch` 调 setting / permission |
| `BlackUserListAdd` / `Del` / `Clear` | 仍在 `cmd.c`；与权限/设置拆分正交，可后迁 `App/Store` |
| `AllSetting` | 仅 `cmd.h` 声明、无实现 |

---

### 2.3 从 `data_handler.c` 出去 → `pass_crypto.c`

| 迁出了什么 | 变成了什么 | 进度 |
|------------|------------|------|
| `VerifySignature` | `pass_crypto.c` | **已完成** |
| `toLowerHex`、`hex2val` | 同上（`hex2val` 已导出，供 `data_handler` 楼层 hex 用） | **已完成** |
| `CheckValidPeriod_WithNow` | 同上 | **已完成** |
| `adjust_begin_time` / `Get_newTime` / `MakeTimestamp14` | 同上 | **已完成** |

| 留下（`data_handler`） | 说明 |
|------------------------|------|
| `g_device_*`、`rly_*_time` | 定义在 `data_handler.c` |
| `ParseDeviceModeRlyTimes*` | 模式解析 |
| `VerifyDeviceCredentials` / `SetDevicePublicKey` / … | 凭证 |
| `AddBlacklistIds_FromPacket` / `Del*` | 黑名单包 |
| `build_frame_A*`、楼层 hex、`BuildCvjJson*` | 组帧杂项 |

**清理债**：`data_handler.h` 仍可能残留已迁到 `pass_crypto.h` 的声明（如 `VerifySignature`）——应以 `pass_crypto.h` 为准，头文件去重。

---

## 3. 文件职责一句话（当前）

| 文件 | 职责 | 典型对外 |
|------|------|----------|
| `qr_comm.c` | 读头帧 → type 分发 | `QRProcessUart4/5` |
| `pass_password.c` | 四位口令 | `qr_handle_password_input` |
| `cmd.c` | CSV 命令总开关 + 黑名单残留 | `CommControl` |
| `pass_permission.c` | cmd `'2'` 权限链 | `Cmd_Permission` |
| `pass_setting.c` | cmd `'1'/'6'/'7'`、AT 后写 EEPROM | `Cmd_Setting*`、`Cmd_Set_*`、`parseYMDHMS` |
| `pass_crypto.c` | 验签 / 时间窗 | `VerifySignature`、`MakeTimestamp14`… |
| `rtc_util.c` | 时间算术 / YMDHM | `ds1302_shift_minutes`、`create_ymdhm` |
| `data_handler.c` | 设备全局 + 凭证 + 组帧 | `g_device_*`、凭证 / 黑名单包 API |

`g_result`：定义在 `Live_data.c`，`Live_data.h` 中 `extern`（勿在 Pass 多文件再定义）。

---

## 4. 名字与 `static` 约定（落地经验）

| 规则 | 说明 |
|------|------|
| 对外入口 | **非** `static`，在 `.h` 声明 |
| 仅本文件帮手 | **`static`**，**不要**写进 `.h` |
| 头文件禁止 | 对跨文件 API 写 `static`（曾导致 `create_ymdhm` 链接错误） |
| 口令对外改名 | `Pass_HandlePassword` **可选**，非必须；现名 `qr_handle_password_input` 可用 |

---

## 5. 勾选进度

1. ~~口令出 `qr_comm`~~ → **完成**  
2. ~~时间工具 → `rtc_util`~~ → **完成**  
3. ~~`cmd` → setting + permission~~ → **完成**（`cmd.c` ~161 行）  
4. ~~验签/时间窗 → `pass_crypto`~~ → **完成**  
5. `uart4/5_find_sub` 合并 → **待做（可选）**  
6. `data_handler.h` 与 `pass_crypto.h` 声明去重 → **待做**  
7. 黑名单命令迁 `App/Store` → **未做（正交）**  
8. `pass_permission` / `pass_setting` 各有一份 CSV `s_recv_csv_*`（可接受）；无用的 `recv_*` 宜注释或删除 → **按需**

---

## 6. 和 optimization-directions 的对应

| 原文建议 | 现状 |
|----------|------|
| qr_comm 只留 QRProcess* | **已做到** |
| pass_password | **已有** |
| pass_permission / pass_setting | **已有** |
| pass_crypto | **已有** |
| Time 接 shift / ymdhm | **`rtc_util` 已有** |
| Cloud 域 Live_data 拆分 | **未开始**（下一优先级） |
