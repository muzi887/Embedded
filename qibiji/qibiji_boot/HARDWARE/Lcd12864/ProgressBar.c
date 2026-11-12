/**
 * @file ProgressBar.c
 * @brief LCD12864进度条显示实现
 * @version 1.0
 * @date 2025-01-27
 * 
 * 功能说明：
 * - 使用GDRAM绘制进度条（16像素高）
 * - 使用DDRAM显示百分比文字（居中显示）
 * - 支持实心、渐变、分段等多种样式
 * - 支持边框显示
 */

#include "ProgressBar.h"
#include "Lcd12864.h"
#include <stdio.h>
#include <string.h>

// DDRAM行地址映射表（对应4行文本显示）
static const uint8_t ddram_row_addr[] = {0x80, 0x90, 0x88, 0x98};

// 全局配置（使用函数初始化）
ProgressBarConfig_t g_progressbar_config;

// 进度条状态（用于增量更新优化）
static uint8_t g_last_percent = 255;  // 255表示未初始化

// 配置初始化标志
static uint8_t g_config_initialized = 0;

/**
 * @brief 初始化默认配置
 */
static void ProgressBar_InitDefaultConfig(void)
{
    g_progressbar_config.row = 3;              // 第三行（DDRAM地址0x88）
    g_progressbar_config.y_start = 0;         // GDRAM Y坐标32（第三行，每行16像素）
    g_progressbar_config.x_start = 1;          // GDRAM X坐标0（最左边）
    g_progressbar_config.width = 6;          // 宽度128像素（全宽）
    g_progressbar_config.height = 15;          // 高度16像素（一行）
    g_progressbar_config.border_enable = 1;    // 启用边框
    g_progressbar_config.style = 0;            // 实心样式
    g_config_initialized = 1;
}

/**
 * @brief 初始化进度条
 * @param config 进度条配置结构指针，NULL则使用默认配置
 */
void ProgressBar_Init(ProgressBarConfig_t *config)
{
    // 如果配置未初始化，先初始化默认配置
    if (!g_config_initialized) {
        ProgressBar_InitDefaultConfig();
    }
    
    if (config == NULL) {
        config = &g_progressbar_config;
    } else {
        // 更新全局配置
        g_progressbar_config = *config;
        g_config_initialized = 1;
    }
    
    // 清除进度条区域
    ProgressBar_Clear(config);
    
    // 绘制边框（如果启用）
    if (config->border_enable) {
        ProgressBar_DrawBorder(config);
    }
    
    // 初始显示0%
    g_last_percent = 255;  // 重置状态
   // ProgressBar_Update(0);
}

/**
 * @brief 更新进度条显示
 * @param percent 百分比值（0-100）
 */
void ProgressBar_Update(uint8_t percent)
{
    ProgressBarConfig_t *config;
    
    // 如果配置未初始化，先初始化默认配置
    if (!g_config_initialized) {
        ProgressBar_InitDefaultConfig();
    }
    
    config = &g_progressbar_config;
    
    // 限制百分比范围
    if (percent > 100) {
        percent = 100;
    }
    
    // 绘制进度条
    ProgressBar_DrawBar(config, percent);
    
    // 显示百分比文字（如果百分比变化）
    if (percent != g_last_percent) {
        ProgressBar_ShowPercent(ddram_row_addr[config->row], percent);
        g_last_percent = percent;
    }
}

/**
 * @brief 绘制进度条到GDRAM
 * @param config 配置结构指针
 * @param percent 百分比值（0-100）
 */
void ProgressBar_DrawBar(ProgressBarConfig_t *config, uint8_t percent)
{
    uint8_t i, j;
    uint8_t fill_width;
    uint8_t pixel_data;
		uint8_t y_pos;
    
    // 计算填充宽度（像素）
    fill_width = (config->width * percent) / 100;
    
    // 切换到图形显示模式
    WRCommandH(0x34);  // 扩展指令集
    WRCommandH(0x36);  // 开启绘图显示
    
    // 绘制进度条（逐行绘制）
    for (j = 0; j < config->height; j++) {
        y_pos = config->y_start + j;
        
        WRCommandH(0x80 + y_pos);						// 设置Y坐标
        WRCommandH(ddram_row_addr[config->row] + config->x_start); // 设置X坐标
        // 绘制已填充区域
        for (i = 0; i < fill_width; i++) {
            // 写入像素数据（每个像素需要2字节）
            WRDataH(0xFF);
            WRDataH(0xFF);
        }
        
//        // 清除未填充区域
//        for (i = fill_width; i < config->width; i++) {
//            WRCommandH(0x80 + config->x_start + i);
//            WRDataH(0x00);
//            WRDataH(0x00);
//        }
    }
}

/**
 * @brief 绘制进度条边框
 * @param config 配置结构指针
 */
void ProgressBar_DrawBorder(ProgressBarConfig_t *config)
{
    uint8_t i, j;
    
    // 切换到图形显示模式
    WRCommandH(0x34);
    WRCommandH(0x36);
    
    // 绘制上边框
    //WRCommandH(ddram_row_addr[config->row] + config->y_start);
	  WRCommandH(0x80 + config->y_start);
	  WRCommandH(ddram_row_addr[config->row] + config->x_start);
    for (i = 0; i < config->width; i++) {
        WRDataH(0xFF);  // 半亮边框
        WRDataH(0xFF);
    }
    
    // 绘制下边框
    WRCommandH(0x80 + config->y_start + config->height);
		WRCommandH(ddram_row_addr[config->row] + config->x_start);
    for (i = 0; i < config->width; i++) {
        WRDataH(0xFF);
        WRDataH(0xFF);
    }
    
    // 绘制左边框
		
    for (j = 0; j < config->height-1; j++) {
        WRCommandH(0x80 + config->y_start + j+1);
        WRCommandH(ddram_row_addr[config->row] + config->x_start);
        WRDataH(0x80);
        WRDataH(0x00);
    }
		
    
    // 绘制右边框
    for (j = 0; j < config->height-1; j++) {
        WRCommandH(0x80 + config->y_start + j+1);
        WRCommandH(ddram_row_addr[config->row] + config->x_start + config->width - 1);
        WRDataH(0x00);
        WRDataH(0x01);
    }
}

/**
 * @brief 在进度条中央显示百分比文字
 * @param row DDRAM行地址（0x80/0x88/0x90/0x98）
 * @param percent 百分比值（0-100）
 */
void ProgressBar_ShowPercent(uint8_t row, uint8_t percent)
{
    char percent_str[6];
    uint8_t str_len;
    uint8_t start_pos;
    uint8_t i;
    
    // 限制百分比范围
    if (percent > 100) {
        percent = 100;
    }
    
    // 格式化百分比字符串 "XX%" (2-4个字符: "0%", "10%", "100%")
    sprintf(percent_str, "%d%%", percent);
    str_len = strlen(percent_str);

		
    
    // 确保字符串长度在2-4个字符范围内
    if (str_len < 2) {
        str_len = 2;
    }
    if (str_len > 4) {
        str_len = 4;
    }
		
		if(str_len == 2)
		{
			percent_str[2] = 0x20;
			percent_str[3] = 0x20;
		}
		
		if(str_len == 3)
		{
			percent_str[3] = 0x20;
		}
    
    // 计算居中位置（每行16个字符位置）
    start_pos = 3;
    
    // 切换到文本显示模式（退出扩展指令寄存器）
    WRCommandH(0x30);
    
    // 清除该行的显示区域（清除4个字符位置，确保完全清除）
    for (i = 0; i < 4; i++) {
        WRCommandH(row + start_pos + i);
        WRDataH(0x20);  // 空格
    }
    
    // 显示百分比文字（每个字符对应一个WRDataH写操作）
    for (i = 0; i < 2; i++) {
        WRCommandH(row + start_pos + i);
        WRDataH(percent_str[i*2]);  // 字符ASCII码
		  	WRDataH(percent_str[i*2+1]); 		
    }
}

/**
 * @brief 清除进度条区域
 * @param config 配置结构指针
 */
void ProgressBar_Clear(ProgressBarConfig_t *config)
{
    uint8_t i, j;
    uint8_t row_addr;
    
    // 切换到图形显示模式
    WRCommandH(0x34);
    WRCommandH(0x36);
    
    // 清除进度条区域
    for (j = 0; j < config->height; j++) {
        WRCommandH(0x80 + config->y_start);
			  WRCommandH(0x80 + config->x_start);
        for (i = 0; i < config->width; i++) {
            WRDataH(0x00);
            WRDataH(0x00);
        }
    }
    
    // 清除百分比文字（清除DDRAM对应行）
    row_addr = ddram_row_addr[config->row];
		WRCommandH(row_addr + i);
    for (i = 0; i < 16; i++) {
        
        WRDataH(0x00);  // 空格
        WRDataH(0x00);
    }
}

void clearall()
{
	int i,j;
	WRCommandH(0x34);
  WRCommandH(0x36);
	        for (j = 0; j < 32; j++) {
            WRCommandH(0x80 + j);
            WRCommandH(0x80);
            for (i = 0; i < 16; i++) {
                WRDataH(0x20);
                WRDataH(0x20);
            }
        }
}

/**
 * @brief 增量更新进度条（优化版本，只更新变化部分）
 * @param new_percent 新的百分比值（0-100）
 */
void ProgressBar_Update_Incremental(uint8_t new_percent)
{
    ProgressBarConfig_t *config;
    uint8_t old_percent;
    uint8_t old_width;
    uint8_t new_width;
    uint8_t j;
    uint8_t y_pos;
    uint8_t i;
    
    // 如果配置未初始化，先初始化默认配置
    if (!g_config_initialized) {
        ProgressBar_InitDefaultConfig();
    }
    
    config = &g_progressbar_config;
    old_percent = g_last_percent;
    
    // 限制范围
    if (new_percent > 100) {
        new_percent = 100;
    }
    
    // 如果是首次更新，使用完整更新
    if (old_percent == 255) {
        ProgressBar_Update(new_percent);
        return;
    }
    
    // 如果百分比没有变化，不更新
    if (new_percent == old_percent) {
        return;
    }
    
    // 计算需要更新的区域
    old_width = (config->width * old_percent) / 100;
    new_width = (config->width * new_percent) / 100;
    
    // 切换到图形显示模式
    WRCommandH(0x34);
    WRCommandH(0x36);
    
    // 更新变化的部分
    if (new_percent > old_percent) {
        // 绘制新增的部分
        for (j = 0; j < config->height; j++) {
            y_pos = config->y_start + j;
            WRCommandH(0x80 + y_pos);
            
            for (i = old_width; i < new_width; i++) {
                WRCommandH(0x80 + config->x_start + i);
                WRDataH(0xFF);
                WRDataH(0xFF);
            }
        }
    } else {
        // 清除减少的部分
        for (j = 0; j < config->height; j++) {
            y_pos = config->y_start + j;
            WRCommandH(0x80 + y_pos);
            
            for (i = new_width; i < old_width; i++) {
                WRCommandH(0x80 + config->x_start + i);
                WRDataH(0x00);
                WRDataH(0x00);
            }
        }
    }
    
    // 更新百分比文字
    ProgressBar_ShowPercent(ddram_row_addr[config->row], new_percent);
    
    // 更新状态
    g_last_percent = new_percent;
}
