# 中断服务程序说明

更新时间：2026-07-19  
入口总表：`User/stm32f4xx_it.c`  
相关：主循环轮询见 [flows/app-poll-flow.md](./flows/app-poll-flow.md)；目录树见 [flows/directory-tree.md](./flows/directory-tree.md)

---

## 1. 设计原则：中断里只做「短而必要」的事

中断会打断主循环，占用时间越长，其它中断和业务轮询越容易被拖慢甚至丢事件。因此约定：

- **ISR 执行路径要短**：清标志、记长度、置完成位、加减计数、开关一两个 GPIO/继电器，即可返回。
- **复杂逻辑放主循环**：JSON 解析、SHA1 验签、通行策略、云协议组帧、长延时、`printf` 大段日志等，**不要**放进 ISR。
- **典型模式**：中断只表示「一帧到了 / 定时到了」→ 置 `*_Complete` 或业务标志 → `app_poll()` 里再处理。

本工程整体符合「裸机轮询 + 中断收数/定时」：中断负责时效性，主循环负责业务厚度。

---

## 2. 总览

| 类别 | 向量 / 函数 | 实现位置 | 中断里做什么 | 谁接着干 |
|------|-------------|----------|--------------|----------|
| 内核异常 | HardFault 等 | `stm32f4xx_it.c` | 死循环或空 | — |
| SysTick | `SysTick_Handler` | `stm32f4xx_it.c` | **空**（未作系统滴答） | — |
| 串口 IDLE | USARTx / UARTx | `Hardware/Serial/*.c` | 定帧、置 `*_RX_Complete` | `PCProcess` / `Rs485Process` / `G4GProcess` / `QRProcessUart*` |
| 定时器 | TIM1～5 | `timer.c`（TIM1/2 经 `it.c` 转发） | 关继电器、蜂鸣节拍、置标志、延时关楼层 | 主循环读标志；继电器/楼层在 ISR 内完成时序 |
| EXTI | PB14 LINKA | `it.c` → `4G.c` | 读链路电平、置标志、刷 LED | 主循环看联网/校时相关标志 |

**说明**：部分串口初始化里打开了 DMA 流的 NVIC，但工程中**没有**实现 `DMA*_IRQHandler`；收包成帧依赖 **USART/UART 空闲中断**，不依赖 DMA 完成中断。

---

## 3. 内核与空处理（`User/stm32f4xx_it.c`）

| 处理函数 | 行为 |
|----------|------|
| `NMI_Handler` / `SVC_Handler` / `DebugMon_Handler` / `PendSV_Handler` | 空 |
| `SysTick_Handler` | 空（毫秒节拍实际用 TIM4 的 `g_tim4_ms_tick`） |
| `HardFault_Handler` / `MemManage_Handler` / `BusFault_Handler` / `UsageFault_Handler` | `while (1)` |

`it.c` 中明确转发的外设向量：

- `EXTI15_10_IRQHandler` → `GM4G_LinkA_Pin14_EXTI_Handler`
- `TIM1_UP_TIM10_IRQHandler` → `TIM1_Update_ISR`
- `TIM2_IRQHandler` → `TIM2_Update_ISR`

TIM3 / TIM4 / TIM5 的 `*_IRQHandler` 定义在 `Hardware/Board/timer.c`（不经过 `it.c` 转发）。

---

## 4. 串口空闲中断：收满一帧就举手

共性流程（USART3 实现更严，步骤更多，语义相同）：

```text
检测到 IDLE
  → 清除 IDLE 标志（读 SR/DR 或 ReceiveData）
  → 用 DMA 剩余计数算出本次字节数
  → 置 *_RX_Complete = 1（必要时拷到工作缓冲）
  → 重新装载并启动 DMA
  → 返回
```

| ISR | 文件 | 通道 | 完成标志（概念） |
|-----|------|------|------------------|
| `USART1_IRQHandler` | `usart1.c` | PC 调试 | `USART_RX_Complete` |
| `USART2_IRQHandler` | `usart2.c` | RS485 | `RS485_RX_Complete` |
| `USART3_IRQHandler` | `usart3.c` | 4G 模组 | `USART3_RX_Complete`（先停 DMA 再算长、拷 `USART3_RX_BUF2`） |
| `UART4_IRQHandler` | `uart4.c` | 读头 1 | `UART4_RX_Complete` |
| `UART5_IRQHandler` | `uart5.c` | 读头 2 | `UART5_RX_Complete` |

**符合短 ISR**：此处不做 QR/云业务解析。

---

## 5. 定时器中断

### 5.1 TIM1 / TIM2 — 继电器自动释放

| 函数 | 作用 |
|------|------|
| `TIM1_Update_ISR` | 累计溢出次数，达到 `rly_1_time` 对应时长后 `Rly_Set1(0)`，关 TIM1 |
| `TIM2_Update_ISR` | 同理释放继电器 2，关 TIM2 |

开门吸合后由定时器保证超时释放，避免主循环卡住导致门一直开着。逻辑短，留在 ISR 合理。

### 5.2 TIM3 — 周期业务标志（校时 / 限时检查）

约按工程配置的节拍累计：

- 4G 链路有效且尚未更新时：置 `updateflag`、`gettimeflag`（推动校时相关流程）
- 约每 60s：置 `check_limit_time_flag`
- 约 24h：清 `updateflag`，允许再次进入更新节奏

ISR 内**只置位**，具体校时/查限时在主循环。

### 5.3 TIM4 — 约 10ms 节拍：时间基准 + 蜂鸣 + LED 标志

- `g_tim4_ms_tick += 10`（软件毫秒基准）
- 蜂鸣：短鸣次数（`Bsp_SetBeep`）或 `beepAlarm` 长短码模式，驱动 `beepout`
- 约 1s：置 `led_flag`（主循环闪灯等）

仍属短时序控制，无协议解析。

### 5.4 TIM5 — 电梯限层延时关闭

按约 5ms 节拍累计；到阈值（代码中 `ctrl_time >= 1000`）后：

- 取当前限位位图，按规则对相应楼层 `Floor_Off`
- 关闭 TIM5

属于「到点执行一组 GPIO/楼层关断」。位图遍历固定很短（5 字节），可接受；若日后逻辑变重，可改为只置标志、主循环关层。

---

## 6. EXTI：4G 链路指示（PB14）

`EXTI15_10_IRQHandler` 仅处理 `EXTI_Line14`：

```text
清挂起 → GM4G_LinkA_Pin14_EXTI_Handler()
         → 读 PB14 电平 → g_gprs_linka_level / g_gprs_linka_changed
         → 刷新链路 LED
         → online_count++
```

不在中断里做 AT 指令或 MQTT。

---

## 7. 与主循环的分工（简图）

```text
                    ┌─────────────────────────┐
  读头/4G/RS485/PC  │  UART IDLE ISR          │  置 RX_Complete
  ─────────────────►│  定帧 + 重启 DMA         │──────► app_poll 各 *Process
                    └─────────────────────────┘         解析 / 通行 / 上报

                    ┌─────────────────────────┐
  TIM3/4 标志       │  短 ISR 置位 / 蜂鸣节拍  │──────► 主循环读 flag
  TIM1/2/5 时序     │  关继电器 / 关楼层       │  （部分动作已在 ISR 完成）
                    └─────────────────────────┘

                    ┌─────────────────────────┐
  PB14 边沿         │  EXTI：记链路状态        │──────► 联网/校时相关逻辑
                    └─────────────────────────┘
```

---

## 8. 新增或修改中断时的检查清单

1. 这条路径能否在几十～数百微秒量级返回？（视主频与负载而定，以「明显短于业务函数」为准）  
2. 是否引入了阻塞等待、长循环、动态分配、大段字符串处理？有则移出。  
3. 与主循环共享的变量是否 `volatile`，多字节数据是否有「中断写、主循环读」的一致性约定？  
4. 串口：只定帧；解析放 `*Process`。  
5. 不要依赖未实现的 `DMA*_IRQHandler`；成帧继续用 IDLE，除非同时补齐 DMA ISR 设计。  
6. TIM1 与 TIM10 共用 `TIM1_UP_TIM10_IRQn`：不要再给 TIM10 开 Update 中断抢同一向量（见 `timer.h` 注释）。

---

## 9. 源码索引

| 文件 | 内容 |
|------|------|
| `User/stm32f4xx_it.c` | 异常、EXTI15_10、TIM1/TIM2 向量入口 |
| `Hardware/Board/timer.c` | TIM1～5 逻辑与 TIM3/4/5 Handler |
| `Hardware/Serial/usart1.c`～`uart5.c` | 各口 IDLE ISR |
| `Hardware/Modem/4G.c` | `GM4G_LinkA_Pin14_EXTI_Handler` |
