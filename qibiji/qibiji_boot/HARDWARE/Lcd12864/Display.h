#ifndef __DISPLAY_H
#define __DISPLAY_H

#include "Lcd12864.h"

//#define MoPage      0
#define ViewPage    0
#define SetPage     2
#define SetPara     3   
#define SetVal      4

#define View_Num    4
#define SetP_Num    2
#define SetParaN    2

#define Home_Num    3

#define  BackLight PAout(0)

void Lcd_Process(void); //液晶处理函数
void BLight_Ctrl(void);	//背光控制
void LCDInit(void);
void Lcd_Init(void);
void Lcd_Gpio_Init(void);
void DisplayInit(void);
void LcdDisplay(void);  // 液晶显示

void kaiduVal(u8 *val,u8 *src);   //开度值和状态显示

void fjiaozhun(u8 *src, u8 style);  //校准显示

void DisplayState(u8 *src);   //状态 连接显示

int asciitoInt(u8 *src,u8 len);

void homepage(void); //主页
void setpage(void);  //设置页
void G4gpage(void);  //4g
void Devicepage(void); // 设备页面

uint8_t ViewPages(uint8_t KeyNum); //查询页面
void DisplayViewPages(uint8_t PageNum);


void getval();//获取值并显示

void DisplayInt(u8 *src, u16 val,u8 style);   //显示整数
void DisplayFloat(u8 *src, u16 val,u8 style); //显示小数 val 2位整数1位小数 

// 字符串显示函数
uint8_t Lcd_DisplayString(const char *str, uint8_t row, uint8_t col);  // 通用字符串显示函数
uint8_t Lcd_DisplayString_Default(const char *str);  // 默认位置显示字符串
void loading(void);  // 显示"wait net"（兼容旧版本）


void progressbar_Init(void);
void ota_step1(void);
void ota_step2(void);
void ota_step3(void);
void ota_step4(void);
void ota_step5(void);   /* 第3行显示“更新出错”（内部用；要显示错误类型请用 ota_show_error） */
void ota_step6(void);
void ota_step7(void);   /* 清屏（复位前调用；此前缺少声明，属隐式声明） */

/* OTA 出错类型：液晶第4行显示对应中文（实现见 Dispaly.c 的 ota_show_error） */
#define OTA_ERR_NET         0   /* 网络连接超时：AT 无应答 / 等待上线超时 */
#define OTA_ERR_TIMEOUT     1   /* 请求超时：等平台回包超时重试超限 */
#define OTA_ERR_LINK        2   /* 连接中断：升级过程中掉线超限 */
#define OTA_ERR_DATA        3   /* 数据校验失败：CRC / 长度 / 序号 */
#define OTA_ERR_IMAGE       4   /* 固件包无效：A2 镜像头非法 / A1、A2 均无可用镜像 */
#define OTA_ERR_VERIFY      5   /* 写入校验失败：搬运 memcmp 不一致 / 自愈校验失败 */
#define OTA_ERR_SIZE        6   /* 固件大小错误：OTA_size 非法 */
#define OTA_ERR_EEPROM      7   /* 存储异常：24C02 自检失败（OTA 被关闭） */

/* 统一错误画面：第1行标题 + 第2行清空 + 第3行“更新出错” + 第4行错误类型 */
void ota_show_error(uint8_t kind);

#endif
