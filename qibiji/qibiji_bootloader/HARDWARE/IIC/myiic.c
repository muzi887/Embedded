#include "myiic.h"
#include "delay.h"      /* delay_us：I²C 时序节拍，全部按微秒级手排延时 */

/* 两引脚配为推挽输出并拉高：上电后 I²C 总线进入空闲态（两线均为高电平） */
void IIC_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;                                      
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);                       

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;                    /* PB10 = SCL，PB11 = SDA */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;                            /* 推挽输出：模拟 I²C 由主机强驱动两线 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);                                  
    GPIO_SetBits(GPIOB, GPIO_Pin_10 | GPIO_Pin_11);                             /* 两线拉高 = 总线空闲 */
}

/* 起始信号：SCL 为高时把 SDA 从 1 拉到 0，从机看到即进入接收准备 */
void IIC_Start(void)
{
    SDA_OUT();              /* SDA 切输出：起始信号由主机驱动 */
    IIC_SDA = 1;
    IIC_SCL = 1;            /* 空闲态两线高，保证 SDA 下降沿发生在 SCL 高电平期间 */
    delay_us(4);            /* 建立时间：电平稳定后再动作 */
    IIC_SDA = 0;
    delay_us(4);
    IIC_SCL = 0;            /* 拉低时钟：钳住总线，随后可以开始送位 */
}

/* 停止信号：SCL 为高时把 SDA 从 0 抬到 1，从机看到即释放总线 */
void IIC_Stop(void)
{
    SDA_OUT();
    IIC_SCL = 0;
    IIC_SDA = 0;            /* 先保证 SDA 处于低，才有"低到高"的停止沿 */
    delay_us(4);
    IIC_SCL = 1;
    IIC_SDA = 1;            /* SCL 高期间的上升沿 = 停止 */
    delay_us(4);
}

/* 等待从机应答：第 9 个时钟把 SDA 交给从机，从机拉低表示 ACK。
   返回 0 = 收到应答；返回 1 = 约 250us 内 SDA 一直为高（无应答），顺带发停止释放总线 */
u8 IIC_Wait_Ack(void)
{
    u8 ucErrTime = 0;       /* 轮询计数：每圈约 2us，250 圈即超时上限 */

    SDA_IN();               /* SDA 切输入：电平由从机决定 */
    IIC_SDA = 1;            /* 先释放总线（输出锁存置 1，切回输出时不会突然拉低） */
    delay_us(1);
    IIC_SCL = 1;            /* 第 9 个时钟高电平：此刻读 SDA */
    delay_us(1);
    while (READ_SDA)        /* SDA 仍为高 = 还没应答 */
    {
        ucErrTime++;
        if (ucErrTime > 250)
        {
            IIC_Stop();     /* 无应答：发停止，把总线恢复到空闲，避免卡死 */
            return 1;
        }
    }
    IIC_SCL = 0;            /* 收到应答：拉低时钟，准备下一个字节 */
    return 0;
}

/* 主机发 ACK：第 9 个时钟期间主机把 SDA 拉低，告诉从机"还想继续读" */
void IIC_Ack(void)
{
    IIC_SCL = 0;
    SDA_OUT();
    IIC_SDA = 0;            /* 低电平 = ACK */
    delay_us(2);
    IIC_SCL = 1;            /* 从机在第 9 个时钟高电平期间采样 */
    delay_us(2);
    IIC_SCL = 0;            /* 时钟回落，一拍结束 */
}

/* 主机发 NACK：第 9 个时钟期间 SDA 保持高，告诉从机"这是最后一个字节" */
void IIC_NAck(void)
{
    IIC_SCL = 0;
    SDA_OUT();
    IIC_SDA = 1;            /* 高电平 = NACK */
    delay_us(2);
    IIC_SCL = 1;
    delay_us(2);
    IIC_SCL = 0;
}

/* 发送一个字节：MSB 在前逐位移出；每位移出后给一段时钟脉冲让从机采样。
   只负责发送 8 位，应答由调用方随后调 IIC_Wait_Ack() 收 */
void IIC_Send_Byte(u8 txd)
{
    u8 t;                   /* 位循环计数，8 位一个字节 */
    SDA_OUT();
    IIC_SCL = 0;            /* 时钟先拉低：数据在 SCL 低电平期间变化才合法 */
    for (t = 0; t < 8; t++)
    {
        if ((txd & 0x80) >> 7)      /* 取当前最高位 */
            IIC_SDA = 1;
        else
            IIC_SDA = 0;
        txd <<= 1;          /* 左移为下一位做准备 */
        delay_us(2);        /* 数据建立时间 */
        IIC_SCL = 1;        /* 时钟高：从机在此采样当前位 */
        delay_us(2);
        IIC_SCL = 0;        /* 时钟低：本位结束 */
        delay_us(2);
    }
}

/* 读一个字节：逐位采样 SDA 拼成字节（MSB 在前）；收完按 ack 决定回 ACK 还是 NACK */
u8 IIC_Read_Byte(unsigned char ack)
{
    unsigned char i, receive = 0;   /* i = 位循环计数；receive = 逐位拼装的接收值 */

    SDA_IN();               /* SDA 切输入：数据由从机驱动 */
    for (i = 0; i < 8; i++)
    {
        IIC_SCL = 0;        /* 时钟低：从机准备下一位 */
        delay_us(2);
        IIC_SCL = 1;        /* 时钟高：此刻采样合法 */
        receive <<= 1;      /* 腾出最低位 */
        if (READ_SDA) receive++;    /* SDA 高则该位置 1 */
        delay_us(1);
    }
    if (!ack)
        IIC_NAck();         /* 最后一字节：回 NACK 结束本次传输 */
    else
        IIC_Ack();          /* 还有后续字节：回 ACK 继续 */
    return receive;
}
