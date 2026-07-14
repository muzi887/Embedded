# STM32 GPIO 说明

> **相关文档**
>
> - [文件说明.md](./文件说明.md) — Embedded 目录索引
> - [STM32外设说明.md](./STM32外设说明.md) — 外设总览（GPIO 见 §4.2）
> - [STM32F103C8T6引脚说明.md](./STM32F103C8T6引脚说明.md) — 引脚与复用功能
> - [STM32入门环境与工程配置备忘.md](./STM32入门环境与工程配置备忘.md) — Keil / SPL 工程配置
> - [STM32最小系统板与面包板器件说明.md](./STM32最小系统板与面包板器件说明.md) — LED、蜂鸣器等接线对象
> - [STM32短期入门规划.md](./STM32短期入门规划.md) — 2 周入门日程
> - 工程示例：[project/Hardware/LED.c](../project/Hardware/LED.c)、[project/Hardware/Key.c](../project/Hardware/Key.c)

更新时间：2026-07-13

---

## 一、GPIO 是什么

**GPIO**（General Purpose Input/Output，通用输入输出）是 STM32 引脚最基础的工作方式：同一物理引脚，可配置为 **数字输入、数字输出、模拟输入或复用外设**，内部由 **寄存器 + MOS 驱动 + 施密特触发器** 等电路实现。

可以把每个 GPIO 引脚理解成一根 **由程序控制的电线**：


| 方向     | 作用                        | 典型外设        |
| ------ | ------------------------- | ----------- |
| **输出** | MCU 向引脚写 **高/低电平**，驱动外部电路 | LED、蜂鸣器、继电器 |
| **输入** | MCU 从引脚 **读高/低电平**，感知外部状态 | 按键、传感器 DO   |


同一根引脚在同一时刻只能是一种模式（输入或输出等），使用前必须先 **初始化配置**。

**记忆口诀**：**GPIO = 脚；输出写电平，输入读电平；用之前先开时钟、再 Init。**

### I/O 端口位的基本结构

下图对应 RM0008 参考手册 **图 13：I/O 端口位的基本结构**（单个引脚内部电路）。

STM32 GPIO 位结构

#### 引脚与保护


| 模块         | 说明                                      |
| ---------- | --------------------------------------- |
| **I/O 引脚** | 芯片封装上的物理引脚，连面包板 / 模块                    |
| **保护二极管**  | 接 V~~DD~~ 与 V~~SS~~，抑制 ESD、过压/欠压，保护内部电路 |


#### 输入路径（上半部分）


| 模块              | 说明                                      |
| --------------- | --------------------------------------- |
| **上拉 / 下拉电阻**   | 开关可 **开/关**；配置为带上拉/下拉输入时，悬空引脚有默认电平      |
| **TTL 施密特触发器**  | 把引脚电压整形为稳定 **0/1**；可开关，关闭时数字输入路径断开      |
| **输入数据寄存器 IDR** | CPU **读出** 当前引脚数字电平                     |
| **模拟输入**        | 绕过数字逻辑，信号 **直通片上外设**（如 **ADC**）         |
| **复用功能输入**      | 引脚交给 **USART RX、SPI MISO、TIM 捕获** 等外设读入 |


#### 输出路径（下半部分）


| 模块                 | 说明                                                                    |
| ------------------ | --------------------------------------------------------------------- |
| **位设置/清除寄存器 BSRR** | CPU **写入** 时可单独置位/清零 ODR 某一位，避免读-改-写竞争                                |
| **输出数据寄存器 ODR**    | 保存待输出电平；可读可写                                                          |
| **输出模式选择**         | **推挽**：P-MOS + N-MOS 主动拉高/拉低；**开漏**：仅 N-MOS 拉低，高电平为高阻；**关闭**：两管截止（高阻） |
| **复用功能输出**         | 由 **定时器 PWM、USART TX** 等外设驱动，不经 CPU 直接写 ODR                           |
| **输出控制**           | 根据配置寄存器选择 **GPIO 输出** 还是 **复用输出**，以及推挽/开漏                             |


#### 与写代码的对应关系


| 你在代码里做的事                   | 对应硬件                      |
| -------------------------- | ------------------------- |
| `GPIO_Init()` 选模式          | 配置上下拉、推挽/开漏、复用/模拟开关       |
| `GPIO_SetBits()` / `BSRR`  | 写 **BSRR** 置位 ODR         |
| `GPIO_ResetBits()` / `BRR` | 写 **BRR** 或 BSRR 高 16 位清零 |
| `GPIO_ReadInputDataBit()`  | 读 **IDR**                 |
| `TIM_PWM` 输出到引脚            | **复用功能输出** 路径             |
| `ADC` 采样 PA0               | **模拟输入** 路径，GPIO 数字部分无效   |


---

## 二、GPIO 的用法（四步）

配置任意 GPIO 引脚固定四步：

```text
1. 初始化时钟（RCC）
2. 定义结构体变量（GPIO_InitTypeDef）
3. 给结构体赋值（填写 Pin、Mode、Speed）
4. 调用 GPIO 初始化函数（GPIO_Init）
```

### 第 1 步：初始化时钟

STM32 外设默认 **时钟关闭**（省电）。操作 GPIO 前，必须先打开对应端口的时钟。

GPIO 挂在 **APB2 总线** 上，使用库函数：

```c
RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);  // 开启 GPIOA
RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);  // 开启 GPIOB
RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);  // 开启 GPIOC
```


| 你要用的引脚   | 应开的时钟                  |
| -------- | ---------------------- |
| PA0～PA15 | `RCC_APB2Periph_GPIOA` |
| PB0～PB15 | `RCC_APB2Periph_GPIOB` |
| PC0～PC15 | `RCC_APB2Periph_GPIOC` |


### 第 2 步：定义结构体

定义一个 **GPIO 初始化结构体变量**，用来告诉库函数「要配哪些引脚、什么模式」：

```c
GPIO_InitTypeDef GPIO_InitStructure;
```

`GPIO_InitTypeDef` 在 `stm32f10x_gpio.h` 中定义，主要包含三个成员：


| 成员           | 含义                                    |
| ------------ | ------------------------------------- |
| `GPIO_Pin`   | 选择引脚，如 `GPIO_Pin_1`、`GPIO_Pin_1       |
| `GPIO_Mode`  | 工作模式，见本文 **§五** 八种模式                  |
| `GPIO_Speed` | 输出速度（输入模式时也需填写，常用 `GPIO_Speed_50MHz`） |


### 第 3 步：给结构体赋值

给结构体各成员 **赋值**，指定引脚和模式：

**示例 A：推挽输出 — 驱动 LED（PA1）**

```c
GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;    // 推挽输出
GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;         // PA1
GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
```

**示例 B：上拉输入 — 读按键（PB1）**

```c
GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;     // 上拉输入
GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;         // PB1
GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
```

### 第 4 步：调用 GPIO 初始化函数

把 **端口** 和 **结构体地址** 传给 `GPIO_Init`，配置写入硬件寄存器：

```c
GPIO_Init(GPIOA, &GPIO_InitStructure);   // 初始化 GPIOA
GPIO_Init(GPIOB, &GPIO_InitStructure);   // 初始化 GPIOB
```

### 完整示例（LED 输出）

与本工程 [LED.c](../project/Hardware/LED.c) 相同：

```c
#include "stm32f10x.h"

void LED_Init(void)
{
    // 1. 初始化时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    // 2. 定义结构体
    GPIO_InitTypeDef GPIO_InitStructure;

    // 3. 给结构体赋值
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    // 4. 调用初始化函数
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 初始化完成后，可设置默认电平
    GPIO_SetBits(GPIOA, GPIO_Pin_1 | GPIO_Pin_2);   // 置高，LED 灭
}
```

### 完整示例（按键输入）

与本工程 [Key.c](../project/Hardware/Key.c) 相同：

```c
void Key_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_Init(GPIOB, &GPIO_InitStructure);
}
```

---

## 三、GPIO 八个读写库函数

初始化完成后，用下面 **8 个库函数** 读写引脚。江协课程中 **输出 4 个 + 读 4 个**，合称 GPIO 八大函数。

### 3.1 输出写函数（4 个）


| 函数               | 作用              | 参数说明                             |
| ---------------- | --------------- | -------------------------------- |
| `GPIO_SetBits`   | 将指定引脚 **置高**（1） | `GPIOx`：端口；`GPIO_Pin`：引脚掩码       |
| `GPIO_ResetBits` | 将指定引脚 **置低**（0） | 同上                               |
| `GPIO_WriteBit`  | 写 **单个** 引脚为指定值 | `BitVal`：`Bit_SET` 或 `Bit_RESET` |
| `GPIO_Write`     | 写 **整个端口** 16 位 | `PortVal`：16 位值，每位对应一个引脚         |


```c
// 置 PA1 高、PA2 低
GPIO_SetBits(GPIOA, GPIO_Pin_1);
GPIO_ResetBits(GPIOA, GPIO_Pin_2);

// 写 PA1 为低
GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_RESET);

// 写整个 GPIOA 输出寄存器（流水灯常用）
GPIO_Write(GPIOA, ~0x0001);
```

**区别速记**：


| 函数                      | 粒度                   |
| ----------------------- | -------------------- |
| `SetBits` / `ResetBits` | 只改 **某几位**，其它位不变     |
| `WriteBit`              | 只改 **一位**            |
| `Write`                 | **16 位一起** 写，会覆盖整个端口 |


### 3.2 输入 / 输出读函数（4 个）


| 函数                       | 作用                  | 返回值        |
| ------------------------ | ------------------- | ---------- |
| `GPIO_ReadInputDataBit`  | 读 **单个引脚** 的输入电平    | `0` 或 `1`  |
| `GPIO_ReadInputData`     | 读 **整个端口** 输入（16 位） | `uint16_t` |
| `GPIO_ReadOutputDataBit` | 读 **单个引脚** 的输出寄存器   | `0` 或 `1`  |
| `GPIO_ReadOutputData`    | 读 **整个端口** 输出寄存器    | `uint16_t` |


```c
// 读 PB1 按键（上拉输入，按下为 0）
if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1) == 0)
{
    // 按键按下
}

// 读 GPIOB 全部 16 个输入引脚
uint16_t portVal = GPIO_ReadInputData(GPIOB);

// 读 PA1 当前输出状态（翻转 LED 时用）
if (GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_1) == 0)
{
    GPIO_SetBits(GPIOA, GPIO_Pin_1);    // 当前低 → 置高
}
```

**输入读 vs 输出读**：


| 读谁              | 用哪个                                    | 场景              |
| --------------- | -------------------------------------- | --------------- |
| 引脚 **外部** 实际电平  | `ReadInputDataBit` / `ReadInputData`   | 读按键、读传感器 DO     |
| **输出寄存器** 里保存的值 | `ReadOutputDataBit` / `ReadOutputData` | 翻转 LED、确认上次写了什么 |


底层对应关系（了解即可）：


| 库函数                     | 操作的寄存器               |
| ----------------------- | -------------------- |
| `ReadInputData`*        | **IDR**（输入数据寄存器）     |
| `ReadOutputData`*       | **ODR**（输出数据寄存器）     |
| `SetBits` / `ResetBits` | 写 **BSRR** / **BRR** |


头文件：`#include "stm32f10x_gpio.h"`（通常通过 `stm32f10x.h` 已包含）。

---

## 四、八个函数速查表


| #   | 函数                       | 读/写 | 粒度       |
| --- | ------------------------ | --- | -------- |
| 1   | `GPIO_SetBits`           | 写   | 多引脚置 1   |
| 2   | `GPIO_ResetBits`         | 写   | 多引脚置 0   |
| 3   | `GPIO_WriteBit`          | 写   | 单引脚      |
| 4   | `GPIO_Write`             | 写   | 整端口 16 位 |
| 5   | `GPIO_ReadInputDataBit`  | 读   | 单引脚输入    |
| 6   | `GPIO_ReadInputData`     | 读   | 整端口输入    |
| 7   | `GPIO_ReadOutputDataBit` | 读   | 单引脚输出    |
| 8   | `GPIO_ReadOutputData`    | 读   | 整端口输出    |


---

## 五、GPIO 八种工作模式

通过配置 **端口配置寄存器 CRH/CRL**（每引脚 4 bit 模式位），每个 I/O 可设为下列 **8 种模式之一**：

STM32 GPIO 八种模式

### 5.1 四种输入模式


| 模式名称     | 性质   | 特征                           | 典型用途                   |
| -------- | ---- | ---------------------------- | ---------------------- |
| **浮空输入** | 数字输入 | 可读电平；悬空时电平 **不确定**           | 外接已有明确驱动源的 DO          |
| **上拉输入** | 数字输入 | 内部上拉；悬空时 **默认高**             | 按键一端接 GND、一端接 IO（按下为低） |
| **下拉输入** | 数字输入 | 内部下拉；悬空时 **默认低**             | 按键一端接 3.3V、按下为高        |
| **模拟输入** | 模拟输入 | **GPIO 数字逻辑关闭**；引脚直连 **ADC** | 光敏/声音模块 **AO**、电位器     |


### 5.2 四种输出模式


| 模式名称       | 性质   | 特征                                    | 典型用途                     |
| ---------- | ---- | ------------------------------------- | ------------------------ |
| **开漏输出**   | 数字输出 | 低电平接 V~~SS~~；高电平 **高阻**（需外部上拉）（只有漏极开） | I2C 总线、5 V 容忍开漏场景        |
| **推挽输出**   | 数字输出 | 高接 V~~DD~~，低接 V~~SS~~，可主动驱动           | **LED、蜂鸣器 IN**（入门最常用）    |
| **复用开漏输出** | 数字输出 | 由 **片上外设** 控制；高阻 / 拉低                 | I2C1 SCL/SDA（硬件 I2C）     |
| **复用推挽输出** | 数字输出 | 由 **片上外设** 控制；高 V~~DD~~ / 低 V~~SS~~   | **USART TX、SPI、TIM PWM** |

开漏是源极接 GND，且 内部没有接 VDD 的上管，漏极接引脚；
推挽是上管接 VDD + 下管接 GND，两个漏极共接引脚


### 5.3 模式怎么选（入门套件）


| 接什么               | 推荐模式                | 本工程示例                 |
| ----------------- | ------------------- | --------------------- |
| 板载 / 外接 LED       | **推挽输出**            | `LED.c`（PA1/PA2）      |
| 有源蜂鸣器模块 IN        | **推挽输出**            | `Buzzer.c`（PB12）      |
| 轻触按键（一脚 GND）      | **上拉输入**            | `Key.c`（PB1/PB11）     |
| 传感器模块 DO          | **浮空输入** 或 **上拉输入** | `LightSensor.c`（PB13） |
| 传感器 AO → PA0      | **模拟输入**            | —                     |
| OLED I2C（PB6/PB7） | **复用开漏**            | 江协后续章节                |
| USB-TTL 串口 TX/RX  | **复用推挽**（USART1）    | PA9/PA10              |
| 舵机 PWM            | **复用推挽**（TIM 通道）    | —                     |


### 5.4 SPL 模式常量对照


| `GPIO_Mode` 常量          | 对应模式 |
| ----------------------- | ---- |
| `GPIO_Mode_IN_FLOATING` | 浮空输入 |
| `GPIO_Mode_IPU`         | 上拉输入 |
| `GPIO_Mode_IPD`         | 下拉输入 |
| `GPIO_Mode_AIN`         | 模拟输入 |
| `GPIO_Mode_Out_OD`      | 开漏输出 |
| `GPIO_Mode_Out_PP`      | 推挽输出 |
| `GPIO_Mode_AF_OD`       | 复用开漏 |
| `GPIO_Mode_AF_PP`       | 复用推挽 |


AF = Alternate Function = 引脚「复用」给外设用。

### 5.5 推挽 vs 开漏（易混）

```text
推挽输出 (Push-Pull)
  输出 1 → P-MOS 导通 → 引脚接 VDD
  输出 0 → N-MOS 导通 → 引脚接 VSS
  → 可主动驱动 LED、蜂鸣器

开漏输出 (Open-Drain)
  输出 0 → N-MOS 导通 → 引脚接 VSS
  输出 1 → 两管截止 → 高阻，需外部上拉才变高
  → I2C 多设备共线、电平转换
```

**记忆口诀**：**推挽能拉高拉低，开漏只拉低要外挂；复用交给外设，模拟直通 ADC。**

### 5.6 直接写寄存器（进阶）

不经过库函数时，典型步骤与 §1 结构图一致：

1. **RCC_APB2ENR** 使能对应 GPIO 端口时钟（如 GPIOC → **IOPCEN**）
2. **GPIOC_CRH / CRL** 配置模式与速度（4 bit × 引脚）
3. **GPIOC_ODR** 或 **BSRR** 写输出电平

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
GPIOC->CRH &= ~(0xF << 20);
GPIOC->CRH |=  (0x3 << 20);
GPIOC->BSRR = GPIO_BSRR_BS13;
```

具体位域以 [RM0008](https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) **GPIO 章节** 为准。

---

## 六、自检

1. 使用 GPIO 前为什么要 `RCC_APB2PeriphClockCmd`？
  → 外设时钟默认关闭，不开时钟配置不生效。
2. `GPIO_Init` 之前必须完成哪三步？
  → 定义结构体、给成员赋值、（之前还要）开时钟。
3. CPU 读引脚输入电平读哪个寄存器？
  → **IDR**；库函数为 `GPIO_ReadInputDataBit`。
4. 读按键用 `ReadInputDataBit` 还是 `ReadOutputDataBit`？
  → **ReadInputDataBit**（读外部输入）。
5. `GPIO_SetBits` 和 `GPIO_Write` 的主要区别？
  → SetBits 只改指定几位；Write 一次写满 16 位。
6. 点灯一般用推挽还是开漏？
  → **推挽输出**。
7. I2C 为什么用复用开漏？
  → 多主机/多从机 **线与**，高电平靠 **外部上拉**，不会冲突。
8. ADC 采样时 GPIO 应设什么模式？
  → **模拟输入**；数字施密特触发器关闭。
9. 本工程 LED 是低电平点亮，应该用 SetBits 还是 ResetBits 来「打开」？
  → **ResetBits**（置低点亮）。

---

## 延伸阅读


| 资料                                                  | 内容                            |
| --------------------------------------------------- | ----------------------------- |
| RM0008 §General-purpose and alternate-function I/Os | 图 13 位结构、CRL/CRH、IDR/ODR/BSRR |
| SPL `stm32f10x_gpio.c` / `.h`                       | `GPIO_Init` 与八种模式枚举           |
| [STM32外设说明.md](./STM32外设说明.md) §4.2                 | GPIO 与外设总览                    |
| [STM32入门环境与工程配置备忘.md](./STM32入门环境与工程配置备忘.md)        | Define、Include Path、库文件加入工程   |


