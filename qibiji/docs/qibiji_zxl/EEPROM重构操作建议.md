# EEPROM（AT24C02）重构操作建议

> 独立于 GPRS 阶段一~四，本模块为参数持久化层。GPRS 阶段四的远程配置（`getConfig` / `config`）依赖本模块落盘，**所以建议在阶段四之前完成**。

## 原工程现状

- **芯片**：AT24C02（256 字节，硬件 I²C 走 GPIO 模拟：PCx SCL/SDA）
- **驱动**：`HARDWARE/24CXX/24cxx.c` + `.h`（I²C 字节级读写 + 单字节/字/定长读写 + Check）
- **参数层**：`HARDWARE/REG/reg.c` + `.h`（`Register` 结构体 + 地址 map + `para_read/para_save`）
- **三个用途**：

| 区 | 地址 | 大小 | 存什么 |
|---|---|---|---|
| 闸门参数 | 0x00 - 0x13 | 10 × u16（20 字节） | `gate_maxheight`（mm）、`motor_warning_curr_upper`（mA）、`gate_maxturns`、`gate_kaidu`、`gate_kaidu_count` |
| MQTT 凭据 | 0x40 - 0xBF | 4 × 32B | `productKey` / `deviceKey` / `deviceSecret` / `mqtt_addr`（各 32 字节槽） |
| OTA Info | 0xC8 - 0xF3 | OTA_InfoCB 结构体 | 升级标志、版本串等 |
| Marker | 0xFA | 1B | `0x55` 标志，`AT24CXX_Check()` 用 |

## 重构目标

1. 保持与原工程**完全相同**的芯片、地址布局、引脚、驱动实现（直接拷贝 24cxx）；
2. **拆掉**原工程 `reg.h` 里那种"一大坨 Register 结构体 + armReg 全局变量"的设计，改成**每个业务模块自带 RAM 变量 + EEPROM 读写函数**；
3. **保留**原工程的两条安全机制：
   - `AT24CXX_Check()` 的"marker 丢失但数据有效 → 不播种"判定
   - `mqtt_eeprom_write_str()` 的"只写空白槽，不覆盖已有凭据"保护

## 新增文件清单

| 文件 | 职责 | 来源 |
|---|---|---|
| `HARDWARE/IIC/myiic.h` + `.c` | GPIO 模拟 I²C（PB10=SCL / PB11=SDA），`IIC_Init/Start/Stop/Send_Byte/Read_Byte/Wait_Ack` | **直接从原工程 `HARDWARE/IIC/` 拷贝**，不改 |
| `HARDWARE/EEPROM/eeprom_drv.h` + `.c` | AT24C02 字节级/字级读写 + Check 播种 | **从原工程 `HARDWARE/24CXX/` 拷贝**，改名（24cxx → eeprom_drv），去掉 reg.h 依赖、保留喂狗（改为 `WDG_Feed()`，见 §1 与《看门狗重构操作建议》§3.2） |
| `HARDWARE/GPRS/gprs_config_store.h` + `.c` | MQTT 凭据读写（PK/DK/Secret/Addr） | 新建，内部调 eeprom_drv |
| `HARDWARE/DCMOTOR/dcmotor_param.h` + `.c` | 闸门参数读写（maxheight / warning_curr / maxturns） | 新建，内部调 eeprom_drv |
| `HARDWARE/GPRS/ota_store.h` + `.c` | OTA Info 读写（阶段四才用，先占位） | 新建 |

> **注意**：zxl 工程里原来有一份编译期参数宏（`GPRS_PRODUCT_KEY` 等在 `gprs_config.h`），EEPROM 层接管后它们要变成**默认值 fallback**——EEPROM 里有效就用 EEPROM，否则用编译期宏。

## §1 eeprom_drv（从原工程拷贝，接口完全一致）

`eeprom_drv.h`：

```c
#ifndef __EEPROM_DRV_H
#define __EEPROM_DRV_H

#include "stm32f10x.h"   /* 提供 uint8_t / uint16_t / uint32_t 类型 */

/* ---- 芯片型号容量（最大可访问字节地址），与原工程 24cxx.h 一致 ---- */
#define AT24C01     127
#define AT24C02     255
#define AT24C04     511
#define AT24C08     1023
#define AT24C16     2047
#define AT24C32     4095
#define AT24C64     8191
#define AT24C128    16383
#define AT24C256    32767

#define EE_TYPE     AT24C02         /* 本板用 AT24C02（256 字节） */
#define EE_ADDR     250u            /* Marker 标志字节地址 */

/* ---- 字节级读写 ---- */
uint8_t  AT24CXX_ReadOneByte(uint16_t ReadAddr);
void     AT24CXX_WriteOneByte(uint16_t WriteAddr, uint8_t DataToWrite);

/* ---- 字级（uint16_t little endian）读写 ---- */
void     AT24CXX_WriteWord(uint16_t WriteAddr, uint16_t Data);
uint32_t AT24CXX_ReadLenByte(uint16_t ReadAddr, uint8_t Len);  /* 2 或 4 */
void     AT24CXX_WriteLenByte(uint16_t WriteAddr, uint32_t DataToWrite, uint8_t Len);

/* ---- 定长缓冲 ---- */
void     AT24CXX_Read(uint16_t ReadAddr, uint8_t *pBuffer, uint16_t NumToRead);
void     AT24CXX_Write(uint16_t WriteAddr, uint8_t *pBuffer, uint16_t NumToWrite);

/* ---- 批量读取闸门参数（10 × uint16_t） ---- */
void     AT24CXX_Readdatas(uint16_t ReadAddr, uint16_t *Readbuf, uint8_t Len);

/* ---- 上电自检 + 空白播种 ---- */
uint8_t  AT24CXX_Check(void);

/* ---- 初始化（内部调 IIC_Init） ---- */
void     AT24CXX_Init(void);

#endif
```

`eeprom_drv.c` 拷自原工程 `24cxx.c`，**仅以下改动**：① 保留原工程的喂狗动作，`IWDG_Feed()` 改名为 `WDG_Feed()`（IWDG 已启用，见《看门狗重构操作建议》§3.2）；② `#include` 由 `reg.h` / `adc.h` 改为 `dcmotor_param.h` + `gprs_config_store.h` + `wdg.h`（`reg.h` 已删除，符号迁入新模块）；③ 空白播种分支补一行 `printf` 便于上电验证。其余逐字保留，包括：
- `IIC_Start/Stop/Send_Byte/Read_Byte/Wait_Ack`（来自 `myiic.h`，GPIO 模拟 I²C，PB10=SCL / PB11=SDA，见文件清单）
- `AT24CXX_Check()` 里的"marker 丢失但数据有效 → 不播种"逻辑（`paraToEeprom` 已迁入 `dcmotor_param.c`，见 §2，仅在真空白时调用）
- `AT24CXX_WriteOneByte` 里的 `delay_ms(10)`（EEPROM 写周期，必须保留）
- `AT24CXX_Write()` 里每 8 字节 `UartRx_PumpFromDma(0)` 搬帧（长写期间不让串口停摆）

`eeprom_drv.c` 完整源码：

```c
#include "eeprom_drv.h"
#include "delay.h"               /* delay_ms */
#include "wdg.h"                 /* WDG_Feed()：单字节写周期后喂狗 */
#include "dcmotor_param.h"       /* paraToEeprom()：闸门参数播种 */
#include "gprs_config_store.h"   /* MQTT_PRODUCTKEY_BYTE_ADDR 等地址宏 */
#include "uart_queue.h"          /* UartRx_PumpFromDma */
#include <stdio.h>

// 初始化IIC接口
void AT24CXX_Init(void)
{
	IIC_Init();
}

// 从AT24CXX指定地址读出一个字节
// ReadAddr:开始读出的地址
// 返回值  :读到的数据
uint8_t AT24CXX_ReadOneByte(uint16_t ReadAddr)
{
	uint8_t temp = 0;
	IIC_Start();
	if (EE_TYPE > AT24C16)
	{
		IIC_Send_Byte(0xA0); // 发送写命令
		IIC_Wait_Ack();
		IIC_Send_Byte(ReadAddr >> 8); // 发送高地址
		IIC_Wait_Ack();
	}
	else
		IIC_Send_Byte(0xA0 + ((ReadAddr / 256) << 1)); // 发送器件地址0xA0,写命令

	IIC_Wait_Ack();
	IIC_Send_Byte(ReadAddr % 256); // 发送低地址
	IIC_Wait_Ack();
	IIC_Start();
	IIC_Send_Byte(0xA1 + ((ReadAddr / 256) << 1)); // 进入接收模式
	IIC_Wait_Ack();
	temp = IIC_Read_Byte(0);
	IIC_Stop(); // 产生一个停止条件
	return temp;
}
// 在AT24CXX指定地址写入一个字节
// WriteAddr  :写入数据的目的地址
// DataToWrite:要写入的数据
void AT24CXX_WriteOneByte(uint16_t WriteAddr, uint8_t DataToWrite)
{
	IIC_Start();
	if (EE_TYPE > AT24C16)
	{
		IIC_Send_Byte(0xA0); // 发送写命令
		IIC_Wait_Ack();
		IIC_Send_Byte(WriteAddr >> 8); // 发送高地址
	}
	else
	{
		IIC_Send_Byte(0xA0 + ((WriteAddr / 256) << 1)); // 发送器件地址0XA0,写命令
	}
	IIC_Wait_Ack();
	IIC_Send_Byte(WriteAddr % 256); // 发送低地址
	IIC_Wait_Ack();
	IIC_Send_Byte(DataToWrite); // 发送字节
	IIC_Wait_Ack();
	IIC_Stop(); // 产生一个停止条件
	delay_ms(10); /* EEPROM 单字节写周期：必须等待，否则下一次读写会失败 */
	WDG_Feed();   /* 单字节 10ms，32B 槽 320ms、四槽连续 1.28s：这里喂狗避免越过 2.1s 窗口 */
}
// 在AT24CXX里面的指定地址开始写入长度为Len的数据
// 该函数可以写入16bit或者32bit的数据.
// WriteAddr  :开始写入的地址
// DataToWrite:数据数组的首地址
// Len        :要写入数据的长度2,4
void AT24CXX_WriteLenByte(uint16_t WriteAddr, uint32_t DataToWrite, uint8_t Len)
{
	uint8_t t;
	for (t = 0; t < Len; t++)
	{
		AT24CXX_WriteOneByte(WriteAddr + t, (DataToWrite >> (8 * t)) & 0xff);
	}
}

// 一次性读取16bit eeprom数据
void AT24CXX_Readdatas(uint16_t ReadAddr, uint16_t *Readbuf, uint8_t Len)
{
	uint8_t t;
	for (t = 0; t < Len; t++)
	{
		Readbuf[t] = AT24CXX_ReadLenByte(ReadAddr, 2);
		ReadAddr += 2;
	}
}

// 在AT24CXX里面的指定地址开始读出长度为Len的数据
// 该函数用于读出16bit或者32bit的数据.
// ReadAddr   :开始读出的地址
// 返回值     :数据
// Len        :要读出数据的长度2,4
uint32_t AT24CXX_ReadLenByte(uint16_t ReadAddr, uint8_t Len)
{
	uint8_t t;
	uint32_t temp = 0;
	for (t = 0; t < Len; t++)
	{
		temp <<= 8;
		temp += AT24CXX_ReadOneByte(ReadAddr + Len - t - 1);
	}
	return temp;
}

// 指定地址写入一个字
void AT24CXX_WriteWord(uint16_t WriteAddr, uint16_t Data)
{
	uint8_t temp_l = (uint8_t)(Data >> 8);
	uint8_t temp_h = (uint8_t)(Data & 0x00ff);
	AT24CXX_WriteOneByte(WriteAddr, temp_h);
	AT24CXX_WriteOneByte(WriteAddr + 1, temp_l);
}

// 检查AT24CXX是否正常
// 这里用了24XX的最后一个地址(255)来存储标志字.
// 如果用其他24C系列,这个地址要修改
// 返回1:检测失败
// 返回0:检测成功
uint8_t AT24CXX_Check(void)
{
	uint8_t temp;

	temp = AT24CXX_ReadOneByte(EE_ADDR); // 读取每次上电都检测AT24CXX
	if (temp == 0x55)
		return 0;
	else // 排除第一次初始化的错误
	{
		AT24CXX_WriteOneByte(EE_ADDR, 0x55);
		temp = AT24CXX_ReadOneByte(EE_ADDR);
		if (temp == 0x55)
		{
			/*
			 * 标记字节丢失有两种可能：
			 *   a) 全新/空白 EEPROM（数据区也是空白）→ 按原逻辑播种默认参数；
			 *   b) 标记字节被写坏，但 MQTT 凭据与闸门标定值仍在 →
			 *      只补回标记，绝不回写默认值，否则会把真实 deviceKey/deviceSecret
			 *      覆盖成 pk001/dev001，造成「设备在平台显示在线、业务上行却被拒」的静默半瘫。
			 */
			uint8_t pk_first = AT24CXX_ReadOneByte(MQTT_PRODUCTKEY_BYTE_ADDR);
			uint16_t gate_word = (uint16_t)AT24CXX_ReadLenByte(0x0006, 2); /* gate_maxheight */
			uint8_t data_blank = ((pk_first == 0x00u || pk_first == 0xFFu) &&
							 (gate_word == 0x0000u || gate_word == 0xFFFFu)) ? 1u : 0u;

			if (data_blank)
			{
				// 写入参数
				printf("24C02 blank: seed defaults via paraToEeprom\r\n");
				paraToEeprom();
			}
			else
			{
				printf("24C02 marker lost but data valid: keep params (pk[0]=%02X, gate=0x%04X)\r\n",
					   (unsigned)pk_first, (unsigned)gate_word);
			}
			return 0;
		}
	}
	return 1;
}

// 在AT24CXX里面的指定地址开始读出指定个数的数据
// ReadAddr :开始读出的地址 对24c02为0~255
// pBuffer  :数据数组首地址
// NumToRead:要读出数据的个数
void AT24CXX_Read(uint16_t ReadAddr, uint8_t *pBuffer, uint16_t NumToRead)
{
	while (NumToRead)
	{
		*pBuffer++ = AT24CXX_ReadOneByte(ReadAddr++);
		NumToRead--;
	}
}

// 在AT24CXX里面的指定地址开始写入指定个数的数据
// WriteAddr :开始写入的地址 对24c02为0~255
// pBuffer   :数据数组首地址
// NumToWrite:要写入数据的个数
void AT24CXX_Write(uint16_t WriteAddr, uint8_t *pBuffer, uint16_t NumToWrite)
{
	uint16_t written = 0;

	while (NumToWrite--)
	{
		AT24CXX_WriteOneByte(WriteAddr, *pBuffer);
		WriteAddr++;
		pBuffer++;

		/*
		 * 长写（BLE 一次 128 字节 ≈ 1.28s、OTA 标志 41 字节 ≈ 410ms）期间主循环停摆，
		 * 期间到达的下行帧若只留在 DMA 缓冲里会被下一帧覆盖。这里每 8 字节
		 * 把已收到的帧搬进环形缓冲，把停摆切成长度 ≤80ms 的片段。
		 */
		if ((++written & 0x07u) == 0u)
			UartRx_PumpFromDma(0); /* verbose=0：只搬运不打印，避免写循环里被 printf 拖慢 */
	}
}
```

## §2 闸门参数层（dcmotor_param）

`dcmotor_param.h`：

```c
#ifndef __DCMOTOR_PARAM_H
#define __DCMOTOR_PARAM_H

#include "stm32f10x.h"

/* ---- EEPROM 地址（字节地址，对应原工程 reg.h 地址 map） ---- */
#define EEPROM_ADDR_MOTOR_WARN_CURR   0x0004u   /* u16, mA */
#define EEPROM_ADDR_GATE_MAXHEIGHT    0x0006u   /* u16, mm */
#define EEPROM_ADDR_GATE_MAXTURNS     0x0008u   /* u16, 圈数 */

/* ---- 默认值 ---- */
#define DCMOTOR_DEFAULT_WARN_CURR     3500u     /* 3.5A */
#define DCMOTOR_DEFAULT_MAXHEIGHT     1000u     /* 1000mm */
#define DCMOTOR_DEFAULT_MAXTURNS      80u

/* ---- RAM 变量（由 DCMOTOR 模块直接 extern 引用或通过 Get 接口读） ---- */
extern uint16_t g_gate_maxheight;
extern uint16_t g_motor_warning_curr_upper;
extern uint16_t g_gate_maxturns;

/* 空白 EEPROM 播种闸门参数默认值（被 eeprom_drv 的 AT24CXX_Check 在真空白时调用） */
void paraToEeprom(void);

/* 上电：从 EEPROM 读入 RAM，无效值（0 / 0xFFFF）用默认值并写回播种 */
void dcmotor_param_load(void);

/* 写回单个字段（远程 config 或本地设置触发） */
void dcmotor_param_save_warn_curr(uint16_t mA);
void dcmotor_param_save_maxheight(uint16_t mm);
void dcmotor_param_save_maxturns(uint16_t turns);

#endif
```

`dcmotor_param.c` 完整源码：

```c
#include "dcmotor_param.h"
#include "eeprom_drv.h"      /* AT24CXX_ReadLenByte / AT24CXX_WriteWord */
#include <stdio.h>

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
}

/* 上电：从 EEPROM 读入 RAM，无效值（0 / 0xFFFF）用默认值并写回播种 */
void dcmotor_param_load(void)
{
    uint16_t stored;

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

    /* ---- 闸门最大圈数 ---- */
    stored = (uint16_t)AT24CXX_ReadLenByte(EEPROM_ADDR_GATE_MAXTURNS, 2);
    if (stored == 0u || stored == 0xFFFFu)
    {
        g_gate_maxturns = DCMOTOR_DEFAULT_MAXTURNS;
        AT24CXX_WriteWord(EEPROM_ADDR_GATE_MAXTURNS, g_gate_maxturns);
    }
    else
    {
        g_gate_maxturns = stored;
    }

    printf("gate param load: maxheight=%u maxturns=%u warn_curr=%u\r\n",
           (unsigned)g_gate_maxheight, (unsigned)g_gate_maxturns,
           (unsigned)g_motor_warning_curr_upper);
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
```

### 与 DCMOTOR 模块的接线

zxl 工程的模块边界已经划分好，**dcmotor.c 本身不需要改**：堵转判定在 adc 模块（阈值走 `ADC_SetStallThresholdMa`），全行程脉冲在 dcmotor 模块（走 `DCMOTOR_SetMaxTurns`，0 = 自校准）。三个全局变量的真正接线点在 **main.c**，在 `dcmotor_param_load()` 之后把值推给各消费模块：

| EEPROM 变量 | 消费端 | 接线方式 |
|---|---|---|
| `g_gate_maxturns` | dcmotor 模块（全行程脉冲） | `DCMOTOR_SetMaxTurns(g_gate_maxturns)`；0 = 仍走自校准，非 0 = 直接用 |
| `g_motor_warning_curr_upper` | adc 模块（堵转阈值） | `ADC_SetStallThresholdMa(g_motor_warning_curr_upper)`；须在 `ADC1_Init()` 之后调，覆盖默认 3500 |
| `g_gate_maxheight` | （暂未消费） | zxl 用编码器脉冲百分比算开度，不用高度 mm；该字段先存着，等需要显示/上报实际高度时再接 |

main.c 接线示例（在 `DCMOTOR_Init()` 之后，把原来的 `DCMOTOR_SetMaxTurns(0u)` 那行替换为）：

```c
#include "dcmotor_param.h"
#include "eeprom_drv.h"
#include "adc.h"

AT24CXX_Init();
AT24CXX_Check();             /* 真空白 EEPROM 时播种闸门默认值 */
dcmotor_param_load();        /* EEPROM → g_gate_maxturns / g_motor_warning_curr_upper */

DCMOTOR_SetMaxTurns(g_gate_maxturns);                 /* 替换原来的 DCMOTOR_SetMaxTurns(0u) */
ADC_SetStallThresholdMa(g_motor_warning_curr_upper);  /* 堵转阈值推给 adc */
```

## §3 MQTT 凭据层（gprs_config_store）

`gprs_config_store.h`：

```c
#ifndef __GPRS_CONFIG_STORE_H
#define __GPRS_CONFIG_STORE_H

#include "gprs_config.h"

/* EEPROM 字节地址（各 32 字节槽） */
#define MQTT_PRODUCTKEY_BYTE_ADDR    64u
#define MQTT_DEVICEKEY_BYTE_ADDR     96u
#define MQTT_DEVICESECRET_BYTE_ADDR  128u
#define MQTT_MQTTADDR_BYTE_ADDR      160u
#define MQTT_KEY_EEPROM_LEN          32u
#define MQTT_KEY_STR_LEN             (MQTT_KEY_EEPROM_LEN + 1u)

/* 从 EEPROM 读凭据到指定缓冲；返回 1=槽有效，0=空白 */
uint8_t gprs_store_read_pk(char *buf);
uint8_t gprs_store_read_dk(char *buf);
uint8_t gprs_store_read_secret(char *buf);
uint8_t gprs_store_read_addr(char *buf);

/* 上电统一调：依次读四个槽，无效槽回退到 gprs_config.h 里的默认宏 */
void gprs_config_load(void);

/* 写入：只写空白槽（首字节非 0/0xFF 则跳过，保护真实凭据不被覆盖） */
void gprs_store_write_str(uint16_t addr, const char *s);

/* RAM 中的凭据全局变量（在 gprs_config_store.c 定义，消费端直接引用） */
extern char g_mqtt_productKey[MQTT_KEY_STR_LEN];
extern char g_mqtt_deviceKey[MQTT_KEY_STR_LEN];
extern char g_mqtt_deviceSecret[MQTT_KEY_STR_LEN];
extern char g_mqtt_addr[MQTT_KEY_STR_LEN];

#endif
```

`gprs_config_store.c` 完整源码：

```c
#include "gprs_config_store.h"   /* MQTT_*_BYTE_ADDR / MQTT_KEY_STR_LEN 等 */
#include "eeprom_drv.h"          /* AT24CXX_Read / AT24CXX_Write / AT24CXX_ReadOneByte */
#include "gprs_config.h"         /* GPRS_* 编译期 fallback 宏 */
#include <string.h>
#include <stdio.h>

/* ---- RAM 凭据全局变量定义（对应 gprs_config_store.h 里的 extern 声明） ---- */
char g_mqtt_productKey[MQTT_KEY_STR_LEN];
char g_mqtt_deviceKey[MQTT_KEY_STR_LEN];
char g_mqtt_deviceSecret[MQTT_KEY_STR_LEN];
char g_mqtt_addr[MQTT_KEY_STR_LEN];

/* 读单个槽到 buf（buf 至少 MQTT_KEY_STR_LEN 字节）；返回 1=槽有效，0=空白 */
static uint8_t gprs_store_read_slot(uint16_t addr, char *buf)
{
    AT24CXX_Read(addr, (uint8_t *)buf, MQTT_KEY_EEPROM_LEN);
    buf[MQTT_KEY_EEPROM_LEN] = '\0';
    return (buf[0] != '\0' && (uint8_t)buf[0] != 0xFFu) ? 1u : 0u;
}

uint8_t gprs_store_read_pk(char *buf)     { return gprs_store_read_slot(MQTT_PRODUCTKEY_BYTE_ADDR, buf); }
uint8_t gprs_store_read_dk(char *buf)     { return gprs_store_read_slot(MQTT_DEVICEKEY_BYTE_ADDR, buf); }
uint8_t gprs_store_read_secret(char *buf) { return gprs_store_read_slot(MQTT_DEVICESECRET_BYTE_ADDR, buf); }
uint8_t gprs_store_read_addr(char *buf)   { return gprs_store_read_slot(MQTT_MQTTADDR_BYTE_ADDR, buf); }

/* 上电：EEPROM 有效则用 EEPROM，否则用 gprs_config.h 里的编译期宏（不写回 EEPROM） */
void gprs_config_load(void)
{
    char tmp[MQTT_KEY_STR_LEN];

    if (gprs_store_read_pk(tmp))
        memcpy(g_mqtt_productKey, tmp, MQTT_KEY_STR_LEN);
    else
        strncpy(g_mqtt_productKey, GPRS_PRODUCT_KEY, MQTT_KEY_EEPROM_LEN);

    if (gprs_store_read_dk(tmp))
        memcpy(g_mqtt_deviceKey, tmp, MQTT_KEY_STR_LEN);
    else
        strncpy(g_mqtt_deviceKey, GPRS_DEVICE_KEY, MQTT_KEY_EEPROM_LEN);

    if (gprs_store_read_secret(tmp))
        memcpy(g_mqtt_deviceSecret, tmp, MQTT_KEY_STR_LEN);
    else
        strncpy(g_mqtt_deviceSecret, GPRS_DEVICE_SECRET, MQTT_KEY_EEPROM_LEN);

    if (gprs_store_read_addr(tmp))
        memcpy(g_mqtt_addr, tmp, MQTT_KEY_STR_LEN);
    else
        strncpy(g_mqtt_addr, GPRS_MQTT_ADDR, MQTT_KEY_EEPROM_LEN);

    /* 统一强制结尾，防止 strncpy 在源串长度 ≥ 32 时未补 '\0' */
    g_mqtt_productKey[MQTT_KEY_EEPROM_LEN]   = '\0';
    g_mqtt_deviceKey[MQTT_KEY_EEPROM_LEN]    = '\0';
    g_mqtt_deviceSecret[MQTT_KEY_EEPROM_LEN] = '\0';
    g_mqtt_addr[MQTT_KEY_EEPROM_LEN]         = '\0';

    printf("mqtt load: pk=%s dk=%s addr=%s\r\n",
           g_mqtt_productKey, g_mqtt_deviceKey, g_mqtt_addr);
}

/* 写凭据：只写空白槽（首字节非 0 且非 0xFF 则跳过，保护真实凭据不被默认值覆盖） */
void gprs_store_write_str(uint16_t addr, const char *s)
{
    uint8_t first = AT24CXX_ReadOneByte(addr);
    if (first != 0x00u && first != 0xFFu)
        return;   /* 槽已有值，不覆盖 */

    {
        uint8_t buf[MQTT_KEY_EEPROM_LEN];
        uint16_t i;
        uint16_t len = (uint16_t)strlen(s);
        if (len > MQTT_KEY_EEPROM_LEN)
            len = MQTT_KEY_EEPROM_LEN;
        for (i = 0; i < len; i++)
            buf[i] = (uint8_t)s[i];
        for (; i < MQTT_KEY_EEPROM_LEN; i++)
            buf[i] = 0u;
        AT24CXX_Write(addr, buf, MQTT_KEY_EEPROM_LEN);
    }
}
```

> 前置依赖：`gprs_config.h` 当前只有 `GPRS_PRODUCT_KEY` / `GPRS_DEVICE_KEY` 两个宏，需再补两个编译期 fallback 宏（**占位值，部署时请替换为你自己的真实凭据**；仅当 EEPROM 对应槽为空白时才会用到，不会写入 EEPROM）：
> ```c
> #define GPRS_DEVICE_SECRET  "replace_with_your_device_secret"
> #define GPRS_MQTT_ADDR      "replace_with_your_broker_host:port"
> ```

### 与 GPRS 阶段四的接线

远程 `config` 命令要能改凭据（虽然实际很少改），`gprs_json.c` 的 `config` service handler 里调 `gprs_store_write_str()` 即可。`getConfig` 命令回当前值时，优先用 gprs_config_store 层的读结果。

消费端改宏为全局变量：`gprs_json.c` / `property_report.c` 里当前直接引用 `GPRS_PRODUCT_KEY` / `GPRS_DEVICE_KEY` 宏的地方，改为引用 `g_mqtt_productKey` / `g_mqtt_deviceKey`（在这些文件顶部 `#include "gprs_config_store.h"` 即可拿到 extern 声明），这样 EEPROM 中下发的真实凭据才能生效。

## §4 OTA Info 层（ota_store，占位）

阶段四才用。先定义 `ota_store_load() / ota_store_save()` 两个接口，实现留到那时。地址 `OTA_BYTE_ADDR = 200`，结构体定义从原工程搬。

## §5 main 初始化顺序

```c
int main(void)
{
    /* ... 已有 init ... */
    TIM3_10ms_Init();             /* g_sys_ms 开始走（TIMER/timer.c） */
    GPRS_PORT_Init(115200);

    /* ---- 看门狗（见《看门狗重构操作建议》）---- */
    WDG_Init();                   /* 必须在 EEPROM 长写与 LCD 上电等待之前启动 */

    /* ---- EEPROM 层 ---- */
    AT24CXX_Init();               /* I²C 初始化 */
    AT24CXX_Check();              /* 自检 + 闸门参数播种（仅真空白时） */
    dcmotor_param_load();         /* 闸门参数 → RAM */
    gprs_config_load();           /* MQTT 凭据 → RAM（EEPROM 有效则用，否则用编译期宏） */

    /* ---- DS1302（阶段四）---- */
    GPRS_RTC_Init();

    /* ---- GPRS 模块 ---- */
    GPRS_4G_Init();

    /* ... */
}
```

关键点：**AT24CXX_Check() 必须在第一次写入之前**——它的播种逻辑内部会调 `paraToEeprom`，如果先调了 `dcmotor_param_load()` 把参数读完了再播种，会导致 `paraToEeprom` 覆盖 load 读出来的值。

另外，`WDG_Init()` 必须先于 `AT24CXX_Init()` / `AT24CXX_Check()`：EEPROM 单字节写要等 10ms，`AT24CXX_Check()` 播种会连续写多个字，看门狗需要覆盖这段阻塞；每字节写完后的 `WDG_Feed()` 见《看门狗重构操作建议》§3.2。

## §6 删掉原工程的哪些东西

原工程 `reg.h` / `reg.c` 里这些在新工程里**不再需要**：

| 原工程 | 替换为 |
|---|---|
| `Register armReg` 全局大结构体 | `g_gate_maxheight / g_motor_warning_curr_upper / g_gate_maxturns` 分散在各模块 |
| `eeprom_buf[10]` 中间缓冲 | `dcmotor_param_load()` 里直接读 |
| `Eeprom_Wpara()` 按地址 switch 写 | `dcmotor_param_save_xxx()` 三个专用函数 |
| `setReg / getReg / setRegMul` | 直接用各模块的 `save/load` 函数 |

原工程的 `24cxx.c` 里这些**保留**：
- I²C 所有 GPIO 操作
- `AT24CXX_Check()` 的播种判定
- 每 8 字节的 `UartRx_PumpFromDma(0)`（长写期间搬帧）
- `AT24CXX_WriteOneByte` 里的 `delay_ms(10)`（EEPROM 写周期等待）+ `WDG_Feed()`（写周期后喂狗，窗口见《看门狗重构操作建议》）

## §7 验证清单

1. **上电自检**：烧入后首次冷启动，空白 EEPROM 串口打印 `24C02 blank: seed defaults via paraToEeprom`；若标记丢失但数据仍在则打印 `24C02 marker lost but data valid: keep params ...`。
2. **闸门参数**：设堵转电流 4000mA，重启后读回确认；闸门上限 1200mm，重启后确认。
3. **MQTT 凭据空白槽保护**：第一次烧入后 EEPROM 空白 → `gprs_config_load()` 走编译期宏 fallback；如果有人手动在 EEPROM 里写了真实 DK，重启后再调 `gprs_store_write_str()` 写默认值应**跳过不覆盖**。
4. **长写期间不丢帧**：用 USB 口模拟模块在 EEPROM 长写期间发下行命令，确认命令不丢（`AT24CXX_Write` 里每 8 字节 `UartRx_PumpFromDma(0)` 起作用）。
5. **看门狗不误触发**：连续写四个 32B 凭据槽（约 1.28s），确认不复位、串口不出现 `reset cause: IWDG`；若复位，检查 `AT24CXX_WriteOneByte()` 末尾是否漏了 `WDG_Feed()`。
