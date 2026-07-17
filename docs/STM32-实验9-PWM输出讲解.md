# 实验 9 PWM 输出 — 逻辑讲解（结合代码）

> **相关文档**
>
> - [STM32-定时器Timer说明.md](./STM32-定时器Timer说明.md) — 时钟源、时基公式、CCR、PWM 五步
> - 工程目录：[实验9 PWM输出实验/](../实验9%20PWM输出实验/)
> - 对照本仓库江协风封装：[PWM.c](../project/Hardware/PWM.c)

更新时间：2026-07-17

---

## 一、这个实验在干什么

**目标**：用 **TIM3 通道 2** 在战舰板 **LED0（PB5）** 上输出 PWM，主循环不断改 **CCR2**，灯由暗→亮→暗（呼吸灯）。

```text
硬件自己吐 80 kHz 方波（频率由 PSC/ARR 定死）
主循环每 10 ms 改一次 CCR2（占空比变 → 平均亮度变）
     ↑
  你眼睛看到的「慢慢变亮/变暗」
```

| 项 | 本实验取值 |
|----|------------|
| 板子 | ALIENTEK 战舰 STM32F103 |
| 定时器 / 通道 | **TIM3 / CH2** |
| 引脚 | **PB5**（部分重映射后）= LED0 |
| 频率 | `CK_PSC/(PSC+1)/(ARR+1) = 72M/1/900 = 80 kHz` |
| 调光 | `TIM_SetCompare2` 改 CCR2，0～300 来回扫 |

理论对应：[定时器说明](./STM32-定时器Timer说明.md) **第五章 OC / CCR / PWM**。

---

## 二、先看懂 `main`：谁配频率、谁改亮度

```12:30:实验9 PWM输出实验/USER/main.c
 int main(void)
 {		
 	u16 led0pwmval=0;
	u8 dir=1;	
	delay_init();
	NVIC_Configuration();
	uart_init(9600);
 	LED_Init();
 	TIM3_PWM_Init(899,0);	 // PWM 频率 ≈ 72M/900 = 80 kHz
   	while(1)
	{
 		delay_ms(10);	 
		if(dir)led0pwmval++;
		else led0pwmval--;
 		if(led0pwmval>300)dir=0;
		if(led0pwmval==0)dir=1;										 
		TIM_SetCompare2(TIM3,led0pwmval);		   
	}	 
 }
```

### 2.1 变量在干什么

| 变量 | 含义 |
|------|------|
| `led0pwmval` | 要写入的 **CCR2**（脉宽比较值） |
| `dir` | 扫描方向：1 递增（变亮），0 递减（变暗） |

### 2.2 主循环逻辑（呼吸）

```text
每 10 ms：
  led0pwmval 按 dir +1 或 -1
  到 >300 → dir=0（改往下减）
  到 ==0  → dir=1（改往上加）
  TIM_SetCompare2(TIM3, led0pwmval)  → 硬件立刻按新占空比吐波
```

```text
CCR2:  0 ──► 300 ──► 0 ──► 300 …
亮度:  暗 ──► 亮 ──► 暗 …
```

占空比大约：

```text
Duty ≈ CCR2 / (ARR+1) = led0pwmval / 900
```

最大约 `300/900 ≈ 33%`，够做呼吸演示，不必扫满 899。

### 2.3 和「GPIO 翻转闪灯」的差别

| | 普通闪灯 | 本实验 PWM |
|--|----------|------------|
| 谁改引脚 | CPU `GPIO_SetBits` | **定时器硬件** 按 CNT/CCR 自动改 |
| 主循环 | 延时 + 翻转 | 只 **改 CCR**，不直接操作 LED 电平 |
| 观感 | 亮/灭两档 | 可连续调「多亮」 |

---

## 三、`TIM3_PWM_Init`：按五步拆代码

调用：`TIM3_PWM_Init(899, 0)` → 形参 **`arr=899`（ARR）**，**`psc=0`（PSC）**。

对应定时器文档五步：**开时钟 → 时基 → OC → GPIO(AF_PP) → TIM_Cmd**（本文件 GPIO 写在时基前，效果一样）。

### 3.1 开时钟 + 重映射到 PB5

```74:83:实验9 PWM输出实验/HARDWARE/TIMER/timer.c
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
 	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB  | RCC_APB2Periph_AFIO, ENABLE);
	
	GPIO_PinRemapConfig(GPIO_PartialRemap_TIM3, ENABLE); // TIM3_CH2 → PB5    
 
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;  // 必须复用推挽
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
```

| 点 | 说明 |
|----|------|
| 为何开 **AFIO** | 要用 **引脚重映射** |
| 默认 TIM3_CH2 | **PA7** |
| 部分重映射后 | **PB5** = 战舰 **LED0** |
| **AF_PP** | PWM 由定时器驱动脚，不能用普通 `Out_PP` |

战舰 `led.h`：`#define LED0 PBout(5)`，所以必须把 CH2 映到 PB5。

### 3.2 时基：定 80 kHz

```86:90:实验9 PWM输出实验/HARDWARE/TIMER/timer.c
	TIM_TimeBaseStructure.TIM_Period = arr;     // ARR = 899
	TIM_TimeBaseStructure.TIM_Prescaler =psc; // PSC = 0
	TIM_TimeBaseStructure.TIM_ClockDivision = 0;
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);
```

统一公式（内部时钟，`CK_PSC = 72 MHz`）：

```text
f_PWM = CK_PSC / (PSC + 1) / (ARR + 1)
      = 72M / (0+1) / (899+1)
      = 72M / 900
      = 80 kHz
```

注释「72000000/900=80Khz」即此。频率固定后，呼吸只靠改 CCR，**不必改 PSC/ARR**。

### 3.3 输出比较：CH2 + PWM2

```92:100:实验9 PWM输出实验/HARDWARE/TIMER/timer.c
	TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM2;
 	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
	TIM_OC2Init(TIM3, &TIM_OCInitStructure);  // 通道 2 → CCR2

	TIM_OC2PreloadConfig(TIM3, TIM_OCPreload_Enable);
	TIM_Cmd(TIM3, ENABLE);
```

| 配置 | 含义 |
|------|------|
| `TIM_OC2Init` | 用的是 **通道 2**，所以 main 里必须 `SetCompare**2**` |
| `PWM2` | 与江协常用 PWM1 有效区间相反；配合极性后仍是「CCR 大则平均更亮/更暗」一类效果，先会调 CCR 即可 |
| `OCPreload_Enable` | CCR 预装载，改占空比更平滑 |
| `TIM_Cmd` | 开始计数，引脚开始出 PWM |

结构体里**没写** `TIM_Pulse` 时，初始 CCR 多为 0，灯先最暗，再由 main 扫起来。

---

## 四、整条数据路径（串起来）

```text
main
  │
  ├─ TIM3_PWM_Init(899, 0)
  │     ├─ 重映射 TIM3_CH2 → PB5，AF_PP
  │     ├─ PSC=0, ARR=899  →  f = 80 kHz
  │     ├─ OC2 = PWM2，使能输出
  │     └─ TIM_Cmd → CNT 开始跑，硬件自动比较 CCR2
  │
  └─ while(1)
        led0pwmval 来回扫 0…300
        TIM_SetCompare2(TIM3, led0pwmval)  → 写 CCR2
              │
              ▼
        CNT 与 CCR2 比较 → PB5 高低电平占空比变化 → LED0 呼吸
```

**CPU 从不在循环里 `LED0=0/1`**；只更新比较寄存器。

---

## 五、容易误解的三处

### 5.1 `timer.c` 里还有 `TIM3_Int_Init` / `TIM3_IRQHandler`

那是 **定时中断实验** 残留（翻转 LED1）。本实验 **`main` 没有调用 `TIM3_Int_Init`**，读 PWM 时可先当不存在。

### 5.2 为何先 `LED_Init` 又 `TIM3_PWM_Init`

`LED_Init` 把 PB5 配成 **普通推挽 Out_PP**；随后 `TIM3_PWM_Init` 再改成 **AF_PP**。  
**以后者为准**，PWM 才能出波。若只做 PWM 呼吸，逻辑上可以不调 `LED_Init`；保留是工程模板习惯。

### 5.3 蓝 pill / 江协工程不要照搬引脚

| | 实验 9（正点原子） | 本仓库 `PWM.c` |
|--|-------------------|----------------|
| 定时器 | TIM3 CH2 | TIM2 CH3 |
| 脚 | PB5 + 部分重映射 | PA2（默认，无重映射） |
| 模式 | PWM2 | PWM1 |
| 改 CCR | `TIM_SetCompare2` | `PWM_SetCompare3` |

原理相同：**时基定 f，SetCompare 改 CCR。**

---

## 六、动手改参数（巩固）

| 改法 | 预期 |
|------|------|
| `TIM3_PWM_Init(1799, 0)` | 频率约减半（40 kHz） |
| `led0pwmval>300` → `>800` | 最亮更亮（占空比上限更大） |
| `delay_ms(10)` → `50` | 呼吸变慢 |
| `SetCompare2` 误写成 `SetCompare1` | CH2 无变化，灯不跟扫 |

---

## 七、对照定时器文档自检

1. 本实验频率公式分子是谁？算出 80 kHz。  
   → **CK_PSC=72M**；`72M/(0+1)/(899+1)=80k`。  
2. `led0pwmval` 对应哪个寄存器？  
   → **CCR2**。  
3. 为何必须 `AF_PP` + 部分重映射？  
   → 定时器驱动脚；战舰 LED0 在 PB5 不是默认 PA7。  
4. 主循环有没有翻转 GPIO？  
   → **没有**，只 `SetCompare2`。

---

## 八、建议阅读顺序

```text
1. 本文 §二 main（呼吸在干什么）
2. 本文 §三 TIM3_PWM_Init（五步对代码）
3. STM32-定时器Timer说明.md §三公式 + §五 CCR/PWM
4. 回工程改一个参数，看灯/示波器变化
```

工程打开：`实验9 PWM输出实验/USER/PWM.Uv2`（或对应 uvprojx）。
