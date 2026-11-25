#ifndef __4G_H
#define __4G_H
#include "sys.h"

/* 4G 模块网络状态输入脚 PC5：模块把「是否注册上网络」的电平从这根线送进来，
   高 = 在线、低 = 掉线；同时是 EXTI5 的中断源（双沿触发）。
   宏名 GPRS_LINK_STATUS = GPRS 链路状态；对应模块数据手册的状态引脚 LINKA
   （PC4 的 LINKB 同源，本工程暂只留位不用，引脚名出处保留在注释里） */
#define GPRS_LINK_STATUS  PCin(5)

/* 网络在线标志：由 EXTI 中断服务程序随 PC5 电平实时维护，
   消费者是阶段 10 的 GPRS 状态机与阶段 12 的 main（联网判断用） */
extern u8 g_gprs_online;

/* 4G 模块初始化：状态脚输入 + EXTI 双沿中断 + PA1 复位脉冲（模块点火） */
void Gprs4G_Init(void);

#endif
