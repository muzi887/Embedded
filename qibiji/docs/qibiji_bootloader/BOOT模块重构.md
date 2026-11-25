# BOOT 模块重构方案（qibiji_boot → qibiji_bootloader）

> 本文档是 BOOT 模块的重构评审稿：**逻辑与原 boot.c 逐行等价，不修任何缺陷**，只做
> 拆文件、改名字、重写注释三件事。评审通过后按本文代码落盘。
>
> - 原始基线：`qibiji_boot/HARDWARE/BOOT/boot.c`（344 行）、`boot.h`（122 行）
> - 新结构：`boot.c`（决策+跳转）+ `ota_copy.c`（A2→A1 搬运）+ `boot.h`（声明），
>   EEPROM 标志函数迁入 `HARDWARE/REG/`
> - 迁入函数与 reg.h/reg.c 的完整文本见附录 1，未做任何 Reg_ 前缀改名（那属于 REG 模块的重构范围）

## 0. 注释风格约定（对齐 docs/qibiji_zxl 各重构文档）

1. **全覆盖**：每个函数、每个变量（全局 / 静态 / 结构体成员 / 局部）、每个宏，都必须有一处注释说明其功能；
2. **全部新写**：注释一律按当前语义重新撰写，不复用原工程注释原文；
3. **函数头格式**：定义/声明上方**一行** `/* */` 注释，一句话讲清做什么；返回值约定（如 1/0 含义）在同一行带出；实现上有讲究的最多再补一行；不写「函数功能 / 参 数 / 返 回 值」三段式；
4. **行内注**：`/* */` 跟在语句后，一句话讲这行在做什么、为什么；
5. **关键坑位**：需要警示的设计意图（防变砖、防磨损、防错位）用 `/* ... */` 或函数头内说明，写清「为什么」；
6. **文件内分组**：`/* ---- xxx ---- */`。

## 1. 改名对照表

| 原名 | 新名 | 理由 |
|---|---|---|
| `BootLoader_Brance` | `Boot_Branch` | 拼写订正（Brance→Branch）+ 前缀统一（boot 动作统一 `Boot_`，原 `BootLoader_` 过长且与 `Boot_JumpToA1` 混用） |
| `LOAD_A` | `Boot_JumpToA1` | 原名看不出「跳转到哪」，新名直接说明跳 A1 |
| `MSR_MSP` | `Boot_SetMSP`（改 static，boot.h 声明删除） | 原名是 ARM 指令助记符，不体现「设主栈指针」的功能语义；仅 Boot_JumpToA1 调用，收成本文件私有 |
| `BootLoader_Clear` | `Boot_Clear` | 前缀统一（同 Boot_Branch 行） |
| `OtaImageValidAt / OtaA2ImageValid / OtaA1ImageValid / OtaCopyVerify` | `Ota_ImageValidAt / Ota_A2ImageValid / Ota_A1ImageValid / Ota_CopyVerify` | 前缀统一（OTA 动作统一 `Ota_` 带下划线，与 Ota_CalcCopyPlan / Ota_CopyA2ToA1 对齐） |
| `load_a` / `load_A` | `app_entry_fn` / `s_AppEntry`（改 static） | 类型名含义化；指针只在本文件用，收成 static |
| `Cal_Block` | `Ota_CalcCopyPlan` | 原名看不出算的是什么，实际算的是搬运计划 |
| `Update_A1` | `Ota_CopyA2ToA1` | 原名像「更新」，实际是「把 A2 复制到 A1」 |
| `UpDataA_CB` / `UpDataA` | `OtaCopyPlan_CB` / `g_CopyPlan` | 与搬运计划语义对齐；成员见下 |
| `Updatabuff` / `BlockNB` / `size` | `PageBuf` / `PageNB` / `TailHalfwords` | `size` 原注释写「字节数」但实际是**半字数**，新名直接纠正歧义 |
| `BootStaFlag` | （删除） | 全工程无引用的死变量 |
| boot.h 尾部 4 个死声明 `BootLoader_Enter / BootLoader_Info / BootLoader_Event / Xmodem_CRC16` | （删除） | 无实现、无调用（Xmodem 遗留） |
| boot.c 内 `OtaWriteFlag / OtaClearInstalledVersion / OtaCloseUpdateGate / OtaRetryCount / OtaRetryCountSet` | （迁入 reg.c，改名 `Ota_WriteFlag / Ota_ClearInstalledVersion / Ota_CloseUpdateGate / Ota_RetryCount / Ota_RetryCountSet`，static 放开） | 它们只操作 24C02，放 REG 模块才对位；改名登记见《REG重构.md》§3 |
| `Boot_Clear` 末尾**新增 `WDG_Feed()`**（2026-11-25 WDG 阶段联动） | —（原工程无） | boot.c | IWDG 使能后软件关不掉、跳 A1 后继续计时；跳转前喂狗把整个窗口（≥2.1s）留给 A1 应用启动到第一次喂狗（A1 应用自带 WDG 接续），否则正式 main 开狗后每次跳转都可能在应用初始化途中喂狗超窗复位 |

## 2. `HARDWARE/BOOT/boot.h`（重构后全文）

```c
#ifndef BOOT_H
#define BOOT_H

#include "sys.h"
#include <stddef.h>
#include "stmflash.h"   /* 提供 STM32_FLASH_SIZE：分区断言用它核对片内 Flash 总容量 */
#include "24cxx.h"      /* 提供 EE_ADDR：分区断言用它核对 EEPROM 地址边界 */

/* A1 应用入口函数指针类型：指向应用镜像的复位向量，调用它即完成跳转 */
typedef void (*app_entry_fn)(void);

/* 芯片资源对照：分区推导与断言按 VET6（512K Flash / 2K 页）校验
   stm32f103vct6  flash:256k  ram:48k  页大小:2k
   stm32f103vet6  flash:512k  ram:64k  页大小:2k */

#define  ST32_FLASH_SADDR    0x08000000   /* 片内 Flash 基地址，三个分区的共同起点 */
#define  ST32_PAGE_SIZE      2048         /* Flash 页大小：擦除与搬运的最小单位 */
#define  ST32_PAGE_NUM       256          /* 片内 Flash 总页数（512K / 2K） */
#define  ST32_B_PAGE_NUM     28           /* B 区页数：BootLoader 自身占用 56KB（0x08000000~0x0800E000） */
#define  ST32_A1_PAGE_NUM    100          /* A1 区页数：正式应用运行区，共 200KB */
#define  ST32_A1_START_PAGE  ST32_B_PAGE_NUM                                         /* A1 起始页号 = B 区页数（两区紧邻） */
#define  ST32_A1_SADDR       (ST32_FLASH_SADDR + ST32_A1_START_PAGE * ST32_PAGE_SIZE) /* A1 起始地址，由宏链推导，当前 0x0800E000 */
#define  ST32_A2_PAGE_NUM    100                                                          /* A2 区页数：新固件下载暂存区，共 200KB */
#define  ST32_A2_START_PAGE  (ST32_A1_START_PAGE + ST32_A1_PAGE_NUM)                     /* A2 起始页号 = A1 末页（两区紧邻） */
#define  ST32_A2_SADDR       (ST32_FLASH_SADDR + ST32_A2_START_PAGE * ST32_PAGE_SIZE)    /* A2 起始地址，由宏链推导，当前 0x08040000 */

#define  OTA_A1_FLAG         0xA1A1A1A1   /* OTA_flag 取此值：A1 区为当前有效应用 */
#define  OTA_A2_FLAG         0xA2A2A2A2   /* OTA_flag 取此值：A2 区存有待安装固件 */

#define  OTA_ADDR            200          /* OTA 状态块在 24C02 中的存放起始地址 */

/* OTA 状态块：整块存于 24C02（200~243 共 44 字节），上电读入 RAM，关键变化写回 */
typedef struct {
    u32 OTA_flag;      /* 生效分区标志：OTA_A1_FLAG / OTA_A2_FLAG */
    u8  OTA_ver[32];   /* 版本串；全 0 = 本机没有可用的已安装版本（版本闸以此判定） */
    u32 OTA_size;      /* A2 中待安装固件的总字节数；0 = 无固件 */
    u8  UPDATE_flag;   /* 升级闸：1 = 上电进入联网 OTA 流程；0 = 上电直接跳 A1 */
} OTA_InfoCB;
#define OTA_INFOCB_SIZE sizeof(OTA_InfoCB)   /* 状态块总字节数，整块读写时使用 */

/* UPDATE_flag 的两个写入端：① 出厂初始化 Reg_SeedDefaults() 置 1，APP 参数协议也可改；
   ② 安装/自愈成功后由 reg.c 的 Ota_CloseUpdateGate() 自动置 0；下次升级由平台经 A1 置回 1。
   24C02 自检失败时 reg.c 显式置 0——状态落不了盘，OTA 无法进行。 */

/* 状态块各字段在 24C02 中的字节偏移：分字段读写时按此寻址 */
#define OTA_OFF_FLAG        0     /* OTA_flag 偏移（4 字节） */
#define OTA_OFF_VER         4     /* OTA_ver 偏移（32 字节） */
#define OTA_OFF_SIZE        36    /* OTA_size 偏移（4 字节） */
#define OTA_OFF_UPDATE      40    /* UPDATE_flag 偏移（1 字节） */

/* 布局断言：结构体任一成员的实际偏移与上面宏不符即编译失败（负数组长度技巧），
   防止日后改结构体导致分字段读写地址错位 */
typedef char ota_info_layout_check[
    (offsetof(OTA_InfoCB, OTA_flag)    == OTA_OFF_FLAG   &&
     offsetof(OTA_InfoCB, OTA_ver)     == OTA_OFF_VER    &&
     offsetof(OTA_InfoCB, OTA_size)    == OTA_OFF_SIZE   &&
     offsetof(OTA_InfoCB, UPDATE_flag) == OTA_OFF_UPDATE) ? 1 : -1];

/* 自愈重搬失败计数：1 字节 EEPROM，地址 244，位于状态块之后的空闲段
   （0~63 留给 APP、64~191 为 MQTT 参数、250 是 EE_ADDR 标志）。
   状态块一旦扩大会覆盖到它，ota_layout_check 会在编译期拦截 */
#define OTA_RETRY_ADDR      (OTA_ADDR + 44)
#define OTA_RETRY_MAX       3   /* 自愈重搬连续失败上限：达到后不再擦写 A1 */

/* 分区合法性编译期断言：全部由宏推导，任何一条不满足即因负数组长度编译失败
   ① A1 紧接 B 区；② A2 紧接 A1；③ A2 末页不越过片内 Flash；
   ④⑤ A1/A2 各恰为 200KB（OTA 固件上限不变）；⑥ B 区大小与 Keil IROM1 的 0xE000 一致；
   ⑦ 自愈计数地址不越过 EE_ADDR */
typedef char ota_layout_check[
    (ST32_A1_START_PAGE == ST32_B_PAGE_NUM &&
     ST32_A2_START_PAGE == (ST32_A1_START_PAGE + ST32_A1_PAGE_NUM) &&
     ((ST32_A2_START_PAGE + ST32_A2_PAGE_NUM) * ST32_PAGE_SIZE) <= (1024 * STM32_FLASH_SIZE) &&
     (ST32_A1_PAGE_NUM * ST32_PAGE_SIZE) == (200 * 1024) &&
     (ST32_A2_PAGE_NUM * ST32_PAGE_SIZE) == (200 * 1024) &&
     (ST32_B_PAGE_NUM * ST32_PAGE_SIZE) == 0xE000 &&
     (OTA_RETRY_ADDR + 1) <= EE_ADDR) ? 1 : -1];

/* A2→A1 搬运计划：Ota_CalcCopyPlan 按固件长度填写，Ota_CopyA2ToA1 按它执行 */
typedef struct {
    u8  PageBuf[ST32_PAGE_SIZE];   /* 单页搬运缓冲：A2→RAM→A1 的中转区 */
    u8  PageNB;                    /* 页块总数（含末块）；0 = 计划无效，不执行搬运 */
    u16 TailHalfwords;             /* 末块长度（半字数）：末块通常不满一页 */
} OtaCopyPlan_CB;

extern OTA_InfoCB OTA_Info;   /* OTA 状态块的 RAM 副本（定义在 boot.c，上电由 main 从 24C02 读入） */

/* LCD 错误提示登记：引导阶段屏幕尚未初始化，先在此记下错误，
   由 main 在屏就绪后统一显示（配合 LCD12864_ClearGDRAM() 清背景；
   LCD 阶段驱动换源 zxl，原 CLEARGDRAMH，对照见《LCD重构.md》§1.1） */
extern u8 g_ota_lcd_err;    /* 待显示的 OTA_ERR_* 错误码；0 = 无错误 */
extern u8 g_ota_lcd_hold;   /* 1 = 显示后停留 3 秒再尝试进应用；0 = 显示即返回 */

/* ---- 上电决策（boot.c） ---- */
/* 上电三分支：① flag≠A1 → 校验后搬 A2→A1；② flag=A1 但 A1 损坏 → 自愈重搬；
   ③ 其余 → 直接返回，由 main 决定联网 OTA 还是跳应用 */
void Boot_Branch(void);

/* ---- 跳转（boot.c） ---- */
/* Boot_SetMSP 为 static 内联汇编（仅 Boot_JumpToA1 内部调用），不再在 boot.h 声明 */
void Boot_JumpToA1(uint32_t addr); /* 跳转 A1 应用；A1 不可运行时打印提示后返回 */
void Boot_Clear(void);       /* 复位 B 区占用的外设，跳转前调用 */

/* ---- A2→A1 搬运（ota_copy.c） ---- */
void Ota_CalcCopyPlan(void);  /* 按 OTA_size 计算页块数与末块长度，写入 g_CopyPlan */
void Ota_CopyA2ToA1(void);    /* 按搬运计划逐页把 A2 内容复制到 A1 */
u8   Ota_CopyVerify(void);     /* 搬运校验：A1 与 A2 前 OTA_size 字节一致返回 1 */

#endif
```

## 3. `HARDWARE/BOOT/boot.c`（重构后全文）

```c
/* BOOT 模块：上电决策与跳转
   Boot_Branch 读 EEPROM 状态块做三分支判定；Boot_JumpToA1 负责换栈跳转。
   A2→A1 的搬运实现在 ota_copy.c，标志/计数的 EEPROM 读写实现在 reg.c。 */

#include "boot.h"
#include "usart.h"    /* printf：调试串口输出 */
#include "reg.h"      /* Ota_WriteFlag / Ota_ClearInstalledVersion / Ota_CloseUpdateGate / Ota_RetryCount(Set) */
#include <string.h>
#include "display.h"  /* OTA_ERR_*：LCD 错误码定义 */
#include "wdg.h"      /* WDG_Feed：跳转 A1 前喂狗（IWDG 一旦使能关不掉，见 Boot_Clear） */

OTA_InfoCB OTA_Info;      /* OTA 状态块 RAM 副本：上电由 main 从 24C02 读入，变化时经 reg.c 写回 */

u8 g_ota_lcd_err = 0;     /* 引导阶段登记的 LCD 错误码（OTA_ERR_*），0 = 无错误 */
u8 g_ota_lcd_hold = 0;    /* 1 = 屏就绪后显示 3 秒再尝试进应用；0 = 显示即返回 */

static app_entry_fn s_AppEntry;   /* A1 应用入口：跳转前指向其复位向量，仅本文件使用 */

/* 校验镜像头部：栈顶须落片内 RAM 区、复位向量须落 A1 区（应用按 A1 链接）；1=合法 0=坏包 */
/* 注意：分区地址宏展开为「0x08000000 + 偏移」的算式，取值时必须整体加括号再转型。
   若写成 *(volatile u32 *)ST32_A2_SADDR，强制转换只作用于第一个加数，
   实际读取地址变成基址 + 偏移×4，越界读取会把好固件判成坏包。 */
static u8 Ota_ImageValidAt(u32 base)
{
    u32 sp = *(volatile u32 *)(base);     /* 镜像首字：初始栈顶指针 */
    u32 rv = *(volatile u32 *)(base + 4); /* 镜像第 4 字节：复位向量（应用入口） */

    if (sp < 0x20000000 || sp > 0x2000FFFF)
        return 0;   /* 栈顶不在 RAM 区：不是可运行镜像 */
    if (rv < (u32)(ST32_A1_SADDR)
        || rv >= ((u32)(ST32_A1_SADDR) + (u32)ST32_A1_PAGE_NUM * ST32_PAGE_SIZE))
        return 0;   /* 复位向量不在 A1 区：应用按错误基址链接 */
    return 1;
}

/* 校验 A2 暂存区镜像头部：1=合法 0=无有效固件 */
static u8 Ota_A2ImageValid(void)
{
    return Ota_ImageValidAt((u32)(ST32_A2_SADDR));   /* 以 A2 起始地址做头部校验 */
}

/* 校验 A1 应用区镜像头部：1=合法 0=A1 已损坏或为空 */
static u8 Ota_A1ImageValid(void)
{
    return Ota_ImageValidAt((u32)(ST32_A1_SADDR));   /* 以 A1 起始地址做头部校验 */
}

/* 上电三分支决策（读 OTA_flag）：①flag≠A1 → 护栏校验后搬运安装；②flag=A1 但 A1 损坏 → 限流自愈或清版本；
   ③A1 正常 → 返回交给 main。失败路径登记 LCD 错误码后返回，成功路径直接跳应用不返回 */
void Boot_Branch(void)
{
    if (OTA_Info.OTA_flag != OTA_A1_FLAG)   /* 分支一：标志指向 A2，存在待安装固件 */
    {
        /* 护栏一：OTA_size 必须是合法固件字节数。
           size=0 会让 Ota_CalcCopyPlan 得出 PageNB=0，Ota_CopyA2ToA1 内
           PageNB-1 以 u16 存储下溢成 65535，把整片 Flash 当固件刷进 A1（变砖）；
           size 超过 A2 容量说明 EEPROM 数据已损坏，同样不可作为搬运依据。 */
        if (OTA_Info.OTA_size == 0 || OTA_Info.OTA_size > (u32)(ST32_A2_PAGE_NUM * ST32_PAGE_SIZE))
        {
            printf("bad OTA_size=%u\r\n", OTA_Info.OTA_size);
            Ota_WriteFlag(OTA_A1_FLAG);             /* 放弃本次安装，标志归位 A1 */
            Ota_ClearInstalledVersion();            /* 同步清版本：版本闸不得误判「已是最新」 */
            g_ota_lcd_err = OTA_ERR_SIZE;          /* 登记 LCD 错误：固件长度非法 */
            g_ota_lcd_hold = 1;                    /* 持续性错误，显示后停留 3 秒提醒用户 */
            return;
        }

        /* 护栏二：A2 头部必须是可运行镜像。拦截「误传 .hex 文本」「下载中途截断」
           等坏包，避免垃圾被搬进 A1 后又被搬运比对「确认」成有效应用。 */
        if (!Ota_A2ImageValid())
        {
            printf("A2 hdr invalid\r\n");
            Ota_WriteFlag(OTA_A1_FLAG);             /* 标志归位 A1 */
            Ota_ClearInstalledVersion();            /* 清版本，等待平台重新下发 */
            g_ota_lcd_err = OTA_ERR_IMAGE;         /* 登记 LCD 错误：镜像头部非法 */
            g_ota_lcd_hold = 1;
            return;
        }

        printf("loading progarm file A1\r\n");
        Ota_CalcCopyPlan();                        /* 由 OTA_size 算出页块数与末块长度 */
        Ota_CopyA2ToA1();                          /* 按计划把 A2 逐页复制到 A1 */

        /* 搬运校验：A1 与 A2 逐字节一致才承认安装成功；
           不一致则保持 A2 标志，下次上电自动重搬 */
        if (!Ota_CopyVerify())
        {
            Ota_RetryCountSet(Ota_RetryCount() + 1); /* 失败计数 +1，供自愈路径限流 */
            printf("A1 verify FAIL\r\n");
            g_ota_lcd_err = OTA_ERR_VERIFY;        /* 登记 LCD 错误：搬运校验失败 */
            g_ota_lcd_hold = 1;
            return;
        }

        if (!Ota_WriteFlag(OTA_A1_FLAG))            /* 标志写回 EEPROM：此刻起 A1 才算「已安装」 */
        {
            Ota_RetryCountSet(0);                   /* 安装成功，自愈失败计数清零 */
            Ota_CloseUpdateGate();                  /* 关升级闸：此后上电不再进 OTA 流程 */

            printf("write progarm file success\r\n");
            Boot_JumpToA1((u32)(ST32_A1_SADDR));   /* 安装完成，直接进入新应用 */
        }
        else
            printf("write progarm file error\r\n"); /* 标志落盘失败：停在 boot，下次重试安装 */
        return;
    }

    /* 分支二：标志说 A1 是当前应用，但 A1 头部非法。
       典型成因：烧写器整片擦除只清 Flash，EEPROM 里的标志与版本串原样保留，
       设备误以为应用健在。此处识别并尝试抢救。 */
    if (!Ota_A1ImageValid())
    {
        printf("A1 invalid, recover\r\n");

        /* A2 中还留有同一份镜像 → 直接重搬。三个条件缺一不可：
           size 在合法区间（防 EEPROM 脏数据）、A2 头部合法（防把垃圾转正成应用） */
        if (OTA_Info.OTA_size > 0
            && OTA_Info.OTA_size <= (u32)(ST32_A2_PAGE_NUM * ST32_PAGE_SIZE)
            && Ota_A2ImageValid())
        {
            /* 限流：连续自愈失败达上限后不再擦写 A1，防止隐形坏镜像反复磨损 Flash 寿命。
               仅约束「重搬」动作；A2 也坏时下面的清版本路径不受限流影响。 */
            if (Ota_RetryCount() >= OTA_RETRY_MAX)
            {
                printf("recover limit\r\n");
                g_ota_lcd_err = OTA_ERR_VERIFY;    /* 登记 LCD 错误：自愈次数耗尽 */
                g_ota_lcd_hold = 1;
                return;
            }

            printf("recover from A2\r\n");
            Ota_CalcCopyPlan();
            Ota_CopyA2ToA1();                      /* 用 A2 备份重建 A1 */
            if (Ota_CopyVerify())                   /* 成功后置与安装路径相同 */
            {
                Ota_WriteFlag(OTA_A1_FLAG);
                Ota_RetryCountSet(0);               /* 自愈成功，失败计数清零 */
                Ota_CloseUpdateGate();              /* 自愈同样视为安装完成，关升级闸 */
                printf("recover OK, jump A1\r\n");
                Boot_JumpToA1((u32)(ST32_A1_SADDR));
                return;
            }
            Ota_RetryCountSet(Ota_RetryCount() + 1); /* 本次自愈的搬运校验失败，计数累加 */
            printf("recover FAIL\r\n");
            g_ota_lcd_err = OTA_ERR_VERIFY;
            g_ota_lcd_hold = 1;
            return;
        }

        /* A2 也没有可用镜像 → 清版本标记：版本闸失效后平台会重新下发完整固件 */
        printf("A1/A2 invalid\r\n");
        Ota_ClearInstalledVersion();
        g_ota_lcd_err = OTA_ERR_IMAGE;             /* 登记 LCD 错误：A1、A2 均不可用 */
        g_ota_lcd_hold = 1;
    }
    /* 分支三：A1 正常 → 函数返回，main 继续走 OTA 状态机或跳应用 */
}

/* MSP = addr：跳转应用前先把栈换到应用自己的栈（Keil 内联汇编，addr 经 r0 写入 MSP）。
   仅 Boot_JumpToA1 调用，收成 static；若个别 armcc 版本不接受 static __asm 组合，去掉 static 即可 */
static __asm void Boot_SetMSP(uint32_t addr)
{
    MSR MSP, r0
    BX r14
}

/* 跳转 A1 应用：校验栈顶 → 换栈 → 取复位向量 → 清 B 区外设 → 跳；A1 不可运行时打印后返回重试 */
void Boot_JumpToA1(uint32_t addr)
{
    uint32_t vtor;   /* 当前向量表偏移，仅用于调试打印 */

    printf("A zone %x,%x,%x\r\n", (*(vu32 *)(addr)), addr, (*(vu32 *)(addr + 4)));
    if ((*(uint32_t *)addr >= 0x20000000) && (*(uint32_t *)addr <= 0x2000FFFF))   /* 应用首字（栈顶初值）须落在片内 RAM 区 */
    {
        vtor = SCB->VTOR;
        printf("vtor %x\n", vtor);
        Boot_SetMSP(*(uint32_t *)addr);                    /* 切换主栈指针到应用的栈顶 */
        s_AppEntry = (app_entry_fn) * (uint32_t *)(addr + 4);   /* 取应用复位向量作为入口 */
        printf("load progarm A is ok\r\n");
        Boot_Clear();                            /* 复位 USART1/2 与清屏，把干净外设交给应用 */
        s_AppEntry();                                  /* 跳转入应用，不再返回 */
    }
    else
        printf("load progarm A is fail\r\n");          /* 栈顶非法：A1 无有效应用，返回走自愈/OTA */
}

/* 清理 B 区外设：复位 USART1/2 + LCD 清屏 + 跳转前喂狗——IWDG 使能后软件关不掉，
   把整个窗口（≥2.1s，见《WDG重构.md》）留给 A1 应用启动到第一次喂狗 */
void Boot_Clear(void)
{
    USART_DeInit(USART1);
    USART_DeInit(USART2);

    LCD12864_Clear();   /* LCD 清屏（原工程 WRCommandH(0x01)，LCD 阶段驱动换源 zxl， */
                        /* 对照见《LCD重构.md》§1.1） */

    WDG_Feed();         /* 跳转前喂狗：给 A1 应用留满整个 IWDG 窗口（《WDG重构.md》§7.5） */
}
```

## 4. `HARDWARE/BOOT/ota_copy.c`（新文件，全文）

```c
/* OTA 搬运模块：把暂存在 A2 的新固件复制到 A1 应用区
   Ota_CalcCopyPlan 先按固件长度制定计划，Ota_CopyA2ToA1 执行逐页复制，
   Ota_CopyVerify 用逐字节比对确认结果，三步配合完成一次固件安装。
   本文件逻辑与原 boot.c 的 Cal_Block / Update_A1 / OtaCopyVerify（现 Ota_CalcCopyPlan /
   Ota_CopyA2ToA1 / Ota_CopyVerify，见 §1 改名表）逐行等价。 */

#include "boot.h"
#include "usart.h"    /* printf：调试串口输出 */
#include <string.h>   /* memcmp：搬运校验 */

OtaCopyPlan_CB g_CopyPlan;   /* 全局搬运计划：先由 Ota_CalcCopyPlan 填写，再由 Ota_CopyA2ToA1 消费 */

/* 按 OTA_size 制定搬运计划写入 g_CopyPlan：整页块 + 末块（半字数，奇数字节补足 1 半字）；
   size 非法（0 或超 A2 容量）时置 PageNB=0，搬运函数据此跳过——防下游 PageNB-1 下溢整片误刷 */
void Ota_CalcCopyPlan(void)
{
    if (OTA_Info.OTA_size == 0 || OTA_Info.OTA_size > (u32)(ST32_A2_PAGE_NUM * ST32_PAGE_SIZE))
    {
        g_CopyPlan.PageNB = 0;              /* 计划作废：搬运函数检测到 0 会直接返回 */
        g_CopyPlan.TailHalfwords = 0;
        printf("Ota_CalcCopyPlan: invalid OTA_size=%u, skip\r\n", OTA_Info.OTA_size);
        return;
    }

    if ((OTA_Info.OTA_size % ST32_PAGE_SIZE) == 0)    /* 情形一：固件恰为整页的整数倍 */
    {
        g_CopyPlan.PageNB = OTA_Info.OTA_size / ST32_PAGE_SIZE;        /* 页块数 = 长度 / 页大小 */
        g_CopyPlan.TailHalfwords = ST32_PAGE_SIZE / 2;                 /* 末块也是满页：1024 个半字 */
    }
    else                                              /* 情形二：有零头，多留一个不满页的末块 */
    {
        g_CopyPlan.PageNB = OTA_Info.OTA_size / ST32_PAGE_SIZE + 1;    /* 整页块数 + 1 个末块 */
        g_CopyPlan.TailHalfwords = OTA_Info.OTA_size % ST32_PAGE_SIZE;
        if ((g_CopyPlan.TailHalfwords % 2) == 0)
            g_CopyPlan.TailHalfwords = (g_CopyPlan.TailHalfwords / 2);       /* 偶数字节：直接折算半字数 */
        else
            g_CopyPlan.TailHalfwords = (g_CopyPlan.TailHalfwords / 2) + 1;   /* 奇数字节：补足 1 个半字 */
    }
}

/* 按搬运计划逐页把 A2 复制到 A1：前 PageNB-1 块满页搬运，末块按 TailHalfwords 个半字；
   依赖 STMFLASH_Read/Write 的半字单位与 Write 内部按页擦除，写入地址须对齐页边界 */
void Ota_CopyA2ToA1(void)
{
    u16 i = 0;                    /* 页块循环计数 */
    u32 addrW = 0, addrR = 0;     /* A1 写入地址、A2 读取地址 */

    if (g_CopyPlan.PageNB == 0)   /* 计划无效（size 非法或未制定）：不执行任何擦写 */
    {
        printf("skip update A1: no valid block\r\n");
        return;
    }

    for (i = 0; i < g_CopyPlan.PageNB - 1; i++)    /* 逐个搬运满页块（不含末块） */
    {
        addrR = ST32_A2_SADDR + i * ST32_PAGE_SIZE;   /* 第 i 块在 A2 的读取地址 */
        STMFLASH_Read(addrR, (u16 *)&g_CopyPlan.PageBuf[0], ST32_PAGE_SIZE / 2);    /* 整页读入缓冲（1024 半字） */

        addrW = ST32_A1_SADDR + i * ST32_PAGE_SIZE;   /* 同一块在 A1 的对称写入地址 */
        STMFLASH_Write(addrW, (u16 *)&g_CopyPlan.PageBuf[0], ST32_PAGE_SIZE / 2);   /* 整页写回（内部先擦后写） */
    }

    i = g_CopyPlan.PageNB - 1;    /* 末块单独处理：长度通常不满一页 */
    addrR = ST32_A2_SADDR + i * ST32_PAGE_SIZE;
    STMFLASH_Read(addrR, (u16 *)&g_CopyPlan.PageBuf[0], g_CopyPlan.TailHalfwords);    /* 只读末块实际长度 */

    addrW = ST32_A1_SADDR + i * ST32_PAGE_SIZE;
    STMFLASH_Write(addrW, (u16 *)&g_CopyPlan.PageBuf[0], g_CopyPlan.TailHalfwords);   /* 只写末块实际长度 */
}

/* 搬运校验：逐字节比对 A1/A2 固件长度范围，1=一致（安装有效）0=不一致或长度非法；填充字节不参与 */
u8 Ota_CopyVerify(void)
{
    u32 len = OTA_Info.OTA_size;   /* 参与比较的固件总字节数 */

    if (len == 0 || len > (u32)(ST32_A2_PAGE_NUM * ST32_PAGE_SIZE))
        return 0;   /* 长度非法：直接判失败，不做无意义比对 */
    if (memcmp((const void *)((u32)(ST32_A2_SADDR)), (const void *)((u32)(ST32_A1_SADDR)), len) != 0)
        return 0;   /* 存在不一致字节：搬运出错 */
    return 1;
}
```

## 附录 1：REG 模块联动（boot.c 依赖，必须与本模块一起落地）

### 附录 1.1 `HARDWARE/REG/reg.h`（在现有文件末尾 `#endif` 前追加）

```c
/* ---- OTA 标志读写（自 boot.c 迁入，逻辑不变） ---- */
/* 以下五个函数直接操作 24C02 中 OTA 状态块 / 自愈计数的对应字段 */

u8  Ota_WriteFlag(u32 flag);           /* 写 OTA_flag（4 字节）：传入 OTA_A1_FLAG / OTA_A2_FLAG */
void Ota_ClearInstalledVersion(void);  /* 清空 OTA_ver：版本闸失效，平台将重新下发固件 */
void Ota_CloseUpdateGate(void);        /* 写 UPDATE_flag=0：关闭升级闸，上电不再进 OTA */
u8  Ota_RetryCount(void);              /* 读自愈重搬失败计数（1 字节） */
void Ota_RetryCountSet(u8 v);          /* 写自愈重搬失败计数（1 字节） */
```

### 附录 1.2 `HARDWARE/REG/reg.c`（在现有文件末尾追加，正文一字未改，仅去掉 static）

```c
/* ---- OTA 标志读写（自 boot.c 迁入） ---- */

/* 分区标志同步 RAM 副本并落盘 EEPROM；返回 AT24CXX_Write 返回值（0 = 成功） */
u8 Ota_WriteFlag(u32 flag)
{
    OTA_Info.OTA_flag = flag;    /* 同步 RAM 副本，后续判定直接读它 */
    return AT24CXX_Write(OTA_ADDR + OTA_OFF_FLAG, (uint8_t *)&OTA_Info.OTA_flag, 4);
}

/* 清空已安装版本串并落盘：版本闸失效，平台将重新下发完整固件 */
void Ota_ClearInstalledVersion(void)
{
    memset(&OTA_Info.OTA_ver, 0, 32);   /* RAM 副本清零 */
    AT24CXX_Write(OTA_ADDR + OTA_OFF_VER, (uint8_t *)&OTA_Info.OTA_ver[0], 32);   /* EEPROM 同步清零 */
}

/* 关升级闸：UPDATE_flag=0 并落盘，此后上电直接进 A1；下次升级由平台经 A1 置回 1 */
void Ota_CloseUpdateGate(void)
{
    OTA_Info.UPDATE_flag = 0;
    AT24CXX_Write(OTA_ADDR + OTA_OFF_UPDATE, (uint8_t *)&OTA_Info.UPDATE_flag, 1);
}

/* 读自愈重搬失败计数（EEPROM 1 字节） */
u8 Ota_RetryCount(void)
{
    u8 v = 0;   /* 计数值容器 */
    AT24CXX_Read(OTA_RETRY_ADDR, &v, 1);
    return v;
}

/* 写自愈重搬失败计数（成功路径清零、失败路径累加） */
void Ota_RetryCountSet(u8 v)
{
    AT24CXX_Write(OTA_RETRY_ADDR, &v, 1);
}
```

## 附录 2：其它模块联动清单（应用本模块时必须同步）

| 文件 | 位置 | 改动 |
|---|---|---|
| `USER/main.c` | :29、:50 | `LOAD_A(ST32_A1_SADDR)` → `Boot_JumpToA1(ST32_A1_SADDR)`；:24、:42 注释里的 `LOAD_A` 同步改文本 |
| `USER/main.c` | :87 | `BootLoader_Brance()` → `Boot_Branch()` |
| `USER/BOOTV10.uvprojx` | HARDWARE 组 | 新增 `ota_copy.c` 条目（路径 `../HARDWARE/BOOT/ota_copy.c`） |
| 后续 STMFLASH 模块 md | — | `STMFLASH_Read/Write` → `Flash_Read/Flash_Write`（届时改 ota_copy.c 4 处调用 + gprs_rx.c 1 处 + stmflash.c 自身） |

## 附录 3：本次删掉的死代码清单（均无引用，删除不影响逻辑）

1. `boot.h:77-83` 注释掉的旧 `UpDataA_CB`（W25Q64/Xmodem 时代遗留）
2. `boot.h:113-116` 4 个无实现声明：`BootLoader_Enter / BootLoader_Info / BootLoader_Event / Xmodem_CRC16`
3. `boot.c:13` `BootStaFlag`（全工程零引用）
4. `boot.c:120-126` `BootLoader_Brance` 开头整段注释掉的旧分支
5. `boot.c:273-275` `BootLoader_Clear`（现 `Boot_Clear`）里注释掉的 `usart_deinit(GPIOA/B)` 等
6. `boot.c:310` 注释掉的调试打印
7. `boot.c` 不再使用的 include：`delay.h`、`gprs.h`、`24cxx.h`、`stmflash.h`（boot.h 仍保留后两者，供宏断言与 ota_copy.c 使用）
