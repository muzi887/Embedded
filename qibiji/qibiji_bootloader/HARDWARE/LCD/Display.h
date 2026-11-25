#ifndef __DISPLAY_H
#define __DISPLAY_H

#include "Lcd12864.h"

/* ---------------- OTA 状态页 ---------------- */
void Ota_ShowConnecting(void);  /* 四行清空 + 第1行「在线升级」+ 第2行「连接服务器中」 */
void Ota_ShowRequestVer(void);  /* 第2行「连接成功」+ 第3行「申请最新固件」 */
void Ota_ShowUpdating(void);    /* 第3行「开始更新」 */
void Ota_ShowNoUpdate(void);    /* 第3行「无新版本」 */
void Ota_ShowErrorHint(void);   /* 第3行「更新出错」（要显示错误类型请用 Ota_ShowError） */
void Ota_ShowDone(void);        /* 第3行「更新完毕」 */
void Ota_ClearText(void);       /* 四行全清（复位/跳转前调用） */

/* 统一错误画面：第1行标题 + 第2行清空 + 第3行「更新出错」 + 第4行错误类型 */
void Ota_ShowError(u8 kind);

/* ---------------- 进度条入口 / 联网提示 ---------------- */
void ProgressBar_Setup(void);   /* 清图形层 + 装配并画出进度条底框 + 显示 0% */
void Lcd_ShowWaitNet(void);     /* 第1行显示 "wait net" */

/* ---------------- OTA 出错类型 ----------------
 * 液晶第 4 行显示对应中文文案； */
#define OTA_ERR_NET         0   /* 网络连接超时：AT 无应答 / 等待上线超时 */
#define OTA_ERR_TIMEOUT     1   /* 请求超时：等平台回包超时重试超限 */
#define OTA_ERR_LINK        2   /* 连接中断：升级过程中掉线超限 */
#define OTA_ERR_DATA        3   /* 数据校验失败：CRC / 长度 / 序号 */
#define OTA_ERR_IMAGE       4   /* 固件包无效：A2 镜像头非法 / A1、A2 均无可用镜像 */
#define OTA_ERR_VERIFY      5   /* 写入校验失败：搬运 memcmp 不一致 / 自愈校验失败 */
#define OTA_ERR_SIZE        6   /* 固件大小错误：OTA_size 非法 */
#define OTA_ERR_EEPROM      7   /* 存储异常：24C02 自检失败（OTA 被关闭） */

#endif
