# UART4 DMA 收包路径说明

> **相关文档**
>
> - [docs/README.md](../README.md) — 文档索引
> - [project-overview.md](../overview/project-overview.md) — 五路串口分工
> - [onboarding-f103.md](../overview/onboarding-f103.md) — 阶段 B/C：DMA 空闲收包 → 扫码命令
> - [qr-process-uart45.md](../pass/qr-process-uart45.md) — `QRProcessUart4` 业务轮询
> - Embedded：[STM32-串口USART说明.md](../../../docs/STM32-串口USART说明.md) — 帧、空闲、字节流
> - Embedded：[STM32-DMA说明.md](../../../docs/STM32-DMA说明.md) — DMA 外设→内存、NDTR
>
> **源码**：`Hardware/Serial/uart4.c` / `uart4.h`；业务 `App/Pass/qr_comm.c`（`QRProcessUart4`）

更新时间：2026-07-18

---

## 一、一句话结论

UART4（读头 1）采用 **DMA 搬字节 + USART 空闲中断（IDLE）定帧**：

1. 字节由 **DMA1 Stream2** 写入 `UART4_RX_BUF`（CPU 不逐字节进中断）
2. 一帧发完、线空闲约 1 字节时间 → `UART4_IRQHandler`（`USART_IT_IDLE`）置 `UART4_RX_Complete` / `UART4_RX_CNT`，并 `UART4_DMA_ReInit()`
3. 主循环 `app_poll()` → `QRProcessUart4()` 轮询该标志，再拼 JSON / 进 `CommContrl`

**本工程不定帧、不处理 DMA 收满（TC）中断。** 扫码帧远小于 512 字节缓冲，仅靠 IDLE 即可。

---

## 二、角色与硬件

| 项 | 本工程 |
| --- | --- |
| 外设 | **UART4**，仅接收（`USART_Mode_Rx`） |
| 引脚 | **PC10** TX 复用、**PC11** RX 复用（`GPIO_AF_UART4`） |
| 波特率 | `uart4_init(9600)`（`User/board_init.c`） |
| 用途 | 读头 1 → `QRProcessUart4` → `CommContrl` 等 |
| DMA | **DMA1 Stream2 / Channel4**，外设 `UART4->DR` → 内存 `UART4_RX_BUF` |
| 缓冲 | `UART4_REC_LEN = 512`，`__align(4)` |
| 定帧 | **仅 IDLE**（`USART_IT_IDLE`） |

全局状态（`uart4.c`）：

| 变量 | 含义 |
| --- | --- |
| `UART4_RX_BUF[]` | DMA 写入的原始字节 |
| `UART4_RX_CNT` | 本帧长度 = `UART4_REC_LEN - NDTR` |
| `UART4_RX_Complete` | 一帧就绪（IDLE 置位）；业务侧清零 |

---

## 三、完整路径总览

```text
读头 / 串口助手 @9600
        │ 字节流
        ▼
   UART4 RX (PC11) ──USART_DMAReq_Rx──► DMA1 Stream2
        │                                    │
        │                              写入 UART4_RX_BUF[]
        │                              NDTR 递减
        │
        │ 线空闲约 1 字节时间
        ▼
   UART4_IRQHandler  (USART_IT_IDLE)
        │
        ├─ 清 IDLE（读 DR）
        ├─ UART4_RX_CNT = 512 - NDTR
        ├─ UART4_RX_Complete = 1
        └─ UART4_DMA_ReInit()   ← 关流、清标志、重装 NDTR、再使能
        │
        ▼
   app_poll() → QRProcessUart4()
        │
        └─ if (UART4_RX_Complete)
              追加到 uart4_rx_accum，凑齐 "{...}"
              切 type/data/uid → CommContrl 或密码处理
              清 Complete / CNT
```

与 USART1 / USART3 / UART5 同一套路：**DMA 拼包 + IDLE 定帧 + 主循环 `*Process` 业务**。本工程 **不使用** DMA TC（收满）路径。

---

## 四、初始化（`uart4_init`）

顺序（`Hardware/Serial/uart4.c`）：

1. 开 GPIOC、UART4 时钟；PC10/PC11 复用
2. `USART_Init`：8N1、仅 Rx、波特率参数
3. **`USART_ITConfig(UART4, USART_IT_IDLE, ENABLE)`** ← 定帧关键
4. `USART_Cmd(UART4, ENABLE)`
5. `UART4_DMA_RX_Init()`
6. NVIC 使能 **`UART4_IRQn`**（处理 IDLE）

### 4.1 DMA 配置要点（`UART4_DMA_RX_Init`）

| 参数 | 取值 | 含义 |
| --- | --- | --- |
| Stream / Channel | DMA1 Stream2 / Channel4 | F4 手册：UART4_RX 映射 |
| 方向 | PeripheralToMemory | DR → 缓冲 |
| BufferSize | `UART4_REC_LEN` (512) | Normal 模式下一次最多搬这么多 |
| Mode | **Normal** | 搬满后 DMA 停止，需软件重启（本工程靠 IDLE 后 `ReInit`） |
| MemoryInc | Enable | 缓冲地址递增 |
| USART_DMACmd | `USART_DMAReq_Rx` | UART 收字节触发 DMA |

---

## 五、运行时：IDLE 如何置 `RX_Complete`

```c
/* Hardware/Serial/uart4.c — 示意 */
void UART4_IRQHandler(void)
{
	if (USART_GetITStatus(UART4, USART_IT_IDLE) != RESET)
	{
		USART_ReceiveData(UART4); /* 清 IDLE */

		UART4_RX_CNT = UART4_REC_LEN - DMA_GetCurrDataCounter(DMA1_Stream2);
		UART4_RX_Complete = 1;

		UART4_DMA_ReInit();
	}
}
```

| 步骤 | 行为 |
| --- | --- |
| 1 | 总线空闲 → `USART_IT_IDLE` |
| 2 | 读 `DR` 清 IDLE（更稳妥：先读 SR 再读 DR） |
| 3 | **已收长度** = 设定总长 − 当前 NDTR |
| 4 | `UART4_RX_Complete = 1`（中断里只置标志，不做业务） |
| 5 | `UART4_DMA_ReInit()`：关 Stream → 清标志 → `NDTR=512`、重装 `M0AR` → 再使能 |

`UART4_DMA_ReInit` 作用：Normal 模式每帧结束后必须重开，否则下一帧 DMA 不再搬数。

---

## 六、如何进 `QRProcessUart4()`

中断 **不调用** 业务函数。主循环轮询：

```c
/* User/app_run.c — app_poll() */
QRProcessUart5();
QRProcessUart4();
```

业务侧见 [qr-process-uart45.md](../pass/qr-process-uart45.md)：见 `UART4_RX_Complete` → 累积 → 完整 `{...}` → `CommContrl` / 密码。

处理顺序：

1. 见 `UART4_RX_Complete == 1`
2. 用 `UART4_RX_CNT` 拷贝缓冲（先用完再清标志）
3. 清 `Complete` / `CNT`，避免重入重复处理

---

## 七、与其它串口对照

| 串口 | Process | DMA（典型） | 定帧 | Complete 变量 |
| --- | --- | --- | --- | --- |
| USART1 | `PCProcess` | DMA2 Stream2 | IDLE | `USART_RX_Complete` |
| USART2 | `Rs485Process` | DMA1 Stream5 | IDLE | `RS485_RX_Complete` |
| USART3 | `G4GProcess` | DMA1 Stream1 | IDLE | `USART3_RX_Complete` |
| **UART4** | **`QRProcessUart4`** | **DMA1 Stream2** | **IDLE** | **`UART4_RX_Complete`** |
| UART5 | `QRProcessUart5` | DMA1… | IDLE | `UART5_RX_Complete` |

共性：**IDLE 中断只置标志 + 重开 DMA；业务在主循环。本工程各口均不以 DMA TC 定帧。**

---

## 八、时序示意（短帧扫码，仅 IDLE）

```text
时间 →
RX 线:  ....[字节1][字节2]...[字节N]........(空闲)........
DMA:         写入 BUF[0..N-1]，NDTR: 512→511→…→(512-N)
IDLE:                                         ▲ 进 UART4_IRQHandler
                                              │ Complete=1, CNT=N
                                              │ DMA_ReInit
main:                                         稍后 QRProcessUart4 看到 Complete
```

说明：若连续写入接近或超过 512 字节且中间无空闲，DMA 在 Normal 模式下会停；本工程读头 JSON 为短帧，设计上不依赖 TC 兜底。联调勿送超长连续无空闲数据。

---

## 九、排障清单

1. **收不到**：波特率是否 9600、PC11 接线、`uart4_init` 是否调用、DMA / `USART_DMAReq_Rx` 是否开。
2. **有字节但业务不跑**：`app_poll` 是否在跑（是否卡在 4G AT 序列）；是否看 `UART4_RX_Complete`；`QRProcessUart4` 是否被调用。
3. **长度不对**：IDLE 清法是否偶发丢标志；与 USART3「先 SR 后 DR」对比。
4. **第二帧起丢失**：`UART4_DMA_ReInit` 是否每次 IDLE 都执行；Normal 模式忘重开则 DMA 停死。
5. **帧不完整 / 进不了 CommContrl**：业务侧要求累积后以 `{` 开头、以 `}` 结尾，见 [qr-process-uart45.md](../pass/qr-process-uart45.md)。

---

## 十、源码索引

| 符号 / 文件 | 职责 |
| --- | --- |
| `uart4_init` | GPIO + UART + IDLE + DMA + NVIC |
| `UART4_DMA_RX_Init` | 首次配置 DMA1 Stream2 |
| `UART4_DMA_ReInit` | IDLE 收尾后重启 DMA |
| `UART4_IRQHandler` | IDLE → CNT / Complete |
| `QRProcessUart4` | `App/Pass/qr_comm.c`：主循环消费一帧 |
| `User/board_init.c` | `uart4_init(9600)` |
| `User/app_run.c` | `app_poll` 调用 `QRProcessUart4` |
| `App/Pass/cmd.c` | `CommContrl` 等命令 |
