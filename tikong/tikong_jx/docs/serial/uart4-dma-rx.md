# UART4 DMA 收包路径说明

> **相关文档**
>
> - [docs/README.md](../README.md) — 文档索引
> - [project-overview.md](../overview/project-overview.md) — 五路串口分工
> - [onboarding-f103.md](../overview/onboarding-f103.md) — 阶段 B/C：DMA 空闲收包 → 扫码命令
> - Embedded：[STM32-串口USART说明.md](../../../docs/STM32-串口USART说明.md) — 帧、空闲、字节流
> - Embedded：[STM32-DMA说明.md](../../../docs/STM32-DMA说明.md) — DMA 外设→内存、NDTR
>
> **源码**：`Hardware/Serial/uart4.c` / `uart4.h`，主循环 `User/main.c`；业务 `App/Pass/qr_comm.c`

更新时间：2026-07-17

---

## 一、一句话结论

UART4（扫码枪）采用 **DMA 搬字节 + 中断定帧**：

1. 字节由 **DMA1 Stream2** 写入 `UART4_RX_BUF`（CPU 不逐字节进中断）
2. 定帧有两条可并行的中断路径（见下表），最终都置 `UART4_RX_Complete` / `UART4_RX_CNT`
3. 主循环调用 `**QRProcess()`** 轮询该标志，再解析 CVJ 命令


| 路径             | 触发条件                               | 本工程现状                                                          |
| -------------- | ---------------------------------- | -------------------------------------------------------------- |
| **IDLE（空闲）**   | 一帧发完、线空闲约 1 字节时间                   | **已实现**（`UART4_IRQHandler`）                                    |
| **DMA TC（收满）** | Normal 模式搬满 `UART4_REC_LEN`（512）字节 | NVIC 已配，**需补** `DMA_IT_TC` + `DMA1_Stream2_IRQHandler`（流程见 §六） |


短帧（扫码常态）走 IDLE；超长连续数据写满缓冲时靠 TC 及时收尾并重开 DMA，避免后续字节静默丢失。

**TC** = **Transfer Complete**（传输完成）。

---

## 二、角色与硬件


| 项   | 本工程                                                            |
| --- | -------------------------------------------------------------- |
| 外设  | **UART4**，仅接收（`USART_Mode_Rx`）                                 |
| 引脚  | **PC10** TX 复用、**PC11** RX 复用（`GPIO_AF_UART4`）                 |
| 波特率 | `uart4_init(9600)`（见 `main.c`）                                 |
| 用途  | 二维码模块 → `QRProcess` → 命令 1～6                                   |
| DMA | **DMA1 Stream2 / Channel4**，外设 `UART4->DR` → 内存 `UART4_RX_BUF` |
| 缓冲  | `UART4_REC_LEN = 512`，`__align(4)`                             |


全局状态（`uart4.c`）：


| 变量                  | 含义                                  |
| ------------------- | ----------------------------------- |
| `UART4_RX_BUF[]`    | DMA 写入的原始字节                         |
| `UART4_RX_CNT`      | 本帧长度（IDLE：`512 - NDTR`；TC：固定 `512`） |
| `UART4_RX_Complete` | 一帧就绪（IDLE 或 TC 置位）；主循环清零            |


---

## 三、完整路径总览

### 3.1 当前已实现：IDLE 定帧（短帧主路径）

```text
扫码枪 / 串口助手 @9600
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
   main while(1) → QRProcess()
        │
        └─ if (UART4_RX_Complete)
              打印 / memcpy → received_data
              清 Complete / CNT
              ExtractValidData → IsCvjFormat → IsCmd → DoCmd1～6
```

### 3.2 实现 DMA TC 后：双路径汇合

```text
                    UART4 RX → DMA1 Stream2 → UART4_RX_BUF
                              NDTR 从 512 递减
                         ┌────────────┴────────────┐
                         │                         │
              未写满且线空闲              NDTR 减到 0（写满 512）
                         │                         │
                         ▼                         ▼
              UART4_IRQHandler            DMA1_Stream2_IRQHandler
              (USART_IT_IDLE)             (DMA_IT_TCIF2)
                         │                         │
                         │              CNT = UART4_REC_LEN
                         │              Complete = 1
                         │              清 TC 标志
                         │              UART4_DMA_ReInit()
                         │                         │
                         └──────────┬──────────────┘
                                    ▼
                         UART4_RX_Complete == 1
                                    ▼
                              QRProcess()
```

和 USART1 / USART3 / UART5 同一套路：**DMA 拼包 + 中断定帧 + `*Process()` 业务**；UART4 在启用 TC 后，定帧来源变为 **IDLE 或 TC 二选一（或先后）**。

---

## 四、初始化（`uart4_init`）

顺序（`uart4.c`）：

1. 开 GPIOC、UART4 时钟；PC10/PC11 复用
2. `USART_Init`：8N1、仅 Rx、波特率参数
3. `**USART_ITConfig(UART4, USART_IT_IDLE, ENABLE)`** ← 短帧定帧关键
4. `USART_Cmd(UART4, ENABLE)`
5. `**UART4_DMA_RX_Init()**`（实现 TC 时在此开 `DMA_IT_TC`）
6. NVIC 使能 `**UART4_IRQn**`（处理 IDLE）

### 4.1 DMA 配置要点（`UART4_DMA_RX_Init`）


| 参数               | 取值                      | 含义                 |
| ---------------- | ----------------------- | ------------------ |
| Stream / Channel | DMA1 Stream2 / Channel4 | F4 手册：UART4_RX 映射  |
| 方向               | PeripheralToMemory      | DR → 缓冲            |
| BufferSize       | `UART4_REC_LEN` (512)   | Normal 模式下一次最多搬这么多 |
| Mode             | **Normal**              | 搬满后 DMA 停止，需软件重启   |
| MemoryInc        | Enable                  | 缓冲地址递增             |
| USART_DMACmd     | `USART_DMAReq_Rx`       | UART 收字节触发 DMA     |


同时配置了 `DMA1_Stream2_IRQn` 的 NVIC。要走通 **收满（TC）** 路径，还需在 `DMA_Cmd` 之前增加：

```c
DMA_ITConfig(DMA1_Stream2, DMA_IT_TC, ENABLE);  // 使能传输完成中断
```

并实现 `DMA1_Stream2_IRQHandler`（完整流程见 §六）。仅配 NVIC、不开 `DMA_IT_TC`、无 ISR，TC 不会进 CPU。

---

## 五、运行时：IDLE 如何置 `RX_Complete`

```116:128:SYSTEM/usart/uart4.c
void UART4_IRQHandler(void)
{
	if (USART_GetITStatus(UART4, USART_IT_IDLE) != RESET)
	{
		USART_ReceiveData(UART4); // 清除空闲中断标志

		UART4_RX_CNT = UART4_REC_LEN - DMA_GetCurrDataCounter(DMA1_Stream2);
		UART4_RX_Complete = 1;

		UART4_DMA_ReInit();
	}
}
```


| 步骤  | 行为                                                                       |
| --- | ------------------------------------------------------------------------ |
| 1   | 总线空闲 → `USART_IT_IDLE`                                                   |
| 2   | 读 `DR` 清 IDLE（更稳妥写法：先读 SR 再读 DR，USART3 注释里有说明）                           |
| 3   | **已收长度** = 设定总长 − 当前 NDTR                                                |
| 4   | `UART4_RX_Complete = 1`（中断里只置标志，不做业务）                                    |
| 5   | `UART4_DMA_ReInit()`：关 Stream → 清 TC/HT/错标志 → `NDTR=512`、重装 `M0AR` → 再使能 |


`UART4_DMA_ReInit` 作用：Normal 模式每帧结束后必须重开，否则下一帧 DMA 不再搬数。

---

## 六、实现 DMA 收满（TC）中断的完整流程

本节描述 **在现有 IDLE 路径之上补齐 TC** 后，从初始化到 `QRProcess` 的完整行为。与当前源码的差异：多开 `DMA_IT_TC`，并新增 `DMA1_Stream2_IRQHandler`。

### 6.1 为何需要 TC


| 场景            | 仅 IDLE                           | IDLE + TC                                   |
| ------------- | -------------------------------- | ------------------------------------------- |
| 短帧（<512）后空闲   | 正常定帧                             | 同左（仍走 IDLE）                                 |
| 连续写入刚好/超过 512 | DMA 已停，后续字节丢失，要等 IDLE 才 `ReInit` | **一写满即 ISR**：置 Complete、立刻 `ReInit`，可继续收下一截 |
| 业务含义          | 扫码短包足够                           | 防缓冲打满后「静默丢数」                                |


TC **不是**替代 IDLE 的「帧结束」语义：它表示 **本轮 DMA 缓冲用尽**。扫码短包仍以 IDLE 为准；TC 是缓冲上限的安全网。

### 6.2 初始化补丁（`UART4_DMA_RX_Init`）

在已有 `DMA_Init` + NVIC（`DMA1_Stream2_IRQn`）基础上：

```c
DMA_Init(DMA1_Stream2, &DMA_InitStructure);

/* 已有：NVIC_Init → DMA1_Stream2_IRQn */

DMA_ITConfig(DMA1_Stream2, DMA_IT_TC, ENABLE);  /* ← 补这一行 */
USART_DMACmd(UART4, USART_DMAReq_Rx, ENABLE);
DMA_Cmd(DMA1_Stream2, ENABLE);
```

启动条件 checklist：

1. Stream 映射正确（UART4_RX → DMA1 Stream2 Channel4）
2. `NVIC` 已使能 `DMA1_Stream2_IRQn`
3. `**DMA_ITConfig(..., DMA_IT_TC, ENABLE)**` 已调用
4. 存在 `**DMA1_Stream2_IRQHandler**`（名称必须与启动文件向量一致）

### 6.3 TC 中断服务流程

```text
NDTR 减到 0，DMA 置 TCIF2
        │
        ▼
DMA1_Stream2_IRQHandler
        │
        ├─ if (DMA_GetITStatus(DMA1_Stream2, DMA_IT_TCIF2) == SET)
        │       │
        │       ├─ UART4_RX_CNT = UART4_REC_LEN   // 写满，长度为缓冲总长
        │       ├─ UART4_RX_Complete = 1
        │       ├─ DMA_ClearITPendingBit(DMA1_Stream2, DMA_IT_TCIF2)
        │       └─ UART4_DMA_ReInit()             // 与 IDLE 共用：重装 NDTR 再开
        │
        └─ （可选）处理 TE/FE 等错误标志并清位
        │
        ▼
主循环 QRProcess() 同 IDLE 路径（见 §七）
```

参考实现（与现有 IDLE 风格对齐，可直接落在 `uart4.c`）：

```c
void DMA1_Stream2_IRQHandler(void)
{
	if (DMA_GetITStatus(DMA1_Stream2, DMA_IT_TCIF2) != RESET)
	{
		UART4_RX_CNT = UART4_REC_LEN;   /* 收满：长度即缓冲容量 */
		UART4_RX_Complete = 1;

		DMA_ClearITPendingBit(DMA1_Stream2, DMA_IT_TCIF2);
		UART4_DMA_ReInit();             /* Normal 模式必须重开 */
	}
}
```

要点：


| 步骤     | 说明                                                                |
| ------ | ----------------------------------------------------------------- |
| 判 TC   | 用 `DMA_IT_TCIF2`（Stream2 的传输完成挂起位）                                |
| 长度     | 写满时 NDTR=0，直接 `CNT = UART4_REC_LEN`，不必再算 `512 - NDTR`             |
| 置标志    | 与 IDLE 相同：`UART4_RX_Complete = 1`，**不在 ISR 里调 `QRProcess`**       |
| 清标志    | `DMA_ClearITPendingBit`；`UART4_DMA_ReInit` 里也会写 `LIFCR` 清 TC/HT/错 |
| 重开 DMA | 必须调用，否则 Stream 停在 Normal 结束态，后续字节丢失                               |


### 6.4 与 IDLE 的竞态与协作

两条 ISR 可能先后或接近同时到达同一缓冲，需约定：


| 情况           | 建议处理                                                                                                                                      |
| ------------ | ----------------------------------------------------------------------------------------------------------------------------------------- |
| 先 TC 后 IDLE  | TC 已 `ReInit`，缓冲已给下一轮；IDLE 若仍触发，可能算到 **新一轮** 的 NDTR → 长度异常或二次 Complete。IDLE 里可加：若 `UART4_RX_Complete` 已为 1 则只清 IDLE、不算长度；或 TC 后短暂忽略 IDLE。 |
| 先 IDLE 后 TC  | 短帧不会写满，一般不会进 TC。                                                                                                                          |
| 恰好 512 字节后空闲 | 可能 TC、IDLE 都进：第二次 `ReInit` 冗余但通常无害；业务侧 `QRProcess` 清 Complete 后若再被置 1，可能 **同内容处理两次**——可用「处理中锁」或 IDLE 侧检测 Complete 已置则跳过。                  |
| 优先级          | 本工程 DMA Stream2 与 UART4 同为抢占优先级 1；谁先跑取决于挂起顺序。                                                                                             |


最小稳妥策略（实现 TC 时推荐）：

```c
/* IDLE 与 TC 共用的收尾，避免重复置位/重复 ReInit 逻辑分叉过大 */
static void UART4_RxFrameDone(u16 len)
{
	if (UART4_RX_Complete)
		return;                 /* 另一路已收尾，本路只负责清自己的挂起位 */
	UART4_RX_CNT = len;
	UART4_RX_Complete = 1;
	UART4_DMA_ReInit();
}
```

- IDLE：`UART4_RxFrameDone(UART4_REC_LEN - DMA_GetCurrDataCounter(DMA1_Stream2));` 再清 IDLE  
- TC：`UART4_RxFrameDone(UART4_REC_LEN);` 再清 TCIF2

### 6.5 时序示意（写满 512）

```text
时间 →
RX:   [B0][B1]……[B511][B512…]…
DMA:  NDTR 512→…→1→0  ▲ TCIF2
                       │
                       DMA1_Stream2_IRQHandler
                       CNT=512, Complete=1, ReInit(NDTR=512)
                       │
                       后续字节进入「下一轮」缓冲（若仍有数据）
main:                  QRProcess 拷贝本帧 512 字节
```

对比短帧（仅 IDLE）见 §九。

### 6.6 实现后行为对照表


| 情况                     | 谁置 Complete                   | `UART4_RX_CNT` |
| ---------------------- | ----------------------------- | -------------- |
| 一帧 < 512，然后空闲          | IDLE                          | 实际长度           |
| 连续写入满 512              | **TC**                        | **512**        |
| 写满后还有数据                | TC 已 ReInit，后续由下一轮 IDLE/TC 定帧 | 按下一轮计算         |
| 未开 `DMA_IT_TC` / 无 ISR | 仅 IDLE（可能已丢数）                 | —              |


口诀：**短帧靠 IDLE；打满靠 TC；两条都只置标志，业务仍在 `QRProcess`。**

---

## 七、如何进 `QRProcess()`

中断 **不调用** `QRProcess`。主循环轮询：

```c
// USER/main.c 主循环（示意）
QRProcess();   // 需取消注释才会跑业务
```

```142:168:SYSTEM/usart/uart4.c
	if (UART4_RX_Complete)
	{
		// printf 原始帧；memcpy → received_data；补 '\0'
		UART4_RX_Complete = 0;
		UART4_RX_CNT = 0;
		valid_data = ExtractValidData_Bytes(...);
		if (IsCvjFormat(valid_data)) {
			switch (IsCmd(valid_data)) {
				case '1': /* ... */ DoCmd1 ...
				case '3': /* 上报通行 */ DoCmd3 ...
				// 2,4,5,6 同理
			}
		}
		// 非 CVJ：经 USART3 上报失败类 JSON 等
	}
```

处理顺序：

1. 见 `UART4_RX_Complete == 1`
2. 用 `UART4_RX_CNT` 拷贝缓冲（注意先用完再清标志）
3. 清 `Complete` / `CNT`，避免重入重复处理
4. 去头尾 → CVJ 判断 → `DoCmd1`～`DoCmd6`

联调注意：当前 `main.c` 里 `**QRProcess()` 可能仍被注释**；标志会置位，但业务不跑。阶段 C 验收需取消注释。

---

## 八、与其它串口对照


| 串口        | Process         | DMA（典型）          | 定帧               | Complete 变量             |
| --------- | --------------- | ---------------- | ---------------- | ----------------------- |
| USART1    | `PCProcess`     | DMA2 Stream2     | IDLE             | `USART_RX_Complete`     |
| USART2    | `Rs485Process`  | DMA1 Stream5     | IDLE             | `RS485_RX_Complete`     |
| USART3    | `G4GProcess`    | DMA1 Stream1     | IDLE             | `USART3_RX_Complete`    |
| **UART4** | `**QRProcess`** | **DMA1 Stream2** | **IDLE（+可选 TC）** | `**UART4_RX_Complete`** |
| UART5     | （主循环直接判）        | DMA1…            | IDLE             | `UART5_RX_Complete`     |


共性：**中断（IDLE 和/或 TC）只置标志 + 重开 DMA；业务在主循环。**

---

## 九、时序示意

### 9.1 短帧扫码（仅 IDLE，常态）

```text
时间 →
RX 线:  ....[字节1][字节2]...[字节N]........(空闲)........
DMA:         写入 BUF[0..N-1]，NDTR: 512→511→…→(512-N)
IDLE:                                         ▲ 进 UART4_IRQHandler
                                              │ Complete=1, CNT=N
                                              │ DMA_ReInit
main:                                         稍后 QRProcess 看到 Complete
```

### 9.2 缓冲写满（实现 TC 后）

```text
时间 →
RX 线:  ....[B0]…………[B511][后续字节…]....................
DMA:         NDTR → 0，Stream 停
TC:                        ▲ DMA1_Stream2_IRQHandler
                           │ CNT=512, Complete=1, ReInit
                           │ 后续字节进入新一轮 NDTR
main:                      QRProcess 处理满缓冲这一帧
```

---

## 十、排障清单

1. **收不到**：波特率是否 9600、PC11 接线、`uart4_init` 是否调用、DMA/`USART_DMAReq_Rx` 是否开。
2. **有字节但 Process 不跑**：`QRProcess()` 是否注释；是否在看 `UART4_RX_Complete`。
3. **长度不对**：IDLE 清法是否偶发丢标志；与 USART3「先 SR 后 DR」对比。
4. **第二帧起丢失**：`UART4_DMA_ReInit` 是否每次 IDLE/TC 都执行；Normal 模式忘重开则 DMA 停死。
5. **超长帧截断**：缓冲 512；实现 TC 后可分段上报/拼接，或加大 `UART4_REC_LEN`、改环形/双缓冲。
6. **TC 不进中断**：是否调用了 `DMA_ITConfig(..., DMA_IT_TC, ENABLE)`；是否实现 `DMA1_Stream2_IRQHandler`；NVIC 是否使能；调试时看 `DMA_LISR` 的 TCIF2 是否置位。
7. **同帧处理两次**：TC 与 IDLE 竞态（见 §6.4），用共用 `UART4_RxFrameDone` 或处理锁。
8. **仅配了 Stream2 NVIC 仍无 TC**：未开 `DMA_IT_TC` 时硬件不向 CPU 请求，定帧仍只靠 `UART4_IRQHandler`。

---

## 十一、源码索引


| 符号 / 文件                   | 职责                                       |
| ------------------------- | ---------------------------------------- |
| `uart4_init`              | GPIO + UART + IDLE + DMA + NVIC          |
| `UART4_DMA_RX_Init`       | 首次配置 DMA1 Stream2；实现 TC 时于此开 `DMA_IT_TC` |
| `UART4_DMA_ReInit`        | IDLE/TC 收尾后重启 DMA                        |
| `UART4_IRQHandler`        | IDLE → CNT / Complete（已实现）               |
| `DMA1_Stream2_IRQHandler` | TC → CNT=512 / Complete（实现收满路径时新增）       |
| `QRProcess`               | 主循环消费一帧 → 命令                             |
| `USER/main.c`             | `uart4_init(9600)`、`QRProcess()` 调用点     |
| `SYSTEM/utils/cmd.c`      | `DoCmd1`～`DoCmd6` 等                      |


