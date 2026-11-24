// BOOT 模块：上电决策与跳转
// BootLoader_Branch 读 EEPROM 状态块做三分支判定；Boot_JumpToA1 负责换栈跳转。
// A2→A1 的搬运实现在 ota_copy.c，标志/计数的 EEPROM 读写实现在 reg.c。

#include "boot.h"
#include "usart.h"    // printf：调试串口输出
#include "reg.h"      // OtaWriteFlag / OtaClearInstalledVersion / OtaCloseUpdateGate / OtaRetryCount(Set)
#include <string.h>
#include "display.h"  // OTA_ERR_*：LCD 错误码定义

OTA_InfoCB OTA_Info;      // OTA 状态块 RAM 副本：上电由 main 从 24C02 读入，变化时经 reg.c 写回

u8 g_ota_lcd_err = 0;     // 引导阶段登记的 LCD 错误码（OTA_ERR_*），0 = 无错误
u8 g_ota_lcd_hold = 0;    // 1 = 屏就绪后显示 3 秒再尝试进应用；0 = 显示即返回

static app_entry_fn s_AppEntry;   // A1 应用入口：跳转前指向其复位向量，仅本文件使用

// 函数功能：校验 base 处镜像头部是否像一份可运行的应用
// 参    数：base —— 镜像起始地址（A1 或 A2）
// 返 回 值：1 = 头部合法；0 = 坏包
// 判定依据：镜像首字（初始栈顶）必须落在片内 RAM 区；第 4 字节（复位向量）
//           必须落在 A1 区内——应用固件一律按 A1 地址链接
/* 注意：分区地址宏展开为「0x08000000 + 偏移」的算式，取值时必须整体加括号再转型。
   若写成 *(volatile u32 *)ST32_A2_SADDR，强制转换只作用于第一个加数，
   实际读取地址变成基址 + 偏移×4，越界读取会把好固件判成坏包。 */
static u8 OtaImageValidAt(u32 base)
{
    u32 sp = *(volatile u32 *)(base);     // 镜像首字：初始栈顶指针
    u32 rv = *(volatile u32 *)(base + 4); // 镜像第 4 字节：复位向量（应用入口）

    if (sp < 0x20000000 || sp > 0x2000FFFF)
        return 0;   // 栈顶不在 RAM 区：不是可运行镜像
    if (rv < (u32)(ST32_A1_SADDR)
        || rv >= ((u32)(ST32_A1_SADDR) + (u32)ST32_A1_PAGE_NUM * ST32_PAGE_SIZE))
        return 0;   // 复位向量不在 A1 区：应用按错误基址链接
    return 1;
}

// 函数功能：校验 A2 暂存区里的镜像头部
// 返 回 值：1 = A2 头部合法；0 = A2 无有效固件
static u8 OtaA2ImageValid(void)
{
    return OtaImageValidAt((u32)(ST32_A2_SADDR));   // 以 A2 起始地址做头部校验
}

// 函数功能：校验 A1 应用区里的镜像头部
// 返 回 值：1 = A1 头部合法；0 = A1 已损坏或为空
static u8 OtaA1ImageValid(void)
{
    return OtaImageValidAt((u32)(ST32_A1_SADDR));   // 以 A1 起始地址做头部校验
}

// 函数功能：上电分支决策。读 OTA_flag 决定本次启动做什么：
//           分支一 flag≠A1：A2 有待安装固件 → 两道护栏校验 → 搬运 → 校验 → 置标志跳转；
//           分支二 flag=A1 但 A1 头部非法（如烧写器整片擦除后）：A2 有镜像则限流自愈重搬，
//                  A2 也坏则清版本标记，让平台重新下发固件；
//           分支三 flag=A1 且 A1 正常：直接返回，由 main 决定联网 OTA 还是跳应用
// 参    数：无
// 返 回 值：无（失败路径均登记 LCD 错误码后返回，成功路径直接跳应用不返回）
void BootLoader_Branch(void)
{
    if (OTA_Info.OTA_flag != OTA_A1_FLAG)   // 分支一：标志指向 A2，存在待安装固件
    {
        /* 护栏一：OTA_size 必须是合法固件字节数。
           size=0 会让 Ota_CalcCopyPlan 得出 PageNB=0，Ota_CopyA2ToA1 内
           PageNB-1 以 u16 存储下溢成 65535，把整片 Flash 当固件刷进 A1（变砖）；
           size 超过 A2 容量说明 EEPROM 数据已损坏，同样不可作为搬运依据。 */
        if (OTA_Info.OTA_size == 0 || OTA_Info.OTA_size > (u32)(ST32_A2_PAGE_NUM * ST32_PAGE_SIZE))
        {
            printf("bad OTA_size=%u\r\n", OTA_Info.OTA_size);
            OtaWriteFlag(OTA_A1_FLAG);             // 放弃本次安装，标志归位 A1
            OtaClearInstalledVersion();            // 同步清版本：版本闸不得误判「已是最新」
            g_ota_lcd_err = OTA_ERR_SIZE;          // 登记 LCD 错误：固件长度非法
            g_ota_lcd_hold = 1;                    // 持续性错误，显示后停留 3 秒提醒用户
            return;
        }

        /* 护栏二：A2 头部必须是可运行镜像。拦截「误传 .hex 文本」「下载中途截断」
           等坏包，避免垃圾被搬进 A1 后又被搬运比对「确认」成有效应用。 */
        if (!OtaA2ImageValid())
        {
            printf("A2 hdr invalid\r\n");
            OtaWriteFlag(OTA_A1_FLAG);             // 标志归位 A1
            OtaClearInstalledVersion();            // 清版本，等待平台重新下发
            g_ota_lcd_err = OTA_ERR_IMAGE;         // 登记 LCD 错误：镜像头部非法
            g_ota_lcd_hold = 1;
            return;
        }

        printf("loading progarm file A1\r\n");
        Ota_CalcCopyPlan();                        // 由 OTA_size 算出页块数与末块长度
        Ota_CopyA2ToA1();                          // 按计划把 A2 逐页复制到 A1

        // 搬运校验：A1 与 A2 逐字节一致才承认安装成功；
        // 不一致则保持 A2 标志，下次上电自动重搬
        if (!OtaCopyVerify())
        {
            OtaRetryCountSet(OtaRetryCount() + 1); // 失败计数 +1，供自愈路径限流
            printf("A1 verify FAIL\r\n");
            g_ota_lcd_err = OTA_ERR_VERIFY;        // 登记 LCD 错误：搬运校验失败
            g_ota_lcd_hold = 1;
            return;
        }

        if (!OtaWriteFlag(OTA_A1_FLAG))            // 标志写回 EEPROM：此刻起 A1 才算「已安装」
        {
            OtaRetryCountSet(0);                   // 安装成功，自愈失败计数清零
            OtaCloseUpdateGate();                  // 关升级闸：此后上电不再进 OTA 流程

            printf("write progarm file success\r\n");
            Boot_JumpToA1((u32)(ST32_A1_SADDR));   // 安装完成，直接进入新应用
        }
        else
            printf("write progarm file error\r\n"); // 标志落盘失败：停在 boot，下次重试安装
        return;
    }

    // 分支二：标志说 A1 是当前应用，但 A1 头部非法。
    // 典型成因：烧写器整片擦除只清 Flash，EEPROM 里的标志与版本串原样保留，
    // 设备误以为应用健在。此处识别并尝试抢救。
    if (!OtaA1ImageValid())
    {
        printf("A1 invalid, recover\r\n");

        // A2 中还留有同一份镜像 → 直接重搬。三个条件缺一不可：
        // size 在合法区间（防 EEPROM 脏数据）、A2 头部合法（防把垃圾转正成应用）
        if (OTA_Info.OTA_size > 0
            && OTA_Info.OTA_size <= (u32)(ST32_A2_PAGE_NUM * ST32_PAGE_SIZE)
            && OtaA2ImageValid())
        {
            /* 限流：连续自愈失败达上限后不再擦写 A1，防止隐形坏镜像反复磨损 Flash 寿命。
               仅约束「重搬」动作；A2 也坏时下面的清版本路径不受限流影响。 */
            if (OtaRetryCount() >= OTA_RETRY_MAX)
            {
                printf("recover limit\r\n");
                g_ota_lcd_err = OTA_ERR_VERIFY;    // 登记 LCD 错误：自愈次数耗尽
                g_ota_lcd_hold = 1;
                return;
            }

            printf("recover from A2\r\n");
            Ota_CalcCopyPlan();
            Ota_CopyA2ToA1();                      // 用 A2 备份重建 A1
            if (OtaCopyVerify())                   // 成功后置与安装路径相同
            {
                OtaWriteFlag(OTA_A1_FLAG);
                OtaRetryCountSet(0);               // 自愈成功，失败计数清零
                OtaCloseUpdateGate();              // 自愈同样视为安装完成，关升级闸
                printf("recover OK, jump A1\r\n");
                Boot_JumpToA1((u32)(ST32_A1_SADDR));
                return;
            }
            OtaRetryCountSet(OtaRetryCount() + 1); // 本次自愈的搬运校验失败，计数累加
            printf("recover FAIL\r\n");
            g_ota_lcd_err = OTA_ERR_VERIFY;
            g_ota_lcd_hold = 1;
            return;
        }

        // A2 也没有可用镜像 → 清版本标记：版本闸失效后平台会重新下发完整固件
        printf("A1/A2 invalid\r\n");
        OtaClearInstalledVersion();
        g_ota_lcd_err = OTA_ERR_IMAGE;             // 登记 LCD 错误：A1、A2 均不可用
        g_ota_lcd_hold = 1;
    }
    // 分支三：A1 正常 → 函数返回，main 继续走 OTA 状态机或跳应用
}

// 函数功能：将主栈指针 MSP 设为指定值——跳转应用前必须先把栈换到应用自己的栈
// 参    数：addr —— 新的 MSP 初值（取自应用镜像首字）
// 返 回 值：无
// 实现：Keil 内联汇编。调用时 addr 位于 r0，MSR 指令把它写入 MSP，BX lr 返回调用点
__asm void MSR_MSP(uint32_t addr)
{
    MSR MSP, r0
    BX r14
}

// 函数功能：跳转到 A1 应用。校验栈顶合法 → 切换栈 → 取复位向量 → 清理 B 区外设 → 跳转
// 参    数：addr —— A1 应用起始地址（即其向量表首地址）
// 返 回 值：正常不返回；A1 不可运行时打印提示后返回（调用方留在 boot 继续重试）
void Boot_JumpToA1(uint32_t addr)
{
    uint32_t vtor;   // 当前向量表偏移，仅用于调试打印

    printf("A zone %x,%x,%x\r\n", (*(vu32 *)(addr)), addr, (*(vu32 *)(addr + 4)));
    if ((*(uint32_t *)addr >= 0x20000000) && (*(uint32_t *)addr <= 0x2000FFFF))   // 应用首字（栈顶初值）须落在片内 RAM 区
    {
        vtor = SCB->VTOR;
        printf("vtor %x\n", vtor);
        MSR_MSP(*(uint32_t *)addr);                    // 切换主栈指针到应用的栈顶
        s_AppEntry = (app_entry_fn) * (uint32_t *)(addr + 4);   // 取应用复位向量作为入口
        printf("load progarm A is ok\r\n");
        BootLoader_Clear();                            // 复位 USART1/2 与清屏，把干净外设交给应用
        s_AppEntry();                                  // 跳转入应用，不再返回
    }
    else
        printf("load progarm A is fail\r\n");          // 栈顶非法：A1 无有效应用，返回走自愈/OTA
}

// 函数功能：清理 B 区占用、可能干扰 A1 运行的外设资源
// 参    数：无
// 返 回 值：无
// 内容：复位调试串口 USART1 与 OTA 通讯串口 USART2；向 LCD 发清屏命令
void BootLoader_Clear(void)
{
    USART_DeInit(USART1);
    USART_DeInit(USART2);

    WRCommandH(0x01);   // LCD 清屏指令
}