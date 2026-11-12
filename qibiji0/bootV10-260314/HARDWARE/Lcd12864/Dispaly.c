#include "display.h"
#include "Lcd12864.h"
// #include "timer.h"
#include "usart.h"
// #include "key.h"
#include "delay.h"
// #include "adc.h"
#include "24cxx.h"
#include "ProgressBar.h"
#include <string.h>

u8 autoFlag = 0;	// 校准自动标志
u8 setParaflag = 0; // 设置页面参数设置状态

// 60s 无操作自动返回主页并关闭背光
// gb2312
// բ�� d5a2 cebb
 //在线升级
uint8_t title[16] = {0x20,0x20,0x20,0x20,0xd4,0xda,0xcf,0xdf,0xc9,0xfd,0xbc,0xb6,0x20,0x20,0x20,0x20};
//连接服务器中
uint8_t step1[16] = {0x20,0x20,0xc1,0xac,0xbd,0xd3,0xb7,0xfe,0xce,0xf1,0xc6,0xf7,0xd6,0xd0,0x20,0x20};
//连接成功
uint8_t step2[16] = {0x20,0x20,0x20,0x20,0xc1,0xac,0xbd,0xd3,0xb3,0xc9,0xb9,0xa6,0x20,0x20,0x20,0x20};
//申请最新固件
uint8_t step3[16] = {0x20,0x20,0xc9,0xea,0xc7,0xeb,0xd7,0xee,0xd0,0xc2,0xb9,0xcc,0xbc,0xfe,0x20,0x20};
//开始更新 
uint8_t step4[16] = {0x20,0x20,0x20,0x20,0xbf,0xaa,0xca,0xbc,0xb8,0xfc,0xd0,0xc2,0x20,0x20,0x20,0x20};
//无新版本 
uint8_t step5[16] = {0x20,0x20,0x20,0x20,0xce,0xde,0xd0,0xc2,0xb0,0xe6,0xb1,0xbe,0x20,0x20,0x20,0x20};
//更新出错 
uint8_t step6[16] = {0x20,0x20,0x20,0x20,0xb8,0xfc,0xd0,0xc2,0xb3,0xf6,0xb4,0xed,0x20,0x20,0x20,0x20};
//更新完毕 
uint8_t step7[16] = {0x20,0x20,0x20,0x20,0xb8,0xfc,0xd0,0xc2,0xcd,0xea,0xb1,0xcf,0x20,0x20,0x20,0x20};

uint8_t empty[16] = {0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20};

u8 code[][24] = {
	{0x42, 0x00, 0x41, 0x00, 0x4F, 0xE0, 0xE8, 0x20, 0x42, 0x80, 0x44, 0x40, 0x68, 0x20, 0xC7, 0xC0, 0x41, 0x00, 0x41, 0x00, 0x41, 0x00, 0xCF, 0xE0}, /*"上",0*/

};

u8 HomeNum = 0;	   // 主页的索引
u8 ViewNum = 0;	   // 查询页面的索引
u8 SetPageNum = 0; // 设置页面的索引
u8 SetParaNum = 0; // 参数设置索引
u8 PageSta = 0;	   // 0:显示主页

// io初始化
void progressbar_Init(void)
{
	uint8_t i, j;
	
	ProgressBarConfig_t config;
	
	//Lcd_Gpio_Init();
	//delay_ms(500);	   // 等待屏幕启动时间
	CLEARGDRAMH(0x00); // 清空显示图形
	
	//WRGDRAM1(0xff,0xc0,0x03);		
		
	
	// 配置进度条
	config.row = 3;              // 第三行（DDRAM地址0x88）字符
	config.y_start = 18;         // GDRAM Y坐标32（第三行，每行16像素）字符
	config.x_start = 1;          // 从左边开始字符
	config.width = 6;          // 全宽,字符
	config.height = 10;          // 16像素高
	config.border_enable = 1;    // 启用边框
	config.style = PROGRESS_STYLE_SOLID;  // 实心样式
	
	//開始
//    loading();
	// 初始化进度条
	ProgressBar_Init(&config);
	ProgressBar_Update(0);
	
	

	//ProgressBar_DrawBorder(&config);
//ProgressBar_ShowPercent(0x80,95);
    
    
}

//标题显示和提示连接网络
void ota_step1(void)
{
	WRCommandH(0x30);
	
	ShowQQCharH(0x80,empty,8);
	ShowQQCharH(0x90,empty,8);
	ShowQQCharH(0x88,empty,8);
	ShowQQCharH(0x98,empty,8);
	
	ShowQQCharH(0x80,title,8);
	ShowQQCharH(0x90,step1,8);
}
//连接成功，问询版本
void ota_step2(void)
{
	//ShowQQCharH(0x80,empty,8);
	ShowQQCharH(0x90,empty,8);
	ShowQQCharH(0x88,empty,8);
	ShowQQCharH(0x98,empty,8);
	
	//ShowQQCharH(0x80,title,8);
	ShowQQCharH(0x90,step2,8);
	ShowQQCharH(0x88,step3,8);
}
//开始更新
void ota_step3(void)
{
	//ShowQQCharH(0x80,empty,8);
	ShowQQCharH(0x90,empty,8);
	ShowQQCharH(0x88,empty,8);
	ShowQQCharH(0x98,empty,8);
	
	//ShowQQCharH(0x80,title,8);
	//ShowQQCharH(0x90,step4,8);
	ShowQQCharH(0x88,step4,8);
}
//无需更新
void ota_step4(void)
{
		//ShowQQCharH(0x80,empty,8);
	ShowQQCharH(0x90,empty,8);
	ShowQQCharH(0x88,empty,8);
	ShowQQCharH(0x98,empty,8);
	
	//ShowQQCharH(0x80,title,8);
	//ShowQQCharH(0x90,step5,8);
	ShowQQCharH(0x88,step5,8);
}
//更新出错
void ota_step5(void)
{
		//ShowQQCharH(0x80,empty,8);
	ShowQQCharH(0x90,empty,8);
	ShowQQCharH(0x88,empty,8);
	//ShowQQCharH(0x98,empty,8);
	
	//ShowQQCharH(0x80,title,8);
	//ShowQQCharH(0x90,step6,8);
	ShowQQCharH(0x88,step6,8);
}
//完成更新
void ota_step6(void)
{
		//ShowQQCharH(0x80,empty,8);
	ShowQQCharH(0x90,empty,8);
	ShowQQCharH(0x88,empty,8);
	//ShowQQCharH(0x98,empty,8);
	
	//ShowQQCharH(0x80,title,8);
	//ShowQQCharH(0x90,step7,8);
	ShowQQCharH(0x88,step7,8);
}
void ota_step7(void)
{
	WRCommandH(0x30);
	
	ShowQQCharH(0x80,empty,8);
	ShowQQCharH(0x90,empty,8);
	ShowQQCharH(0x88,empty,8);
	ShowQQCharH(0x98,empty,8);
}
/**
 * @brief 在LCD12864上显示字符串（通用函数）
 * @param str 要显示的字符串（ASCII字符串）
 * @param row 行号（0-3，对应第1-4行）
 * @param col 列号（0-7，对应每行的8个字符位置）
 * @return 实际显示的字符数
 * 
 * 注意：
 * - 每行最多显示8个字符
 * - 如果字符串长度超过可显示范围，会自动截断
 * - 字符串必须是ASCII字符串
 */
uint8_t Lcd_DisplayString(const char *str, uint8_t row, uint8_t col)
{
	uint8_t i = 0;
	uint8_t len = 0;
	uint8_t addr_base;
	// DDRAM行地址映射：第1行0x80, 第2行0x90, 第3行0x88, 第4行0x98
	uint8_t row_addr[] = {0x80, 0x90, 0x88, 0x98};
	
	// 参数检查
	if (str == NULL) {
		return 0;
	}
	
	// 限制行号和列号范围
	if (row > 3) row = 3;
	if (col > 7) col = 7;
	
	// 计算字符串长度（最多8个字符）
	len = strlen(str);
	if (len > (16 - col)) {
		len = 16 - col;  // 截断到可显示范围
	}
	
	addr_base = row_addr[row];
	
	// 切换到文本显示模式（退出扩展指令寄存器）
	WRCommandH(0x30);
	
	// 先清空整行（8个字符位置）
	for (i = 0; i < 8; i++) {
		WRCommandH(addr_base + i);  // 设置DDRAM地址
		WRDataH(0x20);  // 空格（低字节）
		WRDataH(0x00);  // 空格（高字节）
	}
	
	// 显示字符串
	for (i = 0; i < (len+1)/2; i++) {
		WRCommandH(addr_base + col + i);  // 设置DDRAM地址
		WRDataH(str[i*2]);      // 字符ASCII码低字节
		if (i*2+1 < len) {
			WRDataH(str[i*2+1]);  // 字符ASCII码高字节
		} else {
			WRDataH(0x00);        // 越界时使用默认值0
		}
	}
	
	return len;
}

/**
 * @brief 在LCD12864上显示字符串（兼容旧版本，默认显示在第一行第2列）
 * @param str 要显示的字符串
 * @return 实际显示的字符数
 */
uint8_t Lcd_DisplayString_Default(const char *str)
{
	return Lcd_DisplayString(str, 0, 2);  // 第一行，第3列（0x82）
}

/**
 * @brief 保留原loading函数，调用新的通用函数
 */
void loading(void)
{
	Lcd_DisplayString("wait net", 0, 2);  // 第一行，第3列开始
}


void Lcd_Gpio_Init(void)
{
	/*GPIO初始化*/
	GPIO_InitTypeDef GPIO_InitStructure;

	/*使能时钟*/
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE | RCC_APB2Periph_GPIOA, ENABLE); // 使能GPIOB的时钟

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOE, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	CS = 0;
	SID = 0;
	SCLK = 0;

	BackLight = 1; // 关闭背光
}

// 液晶处理函数
void Lcd_Process()
{
	
}

// 背光控制
void BLight_Ctrl()
{

	BackLight = 1; // 打开背光
}

// 初始化LCD-8位接口
void LCDInit(void)
{
	WRCommandH(0x30); // 基本指令,8位接口
	WRCommandH(0x06); // 光标移动设置光标移动方向
	//    WRCommandH(0x01);	//清空显示DDRAM
	WRCommandH(0x0C); // 显示状态开，光标显示关，光标闪烁关，显示开，光标关，光标闪烁关
	WRCommandH(0x02); // 地址归位
}

// 页面显示
void DisplayViewPages(uint8_t PageNum)
{

	//	printf("ViewNum = %d\n",PageNum);
}
