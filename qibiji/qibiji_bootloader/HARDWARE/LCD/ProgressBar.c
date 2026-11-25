/* ===========================================================================
 * LCD12864 进度条 - qibiji_bootloader
 * =========================================================================== */
#include "ProgressBar.h"
#include <stdio.h>
#include <string.h>

/* 全局配置 */
ProgressBarConfig_t g_progressbar_config;

/* 进度条状态 */
static u8 s_last_percent = 255;         /* 255 = 尚未画过 */
static u8 s_config_initialized = 0;

/* 默认配置（= OTA 装配几何），与实机画面一致。*/
static void ProgressBar_InitDefaultConfig(void)
{
    g_progressbar_config.row           = 3;     /* 百分比：文本第 4 行      */
    g_progressbar_config.y_start       = 50;    /* 底行区域（像素行 50 起） */
    g_progressbar_config.x_word        = 1;     /* 像素列 16 起             */
    g_progressbar_config.width         = 6;     /* 宽 6 字 = 96 像素        */
    g_progressbar_config.height        = 11;    /* 含上下边框共 11 像素行   */
    g_progressbar_config.border_enable = 1;
    g_progressbar_config.style         = PROGRESS_STYLE_SOLID;
    s_config_initialized = 1;
}

/* 初始化进度条（清条区 + 画边框；NULL 则用默认配置）
 * config 进度条配置结构指针
 */
void ProgressBar_Init(ProgressBarConfig_t *config)
{
    if (!s_config_initialized) {
        ProgressBar_InitDefaultConfig();
    }

    if (config == NULL) {
        config = &g_progressbar_config;
    } else {
        g_progressbar_config = *config;
    }

    ProgressBar_Clear(config);                   /* 条区 + 百分比行清空 */

    if (config->border_enable) {
        ProgressBar_DrawBorder(config);
    }

    s_last_percent = 255;                        /* 重置状态 */
}

/* 更新进度条显示 */
void ProgressBar_Update(u8 percent)
{
    ProgressBarConfig_t *config;
    u8 fill_words;

    if (!s_config_initialized) {
        ProgressBar_InitDefaultConfig();
    }
    config = &g_progressbar_config;

    if (percent > 100) {
        percent = 100;
    }

    /* 实心填充：行覆盖 y_start..y_start+height-2（底行像素留给下边框），
     * 字覆盖 x_word 起 fill_words 个字列；0% 时无事可做。
     * 填充值 0xFF 会盖住已填充字列的上边框与左右边框像素——与原实机行为一致。 */
    fill_words = (u8)((config->width * percent) / 100);
    if (fill_words > 0) {
        LCD12864_FillBlock(config->y_start, config->height - 1,
                           config->x_word, fill_words, 0xFF);
    }

    if (percent != s_last_percent) {             /* 百分比文字只在变化时重画 */
        ProgressBar_ShowPercent(config->row, percent);
        s_last_percent = percent;
    }
}

/* 绘制进度条边框（上下实线 + 左右单像素）*/
void ProgressBar_DrawBorder(ProgressBarConfig_t *config)
{
    /* 上/下边框：整宽 0xFF 实线；左/右边框：端字列内的单像素
     * （0x80 = 字内最左点，0x01 = 字内最右点）。
     * FillBlock 按像素行入参，自动处理 ST7920 上下半屏地址偏移。 */
    LCD12864_FillBlock(config->y_start, 1,
                       config->x_word, config->width, 0xFF);            /* 上边框 */
    LCD12864_FillBlock(config->y_start + config->height - 1, 1,
                       config->x_word, config->width, 0xFF);            /* 下边框 */
    LCD12864_FillBlock(config->y_start + 1, config->height - 2,
                       config->x_word, 1, 0x80);                        /* 左边框 */
    LCD12864_FillBlock(config->y_start + 1, config->height - 2,
                       config->x_word + config->width - 1, 1, 0x01);    /* 右边框 */
}

/**
 * @brief 在文本行 row 上显示百分比文字（第 4 字符位起，居中位置与原版一致）
 * @param row 文本行号（0-3）
 * @param percent 百分比值（0-100）
 */
void ProgressBar_ShowPercent(u8 row, u8 percent)
{
    char percent_str[5];    /* "100%" + '\0' */
    u8 line[16];
    u8 len;
    u8 i;

    if (percent > 100) {
        percent = 100;
    }
    if (row > 3) {
        row = 3;
    }

    /* 格式化百分比字符串（"0%"、"50%"、"100%"，2-4 字符） */
    sprintf(percent_str, "%d%%", percent);
    len = strlen(percent_str);
    if (len > 4) {
        len = 4;
    }

    /* 整行 16 字节重画：前 3 格空格 + 百分比（不足 4 字符右补空格）+ 尾 1 格空格。
     * 替代原版「清 4 字符位后只写 2 格」的绕行写法，画面相同。 */
    for (i = 0; i < 16; i++) {
        line[i] = 0x20;
    }
    for (i = 0; i < len; i++) {
        line[6 + i] = (u8)percent_str[i];
    }

    LCD12864_ShowHanzi(row, line, 8);    /* 8 格整行；函数内部自带切回文本模式 */
}

/**
 * @brief 清除进度条区域（条区 GDRAM + 百分比行）
 * @param config 配置结构指针
 */
void ProgressBar_Clear(ProgressBarConfig_t *config)
{
    u8 line[16];
    u8 i;

    /* 条区 GDRAM 清空（原版此函数 Y 地址不随循环递增、只反复清同一行，
     * 且 X 用的 0x80+x_start 与画条用的基址还不一致——重写版按几何全清） */
    LCD12864_FillBlock(config->y_start, config->height,
                       config->x_word, config->width, 0x00);

    for (i = 0; i < 16; i++) {
        line[i] = 0x20;
    }
    LCD12864_ShowHanzi(config->row, line, 8);    /* 百分比行清空 */
}
