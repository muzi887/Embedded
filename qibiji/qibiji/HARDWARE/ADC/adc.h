#ifndef __ADC_H
#define __ADC_H

#include "stm32f10x.h"
#include "reg.h"
#define N 256 // 每通道采样128次
#define M 3   // 为3个通道

/* 电流计算公式：
 * I=(当前采样电压-起始参考电压)/(6*0.02)A  (6*0.005) (6*0.05  //50毫欧)
 * ADC值转换为电压值：电压=ADC值*3.3/4096，参考电压单位为V，但是我们换算mV,4096/1000=4.096，采样电阻直接换算为mA
 * 综合公式可以得出： I= (当前ADC值-起始参考ADC值)* (3.3 / 4.096 / 0.12)
 */

#define ADC2CURT (float)(3.3f / 4.096f / (SAMPLING_RES * GAIN) )

#define VOLT_REF 3.3f // ADC参考电压.标准值是3.3V,通常实际测量得到3.307V
/* 电流采样电路使用放大电路 和 采样电阻 */
#define GAIN 2.0f            // 放大倍数
#define SAMPLING_RES 0.05f // 采样电阻
/** 电压分辨率 =  ADC(Hex) * 3.3 / 2^n * 1000(mV) 单位为mV
 * STM32的ADC分辨率n = 12bit,4096
 */
#define VOLT_RESOLUTION ((float)((VOLT_REF / (float)(4096)) * (float)1000))      // ADC 电压分辨率,单位:mV
#define VOLTBUS_RESOLUTION ((float)(3.3f / (float)4096) * (42.4f + 2.0f) / 2.0f) // 总线电压值分辨率,电压分压:42.4k欧/2.0k欧

extern u16 After_filter[M]; // 滤波后ADC平均值之后的结果

extern u16 g_adc_val[M]; // 滤波后ADC平均值之后的结果

extern Register armReg;

void AD_Process(void); // AD 数据处理
void ADC1_Init(void);
void ADC1_Read_filter(void);
void ADC1_Read_filter_old(void);
u8 ADC_CurrentIsReady(void);
void ADC1_Mode_Config(void);
void ADC1_GPIO_Config(void);

// void calc_adc_val_init(void);   // 计算adc采样初始值
void calc_adc_val(uint16_t *p); // 计算平均值

#endif /* __ADC_H */
