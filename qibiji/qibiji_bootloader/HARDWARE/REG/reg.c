/* REG 模块：24C02 参数登记处。
   职责：EEPROM 布局属主；上电把 OTA 状态块与 MQTT 身份
   从 24C02 装载进 RAM（新芯片先播种再装载）；提供状态块分字段读写供 BOOT/GPRS 使用。 */

#include "reg.h"
#include <stdio.h>      /* printf/sprintf/snprintf：调试输出与字符串组装 */
#include <string.h>     /* memset：状态块清零与版本串清空 */

/* ---- 全局定义---- */
OTA_InfoCB OTA_Info;    /* 状态块 RAM 副本：上电装载，关键变化由分字段写函数同步落盘 */
u8 g_ota_lcd_err  = 0;  /* 待显示错误码：0 = 无错误，登记点见各使用处 */
u8 g_ota_lcd_hold = 0;  /* 1 = 屏就绪后显示 3 秒再尝试进应用；0 = 显示即返回 */

char g_mqtt_productKey[MQTT_KEY_STR_LEN] = MQTT_PRODUCTKEY_DEFAULT;   /* 初值取兜底值，EEPROM 槽位有效时被覆盖 */
char g_mqtt_deviceKey[MQTT_KEY_STR_LEN]  = MQTT_DEVICEKEY_DEFAULT;

/* 槽位首字节必须是可打印 ASCII：0x00（空）与 0xFF（未写）都视为无效 */
static u8 MqttKeyUsable(const char *buf)
{
    u8 c = (u8)buf[0];                                 /* 取槽位首字节判断 */
    return ((c >= 0x20u) && (c < 0x7Fu)) ? 1u : 0u;    /* 可打印 ASCII 区间 [0x20, 0x7E] 才可信 */
}

/* 从 24C02 读取 productKey / deviceKey（与 A1 应用同地址、同长度）。
   槽位无效时全局变量保持兜底值不动；只读不写，不碰 EEPROM 键槽位。 */
void Reg_ReadMqttKeys(void)
{
    char pk[MQTT_KEY_STR_LEN];   /* productKey 读出缓冲：32 字节 + 结尾 */
    char dk[MQTT_KEY_STR_LEN];   /* deviceKey 读出缓冲 */

    memset(pk, 0, sizeof(pk));   /* 先清零，防止槽位读出的杂质尾巴参与字符串化 */
    memset(dk, 0, sizeof(dk));
    AT24CXX_Read(MQTT_PRODUCTKEY_BYTE_ADDR, (u8 *)pk, MQTT_KEY_EEPROM_LEN);
    AT24CXX_Read(MQTT_DEVICEKEY_BYTE_ADDR, (u8 *)dk, MQTT_KEY_EEPROM_LEN);
    pk[MQTT_KEY_EEPROM_LEN] = '\0';   /* 兜底封口：槽位写满 32 字符时也要有结尾 */
    dk[MQTT_KEY_EEPROM_LEN] = '\0';

    if (MqttKeyUsable(pk))   /* 槽位可信才覆盖全局变量，否则保持兜底值 */
    {
        snprintf(g_mqtt_productKey, sizeof(g_mqtt_productKey), "%s", pk);
    }
    if (MqttKeyUsable(dk))
    {
        snprintf(g_mqtt_deviceKey, sizeof(g_mqtt_deviceKey), "%s", dk);
    }

    printf("mqtt id: pk=%s, dk=%s\r\n", g_mqtt_productKey, g_mqtt_deviceKey);
}

/* 上电装载入口（main 调用）：三态自检分派。
   OK(0)    = 标志在，EEPROM 内容可信：直接读状态块 + 取 MQTT 身份；
   FRESH(2) = 新芯片/被整片擦过，标志已由 Check 补写：先播种默认值，再走正常读取；
   ERROR(1) = 写标志后仍读不回 0x55，硬件故障：状态块用 RAM 默认值，关闭联网 OTA。。 */
void Reg_ReadOtaInfo(void)
{
    u8 chk;   /* 自检结论：AT24CXX_OK / AT24CXX_FRESH / AT24CXX_ERROR */

    memset(&OTA_Info, 0, OTA_INFOCB_SIZE);   /* 先整体清零：故障分支的默认值在此基础上填 */
    chk = AT24CXX_Check();                   /* 三态自检 */

    if (chk == AT24CXX_FRESH)
    {
        Reg_SeedDefaults();   /* 新芯片：播种默认值（内部写回 24C02），随后走正常读取路径与老芯片殊途同归 */
    }
    else if (chk == AT24CXX_ERROR)
    {
        /* 24C02 自检/读写失败（芯片或 I2C 总线异常）：升级状态无法落盘，
           下载好的固件也没法靠 OTA_flag 引导，显式关闭联网 OTA，让设备直接运行 A1。
           只 memset 成 0 属于「静默失效」，现场看不出原因；版本串与 Reg_SeedDefaults() 的 "v0.0.0" 保持一致。 */
        OTA_Info.OTA_flag    = OTA_A1_FLAG;
        OTA_Info.OTA_size    = 0;
        OTA_Info.UPDATE_flag = 0;
        g_ota_lcd_err  = OTA_ERR_EEPROM;                /* main 在 LCD 就绪后提示「存储异常」 */
        g_ota_lcd_hold = 0;                             /* 持久性错误：只显示不等待 */
        sprintf((char *)OTA_Info.OTA_ver, "v0.0.0");    /* 首参转 char*：OTA_ver 是 u8 数组，消除 #167-D 符号性告警 */
        printf("24C02 check fail: OTA disabled, run A1 (OTA_ver = %s)\n", OTA_Info.OTA_ver);
        return;   /* 故障分支到此为止：不读 24C02，MQTT 键保持兜底值 */
    }

    /* OK 或 FRESH（播种后）都到这里：读状态块 + 取 MQTT 身份 */
    AT24CXX_Read(OTA_ADDR, (u8 *)&OTA_Info, OTA_INFOCB_SIZE);   /* 整块读入 RAM 副本 */
    printf("OTA_ver = %s,%x,%d\n", OTA_Info.OTA_ver, OTA_Info.OTA_flag, OTA_Info.UPDATE_flag);
    Reg_ReadMqttKeys();   /* 同时取出 platform 需要的 productKey / deviceKey */
}

/* 播种：把 OTA 状态块默认值写进 24C02。
   触发者只有 Reg_ReadOtaInfo 的 FRESH 分支；写的是整块默认值
   （flag=A1 当前区最新、UPDATE=1 默认开启联网 OTA、ver=v0.0.0 尚无版本记录）。 */
void Reg_SeedDefaults(void)
{
    memset(&OTA_Info, 0, OTA_INFOCB_SIZE);
    OTA_Info.OTA_flag = OTA_A1_FLAG;   /* 标志位默认值：当前 A 区是最新的 */
    OTA_Info.UPDATE_flag = 1;          /* 0 = 无需 OTA，1 = 需要联网 OTA（默认开启） */
    sprintf((char *)OTA_Info.OTA_ver, "v0.0.0");   /* 首参转 char*：OTA_ver 是 u8 数组，消除 #167-D 符号性告警 */
    printf("set default para\r\n");
    printf("OTA_ver = %s,%x\n", OTA_Info.OTA_ver, OTA_Info.OTA_flag);
    AT24CXX_Write(OTA_ADDR, (u8 *)&OTA_Info, OTA_INFOCB_SIZE);   /* 整块写回 24C02 */
}

/* ---- OTA 状态块分字段读写 ---- */

/* 只写 OTA_flag 字段（偏移 OTA_OFF_FLAG，4 字节）：RAM 副本与 24C02 同步更新。
   返回 AT24CXX_OK/ERROR，BOOT 用返回值确认「已安装」是否真的落盘 */
u8 Ota_WriteFlag(u32 flag)
{
    OTA_Info.OTA_flag = flag;   /* 先改 RAM 副本，保持内存与 EEPROM 一致 */
    return AT24CXX_Write(OTA_ADDR + OTA_OFF_FLAG, (u8 *)&OTA_Info.OTA_flag, 4);
}

/* 清掉 OTA_ver：表示「本机没有可用的已安装版本」。
   这样 putVersion 的版本闸不会再认为「已是最新」，下次联网会重新下载安装 */
void Ota_ClearInstalledVersion(void)
{
    memset(&OTA_Info.OTA_ver, 0, 32);   /* 全 0 = 版本闸失效 */
    AT24CXX_Write(OTA_ADDR + OTA_OFF_VER, (u8 *)&OTA_Info.OTA_ver[0], 32);
}

/* 安装成功 → 关闭升级标志（UPDATE_flag=0）：之后上电直接跑应用、不再联网核查。
   再次升级需由平台经 A1 应用把 UPDATE_flag 置 1（见 reg.h 字段注释） */
void Ota_CloseUpdateGate(void)
{
    OTA_Info.UPDATE_flag = 0;
    AT24CXX_Write(OTA_ADDR + OTA_OFF_UPDATE, (u8 *)&OTA_Info.UPDATE_flag, 1);
}

/* 读自愈重搬失败计数（地址 OTA_RETRY_ADDR，1 字节）：成功清零、失败累加，供自愈限流 */
u8 Ota_RetryCount(void)
{
    u8 v = 0;   /* 读出值：空片 0xFF 会得到 255，但正常流程下播种/成功路径都会清零 */
    AT24CXX_Read(OTA_RETRY_ADDR, &v, 1);
    return v;
}

/* 写自愈重搬失败计数 */
void Ota_RetryCountSet(u8 v)
{
    AT24CXX_Write(OTA_RETRY_ADDR, &v, 1);
}
