# REG 重构（迁移顺序阶段 6：参数登记处）

> 来源：`qibiji_boot/HARDWARE/REG/reg.c/.h` + `qibiji_boot/HARDWARE/BOOT/boot.c/.h` 中的
> OTA 状态块族（`OTA_InfoCB`、偏移宏、`OTA_ADDR/OTA_RETRY_*`、布局断言）与五个 EEPROM
> 标志函数（原名 `OtaWriteFlag / OtaClearInstalledVersion / OtaCloseUpdateGate / OtaRetryCount(Set)`，
> 迁入后统一改名 `Ota_WriteFlag / Ota_ClearInstalledVersion / Ota_CloseUpdateGate / Ota_RetryCount(Set)`，见 §3 改名表）。
> REG 是 **24C02 地址布局的属主**：布局总表与 OTA 状态块归 reg.h。
> 逻辑等价 + 一处契约修订（见 §1.1，为阶段 5《EEPROM硬件访问层重构.md》预先约定的联动）。
> 依赖：只依赖已入库的 24CXX 与现有 usart1 发送路径；验证 main 替换阶段 5 的 EEPROM main。

## 1. 本部分产出清单

| 文件 | 动作 | 说明 |
|---|---|---|
| `HARDWARE/REG/reg.h` | 重写 | 迁入 OTA_InfoCB 族与 EEPROM 域断言（见 §2） |
| `HARDWARE/REG/reg.c` | 重写 | `Reg_ReadOtaInfo` 三态分派重写；五个标志函数从 boot.c 迁入；删反向依赖 include（见 §3） |
| `USER/main.c` | 替换 | 本部分验证代码：装载 + 分字段写读回环（见 §4） |
| Keil 工程 | 手动加 | HARDWARE 组确认 `reg.c` 在列表（首次加入）；Include Paths 加 `..\HARDWARE\REG`（见 §5） |

### 1.1 契约修订（不是缺陷修复，是阶段 5 预先约定的联动）

**自检分派重写**：原 `reg.c:48` 的 `if(AT24CXX_Check())` 按「非 0 即失败」判断——阶段 5 把
Check 返回值从 0/1 扩为 0/1/2（`FRESH=2` 表示新芯片已补标志），旧写法会把新芯片误判成故障。
新代码按三态分派，且**播种动作 `Reg_SeedDefaults()` 从 24CXX 层移回 REG 层调用**
（阶段 5 已把播种从 `AT24CXX_Check()` 内部摘除，消除 24CXX→REG 反向依赖）。

**LCD 相关内容不进本阶段**（LCD 尚未入库）：OTA_ERR_* 错误码仍归 `Display.h`、
`g_ota_lcd_err/hold` 登记变量仍归 `boot.c`（与原工程归属一致），待 LCD/BOOT 阶段各自落位；
本阶段 reg.c 的故障分支只做 RAM 默认值 + printf，不登记 LCD 变量。

## 2. reg.h（重写，全文）

```c
#ifndef __REG_H
#define __REG_H

#include "sys.h"        /* u8/u16/u32 基础类型 */
#include <stddef.h>     /* offsetof：布局断言取字段实际偏移 */
#include "24cxx.h"      /* EE_ADDR（EEPROM 域断言用）与 AT24CXX_* 读写函数 */

/* ---- 24C02 地址布局总表（REG 是布局属主，全工程只允许从这里取地址） ----
   0~63     APP 参数区：A1 应用自己的参数，B 区不使用
   64~95    productKey 槽位（32 字节）
   96~127   deviceKey 槽位（32 字节）
   128~199  MQTT 参数预留段（后续扩展用）
   200~243  OTA 状态块 OTA_InfoCB（44 字节）
   244      自愈重搬失败计数（1 字节）
   245~249  空闲
   250      0x55 初始化标志（AT24CXX_Check 维护，REG 只消费其结论） */

/* ---- MQTT 身份参数：24C02 布局与 A1 应用 reg.h 完全一致 ----
   平台会校验「消息体 productKey/deviceKey == 主题里的 productKey/deviceKey」，
   不一致直接判失败并且不回包，所以 B 区发出的 putVersion/getFirmware/otaError
   必须带 A1 应用同一套身份，否则现场表现是"putVersion 发出后一直 request timeout"。 */
#define MQTT_PRODUCTKEY_BYTE_ADDR   64                          /* productKey 槽位起始地址 */
#define MQTT_DEVICEKEY_BYTE_ADDR    96                          /* deviceKey 槽位起始地址 */
#define MQTT_KEY_EEPROM_LEN         32                          /* 每项槽位字节数 */
#define MQTT_KEY_STR_LEN            (MQTT_KEY_EEPROM_LEN + 1)   /* 32 字符 + '\0' */
#define MQTT_PRODUCTKEY_DEFAULT     "pk001"                     /* 槽位空/未写时的兜底值 */
#define MQTT_DEVICEKEY_DEFAULT      "dev001"

extern char g_mqtt_productKey[MQTT_KEY_STR_LEN];   /* 当前生效的 productKey（RAM 副本，空槽时为兜底值） */
extern char g_mqtt_deviceKey[MQTT_KEY_STR_LEN];    /* 当前生效的 deviceKey（同上） */

/* ---- OTA 状态块：整块存于 24C02 的 200~243，上电读入 RAM，关键变化分字段写回 ---- */
#define OTA_A1_FLAG   0xA1A1A1A1   /* OTA_flag 取此值：A1 区为当前有效应用 */
#define OTA_A2_FLAG   0xA2A2A2A2   /* OTA_flag 取此值：A2 区存有待安装固件 */
#define OTA_ADDR      200          /* OTA 状态块在 24C02 中的存放起始地址 */

typedef struct {
    u32 OTA_flag;      /* 生效分区标志：OTA_A1_FLAG / OTA_A2_FLAG */
    u8  OTA_ver[32];   /* 版本串；全 0 = 本机没有可用的已安装版本（版本闸以此判定） */
    u32 OTA_size;      /* A2 中待安装固件的总字节数；0 = 无固件 */
    u8  UPDATE_flag;   /* 升级闸：1 = 上电进入联网 OTA 流程；0 = 上电直接跳 A1。
                          写入端：① 出厂默认 Reg_SeedDefaults() 置 1（APP 参数协议也可改）；
                          ② 安装/自愈成功后由 Ota_CloseUpdateGate() 自动置 0，
                          再次升级需由平台经 A1 应用把它置回 1。
                          24C02 自检失败时 Reg_ReadOtaInfo 显式置 0（状态落不了盘，做不了 OTA）。 */
} OTA_InfoCB;
#define OTA_INFOCB_SIZE sizeof(OTA_InfoCB)   /* 状态块总字节数 = 44（含对齐填充），整块读写时使用 */

/* 状态块各字段在 24C02 中的字节偏移：分字段读写（Ota_WriteFlag 等）按此寻址 */
#define OTA_OFF_FLAG    0    /* u32 OTA_flag */
#define OTA_OFF_VER     4    /* u8  OTA_ver[32] */
#define OTA_OFF_SIZE    36   /* u32 OTA_size */
#define OTA_OFF_UPDATE  40   /* u8  UPDATE_flag */

/* 布局断言：结构体任一字段的实际偏移与上面宏不符即编译失败（负数组长度技巧），
   防止日后改结构体导致分字段读写地址错位 */
typedef char ota_info_layout_check[
    (offsetof(OTA_InfoCB, OTA_flag)    == OTA_OFF_FLAG   &&
     offsetof(OTA_InfoCB, OTA_ver)     == OTA_OFF_VER    &&
     offsetof(OTA_InfoCB, OTA_size)    == OTA_OFF_SIZE   &&
     offsetof(OTA_InfoCB, UPDATE_flag) == OTA_OFF_UPDATE) ? 1 : -1];

/* 自愈重搬失败计数：1 字节 EEPROM，紧跟状态块（地址 244），落在 244~249 空闲段。
   状态块一旦扩大会覆盖到它，下面的断言在编译期拦截 */
#define OTA_RETRY_ADDR  (OTA_ADDR + 44)   /* 计数字节地址 = 状态块末尾 + 1 */
#define OTA_RETRY_MAX   3                 /* 连续失败上限：达到后不再擦写 A1（保护 Flash 寿命） */

/* EEPROM 域断言：计数地址必须不越过 0x55 标志位。
   这是原 boot.h 分区断言的第七条，随 EEPROM 布局归 REG；
   其余六条分区断言已在阶段 4 归 flash_layout.h */
typedef char reg_layout_check[
    ((OTA_RETRY_ADDR + 1) <= EE_ADDR) ? 1 : -1];

/* OTA 状态块的 RAM 副本：上电由 Reg_ReadOtaInfo 从 24C02 读入（新芯片则播种生成） */
extern OTA_InfoCB OTA_Info;

/* ---- 对外函数 ---- */
void Reg_ReadOtaInfo(void);      /* 上电入口：三态自检分派（播种 / 故障 / 正常读取），填 OTA_Info */
void Reg_ReadMqttKeys(void);     /* 从 24C02 读 productKey/deviceKey，空槽保持兜底值 */
void Reg_SeedDefaults(void);     /* 播种：把 OTA 状态块默认值写进 24C02（原名 paraToEeprom） */

u8   Ota_WriteFlag(u32 flag);             /* 只写 OTA_flag（4 字节）：传入 OTA_A1_FLAG / OTA_A2_FLAG */
void Ota_ClearInstalledVersion(void);     /* 清空 OTA_ver：版本闸失效，平台将重新下发固件 */
void Ota_CloseUpdateGate(void);           /* 写 UPDATE_flag=0：关闭升级闸，上电不再进 OTA */
u8   Ota_RetryCount(void);                /* 读自愈重搬失败计数（1 字节） */
void Ota_RetryCountSet(u8 v);             /* 写自愈重搬失败计数（1 字节） */

#endif
```

## 3. reg.c（重写，全文）

```c
/* REG 模块：24C02 参数登记处。
   职责：EEPROM 布局属主（地址总表见 reg.h）；上电把 OTA 状态块与 MQTT 身份
   从 24C02 装载进 RAM（新芯片先播种再装载）；提供状态块分字段读写供 BOOT/GPRS 使用。
   注意：本文件不再 include boot.h / display.h——OTA 状态块族归 reg.h 后
   BOOT/GPRS 的反向依赖消除，依赖方向变为 BOOT/GPRS → REG 单向；
   OTA_ERR_* 错误码与 LCD 登记变量维持原工程归属（Display.h / boot.c），REG 零引用。 */

#include "reg.h"
#include <stdio.h>      /* printf/sprintf/snprintf：调试输出与字符串组装 */
#include <string.h>     /* memset：状态块清零与版本串清空 */

/* ---- 全局定义（声明见 reg.h） ---- */
OTA_InfoCB OTA_Info;    /* 状态块 RAM 副本：上电装载，关键变化由分字段写函数同步落盘 */

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
   ERROR(1) = 写标志后仍读不回 0x55，硬件故障：状态块用 RAM 默认值，关闭联网 OTA。
   原 if(AT24CXX_Check()) 的「非 0 即失败」写法在新三态契约下会把 FRESH 误判成
   故障（新芯片永远进不了 OTA），所以本函数必须按三态分派。 */
void Reg_ReadOtaInfo(void)
{
    u8 chk;   /* 自检结论：AT24CXX_OK / AT24CXX_FRESH / AT24CXX_ERROR */

    memset(&OTA_Info, 0, OTA_INFOCB_SIZE);   /* 先整体清零：故障分支的默认值在此基础上填 */
    chk = AT24CXX_Check();                   /* 三态自检（阶段 5 契约） */

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
        sprintf((char *)OTA_Info.OTA_ver, "v0.0.0");   /* 首参转 char*：OTA_ver 是 u8 数组，消除 #167-D 符号性告警 */
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

/* ---- OTA 状态块分字段读写（原 boot.c 五个 static 函数迁入，static 放开并统一 Ota_ 前缀） ---- */

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
    memset(&OTA_Info.OTA_ver, 0, 32);   /* 全 0 = 版本闸失效（见 OTA_ver 字段注释） */
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
```

**本部分改名与清理**：

| 原名 | 处理 | 理由 |
|---|---|---|
| `AT24CXX_ReadOTAInfo` | 改名 `Reg_ReadOtaInfo` | 原名带 AT24CXX 前缀，实为 REG 的入口函数，且 AT24CXX 是另一个模块的名字，易误导 |
| `paraToEeprom` | 改名 `Reg_SeedDefaults` | 原名无模块前缀、看不出「播种默认值」语义（字面只是「参数到 EEPROM」）；统一 `Reg_` 前缀 |
| `MQTT_ReadKeysFromEeprom` | 改名 `Reg_ReadMqttKeys` | REG 模块的函数却带 MQTT 前缀（MQTT 是 GPRS 层概念），统一 `Reg_` 前缀 |
| `OtaWriteFlag / OtaClearInstalledVersion / OtaCloseUpdateGate / OtaRetryCount / OtaRetryCountSet` | 从 boot.c 迁入、static 放开，改名 `Ota_WriteFlag / Ota_ClearInstalledVersion / Ota_CloseUpdateGate / Ota_RetryCount / Ota_RetryCountSet` | 只操作 24C02，归 REG 才对位（《BOOT模块重构.md》附录 1 作废，由本部分接替）；OTA 动作统一 `Ota_` 带下划线（与《BOOT模块重构.md》改名规则一致） |
| `eepromLength` 宏 | 删除 | 全工程零引用（grep 证实） |
| `#include "boot.h"` | 删除 | OTA_InfoCB 族归 reg.h 后，REG→BOOT 反向依赖消除 |
| `#include "display.h"` | 删除 | 故障分支不再登记 LCD 变量；错误码维持原归属（Display.h），REG 不再引用 LCD 层 |
| `#include "delay.h"` | 删除 | 文件内无延时调用 |
| OTA_ERR_* 8 个错误码、`g_ota_lcd_err/hold` | **不迁入**（留原位） | LCD 尚未入库，维持原工程归属（Display.h / boot.c），LCD/BOOT 阶段各自落位 |
| 两处 `sprintf(OTA_Info.OTA_ver, ...)` | 首参加 `(char *)` 强制转换 | OTA_ver 是 u8 数组，sprintf 形参是 char*，armcc 报 #167-D（原工程同位置就有此告警）；符号性对纯 ASCII 版本串无影响，转换后行为零变化、告警消除 |

## 4. 本部分验证 main.c（替换阶段 5 的 main，全文）

验证目标：**装载链路（含播种分派）+ 分字段写读回环**。测试动过的字段全部**先存原值再恢复**，
不碰 MQTT 键槽位与 APP 参数区（键只读不写）。

```c
#include "stm32f10x.h"
#include "sys.h"
#include "delay.h"
#include "usart.h"
#include "led.h"
#include "reg.h"
#include "24cxx.h"   /* 直接回读 24C02：验证分字段写确实落盘（不经过 RAM 副本） */

static u32 blink_cnt = 0;                        /* 心跳计数 */
static u8  s_block_buf[OTA_INFOCB_SIZE];         /* 状态块整块回读缓冲：验证落盘内容 */

int main(void)
{
    u32 flag_save;    /* 测试前 OTA_flag 原值（恢复用） */
    u8  update_save;  /* 测试前 UPDATE_flag 原值（恢复用） */
    u32 flag_back;    /* 回读出的 OTA_flag */
    u8  retry_back;   /* 回读出的自愈计数 */
    u8  gate_back;    /* 回读出的 UPDATE_flag */

    delay_init();
    NVIC_Configuration();
    uart1_init(115200);
    LED_Init();
    LED_CLOSE();
    AT24CXX_Init();     /* I²C 引脚初始化（PB10/PB11）：Reg_ReadOtaInfo 内部只调 Check 不做引脚准备，漏了这行总线读回来全是 0 */

    printf("reg test start\r\n");

    /* ① 上电装载：老芯片走 OK 分支打印状态块与 MQTT 键；
       新芯片会先打 "set default para"（播种）再读出——两种打印都算通过 */
    Reg_ReadOtaInfo();

    flag_save  = OTA_Info.OTA_flag;     /* 存测试前原值 */
    update_save = OTA_Info.UPDATE_flag;

    /* ② 标志分字段写读：写 A2 → 整块回读 24C02 校验 → 恢复原值 */
    if (Ota_WriteFlag(OTA_A2_FLAG) == AT24CXX_OK)
    {
        AT24CXX_Read(OTA_ADDR, s_block_buf, OTA_INFOCB_SIZE);   /* 从 24C02 整块读回（非 RAM 副本） */
        flag_back = (u32)s_block_buf[0]
                  | ((u32)s_block_buf[1] << 8)
                  | ((u32)s_block_buf[2] << 16)
                  | ((u32)s_block_buf[3] << 24);                /* 按小端拼回 u32 */
        printf("flag write-back: %08X %s\r\n", (unsigned int)flag_back,
               (flag_back == OTA_A2_FLAG) ? "PASS" : "FAIL");
        Ota_WriteFlag(flag_save);   /* 恢复测试前标志（可能是 A1，也可能本就挂着 A2 待安装） */
    }
    else
    {
        printf("flag write FAIL\r\n");
    }

    /* ③ 自愈计数往返：写 2 → 读回应为 2 → 清零复原 */
    Ota_RetryCountSet(2);
    retry_back = Ota_RetryCount();
    printf("retry roundtrip: %u %s\r\n", (unsigned int)retry_back,
           (retry_back == 2) ? "PASS" : "FAIL");
    Ota_RetryCountSet(0);

    /* ④ 升级闸往返：置 0 → 回读 24C02 校验 → 恢复原值（不用 Reg_SeedDefaults 整块恢复，
       避免覆盖真实设备的版本记录与 OTA_size） */
    Ota_CloseUpdateGate();
    AT24CXX_Read(OTA_ADDR + OTA_OFF_UPDATE, &gate_back, 1);
    printf("gate write-back: %u %s\r\n", (unsigned int)gate_back,
           (gate_back == 0) ? "PASS" : "FAIL");
    OTA_Info.UPDATE_flag = update_save;   /* RAM 副本同步恢复 */
    AT24CXX_Write(OTA_ADDR + OTA_OFF_UPDATE, (u8 *)&OTA_Info.UPDATE_flag, 1);

    printf("reg test done\r\n");

    while (1)
    {
        delay_ms(500);          /* 半秒节拍 */
        LED_ERR = !LED_ERR;     /* 位带翻转 LED_ERR：完整周期约 1 秒，灯光心跳 */
        blink_cnt++;            /* 心跳计数递增 */
        printf("reg alive, cnt=%u\r\n", (unsigned int)blink_cnt);   /* 串口心跳：与灯光同拍 */
    }
}
```

## 5. Keil 配置要点（REG）

| 配置项 | 值 | 说明 |
|---|---|---|
| 分组 | HARDWARE 组加 `reg.c`（首次加入） | reg.h 纯头文件，不进编译清单 |
| Include Paths | 追加 `..\HARDWARE\REG` | reg.h 在这个目录 |
| FWLib 勾选 | 不变 | 本部分只调 24CXX/printf，无新外设 |
| 依赖 | 24CXX、USART、LED 均已入库 | reg.c 只依赖 24cxx.h 与 printf |

## 6. 验证清单（REG，全部通过才算完成）

1. 编译 0 Error（`ota_info_layout_check`、`reg_layout_check` 两条新断言随编译一起验证）；
2. 串口收到 `reg test start`，随后 **三选一**：
   - 老芯片：`OTA_ver = ...,a1a1a1a1,...` + `mqtt id: pk=..., dk=...`；
   - 新芯片：先 `set default para` 再 `OTA_ver = ...` + `mqtt id: ...`；
   - 芯片故障：`24C02 check fail: OTA disabled...`（flag/retry 写路径有独立 ACK 检查，能写说明只是 Check 误报）；
   - **排查提示**：若 `check fail` 且 `flag write-back: 00000000 FAIL`、`retry roundtrip: 0 FAIL`、
     `gate write-back: 0 PASS`——回读全 0 + 写全部「成功」，是总线引脚没初始化的典型特征
     （浮空输入下 SDA 恒读低电平：读回全 0、ACK 判定恒通过）。先确认 main 里
     `AT24CXX_Init()` 在 `Reg_ReadOtaInfo()` 之前；
3. `flag write-back: A2A2A2A2 PASS`、`retry roundtrip: 2 PASS`、`gate write-back: 0 PASS`；
4. 心跳 `cnt` 每 0.5 秒递增、PD14 约 1 秒一次闪烁；
5. map 文件确认：`Ota_WriteFlag / Ota_RetryCount` 等符号已链接；reg.o 不再引用 boot.o/display.o 的符号。

## 7. 断电持久性验证（验证清单第 3 项通过后做一次）

迁移顺序对本部分的要求是「写 OTA_flag/UPDATE_flag → **断电**回读」：

1. 手动把验证 main 里恢复原值的代码临时跳过（或直接改用下面两行）：
   写 `Ota_WriteFlag(OTA_A2_FLAG)` 与 `Ota_CloseUpdateGate()` 后**断电 → 重新上电**；
2. 预期：`Reg_ReadOtaInfo` 读出的 `OTA_flag = a2a2a2a2`、`UPDATE_flag = 0`——
   证明状态块掉电保持；
3. 测完恢复：`Ota_WriteFlag(OTA_A1_FLAG)` + `Ota_CloseUpdateGate()` 的反向操作
   （把 UPDATE_flag 写回 1）或 `Reg_SeedDefaults()` 整块播种。

## 8. 跨阶段边界（REG 部分，对应《模块迁移顺序.md》，已同步修订）

1. **LCD 阶段（阶段 8）Display.h**：OTA_ERR_* 8 个错误码**照原工程留在 Display.h 定义**
   （本阶段未迁，REG 全程零引用）；`g_ota_lcd_err/hold` 定义也留在 boot.c，随 BOOT 阶段落位；
2. **阶段 10 GPRS**：`OTA_ERR_NET/TIMEOUT/LINK/DATA` 经 `Display.h` 获得（include display.h，
   与原工程一致，LCD 阶段 8 已先入库）；写 OTA 状态块字段（OTA_size/ver/flag）用
   reg.h 的 `OTA_OFF_*` 偏移 + `Ota_WriteFlag` 等函数；
3. **阶段 11 BOOT**（《BOOT模块重构.md》届时同步修订）：
   - boot.h 删 `OTA_InfoCB` 族、`OTA_ADDR/OTA_RETRY_*`、`OTA_A1/A2_FLAG`、
     `ota_info_layout_check`、`OTA_Info`/`g_ota_lcd_*` extern → 改为 `#include "reg.h"`
     ✅ 2026-11-25 去重轮已落盘（分区宏 `ST32_*` 与 `ota_layout_check` 暂留 boot.h，
     瘦身属《STMFLASH与分区布局重构.md》阶段 11）；
   - boot.c 删五个标志函数实现与 `OTA_Info` 定义 ✅（reg.c 提供）；
     `g_ota_lcd_*` 定义亦随去重轮归 reg.c ✅（boot.c 的 `#include "display.h"` 保留，
     注释仍写 OTA_ERR_* 取自 Display.h——实为 reg.h/Display.h 双份均可解析）；
   - `OtaCopyPlan_CB` 的缓冲长度 `ST32_PAGE_SIZE` 经 `#include "flash_layout.h"` 获得；
4. **阶段 12 main**：调用点 `AT24CXX_ReadOTAInfo()` 改为 `Reg_ReadOtaInfo()`。

---

本文档只生成/修订 md，不改任何 `.c/.h` 工程文件；`reg.h / reg.c / main.c` 均由你按本文档落盘，
落盘前提：24CXX（阶段 5）已编译验证通过；验证通过后 main 换成《USART重构.md》的。
