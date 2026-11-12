#include "display.h"
#include "Lcd12864.h"
#include "timer.h"
#include "usart.h"
#include "key.h"
#include "delay.h"
#include "adc.h"
#include "24cxx.h"
#include "dc_motor.h"
#include "main.h"
#include <string.h>

u8 autoFlag = 0;	// 自动标志位
u8 setParaflag = 0; // 设置页面参数状态

// 外部变量声明
extern u8 kaival;			// 开度值
extern u8 sys_moter_state;  // 电机状态
extern u8 gprsonlineflag;   // GPRS在线标志
extern u8 FLAG_OPENALL;	  // 全开标志
extern u8 FLAG_CLOSEALL;	// 全关标志

// 60s 无操作返回到首页并关闭背光
// gb2312
// 闸门 d5a2 cebb

uint8_t motor[16] = {0xb5, 0xe7, 0xbb, 0xfa, 0x3a, 0x20,
					 0x31, 0x2e, 0x36, 0x41, 0x20, 0x31, 0x32, 0x2e, 0x36, 0x56};

uint8_t kaidu[16] = {0xbf, 0xaa, 0xb6, 0xc8, 0x3a, 0x20, 0x31, 0x30, 0x30, 0x25, 0x20,
					 0x20, 0xc8, 0xab, 0xbf, 0xaa};

uint8_t liuliang[16] = {0xc1, 0xf7, 0xc1, 0xbf, 0x31, 0x3a,
						0x20, 0x30, 0x2e, 0x30, 0x20, 0x6d, 0x33, 0x2F, 0x73, 0x20};

uint8_t zhaweimax[16] = {0xc8, 0xab, 0xbf, 0xaa, 0x3a, 0x20,
						 0x31, 0x31, 0x32, 0x36, 0x20, 0x6d, 0x6d, 0x20, 0x20, 0x20};

uint8_t duzhuan[16] = {0xb6, 0xc2, 0xd7, 0xaa, 0x3a, 0x20,
					   0x30, 0x30, 0x2e, 0x30, 0x20, 0x41, 0x20, 0x20, 0x20, 0x20};

uint8_t jiaozhun[16] = {0xd0, 0xa3, 0xd7, 0xbc, 0x3a, 0x20,
						0xb9, 0xd8, 0xb1, 0xd5, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};

uint8_t xinhao[16] = {0xd0, 0xc5, 0xba, 0xc5, 0x3a, 0x20,
					  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};

uint8_t ccid[16] = {0x43, 0x43, 0x49, 0x44, 0x3a, 0x20,
					0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};

uint8_t order[16] = {0xb1, 0xe0, 0xba, 0xc5, 0x3a, 0x20,
					 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};

uint8_t dstate[16] = {0xd7, 0xb4, 0xcc, 0xac, 0x3a, 0x20,
					  0xc0, 0xeb, 0xcf, 0xdf, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};

uint8_t PV[16] = {0xb9, 0xe2, 0xb7, 0xfc, 0x3a, 0x20,
				  0x20, 0x30, 0x2e, 0x30, 0x56, 0x20, 0x20, 0x20, 0x20, 0x20};

uint8_t empty[16] = {0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
					 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};

uint8_t banben[16] = {0xb0,0xe6,0xb1,0xbe,0x3a,0x20,
	                 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};
uint8_t shengji[16] = { 0xc9, 0xfd, 0xbc, 0xb6, 0x3a, 0x20,
	                 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};
u8 xianshi[16] ;

uint8_t shang_xia_xianwei[16] = {
	0xC9, 0xCF, 0xCF, 0xDE, 0xCE, 0xBB, // "上限位"
	0x20, 0x20, 0x20, 0x20,				      // 四个空格
	0xCF, 0xC2, 0xCF, 0xDE, 0xCE, 0xBB	// "下限位"
};

u8 code[][24] = {
	{0x42, 0x00, 0x41, 0x00, 0x4F, 0xE0, 0xE8, 0x20, 0x42, 0x80, 0x44, 0x40, 0x68, 0x20, 0xC7, 0xC0, 0x41, 0x00, 0x41, 0x00, 0x41, 0x00, 0xCF, 0xE0}, /*"控",0*/
	{0x10, 0x20, 0x50, 0x20, 0x7D, 0x20, 0x91, 0x20, 0xFF, 0x20, 0x11, 0x20, 0x7D, 0x20, 0x55, 0x20, 0x55, 0x20, 0x54, 0x20, 0x5C, 0x20, 0x10, 0xE0}, /*"制",1*/
		
	{0x00, 0x00, 0xFE, 0xE0, 0x28, 0x20, 0xFE, 0x20, 0xAA, 0x20, 0xAA, 0xE0, 0xC6, 0x80, 0x82, 0x80, 0xFE, 0x80, 0x82, 0xA0, 0xFE, 0xA0, 0x82, 0x60}, /*"配",2*/
	{0x7F, 0xC0, 0x4A, 0x40, 0x7F, 0xC0, 0x04, 0x00, 0xFF, 0xE0, 0x04, 0x00, 0x3F, 0x80, 0x20, 0x80, 0x20, 0x80, 0x24, 0x80, 0x24, 0x80, 0xFF, 0xE0}, /*"置",3*/
		
	{0x08, 0x00, 0x08, 0x00, 0xFF, 0x80, 0x88, 0x80, 0x88, 0x80, 0xFF, 0x80, 0x88, 0x80, 0x88, 0x80, 0xFF, 0x80, 0x88, 0x20, 0x08, 0x20, 0x07, 0xE0}, /*"电",4*/
	{0x02, 0x00, 0x82, 0x00, 0x52, 0xC0, 0x13, 0x40, 0x16, 0x40, 0x9A, 0x40, 0x72, 0x40, 0x12, 0x40, 0x12, 0xC0, 0x52, 0x00, 0x90, 0x20, 0x0F, 0xE0}, /*"池",5*/
	{0x20, 0x00, 0x27, 0x80, 0x24, 0x80, 0xF4, 0x80, 0x24, 0x80, 0x74, 0x80, 0x6C, 0x80, 0xA4, 0x80, 0x24, 0x80, 0x24, 0xA0, 0x28, 0xA0, 0x30, 0x60}, /*"机",6*/
		
	{0x02, 0x00, 0x81, 0x00, 0x5F, 0xE0, 0x04, 0x00, 0x08, 0x40, 0x9F, 0xE0, 0x40, 0x20, 0x0A, 0x80, 0x0A, 0x80, 0x4A, 0xA0, 0x92, 0xA0, 0x22, 0x60}, /*"流",7*/
	{0x7F, 0xC0, 0x44, 0x40, 0x42, 0x40, 0xFF, 0xE0, 0x44, 0x40, 0x7F, 0xC0, 0x44, 0x40, 0x7F, 0xC0, 0x04, 0x00, 0x7F, 0xC0, 0x04, 0x00, 0xFF, 0xE0}, /*"量",8*/
		
	{0x20, 0x00, 0x27, 0xE0, 0x24, 0x20, 0xF4, 0x20, 0x24, 0xC0, 0x24, 0x00, 0x37, 0xE0, 0xE5, 0x20, 0x25, 0x40, 0x24, 0x80, 0x25, 0x40, 0x66, 0x20}, /*"报",9*/
	{0x49, 0x00, 0xFD, 0xE0, 0x4A, 0x40, 0x7D, 0x40, 0xD4, 0x80, 0x75, 0x40, 0x0A, 0x20, 0xFF, 0xE0, 0x00, 0x00, 0x7F, 0xC0, 0x20, 0x80, 0x3F, 0x80}, /*"警",10*/

	{0xFF, 0xE0, 0x80, 0x20, 0x80, 0x20, 0xC4, 0x60, 0xAA, 0xA0, 0x91, 0x20, 0x91, 0x20, 0xAA, 0xA0, 0xC4, 0x60, 0x80, 0x20, 0x80, 0x20, 0x80, 0xE0}, /*"网",11*/
	{0x22, 0x00, 0x23, 0xC0, 0x44, 0x40, 0x9A, 0x80, 0xE1, 0x00, 0x22, 0x80, 0x4C, 0x60, 0xF7, 0xC0, 0x04, 0x40, 0x34, 0x40, 0xC7, 0xC0, 0x04, 0x40}, /*"络",12*/

	{0x47, 0x80, 0x24, 0x80, 0x04, 0x80, 0x04, 0x80, 0xE8, 0x60, 0x20, 0x00, 0x2F, 0xC0, 0x24, 0x40, 0x22, 0x80, 0x31, 0x00, 0x22, 0x80, 0x0C, 0x60}, /*"设",13*/
	{0x10, 0x00, 0x3F, 0x80, 0x51, 0x00, 0x0E, 0x00, 0x31, 0x80, 0xC0, 0x60, 0x3F, 0x80, 0x24, 0x80, 0x3F, 0x80, 0x24, 0x80, 0x3F, 0x80, 0x20, 0x80}, /*"备",14*/

	{0x80, 0x00, 0x4F, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xE0, 0xC4, 0x80, 0x44, 0x80, 0x44, 0x80, 0x48, 0xA0, 0x50, 0x60, 0x40, 0x00, 0xBF, 0xE0}, /*"远",15*/
	{0x21, 0x00, 0x21, 0x00, 0x25, 0x60, 0x25, 0xA0, 0xF7, 0x20, 0x2D, 0x20, 0x25, 0x20, 0x25, 0x60, 0x25, 0x00, 0x34, 0x20, 0xC4, 0x20, 0x03, 0xE0}, /*"地",16*/

	{0xF7, 0xC0, 0x94, 0x40, 0x94, 0x40, 0xA7, 0xC0, 0xA4, 0x40, 0x94, 0x40, 0x97, 0xC0, 0x95, 0x20, 0x95, 0x40, 0xE4, 0x80, 0x86, 0x40, 0x84, 0x20}, /*"限",17*/
	{0x22, 0x00, 0x21, 0x00, 0x40, 0x00, 0x5F, 0xE0, 0xC0, 0x00, 0x48, 0x40, 0x48, 0x40, 0x44, 0x40, 0x44, 0x80, 0x44, 0x80, 0x41, 0x00, 0x5F, 0xE0}, /*"位",18*/

		
{0x00,0x00,0x00,0x00,0x38,0x7C,0x44,0x10,0x44,0x10,0x44,0x10,0x44,0x10,0x44,0x10,0x44,0x10,0x44,0x10,0x38,0x10,0x00,0x00},/*"OT",19*/
{0x00,0x00,0x00,0x00,0x10,0x00,0x10,0x00,0x28,0x00,0x28,0x00,0x28,0x00,0x44,0x00,0x7C,0x00,0x44,0x00,0x44,0x00,0x00,0x00},/*"A ",20*/
{0x10,0x00,0x20,0x80,0x7F,0xC0,0x08,0x00,0xFF,0xE0,0x22,0x80,0x4C,0x40,0xB1,0x20,0x06,0x00,0x78,0x40,0x03,0x80,0x7C,0x00},/*"参",21*/
{0x11,0x00,0x55,0x00,0x11,0x00,0xFD,0xE0,0x32,0x40,0x59,0x40,0x95,0x40,0x21,0x40,0xF9,0x40,0x48,0x80,0x31,0x40,0xCA,0x20},/*"数",22*/
{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},/*" ",23*/

//字母时需要合并两个字母,cursor提示词
//交替合并这两个数组：第一个数组的第1个元素，第二个数组的第1个元素，第一个数组的第2个元素，第二个数组的第2个元素，以此类推。
};


u8 HomeNum = 0;	   // 首页项目数
u8 ViewNum = 0;	   // 查询页面项目数
u8 SetPageNum = 0; // 设置页面项目数0
u8 SetParaNum = 0; // 设置参数项目数
u8 PageSta = 0;	   // 0:显示首页

// 显示缓存 - 用于事件驱动刷新
DisplayCache displayCache;

// 初始化显示缓存
void DisplayCache_Init(void)
{
	displayCache.page_last = 0xFF;
	displayCache.motor_curr_last = 0xFFFF;
	displayCache.motor_vol_last = 0xFFFF;
	displayCache.kaidu_last = 0xFF;
	displayCache.motor_state_last = 0xFF;
	displayCache.gate_max_last = 0xFFFF;
	displayCache.motor_warning_curr_last = 0xFFFF;
	displayCache.autoFlag_last = 0xFF;
	displayCache.PV_last = 0xFFFF;
	displayCache.gprsonlineflag_last = 0xFF;
	displayCache.positionState_last = 0xFF;
	memset(displayCache.signal_last, 0xFF, 2);  // 初始化信号值数组
	memset(displayCache.ccid_last, 0xFF, 10);   // 初始化CCID值数组
}

// io初始化
void Lcd_Init(void)
{
	Lcd_Gpio_Init();
	delay_ms(1500);	   // 等待屏幕启动时间
	CLEARGDRAMH(0x00); // 清除显示区域
	DisplayCache_Init(); // 初始化显示缓存
	homepage();
}

void Lcd_Gpio_Init(void)
{
	/*GPIO初始化*/
	GPIO_InitTypeDef GPIO_InitStructure;

	/*使能时钟*/
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE | RCC_APB2Periph_GPIOA, ENABLE); // 使能GPIOB时钟

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

// 液晶显示处理
void Lcd_Process()
{
	BLight_Ctrl();
	LcdDisplay();
}

// 背光控制
void BLight_Ctrl()
{
	static u16 cnt = 0;

	if (KeyNum)
	{
		BackLight = 1; // 打开背光
		cnt = 0;
	}
	else
	{
		if (flag300ms & 0x01)
		{
			flag300ms &= 0xfe;
			cnt++;
			if (cnt >= 100)
			{
				BackLight = 0; // 关闭背光
				cnt = 0;
			}
		}
	}
}

// 初始化LCD-8位接口
void LCDInit(void)
{
	WRCommandH(0x30); // 功能指令集,8位接口
	WRCommandH(0x06); // 初始设定，光标移动方向
	//    WRCommandH(0x01);	//清除显示DDRAM
	WRCommandH(0x0C); // 显示状态开关，整体显示开，光标显示关，光标闪烁关
	WRCommandH(0x02); // 地址归位
}

//----------------------------------------------------------------------
// 液晶显示 - 事件驱动刷新优化版本
void LcdDisplay()
{
	static u16 refresh_cnt = 0; // 定时刷新计数器
	u8 need_refresh = 0;		 // 是否需要刷新标志

	// if (KeyNum)
	// {
	// 	printf("KeyNum = %d\n", KeyNum);
	// }

	// 1. 页面切换检测 - 页面切换时强制刷新
	if (displayCache.page_last != ViewNum)
	{
		need_refresh = 1;
	}

	// 2. 按键事件检测 - 有按键时强制刷新
	if (KeyNum)
	{
		need_refresh = 1;
	}

	// 3. 数据变化检测 - 根据当前页面检测对应数据变化
	if (ViewNum == 0) // 首页数据变化检测
	{
		u16 motor_curr = armReg.motor_curr / 100;
		u16 motor_vol = g_adc_val[2];

		if (displayCache.motor_curr_last != motor_curr ||
			displayCache.motor_vol_last != motor_vol ||
			displayCache.kaidu_last != armReg.gate_kaidu ||
			displayCache.motor_state_last != sys_moter_state)
		{
			need_refresh = 1;
			displayCache.motor_curr_last = motor_curr;
			displayCache.motor_vol_last = motor_vol;
			displayCache.kaidu_last = armReg.gate_kaidu;
			displayCache.motor_state_last = sys_moter_state;
		}
	}
	else if (ViewNum == 1) // 设置页面数据变化检测
	{
		if (displayCache.gate_max_last != armReg.gate_maxheight ||
			displayCache.motor_warning_curr_last != armReg.motor_warning_curr_upper ||
			displayCache.autoFlag_last != autoFlag)
		{
			need_refresh = 1;
			displayCache.gate_max_last = armReg.gate_maxheight;
			displayCache.motor_warning_curr_last = armReg.motor_warning_curr_upper;
			displayCache.autoFlag_last = autoFlag;
		}
	}
	else if (ViewNum == 2) // 4G页面数据变化检测
	{
		if (displayCache.gprsonlineflag_last != gprsonlineflag ||
			memcmp(displayCache.signal_last, &xinhao[6], 2) != 0 ||
			memcmp(displayCache.ccid_last, &ccid[6], 10) != 0)
		{
			need_refresh = 1;
			displayCache.gprsonlineflag_last = gprsonlineflag;
			memcpy(displayCache.signal_last, &xinhao[6], 2);
			memcpy(displayCache.ccid_last, &ccid[6], 10);
		}
	}
	else if (ViewNum == 3) // 设备页面数据变化检测
	{
		u16 PV_val = g_adc_val[1];

		if (displayCache.PV_last != PV_val)
		{
			need_refresh = 1;
			displayCache.PV_last = PV_val;
		}
	}
	else if (ViewNum == 4) // 操作页面数据变化检测
	{
		u8 positionState = 0;
		if (FLAG_OPENALL)
			positionState = 1;
		else if (FLAG_CLOSEALL)
			positionState = 2;

		if (displayCache.positionState_last != positionState)
		{
			need_refresh = 1;
			displayCache.positionState_last = positionState;
		}
	}

	// 4. 定时刷新（降低频率，仅作为兜底机制，0.5秒刷新一次）
	if (flag500ms)
	{
		flag500ms = 0;
		refresh_cnt++;
		// if (refresh_cnt >= 10) // 10 * 500ms = 5秒
		// {
			need_refresh = 1;
			refresh_cnt = 0;
	//	}
	}

	// 5. 只在需要时刷新
	if (need_refresh)
	{
		ViewPages(KeyNum);
		// 页面刷新完成后，更新page_last
		displayCache.page_last = ViewNum;
	}
}

/*------------------查询页面---------------------*/
uint8_t ViewPages(uint8_t KeyNum)
{
	switch (KeyNum)
	{
	case KEY_LEFT:
		if (ViewNum > 0)
		{
			ViewNum--;
		}
		else{
			ViewNum=View_Num;
		}
		HomeNum = 0;
		break;

	case KEY_RIGHT:
		if (ViewNum < View_Num)
		{
			ViewNum++;
		}
		else
			ViewNum = 0;
		HomeNum = 0;
		break;

	case KEY_ENTER: // 返回首页
		if (ViewNum != 1)
		{
			ViewNum = 0;
			HomeNum = 0;
		}
		break;
	default:
		break;
	}

	DisplayViewPages(ViewNum);

	if (ViewNum == 0)
	{
		PageSta = 0;
	}
	else
		PageSta = ViewNum;
	return ViewPage;
}

// 页面显示
void DisplayViewPages(uint8_t PageNum)
{
	switch (PageNum)
	{
	case 0:
		homepage();
		WRGDRAM128X8(0, 0x10, 0x00); // 第二行反选
		WRGDRAM128X8(8, 0, 0x00);	 // 第三行反选
		WRGDRAM128X8(8, 0x10, 0x00); // 第四行反选
		// SetPageNum = 0;
		// setParaflag = 0;
		break;
	case 1:
	// SetPageNum = 0;
	// setParaflag = 0;
		setpage();
		break;
	case 2:
		G4gpage();
		WRGDRAM128X8(0, 0x10, 0x00); // 第二行反选
		WRGDRAM128X8(8, 0, 0x00);	 // 第三行反选
		WRGDRAM128X8(8, 0x10, 0x00); // 第四行反选
		// SetPageNum = 0;
		// setParaflag = 0;
		break;
	case 3:
		Devicepage();
		WRGDRAM128X8(0, 0x10, 0x00); // 第二行反选
		WRGDRAM128X8(8, 0, 0x00);	 // 第三行反选
		WRGDRAM128X8(8, 0x10, 0x00); // 第四行反选
		break;
	case 4:
		OperatePage();
		break;
	default:
		break;
	}

	//	printf("ViewNum = %d\n",PageNum);
}

// 首页显示 - 优化版本（事件驱动刷新）
void homepage()
{
	static u8 page_init_flag = 0; // 页面初始化标志


	// 只在首次显示或页面切换时初始化LCD和绘制静态内容
	if (page_init_flag == 0 || displayCache.page_last != 0)
	{
		LCDInit();	   // 初始化
		delay_us(500); // 等待时间
		// 绘制静态内容（只绘制一次）

		//标头
		WRGDRAM(2, &code[23][0]);
		WRGDRAM(3, &code[0][0]);
		WRGDRAM(4, &code[1][0]);
		WRGDRAM(5, &code[23][0]);

		
		WRGDRAM_line();	  // 画边框
		
		GenerateProgressBar1(1,5,xianshi);
		page_init_flag = 1;
	}
		// 更新动态数据（每次刷新都更新）
	DisplayFloat(&motor[5], armReg.motor_curr / 100, 0);
	DisplayFloat(&motor[11], g_adc_val[2], 0);
	kaiduVal(&kaidu[6], &kaidu[12]);
	ShowQQCharH(0x98, liuliang, 8); // 显示'流量'
	ShowQQCharH(0x90, kaidu, 8); // 显示'开度：'
	ShowQQCharH(0x88, motor, 8);  // 显示'电机：'
}

// 设置页面
void setpage()
{
	static u8 Num = 0x30;
	static u8 cnt = 0, flag = 0, row3flag = 0;
	static u8 lastSetPageNum = 0xFF; // 记录上一次的SetPageNum值，初始化为0xFF确保首次执行
	static u8 page_init_flag = 0;	// 页面初始化标志
	static u8 browse_init_flag = 0;  // 浏览模式初始化标志

	// 只在首次显示或页面切换时初始化LCD
	if (page_init_flag == 0 || displayCache.page_last != 1)
	{
		LCDInit();	   // 初始化
		delay_us(500); // 等待时间
		page_init_flag = 1;
		SetPageNum = 0xff;
		setParaflag = 0;
	// }
	// 	// 只在首次显示或页面切换时绘制静态内容
	// 	if (browse_init_flag == 0 || displayCache.page_last != 1)
	// 	{
			WRGDRAM_line(); // 画边框
		GenerateProgressBar1(2,5,xianshi);
			WRGDRAM(2, &code[23][0]);
			WRGDRAM(3, &code[2][0]);
			WRGDRAM(4, &code[3][0]);
			WRGDRAM(5, &code[23][0]);
			browse_init_flag = 1;
		}
	if (setParaflag) // 设置参数
	{
		// 按键处理逻辑
		switch (KeyNum)
		{
		case KEY_UP:
			if (Num < 0x39)
			{
				Num++;
			}
			row3flag = 1;
			break;

		case KEY_DOWN:
			if (Num > 0x30)
			{
				Num--;
			}
			row3flag = 0;
			break;

		case KEY_ENTER:
			cnt++;
			flag = 1;
			switch (SetPageNum)
			{
			case 0:
				zhaweimax[cnt - 1] = Num;
				if (cnt >= 10)
				{
					setParaflag = 0;
					armReg.gate_maxheight = asciitoInt(&zhaweimax[6], 4);
					AT24CXX_WriteWord(0x0006, armReg.gate_maxheight);
				}
				else
				{
					Num = zhaweimax[cnt];
				}
				break;

			case 1:
				duzhuan[cnt - 1] = Num;
				if (cnt == 8)
					cnt++;

				if (cnt >= 10)
				{
					setParaflag = 0;
					armReg.motor_warning_curr_upper = asciitoInt(&duzhuan[6], 4);
					AT24CXX_WriteWord(0x0004, armReg.motor_warning_curr_upper);
				}
				else
				{
					Num = duzhuan[cnt];
				}
				break;

			case 2:
				setParaflag = 0;
				if (row3flag)
				{
					autoFlag = 1;
				}
				else
				{
					autoFlag = 0;
				}

				fjiaozhun(&jiaozhun[6], row3flag);
				break;
			}
			break;
		default:
			break;
		}

		// 显示参数，编辑位显示闪烁效果
		switch (SetPageNum)
		{
		case 0:
			if (cnt < 10)
			{
				if (flag)
				{
					flag = 0;
					zhaweimax[cnt] = 0x20;
				}
				else
				{
					flag = 1;
					zhaweimax[cnt] = Num;
				}
			}

			ShowQQCharH(0x90, zhaweimax, 8); // 显示'闸位：'
			break;

		case 1:
			if (cnt < 10)
			{
				if (flag)
				{
					flag = 0;
					duzhuan[cnt] = 0x20;
				}
				else
				{
					flag = 1;
					duzhuan[cnt] = Num;
				}
			}

			ShowQQCharH(0x88, duzhuan, 8); // 显示'堵转：'
			break;

		case 2:
			if (cnt < 10)
			{
				if (flag)
				{
					flag = 0;
					jiaozhun[6] = 0x20;
					jiaozhun[7] = 0x20;
					jiaozhun[8] = 0x20;
					jiaozhun[9] = 0x20;

					jiaozhun[10] = 0x20;
					jiaozhun[11] = 0x20;
					jiaozhun[12] = 0x20;
					jiaozhun[13] = 0x20;
				}
				else
				{
					flag = 1;
					fjiaozhun(&jiaozhun[6], row3flag);
				}
			}
			ShowQQCharH(0x98, jiaozhun, 8); // 显示'校准：'
			break;
		}
	}
	else // 按键处理逻辑
	{
		switch (KeyNum)
		{
		case KEY_UP:
			if (SetPageNum > 0)
			{
				SetPageNum--;
			}
			break;

		case KEY_DOWN:
			 if (SetPageNum < SetP_Num)
			{
				SetPageNum++;
			}
			break;

		case KEY_ENTER:
			if(SetPageNum==0xff)
			{
				SetPageNum=0;
			}else
				setParaflag = 1;
			break;
		default:
			break;
		}


				// 更新动态数据
		DisplayInt(&zhaweimax[6], armReg.gate_maxheight, 1);				   // 显示闸位最大值
		DisplayFloat(&duzhuan[6], armReg.motor_warning_curr_upper, 1); // 显示堵转电流
		fjiaozhun(&jiaozhun[6], autoFlag);							   // 显示校准状态
		ShowQQCharH(0x90, zhaweimax, 8);							   // 显示'闸位：'
		ShowQQCharH(0x88, duzhuan, 8);								   // 显示'堵转：'
		ShowQQCharH(0x98, jiaozhun, 8);		   						   // 显示'校准：'



						

		// 按键行显示 - 仅当SetPageNum改变时执行
		if (lastSetPageNum != SetPageNum || page_init_flag == 0 || displayCache.page_last != 1)
		{
			lastSetPageNum = SetPageNum; // 更新记录值
			switch (SetPageNum)
			{
			case 0:
				cnt = 6;
				Num = zhaweimax[6];
				WRGDRAM128X8(0, 0x10, 0xff); // 第二行反选
				WRGDRAM128X8(8, 0, 0x00);	 // 第三行反选
				WRGDRAM128X8(8, 0x10, 0x00); // 第四行反选
				break;

			case 1:
				cnt = 6;
				Num = duzhuan[6];
				WRGDRAM128X8(0, 0x10, 0x00); // 第二行反选
				WRGDRAM128X8(8, 0, 0xff);	 // 第三行反选
				WRGDRAM128X8(8, 0x10, 0x00); // 第四行反选
				break;

			case 2:
				cnt = 9;
				if (autoFlag == 0)
					row3flag = 0;
				else
					row3flag = 1;
				WRGDRAM128X8(0, 0x10, 0x00); // 第二行反选
				WRGDRAM128X8(8, 0, 0x00);	 // 第三行反选
				WRGDRAM128X8(8, 0x10, 0xff); // 第四行反选
				break;
			default:
				break;
			}
		}
	}
}

// 4G页面 - 优化版本（事件驱动刷新）
void G4gpage()
{
	static u8 page_init_flag = 0; // 页面初始化标志

	// 只在首次显示或页面切换时初始化LCD和绘制静态内容
	if (page_init_flag == 0 || displayCache.page_last != 2)
	{
		LCDInit();	   // 初始化
		delay_us(500); // 等待时间

		
		//ShowQQCharH(0x98, empty, 8);  // 显示'空行'

		WRGDRAM_line(); // 画边框
		GenerateProgressBar1(3,5,xianshi);
		WRGDRAM(2, &code[23][0]);
		WRGDRAM(3, &code[11][0]);
		WRGDRAM(4, &code[12][0]);
		WRGDRAM(5, &code[23][0]);

		page_init_flag = 1;
	}
	// 4G页面数据变化较少，主要依赖页面切换刷新
	DisplayState(&dstate[6]);
	ShowQQCharH(0x90, xinhao, 8); // 显示'信号：'
	ShowQQCharH(0x88, ccid, 8);	  // 显示'ccid：'
	ShowQQCharH(0x98, dstate, 8); // 显示'状态：'
}

// OTA 参数- 优化版本（事件驱动刷新）
void Devicepage()
{
	static u8 page_init_flag = 0; // 页面初始化标志

	// 只在首次显示或页面切换时初始化LCD和绘制静态内容
	if (page_init_flag == 0 || displayCache.page_last != 3)
	{
		LCDInit();	   // 初始化
		delay_us(500); // 等待时间

		DisplayOTAenable(&shengji[6]);
		ShowQQCharH(0x90, order, 8); // 显示'编号：'
		ShowQQCharH(0x88, banben, 8); // 显示'版本：'
		ShowQQCharH(0x98, shengji, 8); // 显示'升级：'

		WRGDRAM_line();  // 画边框
		GenerateProgressBar1(4,5,xianshi);
		//OTA
		WRGDRAM(2, &code[19][0]);
		WRGDRAM(3, &code[20][0]);
		WRGDRAM(4, &code[21][0]);
		WRGDRAM(5, &code[22][0]);

		page_init_flag = 1;
	}

	// 更新动态数据
	
	// DisplayFloat(&PV[6], g_adc_val[1], 0);
	// ShowQQCharH(0x88, empty, 8);			 // 显示'空'
	// ShowQQCharH(0x98, PV, 8);	  // 显示'光伏：'
}

// 操作页面 - 优化版本（事件驱动刷新）
void OperatePage()
{
	static u8 page_init_flag = 0; // 页面初始化标志
	int positionState;
	positionState = 0;

	if (FLAG_OPENALL)
	{
		positionState = 1;
	}
	else if (FLAG_CLOSEALL)
	{
		positionState = 2;
	}
	else
	{
		positionState = 0;
	}

	// 只在首次显示或页面切换时初始化LCD和绘制静态内容
	if (page_init_flag == 0 || displayCache.page_last != 4)
	{
		LCDInit();	   // 初始化
		delay_us(500); // 等待时间

		ShowQQCharH(0x90, empty, 8);			 // 显示'空'
		ShowQQCharH(0x88, shang_xia_xianwei, 8); // 显示'上限位  下限位'
		ShowQQCharH(0x98, empty, 8);			 // 显示'空'

		WRGDRAM_line(); // 画边框
		GenerateProgressBar1(5,5,xianshi);
		WRGDRAM(2, &code[23][0]);
		WRGDRAM(3, &code[17][0]);
		WRGDRAM(4, &code[18][0]);
		WRGDRAM(5, &code[23][0]);

		page_init_flag = 1;
	}

	// 按键行显示
	switch (positionState)
	{
	case 0:
		WRGDRAM128X3(8, 0, 0x00);  // 上限位不反选
		WRGDRAM128X3(13, 0, 0x00); // 下限位不反选
		break;

	case 1:
		WRGDRAM128X3(8, 0, 0xff);  // 上限位反选
		WRGDRAM128X3(13, 0, 0x00); // 下限位不反选
		break;

	case 2:
		WRGDRAM128X3(8, 0, 0x00);  // 上限位不反选
		WRGDRAM128X3(13, 0, 0xff); // 下限位反选
		break;
	default:
		break;
	}
}
// 首页
// void homepage()
//{
////	switch(KeyNum)
////	{
////		case KEY_UP:
////			if(HomeNum > 0)
////			{
////				HomeNum--;
////			}
////			break;
////
////		case KEY_DOWN:
////			if(HomeNum < Home_Num)
////			{
////				HomeNum++;
////			}
////			break;
////
////		default:
////			break;
////	}
//	LCDInit();				//初始化
//	delay_us(500);		//等待时间
//
////	switch(HomeNum)
////	{
////		case 0:
////			ShowQQCharH(0x90,kaidu,8);//显示'开度：'
////			ShowQQCharH(0x88,warn,8); //显示'警告：'
////			break;
////		case 1:
////			ShowQQCharH(0x90,warn,8);  //显示'警告：'
////			ShowQQCharH(0x88,zhaweimax,8); //显示'闸位：'
////			break;
////		case 2:
////			ShowQQCharH(0x90,zhaweimax,8);  //显示'闸位：'
////			ShowQQCharH(0x88,dianliu,8); //显示'电流：'
////			break;
////		case 3:
////			break;
////		default:
////			break;
////	}
//
//	ShowQQCharH(0x80,time,8);//显示'时间'
//	ShowQQCharH(0x90,kaidu,8);//显示'开度：'
//	ShowQQCharH(0x88,dianliu1,8); //显示'电流：'
//
//	WRGDRAM1(0xff,0xc0,0x03);			//画边框
//
//	WRGDRAM(11,&code[0][0]);
//	WRGDRAM(12,&code[1][0]);
//}
