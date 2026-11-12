/**
 * @file ProgressBar.h
 * @brief LCD12864进度条显示头文件
 * @version 1.0
 * @date 2025-01-27
 */

#ifndef __PROGRESSBAR_H
#define __PROGRESSBAR_H

#include "Lcd12864.h"
#include <stdint.h>

// 进度条配置结构
typedef struct {
    uint8_t row;           // DDRAM行（0-3，对应4行文本显示）
    uint8_t y_start;       // GDRAM起始Y坐标（0-47，每行16像素）
    uint8_t x_start;       // GDRAM起始X坐标（0-127）
    uint8_t width;         // 进度条宽度（像素，建议128）
    uint8_t height;        // 进度条高度（像素，建议16）
    uint8_t border_enable; // 边框使能（0=禁用，1=启用）
    uint8_t style;         // 样式（0=实心，1=渐变，2=分段）
} ProgressBarConfig_t;

// 进度条样式定义
#define PROGRESS_STYLE_SOLID     0  // 实心样式
#define PROGRESS_STYLE_GRADIENT  1  // 渐变样式
#define PROGRESS_STYLE_SEGMENTED 2  // 分段样式

// 函数声明
void ProgressBar_Init(ProgressBarConfig_t *config);
void ProgressBar_Update(uint8_t percent);
void ProgressBar_DrawBar(ProgressBarConfig_t *config, uint8_t percent);
void ProgressBar_DrawBorder(ProgressBarConfig_t *config);
void ProgressBar_ShowPercent(uint8_t row, uint8_t percent);
void ProgressBar_Clear(ProgressBarConfig_t *config);
void ProgressBar_Update_Incremental(uint8_t new_percent);
void loading(void);

// 全局配置（可在外部修改）
extern ProgressBarConfig_t g_progressbar_config;

#endif /* __PROGRESSBAR_H */
