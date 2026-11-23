#include "adc.h"
#include "gprs_config.h"    /* GPRS_Printf, temporary debug */

/* ===========================================================================
 * ADC current-sense 电流感测 implementation
 *
 * Ported from ADC1_Read_filter. Differences:
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

/* ---- link-loss detection 掉线检测 ---- */
#define ADC_LINK_STATIC_SLOTS  25u   /* 25 x 20ms = 500ms CNDTR frozen  */
#define ADC_LINK_RAIL_SLOTS    50u   /* 50 x 20ms = 1s raw stuck at rail */

volatile uint16_t g_adc_buf[ADC_SAMPLE_COUNT][ADC_CHANNEL_COUNT];

/* zero point */
static uint8_t  s_ready;              // 标定完成的开关。为 0 表示还在标定、电流值不可用
static u32      s_zero_sum;           // 标定期间 16 次读数的累加和（用 u32 防溢出）
static uint8_t  s_zero_cnt;           // 已经采了几次
static uint16_t s_zero_avg;           // 零点平均值
static u32      s_last_run_ms;
static u32      s_last_track_ms;
static u32      s_ms;                 /* local ms from 10ms ticks */

/* smoothing window：把最近 25 个槽（25×20ms = 500ms）的读数存下来，报平均值。 */
static uint16_t s_win[AVG_WINDOW];  // 存最近 25 次的电流样本（环形缓冲区）。
static u32      s_win_sum;          // 25 个电流的总和
static uint8_t  s_win_cnt;          // 当前已填满几格
static uint8_t  s_win_idx;          // 写指针，下一个样本该放哪

/* link-loss */
static uint16_t s_dma_cnt_last;  /* CNDTR 寄存器值 */
static uint8_t  s_dma_static_cnt;/* CNDTR 连续多少个 20ms 槽没变化的计数器 */
static uint8_t  s_dma_dead;      /* latched: chain stopped, needs re-init */
static uint8_t  s_rail_cnt;      /* 读数连续多少个槽贴边 */
static uint8_t  s_sensor_open;   /* self-clearing when raw comes back 自恢复 */

/* stall + slow path */
static uint8_t  s_stall_flag;
static uint8_t  s_stall_cnt;
static uint16_t s_stall_threshold;
static uint16_t s_rail_low = ADC_RAIL_LOW;
static uint8_t  s_slot_cnt;           /* counts 20ms slots for slow path */
static uint8_t  s_motor_running;      /* set by motor module, freezes zero track */

/* latest values */
static uint16_t s_sample_ma;         // 本次瞬时电流
static uint16_t s_current_ma;        // 25 点平滑后的最终电流值
static uint16_t s_so_mv;             // 太阳能电压
static uint16_t s_bat_mv;            // 电池电压

void ADC_HwInit(void)     // Hardware initialization: GPIO, DMA, ADC1
{
    GPIO_InitTypeDef GPIO_InitStructure;
    DMA_InitTypeDef DMA_InitStructure;
    ADC_InitTypeDef ADC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_ADC1, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    /* PC0 / PC2 / PC3 analog inputs (PC1 left as input too, harmless). */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3; // 太阳能电压，电池电压，电流采样
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;                                    // GPIO配置为模拟输入
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    /* DMA channel1 configuration */
    DMA_DeInit(DMA1_Channel1);                                                  // 指定DMA通道
    DMA_InitStructure.DMA_PeripheralBaseAddr = ADC1_DR_ADDRESS;                 // 设置DMA外设地址
    DMA_InitStructure.DMA_MemoryBaseAddr = (u32)&g_adc_buf;                   // 设置DMA内存地址，ADC转换结果直接访问的地址
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;                          // 设置为外设为数据传输源
    DMA_InitStructure.DMA_BufferSize = ADC_SAMPLE_COUNT * ADC_CHANNEL_COUNT;    // DMA缓冲区大小为1次
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;            // 外设地址寄存器不递增
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;                     // 内存地址寄存器递增
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord; // 外设数据宽度 16位
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;         // 数据宽度16位
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;                             // 循环传输模式
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;                         // 拥有高优先级
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;                                // 禁止内存到内存传输
    DMA_DeInit(DMA1_Channel1);                                                  // 复位通道所有寄存器
    DMA_Init(DMA1_Channel1, &DMA_InitStructure);
    DMA_Cmd(DMA1_Channel1, ENABLE);                                             // 使能DMA通道

    RCC_ADCCLKConfig(RCC_PCLK2_Div6);                    // 设置ADC时钟分频为6 72M/6=12，ADC采样时间不能超过14M

    /* ADC1 configuration */
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;                  // 使用独立模式ADC1
    ADC_InitStructure.ADC_ScanConvMode = ENABLE;                        // 使能扫描模式
    ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;                  // 使能连续转换模式
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None; // 不使用外部触发，软件启动
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;              // 使用右对齐
    ADC_InitStructure.ADC_NbrOfChannel = 3;                             // 只有4个转换通道
    ADC_Init(ADC1, &ADC_InitStructure);

    // ADC_Channel_10 电流采样通道  ADC_Channel_12 太阳能电压通道   ADC_Channel_13 电池电压通道
    // ADC_Channel_1  门限宽度采样   ADC_Channel_2 电流采样
    ADC_RegularChannelConfig(ADC1, ADC_Channel_10, 1, ADC_SampleTime_239Cycles5); // 通道1采样时间为239.5个时钟周期  1/12M *(239.5+12.5) = 21us * 300 =6.3ms
    ADC_RegularChannelConfig(ADC1, ADC_Channel_12, 2, ADC_SampleTime_239Cycles5); // 通道1采样时间为239.5个时钟周期  1/12M *(239.5+12.5) = 21us * 300 =6.3ms
    ADC_RegularChannelConfig(ADC1, ADC_Channel_13, 3, ADC_SampleTime_239Cycles5); // 通道1采样时间为239.5个时钟周期

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
    /* zero point */
    s_ready = 0u;
    s_zero_sum = 0u;
    s_zero_cnt = 0u;
    s_zero_avg = 0u;
    s_last_run_ms = 0u;
    s_last_track_ms = 0u;
    /* smoothing window */
    s_ms = 0u;
    s_win_sum = 0u;
    s_win_cnt = 0u;
    s_win_idx = 0u;
    /* stall + slow path */
    s_stall_flag = 0u;
    s_stall_cnt = 0u;
    s_stall_threshold = ADC_STALL_MA_DEFAULT;
    s_slot_cnt = 0u;
    s_motor_running = 0u; 
    /* link-loss */                      
    s_dma_cnt_last = 0u;
    s_dma_static_cnt = 0u;
    s_dma_dead = 0u;
    s_rail_cnt = 0u;
    s_sensor_open = 0u;
    /* lastest values */
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

/* DMA-counter watchdog + sensor rail check, once per 20ms slot.
 * Must run before the value is converted, so a dead chain never feeds
 * the zero tracker or the stall detector. */
static void ADC_LinkCheck(uint16_t raw)
{
    uint16_t cnt = (uint16_t)DMA_GetCurrDataCounter(DMA1_Channel1);   /* 读当前 CNDTR */

    /* 1) circular DMA walks the whole buffer in ~16ms, so two slots 20ms
     *    apart can never read the same CNDTR unless the chain stopped. */
    if (cnt == s_dma_cnt_last)
    {
        if (s_dma_static_cnt < 0xFFu)
            s_dma_static_cnt++;
        if (s_dma_static_cnt >= 25u) /* 连续 500ms 都不动 */
            s_dma_dead = 1u;         /* 锁存 */
    }
    else
    {
        s_dma_static_cnt = 0u;       /* 静止计数清零，重新计时 */
        s_dma_cnt_last = cnt;
    }

    /* 2) open wire or shorted sensor parks the input at a rail.
     *    This one is recoverable: a sane reading clears both counters. */
    if ((raw <= s_rail_low) || (raw >= ADC_RAIL_HIGH))
    {
        if (s_rail_cnt < 0xFFu)
            s_rail_cnt++;
        if (s_rail_cnt >= 50u)            /* 连续 1 秒 */
            s_sensor_open = 1u;           /* 判传感器断线 */
    }
    else
    {
        s_rail_cnt = 0u;
        s_sensor_open = 0u;               /* 两个都清 —— 自恢复 */
    }
}

/* One 20ms sampling slot: zero calibration / conversion + stall detect. */
static void ADC_SampleSlot(void)
{
    uint16_t raw;
    uint16_t sample_ma;

    s_slot_cnt++;
    raw = ADC_RawAvg(0u);   /* channel 10 = current */
    ADC_LinkCheck(raw);      /* must precede zero tracking + stall detect */

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
            GPRS_Printf("[ADC] zero_avg=%u (%umV)\r\n",
                        (unsigned)s_zero_avg,
                        (unsigned)((u32)s_zero_avg * 3300u / 4095u));
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

    /* 500ms voltage slow path */
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

/* ADC_Tick() 每 10ms 调一次；内部 s_tick_div 数到 2（=20ms）才执行一次 ADC_SampleSlot()； */
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

uint16_t ADC_GetCurrentMa(void)   { return s_current_ma; }
uint16_t ADC_GetSampleMa(void)    { return s_sample_ma; }
uint16_t ADC_GetSolarMv(void)     { return s_so_mv; }
uint16_t ADC_GetBatMv(void)       { return s_bat_mv; }

/* 消费 ADC 的堵转 */
uint8_t ADC_PopStall(void)
{
    uint8_t v = s_stall_flag;
    s_stall_flag = 0u;
    return v;
}

void ADC_SetRailLow(uint16_t v) { s_rail_low = v; }
void ADC_SetStallThresholdMa(uint16_t ma) { s_stall_threshold = ma; }
void ADC_SetMotorRunning(uint8_t running) { s_motor_running = running; }
