# STM32 中断与外部中断说明

> **相关文档**
>
> - [文件说明.md](./文件说明.md) — Embedded 目录索引
> - [STM32-GPIO说明.md](./STM32-GPIO说明.md) — GPIO 模式（上拉输入等）
> - [STM32外设说明.md](./STM32外设说明.md) — AFIO / EXTI / NVIC 总览
> - [STM32短期入门规划.md](./STM32短期入门规划.md) — 中断阶段日程
> - 工程示例：[CountSensor.c](../project/Hardware/CountSensor.c)、[Encoder.c](../project/Hardware/Encoder.c)

更新时间：2026-07-14

---

## 一、什么是中断

**中断**（Interrupt）：CPU 正在执行主程序时，某个事件突然需要处理，CPU **暂停当前工作**，去跑一段专门的函数（**中断服务函数 / IRQHandler**），处理完再 **回到原来断点继续**。


| 对比  | 轮询（Polling） | 中断（Interrupt）     |
| --- | ----------- | ----------------- |
| 做法  | 主循环不停读引脚    | 事件发生时硬件通知 CPU     |
| 优点  | 逻辑简单        | 不浪费时间空等；响应快       |
| 缺点  | 大量时间在空转读 IO | 要配置通道、优先级；ISR 要写好 |


**生活类比**：

- **轮询**：每隔几秒就去门口看有没有人敲门  
- **中断**：门铃响了再开门；门铃没响就继续干别的

**记忆口诀**：**事件敲门 → 进 IRQHandler → 干活要短 → 清标志 → 回去主循环。**

### 1.1 和中断相关的三块硬件

```text
GPIO 引脚边沿
    ↓
  EXTI（外部中断线）
    ↓
  NVIC（中断控制器，管优先级与使能）
    ↓
  CPU 执行 EXTI0_IRQHandler 等
```


| 模块             | 作用             |
| -------------- | -------------- |
| **EXTI**       | 哪根线、上升沿还是下降沿触发 |
| **NVIC**       | 开不通哪个中断通道、谁更优先 |
| **IRQHandler** | 你写的处理代码        |


---

## 二、外部中断（EXTI）

### 2.1 是什么

**外部中断**：监视指定 **GPIO 引脚** 的电平变化（上升沿 / 下降沿 / 双边沿），变化一发生就进中断。

典型用途：按键、对射管、旋转编码器、红外等。

本工程：


| 模块          | 引脚      | 触发  | 作用    |
| ----------- | ------- | --- | ----- |
| CountSensor | PB14    | 下降沿 | 计数 +1 |
| Encoder     | PB0、PB1 | 下降沿 | 判向增减  |


### 2.2 GPIO 与 Pin：一条线只能选一个口的同号脚

STM32F103 有外部中断线：**EXTI0～EXTI15**。

规则很重要：


| 规则               | 说明                                            |
| ---------------- | --------------------------------------------- |
| **线号 = 引脚号**     | PA0 / PB0 / PC0 … 都接到 **EXTI0**，只能 **选其中一个口** |
| **同号互斥**         | 用了 PB0 做 EXTI0，就 **不能同时** 把 PA0 也配成外部中断       |
| **不同 Pin 可用不同口** | PB0→EXTI0、PB1→EXTI1、PA2→EXTI2，互不抢线            |


```text
                EXTI0 只能接一个
PA0 ──┐
PB0 ──┼──► EXTI0 ──► NVIC
PC0 ──┘     （三选一，本工程选 PB0）

PB1 ─────────► EXTI1 ──► NVIC   （不冲突）
PB14 ────────► EXTI14 ──► NVIC
```

**记忆**：**同数字 Pin 争同一根 EXTI 线；不同数字 Pin 各走各的线。**

### 2.3 中断通道（NVIC IRQ）

EXTI 线触发后，还要通过 **NVIC 通道** 才能进具体函数：


| EXTI 线    | NVIC 通道          | 服务函数名（须拼写正确）                  |
| --------- | ---------------- | ----------------------------- |
| EXTI0     | `EXTI0_IRQn`     | `EXTI0_IRQHandler`            |
| EXTI1     | `EXTI1_IRQn`     | `EXTI1_IRQHandler`            |
| EXTI2     | `EXTI2_IRQn`     | `EXTI2_IRQHandler`            |
| EXTI3     | `EXTI3_IRQn`     | `EXTI3_IRQHandler`            |
| EXTI4     | `EXTI4_IRQn`     | `EXTI4_IRQHandler`            |
| EXTI5～9   | `EXTI9_5_IRQn`   | `EXTI9_5_IRQHandler`（合用，内部分线） |
| EXTI10～15 | `EXTI15_10_IRQn` | `EXTI15_10_IRQHandler`（合用）    |


CountSensor 用 **PB14** → Line14 → `**EXTI15_10_IRQHandler`**，函数里必须再用 `EXTI_GetITStatus(EXTI_Line14)` 判断是不是 14 号线。

### 2.4 电路（常见接法）

外部中断脚一般配成 **上拉输入** 或 **下拉输入**，保证悬空时电平稳定。

**按键（一端 GND，上拉输入）：**

```text
  3.3V
    │
   内部上拉（GPIO_Mode_IPU）
    │
 MCU 引脚 ──── 按键 ──── GND
```

- 松开：高电平  
- 按下：拉到 GND → **下降沿**（`EXTI_Trigger_Falling`）→ 进中断

**对射/遮挡类模块 DO（CountSensor 类）：**

```text
模块 VCC → 3.3V
模块 GND → GND
模块 DO  → PB14（上拉输入）
```

DO 由模块内部比较器驱动；触发边沿以模块手册为准，江协计数实验常用 **下降沿**。

**编码器（两路）：**

```text
编码器 A → PB0 → EXTI0
编码器 B → PB1 → EXTI1
编码器 VCC / GND → 3.3V / GND
```

两相各自占不同 EXTI 线，符合「不同 Pin」规则。

### 2.5 配置步骤

```text
1. 开 GPIOx 时钟 + AFIO 时钟
2. GPIO 配成输入（常 IPU）
3. GPIO_EXTILineConfig：选「哪个口的哪号脚」接到 EXTI 线
4. EXTI_Init：线号、中断模式、上升/下降沿
5. NVIC_Init：开对应 IRQ 通道、设优先级
6. 写 IRQHandler：判线、做事、清挂起标志
```

示意（CountSensor）：

```c
RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
// GPIO_Init: PB14, GPIO_Mode_IPU
GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource14);
// EXTI_Line14, EXTI_Mode_Interrupt, EXTI_Trigger_Falling
// NVIC: EXTI15_10_IRQn
```

---

## 三、NVIC（嵌套向量中断控制器）

**NVIC**（Nested Vectored Interrupt Controller）在 Cortex-M3 内核里，负责：

| 职责 | 一句话 |
|------|--------|
| **使能 / 关闭** | 某个中断通道开不开 |
| **优先级** | 谁先谁后、能不能打断谁 |
| **嵌套** | 高优先级可以打断正在执行的低优先级 ISR |
| **向量跳转** | 中断发生后，CPU 跳进对应的 `xxx_IRQHandler` |

可以把 NVIC 理解成 **中断调度大门**：

```text
EXTI / 定时器 / 串口 … 产生请求
           ↓
         NVIC（允不允许、谁更急）
           ↓
      对应 IRQHandler
```

EXTI 只解决「哪根脚、哪个边沿」；**进不进你的函数、谁优先**，由 **NVIC** 决定。  
所以配置外部中断时，在 `EXTI_Init` 之后一定还有 **`NVIC_Init`**。

### 3.1 中断通道（IRQ Channel）

每个可中断外设在 NVIC 里占一个（或共享一个）**通道**，名字以 `_IRQn` 结尾。

外部中断与通道对应关系见 **§2.3**。其它常见例子：

| 外设事件 | NVIC 通道 | 服务函数 |
|----------|-----------|----------|
| EXTI0 | `EXTI0_IRQn` | `EXTI0_IRQHandler` |
| EXTI10～15 | `EXTI15_10_IRQn` | `EXTI15_10_IRQHandler` |
| USART1 | `USART1_IRQn` | `USART1_IRQHandler` |
| TIM2 | `TIM2_IRQn` | `TIM2_IRQHandler` |

**通道打开了，函数名还要和启动文件里的一致**，否则 NVIC 跳过去也进不了你的代码。

### 3.2 优先级分组

Cortex-M 用若干 bit 表示优先级，STM32F103 常用 **4 bit**，先通过 **优先级分组** 决定：多少 bit 表示 **抢占优先级**，多少 bit 表示 **响应（子）优先级**。

江协与本工程常用：

```c
NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
// Group_2：抢占优先级 2 bit（0～3），子优先级 2 bit（0～3）
```

| 概念 | 作用 |
|------|------|
| **抢占优先级**（Preemption） | 数值 **更小** 的可以 **打断** 正在跑的、抢占更大的 ISR（嵌套） |
| **子优先级**（Sub） | 抢占相同、两个请求同时挂起时，子优先级小的先执行；**不能**打断对方 |

**记忆**：先比抢占，再比子优先级；**数字越小越优先**。

### 3.3 `NVIC_Init` 配置项

```c
NVIC_InitTypeDef NVIC_InitStructure;
NVIC_InitStructure.NVIC_IRQChannel = EXTI15_10_IRQn;           // 选哪个通道
NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;                // 使能该通道
NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;      // 抢占优先级
NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;             // 子优先级
NVIC_Init(&NVIC_InitStructure);
```

编码器示例：`EXTI0` 与 `EXTI1` 抢占可同为 1，子优先级分别设 1、2，表示同一时刻争用时有先后，但一般仍按沿发生顺序处理。

### 3.4 嵌套直观理解

```text
主循环
  └─ 低优先级 ISR（抢占=2）正在跑
        └─ 高优先级中断来了（抢占=0）
              → 打断低优先级 ISR
              → 跑完高优先级再回来继续低优先级
              → 再回主循环
```

入门阶段多数实验优先级相近、ISR 很短，**很少真正嵌套**；先会 **开通道 + 写对函数名 + 清标志** 即可。

### 3.5 和 EXTI 的分工

| 步骤 | 谁管 |
|------|------|
| 选脚、边沿 | GPIO + AFIO + **EXTI** |
| 开不通、优先级、进哪个 Handler | **NVIC** |
| Handler 里判线、改变量、清 EXTI 标志 | 你的代码 |

**口诀**：**EXTI 产生请求，NVIC 批准进场。**

---

## 四、AFIO（复用 IO）

名字是 **AFIO**（Alternate Function I/O）。  
外部中断选脚、引脚功能重映射，都走 **AFIO**，所以用 EXTI 前要开 AFIO 时钟：

```c
RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
```

### 4.1 复用引脚功能重映射（Remap）

部分外设默认脚可以 **整组改到其它脚**，例如 USART1、I2C1、TIM 等。

| 操作 | 库函数（概念） |
|------|----------------|
| 打开 AFIO 时钟 | `RCC_APB2PeriphClockCmd(..., AFIO)` |
| 开启某外设重映射 | `GPIO_PinRemapConfig(GPIO_Remap_xxx, ENABLE)` |

重映射后，GPIO 仍要按新脚的复用功能重新初始化。入门阶段（点灯、按键中断）往往 **不必 Remap**；串口改脚、定时器改脚时再查表。

### 4.2 中断引脚选择（EXTI 线路由）

这是入门 **必须会** 的 AFIO 用途：

```c
GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource14);
//                   ↑ 选端口 B                 ↑ 选 14 号脚
// 结果：EXTI14 监听的是 PB14，而不是 PA14 / PC14
```

| 参数 | 含义 |
|------|------|
| `GPIO_PortSourceGPIOx` | 选 A/B/C… 哪个口 |
| `GPIO_PinSourcey` | 选 0～15 哪根脚 → 对应 EXTIy |

**和 §2.2 合在一起记**：AFIO 负责把「某口的某脚」接到「同号 EXTI 线」；同号脚多个口里只能选一个。

---

## 五、写中断服务函数的三点原则

### 5.1 简短、快速

IRQHandler 里 **不要**：

- `Delay_ms`、大循环等待  
- 复杂 printf、刷整屏 OLED  
- 耗时运算、开其它长时间外设事务

ISR 占着 CPU 时，主循环和其它中断都受影响。耗时活放到主循环。

### 5.2 不冲突

| 注意点 | 做法 |
|--------|------|
| **函数名写对** | 必须是 `EXTI0_IRQHandler`，写错（如 `ERTI0_...`）则永不进入 |
| **合用通道先判线** | `EXTI15_10` 里用 `EXTI_GetITStatus(EXTI_Line14)` |
| **必须清标志** | `EXTI_ClearITPendingBit(...)`，否则会反复进中断 |
| **同号 EXTI 不抢脚** | 不要 PA0、PB0 同时当 EXTI0 |
| **优先级合理** | 见 **§三 NVIC**：抢占小的可打断抢占大的 |

### 5.3 中断里主要操作「变量 / 标志位」

推荐模式：

```text
中断里：Count++ 或 Flag = 1（几条语句）
主循环：if (Flag) { 真正业务；Flag = 0; }
```

| 做法 | 说明 |
|------|------|
| 改全局计数 / 标志 | CountSensor 的 `CountSensor_Count++` |
| 主循环读走、清零 | Encoder 的 `Encoder_Get()` 取增量 |
| 少在 ISR 里刷屏、延时 | 显示、通信放 `while(1)` |

**记忆口诀**：**中断记账，主循环干活。**

---

## 六、完整对照：CountSensor

```text
遮挡/脉冲 → PB14 下降沿
    → EXTI14
    → NVIC EXTI15_10（通道使能 + 优先级）
    → EXTI15_10_IRQHandler
         判 Line14
         CountSensor_Count++
         清挂起标志
主循环 OLED 显示 CountSensor_Get()
```

---

## 七、自检

1. 轮询和中断的本质区别？  
   → 轮询主动反复查；中断事件来了才打断 CPU。

2. PA0 和 PB0 能同时做外部中断吗？  
   → **不能**，都争 **EXTI0**。

3. EXTI 和 NVIC 各管什么？  
   → EXTI：线与边沿；NVIC：使能、优先级、进哪个 Handler。

4. 抢占优先级数字越小意味着什么？  
   → **越优先**，可以打断抢占优先级更大的 ISR。

5. 为什么配置 EXTI 要开 AFIO 时钟？  
   → 选「哪个口接到 EXTI 线」由 **AFIO** 的 `GPIO_EXTILineConfig` 完成。

6. PB14 的中断函数叫什么？为什么不是 `EXTI14_IRQHandler`？  
   → **`EXTI15_10_IRQHandler`**，10～15 共用一个通道。

7. 中断里为什么要 `ClearITPendingBit`？  
   → 不清除会认为事件一直有效，**反复进中断**。

8. 中断函数里适合 `Delay_ms(1000)` 吗？  
   → **不适合**；只改标志/计数，延时放主循环。

---

## 延伸阅读

| 资料 | 内容 |
|------|------|
| RM0008 EXTI / AFIO 章节 | 中断线、EXTICR 寄存器 |
| PM0056 / Cortex-M3 编程手册 | NVIC、优先级分组 |
| [STM32外设说明.md](./STM32外设说明.md) §4.3～4.5 | AFIO、EXTI、NVIC |
| 江协科技「外部中断」章节 | 与本工程代码对齐 |
| [STM32短期入门规划.md](./STM32短期入门规划.md) | 第 2 周中断日程 |


