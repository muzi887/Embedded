# STM32 定时器 Timer 说明

> **相关文档**
>
> - [文件说明.md](./文件说明.md) — Embedded 目录索引
> - [STM32外设说明.md](./STM32外设说明.md) — TIM 外设总览 §6.2
> - [STM32-中断与外部中断说明.md](./STM32-中断与外部中断说明.md) — NVIC / IRQHandler
> - [STM32-GPIO说明.md](./STM32-GPIO说明.md) — 复用推挽（PWM）、上拉输入（捕获）
> - [STM32F103C8T6引脚说明.md](./STM32F103C8T6引脚说明.md) — TIMx 通道引脚
> - 工程示例：[Timer.c](../project/System/Timer.c)、[PWM.c](../project/Hardware/PWM.c)、[Encoder2.c](../project/Hardware/Encoder2.c)
> - 实验精读：[STM32-实验9-PWM输出讲解.md](./STM32-实验9-PWM输出讲解.md) — 正点原子 PWM 呼吸灯结合代码

更新时间：2026-07-17

---

## 一、总览：定时器在干什么

**定时器（Timer / TIM）** 是芯片内的 **硬件计数器**：时钟驱动 **CNT** 走数，到条件后产生更新中断、PWM、捕获等，几乎不占 CPU。

| 类型 | 举例 | 特点 |
|------|------|------|
| **基本定时器** | TIM6、TIM7 | 只有时基；无 GPIO 通道 |
| **通用定时器** | TIM2～TIM5 | 时基 + PWM + 输入捕获 + 编码器 |
| **高级定时器** | TIM1、TIM8 | 另有死区、互补输出（电机） |

本工程入门多用 **TIM2 / TIM3**。

### 1.1 一条主线（先建立图景）

```text
① 选时钟源 ──► 得到 CK_PSC
② 时基 PSC/ARR/CNT ──► 定「多快数、数多少算一轮」
        │
        ├──► ③ 更新中断（到点打断 CPU）
        ├──► ④ 输出比较 / PWM（往引脚吐波形，靠 CCR）
        └──► ⑤ 输入捕获 / 编码器（从引脚量时间/位置，靠 CCR）
```

| 顺序 | 章节 | 解决什么 |
|------|------|----------|
| 1 | **二、时钟源** | CK_PSC 从哪来 |
| 2 | **三、时基与公式** | PSC、ARR、频率怎么算 |
| 3 | **四、更新中断** | 计满一轮如何进 IRQ |
| 4 | **五、OC / CCR / PWM** | 往外吐波形、调占空比 |
| 5 | **六、IC / 编码器** | 往里量脉宽、位置 |

**口诀**：**先定拍从哪来，再定数多快；中断管节拍，CCR 管通道吐/收。**

![[attachments/stm32-general-timer-block-diagram.png]]

---

## 二、时钟源：CK_PSC 从哪来

**时钟源**决定 **CK_PSC**（送进预分频器的时钟）。PSC/ARR 只决定「数多少拍算一轮」——**先选时钟源，再配时基**。

| 时钟源 | 拍从哪来 | 典型用途 |
|--------|----------|----------|
| **内部时钟** | APB 定时器时钟 → **CK_PSC**（F103 常见 **72 MHz**） | 毫秒定时、PWM、舵机 |
| **外部时钟模式 1** | 通道脚 TIx 边沿 | 外部脉冲计数 |
| **外部时钟模式 2** | **ETR** 引脚脉冲 | 对射管、脉冲计数（本工程 `Timer.c`） |
| **内部触发 ITR** | 其它定时器 | 级联（进阶） |

```text
内部：APB 定时器时钟 ──► CK_PSC ──► PSC ──► CK_CNT ──► CNT
外部：引脚脉冲 ──►（模式1/2）──► 往往直接推 CNT（或经定时器时钟树，入门先记「每脉冲 CNT+1」）
```

| 你想要… | 选 |
|---------|----|
| 固定时间（1 ms、50 Hz PWM） | **内部时钟** + 算 PSC/ARR |
| 数外部脉冲、满 N 次做事 | **外部时钟** + `ARR = N−1` |
| PWM / 舵机 | **内部时钟** + 第五章 OC |

| 函数 | 作用 |
|------|------|
| `TIM_InternalClockConfig` | 内部时钟（多数例程可省略，默认内部） |
| `TIM_ETRClockMode2Config` | 外部时钟模式 2（ETR） |
| `TIM_TIxExternalClockConfig` | 外部时钟模式 1 |
| `TIM_GetCounter` | 读 CNT（外部时钟下≈脉冲累计） |

**注意**：外部时钟下「更新中断」表示 **脉冲个数到点**，不是墙上时钟的 1 ms。机械开关脉冲宜滤波/消抖。

---

## 三、时基单元与统一公式

时基三件套是中断、PWM、捕获的共同底座：

| 概念 | 简称 | 作用 |
|------|------|------|
| 预分频器 | **PSC** | 把 **CK_PSC** 除频得到 **CK_CNT** |
| 自动重装载 | **ARR** | CNT 从 0 计到 ARR 后溢出并重装 |
| 计数器 | **CNT** | 当前计数值；按 **CK_CNT** 节拍加一 |

### 3.1 CK_PSC 与 CK_CNT

```text
        CK_PSC     （进 PSC；频率公式分子只用它）
           │
        PSC 分频
           │
        CK_CNT     （驱动 CNT 加一）
           │
    CNT：0 → … → ARR → 溢出/更新
```

| 名称 | 含义 |
|------|------|
| **CK_PSC** | 进预分频器的时钟（Hz）；内部时钟时常 **72 MHz** |
| **CK_CNT** | `CK_PSC / (PSC + 1)`，CNT 的计数节拍 |

本文件不用 `f_CK` 等别名；PPT 上的 **CK_PSC** 即此处。

### 3.2 统一公式

寄存器存「实际值 − 1」，公式里一律 **+1**：

```text
CK_CNT = CK_PSC / (PSC + 1)

f = CK_PSC / (PSC + 1) / (ARR + 1)     ← 更新或 PWM 频率（Hz）
T = 1 / f
  = (PSC + 1) × (ARR + 1) / CK_PSC     ← 一轮周期（s）
```

例：`PSC = 72-1`，`ARR = 1000-1`，`CK_PSC = 72 MHz`

```text
CK_CNT = 72M / 72 = 1 MHz
f      = 72M / 72 / 1000 = 1 kHz
T      = 1 ms
```

**口诀**：**分子永远是 CK_PSC；先 ÷(PSC+1)，再 ÷(ARR+1)。**

![[attachments/stm32-timer-timebase-overview.png]]

### 3.3 配置时基（内部时钟示例）

```c
TIM_InternalClockConfig(TIM2);
TIM_TimeBaseInitStructure.TIM_Prescaler = 72 - 1;   // PSC
TIM_TimeBaseInitStructure.TIM_Period = 1000 - 1;    // ARR → 1 ms @ 72 MHz
TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);
```

外部时钟示例见 [Timer.c](../project/System/Timer.c)：`TIM_ETRClockMode2Config` + `ARR = 10-1`（每 10 个脉冲更新一次）。

---

## 四、更新中断（定时中断）

**更新中断**：产生 **更新事件（UEV）**（向上计数即 CNT 计满 ARR）后进 `TIMx_IRQHandler`。

| | 空循环 `delay` | 更新中断 |
|--|----------------|----------|
| CPU | 空转等待 | 到点才打断，其余可干别的 |
| 含义 | 软延时 | 内部时钟≈时间到；外部时钟≈脉冲数到 |

与 EXTI 不同：EXTI 看 **GPIO 边沿**；这里看 **计数更新**。

### 4.1 配置流程

```text
1 开 TIMx 时钟
2 选时钟源（第二章）
3 配时基 PSC/ARR（第三章）
4 清更新标志（避免一启动就进中断）
5 TIM_IT_Update + NVIC
6 TIM_Cmd(ENABLE)
   → 更新 → IRQ：判标志 → 简短处理 → 清挂起位
```

| 目标 | 时钟源 | 参考 |
|------|--------|------|
| 固定时间节拍（如 1 ms） | 内部 | 第三章公式 + 本章步骤 |
| 脉冲计满再中断 | 外部模式 2 | [Timer.c](../project/System/Timer.c) |

**不要**把 `Timer.c` 当成「内部 1 ms」模板——它是 **外部脉冲 + 更新中断**。

### 4.2 关键函数与 ISR

| 函数 | 作用 |
|------|------|
| `TIM_TimeBaseInit` | PSC、ARR |
| `TIM_ITConfig(..., TIM_IT_Update, ENABLE)` | 更新中断 |
| `NVIC_Init` | `TIMx_IRQn` |
| `TIM_GetITStatus` / `TIM_ClearITPendingBit` | 判 / 清标志 |
| `TIM_Cmd` | 启停 |

```c
void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) == SET)
    {
        /* 置标志等，保持简短 */
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    }
}
```

**原则**：ISR 少干活；复杂逻辑放主循环。典型用途：1 ms 节拍、软件定时器、按键扫描、脉冲满 N 报警。

---

## 五、通道输出：OC、CCR、PWM

时基解决「数多快」；**通道**解决「跟引脚怎么互动」。每个通道有一份 **CCR**。

### 5.1 CCR（Capture/Compare Register）

**CCR** = 捕获/比较寄存器：通道 1→**CCR1**，通道 2→**CCR2** …

| 用法 | 方向 | 软件 | 作用 |
|------|------|------|------|
| **比较（OC/PWM）** | MCU → 引脚 | **写** CCR | 与 CNT 比较，定翻转点 / 脉宽 |
| **捕获（IC）** | 引脚 → MCU | **读** CCR | 边沿时把 CNT **锁存**进 CCR |

```text
CK_PSC → PSC → CNT ──┬── 与 CCRx 比较 → 通道脚（PWM）
                     └── 边沿到来：CNT → CCRx（捕获）
```

| 寄存器 | PWM 时管什么 |
|--------|----------------|
| **ARR** | 周期 / 频率 |
| **CCR** | 脉宽 / 占空比：`Duty ≈ CCR / (ARR + 1)` |

| 操作 | API |
|------|-----|
| 初始化写 CCR | `TIM_OCInitStructure.TIM_Pulse` |
| 运行中改占空比 | `TIM_SetCompare1`～`4` |
| 读捕获值 | `TIM_GetCapture1`～`4` |

例：`TIM_SetCompare2(TIM3, led0pwmval)` → 改 **CCR2**（实验 9 呼吸灯）。

**口诀**：**CCR：PWM 写、捕获读；通道几用 SetCompare几。**

![[attachments/stm32-timer-cc-output-channel1.png]]

### 5.2 输出比较 OC

**输出比较**：CNT 与 CCR 满足关系时，硬件改引脚或产生比较中断/DMA。PWM 是 OC 的一种模式。

### 5.3 PWM

**PWM**（脉冲宽度调制）：固定周期内调高电平占比，等效调平均电压/能量。

```text
|<------------- T_period（由 ARR）------------>|
|████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░|
  t_high（由 CCR）
```

```text
f_PWM = CK_PSC / (PSC + 1) / (ARR + 1)     ← 与第三章同一公式
Duty  ≈ CCR / (ARR + 1)
```

| 模式 | 含义 |
|------|------|
| **PWM1** | CNT < CCR → 有效电平；否则无效（再由极性定高低） |
| **PWM2** | 有效/无效与 PWM1 相反 |

例：

| 场景 | 参数 | 结果 |
|------|------|------|
| 本工程舵机 [PWM.c](../project/Hardware/PWM.c) | PSC=71，ARR=19999，CK_PSC=72M | `f = 72M/72/20000 = 50 Hz`，CH3→PA2 |
| 实验 9 | `TIM3_PWM_Init(899,0)` | `f = 72M/1/900 = 80 kHz`，CH2→PB5（重映射） |

**口诀**：**ARR 定周期，CCR 定脉宽。**

![[attachments/stm32-pwm-basic-structure.png]]

![[attachments/stm32-pwm-freq-duty-resolution.png]]

### 5.4 PWM 配置五步

```text
1 开 TIMx（及 GPIO）时钟
2 时基：内部时钟 + PSC/ARR → 定 f_PWM
3 OC：模式（PWM1/2）、TIM_Pulse(CCR)、极性、输出使能
4 GPIO：通道脚 = 复用推挽 AF_PP（不是普通 Out_PP）
5 TIM_Cmd；之后用 SetCompare 改占空比
```

高级定时器另需 `TIM_CtrlPWMOutputs`（MOE）。GPIO 写在 OC 前或后均可。

| 函数 | 作用 |
|------|------|
| `TIM_OCStructInit` / `TIM_OCxInit` | 配通道 |
| `TIM_SetComparex` | 改 CCR |
| `TIM_OCPreloadConfig` | CCR 预装载，换 duty 更平滑 |
| `TIM_PrescalerConfig` | 运行中改 PSC（改频率） |

| 本工程 | 对应 |
|--------|------|
| `PWM_Init` | 内部时钟 + 时基 + OC3 PWM1 |
| `PWM_SetCompare3` | `TIM_SetCompare3` |

应用：LED 呼吸、电机调速、舵机（~50 Hz，脉宽约 0.5～2.5 ms）、蜂鸣器音调。

---

## 六、输入捕获 IC 与编码器

**输入捕获**：选定边沿到来时，硬件把 **CNT 锁存进 CCR**，可读时间戳，再算脉宽/周期/频率。

| | OC / PWM | IC |
|--|----------|-----|
| 方向 | MCU → 引脚 | 引脚 → 锁存 CNT |
| CCR | **写** | **读** |
| GPIO | AF_PP 输出 | 浮空/上拉 **输入** |

![[attachments/stm32-timer-cc-input-channel1.png]]

![[attachments/stm32-timer-input-capture-reset.png]]

### 6.1 测脉宽思路

```text
1 内部时钟 + 时基（T_tick = (PSC+1) / CK_PSC）
2 TIM_ICInit / TIM_PWMIConfig
3 GPIO 输入
4 TIM_Cmd
5 边沿1 → CCR=t1；边沿2 → CCR=t2
6 脉宽 ≈ (t2 − t1) × T_tick   （跨 ARR 须累计溢出）
```

### 6.2 编码器接口

硬件用 A/B 相（TI1/TI2）解码方向与脉冲，**CNT 表示位置**（不是普通「测一次边沿」）。见 [Encoder2.c](../project/Hardware/Encoder2.c)。

```text
GPIO 上拉 → IC 滤波 → TIM_EncoderInterfaceConfig → TIM_Cmd
→ TIM_GetCounter 读增量
```

![[attachments/stm32-timer-encoder-interface.png]]

![[attachments/stm32-timer-encoder-mode-timing.png]]

![[attachments/stm32-timer-pwm-input-mode.png]]

### 6.3 函数与注意

| 函数 | 作用 |
|------|------|
| `TIM_ICInit` / `TIM_PWMIConfig` | 捕获 / PWM 输入 |
| `TIM_GetCapturex` | 读 CCR |
| `TIM_ITConfig(..., TIM_IT_CCx, …)` | 捕获中断 |
| `TIM_EncoderInterfaceConfig` | 编码器 |

1. 间隔超过一个 ARR 周期必须处理 **溢出**。  
2. 引脚须是该定时器通道的复用输入。  
3. **同一通道不能同时既做 OC 又做 IC**。

应用：测频/脉宽、红外、超声波、编码器、光电测速。

---

## 七、与本工程对照

| 能力 | 文件 | 要点 |
|------|------|------|
| 外部时钟 + 更新中断 | `System/Timer.c` | ETR + `TIM_IT_Update`；**不是**内部 1 ms 模板 |
| PWM | `Hardware/PWM.c` | 内部时钟；PWM1；`SetComparex` |
| 编码器 | `Hardware/Encoder2.c` | `TIM_EncoderInterfaceConfig` |

---

## 八、常见问题

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| 进不了定时中断 | 未开 IT / NVIC / `TIM_Cmd` | 逐项核对 |
| 一启动就进中断 | 更新标志未清 | `TimeBaseInit` 后 `ClearFlag(Update)` |
| 以为是 1 ms 却对不上 | 实际用了外部时钟 | 先确认时钟源 |
| PWM 无波形 | 非 AF_PP；通道与引脚不符；高级定时器无 MOE | 对脚、对 `OCxInit` |
| 占空比不对 | CCR/ARR 关系错；写错通道 | Duty≈CCR/(ARR+1)；CH3 用 `SetCompare3` |
| 捕获乱跳 | 未处理溢出；滤波不足 | 计溢出；加大滤波；共地 |
| 外部时钟不计数 | 极性/引脚/上拉错 | 查 ETR、示波器 |

---

## 九、自检

1. 学习顺序为何是「时钟源 → 时基 → 中断 → 通道」？  
   → 先有 CK_PSC，才能算 PSC/ARR；通道挂在 CNT 上。  
2. 频率公式分子是谁？完整公式？  
   → **CK_PSC**；`f = CK_PSC/(PSC+1)/(ARR+1)`。  
3. CK_CNT 与 CK_PSC 关系？  
   → `CK_CNT = CK_PSC/(PSC+1)`。  
4. CCR 全称？PWM / 捕获各怎么用？  
   → Capture/Compare Register；PWM **写**，捕获 **读**。  
5. ARR 与 CCR 在 PWM 里各管什么？  
   → ARR 周期；CCR 占空比。  
6. `Timer.c` 是 1 ms 内部定时吗？  
   → 否；外部脉冲计满 ARR+1。  
7. PWM 脚为何必须 AF_PP？  
   → 由定时器外设驱动，不是普通 GPIO 推挽。  
8. 编码器主要配置函数？  
   → `TIM_EncoderInterfaceConfig`。

---

## 延伸阅读

| 资料 | 内容 |
|------|------|
| [STM32外设说明.md](./STM32外设说明.md) §6.2 | TIM 总览 |
| [STM32-中断与外部中断说明.md](./STM32-中断与外部中断说明.md) | NVIC / ISR |
| [STM32F103C8T6引脚说明.md](./STM32F103C8T6引脚说明.md) | TIMx_CHx / ETR |
| STM32F10x 参考手册 TIM 章 | 模式与寄存器 |
| 江协科技 STM32 教程 | 定时中断、PWM、输入捕获、编码器 |
| `docs/attachments/stm32-*-timer-*.png`、`stm32-pwm-*.png` | 框图与波形 |

---

*文档随课程与工程 Timer / PWM / Encoder 进度补充。*
