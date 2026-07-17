# STM32 DMA 说明

> **相关文档**
>
> - [文件说明.md](./文件说明.md) — Embedded 目录索引
> - [STM32系统结构说明.md](./STM32系统结构说明.md) — 总线矩阵与 DMA1/DMA2 §七
> - [STM32外设说明.md](./STM32外设说明.md) — DMA 外设总览 §6.3
> - [STM32-串口USART说明.md](./STM32-串口USART说明.md) — 串口字节流、收包思路
> - [STM32-中断与外部中断说明.md](./STM32-中断与外部中断说明.md) — NVIC；DMA/空闲中断会用到
> - 产品工程示例：`tikong/tikong-20260403/SYSTEM/usart/`（USART + DMA + IDLE）
> - 接手对照：[梯控固件入门规划-基于F103基础.md](../tikong/tikong-20260403/docs/梯控固件入门规划-基于F103基础.md)

更新时间：2026-07-17

---

## 一、DMA 是什么

**DMA**（Direct Memory Access，**直接存储器访问**）是 MCU 片内的一套 **硬件搬运工**：在 **外设寄存器 ↔ 内存（SRAM）** 或 **内存 ↔ 内存** 之间自动搬数据，**不必 CPU 逐字节读写**。

```text
不用 DMA（串口收一字打断一次）：
  每到 1 字节 → RXNE 中断 → CPU 读 DR → 写入数组 → 返回
  波特率高 / 多路串口时，CPU 很忙，还容易丢字节

用 DMA：
  每到 1 字节 → USART 发 DMA 请求 → DMA 把 DR 写入缓冲区
  CPU 可继续跑主循环；一帧结束再用空闲中断通知「包到齐了」
```

| 项 | 说明 |
|----|------|
| **全称** | Direct Memory Access |
| **角色** | 总线矩阵上的 **主设备（Master）**，可主动访问 SRAM / 外设 |
| **触发** | 外设发出 **DMA 请求**（如 USART RXNE、ADC 转换完成、TIM 更新） |
| **典型用途** | UART 收发缓冲、ADC 连续采样、SPI/I2S 高速吞吐、内存块拷贝 |

**记忆**：CPU 管「干什么」；DMA 管「怎么搬字节」。

与系统结构关系见 [STM32系统结构说明.md](./STM32系统结构说明.md) §七。

---

## 二、三种搬运方向

| 方向（概念） | SPL / 手册常见写法 | 例子 |
|--------------|--------------------|------|
| **外设 → 内存** | PeripheralToMemory | USART RX、ADC → `buf[]` |
| **内存 → 外设** | MemoryToPeripheral | USART TX、DAC ← `buf[]` |
| **内存 → 内存** | MemoryToMemory | 大块拷贝（部分流支持） |

串口接收最常用：**外设→内存**。

```text
USART_DR ──DMA──► USART_RX_BUF[i++]
  (地址固定)         (地址每次 +1)
```

---

## 三、配置时必须想清的参数

无论 F1 还是 F4，配 DMA 时心里要有这几项：

| 参数 | 含义 | 串口 RX 常见取值 |
|------|------|------------------|
| **外设地址** | 如 `&USARTx->DR` | 固定，不递增 |
| **内存地址** | 接收缓冲区首地址 | 递增（`MemoryInc_Enable`） |
| **方向** | 谁到谁 | PeripheralToMemory |
| **数据宽度** | 字节/半字/字 | Byte（串口一字节） |
| **缓冲区长度** | 最多搬多少次 | `USART_REC_LEN` |
| **模式** | Normal / Circular | 见 §四 |
| **通道/流** | 硬件绑定哪条通路 | 查手册「DMA 请求映像表」 |

口诀：**外设地址死、内存地址走；方向对、长度够、通道别选错。**

---

## 四、Normal 与 Circular

| 模式 | 行为 | 适合 |
|------|------|------|
| **Normal（普通）** | 搬满设定长度后 **停止**；要再收必须重新设 NDTR 并 Enable | 不定长串口帧 + 空闲中断（梯控工程常用） |
| **Circular（循环）** | 搬满后自动从缓冲区头再开始 | ADC 连续采样、固定长度环形缓冲 |

```text
Normal + 串口不定长帧：
  DMA 开着一直收 → 线上静默 → IDLE 中断
  → 算出本帧长度 → 置 Complete → DMA_ReInit 再开下一轮

Circular + ADC：
  DMA 不停写环形缓冲 → 半满/全满中断里取半区数据做滤波
```

---

## 五、F103 与 F407：通道 vs 流

入门蓝 pill 是 **F1**；梯控工程是 **F4**。概念相同，硬件称呼不同：

| | STM32F103（F1） | STM32F407（F4） |
|--|-----------------|-----------------|
| 控制器 | DMA1、DMA2 | DMA1、DMA2 |
| 通路单位 | **Channel（通道）** | **Stream（流）+ Channel** |
| 例：USART1 RX | 查手册映射到某 Channel | 常见 **DMA2 Stream2 Channel4** |
| 时钟 | `RCC_AHBPeriph_DMAx` | `RCC_AHB1Periph_DMAx` |
| 剩余计数 | `DMA_GetCurrDataCounter` | 同名（读 NDTR） |

**重要**：哪个 USART 必须挂哪条 DMA 流/通道，由芯片 **硬件固定映射**，不能随便编。配错则 DMA 不工作。

梯控工程 USART1 RX 示例（F4）：

```14:48:tikong/tikong-20260403/SYSTEM/usart/usart.c
// DMA接收初始化（节选要点）
RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);
DMA_DeInit(DMA2_Stream2);
DMA_InitStructure.DMA_Channel = DMA_Channel_4;
DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DR;
DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)USART_RX_BUF;
DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralToMemory;
DMA_InitStructure.DMA_BufferSize = USART_REC_LEN;
DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE);
DMA_Cmd(DMA2_Stream2, ENABLE);
```

---

## 六、串口：DMA + 空闲中断（重点）

入门教程常用 **RXNE 中断逐字节收**；产品固件（如梯控）常用：

> **DMA 搬字节 + USART IDLE 判「一帧结束」**

### 6.1 为什么需要 IDLE

DMA 只知道「搬了多少」，不知道「这句话说完了」。  
**空闲中断 IDLE**：线上在接收期间曾有数据，随后 **一个字节时间内保持空闲** → 认为一帧结束。

```text
时间线：
  字节 字节 字节 …… 字节 |||||空闲……|
       ← DMA 写入缓冲区 →   ↑
                          IDLE 中断：算长度、置标志
```

### 6.2 本帧长度怎么算

DMA 启动时设 `BufferSize = N`（如 `USART_REC_LEN`）。  
硬件寄存器 **NDTR** = 还剩多少次没搬。

```text
已收长度 = N - DMA_GetCurrDataCounter(流/通道)
```

梯控 USART1：

```112:125:tikong/tikong-20260403/SYSTEM/usart/usart.c
void USART1_IRQHandler(void)
{
    if(USART_GetITStatus(USART1, USART_IT_IDLE) != RESET)
    {
        USART_ReceiveData(USART1); // 清除空闲中断标志
        USART_RX_CNT = USART_REC_LEN - DMA_GetCurrDataCounter(DMA2_Stream2);
        USART_RX_Complete = 1;
        USART1_DMA_ReInit();       // Normal 模式：重新装载再开收
    }
}
```

主循环里 `PCProcess()` 看到 `USART_RX_Complete` 再处理缓冲区——**ISR 里只记账，业务放主循环**（与串口状态机收包原则一致）。

### 6.3 Normal 模式下为何要 ReInit

Normal 模式搬完或本帧结束后，流会停。下一帧要再收，必须：

1. 关闭 DMA  
2. 清标志  
3. 重装 `NDTR`（及必要时内存地址）  
4. 再 Enable  

对应工程里的 `USART1_DMA_ReInit()`。

### 6.4 与「软件状态机收包」对照

| | RXNE + 状态机（入门） | DMA + IDLE（产品） |
|--|----------------------|---------------------|
| 谁把字节放进缓冲 | CPU 在中断里写 | **DMA 硬件写** |
| 怎么知道一包结束 | 包头包尾 / 定长 | **线上空闲** 或定长满 |
| CPU 负担 | 每字节一次中断 | 每帧一次 IDLE（+ 可选 DMA 完成） |
| 适合 | 低速、协议清晰 | 多路串口、JSON/扫码不定长帧 |

二者可并存：DMA 收完一帧后，仍可用状态机解析命令内容。

---

## 七、和主循环 / 中断的分工

```text
                    ┌─────────────┐
  线上字节 ────────►│  USART + DMA │──► SRAM 缓冲区
                    └──────┬──────┘
                           │ IDLE / TC
                           ▼
                    ┌─────────────┐
                    │  IRQ：置标志 │  （尽量短）
                    │  算长度/ReInit│
                    └──────┬──────┘
                           │
                           ▼
                    ┌─────────────┐
                    │  main while │  解析、业务、printf
                    └─────────────┘
```

| 层级 | 做什么 |
|------|--------|
| DMA | 搬字节 |
| 中断 | 帧界、长度、重启 DMA |
| 主循环 | `parse`、命令、组帧、上报 |

---

## 八、ADC 场景（了解）

```text
ADC 连续转换 ──DMA──► adc_buf[N]（Circular）
                │
         半满/全满中断 → 取一半数据滤波 → 串口打印
```

入门可后做；与串口 DMA 共用「外设→内存、Circular/Normal」同一套概念。详见长期路线中的 ADC 周次。

---

## 九、SPL 配置流程（串口 RX 提纲）

```text
1. 开 DMA 时钟（AHB / AHB1）
2. DMA_DeInit 对应通道/流
3. 填 DMA_Init：外设地址、内存地址、方向、长度、Inc、Mode、优先级…
4. USART_DMACmd(USARTx, USART_DMAReq_Rx, ENABLE)
5. DMA_Cmd(ENABLE)
6. （不定长）USART_ITConfig(IDLE) + NVIC
7. 主循环轮询 Complete 标志处理数据
8. Normal：每帧后 ReInit
```

F1 用 `DMA_Channelx`；F4 用 `DMAy_Streamx` + `DMA_Channel`。

---

## 十、梯控工程里 DMA 落在哪

| 串口 | 用途 | DMA（工程内） |
|------|------|----------------|
| USART1 | PC 调试 | DMA2 Stream2（RX）+ IDLE |
| USART2 | RS485 | DMA 收（回执路径可按需） |
| USART3 | 4G JSON | DMA1 Stream1 + IDLE |
| UART4 | 扫码 | DMA1 Stream2 + IDLE |
| UART5 | 备用 | DMA + IDLE |

接手时重点看：`*Process()` 如何等 `RX_Complete`，以及 IDLE 里如何用 `DMA_GetCurrDataCounter` 算长度。

---

## 十一、常见问题

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| DMA 完全不搬 | 通道/流选错；未 `USART_DMACmd`；未开 DMA 时钟 | 查手册映射表；核对 Init |
| 只能收满缓冲才有数据 | 只用了 TC，没用 IDLE | 不定长帧开 **IDLE** |
| 长度总是错 / 为 0 | IDLE 标志未正确清除；ReInit 时机不对 | 先读 SR 再读 DR 清 IDLE；再算 NDTR |
| 第二帧收不到 | Normal 后未 ReInit | 每帧结束重装 NDTR 并 Enable |
| 缓冲内容被冲掉 | 主循环未处理完 DMA 又写入 | 加 Complete 互斥；或双缓冲/Circular 半满策略 |
| 与 CPU 抢总线偶发卡顿 | 高优先级 DMA 占满 | 调 DMA 优先级；关键外设提权 |

---

## 十二、自检

1. DMA 全称与一句话作用？  
   → Direct Memory Access；硬件在外设与内存间搬数据，减负 CPU。  
2. 串口 RX 的方向与两个地址谁递增？  
   → 外设→内存；外设地址固定，内存递增。  
3. Normal 与 Circular 差别？  
   → Normal 搬完停；Circular 自动回头。  
4. 不定长串口帧为何常配 IDLE？  
   → DMA 不知包界；空闲表示「这句说完了」。  
5. 已收长度公式？  
   → `BufferSize - DMA_GetCurrDataCounter(...)`。  
6. F1 与 F4 通路叫法差别？  
   → F1：Channel；F4：Stream + Channel。

---

## 延伸阅读

| 资料 | 内容 |
|------|------|
| [STM32系统结构说明.md](./STM32系统结构说明.md) §七 | DMA 在总线矩阵中的位置 |
| [STM32外设说明.md](./STM32外设说明.md) §6.3 | 外设地图中的 DMA 条目 |
| [STM32-串口USART说明.md](./STM32-串口USART说明.md) | 字节帧、收包与状态机 |
| 参考手册 RM0008（F1）/ RM0090（F4） | DMA 请求映像表 |
| `tikong/.../SYSTEM/usart/usart.c` 等 | 实战 DMA + IDLE |

---

*文档随 ADC DMA、环形缓冲双缓冲等实验进度可再补充。*
