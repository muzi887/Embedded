/* ===========================================================================
 * LCD12864 进度条显示头文件
 * GDRAM 画条（矩形块）+ DDRAM 百分比文字（第 4 字符位起）；实心样式 + 可选边框。
 * GPRS 下载进度与 OTA 状态画面共用。
 */

#ifndef __PROGRESSBAR_H
#define __PROGRESSBAR_H

#include "Lcd12864.h"   /* u8 类型经 sys.h 提供 */

/* 进度条配置结构（几何像素直述） */
typedef struct {
    u8 row;                /* 百分比文字所在文本行（0-3）                 */
    u8 y_start;            /* 条起始像素行（0-63，含上边框）              */
    u8 x_word;             /* 条起始字列（0-7，半屏内；1 字 = 16 像素宽） */
    u8 width;              /* 条宽（字数，建议 6~8）                      */
    u8 height;             /* 条总高（像素行，含上下边框）                */
    u8 border_enable;      /* 边框使能（0=禁用，1=启用）                  */
    u8 style;              /* 样式（见下方定义；本版仅 SOLID 生效）       */
} ProgressBarConfig_t;

/* 进度条样式定义 */
#define PROGRESS_STYLE_SOLID     0  /* 实心样式 */
#define PROGRESS_STYLE_GRADIENT  1  /* 渐变样式（预留） */
#define PROGRESS_STYLE_SEGMENTED 2  /* 分段样式（预留） */

/* 函数声明 */
void ProgressBar_Init(ProgressBarConfig_t *config);
void ProgressBar_Update(u8 percent);
void ProgressBar_DrawBorder(ProgressBarConfig_t *config);
void ProgressBar_ShowPercent(u8 row, u8 percent);
void ProgressBar_Clear(ProgressBarConfig_t *config);

/* 全局配置（extern 全局保留 g_ 前缀） */
extern ProgressBarConfig_t g_progressbar_config;

#endif /* __PROGRESSBAR_H */
