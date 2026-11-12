#include "stm32f10x.h"
#include "adc.h"
#include "timer.h"
#include "stdio.h"
#include "delay.h"
#include "main.h"
#include "event_pub.h"
// #include "dc_motor.h"

extern u8 flag20ms;

#define ADC1_DR_Address ((u32)0x4001244C)
__IO u16 ADC_ConvertedValue;

u16 REFER[M] = {1580, 0, 0};
u16 AD_Value[N][M]; // 存储所有ADC转换结果，同时也是DMA的目标地址
// u16 After_filter[M];
u16 g_adc_val[M]; // 滤波后ADC平均值之后的结果
u16 gate_range_adc_value;

/*初始化ADC1 */
void ADC1_Init(void)
{
    ADC1_GPIO_Config();
    ADC1_Mode_Config();
}

/*配置采样通道端口 使用GPIO时钟	  配置ADC输入PC0-PC3端口信号*/
void ADC1_GPIO_Config(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3; // 太阳能电压，电池电压，电流采样
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;                                    // GPIO配置为模拟输入
    GPIO_Init(GPIOC, &GPIO_InitStructure);
}

/*配置ADC1的工作模式为MDA模式  */ // ADC 外设函数  DMA中断可以影响其他通道的采样结果
void ADC1_Mode_Config(void)
{
    DMA_InitTypeDef DMA_InitStructure;
    ADC_InitTypeDef ADC_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    // DMA配置
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE); // 使能MDA1时钟
    /* DMA channel1 configuration */
    DMA_DeInit(DMA1_Channel1);                                                  // 指定DMA通道
    DMA_InitStructure.DMA_PeripheralBaseAddr = ADC1_DR_Address;                 // 设置DMA外设地址
    DMA_InitStructure.DMA_MemoryBaseAddr = (u32)&AD_Value;                      // 设置DMA内存地址，ADC转换结果直接访问的地址
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;                          // 设置为外设为数据传输源
    DMA_InitStructure.DMA_BufferSize = N * M;                                   // DMA缓冲区大小为1次
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;            // 外设地址寄存器不递增
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;                     // 内存地址寄存器递增
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord; // 外设数据宽度 16位
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;         // 数据宽度16位
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;                             // 循环传输模式
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;                         // 拥有高优先级
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;                                // 禁止内存到内存传输
    DMA_Init(DMA1_Channel1, &DMA_InitStructure);

    /* Enable DMA channel1 */
    DMA_Cmd(DMA1_Channel1, ENABLE); // 使能DMA通道

    // ADC配置
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE); // 使能ADC1时钟
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);                    // 设置ADC时钟分频为6 72M/6=12,ADC采样时间不能超过14M

    /* ADC1 configuration */
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;                  // 使用独立模式ADC1
    ADC_InitStructure.ADC_ScanConvMode = ENABLE;                        // 使能扫描模式
    ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;                  // 使能连续转换模式
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None; // 不使用外部触发，软件启动
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;              // 使用右对齐
    ADC_InitStructure.ADC_NbrOfChannel = 3;                             // 只有4个转换通道
    ADC_Init(ADC1, &ADC_InitStructure);

    // ADC_Channel_10 电流采样通道  ADC_Channel_12 太阳能电压通道   ADC_Channel_13 电池电压通道
    /* ADC1 regular channel11 configuration */                                    // ADC_Channel_1  门限宽度采样   ADC_Channel_2 电流采样
    ADC_RegularChannelConfig(ADC1, ADC_Channel_10, 1, ADC_SampleTime_239Cycles5); // 通道1采样时间为239.5个时钟周期  1/12M *(239.5+12.5) = 21us * 300 =6.3ms
    //		ADC_RegularChannelConfig(ADC1, ADC_Channel_11, 2, ADC_SampleTime_239Cycles5); //通道1采样时间为239.5个时钟周期
    ADC_RegularChannelConfig(ADC1, ADC_Channel_12, 2, ADC_SampleTime_239Cycles5); // 通道1采样时间为239.5个时钟周期  1/12M *(239.5+12.5) = 21us * 300 =6.3ms
    ADC_RegularChannelConfig(ADC1, ADC_Channel_13, 3, ADC_SampleTime_239Cycles5); // 通道1采样时间为239.5个时钟周期

    ADC_DMACmd(ADC1, ENABLE); // 使能ADC的DMA
    ADC_Cmd(ADC1, ENABLE);    // 使能ADC1

    /* Enable ADC1 reset calibaration register */
    ADC_ResetCalibration(ADC1);
    /* Check the end of ADC1 reset calibration register */
    while (ADC_GetResetCalibrationStatus(ADC1))
    {
    }

    /* Start ADC1 calibaration */
    ADC_StartCalibration(ADC1);
    /* Check the end of ADC1 calibration */
    while (ADC_GetCalibrationStatus(ADC1))
    {
    }

    /* Start ADC1 Software Conversion */
    ADC_SoftwareStartConvCmd(ADC1, ENABLE); // 开始转换
}

// void calc_adc_val_init()
// {
//     u16 t = 0;
//     /* init_adc_val存储电流采样对应的参考电压ADC值，用于电流滤波 */
//     REFER[0] = g_adc_val[0]; /* 取第一次得到的值 */
//     for (t = 0; t < 1000; t++)
//     {
//         REFER[0] += g_adc_val[0]; /* 当前的值和上一次存储的值相加 */
//         REFER[0] /= 2;            /* 取平均值 */
//         delay_ms(1);
//     }
//     // printf("init_adc_val = %d\n", REFER[0]);
// }

/**
 * @brief       计算ADC的平均值滤波
 * @param       * p 存储ADC值的指针地址
 * @note        此函数对电压、温度、电流对应的ADC值进行滤波
 *              p[0]-p[2]对应的分别是电压、温度和电流
 * @retval      无
 */
void calc_adc_val(uint16_t *p)
{
    u32 sum = 0;
    u8 i = 0;
    u16 count;

    for (count = 0; count < N; count++)
    {
        sum += AD_Value[count][0];
    }
    p[i] = sum >> 8;
    sum = 0;
}

void AD_Process()
{
    //if (gprsonlineflag)
        ADC1_Read_filter();
}

/*平均值滤波*/
void ADC1_Read_filter_old(void)
{
    u32 sum = 0;
    u8 i = 0;
    u16 count;

    float temp_c = 0.0;
    static float add_adc = 0, add_adc0 = 0, current = 0.0;
    static float init_adc_value = 0;
    static u8 adc_count1 = 0, adc_count2 = 0, tcnt = 0;

    if (flag20ms) // 20ms 触发路径：处理通道0（电流）并维护计时器 tcnt
    {
        flag20ms = 0;
        adc_count1++;
        tcnt++;

        /* 计算通道0的短期平均值，结果放入 g_adc_val[0] */
        calc_adc_val(g_adc_val);
        /* 累加短期平均以便做更长周期的平均（用于计算电流） */
        add_adc += g_adc_val[0];

        if (adc_count1 >= 15) /* 每累计 15 次（≈ 300ms）处理一次 */
        {
            add_adc = (float)(add_adc / adc_count1); /* 取平均值 */

            if (adc_count2 <= 16) /* 采集16次ADC平均值作为参考电压的ADC值 */
            {
                adc_count2++;
                init_adc_value += add_adc;

                if (adc_count2 == 16)
                {
                    adc_count2 = 17;                           /* 不再进入 */
                    init_adc_value = (init_adc_value / 16.0f); /* 存储初始ADC值 */

                    // printf("------init_adc_value = %.2f---------\n", init_adc_value);
                }
            }
            // printf("add_adc = %.2f,%.2f--\n",add_adc,init_adc_value);
            if (adc_count2 >= 17) /* 采集完参考ADC值后，采集电流通道的当前ADC值 */
            {
                /* 电流计算：减去基线后乘以缩放系数 */
                temp_c = (add_adc - init_adc_value) * ADC2CURT;

                /* 一阶低通滤波，系数 0.6/0.4 用于平滑突变 */
                current = (float)((current * (float)0.60) + ((float)0.40 * temp_c));

                if (current <= 20) /* 过滤掉微小的电流干扰 */
                {
                    current = 0.0;
                }

                /* 写回寄存器结构供系统其余部分使用（单位为mA） */
                armReg.motor_curr = (u16)current;

                // printf("motor_curr = %d\n", armReg.motor_curr);

                /* 过流保护：若超过配置阈值则停止电机 */
                if (sys_moter_state != MOTOR_STOP && current > (armReg.motor_warning_curr_upper * 100))
                {
                    dcmotor_stop();
                    printf("-stop5");
                    sys_moter_state = MOTOR_STOP;
                }

                // else
                // printf("temp_c = %.2f,current = %.2f\n", temp_c, current);
                // 此处为滤波后的结果
                // After_filter[0] = current / 10.0f;
                // armReg.bat_curr=After_filter[0];

                // printf("add_adc = %0.2f\n",current);
                // armReg.change = 1
            }
            add_adc = 0;
            adc_count1 = 0;
        }
    }

    /* 慢路径：每 tcnt 达到 25 时处理其它通道（电压等），大约每 0.5s 执行一次 */
    if (tcnt >= 25)
    {
        tcnt = 0;
        for (i = 1; i < M; i++)
        {
            for (count = 0; count < N; count++)
            {
                /* 只累加高于参考基线的部分，避免负值干扰 */
                if (AD_Value[count][i] >= REFER[i])
                    sum += AD_Value[count][i] - REFER[i];
            }

            /* 缩放公式：((sum * 363) >> 20) + 3
             * - 363 与右移 20 是固定缩放系数（实现定点乘法）
             * - +3 为偏移/校正值（来自硬件标定）
             * 建议把这些常数抽成宏并在注释中记录标定来源
             */
            g_adc_val[i] = ((sum * 363) >> 20) + 3; /* 3 为分压电路输出的偏移值 */
            sum = 0;
            // printf("g_adc_val[%d] = %d\n",i,g_adc_val[i]);
        }
    }
}

// 平均值滤波（新）
void ADC1_Read_filter(void)
{
    u32 sum = 0;
    u8 i = 0;
    u16 count,test;

    float temp_c = 0.0;
    static float add_adc = 0, current = 0.0;
    static float init_adc_value = 0;
    static u8 adc_count1 = 0, adc_count2 = 0, tcnt = 0;
    static u8 stall_detect_count = 0; // 堵转连续检测计数器
    static u8 init_complete = 0;      // 初始化完成标志
		
		static u8 cnt=0,index=0;
		static u16 val[25];
		static u32 isum = 0;

    if (flag20ms) // 20ms采集
    {
        flag20ms = 0;
        adc_count1++;
        tcnt++;
				
				for (count = 0; count < N; count++)
				{
						sum += AD_Value[count][0];
				}
				test = sum * 6 / 640 ;
				
				sum = 0;
				
				if (cnt < 25)
				{
						val[index] = test;
						isum += test;

						cnt++;
				}
				/* FIFO 已满，先进先出 */
				else
				{
						/* 减去即将被覆盖的旧数据 */
						isum -= val[index];

						/* 覆盖旧数据 */
						val[index] = test;
						isum += test;
						
						armReg.motor_curr = isum/25;
						
						//printf("\n--I = %d--%d\n", test,armReg.motor_curr);
				}

				index++;
				if (index >= 25)
				{
						index = 0;
				}
				

				// 优化5: 连续检测机制，避免误触发
				if (sys_moter_state != MOTOR_STOP)
				{
						if (test > (armReg.motor_warning_curr_upper * 100))
						{
								stall_detect_count++;
								// 连续1次检测到超限才停止（约300ms），提高可靠性
								if (stall_detect_count >= 10)
								{
										EventPost(EVT_ALARM);
										dcmotor_stop();
										// printf("-stop5: stall detected, current=%.2f\n", current);
										sys_moter_state = MOTOR_STOP;
										stall_detect_count = 0; // 重置计数器
								}
						}
						else
						{
								stall_detect_count = 0; // 电流正常时重置计数器
						}
				}
				else
				{
						stall_detect_count = 0; // 电机停止时重置计数器
				}
        
				
				
				//                if (current <= 20.0f)
//                {
//                    current = 0.0f;
//                }

//        calc_adc_val(g_adc_val); /* 计算ADC的平均值 */
//        // 电流采样对应通道0
//        add_adc += g_adc_val[0]; /* 取电流采样通道对应的ADC值，进行累计 */

//        // printf("\n--add_adc = %d-----\n", g_adc_val[0] * 30);

//        if (adc_count1 >= 15) /* 累计15次（300ms） */
//        {
//            add_adc = (float)(add_adc / adc_count1); /* 取平均值 */

//            // 优化1: 只在电机停止时初始化参考值，避免包含运行电流
//            if (!init_complete && sys_moter_state == MOTOR_STOP)
//            {
//                if (adc_count2 < 16) /* 采集16次ADC平均值作为参考电压的ADC值 */
//                {
//                    adc_count2++;
//                    init_adc_value += add_adc; /* 将平均值累计起来 */

//                    if (adc_count2 == 16) /* 平均值累计16次 */
//                    {
//                        init_adc_value = (init_adc_value / 16.0f); /* 存储初始ADC值 */
//                        init_complete = 1;                         // 标记初始化完成
//                    }
//                }
//            }

//            // 优化2: 初始化完成后才进行电流计算和堵转检测
//            if (init_complete)
//            {
//                temp_c = (add_adc - init_adc_value) * ADC2CURT; /* 计算电流 */

//                // 优化3: 使用更快的滤波系数，提高响应速度（0.7/0.3替代0.6/0.4）
//                current = (float)((current * 0.70f) + (0.30f * temp_c));

//                // 优化4: 改进小电流过滤逻辑
//                if (current <= 20.0f)
//                {
//                    current = 0.0f;
//                }

//                // 写回寄存器结构供系统其余部分使用（单位为mA）
//                armReg.motor_curr = current;
//                // printf("motor_curr = %d\n", armReg.motor_curr);

//                // 优化5: 连续检测机制，避免误触发
//                if (sys_moter_state != MOTOR_STOP)
//                {
//                    if (current > (armReg.motor_warning_curr_upper * 100))
//                    {
//                        stall_detect_count++;
//                        // 连续1次检测到超限才停止（约300ms），提高可靠性
//                        if (stall_detect_count >= 1)
//                        {
//                            dcmotor_stop();
//                            // printf("-stop5: stall detected, current=%.2f\n", current);
//                            sys_moter_state = MOTOR_STOP;
//                            stall_detect_count = 0; // 重置计数器
//                        }
//                    }
//                    else
//                    {
//                        stall_detect_count = 0; // 电流正常时重置计数器
//                    }
//                }
//                else
//                {
//                    stall_detect_count = 0; // 电机停止时重置计数器
//                }
//            }

//            add_adc = 0;
//            adc_count1 = 0;
//        }
    }

    // 处理其他通道的ADC值（电压等）
    if (tcnt >= 25)
    {
        tcnt = 0;
        for (i = 1; i < M; i++)
        {
            for (count = 0; count < N; count++)
            {
                if (AD_Value[count][i] >= REFER[i])
                    sum += AD_Value[count][i] - REFER[i];
            }

            g_adc_val[i] = ((sum * 363) >> 20) + 3; // 3 分压电路输出电压值
            sum = 0;
            // printf("g_adc_val[%d] = %d\n",i,g_adc_val[i]);
        }
    }
}
