# QRProcessUart4 / QRProcessUart5 说明

更新时间：2026-07-18

> **源码**：`App/Pass/qr_comm.c`（声明见 `qr_comm.h`）  
> **调用**：`User/app_run.c` → `app_poll()`  
> **驱动侧收包**：`Hardware/Serial/uart4.*` / `uart5.*`（DMA + 空闲定帧）  
> **相关**：[uart4-dma-rx.md](../serial/uart4-dma-rx.md)（UART4 驱动路径）；通行命令见 `App/Pass/cmd.c` 的 `CommContrl`

---

## 一、这两个函数是干什么的？

它们是 **读头业务轮询入口**：主循环里不断调用，检查「这一路串口是否刚收完一帧」，若有则把字节拼成 JSON，抽出 `type` / `data` / `uid`，再交给通行逻辑。

| 函数 | 硬件口 | 驱动标志 / 缓冲 | 传给业务的端口号 |
| --- | --- | --- | --- |
| `QRProcessUart4` | UART4（读头 1） | `UART4_RX_Complete`、`UART4_RX_BUF`、`UART4_RX_CNT` | `4` |
| `QRProcessUart5` | UART5（读头 2） | `UART5_RX_Complete`、`UART5_RX_BUF`、`UART5_RX_CNT` | `5` |

**逻辑几乎对称**：两套独立静态缓冲区与切片变量，避免双读头互相踩内存。旧工程里的单入口 `QRProcess()` 已拆成这两个函数。

---

## 二、在系统中的位置

```text
读头模块 @ 9600
    │ 字节流
    ▼
Hardware/Serial/uart4 或 uart5
    │ DMA 写入 RX_BUF，空闲中断置 RX_Complete
    ▼
app_poll()
    ├─ QRProcessUart5()   ← 先轮询 UART5
    └─ QRProcessUart4()   ← 再轮询 UART4
            │
            ├─ type 为 '0' 或 '1' → CommContrl(..., uart_port)
            └─ type 为 '2'       → qr_handle_password_input(..., uart_port)
```

驱动层只负责「收到一截字节」；**组 JSON、解析字段、调命令** 都在这两个函数里完成。

---

## 三、处理步骤（两函数相同）

以 `QRProcessUart4` 为例（Uart5 把名字里的 `4` 换成 `5` 即可）。

### 1. 有没有新帧？

```c
if (UART4_RX_Complete) { ... }
```

未置位则直接返回，几乎零开销。

### 2. 追加到累积缓冲

把本次 `UART4_RX_BUF[0 .. UART4_RX_CNT-1]` 追加到 `uart4_rx_accum[]`（上限 512）。  
若一帧被拆成多次 IDLE 上报，可在这里拼完整。

### 3. 对齐到 `{`

从累积缓冲里找到第一个 `{`，丢掉前面的杂字节（噪声、残留）。

### 4. 是否已是完整 `{...}` 帧？

要求：

- 长度 ≥ 2  
- 首字节 `{`  
- **末字节** `}`  

未凑齐则不清累积缓冲（只清驱动侧 `RX_Complete` / `RX_CNT`），等下次再拼。

### 5. 切片字段（非完整 cJSON 解析）

用字符串搜索键名（注意必须搜 `"type"` / `"data"` / `"uid"` 带引号的键，避免 `data` 内容里碰巧出现 `uid` 子串）：

| 字段 | 含义 | 存到 |
| --- | --- | --- |
| `type` | 业务类型：`0` NFC，`1` 二维码，`2` 密码 | `order_type_uart4` / `uart4_type_slice` |
| `data` | CSV/载荷字符串（命令内容） | `s_received_uart4` |
| `uid` | 卡号等 | `card_Number_uart4` |

USART1 会 `printf` 整帧与 `type`/`data`，便于串口联调。

### 6. 分发业务

当 `type` 与 `data` 都切出非空时：

| `type` | 动作 |
| --- | --- |
| `'0'` 或 `'1'`（单字符） | `CommContrl(data, order_type, uid, 4 或 5)` — 设置/权限等命令 |
| `'2'`（单字符） | `qr_handle_password_input(data, 4 或 5)` — 密码输入；算法见 [password-4digit-auth.md](./password-4digit-auth.md) |

最后一个参数 `uart_port`（4 或 5）用于区分回复/日志走哪路读头。

### 7. 收尾

- 若已处理完整帧：清空 `uart*_rx_accum` 与切片状态  
- **无论是否凑齐完整帧**：`UART*_RX_Complete = 0`，`UART*_RX_CNT = 0`，允许驱动收下一截  

---

## 四、期望的 JSON 形态（示意）

读头侧大致是一层对象（字段顺序需满足当前切片算法对 `"type"` → `"data"` → `"uid"` 的相对位置假设）：

```json
{"type":"1","data":"2,....","uid":"12345678"}
```

- `type`：`"0"` / `"1"` / `"2"`  
- `data`：字符串，内部常为逗号分隔命令载荷（首字符常为命令号，见 `CommContrl`）  
- `uid`：可带引号的字符串或数字串  

解析是 **按偏移切字节**，不是完整 JSON 库；键顺序或额外空格异常时可能导致切错。联调时优先用 USART1 打印的 `Received UARTx data` 对照。

---

## 五、两函数差异一览

| 项 | Uart4 | Uart5 |
| --- | --- | --- |
| 函数名 | `QRProcessUart4` | `QRProcessUart5` |
| 驱动变量 | `UART4_*` | `UART5_*` |
| 累积 / 切片静态区 | `uart4_*` | `uart5_*` |
| `CommContrl` / 密码第 4 参 | `4` | `5` |
| `app_poll` 调用顺序 | 后调用 | **先**调用 |

无其它业务差异；改算法时两处通常要同步改。

---

## 六、和驱动文档怎么配合读

1. 先看 [uart4-dma-rx.md](../serial/uart4-dma-rx.md)：字节如何进 `UART4_RX_BUF`、何时置 `RX_Complete`。  
2. 再看本文：主循环如何消费该标志并进 `CommContrl`。  
3. UART5 驱动路径与 UART4 同类，业务侧即 `QRProcessUart5`。

---

## 七、联调注意

- 波特率 **9600**；线接错口时另一路函数永远看不到 `RX_Complete`。  
- 若 `main` 正跑 4G AT 序列，不会进 `app_poll`，两函数都不会被调用。  
- 帧必须以 `{` 开头、以 `}` 结尾；中间被拆包可靠累积缓冲拼接，但末字节不是 `}` 时不会进 `CommContrl`。  
- 看门狗在主循环喂狗；本函数内若打印极长、阻塞过久需注意（一般 JSON 帧较短）。
