#include "reg.h"
#include "delay.h"
#include "led.h"
#include "24cxx.h"
#include "gprs.h"
#include "uart3_at_sequence.h"
#include <stdio.h>
#include <string.h>

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
        armReg.motor_warning_curr_upper = 3500; // 3.5A，单位mA
        armReg.gate_maxturns = 80;
        OTA_Info.OTA_flag = OTA_A1_FLAG;
        /* 24C02 自检失败：升级状态无法落盘，显式关闭联网 OTA，与 bootloader 的失败策略一致。
           原先把 RAM 默认值置 1，会被后续 setUpdateValue 的整块 AT24CXX_Write 写回 EEPROM，
           等于在“存储异常”状态下把升级门打开。 */
        OTA_Info.UPDATE_flag = 0;
        OTA_Enable = 0;
        sprintf(OTA_Info.OTA_ver, "v0.0.0");
        printf("24C02 check fail: OTA disabled, OTA_ver = %s\n", OTA_Info.OTA_ver);
    }
    else
    {
        // EPROM的地址是前字节的0-2-4-6
        AT24CXX_Readdatas(0x0000, eeprom_buf, 10); // 读取数据，数据为16位宽度
        eepromToPara();

        printf("motor_warning_curr_upper = %x\n", eeprom_buf[2]);

        /* 从 EEPROM 读出 productKey / deviceKey 到全局变量 */
        {
            char pk_buf[MQTT_KEY_STR_LEN];
            char dk_buf[MQTT_KEY_STR_LEN];

            memset(pk_buf, 0, sizeof(pk_buf));
            memset(dk_buf, 0, sizeof(dk_buf));
            AT24CXX_Read(MQTT_PRODUCTKEY_BYTE_ADDR, (u8 *)pk_buf, MQTT_KEY_EEPROM_LEN);
            AT24CXX_Read(MQTT_DEVICEKEY_BYTE_ADDR, (u8 *)dk_buf, MQTT_KEY_EEPROM_LEN);
            pk_buf[MQTT_KEY_EEPROM_LEN] = '\0';
            dk_buf[MQTT_KEY_EEPROM_LEN] = '\0';

            if (pk_buf[0] != '\0' && (u8)pk_buf[0] != 0xFFu)
            {
                memset(g_mqtt_productKey, 0, sizeof(g_mqtt_productKey));
                memcpy(g_mqtt_productKey, pk_buf, MQTT_KEY_EEPROM_LEN);
                g_mqtt_productKey[MQTT_KEY_EEPROM_LEN] = '\0';
            }
            if (dk_buf[0] != '\0' && (u8)dk_buf[0] != 0xFFu)
            {
                memset(g_mqtt_deviceKey, 0, sizeof(g_mqtt_deviceKey));
                memcpy(g_mqtt_deviceKey, dk_buf, MQTT_KEY_EEPROM_LEN);
                g_mqtt_deviceKey[MQTT_KEY_EEPROM_LEN] = '\0';
            }
            printf("mqtt pk=%s, dk=%s\r\n", g_mqtt_productKey, g_mqtt_deviceKey);
        }

        AT24CXX_Read(OTA_BYTE_ADDR, (uint8_t *)&OTA_Info, OTA_INFOCB_SIZE); // 从24C02读取数据，存入OTA_Info结构体
        /* 半擦除/未初始化时版本串可能没有终止符：先强制截断，保证它是合法 C 串
           （同时保护 FirmwareVer() 等处对 OTA_ver 的 %s 使用） */
        OTA_Info.OTA_ver[sizeof(OTA_Info.OTA_ver) - 1] = '\0';

        /* 按字符串复制版本号到显示缓冲，以 '\0' 为界，多余位填空格。
           显示区固定 10 字节（banben[16] = 6 字节表头 + 10 字节版本），超长必须截断，
           否则 memcpy 越界，且 10 - len 会下溢成巨大的 size_t 造成 HardFault */
        len = strlen((char *)OTA_Info.OTA_ver);
        if (len > 10u)
            len = 10u;

        memcpy(&banben[6], OTA_Info.OTA_ver, len);
        memset(&banben[6 + len], ' ', 10 - len);
        /* bootloader 只认 ==1，这里做同样的归一化，避免半擦除的 0xFF 在 LCD 上显示“使能：是” */
        OTA_Enable = (OTA_Info.UPDATE_flag == 1u) ? 1u : 0u;

        printf("OTA_ver-- = %.10s,%x\r\n", OTA_Info.OTA_ver, OTA_Info.OTA_flag);
        printf("OTA_enable-- = %d\r\n", OTA_Info.UPDATE_flag);
    }
}

// ֵ
void eepromToPara(void)
{
    u16 stored_current_limit;
    u32 migrated;

    //armReg.gate_upper = eeprom_buf[0];              // 闸门上限值
    //armReg.gate_lower = eeprom_buf[1];              // 闸门下限值
    stored_current_limit = eeprom_buf[GATE_MOTOR_MAXCURR_WORD_IDX];
    if (stored_current_limit == 0u || stored_current_limit == 0xFFFFu)
    {
        armReg.motor_warning_curr_upper = 3500u;
    }
    else if (stored_current_limit < 1000u)
    {
        /* 兼容旧0.1A编码（如35表示3.5A），迁移后统一为mA。 */
        migrated = (u32)stored_current_limit * 100u;
        armReg.motor_warning_curr_upper = (migrated > 0xFFFFu) ? 0xFFFFu : (u16)migrated;
    }
    else
    {
        armReg.motor_warning_curr_upper = stored_current_limit;
    }

    if (stored_current_limit != armReg.motor_warning_curr_upper)
        AT24CXX_WriteWord(0x0004, armReg.motor_warning_curr_upper);
    armReg.gate_maxheight = eeprom_buf[GATE_MAXHEIGHT_WORD_IDX];                //闸门高差值(高度值)
    armReg.gate_maxturns = eeprom_buf[GATE_MAXTURNS_WORD_IDX];                 //闸门高差对应的螺纹圈数
    armReg.gate_kaidu = eeprom_buf[GATE_KAIDU_WORD_IDX];                        //闸门实际开度值
    armReg.gate_kaidu_count = eeprom_buf[GATE_KAIDU_COUNT_WORD_IDX]; 

    //armReg.range_reality = armReg.gate_kaidu;        // 是实时开度

    //kaival = armReg.gate_kaidu;
    armReg.gate_kaidu= armReg.gate_kaidu_count * 100 / armReg.gate_maxturns;

    Num_count = armReg.gate_kaidu_count;

    printf("gate para is -->HightCount=%d,KaiduCount=%d,kaidu=%d\r\n", armReg.gate_maxturns, Num_count,armReg.gate_kaidu);
}

/**
 * @brief 向指定 EEPROM 字节地址写入定长 MQTT 字符串（最多 32 字符，剩余填 0）
 * @note  只写"空白槽"：首字节既不是 0x00 也不是 0xFF 时视为已有有效内容，直接跳过。
 *        这是 P1-A 的第二道防线——即使走到播种默认值的路径，也不会覆盖真实凭据。
 */
static void mqtt_eeprom_write_str(u16 addr, const char *s)
{
    char buf[MQTT_KEY_EEPROM_LEN];
    u8 first = AT24CXX_ReadOneByte(addr);
    size_t n;

    if (first != 0x00u && first != 0xFFu)
    {
        printf("mqtt eeprom slot @%u keep existing (first=0x%02X)\r\n", (unsigned)addr, (unsigned)first);
        return;
    }

    memset(buf, 0, sizeof(buf));
    if (s != NULL)
    {
        n = strlen(s);
        if (n > MQTT_KEY_EEPROM_LEN)
            n = MQTT_KEY_EEPROM_LEN;
        if (n > 0u)
            memcpy(buf, s, n);
    }
    AT24CXX_Write(addr, (u8 *)buf, MQTT_KEY_EEPROM_LEN);
}

/**
 * @brief MQTT 参数区初始化：写入 PK / DK / Secret / Addr 默认值
 *        地址与长度见 reg.h（各 MQTT_KEY_EEPROM_LEN 字节）
 */
void mqtt_eeprom_init_defaults(void)
{
    static const char def_pk[] = "pk001";
    static const char def_dk[] = "dev001";
    static const char def_sec[] = "d9fb85a1c0134c3c9f6bd94e8d474e27";
    static const char def_addr[] = "82.157.234.123:1883";

    mqtt_eeprom_write_str(MQTT_PRODUCTKEY_BYTE_ADDR, def_pk);
    mqtt_eeprom_write_str(MQTT_DEVICEKEY_BYTE_ADDR, def_dk);
    mqtt_eeprom_write_str(MQTT_DEVICESECRET_BYTE_ADDR, def_sec);
    mqtt_eeprom_write_str(MQTT_MQTTADDR_BYTE_ADDR, def_addr);

    printf("mqtt eeprom init: pk@%u dk@%u sec@%u addr@%u len=%u\r\n",
           (unsigned)MQTT_PRODUCTKEY_BYTE_ADDR,
           (unsigned)MQTT_DEVICEKEY_BYTE_ADDR,
           (unsigned)MQTT_DEVICESECRET_BYTE_ADDR,
           (unsigned)MQTT_MQTTADDR_BYTE_ADDR,
           (unsigned)MQTT_KEY_EEPROM_LEN);
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
// 将参数写入 eeprom
void paraToEeprom()
{
    armReg.gate_maxheight = 1000;               // 1000mm
    armReg.motor_warning_curr_upper = 3500;     // 3.5A，单位mA
    armReg.gate_maxturns = 80;                  // 闸门转速值

    // EPROM 的地址是前字节的 0-2-4-6
    // AT24CXX_WriteWord(0x0000,armReg.range_target);// 写入目标值
    // AT24CXX_WriteWord(0x0002,armReg.range_reality);// 写入实际值
    AT24CXX_WriteWord(0x0004, armReg.motor_warning_curr_upper); // 写入电机报警电流上限值
    AT24CXX_WriteWord(0x0006, armReg.gate_maxheight);                 //
    AT24CXX_WriteWord(0x0008, armReg.gate_maxturns);                 // 
    AT24CXX_WriteWord(0x000A, armReg.gate_kaidu);               //
    AT24CXX_WriteWord(0x000C, armReg.gate_kaidu_count);               //

    mqtt_eeprom_init_defaults();
}
