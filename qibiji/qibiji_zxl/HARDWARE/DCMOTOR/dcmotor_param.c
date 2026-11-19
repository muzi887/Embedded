#include "dcmotor_param.h"
#include "eeprom_drv.h"      /* AT24CXX_ReadLenByte / AT24CXX_WriteWord */
#include <stdio.h>
#include "debug_uart.h" 

/* ---- RAM 变量定义（对应 dcmotor_param.h 里的 extern 声明） ---- */
uint16_t g_gate_maxheight;
uint16_t g_motor_warning_curr_upper;
uint16_t g_gate_maxturns;

/* 空白 EEPROM 播种：仅写闸门三个默认参数，不碰 MQTT 凭据
 * （MQTT 凭据由 gprs_config_store 用编译期 fallback，不播种到 EEPROM）
 * 被 eeprom_drv 的 AT24CXX_Check 在检测到真空白 EEPROM 时调用。
 */
void paraToEeprom(void)
{
    g_motor_warning_curr_upper = DCMOTOR_DEFAULT_WARN_CURR;
    g_gate_maxheight           = DCMOTOR_DEFAULT_MAXHEIGHT;
    g_gate_maxturns            = DCMOTOR_DEFAULT_MAXTURNS;

    AT24CXX_WriteWord(EEPROM_ADDR_MOTOR_WARN_CURR, g_motor_warning_curr_upper);
    AT24CXX_WriteWord(EEPROM_ADDR_GATE_MAXHEIGHT,  g_gate_maxheight);
    AT24CXX_WriteWord(EEPROM_ADDR_GATE_MAXTURNS,   g_gate_maxturns);
    AT24CXX_WriteOneByte(EEPROM_ADDR_PARAM_VERSION, DCMOTOR_PARAM_VERSION);   /* 打版本 */
}

/* 上电：从 EEPROM 读入 RAM，无效值（0 / 0xFFFF）用默认值并写回播种 */
void dcmotor_param_load(void)
{
    uint16_t stored;

    /* 版本不符（旧固件遗留 / 空白 0xFF）→ 整区重播种并打上新版本号 */
    if (AT24CXX_ReadOneByte(EEPROM_ADDR_PARAM_VERSION) != DCMOTOR_PARAM_VERSION)
    {
        paraToEeprom();
    }

    /* ---- 堵转电流上限 ---- */
    stored = (uint16_t)AT24CXX_ReadLenByte(EEPROM_ADDR_MOTOR_WARN_CURR, 2);
    if (stored == 0u || stored == 0xFFFFu)
        g_motor_warning_curr_upper = DCMOTOR_DEFAULT_WARN_CURR;
    else
        g_motor_warning_curr_upper = stored;
    if (stored != g_motor_warning_curr_upper)
        AT24CXX_WriteWord(EEPROM_ADDR_MOTOR_WARN_CURR, g_motor_warning_curr_upper);

    /* ---- 闸门最大高度 ---- */
    stored = (uint16_t)AT24CXX_ReadLenByte(EEPROM_ADDR_GATE_MAXHEIGHT, 2);
    if (stored == 0u || stored == 0xFFFFu)
    {
        g_gate_maxheight = DCMOTOR_DEFAULT_MAXHEIGHT;
        AT24CXX_WriteWord(EEPROM_ADDR_GATE_MAXHEIGHT, g_gate_maxheight);
    }
    else
    {
        g_gate_maxheight = stored;
    }

    /* ---- 闸门最大圈数（0 = 未标定，交 DCMOTOR 自校准） ---- */
    stored = (uint16_t)AT24CXX_ReadLenByte(EEPROM_ADDR_GATE_MAXTURNS, 2);
    if (stored == 0xFFFFu)
    {
        g_gate_maxturns = DCMOTOR_DEFAULT_MAXTURNS;
        AT24CXX_WriteWord(EEPROM_ADDR_GATE_MAXTURNS, g_gate_maxturns);
    }
    else
    {
        g_gate_maxturns = stored;   /* 含 0 */
    }

    {
        char line[96];
        (void)snprintf(line, sizeof(line),
                       "gate param load: maxheight=%u maxturns=%u warn_curr=%u\r\n",
                       (unsigned)g_gate_maxheight, (unsigned)g_gate_maxturns,
                       (unsigned)g_motor_warning_curr_upper);
        DEBUG_UART_SendString(line);
    }
}

/* 写回单个字段：远程 config 或本地设置触发 */
void dcmotor_param_save_warn_curr(uint16_t mA)
{
    g_motor_warning_curr_upper = mA;
    AT24CXX_WriteWord(EEPROM_ADDR_MOTOR_WARN_CURR, mA);
}

void dcmotor_param_save_maxheight(uint16_t mm)
{
    g_gate_maxheight = mm;
    AT24CXX_WriteWord(EEPROM_ADDR_GATE_MAXHEIGHT, mm);
}

void dcmotor_param_save_maxturns(uint16_t turns)
{
    g_gate_maxturns = turns;
    AT24CXX_WriteWord(EEPROM_ADDR_GATE_MAXTURNS, turns);
}