#include "485.h"
#include "crc16.h"
#include "usart.h"
#include "reg.h"

extern u8 flag1s;
extern Register armReg;

// 每秒发送一次数据
void manage485(void)
{
	if (flag1s == 1)
	{
		// Rs485_Tx_data(u8 addr,u8 cmd,u8 regh,u8 regl,u16 val);
		Usart4_Rec_Cnt = 0;
		Rs485_Tx_data(0x01, 0x04, 0x30, 0x33, 0x20);
		flag1s = 0;
		// printf("%d",flag1s);
	}
	Rs485_Rx_Data();
}

void Rs485_Tx_data(u8 addr, u8 cmd, u8 regh, u8 regl, u16 val)
{
	u8 rs485_tx_buf[8];
	u16 crc;

	rs485_tx_buf[0] = addr;
	rs485_tx_buf[1] = cmd;
	rs485_tx_buf[2] = regh;
	rs485_tx_buf[3] = regl;
	rs485_tx_buf[4] = val >> 8;
	rs485_tx_buf[5] = val;

	crc = crc16(rs485_tx_buf, 6);

	rs485_tx_buf[6] = crc >> 8;
	rs485_tx_buf[7] = crc;
	Usart4_Send(rs485_tx_buf, 8);
}

void Rs485_Rx_Data()
{
	// reg
	u16 i;
	u16 crc = 0;
	u16 val_h;
	u16 val_l;

	u8 rs485_rx_buf[100], err = 0;
	u16 len = 0, temp = 0;

	if (!Usart4_Rec_Cnt)
		return;

	len = Usart4_Rec_Cnt;
	Usart4_Rec_Cnt = 0;

	for (i = 0; i < len; i++)
	{
		rs485_rx_buf[i] = DMA_Rece_Buf4[i];
	}

	// CRC校验
	crc = crc16(rs485_rx_buf, len - 2);
	if (crc != CharToInt(&rs485_rx_buf[len - 2]))
	{

		return;
	}

	//	val_h=rs485_rx_buf[3] & 0x00ff;
	//	val_l=rs485_rx_buf[4] & 0x00ff;
	//	armReg.bat_state=(val_h << 8) | val_l;									//13电池状态	33
	//	val_h=rs485_rx_buf[9] & 0x00ff;
	//	val_l=rs485_rx_buf[10] & 0x00ff;
	//	armReg.temperature=(val_h << 8) | val_l;								//16环境温度	36
	//
	//	val_h=rs485_rx_buf[39] & 0x00ff;
	//	val_l=rs485_rx_buf[40] & 0x00ff;
	//	armReg.bat_cap=(val_h << 8) | val_l; 										//12电池容量	45
	//
	//	val_h=rs485_rx_buf[41] & 0x00ff;
	//	val_l=rs485_rx_buf[42] & 0x00ff;
	//	armReg.bat_vol=(val_h << 8) | val_l;										//10电池电压	46
	//
	//	val_h=rs485_rx_buf[43] & 0x00ff;
	//	val_l=rs485_rx_buf[44] & 0x00ff;
	//	armReg.bat_curr=(val_h << 8) | val_l;										//11电池电流	47
	//
	//	val_h=rs485_rx_buf[57] & 0x00ff;
	//	val_l=rs485_rx_buf[58] & 0x00ff;
	//	armReg.solar_vol=(val_h << 8) | val_l;									//15太阳能电压	4E
	//
	//	val_h=rs485_rx_buf[59] & 0x00ff;
	//	val_l=rs485_rx_buf[60] & 0x00ff;
	//	armReg.solar_curr=(val_h << 8) | val_l;								//14太阳能电流	4F
}
