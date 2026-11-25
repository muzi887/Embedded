# main 重构方案（qibiji_boot → qibiji_bootloader）——正式 OTA 状态机（阶段 12）

> 本文档是迁移顺序第 12 阶段（main + 整机联调）的重构评审稿：**逻辑与原 boot main.c 逐行等价，
> 只做换名、删除死代码、接入各阶段已定案接口三件事**；另将验证阶段实机沉淀的分片喂狗模式
>（`s_WaitFeed`）带入正式 main。
>
> - 原始基线：`qibiji_boot/USER/main.c`（353 行）+ `USER/main.h`（OTA_STATE_* 状态码）
> - 新结构：`USER/main.c`（OTA 状态机本体）+ `USER/main.h`（瘦身：只留状态码与 g_ota_state 声明）
> - 第二信源：当前 `qibiji_bootloader/USER/main.c` 合并版验证 main（实机验证过的 `s_WaitFeed`
>   分片喂狗、开狗时序、喂狗即翻灯，见《LCD重构.md》§7）；各阶段文档定案接口
>   （REG / BOOT / 4G / LCD / WDG / flash_layout）
> - **接口占位声明**：阶段 10 GPRS 文档尚未生成，本稿按《模块迁移顺序.md》§2 已定案方向
>   （gprs.h 返回码改 `OTA_RX_*`）先行占位，函数名沿用 boot 原名；《GPRS重构.md》定稿后逐名核对
> - 本文档只生成/修订 md，不改任何 `.c/.h` 工程文件；落盘前提与联动见 §8

## 0. 注释风格约定

与《BOOT模块重构.md》§0 完全一致（全覆盖、全部新写、函数头 `//` 块、行内注、关键坑位警示、
文件内分组），不再重复。

## 1. 改名对照表

| 原名 | 新名 | 理由 |
|---|---|---|
| `JumpToApp()` | （删除包装）直接调 `Boot_JumpToA1((u32)(ST32_A1_SADDR))` | BOOT 文档定案 `LOAD_A → Boot_JumpToA1`；原包装只做转发，无附加值 |
| `LOAD_A(ST32_A1_SADDR)` | `Boot_JumpToA1((u32)(ST32_A1_SADDR))` | BOOT 文档 §1 |
| `IWDG_Init(4, 1990)` | `WDG_Init()` | WDG 阶段参数宏化（`WDG_PRESCALER=4 / WDG_RELOAD=1990`），不再裸传数字 |
| `IWDG_Feed()` | `WDG_Feed()` | 同上 |
| `AT24CXX_ReadOTAInfo()` | `Reg_ReadOtaInfo()` | 《REG重构.md》定案：状态块入口属 REG 模块（含三态自检分派） |
| `gprsonlineflag` | `GPRS_LINK_STATUS` | 《4G模块重构.md》定案：删除 ISR 维护标志，联网判断一律 PC5 电平宏直读 |
| `GM4G_Init()` | `Gprs4G_Init()` | 《4G模块重构.md》定案；含热重启保护（仅 LINK=0 才发 PA1 复位脉冲） |
| `Lcd_Gpio_Init()` + `CLEARGDRAMH(0x00)` | `LCD12864_Init()` | 《LCD重构.md》定案：GPIO + 1.5s 稳定期（内含 3 处喂狗）+ 指令序列 + 清两层 + 背光一步到位 |
| `BootLoader_Brance()` | `Boot_Branch()` | 《BOOT模块重构.md》：拼写订正（Brance→Branch）+ 前缀统一（Boot_） |
| `ota_show_error(kind)` | `Ota_ShowError(kind)` | 《LCD重构.md》：按功能命名 |
| `ota_step1`（标题 + 连接网络） | `Ota_ShowConnecting()` | 同上：按显示内容命名（「在线升级」+「连接服务器中」） |
| `ota_step2`（连接成功 + 申请版本） | `Ota_ShowRequestVer()` | 同上 |
| `ota_step3`（开始更新） | `Ota_ShowUpdating()` | 同上 |
| `ota_step4`（无需更新） | `Ota_ShowNoUpdate()` | 同上 |
| `ota_step5`（第 3 行「更新出错」，无类型） | `Ota_ShowErrorHint()` | 同上（要带具体类型用 `Ota_ShowError`） |
| `ota_step6`（完成更新） | `Ota_ShowDone()` | 同上 |
| `ota_step7`（四行清空） | `Ota_ClearText()` | 同上 |
| `progressbar_Init()` | `ProgressBar_Setup()` | 《LCD重构.md》定案 |
| `GPRS_RX_RET_*` / `GPRS_RX_ERR_*` | `OTA_RX_RET_*` / `OTA_RX_ERR_*` | 迁移顺序 §2.10 已定案：返回码改 OTA_RX_ 前缀（《GPRS重构.md》落地） |
| `OTA_STATE_*` 编号 | 重排为 0~4 连续 | 删除两个死状态后消除编号空洞；状态码仅在 main 内部使用、不落盘不进协议（grep 证实仅 main.c / main.h 引用），重排无外部影响 |
| `SYS_state` | `g_ota_state` | 语义实名化：变量跟踪的是 OTA 升级流程，不是"整个系统"；**g=global 是如实表达**——实查 boot 原版 gprs_rx.c 也在读写它（AT 握手窗口判定，gprs.h:102 extern），跨模块共享不能 static 化，故用 `g_` 而非 `s_`；extern 声明收归 main.h（gprs.h 不再重复声明） |

## 2. 死代码删除清单

1. **`OTA_STATE_REBOOT` 状态与 case 分支**：全工程无任何路径把 `g_ota_state` 置为 REBOOT——
   DOWNLOAD_DONE 路径直接 `NVIC_SystemReset()`，该分支永远进不去；复位前清下载上下文
   （`gprs_download_context_reset()`）的意图随分支一并删除（复位后 RAM 本就清零）；
2. **`OTA_STATE_MQTTCID_OK` + main.c 中注释掉的 MQTTCID 轮询块**：MQTT 参数由模块固件自行
   协商（4G 联测实测 `+MQTTCID:dev001` 由模块自带配置应答），MCU 不再发 MQTTCID；
3. **`at_tick / cid_tick / at_tick_max / cid_tick_max`**：随 MQTTCID 块废弃，全工程未使用
   （boot 分析总纲已确认）；
4. **`pub_version_flag`**（main.c:18）：只写不读（gprs_rx.c:794 置 0 但无任何读取方）——
   删除定义；gprs_rx.c 的 extern 与写入行随阶段 10 GPRS 清理；
5. **main.h 业务段**（`MOTOR_*`、`sys_moter_state`、`FLAG_*`、`Num_count` 等）：属 A1 应用，
   bootloader 不需要；
6. **`JumpToApp()` 包装函数**（见改名表）；
7. **`paraToEeprom()` 注释残留**（该函数已改名 `Reg_SeedDefaults`，见《REG重构.md》）：工厂播种已由 REG 阶段 `Reg_ReadOtaInfo()` 的三态自检
   （播种 / 故障 / 正常读取）接管；
8. **`// clearall();` 注释残留**。

## 3. `USER/main.h`（重构后全文）

```c
#ifndef __MAIN_H
#define __MAIN_H

#include "stm32f10x.h"

/* ---- OTA 状态机状态码（仅 bootloader 内部使用：不落盘、不进协议） ---- */
#define OTA_STATE_WAIT_NET      0   // 等 4G 模块 AT 握手应答
#define OTA_STATE_AT_OK         1   // AT 握手完成，等模块联网上线（LINK 变 1）
#define OTA_STATE_ONLINE        2   // 已联网，可发送版本询问
#define OTA_STATE_SEND_VERSION  3   // 已发 putVersion，等平台回包
#define OTA_STATE_HANDLE_REPLY  4   // 消费平台回包（版本应答 / 下载帧）
/* 原 OTA_STATE_REBOOT（无置位路径，死状态）与 OTA_STATE_MQTTCID_OK（MQTTCID 由模块
   固件自行协商）删除；原 REBOOT 的复位动作由 HANDLE_REPLY 的 DOWNLOAD_DONE 路径
   直接执行，语义不变。 */

extern u8 g_ota_state;   // OTA 状态机当前状态（OTA_STATE_* 之一；gprs_rx.c 握手判定经此声明访问）

#endif
```

## 4. `USER/main.c`（重构后全文）

> 占位接口（以《GPRS重构.md》定稿为准）：`gprs_rx_datas()`、`At_init()`、
> `cjson_pub_version()`、`gprs_rx_defer_pending()`、`gprs_download_pending()`、
> `gprs_resend_last_request()`、`gprs_download_context_reset()`、`OTA_RX_*` 返回码。
> `ST32_A1_SADDR` 取自 `flash_layout.h`（阶段 4 定案的分区宏属主）。

```c
// OTA 主状态机（bootloader 的最高层调度）
// 上电决策链：时基/串口 → EEPROM 状态块读入 → Boot_Branch 三分支（安装/自愈/返回）
// → 外设初始化 → 补显示引导期登记错误 → 升级闸判定（直接跳 A1 或进 OTA 状态机）。
// 状态机只在 UPDATE_flag=1 时运行：等 AT 握手 → 等联网 → 问版本 → 下载写 A2 → 复位进安装分支。

#include "stm32f10x.h"
#include "sys.h"          // NVIC_Configuration：中断分组
#include "delay.h"        // delay_init / delay_ms
#include "led.h"          // LED_Init / LED_CLOSE / LED_ERR 心跳灯
#include "usart.h"        // uart1_init（调试口 printf）+ uart2_init（4G 口）
#include "24cxx.h"        // AT24CXX_Init：I²C 引脚就绪
#include "reg.h"          // Reg_ReadOtaInfo + OTA_Info（状态块属主，REG 阶段产物）
#include "flash_layout.h" // ST32_A1_SADDR：跳转地址（分区宏属主，阶段 4 产物）
#include "4G.h"           // Gprs4G_Init / GPRS_LINK_STATUS
#include "wdg.h"          // WDG_Init / WDG_Feed / g_wdg_reset_cause
#include "boot.h"         // Boot_Branch / Boot_JumpToA1 / g_ota_lcd_err
#include "ds1302.h"       // ds1302_init：gprs_tx 的 JSON 时间戳依赖（不可省，见 §8）
#include "Display.h"      // Ota_Show* / OTA_ERR_*（LCD 阶段产物）
#include "ProgressBar.h"  // ProgressBar_Setup
#include "gprs.h"         // gprs_rx_datas / OTA_RX_* 返回码 / 会话恢复接口（阶段 10 产物，
                          // 函数名以《GPRS重构.md》定稿为准）
#include "main.h"         // OTA_STATE_* 状态码

u8 g_ota_state = OTA_STATE_WAIT_NET;   // OTA 状态机当前状态：上电先等 4G 握手
                                       // （全局：gprs_rx.c 握手窗口判定也读写它，见改名表）
int gprs_return;                     // gprs_rx_datas() 每轮返回码（OTA_RX_*）

/* ---- 分片喂狗等待（自合并版验证 main 沉淀，实机验证见《LCD重构.md》§7） ---- */

static u32 blink_cnt = 0;   // 心跳计数：每次喂狗 +1 并翻转 LED_ERR；程序卡死时灯 1~2s 内停住

// 函数功能：把长延时切成 ≤1s 的片，每片结束喂一次狗 + 翻灯
// 参    数：ms —— 等待总时长（毫秒）
// 说    明：单段 delay_ms(3000) 必超 IWDG 最坏窗口（2123ms，《WDG重构.md》）；
//           正式 main 里所有 ≥1s 的等待一律走本函数——等待即喂，不再逐处心算
static void s_WaitFeed(u16 ms)
{
    while (ms >= 1000u)
    {
        delay_ms(1000);
        WDG_Feed();
        LED_ERR = !LED_ERR;
        blink_cnt++;
        ms -= 1000u;
    }
    if (ms > 0u)
    {
        delay_ms(ms);
        WDG_Feed();
        LED_ERR = !LED_ERR;
        blink_cnt++;
    }
}

// 函数功能：OTA 放弃升级——显示错误类型 → 停留 3 秒 → 跳 A1 应用（不返回）
// 参    数：kind —— OTA_ERR_* 错误码（Display.h）
// 设计要点（沿用 boot 原版语义，名称按各阶段文档更新）：
//   ① 失败不改升级标志：UPDATE_flag 保持 1，平台/应用下次仍可继续升级；
//     「关升级闸」只保留给两个成功路径（安装校验通过、自愈成功，见 boot.c）；
//   ② 不复位、不写 EEPROM：复位会让 B 区重新进 OTA 状态机 → 再失败 → 再复位，
//     平台长期不可用时设备永远进不了应用（复位循环）；直接跳转没有这个问题；
//   ③ 3 秒停留用 s_WaitFeed 分片喂狗（原版 1500ms+喂+1500ms 的等价形式）；
//   ④ 代价：不复位 ⇒ Gprs4G_Init 的 PA1 脉冲不执行，4G 模组不被硬复位（与
//     「无需升级 → 跳 A1」路径一致）；USART1/2 由 Boot_Clear 复位、LCD 由
//     LCD12864_Clear 清屏，其余外设（DMA/EXTI/IWDG/SysTick）保持 B 区状态交 A1 初始化；
//   ⑤ Boot_JumpToA1 自带栈顶合法性保护：A1 不可运行时只打印失败并返回，
//     落回 while(1) 状态机继续联网重试（自愈语义保留）
static void OtaGiveUpAndRunApp(u8 kind)
{
    Ota_ShowError(kind);                    // 第3行"更新出错"、第4行具体类型
    s_WaitFeed(3000);                       // 停留 3 秒让现场看清原因
    Boot_JumpToA1((u32)(ST32_A1_SADDR));    // 跳 A1：不关升级闸、不复位；函数不返回
}

int main(void)
{
    u8 crc_fail_count = 0;               // CRC 连续失败计数（收到好包清零）
    const u8 crc_fail_max = 5;           // 上限：超过放弃升级
    u8 rx_err_count = 0;                 // 接收错误计数（收到正常返回清零）
    const u8 rx_err_max = 5;             // 上限：超过放弃升级
    u8 req_timeout_tick = 0;             // 等平台回包超时节拍（100ms/拍）
    u8 req_retry_count = 0;              // 请求超时重发次数
    const u8 req_timeout_tick_max = 50;  // 50 拍 × 100ms = 5s 无有效回包判超时
    const u8 req_retry_max = 5;          // 连续超时 5 次放弃升级
    u16 at_wait_tick = 0;                // AT 应答等待计数（300ms/拍）
    const u16 at_wait_max = 200;         // 200 拍 × 300ms = 60s 无 AT 应答判模块异常
    u8 online_tick = 0;                  // 等上线计数（200ms/拍；200 拍 = 40s 未上线放弃）
    u8 link_loss_count = 0;              // 升级过程掉线次数（会话级预算，START_UPDATE 清零）
    const u8 link_loss_max = 5;          // 掉线 5 次仍失败 → 放弃升级，引导旧程序

    delay_init();            // SysTick 时基
    NVIC_Configuration();    // 中断分组 2：2 位抢占 + 2 位响应
    uart1_init(115200);      // 调试口
    uart2_init(115200);      // 4G 口

    AT24CXX_Init();          // I²C 引脚就绪
    Reg_ReadOtaInfo();       // 读 OTA 状态块 → OTA_Info（含播种/故障三态分派）
    ds1302_init();           // 外部 RTC：gprs_tx 组 JSON 时间戳要用（§8 联动表）
    Boot_Branch();     // 三分支：安装 / 自愈 / 返回——前两者成功路径直接跳 A1 不返回

    LED_Init();              // LED 引脚初始化
    LED_CLOSE();             // 全灭，建立干净基准
    LCD12864_Init();         // GPIO + 1.5s 稳定期（内含 3 处 WDG_Feed）+ 清两层 + 背光；
                             // 原 Lcd_Gpio_Init + CLEARGDRAMH(0x00) 的等价替代
    Gprs4G_Init();           // EXTI + PA1 点火脉冲（热重启保护：仅 LINK=0 才脉冲）

    printf("bootloader is running!\r\n");

    /* 引导/EEPROM 检查阶段（LCD 就绪前）登记的 OTA 错误：在这里补显示。
       hold=1 → 停 3 秒再尝试运行应用；hold=0（如 EEPROM 异常）→ 只显示不等。 */
    if (g_ota_lcd_err != 0)
    {
        Ota_ShowError(g_ota_lcd_err);
        if (g_ota_lcd_hold)
        {
            s_WaitFeed(3000);                    // 此时尚未开狗，喂狗为无害写（统一等待规则）
            g_ota_lcd_err = 0;
            Boot_JumpToA1((u32)(ST32_A1_SADDR));
        }
        else
            g_ota_lcd_err = 0;
    }

    // 升级闸判定：UPDATE_flag=1 才进 OTA 状态机，否则直接进应用
    if (OTA_Info.UPDATE_flag == 1)
    {
        Ota_ShowConnecting();                // 第1行「在线升级」+ 第2行「连接服务器中」
        printf("OTA is running!\r\n");
    }
    else
    {
        printf("jump to a1!\r\n");
        Boot_JumpToA1((u32)(ST32_A1_SADDR)); // 正常路径：狗不开，外设清理交给 Boot_Clear
    }

    WDG_Init();   // 开狗：仅 OTA 状态机运行期。跳 A1 的路径到不了这里，IWDG 由 A1 应用
                  // 自己的 WDG_Init 接管；若在此前开狗，每次正常跳转都要靠跳转前喂狗续命
    printf("OTA state machine start, wdg reset cause=%u\r\n",
           (unsigned int)g_wdg_reset_cause);

    while (1)
    {
        WDG_Feed();                        // 每圈首喂；圈内长等待由 s_WaitFeed 分片覆盖
        gprs_return = gprs_rx_datas();     // 收包/解析（返回码 OTA_RX_*）

        /* 升级过程中掉线：计入会话级预算，重连后从在途 offset 重发请求
           （见 case OTA_STATE_ONLINE），掉线次数超限才放弃并引导旧程序。
           WAIT_NET / AT_OK 两态本身就是在等网络，不占用掉线预算。 */
        if (GPRS_LINK_STATUS == 0
            && g_ota_state != OTA_STATE_WAIT_NET && g_ota_state != OTA_STATE_AT_OK)
        {
            if (g_ota_state == OTA_STATE_SEND_VERSION || g_ota_state == OTA_STATE_HANDLE_REPLY)
            {
                link_loss_count++;
                printf("link lost during update, retry %d/%d\r\n",
                       link_loss_count, link_loss_max);
                if (link_loss_count >= link_loss_max)
                {
                    Ota_ShowErrorHint();           // 第3行"更新出错"（具体类型下面给）
                    printf("link lost too many times, boot old program\r\n");
                    OtaGiveUpAndRunApp(OTA_ERR_LINK);
                }
            }
            g_ota_state = OTA_STATE_WAIT_NET;    // 回到等网状态重新握手
            req_timeout_tick = 0;              // 在途请求预算作废（会随重连重新计）
            req_retry_count = 0;
            rx_err_count = 0;
            continue;
        }

        if (g_ota_state == OTA_STATE_WAIT_NET)
        {
            if (gprs_return == OTA_RX_RET_AT_OK)       // 模块应答 AT：进入等上线
            {
                g_ota_state = OTA_STATE_AT_OK;
                continue;
            }
            /* 模组无应答保护：未插卡 / 天线断 / 模组坏时不能无限等下去，
               否则应用永远起不来。60s 无 AT 应答 → 先尝试直接运行应用；
               若应用不可运行（Boot_JumpToA1 返回），约 30s 后再试一次。 */
            if (at_wait_tick >= at_wait_max)
            {
                printf("AT timeout\r\n");
                OtaGiveUpAndRunApp(OTA_ERR_NET);
                at_wait_tick = at_wait_max - 100;      // 返回后的冷却：再等 30s 才重判超时
                delay_ms(500);                         // 限速，避免空转刷屏
                continue;
            }
            at_wait_tick++;
            At_init();         // 周期性发 AT；应答经环形缓冲由 gprs_rx 解析回报
            delay_ms(300);
            continue;
        }

        if (g_ota_state == OTA_STATE_AT_OK)
        {
            if (GPRS_LINK_STATUS == 0)
            {
                delay_ms(200);
                online_tick++;
                if (online_tick >= 200)                // 40s 未上线：放弃联网，引导旧应用
                {
                    online_tick = 0;
                    Ota_ShowErrorHint();
                    printf("GPRS not online after waiting, give up, jump to A1\r\n");
                    OtaGiveUpAndRunApp(OTA_ERR_NET);
                }
                continue;
            }
            g_ota_state = OTA_STATE_ONLINE;              // LINK=1：MQTT 服务器已连接
            printf("GPRS is online!\r\n");
        }

        switch (g_ota_state)
        {
            case OTA_STATE_ONLINE:
                /* 掉线重连时若已暂存平台回包（putVersion 回执），直接消费：
                   不重新协商、不重发 putVersion（gprs_rx_datas 下一轮解析暂存帧） */
                if (gprs_rx_defer_pending() && !gprs_download_pending())
                {
                    printf("use stashed version reply\r\n");
                    Ota_ShowRequestVer();              // 「连接成功 / 申请最新固件」
                    g_ota_state = OTA_STATE_HANDLE_REPLY;
                    break;
                }
                /* 下载会话在途（例如掉线后重连）：直接重发在途 offset 请求，
                   不重新协商、不从 0 重来 */
                if (gprs_download_pending() && gprs_resend_last_request())
                {
                    printf("resume pending offset request\r\n");
                    g_ota_state = OTA_STATE_HANDLE_REPLY;
                }
                else
                {
                    Ota_ShowRequestVer();              // 发送版本询问前的画面
                    delay_ms(1000);
                    g_ota_state = OTA_STATE_SEND_VERSION;
                }
                break;

            case OTA_STATE_SEND_VERSION:
                cjson_pub_version();                   // 组 putVersion JSON 并发送
                g_ota_state = OTA_STATE_HANDLE_REPLY;
                break;

            case OTA_STATE_HANDLE_REPLY:
                if (gprs_return == OTA_RX_RET_NO_DATA
                    || gprs_return == OTA_RX_RET_PARSE_FAILED)
                {
                    /* 5s 无有效回包重发在途请求；连续 5 次放弃升级 */
                    req_timeout_tick++;
                    if (req_timeout_tick >= req_timeout_tick_max)
                    {
                        req_timeout_tick = 0;
                        req_retry_count++;
                        if (gprs_resend_last_request())
                        {
                            printf("request timeout, resend last request (%d/%d)\r\n",
                                   req_retry_count, req_retry_max);
                        }
                        else
                        {
                            printf("request timeout, no cached request (%d/%d)\r\n",
                                   req_retry_count, req_retry_max);
                        }
                        if (req_retry_count >= req_retry_max)
                        {
                            printf("request timeout too many times, give up, jump to A1\r\n");
                            OtaGiveUpAndRunApp(OTA_ERR_TIMEOUT);
                        }
                    }
                    delay_ms(100);
                    break;
                }
                else
                {
                    req_timeout_tick = 0;              // 收到有效回包：超时预算清零
                    req_retry_count = 0;
                }

                if (gprs_return == OTA_RX_ERR_CRC_RETRY_EXCEEDED)
                {
                    crc_fail_count++;
                    Ota_ShowError(OTA_ERR_DATA);       // 每次 CRC 失败都提示类型（不延时）
                    printf("crc failed, retry=%d/%d\r\n", crc_fail_count, crc_fail_max);
                    if (crc_fail_count >= crc_fail_max)
                    {
                        printf("crc failed too many times, give up, jump to A1\r\n");
                        OtaGiveUpAndRunApp(OTA_ERR_DATA);
                    }
                    break;
                }
                else if (gprs_return == OTA_RX_RET_DOWNLOAD_CONTINUE)
                {
                    crc_fail_count = 0;                // 当前包写入成功：CRC 失败计数清零
                }

                if (gprs_return < 0
                    && gprs_return != OTA_RX_RET_NO_DATA
                    && gprs_return != OTA_RX_ERR_CRC_RETRY_EXCEEDED)
                {
                    rx_err_count++;
                    Ota_ShowError(OTA_ERR_DATA);       // 每次接收错误都提示类型（不延时）
                    printf("rx error=%d, retry=%d/%d\r\n",
                           gprs_return, rx_err_count, rx_err_max);
                    if (rx_err_count >= rx_err_max)
                    {
                        printf("rx error too many times, give up, jump to A1\r\n");
                        OtaGiveUpAndRunApp(OTA_ERR_DATA);
                    }
                    break;
                }
                else if (gprs_return >= 0)
                {
                    rx_err_count = 0;                  // 正常返回：接收错误计数清零
                }

                if (gprs_return == OTA_RX_RET_NO_UPDATE)       // 平台：已是最新版本
                {
                    crc_fail_count = 0;
                    Ota_ShowNoUpdate();                // 「无新版本」
                    delay_ms(1000);
                    Boot_JumpToA1((u32)(ST32_A1_SADDR));   // 进应用（不返回）
                }
                if (gprs_return == OTA_RX_RET_START_UPDATE)    // 平台：有新固件，开始下载
                {
                    crc_fail_count = 0;
                    link_loss_count = 0;               // 新一轮下载会话：重置掉线预算
                    Ota_ShowUpdating();                // 「开始更新」
                    ProgressBar_Setup();               // 清图形层 + 底框 + 0%
                }
                if (gprs_return == OTA_RX_RET_DOWNLOAD_DONE)   // 全部包收完且逐包校验过
                {
                    crc_fail_count = 0;
                    Ota_ShowDone();                    // 「更新完毕」
                    delay_ms(1000);
                    Ota_ClearText();                   // 四行清空（原 ota_step7）
                    delay_ms(500);
                    NVIC_SystemReset();                // 复位 → 下次上电走 Branch 安装分支
                }
                /* OTA_RX_RET_DOWNLOAD_CONTINUE（下载进行中）无附加动作 */
                break;
        }
    }
}
```

## 5. 状态机流转与超时预算

```text
上电 → 基础初始化 → Reg_ReadOtaInfo → Boot_Branch
  ├─ 安装/自愈成功 → Boot_JumpToA1（不返回）
  └─ 返回 → LED/LCD/4G 初始化 → 补显示 g_ota_lcd_err（hold=1 则停 3s 后跳 A1）
       ├─ UPDATE_flag=0 → Boot_JumpToA1（狗不开）
       └─ UPDATE_flag=1 → WDG_Init → 进入状态机
             WAIT_NET ──AT_OK 回包──→ AT_OK ──LINK=1──→ ONLINE
             ONLINE → SEND_VERSION → HANDLE_REPLY
             HANDLE_REPLY：NO_UPDATE → 跳 A1；START_UPDATE → 下载（进度条）；
                           DOWNLOAD_DONE → 复位 → 安装分支
             任意在途态掉线 → WAIT_NET（掉线预算保留至 START_UPDATE 重置）
             各类超限 → OtaGiveUpAndRunApp → 显示错误 3s → 跳 A1
```

| 状态 | 出口 | 超时 / 重试预算 |
|---|---|---|
| WAIT_NET | AT_OK 回包 → AT_OK | 60s 无 AT 应答 → 放弃进 A1（OTA_ERR_NET）；A1 不可运行则约 30s 后再试 |
| AT_OK | LINK=1 → ONLINE | 40s 未上线 → 放弃进 A1（OTA_ERR_NET） |
| ONLINE | 发版本询问 → SEND_VERSION；有暂存回包/在途请求 → HANDLE_REPLY | — |
| SEND_VERSION | 发 putVersion → HANDLE_REPLY | — |
| HANDLE_REPLY | NO_UPDATE / START_UPDATE / DOWNLOAD_DONE / 继续等 | 5s 无有效回包重发、连续 5 次放弃（OTA_ERR_TIMEOUT）；CRC 连续 5 次放弃（OTA_ERR_DATA）；接收错误连续 5 次放弃（OTA_ERR_DATA）；掉线 5 次放弃（OTA_ERR_LINK） |

掉线重入 WAIT_NET 时清零的预算：`req_timeout_tick / req_retry_count / rx_err_count`；
不清零的预算：`link_loss_count`（会话级，START_UPDATE 才重置）。

## 6. 与合并版验证 main 的差异

| 项 | 合并版验证 main（现状） | 正式 main（本文档） |
|---|---|---|
| 循环体 | LCD 四幕演示 | OTA 状态机（boot 原版逻辑） |
| WDG_Init 时机 | LCD12864_Init 之前 | 升级闸判定之后（跳 A1 路径不开狗，与 boot 原版一致） |
| 心跳观测 | `lcd round N` / `4G alive, cnt=N` 打印 | 状态机事件打印 + `s_WaitFeed` 喂狗即翻灯（`blink_cnt` 保留） |
| `g_gprs_online` 引用 | 有（`online=%u` 打印） | 无（4G 文档定案删除该变量，联网判断走 `GPRS_LINK_STATUS`） |
| `s_WaitFeed` | 有（实机验证） | 保留，原样带入 |
| 四幕演示代码 / `percent` | 有 | 删除（验证完成，使命结束） |
| 新增 | — | `OtaGiveUpAndRunApp`、状态机、引导期错误补显示、`ds1302_init` |

启动顺序对比：boot 原版为 `AT24CXX → ds1302 → Branch → LED → Lcd_Gpio_Init → GM4G_Init`；
新版为 `AT24CXX → Reg_ReadOtaInfo → ds1302 → Branch → LED → LCD12864_Init → Gprs4G_Init`。
**正常启动比 boot 原版多约 1.6s**（LCD 1.5s 稳定期 + 4G 条件脉冲），是 LCD/4G 阶段定案的
代价，换来引导期错误可上屏显示。

## 7. 验证方法（端到端 OTA + 分支覆盖）

前提：§8 落盘前提全部满足后编译烧录。

1. **正常跳转路径（UPDATE_flag=0）**：上电 → 串口 `bootloader is running!` → `jump to a1!` →
   A1 应用跑起来；串口**不应**出现 `OTA state machine start`（狗没开）；
2. **完整 OTA（UPDATE_flag=1 + 服务器可用）**：`OTA is running!` → 屏显「在线升级/连接服务器中」
   → `GPRS is online!` → 「连接成功/申请最新固件」→ 平台下发 → 进度条 0→100 → 「更新完毕」→
   复位 → 串口 `loading progarm file A1` → `write progarm file success` → 新版本运行；
3. **服务器不可用**：WAIT_NET 60s 超时或 AT_OK 40s 超时 → 屏显「更新出错 + 网络连接超时」
   → 3 秒后进 A1 应用；
4. **掉线预算**：下载中途关服务器，反复 5 次 → 屏显「更新出错 + 连接中断」→ 进 A1；
5. **看门狗**：状态机运行中注释循环顶 `WDG_Feed()`（此时无长 s_WaitFeed 在跑才能观察）→
   约 3s 复位 → 重启后 `wdg reset cause=1`；
6. **观察点**：正常路径（闸=0）总耗时比 boot 原版多约 1.6s，见 §6 说明，属预期。

## 8. 落盘前提与跨文档联动

**硬前置：阶段 10《GPRS重构.md》与阶段 11《BOOT模块重构.md》落盘完成后才能编译本 main**——
`gprs.h`（OTA_RX_* 返回码 + 会话接口）、`boot.c/ota_copy.c`（Branch/JumpToA1）都还未落盘。

| 依赖 | 属主文档 | 状态 |
|---|---|---|
| wdg.h/.c（WDG_Init/Feed/复位来源） | 《WDG重构.md》 | ✅ 已落盘并实机验证 |
| flash_layout.h（ST32_A1_SADDR） | 《STMFLASH与分区布局重构.md》 | ✅ 已落盘 |
| Display.h/.c、ProgressBar.h/.c、Lcd12864.h/.c | 《LCD重构.md》 | ✅ 已落盘 |
| 4G.c 条件脉冲版（Gprs4G_Init/GPRS_LINK_STATUS，删 g_gprs_online） | 《4G模块重构.md》§3 | ⬜ 待更新（当前落盘版为无条件脉冲 + ISR 维护标志；本文档假定新版接口） |
| reg.h/reg.c（Reg_ReadOtaInfo/OTA_Info 属主） | 《REG重构.md》 | ⬜ 待核对落盘状态 |
| boot.c/ota_copy.c（Branch/JumpToA1/g_ota_lcd_err） | 《BOOT模块重构.md》 | ⬜ 待落盘 |
| gprs.h/gprs_tx/gprs_frame/gprs_session/gprs_rx（OTA_RX_* 返回码） | 《GPRS重构.md》（未生成） | ⬜ 阶段 10 |
| ds1302.c（gprs_tx 时间戳依赖） | 迁移顺序阶段 7「按需」 | ⚠️ 实查 gprs_tx.c 用 `ds1302_dataread` 组 JSON 时间戳，**是 GPRS 硬依赖而非可选项**，建议在《GPRS重构.md》中一并定案入库 |

| 其它文件 | 位置 | 改动 |
|---|---|---|
| 《BOOT模块重构.md》boot.c | `OTA_InfoCB OTA_Info;` 定义行 | 落盘时**删除该定义行**：`OTA_Info` 属主已归 reg.c（《REG重构.md》），extern 走 reg.h，否则重复定义 |
| 《模块迁移顺序.md》 | 阶段 12 行 | 指向本文档（本次同步更新） |
| gprs_rx.c（随阶段 10） | `extern int pub_version_flag` 及写入行 | 死变量，随《GPRS重构.md》清理 |
| Keil 工程 | USER 组 | 无新增文件（main.c/main.h 已在工程内）；ds1302.c 待阶段 10 决定是否加入 |
