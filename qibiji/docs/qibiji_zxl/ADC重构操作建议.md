# ADC 电流采集模块重构操作建议（qibiji_zxl）

> **起点**：`qibiji_zxl` 现只有 KEY / TIM3 / DEBUG_UART / LCD 四个模块，`main.c` 跑按键 + LCD 页面 + 背光。没有 `HARDWARE/ADC/` 目录，没有 `armReg`、没有 TIM2、没有 4G / EEPROM。
>
> **目标**：把原工程 `qibiji/HARDWARE/ADC/adc.c` 的电流采集搬进来——DMA 循环采样 → 开机零点标定 → 温漂跟踪 → 25 点滑动平均 → 堵转判定，做成独立模块 `HARDWARE/ADC/adc.c/.h`，对外只给只读接口 + 一个堵转标志，不依赖 `armReg`。
>
> **怎么用这份文件**：第 3、4 节是两个新文件的完整代码（可直接拷）；第 5 节改 `main.c`；第 6 节改 Keil 工程；第 7～9 节是机制讲解、验证和与原工程的对照。第 10 节是必须遵守的约束。
>
> **实施顺序**：先做本模块，再做《DCMotor重构操作建议.md》。因为电机模块要调用本模块的 `ADC_CurrentIsReady()` 做联锁、`ADC_PopStall()` 收堵转；反过来的耦合（本模块调 `ADC_SetMotorRunning()`）只是一个"电机在不在跑"的标志，接口已在本文件里定义好。

---

## 1. 本轮做什么、不做什么

**做**：新建 `HARDWARE/ADC/`，ADC1 + DMA1 循环采样 3 路（PC0 电流、PC2 太阳能电压、PC3 电池电压），电流走完整链路（零点标定 / 温漂跟踪 / 滑动平均 / 堵转），电压走慢路径。

**不搬**（明确排除，别顺手加）：

| 项 | 为什么不搬 |
|---|---|
| `armReg` / `reg.c` 整套寄存器 | 新工程没有这套全局结构；本模块用自己的 `static` 变量存电流，对外用 getter 读。`armReg.motor_curr` / `g_adc_val[]` 这些全局广播变量全部去掉 |
| TIM2 的 `flag20ms` / `sys_ms` | 新工程只有 TIM3 的 10ms 节拍；本模块内部对 10ms 计数派生出 20ms 采样节拍和自己的毫秒时钟，不动 `timer.c` |
| 电压上报到 GPRS / LCD | 电压只在 `ADC_GetSolarMv()` / `ADC_GetBatMv()` 里算好放着；接到 LCD 显示是后面"真数据接线"的活，本轮不改 `lcd_pages.c` |
| `ADC1_Read_filter_old()` 旧算法、`calc_adc_val()` | 原工程里是被替换掉的死代码 / 未使用函数，不搬 |
| 直接调 `dcmotor_request_stop()` / `EventPostMotorStall()` | 本模块只置堵转标志 `s_stall_flag`，由电机模块来取。这样 ADC 不依赖电机，两个模块能各自单独验证 |

---

## 2. 时基方案：从 `flag20ms` 到 `Tick_10ms_Take()`

原工程靠 TIM2 每 5ms 中断置 `flag20ms`，`ADC1_Read_filter()` 每 20ms 跑一次快路径。新工程没有 TIM2，只有 TIM3 的 10ms 节拍 `Tick_10ms_Take()`（消费式，取走即清）。

本模块自己派生：

- `ADC_Tick()` 每 10ms 调一次；内部 `s_tick_div` 数到 2（=20ms）才执行一次 `ADC_SampleSlot()`；
- `s_ms` 每次 `ADC_Tick()` 加 10，作为零点跟踪的时间戳（替代原 `sys_ms`）；
- 电压慢路径每 25 个 20ms 槽（=500ms）算一次。

**关键点**：`ADC_Tick()` 必须和 `KEY_Tick()` / `LCD_BACKLIGHT_Tick()` 挂在**同一个** `if (Tick_10ms_Take())` 里，共用一拍。`Tick_10ms_Take()` 是消费式的，写成两个独立 `if` 第二个几乎永远取不到。

```
if (Tick_10ms_Take())
{
    KEY_Tick();
    LCD_BACKLIGHT_Tick();
    ADC_Tick();          /* 本轮新增：内部 2 分频成 20ms 采样 */
    DCMOTOR_Tick();      /* 下一个模块接入后加 */
}
```

---

## 3. 第 1 步：新建 `HARDWARE/ADC/adc.h`

新目录 `qibiji_zxl/HARDWARE/ADC/` 需要你手动建（Keil 里新建文件夹并加进工程）。先放头文件：

```c
#ifndef __ADC_H
#define __ADC_H

/* ---------------------------------------------------------------------------
 * ADC current-sense module for qibiji_zxl
 *
 * Scope: ADC1 + DMA1 circular sampling of PC0 (motor current), PC2 (solar
 *        voltage), PC3 (battery voltage). Converts raw codes to current in
 *        mA, calibrates the zero point at power-up, tracks slow zero drift,
 *        and raises a stall flag when current stays over a threshold.
 *
 * Timing: reuses the existing TIM3 10 ms tick. ADC_Tick() is called once per
 *         10 ms; internally it divides by 2 to get the 20 ms sampling slot
 *         used by the original project. No new timer is allocated.
 *
 * Decoupling: this module never calls the motor code. A stall only sets a
 *             flag; the motor module consumes it with ADC_PopStall().
 *
 * Constraints: C90 (declarations at block start), ASCII-only comments/strings.
 * --------------------------------------------------------------------------- */
#include "sys.h"   /* uint8_t / uint16_t */

#define ADC_SAMPLE_COUNT   256u   /* DMA samples per channel per slot */
#define ADC_CHANNEL_COUNT  3u     /* CH10 current, CH12 solar, CH13 battery */

/* Default stall threshold in mA (matches the original project's 3500 mA). */
#define ADC_STALL_MA_DEFAULT  3500u

void     ADC1_Init(void);
void     ADC_Tick(void);                    /* call once per 10 ms system tick */
uint8_t  ADC_CurrentIsReady(void);          /* zero point calibrated? */
uint16_t ADC_GetCurrentMa(void);            /* 500 ms smoothed current, mA */
uint16_t ADC_GetSampleMa(void);             /* latest 20 ms sample, mA */
uint16_t ADC_GetSolarMv(void);              /* solar voltage, mV */
uint16_t ADC_GetBatMv(void);                /* battery voltage, mV */
uint8_t  ADC_PopStall(void);                /* consume stall flag: 1 once, then 0 */
void     ADC_SetStallThresholdMa(uint16_t ma);
void     ADC_SetMotorRunning(uint8_t running);  /* freeze zero tracking while running */

/* DMA target buffer (written by DMA in firmware; writable by test harness). */
extern volatile uint16_t g_adc_buf[ADC_SAMPLE_COUNT][ADC_CHANNEL_COUNT];

#endif
```

**为什么初始化函数叫 `ADC1_Init` 而不是 `ADC_Init`**：ST 标准库已经有一个 `ADC_Init(ADC_TypeDef*, ADC_InitTypeDef*)`（带两个参数）。如果我们的模块函数也叫 `ADC_Init(void)`，链接时符号重名会冲突。原工程正是为此把它命名成 `ADC1_Init`。`ADC_Tick` / `ADC_GetCurrentMa` 等新名字不和库函数撞。

---

## 4. 第 2 步：新建 `HARDWARE/ADC/adc.c`

```c
#include "adc.h"

/* ===========================================================================
 * ADC current-sense implementation
 *
 * Ported from qibiji/HARDWARE/ADC/adc.c (ADC1_Read_filter). Differences:
 *  - no armReg dependency: state kept in module-private statics
 *  - time base from the existing 10 ms tick instead of TIM2's flag20ms
 *  - stall raises a flag (ADC_PopStall) instead of calling motor stop
 *  - voltage slow-path kept but simplified (same fixed-point formula)
 * =========================================================================== */

#define ADC2CURT   (3.3f / 4.096f / (0.05f * 2.0f))   /* ~8.0566 mA per LSB */
#define ADC1_DR_ADDRESS  ((u32)0x4001244C)

#define ZERO_SAMPLE_COUNT   16u    /* 16 x 20ms = 320ms zero-point window */
#define AVG_WINDOW          25u    /* 25 x 20ms = 500ms smoothing window  */
#define STALL_COUNT         10u    /* 10 x 20ms = 200ms over-current       */
#define ZERO_STABLE_MS      1000u
#define ZERO_TRACK_MS       1000u
#define TICKS_PER_SLOT      2u     /* 10ms tick -> 20ms sampling slot      */
#define SLOW_SLOTS          25u    /* 25 x 20ms = 500ms voltage update     */

volatile uint16_t g_adc_buf[ADC_SAMPLE_COUNT][ADC_CHANNEL_COUNT];

/* zero point */
static uint8_t  s_ready;
static u32      s_zero_sum;
static uint8_t  s_zero_cnt;
static uint16_t s_zero_avg;
static u32      s_last_run_ms;
static u32      s_last_track_ms;
static u32      s_ms;                 /* local ms from 10ms ticks */

/* smoothing window */
static uint16_t s_win[AVG_WINDOW];
static u32      s_win_sum;
static uint8_t  s_win_cnt;
static uint8_t  s_win_idx;

/* stall + slow path */
static uint8_t  s_stall_flag;
static uint8_t  s_stall_cnt;
static uint16_t s_stall_threshold;
static uint8_t  s_slot_cnt;           /* counts 20ms slots for slow path */
static uint8_t  s_motor_running;      /* set by motor module, freezes zero track */

/* latest values */
static uint16_t s_sample_ma;
static uint16_t s_current_ma;
static uint16_t s_so_mv;
static uint16_t s_bat_mv;

static void ADC_HwInit(void)
{
    GPIO_InitTypeDef gpio;
    DMA_InitTypeDef  dma;
    ADC_InitTypeDef  adc;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_ADC1, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    /* PC0 / PC2 / PC3 analog inputs (PC1 left as input too, harmless). */
    gpio.GPIO_Pin  = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOC, &gpio);

    dma.DMA_PeripheralBaseAddr = ADC1_DR_ADDRESS;
    dma.DMA_MemoryBaseAddr     = (u32)&g_adc_buf;
    dma.DMA_DIR                = DMA_DIR_PeripheralSRC;
    dma.DMA_BufferSize         = ADC_SAMPLE_COUNT * ADC_CHANNEL_COUNT;
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode               = DMA_Mode_Circular;
    dma.DMA_Priority           = DMA_Priority_High;
    dma.DMA_M2M                = DMA_M2M_Disable;
    DMA_DeInit(DMA1_Channel1);
    DMA_Init(DMA1_Channel1, &dma);
    DMA_Cmd(DMA1_Channel1, ENABLE);

    RCC_ADCCLKConfig(RCC_PCLK2_Div6);   /* 72/6 = 12MHz */

    adc.ADC_Mode               = ADC_Mode_Independent;
    adc.ADC_ScanConvMode       = ENABLE;
    adc.ADC_ContinuousConvMode = ENABLE;
    adc.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign          = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel       = ADC_CHANNEL_COUNT;
    ADC_Init(ADC1, &adc);

    ADC_RegularChannelConfig(ADC1, ADC_Channel_10, 1, ADC_SampleTime_239Cycles5);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_12, 2, ADC_SampleTime_239Cycles5);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_13, 3, ADC_SampleTime_239Cycles5);

    ADC_DMACmd(ADC1, ENABLE);
    ADC_Cmd(ADC1, ENABLE);
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1)) { }
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1)) { }
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
}

void ADC1_Init(void)
{
    s_ready = 0u;
    s_zero_sum = 0u;
    s_zero_cnt = 0u;
    s_zero_avg = 0u;
    s_last_run_ms = 0u;
    s_last_track_ms = 0u;
    s_ms = 0u;
    s_win_sum = 0u;
    s_win_cnt = 0u;
    s_win_idx = 0u;
    s_stall_flag = 0u;
    s_stall_cnt = 0u;
    s_stall_threshold = ADC_STALL_MA_DEFAULT;
    s_slot_cnt = 0u;
    s_motor_running = 0u;
    s_sample_ma = 0u;
    s_current_ma = 0u;
    s_so_mv = 0u;
    s_bat_mv = 0u;
    ADC_HwInit();
}

static uint16_t ADC_RawAvg(uint8_t ch)
{
    u32 sum = 0u;
    u16 i;
    for (i = 0u; i < ADC_SAMPLE_COUNT; i++)
        sum += g_adc_buf[i][ch];
    return (uint16_t)(sum / ADC_SAMPLE_COUNT);
}

/* One 20ms sampling slot: zero calibration / conversion + stall detect. */
static void ADC_SampleSlot(void)
{
    uint16_t raw;
    uint16_t sample_ma;

    s_slot_cnt++;
    raw = ADC_RawAvg(0u);   /* channel 10 = current */

    if (s_ready == 0u)
    {
        /* Calibrate at power-up. The motor module holds the motor stopped
         * until ADC_CurrentIsReady() is true, so the current here is idle. */
        s_zero_sum += raw;
        s_zero_cnt++;
        if (s_zero_cnt >= ZERO_SAMPLE_COUNT)
        {
            s_zero_avg = (uint16_t)((s_zero_sum + ZERO_SAMPLE_COUNT / 2u) /
                                    ZERO_SAMPLE_COUNT);
            s_ready = 1u;
            s_last_run_ms = s_ms;
            s_last_track_ms = s_ms;
            /* discard zero-period zeros so they do not bias the window */
            s_win_cnt = 0u;
            s_win_idx = 0u;
            s_win_sum = 0u;
        }
        sample_ma = 0u;
    }
    else
    {
        if (raw >= s_zero_avg)
            sample_ma = (uint16_t)(((u32)(raw - s_zero_avg) * 8057u + 500u) / 1000u);
        else
            sample_ma = 0u;

        /* zero drift tracking: freeze while the motor runs (a running motor's
         * raw value carries load current -- tracking it would raise the zero
         * baseline and weaken stall protection). While running, refresh both
         * timestamps so the 1s stable window restarts after it stops. */
        if (s_motor_running != 0u)
        {
            s_last_run_ms = s_ms;
            s_last_track_ms = s_ms;
        }
        else if ((u32)(s_ms - s_last_run_ms) >= ZERO_STABLE_MS &&
                 (u32)(s_ms - s_last_track_ms) >= ZERO_TRACK_MS)
        {
            u32 cand = ((u32)s_zero_avg * 15u + raw + 8u) / 16u;
            if (cand > (u32)s_zero_avg + 1u) cand = (u32)s_zero_avg + 1u;
            else if ((cand + 1u) < s_zero_avg) cand = (u32)s_zero_avg - 1u;
            s_zero_avg = (uint16_t)cand;
            s_last_track_ms = s_ms;
        }
    }
    s_sample_ma = sample_ma;

    /* 25-point sliding window -> smoothed current */
    if (s_win_cnt < AVG_WINDOW)
    {
        s_win[s_win_idx] = sample_ma;
        s_win_sum += sample_ma;
        s_win_cnt++;
    }
    else
    {
        s_win_sum -= s_win[s_win_idx];
        s_win[s_win_idx] = sample_ma;
        s_win_sum += sample_ma;
        s_current_ma = (uint16_t)(s_win_sum / AVG_WINDOW);
    }
    s_win_idx++;
    if (s_win_idx >= AVG_WINDOW) s_win_idx = 0u;

    /* stall detect: this module only records, never touches motor GPIO */
    if (sample_ma > s_stall_threshold)
    {
        s_stall_cnt++;
        if (s_stall_cnt >= STALL_COUNT)
        {
            s_stall_flag = 1u;
            s_stall_cnt = 0u;
        }
    }
    else
    {
        s_stall_cnt = 0u;
    }

    /* 500ms voltage slow path (fixed-point scale from the original project) */
    if (s_slot_cnt >= SLOW_SLOTS)
    {
        uint16_t mv;
        s_slot_cnt = 0u;
        mv = (uint16_t)(((u32)ADC_RawAvg(1u) * 363u) >> 20);
        s_so_mv  = mv;
        mv = (uint16_t)(((u32)ADC_RawAvg(2u) * 363u) >> 20);
        s_bat_mv = mv;
    }
}

void ADC_Tick(void)
{
    static uint8_t s_tick_div;
    s_ms += 10u;
    s_tick_div++;
    if (s_tick_div >= TICKS_PER_SLOT)
    {
        s_tick_div = 0u;
        ADC_SampleSlot();
    }
}

uint8_t  ADC_CurrentIsReady(void) { return s_ready; }
uint16_t ADC_GetCurrentMa(void)   { return s_current_ma; }
uint16_t ADC_GetSampleMa(void)    { return s_sample_ma; }
uint16_t ADC_GetSolarMv(void)     { return s_so_mv; }
uint16_t ADC_GetBatMv(void)       { return s_bat_mv; }

uint8_t ADC_PopStall(void)
{
    uint8_t v = s_stall_flag;
    s_stall_flag = 0u;
    return v;
}

void ADC_SetStallThresholdMa(uint16_t ma) { s_stall_threshold = ma; }
void ADC_SetMotorRunning(uint8_t running) { s_motor_running = running; }
```

**几处和原工程不同的地方，都是刻意的**：

| 位置 | 原工程 | 本模块 | 原因 |
|---|---|---|---|
| 电流换算 | `(raw−zero) * ADC2CURT` 用 float | `* 8057u + 500u) / 1000u` 定点 | 8057/1000 = 8.057 ≈ ADC2CURT；避免每次乘浮点。`+500` 是四舍五入 |
| 零点期 0 混进窗口 | 16 个 0 样本写进 `val[]`，满窗后被压低 | 就绪瞬间 `s_win_cnt/idx/sum = 0` 丢弃 | 修掉解读稿 §11 问题 1；上电后不用等 500ms 才读数准 |
| 零点跟踪门控 | 运行时刷 `current_stop_since` / `zero_track_tick` | `ADC_SetMotorRunning()` 置位时刷两个时间戳 | 等价逻辑；标志由电机模块显式喂进来，ADC 不用读全局 `sys_moter_state` |
| 堵转动作 | `EventPostMotorStall()` + `dcmotor_request_stop()` | 只置 `s_stall_flag` | 解耦；电机模块 `ADC_PopStall()` 取走后自行断电 |

**换算系数怎么来的**：`ADC2CURT = 3.3 / (4.096 × 采样电阻 0.05Ω × 放大倍数 2) = 3.3 / 0.4096 ≈ 8.0566 mA/LSB`，即 1 个 ADC 码值约 8.06mA。定点写成 8057/1000。默认堵转阈值 3500mA ≈ 434 LSB。

**两套时间尺度**（和原工程一致）：`ADC_GetSampleMa()` 是 20ms 单次值，给堵转保护用（要快）；`ADC_GetCurrentMa()` 是 25 点滑动平均（500ms），给上报和显示用（要稳）。堵转判定只看单次值——用平均值会让电机多硬顶半秒。

---

## 5. 第 3 步：改 `USER/main.c`

### 5.1 加 include（放在 `#include "lcd_backlight.h"` 之后）

```c
#include "adc.h"
```

### 5.2 `main()` 里加 Init（放在 `delay_init()` 之后、`TIM3_10ms_Init()` 之前）

```c
    delay_init();
    LCD12864_Init();
    LCD_BACKLIGHT_Init();
    LCD_PAGES_Init();
    ADC1_Init();            /* 本轮新增：ADC1 + DMA 初始化，开始采样 */
    TIM3_10ms_Init();
```

**顺序要求**：`ADC1_Init()` 之后才有 `ADC_Tick()` 的采样节拍，所以要在 `TIM3_10ms_Init()` 之前完成初始化（TIM3 一启动就开始走节拍）。放在 `delay_init()` 之后是因为 ADC 校准等待虽然不用 `delay_ms()`，但保持初始化链一致更稳妥。

### 5.3 主循环 Tick 里加一行（和 `KEY_Tick()` 共用一拍）

```c
        if (Tick_10ms_Take())
        {
            KEY_Tick();
            LCD_BACKLIGHT_Tick();
            ADC_Tick();             /* 本轮新增：内部 2 分频成 20ms 采样 */
        }
```

### 5.4 改完后的 `main.c` 全貌（核对用）

```c
#include "main.h"
#include "key.h"
#include "timer.h"
#include "debug_uart.h"
#include "delay.h"
#include "lcd12864.h"
#include "lcd_pages.h"
#include "lcd_backlight.h"
#include "adc.h"

volatile key_code_t g_last_key = KEY_NONE;

static const char * const s_key_name[] =
{
    "NONE", "UP", "DOWN", "STOP", "SET", "CANCEL"
};

static void APP_DispatchKey(key_code_t key)
{
    const char *name;

    g_last_key = key;

    if((key >= KEY_NONE) && (key <= KEY_CANCEL))
        name = s_key_name[key];
    else
        name = "UNKNOWN";

    DEBUG_UART_SendString("KEY: ");
    DEBUG_UART_SendString(name);
    DEBUG_UART_SendString("\r\n");

    LCD_PAGES_Process(key);
    LCD_BACKLIGHT_Kick();
}

int main(void)
{
    key_code_t key;

    NVIC_Configuration();
    DEBUG_UART_Init();
    KEY_Init();
    delay_init();
    LCD12864_Init();
    LCD_BACKLIGHT_Init();
    LCD_PAGES_Init();
    ADC1_Init();                    /* 新增 */
    TIM3_10ms_Init();

    DEBUG_UART_SendString("qibiji_zxl KEY test ready\r\n");
    DEBUG_UART_SendString("USART1 115200 8N1, PA9=TX\r\n");

    while (1)
    {
        if (Tick_10ms_Take())
        {
            KEY_Tick();
            LCD_BACKLIGHT_Tick();
            ADC_Tick();              /* 新增 */
        }

        key = KEY_Pop();
        if (key != KEY_NONE)
            APP_DispatchKey(key);
    }
}
```

---

## 6. 第 4 步：改 `USER/SRTUV10.uvprojx`

### 6.1 IncludePath（约第 338 行）追加 `..\HARDWARE\ADC`

改后：

```xml
<IncludePath>..\USER;..\STM32F10x_FWLib\inc;..\SYSTEM\delay;..\SYSTEM\sys;..\CORE;..\HARDWARE\DEBUG_UART;..\HARDWARE\KEY;..\HARDWARE\TIMER;..\HARDWARE\LCD12864;..\HARDWARE\ADC</IncludePath>
```

### 6.2 HARDWARE 组 `<Files>` 末尾追加 2 个节点

```xml
            <File>
              <FileName>adc.c</FileName>
              <FileType>1</FileType>
              <FilePath>..\HARDWARE\ADC\adc.c</FilePath>
            </File>
            <File>
              <FileName>adc.h</FileName>
              <FileType>5</FileType>
              <FilePath>..\HARDWARE\ADC\adc.h</FilePath>
            </File>
```

`stm32f10x_adc.c` 和 `stm32f10x_dma.c` 工程里**已经编入**（在 FWLib 组），不用再加。改完 uvprojx 记得**关工程重开**（Keil 内存里是旧配置，直接保存会冲掉磁盘改动）。

---

## 7. 机制讲解（对应解读稿里问过的点）

**① ADC 码值怎么变成电流（量纲链）**

```
AD_Value[][0]  (LSB, 0..4095)
   → raw_avg   = 256 点平均，LSB
   → sample_ma = (raw_avg − zero_avg) × 8.057，mA       ← 20ms 单次，堵转用
   → motor_curr= 25 点滑动平均，mA                        ← 500ms 平均，显示/上报用
```

单边整流：`raw_avg < zero_avg` 时 `sample_ma = 0`，不产生负电流。代价是零点偏低时静止会有"幽灵电流"，原工程同样如此。

**② 开机零点标定**：静止状态下连续 16 个 20ms 槽（320ms）采 `raw_avg` 求基线 `zero_avg`，`(sum+8)/16` 带四舍五入。完成前 `ADC_CurrentIsReady()` 返回 0——电机模块据此强制停机，保证零点采得到。就绪瞬间把滑动窗口清零，丢掉标定期间的 0 样本（这是本轮对解读稿 §11 问题 1 的修复）。

**③ 温漂跟踪**：静止满 1s 且距上次跟踪满 1s 才动；一阶 IIR `(15×zero + raw + 8)/16` 再硬限幅到 ±1 LSB，实际速度约 1 LSB/s ≈ 8mA/s。电机运行时（`s_motor_running=1`）两个时间戳被刷新，跟踪冻结——因为运行电流会抬高基线、变相抬高堵转阈值导致漏报。

**④ 堵转判定**：`sample_ma > 阈值` 连续 10 个 20ms 槽（200ms）→ 置 `s_stall_flag`。200ms 是为了躲开启动浪涌（同样几倍电流但只持续几十 ms）。电机模块取走标志后断电并记 `STALL` 原因。

**⑤ 电压慢路径**：每 500ms 算一次，`((sum×363)>>20)` 是定点缩放，`+3` 在原工程里有偏移标定——本模块把 `+3` 省略了（原工程 `REFER[]` 是历史遗留，`REFER[1]/[2]=0` 时偏移来源说不清）。如果你对电压绝对值有要求，上板用万用表核对后再决定是否补回偏移。

---

## 8. 上板验证

### 8.1 能直接观察到的

本模块没有接 LCD 显示（本轮不改 `lcd_pages.c`），数字要通过调试器看 `ADC_GetCurrentMa()` / `ADC_GetSampleMa()` / `ADC_CurrentIsReady()` 的返回值，或临时在 `main.c` 里用 `DEBUG_UART_SendString` 打印。

推荐临时加一句调试（验证完删掉）：在主循环里每隔一段时间打印一次：

```c
    /* 临时调试：上板确认电流采样在动 */
    if (ADC_CurrentIsReady())
    {
        /* 用 sprintf 组串再 DEBUG_UART_SendString，注意工程启用 microLIB */
    }
```

### 8.2 预期现象

| 时刻 | `ADC_CurrentIsReady()` | `ADC_GetCurrentMa()` | 说明 |
|---|---|---|---|
| 0–320ms | 0 | 0 | 采零点窗口 |
| 320ms | 1 | 0（填充期） | 零点就绪，窗口开始填充 |
| ≈820ms | 1 | 开始输出有效值 | 25 点窗口填满（500ms） |
| 电机堵转 200ms | 1 | 快速上升 | `ADC_PopStall()` 返回 1 |

### 8.3 出问题时的排查

| 症状 | 首查 | 次查 |
|---|---|---|
| `CurrentIsReady()` 一直是 0 | `ADC_Tick()` 是否和 `KEY_Tick()` 在**同一个** `if (Tick_10ms_Take())` 里 | TIM3 是否初始化；PC0 是否真的接了采样电路 |
| 电流恒为 0 | 零点是否偏高（静止时 `raw_avg ≤ zero_avg`）→ 用调试器看 `g_adc_buf[][0]` 的实际码值 | DMA 是否搬运（`DMA1_Channel1` 是否 enable，ADC 是否 `SoftwareStartConv`） |
| 电流读数跳动大 | 供电/采样电路噪声；256 点平均是否够 | 滑动窗口是否满 25 点 |
| 上电就报堵转 | 零点没采到（电机没被强制静止）；阈值是否被 `ADC_SetStallThresholdMa()` 设太低 | 电机模块是否先于本模块就绪；默认阈值 3500mA |
| 编译报 `undefined symbol ADC_Init...` | 是否把函数写成了 `ADC_Init`（必须 `ADC1_Init`，见 §3） | uvprojx 是否加了 `adc.c` 节点 |

---

## 9. 与原工程逐项对照

| 原工程 | 原行为 | 本模块 | 差异说明 |
|---|---|---|---|
| `ADC1_Init()` | GPIO + DMA + ADC + 校准 | `ADC1_Init()` | 硬件时序基本一致（PC0/12/13、DMA1_CH1、12MHz、239.5 周期） |
| `flag20ms`（TIM2） | 5ms 中断里置位 | `ADC_Tick()` 内 2 分频 | 不新增定时器；复用 TIM3 10ms 节拍 |
| `sys_ms` | TIM2 每 5ms +5 | `s_ms` 每 10ms +10 | 本模块私有毫秒时钟；`(u32)(a−b)` 防回绕写法保留 |
| `armReg.motor_curr` | 全局，25 点平均 | `ADC_GetCurrentMa()` | 去掉全局广播，改 getter |
| `g_adc_val[]` | 全局数组 | `ADC_GetSolarMv()` / `ADC_GetBatMv()` | 同上；电压偏移 `+3` 省略（见 §7⑤） |
| `s_current_zero_ready` | 文件级 static | `s_ready` + `ADC_CurrentIsReady()` | 语义一致 |
| 零点期 0 混进窗口 | 有此缺陷 | 就绪瞬间清零窗口 | 修复解读稿 §11 问题 1 |
| 堵转 `EventPostMotorStall()` + 直接停机 | ADC 直接调电机 | `ADC_PopStall()` 标志 | 解耦，电机模块来取 |
| 浮点 `ADC2CURT` | 每槽一次 float 乘 | 定点 `×8057/1000` | 快，结果等价（8.057 ≈ 8.0566） |

---

## 10. 约束

- **C90**（`<uC99>0</uC99>`）：变量全在块首声明；本模块 `.c/.h` 注释全 ASCII（工程带 `--no-multibyte-chars`）。
- **函数名不能叫 `ADC_Init`**：ST 库已有同名函数，必须 `ADC1_Init`（见 §3）。
- **不新增定时器 / 中断**：复用 TIM3 10ms 节拍；本模块没有 `TIMx_IRQHandler`，无冲突。
- **`ADC_Tick()` 与主循环共用一拍**：和 `KEY_Tick()` / `LCD_BACKLIGHT_Tick()` 放在同一个 `if (Tick_10ms_Take())` 里，别写两个独立 if。
- **状态全私有**：`s_ready` / `s_zero_avg` / `s_stall_flag` 等都是 `.c` 内 `static`，不进头文件；对外只有 getter 和 setter。
- **堵转阈值单位 mA**：`ADC_SetStallThresholdMa()` 收的是 mA，不是 0.1A。LCD 设置页的堵转项（`LCD_PARAM_STALL_010A`，单位 0.1A）要接进来的话，换算成 mA 是 `值 × 100`，且 LCD 那个初值 300 对应 30000mA，和默认 3500mA 差很多——本轮**不自动接 LCD**，先用模块默认 3500mA，等你核对过 LCD 初值的真实含义再联动。

---

## 11. 验证记录

- host 端 `gcc -std=c90 -Wall -Wextra -pedantic`：编译通过，仅 1 条 host 专属警告（64 位指针转 `u32`，真机 32 位无此问题）；
- 行为断言 **30 项，0 失败**（ADC 部分 10 项 + 电机部分 20 项，两者一起验证耦合），ADC 相关关键项：
  - 上电 `ready=0`，第 15 个 20ms 槽仍为 0、第 16 个变 1（320ms 零点窗口）；
  - 注入 baseline+100 LSB → 20ms 单次值 ≈ 806mA、500ms 平滑值 ≈ 806mA（换算系数正确）；
  - 连续 9 槽过流不报、第 10 槽报堵转；`ADC_PopStall()` 取走一次后再取为 0；
  - 电流回落基线后无新堵转；
  - 电机运行（`ADC_SetMotorRunning(1)`）期间注入负载电流，恢复静止后采样回 0——证明零点跟踪被正确冻结，负载电流没被折进基线；
- 验证方法：host 端桩掉 STM32 标准库函数，`g_adc_buf[][]` 直接填常数模拟 ADC 码值，读 getter 和电机继电器引脚电平判断行为；
- 代码副本：`C:\Users\JessamynAdmin\AppData\Local\Temp\qibiji_motor_adc_test\`（`adc.c/.h`、`dcmotor.c/.h`、`stubs.c`、`test.c`）。
- 这是 host 桩验证，**不是 Keil 真机编译**；请在 Keil 点一次 Build 确认链接，并按第 8 节上板核对电流读数。

---

## 12. 补充：ADC 掉线检测（2026-11-21 已落盘）

> **状态更新**：本节方案已实施进 `qibiji_zxl/HARDWARE/ADC/adc.c`（用户 2026-11-21 落盘）。12.3 保留原方案文本作对照；实际落盘行号见 12.7 核对表。**尚缺两步收尾**（`adc.h` 声明、`led.c` 联动），见 12.8。

### 12.1 背景与动机

当前 `s_ready` 只有两处：`ADC1_Init()` 清 0（113 行）、零点标定完成置 1（163 行），**之后永不回落**。因此 `ADC_CurrentIsReady()` 在 320ms 标定窗口之后恒为 1，《DCMotor重构操作建议.md》§17.1 的 INTERLOCK 分支只在上电窗口有效——**运行中采集链路挂掉（DMA 停转、传感器断线）无从检测**，电机会在保护全瞎的状态下继续转。

本节给出掉线检测的完整实现。落盘后 `ADC_CurrentIsReady()` 才可能变 0，电机模块的零点联锁才真正闭环。

### 12.2 判据选择：DMA 计数器冻结，不用"采样值不变"

**不用模拟量判据**：本工程 256 点均值把噪声压得很低，电机静止时 `raw` 本来就可能长时间停在同一个数，会误报。

改用 DMA 循环计数器：`256 点 × 3 通道 × 21us ≈ 16.1ms` 转一圈，循环模式下 `CNDTR` 每个 20ms 槽必然变化；连续 500ms 读数完全相同 = 链路停转。

传感器断线/短路另用 rail 判据：`raw ≤ 2`（断线）或 `raw ≥ 4090`（短路/超量程）持续 1s。**前提是零点不贴 rail**：按 `ADC2CURT = 3.3/4.096/(0.05×2)` 推算霍尔零点约 2.5V → raw≈3103，离两端很远，判据安全。**若实际是零点在 0V 的变送器，此判据会常误报，须改为"偏离零点过远"**——落盘前先在标定完成处打印一次 `s_zero_avg` 确认。

### 12.3 代码（四处插入 `adc.c`）

**宏定义（第 23 行 `SLOW_SLOTS` 后）**：

```c
/* ---- link-loss detection 掉线检测 ---- */
#define ADC_LINK_STATIC_SLOTS  25u   /* 25 x 20ms = 500ms CNDTR frozen  */
#define ADC_LINK_RAIL_SLOTS    50u   /* 50 x 20ms = 1s raw stuck at rail */
#define ADC_RAIL_LOW           2u    /* <= this  = open wire            */
#define ADC_RAIL_HIGH          4090u /* >= this  = short / over-range   */
```

**静态变量（第 46 行 `s_motor_running` 后）**：

```c
/* link-loss */
static uint16_t s_dma_cnt_last;
static uint8_t  s_dma_static_cnt;
static uint8_t  s_dma_dead;      /* latched: chain stopped, needs re-init */
static uint8_t  s_rail_cnt;
static uint8_t  s_sensor_open;   /* self-clearing when raw comes back     */
```

**检测函数（`ADC_RawAvg()` 之后）**：

```c
/* DMA-counter watchdog + sensor rail check, once per 20ms slot.
 * Must run before the value is used, so a dead chain never feeds
 * the zero tracker or the stall detector. */
static void ADC_LinkCheck(uint16_t raw)
{
    uint16_t cnt = (uint16_t)DMA_GetCurrDataCounter(DMA1_Channel1);

    /* 1) circular DMA walks the whole buffer in ~16ms, so two slots 20ms
     *    apart can never read the same CNDTR unless the chain stopped. */
    if (cnt == s_dma_cnt_last)
    {
        if (s_dma_static_cnt < 0xFFu)
            s_dma_static_cnt++;
        if (s_dma_static_cnt >= ADC_LINK_STATIC_SLOTS)
            s_dma_dead = 1u;
    }
    else
    {
        s_dma_static_cnt = 0u;
        s_dma_cnt_last = cnt;
    }

    /* 2) open wire or shorted sensor parks the input at a rail.
     *    Recoverable: a sane reading clears both counters. */
    if ((raw <= ADC_RAIL_LOW) || (raw >= ADC_RAIL_HIGH))
    {
        if (s_rail_cnt < 0xFFu)
            s_rail_cnt++;
        if (s_rail_cnt >= ADC_LINK_RAIL_SLOTS)
            s_sensor_open = 1u;
    }
    else
    {
        s_rail_cnt = 0u;
        s_sensor_open = 0u;
    }
}
```

**接入（`ADC_SampleSlot()` 第 151 行 `raw = ADC_RawAvg(0u);` 后）**：

```c
    raw = ADC_RawAvg(0u);
    ADC_LinkCheck(raw);      /* must precede zero tracking + stall detect */
```

**接口（第 258 行起）**：

```c
uint8_t ADC_CurrentIsReady(void)
{
    return ((s_ready != 0u) && (s_dma_dead == 0u) && (s_sensor_open == 0u))
           ? 1u : 0u;
}

/* 1 = acquisition chain or sensor is down. Led / telemetry can read this. */
uint8_t ADC_IsLinkLost(void)
{
    return ((s_dma_dead != 0u) || (s_sensor_open != 0u)) ? 1u : 0u;
}
```

`ADC1_Init()` 里补 5 行清零（第 131 行后）：`s_dma_cnt_last / s_dma_static_cnt / s_dma_dead / s_rail_cnt / s_sensor_open`。

### 12.4 语义分层

| 故障 | 标志 | 恢复方式 | 理由 |
|---|---|---|---|
| DMA 停转 | `s_dma_dead` | **锁存**，需重启 | 循环模式 DMA 不会自己复活，链路级故障 |
| 传感器断线/短路 | `s_sensor_open` | **自动恢复**，raw 回到正常带内即清 | 接回线就好，重启不必要 |

### 12.5 与 LED / 电机的联动（必须同时做，否则静止期漏报）

链路在**静止时**挂掉不会经过电机模块的 INTERLOCK 分支（§17.1 的判据要求 `s_old` 在运行态），只靠 `stop_reason` 看不见。《LED重构操作建议.md》的 ERR 判断需加一条直连：

```c
if ((ADC_IsLinkLost() != 0u) || ...原有三类...) LED_PIN_ON(LED_ERR_PIN);
```

`led.c` 需要 `#include "adc.h"`。两条通道分工：`stop_reason` 管"运行中被中断"（带原因、能上报），`IsLinkLost` 管"链路本身坏了"（只点灯，不碰原因）。

### 12.6 验证步骤

| 操作 | 预期 |
|---|---|
| 正常上电静止 | ERR 灭，`ADC_IsLinkLost()==0` |
| 拔掉电流传感器信号线 | 1s 后 ERR 亮，电机拒动 |
| 插回信号线 | 1s 后 ERR 自动灭（rail 可恢复） |
| 调 `DMA_Cmd(DMA1_Channel1, DISABLE)` | 500ms 后 ERR 亮且**不自动恢复**（需重启） |

### 12.7 落盘核对表（2026-11-21，按当前 `adc.c` 实测行号）

| 内容 | 方案原位置 | 实际落盘位置 |
|---|---|---|
| 4 个宏 `ADC_LINK_STATIC_SLOTS / ADC_LINK_RAIL_SLOTS / ADC_RAIL_LOW / ADC_RAIL_HIGH` | 第 23 行后 | **25–28 行** |
| 5 个静态变量 `s_dma_cnt_last / s_dma_static_cnt / s_dma_dead / s_rail_cnt / s_sensor_open` | 第 46 行后 | **48–52 行**（`/* link-loss */` 分组） |
| `ADC1_Init()` 清零 5 行 | 第 131 行后 | **145–149 行**（`s_motor_running` 后、`s_sample_ma` 前，与声明分组顺序一致） |
| `ADC_LinkCheck()` | `ADC_RawAvg()` 后 | **170–201 行** |
| 接入点 `ADC_LinkCheck(raw);` | `ADC_SampleSlot()` 第 151 行后 | **213 行**（`raw = ADC_RawAvg(0u);` 之后、零点跟踪/堵转检测之前） |
| `ADC_CurrentIsReady()` 三条件与 | 第 258 行起 | **322 行** |
| `ADC_IsLinkLost()` | 第 258 行起 | **327–329 行** |

落盘时的两处小差异（不影响行为）：`ADC_RAIL_LOW/HIGH` 的注释补了中文说明；rail 判据阈值直接写了 `50u` 字面量（等效 `ADC_LINK_RAIL_SLOTS`）。

### 12.8 剩余收尾（截至本文更新时尚未落盘）

1. **`adc.h` 缺 `ADC_IsLinkLost` 声明**（`adc.h` 31 行只有 `ADC_CurrentIsReady`）。`adc.c` 里的定义没有头文件声明，其它模块**无法合法调用**——`led.c` 一旦接入会报 implicit declaration。补一行：

   ```c
   uint8_t  ADC_IsLinkLost(void);              /* 1 = chain stopped or sensor open */
   ```

2. **`led.c` 未接 ERR 直连**（`led.c` 头部只有 `dcmotor.h`、`gprs_at.h`，无 `adc.h`；故障判断没有 `ADC_IsLinkLost()` 项）。不接的话静止期链路挂掉**ERR 灯不亮**，只剩"电机拒动"这一个间接现象。接法见 §12.5。

3. **rail 判据前提待确认**：上板后先看一次 `s_zero_avg`（约 3100 才安全）；在 `s_ready = 1u;`（约 163 行）后临时加 `GPRS_Printf("[ADC] zero_avg=%u\r\n", ...)`，确认后删除。

### 12.9 检验步骤（结合当前落盘状态）

前提：先完成 §12.8 的 1、2 两步（声明 + LED 联动），否则 ERR 灯证据链不完整。

| 步骤 | 操作 | 预期现象 | 验证的是 |
|---|---|---|---|
| ① 基线 | 正常上电，静置 10s | ERR 灭；串口无异常；电机静止 | 计数器不误报（噪声/巧合不动 CNDTR 不至于积累 25 槽） |
| ② 零点确认 | 看临时打印的 `s_zero_avg` | ≈3100（远离 0 和 4090） | rail 判据前提成立 |
| ③ 断线（可恢复） | 拔掉电流传感器信号线，等 2s | **1s 后 ERR 亮**；此时按 UP 电机不转（`CurrentIsReady()==0` → 零点联锁强制停） | rail 判据 + 自恢复语义 |
| ④ 恢复 | 插回信号线，等 2s | **ERR 自动灭**，无需重启；此后电机可正常启动 | `s_sensor_open` 自清除 |
| ⑤ 链路死（不可恢复） | 在代码里临时加 `DMA_Cmd(DMA1_Channel1, DISABLE);` 后编译烧录 | **500ms 后 ERR 亮且不灭**；重启才恢复 | `s_dma_dead` 锁存语义 |
| ⑥ 运行中断线 | 插回线、电机转起来后**再拔线** | ERR 亮；电机立即被零点联锁强制停；下次启动前事件 reason 走 `s_old` 判据（运行中→INTERLOCK） | §17.1 判据与本节联动 |

第 ⑤ 步验证完记得把 `DMA_Cmd(...DISABLE)` 那行删掉再烧正式固件。

判据时间常数速查：CNDTR 冻结判死 = 25 槽 × 20ms = **500ms**（锁存）；rail 贴边判断线 = 50 槽 × 20ms = **1s**（自恢复）。

---

> 下一份《DCMotor重构操作建议.md》在本模块基础上接入电机：按键 UP/DOWN/STOP → `DCMOTOR_Request()`，主循环 `DCMOTOR_Process()`，10ms 节拍里 `DCMOTOR_Tick()`。电流零点联锁和堵转停机都通过本文件的接口完成。
