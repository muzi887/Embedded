# REG 与 USART 重构

> ① 先落 **REG**（§1~8）——它只依赖已入库的 24CXX 与现有 usart1 发送路径，验证 main 跑完 REG 即可确认 EEPROM 业务链路；
> ② 再落 **USART**（§9~16）——回补 usart1 接收路径并新建 usart2，验证 main 是 PA2–PA3 跳线回环。
> 两部分的 main 依次替换：阶段 5 的 EEPROM main → REG main → USART main，最后换成后续阶段的。

---

# 第一部分 REG（迁移顺序阶段 6：参数登记处）

> 来源：`qibiji_boot/HARDWARE/REG/reg.c/.h` + `qibiji_boot/HARDWARE/BOOT/boot.c/.h` 中的
> OTA 状态块族（`OTA_InfoCB`、偏移宏、`OTA_ADDR/OTA_RETRY_*`、布局断言）与五个 EEPROM
> 标志函数（`OtaWriteFlag / OtaClearInstalledVersion / OtaCloseUpdateGate / OtaRetryCount(Set)`）。
> REG 是 **24C02 地址布局的属主**：布局总表与 OTA 状态块归 reg.h。
> 逻辑等价 + 一处契约修订（见 §1.1，为阶段 5《EEPROM硬件访问层重构.md》预先约定的联动）。

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
新代码按三态分派，且**播种动作 `paraToEeprom()` 从 24CXX 层移回 REG 层调用**
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
                          写入端：① 出厂默认 paraToEeprom() 置 1（APP 参数协议也可改）；
                          ② 安装/自愈成功后由 OtaCloseUpdateGate() 自动置 0，
                          再次升级需由平台经 A1 应用把它置回 1。
                          24C02 自检失败时 Reg_ReadOtaInfo 显式置 0（状态落不了盘，做不了 OTA）。 */
} OTA_InfoCB;
#define OTA_INFOCB_SIZE sizeof(OTA_InfoCB)   /* 状态块总字节数 = 44（含对齐填充），整块读写时使用 */

/* 状态块各字段在 24C02 中的字节偏移：分字段读写（OtaWriteFlag 等）按此寻址 */
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
void Reg_ReadOtaInfo(void);              /* 上电入口：三态自检分派（播种 / 故障 / 正常读取），填 OTA_Info */
void MQTT_ReadKeysFromEeprom(void);      /* 从 24C02 读 productKey/deviceKey，空槽保持兜底值 */
void paraToEeprom(void);                 /* 播种：把 OTA 状态块默认值写进 24C02（名字保留原工程） */

u8   OtaWriteFlag(u32 flag);             /* 只写 OTA_flag（4 字节）：传入 OTA_A1_FLAG / OTA_A2_FLAG */
void OtaClearInstalledVersion(void);     /* 清空 OTA_ver：版本闸失效，平台将重新下发固件 */
void OtaCloseUpdateGate(void);           /* 写 UPDATE_flag=0：关闭升级闸，上电不再进 OTA */
u8   OtaRetryCount(void);                /* 读自愈重搬失败计数（1 字节） */
void OtaRetryCountSet(u8 v);             /* 写自愈重搬失败计数（1 字节） */

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
void MQTT_ReadKeysFromEeprom(void)
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
        paraToEeprom();   /* 新芯片：播种默认值（内部写回 24C02），随后走正常读取路径与老芯片殊途同归 */
    }
    else if (chk == AT24CXX_ERROR)
    {
        /* 24C02 自检/读写失败（芯片或 I2C 总线异常）：升级状态无法落盘，
           下载好的固件也没法靠 OTA_flag 引导，显式关闭联网 OTA，让设备直接运行 A1。
           只 memset 成 0 属于「静默失效」，现场看不出原因；版本串与 paraToEeprom() 的 "v0.0.0" 保持一致。 */
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
    MQTT_ReadKeysFromEeprom();   /* 同时取出 platform 需要的 productKey / deviceKey */
}

/* 播种：把 OTA 状态块默认值写进 24C02。
   触发者只有 Reg_ReadOtaInfo 的 FRESH 分支；写的是整块默认值
   （flag=A1 当前区最新、UPDATE=1 默认开启联网 OTA、ver=v0.0.0 尚无版本记录）。 */
void paraToEeprom(void)
{
    memset(&OTA_Info, 0, OTA_INFOCB_SIZE);
    OTA_Info.OTA_flag = OTA_A1_FLAG;   /* 标志位默认值：当前 A 区是最新的 */
    OTA_Info.UPDATE_flag = 1;          /* 0 = 无需 OTA，1 = 需要联网 OTA（默认开启） */
    sprintf((char *)OTA_Info.OTA_ver, "v0.0.0");   /* 首参转 char*：OTA_ver 是 u8 数组，消除 #167-D 符号性告警 */
    printf("set default para\r\n");
    printf("OTA_ver = %s,%x\n", OTA_Info.OTA_ver, OTA_Info.OTA_flag);
    AT24CXX_Write(OTA_ADDR, (u8 *)&OTA_Info, OTA_INFOCB_SIZE);   /* 整块写回 24C02 */
}

/* ---- OTA 状态块分字段读写（原 boot.c 五个 static 函数迁入，名字不变、static 放开） ---- */

/* 只写 OTA_flag 字段（偏移 OTA_OFF_FLAG，4 字节）：RAM 副本与 24C02 同步更新。
   返回 AT24CXX_OK/ERROR，BOOT 用返回值确认「已安装」是否真的落盘 */
u8 OtaWriteFlag(u32 flag)
{
    OTA_Info.OTA_flag = flag;   /* 先改 RAM 副本，保持内存与 EEPROM 一致 */
    return AT24CXX_Write(OTA_ADDR + OTA_OFF_FLAG, (u8 *)&OTA_Info.OTA_flag, 4);
}

/* 清掉 OTA_ver：表示「本机没有可用的已安装版本」。
   这样 putVersion 的版本闸不会再认为「已是最新」，下次联网会重新下载安装 */
void OtaClearInstalledVersion(void)
{
    memset(&OTA_Info.OTA_ver, 0, 32);   /* 全 0 = 版本闸失效（见 OTA_ver 字段注释） */
    AT24CXX_Write(OTA_ADDR + OTA_OFF_VER, (u8 *)&OTA_Info.OTA_ver[0], 32);
}

/* 安装成功 → 关闭升级标志（UPDATE_flag=0）：之后上电直接跑应用、不再联网核查。
   再次升级需由平台经 A1 应用把 UPDATE_flag 置 1（见 reg.h 字段注释） */
void OtaCloseUpdateGate(void)
{
    OTA_Info.UPDATE_flag = 0;
    AT24CXX_Write(OTA_ADDR + OTA_OFF_UPDATE, (u8 *)&OTA_Info.UPDATE_flag, 1);
}

/* 读自愈重搬失败计数（地址 OTA_RETRY_ADDR，1 字节）：成功清零、失败累加，供自愈限流 */
u8 OtaRetryCount(void)
{
    u8 v = 0;   /* 读出值：空片 0xFF 会得到 255，但正常流程下播种/成功路径都会清零 */
    AT24CXX_Read(OTA_RETRY_ADDR, &v, 1);
    return v;
}

/* 写自愈重搬失败计数 */
void OtaRetryCountSet(u8 v)
{
    AT24CXX_Write(OTA_RETRY_ADDR, &v, 1);
}
```

**本部分改名与清理**：

| 原名 | 处理 | 理由 |
|---|---|---|
| `AT24CXX_ReadOTAInfo` | 改名 `Reg_ReadOtaInfo` | 原名带 AT24CXX 前缀，实为 REG 的入口函数，且 AT24CXX 是另一个模块的名字，易误导 |
| `OtaWriteFlag / OtaClearInstalledVersion / OtaCloseUpdateGate / OtaRetryCount / OtaRetryCountSet` | 从 boot.c 迁入，**名字不变、static 放开** | 只操作 24C02，归 REG 才对位（《BOOT模块重构.md》附录 1 作废，由本部分接替） |
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
    if (OtaWriteFlag(OTA_A2_FLAG) == AT24CXX_OK)
    {
        AT24CXX_Read(OTA_ADDR, s_block_buf, OTA_INFOCB_SIZE);   /* 从 24C02 整块读回（非 RAM 副本） */
        flag_back = (u32)s_block_buf[0]
                  | ((u32)s_block_buf[1] << 8)
                  | ((u32)s_block_buf[2] << 16)
                  | ((u32)s_block_buf[3] << 24);                /* 按小端拼回 u32 */
        printf("flag write-back: %08X %s\r\n", (unsigned int)flag_back,
               (flag_back == OTA_A2_FLAG) ? "PASS" : "FAIL");
        OtaWriteFlag(flag_save);   /* 恢复测试前标志（可能是 A1，也可能本就挂着 A2 待安装） */
    }
    else
    {
        printf("flag write FAIL\r\n");
    }

    /* ③ 自愈计数往返：写 2 → 读回应为 2 → 清零复原 */
    OtaRetryCountSet(2);
    retry_back = OtaRetryCount();
    printf("retry roundtrip: %u %s\r\n", (unsigned int)retry_back,
           (retry_back == 2) ? "PASS" : "FAIL");
    OtaRetryCountSet(0);

    /* ④ 升级闸往返：置 0 → 回读 24C02 校验 → 恢复原值（不用 paraToEeprom 整块恢复，
       避免覆盖真实设备的版本记录与 OTA_size） */
    OtaCloseUpdateGate();
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
     `AT24CXX_Init()` 在 `Reg_ReadOtaInfo()` 之前；3. `flag write-back: A2A2A2A2 PASS`、`retry roundtrip: 2 PASS`、`gate write-back: 0 PASS`；
4. 心跳 `cnt` 每 0.5 秒递增、PD14 约 1 秒一次闪烁；
5. map 文件确认：`OtaWriteFlag / OtaRetryCount` 等符号已链接；reg.o 不再引用 boot.o/display.o 的符号。

## 7. 断电持久性验证（验证清单第 3 项通过后做一次）

迁移顺序对本部分的要求是「写 OTA_flag/UPDATE_flag → **断电**回读」：

1. 手动把验证 main 里恢复原值的代码临时跳过（或直接改用下面两行）：
   写 `OtaWriteFlag(OTA_A2_FLAG)` 与 `OtaCloseUpdateGate()` 后**断电 → 重新上电**；
2. 预期：`Reg_ReadOtaInfo` 读出的 `OTA_flag = a2a2a2a2`、`UPDATE_flag = 0`——
   证明状态块掉电保持；
3. 测完恢复：`OtaWriteFlag(OTA_A1_FLAG)` + `OtaCloseUpdateGate()` 的反向操作
   （把 UPDATE_flag 写回 1）或 `paraToEeprom()` 整块播种。

## 8. 跨阶段边界（REG 部分，对应《模块迁移顺序.md》，已同步修订）

1. **LCD 阶段（阶段 8）Display.h**：OTA_ERR_* 8 个错误码**照原工程留在 Display.h 定义**
   （本阶段未迁，REG 全程零引用）；`g_ota_lcd_err/hold` 定义也留在 boot.c，随 BOOT 阶段落位；
2. **阶段 10 GPRS**：`OTA_ERR_NET/TIMEOUT/LINK/DATA` 经 `Display.h` 获得（include display.h，
   与原工程一致，LCD 阶段 8 已先入库）；写 OTA 状态块字段（OTA_size/ver/flag）用
   reg.h 的 `OTA_OFF_*` 偏移 + `OtaWriteFlag` 等函数；
3. **阶段 11 BOOT**（《BOOT模块重构.md》届时同步修订）：
   - boot.h 删 `OTA_InfoCB` 族、`OTA_ADDR/OTA_RETRY_*`、`OTA_A1/A2_FLAG`、
     `ota_info_layout_check`、`ota_layout_check`（分区六条已迁 flash_layout.h、
     第七条已迁 reg.h）、`OTA_Info` extern → 改为 `#include "reg.h"`；
     **`g_ota_lcd_*` extern 留在 boot.h 不迁**；
   - boot.c 删五个标志函数实现与 `OTA_Info` 定义（reg.c 提供）；
     **`g_ota_lcd_*` 定义与 `#include "display.h"` 保留原位**（LCD 归属未变）；
   - `OtaCopyPlan_CB` 的缓冲长度 `ST32_PAGE_SIZE` 经 `#include "flash_layout.h"` 获得；
4. **阶段 12 main**：调用点 `AT24CXX_ReadOTAInfo()` 改为 `Reg_ReadOtaInfo()`。

---

# 第二部分 USART（usart1 回补接收路径 + usart2 入库）

> 来源：`qibiji_boot/HARDWARE/USART/usart1.c` + `usart.h` + `usart2.c`。
> 阶段 1 骨架版只带了 usart1 的发送路径（当时接收无消费者，且 IDLE 中断里调 `Usart2_Send`
> 会强制链接 usart2.c）。本部分把接收路径按原工程回补，并把 4G 口 usart2 一起入库。
> 逻辑等价：不改任何时序、优先级与缓冲长度，只做注释新写与死声明裁剪。

## 9. 本部分产出清单

| 文件 | 动作 | 说明 |
|---|---|---|
| `HARDWARE/USART/usart.h` | 重写 | 裁掉 usart3/4/5、RS485 等未迁移声明，保留 usart1/2 全部内容（见 §10） |
| `HARDWARE/USART/usart1.c` | 重写 | 回补 PA10 RX + DMA1 通道 5 + IDLE 中断 + 转发逻辑，与原工程逐行等价（见 §11） |
| `HARDWARE/USART/usart2.c` | **新建** | 4G 口：DMA1 通道 6 + IDLE 中断 + 4KB 环形缓冲，与原工程逐行等价（见 §12） |
| `USER/main.c` | 替换 | 本部分验证代码：USART1→USART2 回环测试（见 §13），替换 REG 验证 main |
| Keil 工程 | 手动改 | HARDWARE 组加 `usart2.c`（usart1.c 已在工程，替换内容即可）；FWLib 增勾 `stm32f10x_dma.c`、`stm32f10x_usart.c`（见 §14） |

### 9.1 阶段 1 裁剪项的回补对照

| 阶段 1 骨架版裁剪项 | 本部分动作 |
|---|---|
| PA10 RX 引脚配置、`USART_Mode_Rx \| USART_Mode_Tx` | 回补进 `uart1_init`（与原工程一致） |
| RX DMA（DMA1 通道 5）+ `DMA_Rece_Buf1` + `MYDMA_Enable1` | 回补 |
| IDLE 中断使能 + `USART1_IRQHandler`（PC 数据转发 4G 口） | 回补；usart2.c 本部分入库，`Usart2_Send` 可正常链接 |
| `Usart1_Send` 阻塞数组发送 | 回补（原文件组成，保持完整） |
| usart.h 里 usart3/4/5、RS485 等声明 | 仍不迁：对应模块不在迁移计划内（见 §10 删除清单） |

## 10. usart.h（重写，全文）

```c
#ifndef __USART_H
#define __USART_H

#include "stm32f10x.h"
#include "stdio.h"

/* ---- DMA 接收通道对照（本工程只用到 USART1/USART2 两路接收） ----
   USART1 RX = DMA1 通道 5；USART2 RX = DMA1 通道 6。
   原表还有 TX 通道与 USART3/4/5 的映射，对应模块未迁移，需要时查参考手册。 */

/* 接收缓冲长度：DMA 单帧最大字节数；IDLE 中断按「本帧实收字节数」取走数据后重启 DMA */
#define DMA_Rec1_Len  256    /* USART1 接收缓冲：调试口，PC 命令帧短，256 足够 */
#define DMA_Rec2_Len  1024   /* USART2 接收缓冲：4G 口透传帧可达 1KB */

/* USART2 接收环形缓冲总长：必须为 2 的幂（usart2.c 里有编译期检查强制保证）。
   DMA 单帧最多 1KB，环形缓冲给到 4KB，可容纳多个未消费帧不丢数据 */
#define USART2_RX_RING_SIZE  4096

/* printf 总开关（阶段 1 引入，原样保留）。
   1 = printf 正常输出（fputc 重定向到 USART1，实现见 usart1.c）；
   0 = printf 编译期替换为空操作，所有调用点被优化掉、不占代码空间，
       需要产出"静默"固件时改这一个数字即可，调用点不用动。 */
#define ENABLE_PRINTF  1

#if !ENABLE_PRINTF
#define printf(...)  ((void)0)   /* 空操作替换；仅在开关关闭时才会编译到此行 */
#endif

extern char DMA_Rece_Buf1[DMA_Rec1_Len];   /* USART1 DMA 接收缓冲：硬件逐字节自动写入 */
extern char DMA_Rece_Buf2[DMA_Rec2_Len];   /* USART2 DMA 接收缓冲：同上（4G 口） */

extern u16 Usart1_Rec_Cnt;   /* USART1 最近一帧实收字节数，IDLE 中断里更新 */
extern u16 Usart2_Rec_Cnt;   /* USART2 最近一帧实收字节数，同上 */

/* ---- 初始化 ---- */
void uart1_init(uint32_t bound);   /* 调试口：PA9 TX / PA10 RX，bound 为波特率（原工程 115200） */
void uart2_init(uint32_t bound);   /* 4G 口：PA2 TX / PA3 RX */

/* ---- 阻塞式数组发送：逐字节等 TC（发送完成）再发下一个 ---- */
void Usart1_Send(char *buf, u16 len);
void Usart2_Send(char *buf, u16 len);

/* ---- DMA 接收重启：IDLE 中断取走一帧后调用，让 DMA 从缓冲头重新写入 ---- */
void MYDMA_Enable1(DMA_Channel_TypeDef *DMA_CHx);   /* 重启 USART1 的 DMA1 通道 5 */
void MYDMA_Enable2(DMA_Channel_TypeDef *DMA_CHx);   /* 重启 USART2 的 DMA1 通道 6 */

/* ---- USART2 接收环形缓冲：中断只写入，业务层只读取/消费（实现见 usart2.c） ---- */
u16 Usart2_RxAvailable(void);                /* 当前可读字节数 */
u16 Usart2_RxCopy(char *dst, u16 max_len);   /* 拷出最多 max_len 字节（只读不消费） */
void Usart2_RxConsume(u16 len);              /* 消费 len 字节（移动读指针） */
void Usart2_RxClear(void);                   /* 清空缓冲（读指针追平写指针） */
u32  Usart2_RxOverflowCount(void);           /* 溢出累计：环形缓冲满时每帧记一次并丢弃本帧剩余字节 */

#endif
```

**删除的声明**（对应模块不迁移，grep 证实 B 区无引用）：
usart3/4/5 全套（`uart3/4/5_init`、`Usart3/4/5_Send`、`MYDMA_Enable3/4`、`DMA_Rec3/4_Len`、
`DMA_Rece_Buf3/4`、`Usart3/4/5_Rec_Cnt`）、通用 `MYDMA_Enable`、`Dma_Tx1..4_okflag`
（定义在未迁移的 usart3/4/5.c 里）、`RS485_CE4_H/L` 宏（PD6 方向控制，属未迁移的 RS485 电路）。

## 11. usart1.c（重写，全文）

```c
#include "usart.h"

/* ---- printf 重定向（不启用 MicroLIB 的标准做法） ----
   用 __use_no_semihosting 摆脱半主机模式：半主机下 printf 会触发 SWI 陷阱，
   脱离调试器单独运行时程序直接卡死，所以必须补齐标准库要求的符号。 */
#if 1
#pragma import(__use_no_semihosting)

/* 标准库要求 FILE 结构体可见且可实例化，这里只做占位 */
struct __FILE
{
    int handle;   /* 句柄占位成员，重定向下不使用具体值 */
};

FILE __stdout;   /* printf 的默认输出流对象，fputc 通过它被间接调用 */

/* 替换标准库的系统退出桩，防止链接器拉入半主机模式的退出实现；退出码不使用。
   显式补 void 返回类型：C99 模式下隐式 int 会报 #260-D。 */
void _sys_exit(int x)
{
    x = x;   /* 空实现，仅为满足符号引用、消除告警 */
}

/* printf 的底层单字符出口，重定向到 USART1 数据寄存器；f 为输出流指针，重定向下无实际用途 */
int fputc(int ch, FILE *f)
{
    while((USART1->SR & 0X40) == 0);   /* 轮询等待 TC（发送完成）标志：上一字符未发完就写 DR 会丢字 */
    USART1->DR = (u8) ch;              /* 写入数据寄存器即自动启动本次发送 */
    return ch;
}
#endif

/* USART1 接收侧数据 */
char DMA_Rece_Buf1[DMA_Rec1_Len];   /* DMA 硬件自动写入的接收缓冲 */
u16  Usart1_Rec_Cnt;                /* 最近一帧实收字节数，IDLE 中断里更新 */

/* 调试口初始化：PA9 TX / PA10 RX，接收走 DMA1 通道 5 + IDLE 空闲中断。
   IDLE = 总线连续一个字节时间无数据，即「一帧发完了」，此时 DMA 计数器里
   剩多少没写，缓冲里就是这一帧的实收长度——不需要约定帧尾字符。 */
void uart1_init(uint32_t bound)   /* bound 为波特率，原工程 main 调用值 115200 */
{
    GPIO_InitTypeDef GPIO_InitStructure;     /* 引脚配置：引脚号、速度、复用模式 */
    USART_InitTypeDef USART_InitStructure;   /* USART 参数：波特率、字长、停止位等六项 */
    NVIC_InitTypeDef NVIC_InitStructure;     /* 中断配置：通道、抢占/响应优先级 */
    DMA_InitTypeDef DMA_InitStructure;       /* DMA 配置：外设/内存地址、方向、长度等 */

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);   /* USART1 与 GPIOA 同挂 APB2，一并开时钟 */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);                              /* DMA1 挂 AHB，接收搬运用 */

    USART_DeInit(USART1);   /* 复位 USART1 全部寄存器，从缺省状态开始配置，避免残留态 */

    /* TX：PA9 复用推挽，引脚控制权交给 USART 外设 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* RX：PA10 浮空输入，电平完全由对方驱动 */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 中断配置：抢占 1 / 响应 2（原工程值，与其它中断的相对关系不要随意改动） */
    NVIC_InitStructure.NVIC_IRQChannel                   = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 2;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 串口参数：8N1 无流控，收发双向 */
    USART_InitStructure.USART_BaudRate            = bound;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);

    USART_ITConfig(USART1, USART_IT_IDLE, ENABLE);      /* 开 IDLE 空闲中断：一帧结束的信号源 */
    USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE);      /* RX 数据流交给 DMA 搬运，不占 CPU */
    USART_Cmd(USART1, ENABLE);                          /* 使能外设 */

    /* DMA1 通道 5 = USART1 RX：外设→内存，字节宽度，普通模式（发满即停，靠中断重启） */
    DMA_DeInit(DMA1_Channel5);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)&USART1->DR;       /* 源：USART1 数据寄存器 */
    DMA_InitStructure.DMA_MemoryBaseAddr     = (u32)DMA_Rece_Buf1;     /* 目的：接收缓冲 */
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralSRC;  /* 方向：外设 → 内存 */
    DMA_InitStructure.DMA_BufferSize         = DMA_Rec1_Len;           /* 缓冲总长 */
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;  /* 外设地址固定（同一寄存器） */
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;   /* 内存地址递增（逐字节排入缓冲） */
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;  /* 8 位宽度 */
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;        /* 普通模式：计数到 0 停止，等中断重启 */
    DMA_InitStructure.DMA_Priority           = DMA_Priority_Medium;
    DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;        /* 非内存到内存 */
    DMA_Init(DMA1_Channel5, &DMA_InitStructure);
    DMA_Cmd(DMA1_Channel5, ENABLE);   /* 启动接收搬运 */
}

/* 阻塞式数组发送：逐字节等 TC 再发下一个，全部发完后再等一次 TC 确认收尾 */
void Usart1_Send(char *buf, u16 len)
{
    u16 t;   /* 字节循环计数 */
    for (t = 0; t < len; t++)
    {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
        USART_SendData(USART1, buf[t]);
    }
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
}

/* USART1 中断服务：只处理 IDLE。
   读一次 DR 清掉 IDLE 的置位条件，取走本帧长度，把整帧转发给 4G 口（USART2），
   最后重启 DMA 让下一帧从头写缓冲。PC 发来的数据由此透传给 4G 模块。 */
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_IDLE) != RESET)
    {
        USART_ReceiveData(USART1);   /* 读 DR：清 IDLE 置位条件（读动作本身即清除） */
        Usart1_Rec_Cnt = DMA_Rec1_Len - DMA_GetCurrDataCounter(DMA1_Channel5);   /* 实收 = 总长 - 剩余计数 */
        Usart2_Send((char *)DMA_Rece_Buf1, Usart1_Rec_Cnt);   /* 整帧转发 4G 口 */
        USART_ClearITPendingBit(USART1, USART_IT_IDLE);       /* 清中断标志 */
        MYDMA_Enable1(DMA1_Channel5);                         /* 重启 DMA，准备下一帧 */
    }
}

/* 重启接收 DMA：先关通道才能改计数器，重装满长度后再打开 */
void MYDMA_Enable1(DMA_Channel_TypeDef *DMA_CHx)
{
    DMA_Cmd(DMA_CHx, DISABLE);                       /* 关通道：计数器仅在关闭时可写 */
    DMA_SetCurrDataCounter(DMA_CHx, DMA_Rec1_Len);   /* 重装缓冲长度 */
    DMA_Cmd(DMA_CHx, ENABLE);                        /* 重新启动搬运 */
}
```

## 12. usart2.c（新建，全文）

```c
#include "usart.h"

/* USART2 接收侧数据 */
char DMA_Rece_Buf2[DMA_Rec2_Len];   /* DMA 硬件自动写入的接收缓冲 */
u16  Usart2_Rec_Cnt;                /* 最近一帧实收字节数，IDLE 中断里更新 */

/* ---- USART2 接收环形缓冲 ----
   中断只写（生产者），业务层只读/消费（消费者）， head/tail 单向前进不回退，
   用「写指针 - 读指针」的无符号回绕差值表达在途字节数。
   临界保护用 PRIMASK 现场保存/恢复：只关中断的瞬间取快照，不在临界区里做拷贝。 */
static char         s_usart2_rx_ring[USART2_RX_RING_SIZE];   /* 环形存储区 */
static volatile u16 s_usart2_rx_head = 0;                    /* 写指针：中断里前进 */
static volatile u16 s_usart2_rx_tail = 0;                    /* 读指针：业务层消费时前进 */
static volatile u32 s_usart2_rx_overflow_count = 0;          /* 溢出累计：环满丢帧时递增 */

/* 下标回绕掩码：length 为 2 的幂时，index & mask 等价于 index % length */
#define USART2_RX_RING_MASK ((u16)(USART2_RX_RING_SIZE - 1))

/* 编译期强制「长度为 2 的幂」：非 2 的幂会让掩码回绕失效，直接编译失败 */
#if (USART2_RX_RING_SIZE & (USART2_RX_RING_SIZE - 1)) != 0
#error "USART2_RX_RING_SIZE must be a power of two"
#endif

/* 任意下标折算成环内下标 */
static u16 Usart2_RxUseMask(u16 index)
{
    return (u16)(index & USART2_RX_RING_MASK);
}

/* 当前可读字节数：临界区内取 head/tail 快照，差值用无符号回绕计算 */
u16 Usart2_RxAvailable(void)
{
    u16 head;
    u16 tail;
    u32 primask = __get_PRIMASK();

    __disable_irq();
    head = s_usart2_rx_head;
    tail = s_usart2_rx_tail;
    __set_PRIMASK(primask);

    return (u16)((head - tail) & USART2_RX_RING_MASK);
}

/* 拷出最多 max_len 字节：临界区内取快照并限长，拷贝动作在临界区外，
   DMA 缓冲→环形缓冲只发生在中断里，环形缓冲本身在读指针之前不会被覆盖 */
u16 Usart2_RxCopy(char *dst, u16 max_len)
{
    u16 head;
    u16 tail;
    u16 available;
    u16 i;
    u32 primask;

    if (dst == 0 || max_len == 0)
        return 0;   /* 参数非法：拷 0 字节 */

    primask = __get_PRIMASK();
    __disable_irq();
    head = s_usart2_rx_head;
    tail = s_usart2_rx_tail;
    available = (u16)((head - tail) & USART2_RX_RING_MASK);   /* 实际可读量 */
    if (available > max_len)
        available = max_len;                                  /* 不超过调用方缓冲 */
    for (i = 0; i < available; i++)
    {
        dst[i] = s_usart2_rx_ring[Usart2_RxUseMask((u16)(tail + i))];
    }
    __set_PRIMASK(primask);

    return available;
}

/* 消费 len 字节：只移动读指针，超出可读量的部分按可读量截断 */
void Usart2_RxConsume(u16 len)
{
    u16 available;
    u32 primask = __get_PRIMASK();

    __disable_irq();
    available = (u16)((s_usart2_rx_head - s_usart2_rx_tail) & USART2_RX_RING_MASK);
    if (len > available)
        len = available;   /* 防御：消费量不得超过在途数据 */
    s_usart2_rx_tail = Usart2_RxUseMask((u16)(s_usart2_rx_tail + len));
    __set_PRIMASK(primask);
}

/* 清空缓冲：读指针追平写指针，效果等于丢弃全部未读数据 */
void Usart2_RxClear(void)
{
    u32 primask = __get_PRIMASK();

    __disable_irq();
    s_usart2_rx_tail = s_usart2_rx_head;
    __set_PRIMASK(primask);
}

/* 读溢出累计值 */
u32 Usart2_RxOverflowCount(void)
{
    return s_usart2_rx_overflow_count;
}

/* 4G 模块通信口初始化：PA2 TX / PA3 RX，接收走 DMA1 通道 6 + IDLE 空闲中断，
   配置结构与 uart1_init 同构，只有引脚、外设、通道与优先级不同 */
void uart2_init(uint32_t bound)   /* bound 为波特率，原工程 GPRS 调用值 115200 */
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    DMA_InitTypeDef DMA_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);   /* USART2 挂 APB1（与 USART1 的 APB2 不同） */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    USART_DeInit(USART2);   /* 复位后从缺省状态开始配置 */

    /* TX：PA2 复用推挽 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* RX：PA3 浮空输入 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 中断配置：抢占 1 / 响应 1（比 USART1 的响应优先级更高，4G 数据不能等） */
    NVIC_InitStructure.NVIC_IRQChannel                   = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 串口参数：8N1 无流控，收发双向 */
    USART_InitStructure.USART_BaudRate            = bound;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &USART_InitStructure);

    USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);      /* IDLE：一帧结束信号 */
    USART_DMACmd(USART2, USART_DMAReq_Rx, ENABLE);      /* RX 交给 DMA */
    USART_Cmd(USART2, ENABLE);

    /* DMA1 通道 6 = USART2 RX，配置与通道 5 同构 */
    DMA_DeInit(DMA1_Channel6);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)&USART2->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr     = (u32)DMA_Rece_Buf2;
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize         = DMA_Rec2_Len;
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_Medium;
    DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel6, &DMA_InitStructure);
    DMA_Cmd(DMA1_Channel6, ENABLE);
}

/* 阻塞式数组发送（与 Usart1_Send 同构，目标 USART2） */
void Usart2_Send(char *buf, u16 len)
{
    u16 t;
    for (t = 0; t < len; t++)
    {
        while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
        USART_SendData(USART2, buf[t]);
    }
    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
}

/* USART2 中断服务：IDLE 到来即「4G 发来一帧」。
   DMA 缓冲仍由硬件按帧填充，软件立即把它追加进环形缓冲；
   环满时丢弃本帧剩余字节并计一次溢出，不覆盖未消费数据 */
void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_IDLE) != RESET)
    {
        u16 len;   /* 本帧实收字节数 */
        u16 i;     /* 逐字节入环计数 */

        USART_ReceiveData(USART2);   /* 读 DR：清 IDLE 置位条件 */
        len = (u16)(DMA_Rec2_Len - DMA_GetCurrDataCounter(DMA1_Channel6));
        if (len > DMA_Rec2_Len)
            len = DMA_Rec2_Len;      /* 防御：计数异常时不越界 */
        Usart2_Rec_Cnt = len;

        for (i = 0; i < len; i++)
        {
            u16 next = Usart2_RxUseMask((u16)(s_usart2_rx_head + 1));   /* 写指针的下一位置 */
            if (next == s_usart2_rx_tail)   /* 环满：追上读指针 */
            {
                s_usart2_rx_overflow_count++;   /* 记一次溢出 */
                break;                          /* 丢弃本帧剩余字节，绝不覆盖未读数据 */
            }
            s_usart2_rx_ring[s_usart2_rx_head] = DMA_Rece_Buf2[i];
            s_usart2_rx_head = next;
        }

        USART_ClearITPendingBit(USART2, USART_IT_IDLE);
        MYDMA_Enable2(DMA1_Channel6);   /* 重启 DMA，准备下一帧 */
    }
}

/* 重启接收 DMA（与 MYDMA_Enable1 同构） */
void MYDMA_Enable2(DMA_Channel_TypeDef *DMA_CHx)
{
    DMA_Cmd(DMA_CHx, DISABLE);
    DMA_SetCurrDataCounter(DMA_CHx, DMA_Rec2_Len);
    DMA_Cmd(DMA_CHx, ENABLE);
}
```

## 13. 本部分验证 main.c（替换 REG 验证 main，全文）

验证一条完整链路：**PC → USART1 接收（DMA+IDLE）→ 中断转发 USART2 发送 → 跳线回环 →
USART2 接收（DMA+IDLE）→ 环形缓冲 → main 轮询取出 → printf 回显**。
一条链路同时覆盖 usart1 接收回补与 usart2 全部新代码。

**测试前必须用杜邦线把 PA2 短接到 PA3**（回环跳线），否则链路无数据。

```c
#include "stm32f10x.h"
#include "sys.h"
#include "delay.h"
#include "usart.h"
#include "led.h"

static u32  blink_cnt = 0;    /* 心跳计数：watch 窗口看它递增确认主循环在跑，printf 里同步输出 */
static char s_rx_buf[128];    /* 回环数据回显缓冲：每轮最多拷 128 字节，超出部分下一轮继续 */

int main(void)
{
    u16 avail;   /* 环形缓冲当前可读字节数 */
    u16 n;       /* 本轮实际拷出并消费的字节数 */

    delay_init();               /* SysTick 延时（阶段 1 已验证） */
    NVIC_Configuration();       /* 中断分组（阶段 1 已验证） */
    uart1_init(115200);         /* 调试口：printf + PC 命令接收 */
    uart2_init(115200);         /* 4G 口：本部分验证对象 */
    LED_Init();
    LED_CLOSE();                /* 全灭基准态 */

    printf("usart test start\r\n");
    printf("loopback: jumper PA2 to PA3, send text from pc\r\n");

    while (1)
    {
        delay_ms(500);          /* 半秒节拍 */
        LED_ERR = !LED_ERR;     /* 位带翻转 LED_ERR：完整周期约 1 秒，灯光心跳 */
        blink_cnt++;            /* 心跳计数递增 */
        printf("usart alive, cnt=%u\r\n", (unsigned int)blink_cnt);   /* 串口心跳：与灯光同拍 */

        avail = Usart2_RxAvailable();          /* 回环数据检查 */
        while (avail > 0)
        {
            n = Usart2_RxCopy(s_rx_buf, sizeof(s_rx_buf));   /* 拷出（不消费） */
            Usart2_RxConsume(n);                             /* 消费 */
            s_rx_buf[n] = '\0';                              /* 补字符串结尾，按文本回显 */
            printf("echo %u bytes: %s\r\n", (unsigned int)n, s_rx_buf);
            avail = Usart2_RxAvailable();                    /* 一帧可能超过 128 字节，取到空为止 */
        }
    }
}
```

测试方法：SSCOM 连调试口发一行 ASCII（如 `hello`，结尾的回车换行也算字节），
应收到 `echo N bytes: hello`（N 含 `\r\n`）。连发多行可顺带验证环形缓冲的跨帧排队。

## 14. Keil 配置要点（USART）

| 配置项 | 值 | 说明 |
|---|---|---|
| 分组 | HARDWARE 组加 `usart2.c` | usart1.c 已在工程，内容替换为本文 §11 即可 |
| Include Paths | 不变 | `..\HARDWARE\USART` 阶段 1 已配 |
| FWLib 勾选 | 增勾 `stm32f10x_dma.c`、`stm32f10x_usart.c` | DMA 配置函数与 USART 收发函数都在这两个文件；misc.c（NVIC）阶段 1 已勾 |
| stm32f10x_conf.h | 确认包含 `stm32f10x_dma.h`、`stm32f10x_usart.h` | 原工程 conf.h 已包含，原样拷贝即可 |

## 15. 验证清单（USART，全部通过才算完成）

1. 编译 0 Error（环形缓冲 2 的幂检查随编译一起验证）；
2. 串口收到 `usart test start` + 跳线提示；
3. SSCOM 发一行文本，收到对应 `echo N bytes: ...`（N 与所发字节数一致）；
4. 连发多行不丢行（环形缓冲排队生效）；
5. 心跳 `cnt` 每 0.5 秒递增、PD14 约 1 秒一次闪烁；
6. map 文件确认：`usart2.o` 已链接，`Usart2_RxAvailable / USART2_IRQHandler` 符号存在。

## 16. 跨阶段边界（USART 部分）

1. **阶段 10 GPRS**：`usart2.c` 的环形缓冲 API 是 gprs_rx 的取数入口（`Usart2_RxCopy/Consume`）；
2. **阶段 11 BOOT**：`BootLoader_Clear()` 跳转前 `USART_DeInit(USART1/2)`——届时需确认跳转前
   先关两路 IDLE 中断，避免跳转瞬间中断向量切走后来不及退出（阶段 11 文档处理）；
3. usart3/4/5 与 RS485 不在迁移计划内，usart.h 里的相关声明已裁掉，后续若要用再按原工程回补。

---

本文档只生成/修订 md，不改任何 `.c/.h` 工程文件；REG 与 USART 的
`reg.h / reg.c / usart.h / usart1.c / usart2.c / main.c` 均由你按本文档落盘，
落盘顺序：先第一部分 REG（编译 + 上板验证通过），再第二部分 USART。
