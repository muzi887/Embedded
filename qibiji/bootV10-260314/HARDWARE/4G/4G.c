#include "4G.h"
#include "sys.h"
//#include "mqtt.h"
#include "led.h"
#include "usart.h"
#include "delay.h"
#include "gprs.h"

u8 gprsonlineflag;

void GM4G_Init(void) //IO初始化
{
    GPIO_InitTypeDef GPIO_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO,ENABLE);//使能PORTB,PORTE时钟

    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_4;   //LINKB
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_5;   //LINKA
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    //4G--RST 
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_1;	 //4G复位
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP; 		     //推挽输出
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;		       //IO口速度为50MHz
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    //中断配置
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOC, GPIO_PinSource5);
    EXTI_ClearITPendingBit(EXTI_Line5);                               //GPRS 网络连接是否在线
    EXTI_InitStructure.EXTI_Line    = EXTI_Line5;											//选择中断线路7
    EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt; 						//设置为中断请求，非事件请求
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling;		//设置中断触发方式为上、下降沿触发
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;             						//外部中断使能
    EXTI_Init(&EXTI_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel 									 = EXTI9_5_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority 			 = 3;
    NVIC_InitStructure.NVIC_IRQChannelCmd 							 = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    GPIO_SetBits(GPIOA,GPIO_Pin_1);
    delay_ms(500);
    GPIO_ResetBits(GPIOA,GPIO_Pin_1);
}

//外部中断5-9服务程序  // PC.5
void EXTI9_5_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line5))  //gprs网络连接是否在线
    {
        EXTI_ClearITPendingBit(EXTI_Line5);
        if(GPRS_LINKA)
        {
            gprsonlineflag = 1;    //gprs上线
            LED_LINE = 1;          //在线灯亮
		   //MQTT_ConnectPack();  //连接服务器
           //printf("gprsonlineflag = 1\n");
		   //cjson_pub_version(); //开机上报版本号
		   //cjson_pub_getbin(0);
        }
        else
        {
            gprsonlineflag = 0;   //gprs掉线
            LED_LINE = 0;
           // printf("gprsonlineflag = 0\n");
        }
    }	
}

