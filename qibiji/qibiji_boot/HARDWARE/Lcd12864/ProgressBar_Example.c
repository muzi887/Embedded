/**
 * @file ProgressBar_Example.c
 * @brief LCD12864进度条使用示例
 * @version 1.0
 * @date 2025-01-27
 * 
 * 使用说明：
 * 1. 包含ProgressBar.h头文件
 * 2. 调用ProgressBar_Init()初始化
 * 3. 调用ProgressBar_Update()更新进度
 */

#include "ProgressBar.h"
#include "display.h"
#include "delay.h"

/**
 * @brief 示例1：基本使用
 */
void Example1_BasicUsage(void)
{
    // LCD初始化
    Lcd_Init();
    
    // 配置进度条
    ProgressBarConfig_t config = {
        .row = 2,              // 第三行（DDRAM地址0x88）
        .y_start = 32,         // GDRAM Y坐标32（第三行，每行16像素）
        .x_start = 0,          // 从左边开始
        .width = 128,          // 全宽128像素
        .height = 16,          // 16像素高
        .border_enable = 1,    // 启用边框
        .style = PROGRESS_STYLE_SOLID  // 实心样式
    };
    
    // 初始化进度条
    ProgressBar_Init(&config);
    
    // 更新进度条（0% -> 100%）
    uint8_t i;
    for (i = 0; i <= 100; i += 5) {
        ProgressBar_Update(i);
        delay_ms(100);  // 延迟100ms
    }
}

/**
 * @brief 示例2：与其他显示内容结合
 */
void Example2_WithOtherContent(void)
{
    // LCD初始化
    LCDInit();
    delay_us(500);
    
    // 显示标题（第一行）
    uint8_t title[] = {0xb5,0xe7,0xbb,0xfa,0x3a,0x20,0x20,0x20};  // "电机："
    ShowQQCharH(0x80, title, 4);
    
    // 显示信息（第二行）
    uint8_t info[] = {0xd4,0xcb,0xd0,0xd0,0xd6,0xd0,0xcc,0xac};  // "运行状态"
    ShowQQCharH(0x90, info, 4);
    
    // 配置并显示进度条（第三行）
    ProgressBarConfig_t config = {
        .row = 2,
        .y_start = 32,
        .x_start = 0,
        .width = 128,
        .height = 16,
        .border_enable = 1,
        .style = PROGRESS_STYLE_SOLID
    };
    ProgressBar_Init(&config);
    ProgressBar_Update(75);  // 显示75%
    
    // 显示页脚（第四行）
    uint8_t footer[] = {0xd7,0xb4,0xcc,0xac,0x3a,0x20,0x20,0x20};  // "状态："
    ShowQQCharH(0x98, footer, 4);
}

/**
 * @brief 示例3：使用默认配置
 */
void Example3_DefaultConfig(void)
{
    // LCD初始化
    Lcd_Init();
    
    // 使用默认配置初始化进度条
    ProgressBar_Init(NULL);
    
    // 更新进度条
    ProgressBar_Update(50);  // 显示50%
}

/**
 * @brief 示例4：增量更新（性能优化）
 */
void Example4_IncrementalUpdate(void)
{
    // LCD初始化
    Lcd_Init();
    
    // 初始化进度条
    ProgressBar_Init(NULL);
    
    // 使用增量更新（只更新变化部分，性能更好）
    uint8_t i;
    for (i = 0; i <= 100; i++) {
        ProgressBar_Update_Incremental(i);
        delay_ms(50);
    }
}

/**
 * @brief 示例5：不同样式
 */
void Example5_DifferentStyles(void)
{
    // LCD初始化
    Lcd_Init();
    
    // 实心样式
    ProgressBarConfig_t config_solid = {
        .row = 0,
        .y_start = 0,
        .x_start = 0,
        .width = 128,
        .height = 16,
        .border_enable = 1,
        .style = PROGRESS_STYLE_SOLID
    };
    ProgressBar_Init(&config_solid);
    ProgressBar_Update(50);
    delay_ms(2000);
    
    // 渐变样式
    ProgressBarConfig_t config_gradient = {
        .row = 1,
        .y_start = 16,
        .x_start = 0,
        .width = 128,
        .height = 16,
        .border_enable = 1,
        .style = PROGRESS_STYLE_GRADIENT
    };
    ProgressBar_Init(&config_gradient);
    ProgressBar_Update(50);
    delay_ms(2000);
    
    // 分段样式
    ProgressBarConfig_t config_segmented = {
        .row = 2,
        .y_start = 32,
        .x_start = 0,
        .width = 128,
        .height = 16,
        .border_enable = 1,
        .style = PROGRESS_STYLE_SEGMENTED
    };
    ProgressBar_Init(&config_segmented);
    ProgressBar_Update(50);
}

/**
 * @brief 示例6：集成到homepage函数
 */
void Example6_IntegrateToHomepage(void)
{
    LCDInit();
    delay_us(500);
    
    // 显示电机信息
    DisplayFloat(&motor[5], g_adc_val[0]/10, 0);
    DisplayFloat(&motor[11], g_adc_val[2], 0);
    kaiduVal(&kaidu[6], &kaidu[12]);
    
    ShowQQCharH(0x90, kaidu, 8);   // 第一行：开度
    ShowQQCharH(0x88, motor, 8);   // 第二行：电机
    
    // 在第三行显示进度条（开度值）
    ProgressBarConfig_t config = {
        .row = 2,              // 第三行
        .y_start = 32,         // GDRAM Y坐标32
        .x_start = 0,
        .width = 128,
        .height = 16,
        .border_enable = 1,
        .style = PROGRESS_STYLE_SOLID
    };
    
    ProgressBar_Init(&config);
    extern u8 kaival;  // 假设kaival是开度值（0-100）
    ProgressBar_Update(kaival);
    
    // 显示其他内容
    ShowQQCharH(0x98, liuliang, 8);  // 第四行：流量
    
    WRGDRAM1(0xff, 0xc0, 0x03);
    WRGDRAM(3, &code[0][0]);
    WRGDRAM(4, &code[1][0]);
}

/**
 * @brief 示例7：定时器更新进度条
 */
void Example7_TimerUpdate(void)
{
    static uint8_t progress = 0;
    
    // 在定时器中断或主循环中调用
    if (progress < 100) {
        progress++;
        ProgressBar_Update_Incremental(progress);
    }
}
