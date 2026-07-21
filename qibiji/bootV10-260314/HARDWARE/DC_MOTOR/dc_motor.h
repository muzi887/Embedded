#ifndef __DCMOTOR_H
#define __DCMOTOR_H
#include "sys.h"

extern u8 FLAG_OPENALL, FLAG_CLOSEALL;

/*************************************    继电器控制    *****************************************************/
#define RLY_1H_ON   GPIO_SetBits(GPIOD,GPIO_Pin_1)
#define RLY_1H_OFF  GPIO_ResetBits(GPIOD,GPIO_Pin_1)
#define RLY_2H_ON   GPIO_SetBits(GPIOD,GPIO_Pin_3)
#define RLY_2H_OFF  GPIO_ResetBits(GPIOD,GPIO_Pin_3)

#define V01_CTRL    PDout(4)
#define V02_CTRL    PDout(5)

//---------------------------电机控制函数---------------------
void dcmotor_init(void);           /* 直流无刷电机初始化 */
void dcmotor_up(void);             /* 电机正转 */
void dcmotor_down(void);
void dcmotor_stop(void);           /* 关闭电机 */
//电机控制处理
void Dcmotor_Process(void);
void dcmotor_keycheck(void);
void dcmotor_ctrl(void);
void dcmotor_auto(void);

#endif
