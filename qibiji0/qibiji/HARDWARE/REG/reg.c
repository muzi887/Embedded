#include "reg.h"
#include "delay.h"
#include "led.h"
#include "24cxx.h"
#include "gprs.h"

u8 eepromWriteFlag = 0;
Register armReg;
u8 eepromerr;                  // 判断eeprom是否发生错误
u16 eeprom_buf[eepromLength]; // eeprom 存储缓冲区

u16 EepromAddr = 0;
u8 Witeflag = 0; // 写地址的写标志

u8 OTA_Enable=0;


extern u16 Num_count;
extern u8 kaival;
extern uint8_t banben[16];
// 存储参数读取
void para_read(void)
{
    u8 len;
    //TODO 这部分没用，可以删除，或者改成从24C02读取参数
    if (AT24CXX_Check()) // 检测不到24c02，初始化默认值
    {
        armReg.gate_maxheight = 1000;                  // 1000mm
        armReg.motor_warning_curr_upper = 35;   // 3.5A
        OTA_Info.OTA_flag = OTA_A1_FLAG;
        sprintf(OTA_Info.OTA_ver, "1.0.0");
        printf("OTA_ver = %s\n", OTA_Info.OTA_ver);
    }
    else
    {
        // EPROM的地址是前字节的0-2-4-6
        AT24CXX_Readdatas(0x0000, eeprom_buf, 10); // 读取数据，数据为16位宽度
        eepromToPara();

        printf("motor_warning_curr_upper = %x\n", eeprom_buf[2]);

        AT24CXX_Read(OTA_ADDR, (uint8_t *)&OTA_Info, OTA_INFOCB_SIZE); // 从24C02读取数据，存入OTA_Info结构体
        
        /* 按字符串复制版本号到显示缓冲，以 '\0' 为界，多余位填空格 */
       
            len = strlen((char *)OTA_Info.OTA_ver);

            memcpy(&banben[6], OTA_Info.OTA_ver, len);
            memset(&banben[6 + len], ' ', 10 - len);
        OTA_Enable=OTA_Info.UPDATE_flag;

        printf("OTA_ver-- = %s,%x\r\n", OTA_Info.OTA_ver, OTA_Info.OTA_flag);
        printf("OTA_enable-- = %d\r\n", OTA_Info.UPDATE_flag);
    }
}

// ֵ
void eepromToPara(void)
{
    //armReg.gate_upper = eeprom_buf[0];              // 闸门上限值
    //armReg.gate_lower = eeprom_buf[1];              // 闸门下限值
    armReg.motor_warning_curr_upper = eeprom_buf[GATE_MOTOR_MAXCURR_ADDR]; // 电机报警电流上限值
    armReg.gate_maxheight = eeprom_buf[GATE_MAXHEIGHT_ADDR];                //闸门高差值(高度值)
    armReg.gate_maxturns = eeprom_buf[GATE_MAXTURNS_ADDR];                 //闸门高差对应的螺纹圈数
    armReg.gate_kaidu = eeprom_buf[GATE_KAIDU_ADDR];                        //闸门实际开度值
    armReg.gate_kaidu_count = eeprom_buf[GATE_KAIDU_COUNT_ADDR]; 

    //armReg.range_reality = armReg.gate_kaidu;        // 是实时开度

    //kaival = armReg.gate_kaidu;
    armReg.gate_kaidu= armReg.gate_kaidu_count * 100 / armReg.gate_maxturns;

    Num_count = armReg.gate_kaidu_count;

    printf("gate para is -->HightCount=%d,KaiduCount=%d,kaidu=%d\r\n", armReg.gate_maxturns, Num_count,armReg.gate_kaidu);
}

// 将参数写入 eeprom
void paraToEeprom()
{
    armReg.gate_maxheight = 1000;               // 1000mm
    armReg.motor_warning_curr_upper = 35; // 3.5A
    armReg.gate_maxturns = 80;                 // 闸门转速值

    // EPROM 的地址是前字节的 0-2-4-6
    // AT24CXX_WriteWord(0x0000,armReg.range_target);// 写入目标值
    // AT24CXX_WriteWord(0x0002,armReg.range_reality);// 写入实际值
    AT24CXX_WriteWord(0x0002, armReg.motor_warning_curr_upper); // 写入电机报警电流上限值
    AT24CXX_WriteWord(0x0003, armReg.gate_maxheight);                 //
    AT24CXX_WriteWord(0x0004, armReg.gate_maxturns);                 // 
    AT24CXX_WriteWord(0x0005, armReg.gate_kaidu);               //
    AT24CXX_WriteWord(0x0006, armReg.gate_kaidu_count);               //
}

// 初始化寄存器参数
void initReg(void)
{
    armReg.motor_on_off = 3;
}

//eeprom
void Eeprom_Wpara()
{
	if (Witeflag == 0)
		return;
	Witeflag = 0;
	switch (EepromAddr)
	{
	case 0x0004:
		AT24CXX_WriteWord(0x0008, armReg.gate_maxturns); 
		printf("w_gate_rpm=%d\n", armReg.gate_maxturns);
		break;
	case 0x0005:
		AT24CXX_WriteWord(0x000A, armReg.gate_kaidu);
		//printf("w_gate_kaidu=%d\n", armReg.gate_kaidu);
		break;
    case 0x0006:
		AT24CXX_WriteWord(0x000C, armReg.gate_kaidu_count);
		printf("w_gate_kaidu_count=%d\n", armReg.gate_kaidu_count);
		break;
	default:
		break;
	}
}
