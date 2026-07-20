# CommControl 调用条件与权限调用链

更新时间：2026-07-18

> **源码**：`App/Pass/qr_comm.c`（调用方）、`App/Pass/cmd.c`（`CommControl` / `Cmd_Permission` / `Process*`）  
> **相关**：[qr-process-uart45.md](./qr-process-uart45.md)（读头如何进到本链）

本文整理：何时调用 `CommControl`，以及它与 `Cmd_Permission` → `Cmd_Permission_ProcessElevator` 的关系。

---

## 一、总览

```text
读头 UART4/5
    │ DMA + IDLE → RX_Complete
    ▼
QRProcessUart4 / QRProcessUart5
    │ 凑齐 "{...}"，切出 type / data / uid
    │ type 为 '0' 或 '1'（NFC / 二维码）
    ▼
CommControl(data, order_type, uid, uart_port)     ← 读头命令总入口
    │ 仅当 data[0] == '2'（权限 CSV）
    ▼
Cmd_Permission(...)                              ← 权限专用：拆 9 列 CSV，按设备类型分支
    │ g_device_type 含 '1' → ProcessGate（院门禁）
    │ 含 '2'               → ProcessUnit（楼门禁）
    │ 否则                 → ProcessElevator（梯控）
    ▼
Cmd_Permission_ProcessElevator(...)              ← 梯控权限实现
    （验签 → 黑名单 → 时间窗 → 楼层/RS485 等）
```

要点：**`CommControl` 不是只做权限**；**`ProcessElevator` 也不是每次通行都会走到**。

---

## 二、调用 `CommControl(...)` 的条件

工程内 **仅** 在 `QRProcessUart4` / `QRProcessUart5` 中调用（见 `qr_comm.c`）。

### 2.1 前置（能进到解析）

| # | 条件 |
| --- | --- |
| 1 | 对应口 `UART4_RX_Complete` / `UART5_RX_Complete` 为真 |
| 2 | 累积缓冲已是完整帧：以 `{` 开头、以 `}` 结尾 |

### 2.2 直接调用条件（源码 `if`）

| # | 条件 |
| --- | --- |
| 3 | `type` 切片非空 **且** `data` 切片非空 |
| 4 | `type` 长度恰好为 **1**，且字符为 `'0'`（NFC）或 `'1'`（二维码） |

此时调用：

```c
CommControl(s_received_uart*, order_type_uart*, card_Number_uart*, 4或5);
```

### 2.3 不调用 `CommControl` 的常见情况

| 情况 | 实际走向 |
| --- | --- |
| JSON `type` 为 `'2'`（密码） | `qr_handle_password_input(...)`，见 [password-4digit-auth.md](./password-4digit-auth.md) |
| 未凑齐 `{...}` 完整帧 | 只清驱动 `RX_Complete`，等下次拼包 |
| `type` / `data` 切片失败（空） | 不进业务分发 |

---

## 三、`CommControl` 与权限链的关系

它们是 **一层包一层**，不是并列关系。

| 函数 | 角色 |
| --- | --- |
| **`CommControl`** | 读头命令总入口：看 CSV **`data[0]`** 分命令（设置 / 权限 / 校时 / 限层…） |
| **`Cmd_Permission`** | 权限专用：要求 **9 列** CSV；再按 **`g_device_type`** 选 Gate / Unit / Elevator |
| **`Cmd_Permission_ProcessElevator`** | **梯控**那一支的具体实现（与 Gate、Unit 是兄弟函数） |

### 3.1 `CommControl` 内按首字符分支（摘要）

| `data[0]` | 含义 | 下一步 |
| --- | --- | --- |
| `'1'` | 设备参数设置 | `Cmd_Setting` |
| `'2'` | 权限访问 | **`Cmd_Permission`** |
| `'3'` | （读头侧黑名单占位） | 当前直接返回 `-1` |
| `'6'` | 设置时间及漂移 | `Cmd_Set_Time` |
| `'7'` | 常开 / 限层 | `Cmd_Set_OpenLimit` |

只有 **`data[0] == '2'`** 才会进入 `Cmd_Permission`。

### 3.2 `Cmd_Permission` 内按设备类型分支

先把 `data` 按逗号拆成 9 列（不足则返回 `-401`），再：

| `g_device_type` | 分支 |
| --- | --- |
| 字符串中含字符 `'1'` | `Cmd_Permission_ProcessGate`（院门禁） |
| 否则，含 `'2'` | `Cmd_Permission_ProcessUnit`（楼门禁） |
| 否则 | **`Cmd_Permission_ProcessElevator`（梯控）** |

处理完后，按 JSON 的 `type`（`order_type_meta[0]`）回复读头：`'1'` → `qr_Reply`，`'0'` → `card_Reply`。

### 3.3 `ProcessElevator` 内部顺序（摘要）

1. `Cmd_Permission_VerifySignature` — 验签  
2. `Cmd_Permission_CheckBlacklist` — 黑名单  
3. `Cmd_Permission_CheckTimeWindow` — 有效期时间窗  
4. 解析电梯楼层权限字段，执行本地动作 / RS485 等（失败时 `g_result.code == 400` 提前返回）

---

## 四、何时才会走到 `ProcessElevator`？

须 **同时** 满足：

1. 读头 JSON 的 `type` 为 `'0'` 或 `'1'` → 才会调 `CommControl`  
2. `data` 首字符为 `'2'` → 才会进 `Cmd_Permission`  
3. 本机 `g_device_type` **不是** 院门禁（含 `'1'`）也不是楼门禁（含 `'2'`）→ 才会进 `ProcessElevator`

因此：扫码成功进了 `CommControl`，仍可能只做设置（`'1'`）或校时（`'6'`），**不一定**进权限；进了权限，若设备配成院/楼门禁，也 **不会** 进 Elevator。

---

## 五、参数对应关系（速查）

| 参数 | 来自读头 JSON | 用途 |
| --- | --- | --- |
| `received_data` | 字段 `data` 的字符串 | CSV 载荷；`[0]` 为命令号 |
| `order_type_meta` | 字段 `type`（`'0'`/`'1'`） | 权限完成后选 `qr_Reply` / `card_Reply` |
| `card_number_meta` | 字段 `uid` | 刷卡回复等 |
| `uart_port` | 固定 `4` 或 `5` | 区分读头口（Gate/Unit 等会用到） |

注意：`CommControl` 入口处对 `order_type_meta` / `card_number_meta` 有 `(void)` 忽略；真正用于回复是在 **`Cmd_Permission` 末尾**。

---

## 六、联调提示

- USART1 可对照：`Received UARTx data`、`type-->`、`data-->`、`csv[k]=`、`g_device_type`。  
- 权限 CSV 必须 **恰好 9 列**（8 个逗号），否则 `Cmd_Permission` 直接 `-401`。  
- 主循环若卡在 4G AT 序列，不会进 `app_poll`，整条链都不会跑。
