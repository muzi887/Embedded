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
| `HARDWARE/EEPROM/eeprom_drv.h` + `.c` | I²C + AT24C02 字节级/字级读写 | **直接从原工程 `HARDWARE/24CXX/` 拷贝**，改名（24cxx → eeprom_drv） |
| `HARDWARE/GPRS/gprs_config_store.h` + `.c` | MQTT 凭据读写（PK/DK/Secret/Addr） | 新建，内部调 eeprom_drv |
| `HARDWARE/DCMOTOR/dcmotor_param.h` + `.c` | 闸门参数读写（maxheight / warning_curr / maxturns） | 新建，内部调 eeprom_drv |
| `HARDWARE/GPRS/ota_store.h` + `.c` | OTA Info 读写（阶段四才用，先占位） | 新建 |

> **注意**：zxl 工程里原来有一份编译期参数宏（`GPRS_PRODUCT_KEY` 等在 `gprs_config.h`），EEPROM 层接管后它们要变成**默认值 fallback**——EEPROM 里有效就用 EEPROM，否则用编译期宏。

## §1 eeprom_drv（从原工程拷贝，接口完全一致）

`eeprom_drv.h`：

```c
#ifndef __EEPROM_DRV_H
#define __EEPROM_DRV_H

#include "stm32f10x.h"

/* 芯片型号：AT24C02（256 字节） */
#define EE_TYPE     AT24C02
#define EE_ADDR     250u            /* Marker 字节地址 */

/* ---- 字节级读写 ---- */
uint8_t  AT24CXX_ReadOneByte(uint16_t ReadAddr);
void     AT24CXX_WriteOneByte(uint16_t WriteAddr, uint8_t DataToWrite);

/* ---- 字级（u16 little endian）读写 ---- */
void     AT24CXX_WriteWord(uint16_t WriteAddr, uint16_t Data);
uint32_t AT24CXX_ReadLenByte(uint16_t ReadAddr, uint8_t Len);  /* 2 或 4 */
void     AT24CXX_WriteLenByte(uint16_t WriteAddr, uint32_t DataToWrite, uint8_t Len);

/* ---- 定长缓冲 ---- */
void     AT24CXX_Read(uint16_t ReadAddr, uint8_t *pBuffer, uint16_t NumToRead);
void     AT24CXX_Write(uint16_t WriteAddr, uint8_t *pBuffer, uint16_t NumToWrite);

/* ---- 批量读取闸门参数（10 × u16） ---- */
void     AT24CXX_Readdatas(uint16_t ReadAddr, uint16_t *Readbuf, uint8_t Len);

/* ---- 上电自检 + 播种 ---- */
uint8_t  AT24CXX_Check(void);

/* ---- 初始化（IIC_Init） ---- */
void     AT24CXX_Init(void);

#endif
```

`eeprom_drv.c` 的实现**一字不动**拷自原工程 `24cxx.c`，包括：
- `IIC_Start/Stop/Send_Byte/Read_Byte/Wait_Ack`（来自 delay.h 里的 GPIO 模拟 I²C）
- `AT24CXX_Check()` 里的"marker 丢失但数据有效 → 不播种"逻辑（原 reg.c 里的 `paraToEeprom` 只有在真空白时才调）
- `AT24CXX_WriteOneByte` 里的 `delay_ms(10) + IWDG_Feed()`
- `AT24CXX_Write()` 里每 8 字节 `UartRx_PumpFromDma(0)` 搬帧（长写期间不让串口停摆）

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

/* 上电：从 EEPROM 读入 RAM，无效则用默认值；对旧编码自动迁移 */
void dcmotor_param_load(void);

/* 写回单个字段（远程 config 或本地设置触发） */
void dcmotor_param_save_warn_curr(uint16_t mA);
void dcmotor_param_save_maxheight(uint16_t mm);
void dcmotor_param_save_maxturns(uint16_t turns);

#endif
```

`dcmotor_param.c`（实现要点）：

```
dcmotor_param_load():
    读 EEPROM → AT24CXX_ReadLenByte 依次读三个字段
    motor_warning_curr_upper:
      ==0 或 ==0xFFFF → 用默认值 3500
      <1000 → 旧编码（0.1A × 100 迁移到 mA），同时写回 EEPROM 修正
      否则直接用
    gate_maxheight / gate_maxturns:
      ==0 或 ==0xFFFF → 用默认值，同时写回 EEPROM 播种
      否则直接用

dcmotor_param_save_xxx(val):
    更新 RAM 变量
    AT24CXX_WriteWord(对应地址, val)
    （将来如果加"脏标记 + 延时落盘"机制，这里改为只置脏）
```

### 与 DCMOTOR 模块的接线

重构前 DCMOTOR 是硬编码 80 圈，接线后改为：

```c
/* dcmotor.c 内部变量替换 */
extern uint16_t g_gate_maxturns;    /* 原来的 80，现在从 EEPROM 读 */
extern uint16_t g_motor_warning_curr_upper;  /* 堵转阈值 */
extern uint16_t g_gate_maxheight;   /* 上限高度 */
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

#endif
```

`gprs_config_store.c`（实现要点）：

```
gprs_store_read_xxx(buf):
    AT24CXX_Read(addr, buf, 32)
    buf[32] = '\0'
    return (buf[0] != '\0' && buf[0] != 0xFFu) ? 1u : 0u

gprs_config_load():
    四个槽依次读
    无效 → 用 gprs_config.h 里的 GPRS_PRODUCT_KEY / GPRS_DEVICE_KEY
           （编译期 fallback，不是 EEPROM 播种——避免首次上电就把占位符写进 EEPROM）

gprs_store_write_str(addr, s):
    first = AT24CXX_ReadOneByte(addr)
    if (first != 0x00 && first != 0xFF)  return;  /* 保护已有凭据 */
    填 32 字节（多余填 0，不足截断）
    AT24CXX_Write(addr, buf, 32)
```

### 与 GPRS 阶段四的接线

远程 `config` 命令要能改凭据（虽然实际很少改），`gprs_json.c` 的 `config` service handler 里调 `gprs_store_write_str()` 即可。`getConfig` 命令回当前值时，优先用 gprs_config_store 层的读结果。

## §4 OTA Info 层（ota_store，占位）

阶段四才用。先定义 `ota_store_load() / ota_store_save()` 两个接口，实现留到那时。地址 `OTA_BYTE_ADDR = 200`，结构体定义从原工程搬。

## §5 main 初始化顺序

```c
int main(void)
{
    /* ... 已有 init ... */
    TIM4_Init();                  /* g_sys_ms 开始走 */
    GPRS_PORT_Init(115200);

    /* ---- EEPROM 层 ---- */
    AT24CXX_Init();               /* I²C 初始化 */
    AT24CXX_Check();              /* 自检 + 闸门参数播种（仅真空白时） */
    dcmotor_param_load();         /* 闸门参数 → RAM（含旧编码迁移） */
    gprs_config_load();           /* MQTT 凭据 → RAM（EEPROM 有效则用，否则用编译期宏） */

    /* ---- DS1302（阶段四）---- */
    GPRS_RTC_Init();

    /* ---- GPRS 模块 ---- */
    GPRS_4G_Init();

    /* ... */
}
```

关键点：**AT24CXX_Check() 必须在第一次写入之前**——它的播种逻辑内部会调 `paraToEeprom`，如果先调了 `dcmotor_param_load()` 把参数读完了再播种，会导致 `paraToEeprom` 覆盖 load 读出来的值。

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
- `AT24CXX_WriteOneByte` 里的 `delay_ms(10) + IWDG_Feed()`

## §7 验证清单

1. **上电自检**：烧入后首次冷启动，串口打印 `24C02 marker lost → 播种` 或 `24C02 marker lost but data valid: keep params`（取决于 EEPROM 是否已空）。
2. **闸门参数**：设堵转电流 4000mA，重启后读回确认；闸门上限 1200mm，重启后确认。
3. **旧编码迁移**：若 EEPROM 里 `motor_warning_curr_upper = 35`（原 3.5A 编码），重启后 RAM 应变成 3500，EEPROM 被写回 3500。
4. **MQTT 凭据空白槽保护**：第一次烧入后 EEPROM 空白 → `gprs_config_load()` 走编译期宏 fallback；如果有人手动在 EEPROM 里写了真实 DK，重启后再调 `gprs_store_write_str()` 写默认值应**跳过不覆盖**。
5. **长写期间不丢帧**：用 USB 口模拟模块在 EEPROM 长写期间发下行命令，确认命令不丢（`AT24CXX_Write` 里每 8 字节 `UartRx_PumpFromDma(0)` 起作用）。
