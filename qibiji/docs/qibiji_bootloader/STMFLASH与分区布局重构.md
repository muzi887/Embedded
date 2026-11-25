# STMFLASH 与分区布局重构（阶段 4）

> 来源：`qibiji_boot/HARDWARE/STMFLASH/stmflash.c/.h` + `qibiji_boot/HARDWARE/BOOT/boot.h` 中的分区宏。
> 本阶段逻辑等价，**唯一例外**：跨页写地址推进的单位缺陷已定案修正（`WriteAddr += secremain` →
> `+= secremain * 2`，见 §4 与 §8——2026-11-25 审计升级为阶段 10 前置必修项）。
> 其余只做：模块重排、改名、注释按现行规范全部新写。
> 分区宏按《模块迁移顺序.md》§3 从 boot.h 迁出，落到独立的 `flash_layout.h`。

## 1. 本阶段产出清单

| 文件 | 动作 | 说明 |
|---|---|---|
| `HARDWARE/STMFLASH/flash_layout.h` | **新建** | 分区宏 + 分区编译期断言（从 boot.h 迁出，见 §2） |
| `HARDWARE/STMFLASH/stmflash.h` | 重写 | 函数改名 `STMFLASH_*` → `Flash_*`，删死声明（见 §3） |
| `HARDWARE/STMFLASH/stmflash.c` | 重写 | 同步改名；页缓冲 static 化；逻辑逐行等价（见 §4） |
| `USER/main.c` | 替换 | 本阶段验证代码：A2 区试写一页并回读比对（见 §5，替换阶段 1 的 main） |
| Keil 工程 | 手动加 | HARDWARE 组加 `stmflash.c`；Include Paths 加 `..\HARDWARE\STMFLASH`；FWLib 勾 `stm32f10x_flash.c`（见 §6） |

不迁移：`stmflash.h` 里声明但从未实现的 `STMFLASH_WriteLenByte / STMFLASH_ReadLenByte`（死声明）、
无任何引用的 `Test_Write`、stmflash.c 里未使用的 `#include "delay.h"` 与 `#include "usart.h"`。

## 2. flash_layout.h（新建，全文）

为什么独立成头：分区地址（A1/A2）的使用方不止 BOOT——阶段 10 的 GPRS 写 A2、阶段 6 的 reg.h
都要用；锁在 boot.h 里会迫使低层模块反向依赖 BOOT。放 `HARDWARE/STMFLASH/` 目录，
与 stmflash.h 同目录，不需要新增 include 路径之外的配置。

```c
#ifndef __FLASH_LAYOUT_H__
#define __FLASH_LAYOUT_H__

#include "stmflash.h"   /* 取 STM32_FLASH_SIZE（片内容量，单位 KB）：A2 越界断言依赖它 */

/* 片内 Flash 物理参数（F103VET6：512KB，每页 2KB，共 256 页） */
#define ST32_FLASH_SADDR    0x08000000  /* Flash 基地址：三个分区的地址全部由它推出 */
#define ST32_PAGE_SIZE      2048        /* 每页字节数：F103 高容量型固定 2KB，擦除与搬运都以页为单位 */
#define ST32_PAGE_NUM       256         /* 总页数 = 512KB / 2KB */

/* B 区（BootLoader 自身）：从 Flash 基地址开始，大小由页数决定 */
#define ST32_B_PAGE_NUM     28          /* B 区页数：28 页 x 2KB = 56KB（0x08000000 ~ 0x0800E000），必须与 Keil 工程 IROM1 的 Size 一致 */

/* A1 区（应用运行区）：紧接 B 区，起始页号 = B 区总页数 */
#define ST32_A1_START_PAGE  ST32_B_PAGE_NUM                                            /* A1 起始页号：改 B 区页数它自动跟着动 */
#define ST32_A1_PAGE_NUM    100                                                        /* A1 页数：100 页 x 2KB = 200KB */
#define ST32_A1_SADDR       (ST32_FLASH_SADDR + ST32_A1_START_PAGE * ST32_PAGE_SIZE)   /* A1 起始地址（当前 = 0x0800E000），宏必须整体加括号后再参与运算 */

/* A2 区（下载固件暂存区）：紧接 A1 区 */
#define ST32_A2_START_PAGE  (ST32_A1_START_PAGE + ST32_A1_PAGE_NUM)                    /* A2 起始页号 = A1 末页 + 1（当前 = 128） */
#define ST32_A2_PAGE_NUM    100                                                        /* A2 页数：100 页 x 2KB = 200KB */
#define ST32_A2_SADDR       (ST32_FLASH_SADDR + ST32_A2_START_PAGE * ST32_PAGE_SIZE)   /* A2 起始地址（当前 = 0x08040000） */

/* 分区编译期断言：全部是宏与字面量的常量表达式，编译期即可求值；
   任何一条不满足时数组长度为 -1，编译直接失败，把"烧到板上才发现分区错位"提前到编译窗口。
   宏全部整体加括号：不带括号时 *(volatile u32 *)ST32_A2_SADDR 这类转型会把指针运算截断。 */
typedef char flash_layout_check[
    (ST32_A1_START_PAGE == ST32_B_PAGE_NUM                                                     /* A1 紧接 B 区 */
     && ST32_A2_START_PAGE == (ST32_A1_START_PAGE + ST32_A1_PAGE_NUM)                          /* A2 紧接 A1 区 */
     && ((ST32_A2_START_PAGE + ST32_A2_PAGE_NUM) * ST32_PAGE_SIZE) <= (1024 * STM32_FLASH_SIZE) /* A2 末页不越过片内 Flash 尾部 */
     && (ST32_A1_PAGE_NUM * ST32_PAGE_SIZE) == (200 * 1024)                                    /* A1 固定 200KB：OTA 固件上限依据 */
     && (ST32_A2_PAGE_NUM * ST32_PAGE_SIZE) == (200 * 1024)                                    /* A2 固定 200KB */
     && (ST32_B_PAGE_NUM * ST32_PAGE_SIZE) == 0xE000                                           /* B 区 56KB：必须与 Keil IROM1 的 Size 一致 */
    ) ? 1 : -1];

#endif
```

原 boot.h 的 `ota_layout_check` 断言里有第七条 `(OTA_RETRY_ADDR + 1) <= EE_ADDR`——它检查的是
**EEPROM** 地址布局，依赖 `24cxx.h` 的 `EE_ADDR`，而 24CXX 要到阶段 5 才入库。这条**不在本阶段迁入**，
阶段 6 建 reg.h 时随 EEPROM 布局一起落进去。

## 3. stmflash.h（重写，全文）

```c
#ifndef __STMFLASH_H__
#define __STMFLASH_H__

#include "stm32f10x.h"  /* vu16 类型与固件库声明 */

/* 片内 Flash 物理参数（本模块的越界检查与分页逻辑使用；flash_layout.h 的断言也引用容量值） */
#define STM32_FLASH_SIZE    512         /* 片内 Flash 总容量，单位 KB：F103VET6 = 512 */
#define STM32_FLASH_BASE    0x08000000  /* Flash 基地址：合法地址检查、扇区定位都相对它计算 */
#define STM32_FLASH_WREN    1           /* 写使能开关：1 = 编入全部写函数；0 = 只保留读函数（写函数整体被条件编译剔除） */

/* 读单个半字：地址必须是 2 的倍数（Flash 按半字内存映射） */
u16  Flash_ReadHalfWord(u32 faddr);

/* 按半字连续写入：自动按页分片，擦除前把整页旧内容读回合并重写；
   NumToWrite 的单位是"半字个数"（不是字节） */
void Flash_Write(u32 WriteAddr, u16 *pBuffer, u16 NumToWrite);

/* 按半字连续读出：NumToRead 的单位是"半字个数"（不是字节） */
void Flash_Read(u32 ReadAddr, u16 *pBuffer, u16 NumToRead);

#endif
```

读写颗粒度是半字是 STM32F103 Flash 硬件决定的：

- **写**：F103 的 Flash 编程硬件只支持 16 位半字为单位写入，固件库的原子操作是 `FLASH_ProgramHalfWord`（`FLASH_ProgramWord` 是固件库内部拆成两次半字写拼出来的，不是硬件能力）。读接口取同一粒度，读写两侧的数据格式（`u16` 数组）完全对应，合并重写不需要拆分转换。
- **不按整字**：整字读要求 4 字节对齐，而写入起始地址只保证 2 字节对齐（页内偏移按半字计），整字读会跨到相邻数据、需要移位掩码拆开，得不偿失。
- **不按字节**：F103 Flash 擦除后是 `0xFFFF` 而不是 `0xFF`，且不能写单个字节，字节粒度既不能写、判空也对不上值。
- **`Flash_ReadHalfWord` 单独存在的价值**：单点判空（看某个地址是不是 `0xFFFF`，如验证 A1 区是否有固件、读 A1 首半字快照）和 `Flash_Read`/`Flash_Write` 内部循环的底层单元；`Flash_Read` 只是它的批量封装。

## 4. stmflash.c（重写，全文）

逐行对应原文件，仅改名 + 新注释 + 页缓冲 static 化 + 删除未使用的 include；
写流程、擦除判断、跨页循环与原工程完全一致。

```c
#include "stmflash.h"

/* 读单个半字：Flash 在地址空间内存映射，直接按 16 位指针取值即可 */
u16 Flash_ReadHalfWord(u32 faddr)
{
    return *(vu16 *)faddr;
}

#if STM32_FLASH_WREN    /* 写路径整体受写使能开关控制：开关为 0 时下面全部不编译 */

#define STM_SECTOR_SIZE 2048    /* 单个擦除页字节数：本芯片 512KB，取原条件编译的大页分支（原代码按容量 <256KB 与否在 1024/2048 之间切换） */

/* 整页暂存缓冲：写函数需要先读回整页旧内容、擦除、合入新数据后整页重写；
   只在本文件内使用，static 收拢作用域 */
static u16 s_sect_buf[STM_SECTOR_SIZE / 2];

/* 无检查连续写：逐半字调用固件库编程、地址按字节推进；
   不做擦除判断，调用方必须保证目标区间已经擦除（本文件内部使用） */
static void Flash_WriteNoCheck(u32 WriteAddr, u16 *pBuffer, u16 NumToWrite)
{
    u16 i;                                                  /* 半字循环计数 */
    for (i = 0; i < NumToWrite; i++)
    {
        FLASH_ProgramHalfWord(WriteAddr, pBuffer[i]);       /* 编程一个半字 */
        WriteAddr += 2;                                     /* 地址按 2 字节推进 */
    }
}

/* 按半字写入任意长度：以页为单位自动分片；
   对每一段先判断目标区间是否为空白（0xFFFF）：非空白则整页擦除、
   把新数据合入页缓冲后整页写回，从而只改目标区间、不破坏同页其它数据 */
void Flash_Write(u32 WriteAddr, u16 *pBuffer, u16 NumToWrite)
{
    u32 secpos;     /* 当前页序号（相对 Flash 基地址） */
    u16 secoff;     /* 目标在本页内的偏移，单位：半字 */
    u16 secremain;  /* 本轮写入长度，单位：半字（首轮取"页剩余容量"与"请求长度"的较小值） */
    u16 i;          /* 空白检查与数据合并的循环计数 */
    u32 offaddr;    /* 相对 Flash 基地址的字节偏移 */

    if (WriteAddr < STM32_FLASH_BASE || (WriteAddr >= (STM32_FLASH_BASE + 1024 * STM32_FLASH_SIZE))) return;   /* 越出片内 Flash 的地址直接丢弃 */

    FLASH_Unlock();                             /* 解锁：写寄存器序列被接受的前提 */
    offaddr = WriteAddr - STM32_FLASH_BASE;     /* 换算成片内偏移 */
    secpos = offaddr / STM_SECTOR_SIZE;         /* 起始页号 */
    secoff = (offaddr % STM_SECTOR_SIZE) / 2;   /* 页内字节偏移折算成半字数 */
    secremain = STM_SECTOR_SIZE / 2 - secoff;   /* 本页剩余可写半字数 */
    if (NumToWrite <= secremain) secremain = NumToWrite;    /* 请求不跨页时，本轮长度就是请求长度 */

    while (1)
    {
        Flash_Read(secpos * STM_SECTOR_SIZE + STM32_FLASH_BASE, s_sect_buf, STM_SECTOR_SIZE / 2);   /* 读回整页旧内容 */
        for (i = 0; i < secremain; i++)     /* 检查目标区间是否全为 0xFFFF（空白） */
        {
            if (s_sect_buf[secoff + i] != 0xFFFF) break;    /* 出现非空值 → 必须先擦除 */
        }
        if (i < secremain)                  /* 目标区间有旧数据：整页擦除后合入重写 */
        {
            FLASH_ErasePage(secpos * STM_SECTOR_SIZE + STM32_FLASH_BASE);   /* 擦除整页（内容全部回到 0xFFFF） */
            for (i = 0; i < secremain; i++)
            {
                s_sect_buf[i + secoff] = pBuffer[i];    /* 新数据覆盖进页缓冲的目标区间，页内其它数据保持读回值 */
            }
            Flash_WriteNoCheck(secpos * STM_SECTOR_SIZE + STM32_FLASH_BASE, s_sect_buf, STM_SECTOR_SIZE / 2);   /* 整页写回 */
        }
        else
        {
            Flash_WriteNoCheck(WriteAddr, pBuffer, secremain);  /* 目标区间已是空白：直接编程，省一次擦写损耗 */
        }
        if (NumToWrite == secremain) break;     /* 请求全部写完，收尾 */
        else                                    /* 还有剩余，切到下一页继续 */
        {
            secpos++;                           /* 页号 +1 */
            secoff = 0;                         /* 下一页从页首开始 */
            pBuffer += secremain;               /* 源指针滑过已写的半字数 */
            WriteAddr += secremain * 2;         /* 地址按字节推进：本轮写了 secremain 个半字 =
                                                   secremain*2 字节（2026-11-25 定案修正——
                                                   原工程写 `+= secremain`，把半字数当字节数推进，
                                                   跨页写时后续页落在错误地址，见 §8 定案记录） */
            NumToWrite -= secremain;            /* 剩余请求长度同步扣减 */
            if (NumToWrite > (STM_SECTOR_SIZE / 2)) secremain = STM_SECTOR_SIZE / 2;    /* 剩余仍超过一页：下一轮写满整页 */
            else secremain = NumToWrite;        /* 剩余不足一页：下一轮写完收尾 */
        }
    }
    FLASH_Lock();                               /* 重新上锁，防止后续代码误触发编程操作 */
}

#endif

/* 按半字连续读出：逐个取半字、地址按 2 字节推进 */
void Flash_Read(u32 ReadAddr, u16 *pBuffer, u16 NumToRead)
{
    u16 i;                                      /* 半字循环计数 */
    for (i = 0; i < NumToRead; i++)
    {
        pBuffer[i] = Flash_ReadHalfWord(ReadAddr);  /* 取一个半字 */
        ReadAddr += 2;                              /* 地址按 2 字节推进 */
    }
}
```

## 5. 本阶段验证 main.c（替换阶段 1 的 main，全文）

验证目标两个：**① 往 A2 区第 0 页写一页、读回、逐半字比对**——同时证明读、写（含擦除路径）、
分区地址三条链路都活；**② 跨页写**：从页中间起写 4KB（跨两个页边界、循环走三轮），
专验 `Flash_Write` 的多页推进——这正是原工程单位缺陷的病灶（§8 定案记录），下载固件必踩。
写的是 A2 暂存区，不碰 B/A1 区。
缓冲必须 static：验证合计用到 10KB（两页 2×1KB + 跨页 2×4KB），默认启动文件的栈只有 1KB，
放局部数组会溢栈。

```c
#include "stm32f10x.h"
#include "sys.h"
#include "delay.h"
#include "usart.h"
#include "led.h"
#include "stmflash.h"
#include "flash_layout.h"

/* 验证缓冲放 static：合计 10KB，远超默认栈容量，不能作局部数组 */
static u16 s_wbuf[ST32_PAGE_SIZE / 2];      /* 待写页：预填测试图案（单页测试用） */
static u16 s_rbuf[ST32_PAGE_SIZE / 2];      /* 读回页：与待写页逐半字比对（单页测试用） */
static u16 s_wbuf2[ST32_PAGE_SIZE];         /* 跨页测试待写缓冲：4KB = 2048 半字 */
static u16 s_rbuf2[ST32_PAGE_SIZE];         /* 跨页测试读回缓冲 */

int main(void)
{
    u32 i;          /* 页内半字循环计数 */
    u32 err_cnt;    /* 比对不一致的半字个数，0 = 通过 */
    u32 blink_cnt = 0;   /* 心跳计数：watch 窗口看它递增确认主循环在跑，printf 里同步输出 */
    u16 a1_before;  /* 验证前 A1 区首半字快照：证明写 A2 没波及 A1 */
    u16 a1_after;   /* 验证后 A1 区首半字快照 */

    delay_init();               /* SysTick 延时（阶段 1 已验证） */
    NVIC_Configuration();       /* 中断分组（阶段 1 已验证） */
    uart1_init(115200);         /* 调试串口：验证结果从 printf 输出 */
    LED_Init();                 /* 四路 LED（阶段 1 已验证） */
    LED_CLOSE();                /* 全灭基准态 */

    /* 预填测试图案：每个半字都不同，任何错位/漏写都会被比对抓到 */
    for (i = 0; i < (ST32_PAGE_SIZE / 2); i++)
    {
        s_wbuf[i] = (u16)(i * 2 + 1);
    }

    a1_before = Flash_ReadHalfWord(ST32_A1_SADDR);      /* 记录验证前 A1 首半字 */
    printf("flash test start: write A2 page0 @0x%08X\r\n", (unsigned int)ST32_A2_SADDR);

    Flash_Write(ST32_A2_SADDR, s_wbuf, ST32_PAGE_SIZE / 2);     /* 写满一页到 A2 第 0 页：页内含旧数据，走"擦除+重写"路径 */
    Flash_Read(ST32_A2_SADDR, s_rbuf, ST32_PAGE_SIZE / 2);      /* 读回整页 */

    err_cnt = 0;
    for (i = 0; i < (ST32_PAGE_SIZE / 2); i++)
    {
        if (s_rbuf[i] != s_wbuf[i])
        {
            err_cnt++;                              /* 记录不一致个数，不中断，一次看全 */
            if (err_cnt <= 8) printf("  diff @%u: want=%04X got=%04X\r\n", (unsigned int)i, (unsigned int)s_wbuf[i], (unsigned int)s_rbuf[i]);
        }
    }
    a1_after = Flash_ReadHalfWord(ST32_A1_SADDR);       /* 记录验证后 A1 首半字 */

    if (err_cnt == 0 && a1_before == a1_after) printf("flash test PASS (A1 untouched)\r\n");
    else printf("flash test FAIL: err=%u, a1 %04X -> %04X\r\n", (unsigned int)err_cnt, (unsigned int)a1_before, (unsigned int)a1_after);

    /* ── 测试 ②：跨页写（§8 定案的缺陷专测）──
     * 从 A2 第 0 页中间（字节偏移 1024）起写 2048 个半字 = 4KB：
     * 首轮只覆盖本页剩余 512 半字，随后两次跨过页 1、页 2 边界——
     * Flash_Write 内部循环走三轮，每轮的地址推进都被真实验证。
     * 若仍是原工程的半字推进，第 2 轮起地址落后于实际写入位置，
     * 读回比对会出现整段错位。 */
    for (i = 0; i < ST32_PAGE_SIZE; i++)
    {
        s_wbuf2[i] = (u16)(0xC000 + i);         /* 第二种图案：与测试 ① 区分 */
    }
    printf("flash cross-page test: 4KB from A2+0x400\r\n");
    Flash_Write(ST32_A2_SADDR + ST32_PAGE_SIZE / 2, s_wbuf2, ST32_PAGE_SIZE);
    Flash_Read(ST32_A2_SADDR + ST32_PAGE_SIZE / 2, s_rbuf2, ST32_PAGE_SIZE);

    err_cnt = 0;
    for (i = 0; i < ST32_PAGE_SIZE; i++)
    {
        if (s_rbuf2[i] != s_wbuf2[i])
        {
            err_cnt++;
            if (err_cnt <= 8) printf("  cross diff @%u: want=%04X got=%04X\r\n", (unsigned int)i, (unsigned int)s_wbuf2[i], (unsigned int)s_rbuf2[i]);
        }
    }

    if (err_cnt == 0) printf("flash cross-page PASS\r\n");
    else printf("flash cross-page FAIL: err=%u\r\n", (unsigned int)err_cnt);

    while (1)
    {
        delay_ms(500);                          /* 半秒节拍 */
        LED_ERR = !LED_ERR;                     /* 位带翻转 LED_ERR：完整周期约 1 秒，灯光心跳 */
        blink_cnt++;                            /* 心跳计数递增 */
        printf("skeleton alive, cnt=%u\r\n", (unsigned int)blink_cnt);  /* 串口心跳：与灯光同拍，肉眼核对两条链路 */
    }
}
```

注意：这个测试会**清掉 A2 第 0~2 页上的任何旧内容**（包括以前 OTA 下载留在那的镜像）。
本阶段这是预期行为；到阶段 11 做 BOOT 验证前，重新下载一次固件即可，无需特殊处理。

## 6. Keil 配置要点

| 配置项 | 值 | 说明 |
|---|---|---|
| 分组 | HARDWARE 组加 `stmflash.c` | flash_layout.h 是纯头文件，不加入编译清单 |
| Include Paths | 追加 `..\HARDWARE\STMFLASH` | flash_layout.h 与 stmflash.h 都在这个目录 |
| FWLib 勾选 | 增勾 `stm32f10x_flash.c` | `FLASH_Unlock/FLASH_ErasePage/FLASH_ProgramHalfWord/FLASH_Lock` 都在里面 |
| stm32f10x_conf.h | 确认包含 `#include "stm32f10x_flash.h"` | 原工程 conf.h 已包含，原样拷贝即可 |
| IROM1 | 保持 `0x08000000 / 0xE000` | 阶段 1 已按终态配好，本阶段不动 |

## 7. 验证清单（全部通过才算本阶段完成）

1. 编译 0 Error（`flash_layout_check` 断言随编译一起验证：分区规则有任何一条不成立，这一步就编不过）；
2. 串口收到 `flash test start ...`，随后 **`flash test PASS (A1 untouched)`**；
3. **`flash cross-page PASS`**——跨页写专测通过（原工程单位缺陷的回归验证，§8 定案记录；
   若报 FAIL 且 diff 呈整段错位，说明落盘的 stmflash.c 还是旧的 `WriteAddr += secremain`）；
4. 若报 FAIL：先看 diff 打印的地址偏移是否有规律（偏移整体错位 → 长度/地址参数问题；随机散布 → 硬件）；
5. PD14 心跳灯持续闪烁、`blink_cnt` 递增（验证循环仍活着）；
6. map 文件确认：`Flash_Write / Flash_Read` 已链接，`Test_Write / STMFLASH_*` 旧符号不存在。

## 8. 改名对照与跨阶段边界

**改名对照**（全工程引用已逐一核实）：

| 原名 | 新名 | 引用方 |
|---|---|---|
| `STMFLASH_ReadHalfWord` | `Flash_ReadHalfWord` | 仅本模块内部 + 头文件声明 |
| `STMFLASH_Write_NoCheck` | `Flash_WriteNoCheck`（**降为 static**） | 仅 stmflash.c 内部使用 |
| `STMFLASH_Write` | `Flash_Write` | boot.c（阶段 11）、gprs_rx.c（阶段 10） |
| `STMFLASH_Read` | `Flash_Read` | 仅 stmflash.c 内部（boot.c 经 Update_A1 间接使用） |
| `STMFLASH_BUF` | `s_sect_buf`（**降为 static**） | 仅 stmflash.c 内部 |
| `ota_layout_check` | `flash_layout_check` | 无运行期引用，纯编译期 |
| boot.h 的 `ST32_*` 分区宏 | 原名迁入 flash_layout.h | boot.c/boot.h/main.c（阶段 11）、gprs_rx.c（阶段 10）、本阶段 main.c |

**删除的死代码**（grep 证实零引用）：`Test_Write`（stmflash.c 无外部调用者）、
`STMFLASH_WriteLenByte / STMFLASH_ReadLenByte`（头文件声明了但从未实现）、
stmflash.c 的 `#include "delay.h"` / `#include "usart.h"`（文件内未使用）。

**缺陷定案记录（2026-11-25 跨模块审计，替代原「缺陷整改阶段统一处理」的搁置决定）**：

- **缺陷**：原工程 `Flash_Write` 跨页推进 `WriteAddr += secremain`——`secremain` 是半字数，
  按半字数递增**字节**地址，跨页写时第 2 轮起地址落后于实际写入位置（每跨一页错 1024 字节）；
- **危害实锤**：消费方 gprs_rx.c:695-699 按包写 A2（`s_dl_addr += Size * 2`，地址语义是字节），
  包大小任意、A2 固件下载 ~100KB 必然频繁跨页——原缺陷会让下载镜像整段错位，CRC 必挂；
  原 boot 工程的验证从未覆盖跨页写（单页验证侥幸通过），本 bootloader 差点原样继承；
- **定案**：修正为 `WriteAddr += secremain * 2`（§4 已改），并把跨页写验证并入本阶段
  验证 main（§5 测试 ②）作为回归用例——**阶段 10（GPRS 写 A2）开工的前置条件**；
- **落盘动作**：已落盘的 stmflash.c 同步改这一行（±1 处），重跑本阶段验证 main，
  两条 PASS（单页 + 跨页）后此缺陷销案。

**跨阶段边界**（对应《模块迁移顺序.md》§3）：

1. **留在 flash_layout.h（本阶段）**：`ST32_*` 分区宏 + 6 条 Flash 域断言；
2. **阶段 6 归 reg.h**：`OTA_ADDR`、`OTA_RETRY_ADDR / OTA_RETRY_MAX`、`OTA_InfoCB` 及字段偏移断言、
   `OTA_A1_FLAG / OTA_A2_FLAG`（OTA_flag 的取值语义属于 EEPROM 状态块，boot.c/reg.c/gprs_rx.c 都引用它）、
   原断言的第七条 `(OTA_RETRY_ADDR + 1) <= EE_ADDR`；
3. **阶段 11 boot.h 瘦身**：分区宏与断言从 boot.h 移除后，boot.h 需 `#include "flash_layout.h"`
   （`UpDataA_CB` 的缓冲长度仍用 `ST32_PAGE_SIZE`）；boot.c 里的 `STMFLASH_Read/Write` 调用点
   同步改为 `Flash_Read/Flash_Write`。

本阶段只生成文档，不改任何 `.c/.h` 工程文件；`flash_layout.h / stmflash.h / stmflash.c / main.c`
由你按本文档落盘。
