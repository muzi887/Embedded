/* OTA 搬运模块：把暂存在 A2 的新固件复制到 A1 应用区
   Ota_CalcCopyPlan 先按固件长度制定计划，Ota_CopyA2ToA1 执行逐页复制，
   Ota_CopyVerify 用逐字节比对确认结果，三步配合完成一次固件安装。 */

#include "boot.h"
#include "usart.h"    /* printf：调试串口输出 */
#include <string.h>   /* memcmp：搬运校验 */
#include "flash_layout.h"

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
   依赖 Flash_Read/Write 的半字单位与 Write 内部按页擦除，写入地址须对齐页边界 */
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
        Flash_Read(addrR, (u16 *)&g_CopyPlan.PageBuf[0], ST32_PAGE_SIZE / 2);    /* 整页读入缓冲（1024 半字） */

        addrW = ST32_A1_SADDR + i * ST32_PAGE_SIZE;   /* 同一块在 A1 的对称写入地址 */
        Flash_Write(addrW, (u16 *)&g_CopyPlan.PageBuf[0], ST32_PAGE_SIZE / 2);   /* 整页写回（内部先擦后写） */
    }

    i = g_CopyPlan.PageNB - 1;    /* 末块单独处理：长度通常不满一页 */
    addrR = ST32_A2_SADDR + i * ST32_PAGE_SIZE;
    Flash_Read(addrR, (u16 *)&g_CopyPlan.PageBuf[0], g_CopyPlan.TailHalfwords);    /* 只读末块实际长度 */

    addrW = ST32_A1_SADDR + i * ST32_PAGE_SIZE;
    Flash_Write(addrW, (u16 *)&g_CopyPlan.PageBuf[0], g_CopyPlan.TailHalfwords);   /* 只写末块实际长度 */
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
