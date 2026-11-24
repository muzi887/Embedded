#ifndef __MYIIC_H
#define __MYIIC_H

/* 总线协议层：AT24CXX 的所有读写都经模拟 I²C 总线进行 */
#include "sys.h"    /* PBout/PBin 位带宏：把引脚当普通变量读写 */

/* SDA 方向切换：I²C 的 SDA 是双向线，本模拟实现不做开漏，靠改 CRH 在输入/输出间切换。
   改的是 CRH 中管理 PB11 的半字节（bit12~15），其余引脚配置位用掩码保住 */
#define SDA_IN()  {GPIOB->CRH &= 0xFFFF0FFF; GPIOB->CRH |= 8 << 12;}   /* 8 = 上拉/下拉输入模式：读从机应答和数据时用 */
#define SDA_OUT() {GPIOB->CRH &= 0xFFFF0FFF; GPIOB->CRH |= 3 << 12;}   /* 3 = 通用推挽输出 50MHz：主动驱动 SDA 时用 */

/* 引脚到位带的映射（I²C 引脚固定分配在 PB10/PB11，换脚须连这里一起改） */
#define IIC_SCL    PBout(10)    /* SCL 时钟线：主机输出，恒为推挽 */
#define IIC_SDA    PBout(11)    /* SDA 数据线输出视角：主机主动写 0/1 */
#define READ_SDA   PBin(11)     /* SDA 数据线输入视角：读从机放上来的电平 */

void IIC_Init(void);                    /* 两引脚配置为推挽输出并拉高：I²C 空闲态两线均为高 */
void IIC_Start(void);                   /* 起始信号：SCL 高期间 SDA 由高变低 */
void IIC_Stop(void);                    /* 停止信号：SCL 高期间 SDA 由低变高 */
void IIC_Send_Byte(u8 txd);             /* 按位从高到低发一个字节；只发送，不收应答 */
u8   IIC_Read_Byte(unsigned char ack);  /* 按位收一个字节；ack=1 收完回 ACK，ack=0 回 NACK（最后一字节用） */
u8   IIC_Wait_Ack(void);                /* 等从机应答：返回 0 = 应答到，1 = 超时无应答 */
void IIC_Ack(void);                     /* 主机发 ACK：告诉从机"继续发" */
void IIC_NAck(void);                    /* 主机发 NACK：告诉从机"读完了" */

#endif
