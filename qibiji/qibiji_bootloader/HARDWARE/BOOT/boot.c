#include "boot.h"
#include "usart.h"    // printf：调试串口输出
#include "reg.h"      
#include <string.h>
#include "display.h"  // OTA_ERR_*：LCD 错误码定义
#include "wdg.h"      // WDG_Feed：跳转 A1 前喂狗（IWDG 一旦使能关不掉，见 Boot_Clear）

/* BOOT 模块：上电决策与跳转
   Boot_Branch 读 EEPROM 状态块做三分支判定；Boot_JumpToA1 负责换栈跳转。
   A2→A1 的搬运实现在 ota_copy.c，标志/计数的 EEPROM 读写实现在 reg.c。 */
   
static app_entry_fn s_AppEntry;   /* A1 应用入口：跳转前指向其复位向量 */
   
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
        /* OTA_size 必须是合法固件字节数。 */
        if (OTA_Info.OTA_size == 0 || OTA_Info.OTA_size > (u32)(ST32_A2_PAGE_NUM * ST32_PAGE_SIZE))
        {
            printf("bad OTA_size=%u\r\n", OTA_Info.OTA_size);
            Ota_WriteFlag(OTA_A1_FLAG);             /* 放弃本次安装，标志归位 A1 */
            Ota_ClearInstalledVersion();            /* 同步清版本：版本闸不得误判「已是最新」 */
            g_ota_lcd_err = OTA_ERR_SIZE;          /* 登记 LCD 错误：固件长度非法 */
            g_ota_lcd_hold = 1;                    /* 持续性错误，显示后停留 3 秒提醒用户 */
            return;
        }

        /* A2 头部必须是可运行镜像。 */
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

/* MSP = addr：跳转应用前先把栈换到应用自己的栈（Keil 内联汇编，addr 经 r0 写入 MSP） */
static __asm void MSR_MSP(uint32_t addr)
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
        MSR_MSP(*(uint32_t *)addr);                    /* 切换主栈指针到应用的栈顶 */
        s_AppEntry = (app_entry_fn) * (uint32_t *)(addr + 4);   /* 取应用复位向量作为入口 */
        printf("load progarm A is ok\r\n");
        Boot_Clear();                            /* 复位 USART1/2 与清屏，把干净外设交给应用 */
        s_AppEntry();                                  /* 跳转入应用，不再返回 */
    }
    else
        printf("load progarm A is fail\r\n");          /* 栈顶非法：A1 无有效应用，返回走自愈/OTA */
}

/* 清理 B 区外设：复位 USART1/2 + LCD 清屏 + 跳转前喂狗——IWDG 使能后软件关不掉，
   把整个窗口（≥2.1s）留给 A1 应用启动到第一次喂狗 */
void Boot_Clear(void)
{
    USART_DeInit(USART1);
    USART_DeInit(USART2);

    LCD12864_Clear();   /* LCD 清屏 */

    WDG_Feed();         /* 跳转前喂狗：给 A1 应用留满整个 IWDG 窗口 */
}
