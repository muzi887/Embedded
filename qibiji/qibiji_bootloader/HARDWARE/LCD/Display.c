/* ===========================================================================
 * LCD OTA 提示层 - qibiji_bootloader
 * OTA 状态页/错误页文案（GB2312 码表，屏自带汉字库不需字模）+ 状态页/
 * 错误页绘制 + 联网等待提示。
 * =========================================================================== */
#include "Display.h"
#include "ProgressBar.h"

/* ---------------- 文案码表（GB2312，每行 8 格 = 16 字节；只读，进 RO 段） ---------------- */

/* 在线升级 */
static const u8 s_txt_title[16] = {0x20,0x20,0x20,0x20,0xd4,0xda,0xcf,0xdf,0xc9,0xfd,0xbc,0xb6,0x20,0x20,0x20,0x20};
/* 连接服务器中（原 step1） */
static const u8 s_txt_connecting[16] = {0x20,0x20,0xc1,0xac,0xbd,0xd3,0xb7,0xfe,0xce,0xf1,0xc6,0xf7,0xd6,0xd0,0x20,0x20};
/* 连接成功（原 step2） */
static const u8 s_txt_connected[16] = {0x20,0x20,0x20,0x20,0xc1,0xac,0xbd,0xd3,0xb3,0xc9,0xb9,0xa6,0x20,0x20,0x20,0x20};
/* 申请最新固件（原 step3） */
static const u8 s_txt_request_ver[16] = {0x20,0x20,0xc9,0xea,0xc7,0xeb,0xd7,0xee,0xd0,0xc2,0xb9,0xcc,0xbc,0xfe,0x20,0x20};
/* 开始更新（原 step4） */
static const u8 s_txt_updating[16] = {0x20,0x20,0x20,0x20,0xbf,0xaa,0xca,0xbc,0xb8,0xfc,0xd0,0xc2,0x20,0x20,0x20,0x20};
/* 无新版本（原 step5） */
static const u8 s_txt_no_update[16] = {0x20,0x20,0x20,0x20,0xce,0xde,0xd0,0xc2,0xb0,0xe6,0xb1,0xbe,0x20,0x20,0x20,0x20};
/* 更新出错（原 step6） */
static const u8 s_txt_error[16] = {0x20,0x20,0x20,0x20,0xb8,0xfc,0xd0,0xc2,0xb3,0xf6,0xb4,0xed,0x20,0x20,0x20,0x20};
/* 更新完毕（原 step7） */
static const u8 s_txt_done[16] = {0x20,0x20,0x20,0x20,0xb8,0xfc,0xd0,0xc2,0xcd,0xea,0xb1,0xcf,0x20,0x20,0x20,0x20};
/* 空行：8 格全空格（原 empty） */
static const u8 s_txt_blank[16] = {0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20};

/* OTA 出错类型文案（液晶自带汉字库，直接发 GB2312 码不需点阵字模） */
static const u8 s_txt_err_net[16]     = {0xCD,0xF8,0xC2,0xE7,0xC1,0xAC,0xBD,0xD3,0xB3,0xAC,0xCA,0xB1,0x20,0x20,0x20,0x20}; /* 网络连接超时 */
static const u8 s_txt_err_timeout[16] = {0xC7,0xEB,0xC7,0xF3,0xB3,0xAC,0xCA,0xB1,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20}; /* 请求超时 */
static const u8 s_txt_err_link[16]    = {0xC1,0xAC,0xBD,0xD3,0xD6,0xD0,0xB6,0xCF,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20}; /* 连接中断 */
static const u8 s_txt_err_data[16]    = {0xCA,0xFD,0xBE,0xDD,0xD0,0xA3,0xD1,0xE9,0xCA,0xA7,0xB0,0xDC,0x20,0x20,0x20,0x20}; /* 数据校验失败 */
static const u8 s_txt_err_image[16]   = {0xB9,0xCC,0xBC,0xFE,0xB0,0xFC,0xCE,0xDE,0xD0,0xA7,0x20,0x20,0x20,0x20,0x20,0x20}; /* 固件包无效 */
static const u8 s_txt_err_verify[16]  = {0xD0,0xB4,0xC8,0xEB,0xD0,0xA3,0xD1,0xE9,0xCA,0xA7,0xB0,0xDC,0x20,0x20,0x20,0x20}; /* 写入校验失败 */
static const u8 s_txt_err_size[16]    = {0xB9,0xCC,0xBC,0xFE,0xB4,0xF3,0xD0,0xA1,0xB4,0xED,0xCE,0xF3,0x20,0x20,0x20,0x20}; /* 固件大小错误 */
static const u8 s_txt_err_eeprom[16]  = {0xB4,0xE6,0xB4,0xA2,0xD2,0xEC,0xB3,0xA3,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20}; /* 存储异常 */

/* ---------------- 进度条场景装配（GPRS 开始下载时调用） ---------------- */
void ProgressBar_Setup(void)
{
    ProgressBarConfig_t config;

    LCD12864_ClearGDRAM();   /* 清空图形层 */

    /* 进度条装配：底行（与百分比文字同在文本第 4 行区域）、带边框、实心。
     * 几何取值 = 原工程实机渲染出来的画面，像素直述 */
    config.row           = 3;       /* 百分比文字：文本第 4 行 */
    config.y_start       = 50;      /* 条起始像素行（底行区域） */
    config.x_word        = 1;       /* 起始字列（像素列 16 起） */
    config.width         = 6;       /* 宽 6 字 = 96 像素 */
    config.height        = 11;      /* 总高 11 像素行（含上下边框） */
    config.border_enable = 1;
    config.style         = PROGRESS_STYLE_SOLID;

    ProgressBar_Init(&config);
    ProgressBar_Update(0);
}

/* ---------------- OTA 状态页 ---------------- */

/* 四行清空 + 第1行「在线升级」+ 第2行「连接服务器中」 */
void Ota_ShowConnecting(void)
{
    LCD12864_ShowHanzi(0, s_txt_blank, 8);
    LCD12864_ShowHanzi(1, s_txt_blank, 8);
    LCD12864_ShowHanzi(2, s_txt_blank, 8);
    LCD12864_ShowHanzi(3, s_txt_blank, 8);

    LCD12864_ShowHanzi(0, s_txt_title, 8);        /* 第1行：在线升级 */
    LCD12864_ShowHanzi(1, s_txt_connecting, 8);   /* 第2行：连接服务器中 */
}

/* 第2行「连接成功」+ 第3行「申请最新固件」 */
void Ota_ShowRequestVer(void)
{
    LCD12864_ShowHanzi(1, s_txt_blank, 8);
    LCD12864_ShowHanzi(2, s_txt_blank, 8);
    LCD12864_ShowHanzi(3, s_txt_blank, 8);

    LCD12864_ShowHanzi(1, s_txt_connected, 8);    /* 第2行：连接成功 */
    LCD12864_ShowHanzi(2, s_txt_request_ver, 8);  /* 第3行：申请最新固件 */
}

/* 第3行「开始更新」 */
void Ota_ShowUpdating(void)
{
    LCD12864_ShowHanzi(1, s_txt_blank, 8);
    LCD12864_ShowHanzi(2, s_txt_blank, 8);
    LCD12864_ShowHanzi(3, s_txt_blank, 8);

    LCD12864_ShowHanzi(2, s_txt_updating, 8);     /* 第3行：开始更新 */
}

/* 第3行「无新版本」 */
void Ota_ShowNoUpdate(void)
{
    LCD12864_ShowHanzi(1, s_txt_blank, 8);
    LCD12864_ShowHanzi(2, s_txt_blank, 8);
    LCD12864_ShowHanzi(3, s_txt_blank, 8);

    LCD12864_ShowHanzi(2, s_txt_no_update, 8);    /* 第3行：无新版本 */
}

/* 第3行「更新出错」（错误类型用 Ota_ShowError） */
void Ota_ShowErrorHint(void)
{
    LCD12864_ShowHanzi(1, s_txt_blank, 8);
    LCD12864_ShowHanzi(2, s_txt_blank, 8);

    LCD12864_ShowHanzi(2, s_txt_error, 8);        /* 第3行：更新出错 */
}

/* 第3行「更新完毕」 */
void Ota_ShowDone(void)
{
    LCD12864_ShowHanzi(1, s_txt_blank, 8);
    LCD12864_ShowHanzi(2, s_txt_blank, 8);

    LCD12864_ShowHanzi(2, s_txt_done, 8);         /* 第3行：更新完毕 */
}

/* 四行全清（复位/跳转前调用） */
void Ota_ClearText(void)
{
    LCD12864_ShowHanzi(0, s_txt_blank, 8);
    LCD12864_ShowHanzi(1, s_txt_blank, 8);
    LCD12864_ShowHanzi(2, s_txt_blank, 8);
    LCD12864_ShowHanzi(3, s_txt_blank, 8);
}

/*
 * OTA 错误提示：第1行「在线升级」、第2行清空、第3行「更新出错」（复用更新出错文案）、
 * 第4行错误类型。图形模式残留（清屏/进度条）由 ShowHanzi 内部的 0x30 自动切回，
 * 不需手工发指令。
 */
void Ota_ShowError(u8 kind)
{
    const u8 *line;

    switch (kind)
    {
        case OTA_ERR_NET:     line = s_txt_err_net;     break;
        case OTA_ERR_TIMEOUT: line = s_txt_err_timeout; break;
        case OTA_ERR_LINK:    line = s_txt_err_link;    break;
        case OTA_ERR_DATA:    line = s_txt_err_data;    break;
        case OTA_ERR_IMAGE:   line = s_txt_err_image;   break;
        case OTA_ERR_VERIFY:  line = s_txt_err_verify;  break;
        case OTA_ERR_SIZE:    line = s_txt_err_size;    break;
        case OTA_ERR_EEPROM:  line = s_txt_err_eeprom;  break;
        default:              line = s_txt_blank;       break;
    }

    LCD12864_ShowHanzi(0, s_txt_title, 8);          /* 第1行：在线升级 */
    LCD12864_ShowHanzi(1, s_txt_blank, 8);          /* 第2行：清空 */
    LCD12864_ShowHanzi(2, s_txt_error, 8);          /* 第3行：更新出错 */
    LCD12864_ShowHanzi(3, line, 8);                 /* 第4行：错误类型 */
}

/* ---------------- 联网等待提示 ---------------- */

/* 第 1 行显示 "wait net"。
 * 注意本函数不预清整行，由调用方先清（OTA 页先铺 blank 行）。 */
void Lcd_ShowWaitNet(void)
{
    LCD12864_ShowString(0, "    wait net");
}
