# GPRS 模块重构方案（qibiji_boot → qibiji_bootloader）——阶段 10

> 本文档是迁移顺序第 10 阶段（GPRS）的重构评审稿：**OTA 下载链路与原 boot 工程逐行等价**，
> 只做换名、删除死代码、调整 include 归属三件事。
>
> - 原始基线：`qibiji_boot/HARDWARE/GPRS/`——`gprs.h`（153 行）、`gprs_tx.c`（230 行）、
>   `gprs_rx.c`（1120 行）；**`gprs_AT.c`（173 行）不在 Keil 工程内**（`BOOTV10.uvprojx`
>   无此条目、`boot.map` 无 `gprs_at.o`），不迁移（见 §2.1）
> - 新结构：同名 3 文件落盘到 `qibiji_bootloader/HARDWARE/GPRS/`
> - 第二信源结论：zxl 的 GPRS 目录是另一套「AT 命令 + 业务上报」体系
>   （gprs_4g / gprs_at / gprs_json / config_store / event_pub 等），**没有 OTA 下载链路**——
>   boot 版是唯一实现，正文以 boot 版为源；zxl 可取的部分已分别在
>   《4G模块重构.md》（gprs_4g 条件复位/热重启保护）与《USART重构.md》（uart_queue 环形缓冲、
>   溢出计数）吸收完毕，本阶段无新增可吸收项
> - **接口对接已逐名核对**：USART2 环形缓冲 API（`Usart2_Send/RxClear/RxAvailable/RxCopy/
>   RxConsume/RxOverflowCount`）与《USART重构.md》完全同名，无改名；`AT24CXX_Write/AT24CXX_OK`
>   与《EEPROM硬件访问层重构.md》同名同签名；`OTA_Info/OTA_ADDR/OTA_OFF_*/OTA_A1_FLAG/
>   OTA_A2_FLAG/g_mqtt_productKey/g_mqtt_deviceKey` 按《REG重构.md》从 reg.h 取；
>   `ST32_A2_SADDR` 等分区宏按阶段 4 定案从 `flash_layout.h` 取
> - **阶段 9 叶子（cJSON / mymalloc / DS1302）随本阶段入库**，定案三处修订（cJSON realloc
>   钩子对齐内存池、malloc.c 改名 mymalloc.c、DS1302 宏改名/static 化），详见《叶子模块重构.md》；
>   CRC16 仍原样入库（清单见 §9 附录）
> - 本文档只生成/修订 md，不改任何 `.c/.h` 工程文件

## 0. 注释风格约定

与《BOOT模块重构.md》§0 完全一致（全覆盖、全部新写、函数头 `//` 块、行内注、关键坑位警示、
文件内分组），不再重复。gprs_rx.c 现存注释已是新风格（此前轮次重写过），保持并补齐缺头注释。

## 1. 模块结构与信源判定

| boot 文件 | 判定 | 理由 |
|---|---|---|
| `gprs.h` | 迁移（瘦身） | 死声明 14 个 + 死宏 8 个，见 §2 |
| `gprs_tx.c` | 迁移 | 时间戳/消息 id/信封组装/在途请求缓存，全是活代码 |
| `gprs_rx.c` | 迁移 | OTA 接收核心：取帧/解析/写 A2/状态块落盘 |
| `gprs_AT.c` | **不迁移（死文件）** | 不在 Keil 工程内（map 无 `gprs_at.o`）；`handleAT` 写不存在的 `CParas.clientid` 成员、引用未定义的 `xinhao/ccid/order` 数组——脱离工程后已与结构体定义脱节，编译必炸。其唯一活能力（发 AT）由 gprs_tx.c 的 `At_init()` 承担；AT 应答识别由 gprs_rx.c 的 `gprs_rx_consume_at_ok()` 承担 |
| `HARDWARE/CRC16/crc16.c/h` | 原样入库（阶段 9 叶子） | `CRC16_CCITT_FALSE`，固件块校验 |
| `HARDWARE/cJSON/cJSON.c/h` | 入库 + realloc 钩子补丁（阶段 9 叶子） | 内存钩子已挂 mymalloc；realloc 仍指标准库，须改 `myrealloc`（见《叶子模块重构.md》§3） |
| `HARDWARE/MALLOC/malloc.c + mymalloc.h` | 入库 + 改名 `mymalloc.c`（阶段 9 叶子） | cJSON 的内存池；文件名与头文件对齐，内部 static 化（见《叶子模块重构.md》§2） |
| `HARDWARE/DS1302/ds1302.c + ds1302.h` | 入库 + 清理（阶段 9 叶子） | gprs_tx 时间戳唯一数据源；宏改名 + static 化（见《叶子模块重构.md》§4） |

## 2. 死代码删除清单

1. **`gprs_AT.c` 整文件**：见 §1 表；
2. **gprs.h 死声明 14 个**（逐个 grep 核实）：`Gprs_Process / gprs_check / gprs_tx /
   handleMessage / cjson_enquiry / cjson_setting / cjson_version / cjson_sever_reply /
   cjson_cmd`（无实现或无调用）、`CID_init`（唯一调用在 main 注释块）、`generate_random_string /
   get_lasttime`（唯一调用是注释行）、`validate_server_reply_json`（160 行实现、零调用）、
   `str_contains_wh_lte_7s1`（零调用）、`extern u8 atflag`（只写不读）；
3. **gprs_rx.c**：`rxparagate`（零引用）、`atflag` 定义（读方是死文件 gprs_AT.c）、
   `extern int pub_version_flag` 与 `cjson_pub_version` 里的 `pub_version_flag = 0;`
   （《Main重构.md》死代码清单第 4 条的另一半）、`validate_server_reply_json` 及其
   -21~-41 错误码注释块、`str_contains_wh_lte_7s1`；
4. **gprs_tx.c**：`CID_init`、`get_lasttime`、`generate_random_string`、`At_init` 里的
   `atflag = 0;`、`gprs_send_datas` 里未使用的 `num`（=1001）/`test`（=15.52）局部变量与
   `LastTime` 数组、`extern OTA_InfoCB OTA_Info`（本文件正文未引用 OTA_Info，声明多余）；
5. **宏**：`REPLAY / ENQUIRY / SETTINH / WARNING / Topic_Sub / Topic_Pub`（无发送路径的
   遗留类型/主题；gprs_send_datas 的空体 WARNING case 随之删除）、`bts / boffset`
   （字段长度宏——ts 已是 int64_t 非 char 数组、offset 已是 u16，均不再按串长度使用）、
   `GPRS_RX_RET_MQTTCID_OK`（main 已不问 MQTTCID，《Main重构.md》死代码 2）、
   `GPRS_RX_RET_NET_ONLINE`（无生成点）、`GPRS_RX_ERR_JSON_MIN / GPRS_RX_ERR_JSON_MAX`
   （属已删的 validate_server_reply_json）；
6. **注释残留**：`cjson_pub_version / cjson_pub_getbin / cjson_pub_putError` 的大段
   clientid/random 时代注释块、`cjson_upgrade / cjson_download_reply` 里被注释掉的
   validate 调试块。

## 3. 改名对照表

| 原名 | 新名 | 理由 |
|---|---|---|
| `GPRS_RX_RET_*` / `GPRS_RX_ERR_*` | `OTA_RX_RET_*` / `OTA_RX_ERR_*` | 迁移顺序 §2.10 定案：返回码改 OTA_RX_ 前缀；**数值逐一保持不变**（《Main重构.md》§4 按名引用） |
| `STMFLASH_Write` | `Flash_Write` | 《STMFLASH与分区布局重构.md》定案（gprs_rx.c 1 处，写 A2）。**硬前置**：下载按包推进字节地址必然跨页，必须先落盘该文档 §4/§8 的跨页写单位修正，否则镜像整段错位 |
| `progressbar_Init()` | `ProgressBar_Setup()` | 《LCD重构.md》定案（gprs_rx.c 1 处） |
| `boot.h`（include） | `reg.h` + `flash_layout.h` | include 归属调整：`OTA_Info/OTA_A*_FLAG/OTA_ADDR/OTA_OFF_*` 归 reg.h（迁移顺序 §3.1 定案），`ST32_*` 分区宏归 flash_layout.h（阶段 4 定案）；gprs_rx.c 不再 include boot.h |
| `display.h` / `led.h`（include） | （删除） | gprs_tx/gprs_rx 正文无引用（进度条经 ProgressBar.h、错误显示由 main 承担） |
| `extern u8 SYS_state`（gprs.h） | 改名 `g_ota_state`，声明收归 main.h | 《Main重构.md》改名表定案：语义实名化（OTA 流程状态，非"系统状态"）；gprs_rx.c 握手窗口判定仍经 main.h 的 extern 访问，gprs.h 不再重复声明 |

**不改名的接口**（与已落盘文档逐名核对过）：`Usart2_*` 六个环形缓冲 API（USART 阶段）、
`AT24CXX_Write/AT24CXX_OK`（EEPROM 阶段）、`Flash_Read`（STMFLASH 阶段）、
`ProgressBar_Setup/Update`、`LCD12864_*`、`WDG_Feed`、`CRC16_CCITT_FALSE`、
`cJSON_Parse/cJSON_GetObjectItem/...`、`ds1302_dataread`。

## 4. `HARDWARE/GPRS/gprs.h`（重构后全文）

```c
#ifndef __GPRS_H
#define __GPRS_H

#include "stm32f10x.h"
#include "cJSON.h"      // 阶段 9 叶子：JSON 解析（内存钩子挂 mymalloc）
#include "mymalloc.h"   // cJSON 的内存池
#include "reg.h"        // OTA_Info / OTA_A*_FLAG / OTA_ADDR / OTA_OFF_* / g_mqtt_*（REG 属主）
#include "main.h"       // OTA_STATE_* / g_ota_state（gprs_rx 握手窗口判定用）
#include "4G.h"         // GPRS_LINK_STATUS：联网电平宏
#include "stmflash.h"   // Flash_Write：固件块写 A2
#include "24cxx.h"      // AT24CXX_Write / AT24CXX_OK：OTA 状态块落盘
#include "ds1302.h"     // ds1302_dataread：时间戳源（gprs_tx 组信封 ts）
#include <string.h>
#include <stdlib.h>

/* ---- 报文类型（发送侧 switch 用）：OTACode 的取值在 pub_* 函数内填写 ---- */
#define SVERSION  5    // 版本号上报（OTACode = putVersion）
#define UPGRADE   4    // 请求固件块（OTACode = getFirmware）
#define PUTERROR  6    // OTA 结果回报（OTACode = otaError）
/* REPLAY/ENQUIRY/SETTINH/WARNING/Topic_* 删除：无发送路径的遗留类型/主题宏（§2.5） */

#define BYTESIZE  512  // 固件分包大小（字节/包）

/* 信封字段长度（含 '\0'）：bts/boffset 删除——ts 已是 int64_t、offset 已是 u16（§2.5） */
#define bid          16    // id（13 位毫秒时间戳字符串）
#define bver         16    // 协议版本（固定 "v1.0"）
#define bproductKey  32    // 产品标识（24C02 MQTT 键，与 A1 应用同源）
#define bdeviceKey   32    // 设备标识（同上）
#define btype        8     // 报文大类（固定 "OTA"）
#define bOTACode     16    // 具体命令码
#define bversion     16    // 固件版本串
#define bcode        8     // 结果码（putError）
#define bmessage     32    // 结果消息（putError）

/* ---- gprs_rx_datas() 返回码：前缀改 OTA_RX_，数值与 boot 版 GPRS_RX_* 一一对应 ----
   非负值：状态机迁移信号（main 按名判定）；负值：透传的错误码，由 main 计数。
   删除 MQTTCID_OK / NET_ONLINE / JSON_MIN / JSON_MAX，见 §2.5。 */
#define OTA_RX_RET_PARSE_FAILED          0    // 帧无效 / JSON 解析失败（main 按超时计）
#define OTA_RX_RET_NO_UPDATE             1    // 平台判定无需升级
#define OTA_RX_RET_START_UPDATE          2    // 开始升级（size/ver 已填 OtaParas）
#define OTA_RX_RET_DOWNLOAD_DONE         3    // 全部包收完且收尾校验通过
#define OTA_RX_RET_DOWNLOAD_CONTINUE     4    // 一包写完，继续下一包
#define OTA_RX_RET_AT_OK                 5    // AT 握手应答（仅 WAIT_NET/AT_OK 态产出）
#define OTA_RX_RET_NO_DATA            -100    // 本轮无数据（常态返回）

/* 负值错误码：cjson_upgrade / cjson_download_reply 透传 */
#define OTA_RX_ERR_CRC_RETRY_EXCEEDED  -19   // CRC 连续失败重发已到上限（5 次）
#define OTA_RX_ERR_EEPROM_WRITE        -20   // 状态块落盘失败
#define OTA_RX_ERR_PACKET_SHORT        -42   // 帧比声明的包长短
#define OTA_RX_ERR_PACKET_SIZE         -43   // 单包长度非法（0 或超过 512）
#define OTA_RX_ERR_LENGTH_OVERFLOW     -44   // 累计写入将越过协议声明的固件大小
#define OTA_RX_ERR_SEQ_RETRY_EXCEEDED  -45   // 包序号不符重发超限（重复/乱序包）

/* putVersion 回包解析出的固件信息：升级全程的依据（包数换算/收尾校验） */
typedef struct {
    u32  size;              // 固件总字节数（平台声明）
    char ver[32];           // 固件版本号（末包写入 OTA_ver 落盘）
    char signMethod[16];    // 签名方法（协议保留字段，当前未用）
    char dProtocol[16];     // 传输协议（协议保留字段，当前未用）
    u32  streamId;          // MQTT 传输流标识（协议保留字段，当前未用）
    u16  count;             // 总包数（按 512 字节/包向上取整）
    u16  remaining;         // 末包实际字节数
} OtaPara;

/* 发送信封字段容器：pub_* 函数填写，gprs_send_datas 组包发送，resend 缓存整份 */
typedef struct {
    char id[bid];               // 消息 id（13 位毫秒时间戳，每次发送/重发刷新）
    char ver[bver];             // 协议版本
    int64_t ts;                 // 13 位 Unix 毫秒时间戳（每次发送/重发刷新）
    char productKey[bproductKey];
    char deviceKey[bdeviceKey];
    char type[btype];           // 固定 "OTA"
    char OTACode[bOTACode];     // putVersion / getFirmware / otaError
    char version[bversion];     // 上报的已安装版本
    char code[bcode];           // putError 结果码
    char message[bmessage];     // putError 结果消息
    u16 offset;                 // 固件块序号（getFirmware 用）
} CJsonPara;

extern OtaPara    OtaParas;   // 固件信息（rx 填写，download_reply 消费）
extern CJsonPara  CParas;     // 发送信封（tx 组包消费）

/* ---- 接收（gprs_rx.c） ---- */
int  gprs_rx_datas(void);                // 每轮取一个完整帧并分派处理，返回 OTA_RX_*
void gprs_rx_frame_reset(void);          // 丢弃未完成帧：清环形缓冲 + 复位等待状态
u8   gprs_rx_defer_pending(void);        // 掉线窗口是否暂存了 putVersion 回包
u8   gprs_download_pending(void);        // 是否有一轮下载在途（收到过包且未收尾）
void gprs_download_context_reset(void);  // 清零下载上下文（写指针/序号/重试计数/暂存回包）
int  cjson_upgrade(cJSON *root);         // putVersion 回包处理：升级判定 + 包数换算
u8   cjson_download_reply(cJSON *root, char *message, u16 message_len);  // 固件块处理
char *find_json_end_by_bracket_matching(char *start, u16 max_len);       // JSON 括号匹配
u8   str_contains(const char *str, const char *substr);                  // 子串检查

/* ---- 发送（gprs_tx.c） ---- */
void At_init(void);                      // 发 AT 握手命令（等网阶段周期调用）
void cjson_pub_version(void);            // 上报已安装版本（putVersion）
void cjson_pub_getbin(u16 offset);       // 请求固件块（getFirmware）
void cjson_pub_putError(void);           // OTA 结果回报（otaError）
void gprs_send_datas(u8 sta, CJsonPara para);  // 信封组包 + id/ts 刷新 + 缓存 + 发送
int  gprs_resend_last_request(void);     // 重发最近一次缓存请求（无缓存返回 0）
int64_t gprs_unix_ms_now(void);          // 13 位 Unix 毫秒时间戳（DS1302 日历推出）
void gprs_next_msg_id(char *buf, u16 buflen);  // 生成本次发送的消息 id（保证递增不重复）

#endif
```

## 5. `HARDWARE/GPRS/gprs_tx.c`（重构后全文）

```c
// GPRS 发送模块：信封组装 + USART2 发出 + 在途请求缓存
// cjson_pub_* 负责填信封业务字段；gprs_send_datas 负责刷新 id/ts、缓存最近一次
// 可重发请求并实际发送；掉线/超时后的重发由 gprs_resend_last_request 消费缓存完成。

#include "usart.h"      // Usart2_Send：4G 口发送；printf：调试输出
#include "gprs.h"       // CJsonPara / 消息类型宏 / gprs_next_msg_id / gprs_rx_frame_reset
#include <stdio.h>      // sprintf / snprintf / snprintf 的 %llu

/* ---- 消息标识：id 与 ts 每次发送（含超时重发）都重新生成 ----
   协议要求 id 在单设备时间窗内不重复、平台回包原样带回该 id（升级记录里的 MsgId 也取它）；
   A1 应用特意用"新的时间戳 id"重发，就是为了不让平台把重发当成同一条请求。
   原实现把 id 写死 "test-001"、ts 写死 1779771714791，5 次超时重发内容完全一样，
   平台侧既无法区分重发，升级记录也全是同一个 MsgId。 */
#define GPRS_UNIX_BASE_2000  946684800UL   /* 2000-01-01 00:00:00 UTC */
#define GPRS_TZ_OFFSET_SEC   28800UL       /* DS1302 存东八区本地时，换算 UTC 用 */

/* ---- 在途请求缓存：版本请求 / 下载请求可重发 ---- */
static u8 g_last_sta = 0;        // 缓存的消息类型（SVERSION / UPGRADE）
static CJsonPara g_last_para;    // 缓存的完整信封字段
static u8 g_last_valid = 0;      // 1 = 缓存有效（resend 的前提）

// 函数功能：DS1302 日历（ds1302_dataread 已转十进制）→ Unix 秒
// 返 回 值：合法日历返回对应 Unix 秒；字段越界（RTC 未对时/损坏）返回 0
// 换算规则：2000..2099 年能被 4 整除即闰年（该区间不涉及 100/400 年规则）
static u32 Gprs_UnixSecNow(void)
{
    static const u8 dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    char t[7];
    u32 days = 0, local_sec;
    u8 year, month, day, hour, min, sec, leap;
    int y, m;

    ds1302_dataread(t);
    year  = (u8)t[6];
    month = (u8)t[4];
    day   = (u8)t[3];
    hour  = (u8)t[2];
    min   = (u8)t[1];
    sec   = (u8)t[0];

    if (month < 1u || month > 12u) return 0;
    if (day < 1u || day > 31u) return 0;
    if (hour > 23u || min > 59u || sec > 59u) return 0;

    for (y = 0; y < (int)year; y++)                       // 累加整年天数
        days += (u32)(((y & 3) == 0) ? 366 : 365);
    leap = ((year & 3u) == 0u) ? 1u : 0u;
    for (m = 1; m < (int)month; m++)                      // 累加整月天数
    {
        days += dim[m - 1];
        if (m == 2) days += leap;
    }
    days += (u32)(day - 1u);

    local_sec = days * 86400UL + (u32)hour * 3600UL + (u32)min * 60UL + (u32)sec;
    if (local_sec < GPRS_TZ_OFFSET_SEC) return 0;         // 本地时不够减：日历可疑
    return local_sec - GPRS_TZ_OFFSET_SEC + GPRS_UNIX_BASE_2000;
}

// 函数功能：13 位 Unix 毫秒时间戳（信封 ts 用）
// 说    明：RTC 未对时/不可用时退化到 2000 基准值——仍是合法毫秒戳，保证报文可发
int64_t gprs_unix_ms_now(void)
{
    u32 s = Gprs_UnixSecNow();

    if (s < GPRS_UNIX_BASE_2000)
        s = GPRS_UNIX_BASE_2000;
    return (int64_t)s * 1000LL;
}

// 函数功能：生成本次发送的消息 id（信封 id 用）：超时重发也必须拿到新 id
// 说    明：同一秒内连续发送时单调 +1，保证递增且不重复（平台按 id 去重）
void gprs_next_msg_id(char *buf, u16 buflen)
{
    static int64_t s_last_ms = 0;
    int64_t ms = gprs_unix_ms_now();

    if (ms <= s_last_ms)
        ms = s_last_ms + 1;
    s_last_ms = ms;
    snprintf(buf, buflen, "%llu", (unsigned long long)ms);
}

// 函数功能：发一条 AT 握手命令，测试模块串口通道是否活着
// 说    明：应答的 OK 由 gprs_rx_datas 在 WAIT_NET/AT_OK 态识别后回报
//           OTA_RX_RET_AT_OK（main 据此从 WAIT_NET 迁到 AT_OK）
void At_init(void)
{
    printf("Sending AT command\r\n");
    Usart2_Send("usr.cn#AT\r\n", 11);
}

// 函数功能：按消息类型组装 JSON 信封并发到 4G 口；版本/下载请求同时刷新 id/ts 并缓存
// 参    数：sta  —— SVERSION / UPGRADE / PUTERROR
//           para —— 信封字段（业务部分由 pub_* 预填）
// 关键行为：① SVERSION/UPGRADE/PUTERROR 每次都刷新 id 与 ts（重发≠同一条请求）；
//           ② 新请求/重发前 gprs_rx_frame_reset() 丢弃可能卡住的半帧；
//           ③ SVERSION/UPGRADE 缓存到 g_last_*，供掉线重连/超时后重发
void gprs_send_datas(u8 sta, CJsonPara para)
{
    u16 length = 0;
    static char json_str[512] = {'\0'};   // 组包缓冲 512B：静态化，不放栈上

    /* 每次发送（含超时重发）都刷新 id / ts */
    if (sta == SVERSION || sta == UPGRADE || sta == PUTERROR)
    {
        gprs_next_msg_id(para.id, bid);
        para.ts = gprs_unix_ms_now();
    }

    /* 新请求/超时重发前丢弃可能卡住的半帧，避免旧数据被拼进下一帧 */
    if (sta == SVERSION || sta == UPGRADE)
        gprs_rx_frame_reset();

    /* 缓存最近一次可重发请求（版本请求/下载请求） */
    if (sta == SVERSION || sta == UPGRADE)
    {
        g_last_sta = sta;
        g_last_para = para;
        g_last_valid = 1;
    }

    switch (sta)
    {
        case UPGRADE:   // 请求 bin 文件块序号
            sprintf(json_str,
                "%d,{"
                "\"id\":\"%s\","
                "\"ver\":\"%s\","
                "\"ts\":%llu,"
                "\"productKey\":\"%s\","
                "\"deviceKey\":\"%s\","
                "\"type\":\"%s\","
                "\"body\":{"
                "\"OTACode\":\"%s\","
                "\"paras\":{"
                "\"offset\":%d"
                "}"
                "}"
                "}"
                ,7,para.id,para.ver,para.ts,para.productKey,para.deviceKey,para.type,para.OTACode,para.offset
             );
            break;
        case PUTERROR:  // 上报 OTA 结果
            sprintf(json_str,
                "%d,{"
                "\"id\":\"%s\","
                "\"ver\":\"%s\","
                "\"ts\":%llu,"
                "\"productKey\":\"%s\","
                "\"deviceKey\":\"%s\","
                "\"type\":\"%s\","
                "\"body\":{"
                "\"OTACode\":\"%s\","
                "\"paras\":{"
                "\"code\":\"%s\","
                "\"message\":\"%s\""
                "}"
                "}"
                "}"
                ,7,para.id,para.ver,para.ts,para.productKey,para.deviceKey,para.type,para.OTACode,para.code,para.message
             );
            break;
        case SVERSION:  // 版本号上报（服务器据此决定是否允许升级）
            sprintf(json_str,
                "%d,{"
                "\"id\":\"%s\","
                "\"ver\":\"%s\","
                "\"ts\":%llu,"
                "\"productKey\":\"%s\","
                "\"deviceKey\":\"%s\","
                "\"type\":\"%s\","
                "\"body\":{"
                "\"OTACode\":\"%s\","
                "\"paras\":{"
                "\"version\":\"%s\""
                "}"
                "}"
                "}"
                ,7,para.id,para.ver,para.ts,para.productKey,para.deviceKey,para.type,para.OTACode,para.version
             );
            break;
    }

    length = strlen(json_str);           // 首个数字是 MQTT 主题号（7 = OTA 主题）
    printf("\ncJSON: %d, %s\n", length, json_str);
    Usart2_Send(json_str, length);
}

// 函数功能：重发最近一次缓存的请求（掉线重连 / 请求超时路径）
// 返 回 值：1 = 已重发；0 = 无缓存（调用方据此打印"no cached request"）
int gprs_resend_last_request(void)
{
    if (!g_last_valid)
        return 0;
    gprs_send_datas(g_last_sta, g_last_para);   // 内部会刷新 id/ts：重发是新请求
    return 1;
}
```

## 6. `HARDWARE/GPRS/gprs_rx.c`（重构后全文）

```c
// GPRS 接收模块：USART2 环形缓冲取帧 → JSON/固件块解析 → 写 A2 / 状态块落盘
// gprs_rx_datas 每轮最多取一个完整帧（JSON 信封 + 可选固件块 + CRC），
// 按 OTACode 分派：putVersion 回包 → cjson_upgrade（升级判定），
// getFirmware 固件块 → cjson_download_reply（闸门校验 + 写 A2）。
// 返回码 OTA_RX_* 交 main 状态机判定；进度条由本模块直接刷新。

#include "main.h"         // OTA_STATE_* / g_ota_state：握手窗口判定
#include "usart.h"        // printf + Usart2_Rx* 环形缓冲 API（USART 阶段产物）
#include "gprs.h"         // OtaPara/CJsonPara/OTA_RX_* 返回码/工具函数声明
#include <stdio.h>
#include <string.h>
#include "crc16.h"        // CRC16_CCITT_FALSE：固件块校验（阶段 9 叶子）
#include "delay.h"        // delay_ms：发包节奏
#include "stmflash.h"     // Flash_Write：固件块写 A2
#include "flash_layout.h" // ST32_A2_SADDR / ST32_A2_PAGE_NUM / ST32_PAGE_SIZE
#include "reg.h"          // OTA_Info / OTA_ADDR / OTA_OFF_* / OTA_A2_FLAG
#include "24cxx.h"        // AT24CXX_Write / AT24CXX_OK
#include "ProgressBar.h"  // ProgressBar_Setup / Update：下载进度

OtaPara OtaParas;    // putVersion 回包解析出的固件信息（升级全程依据）
CJsonPara CParas;    // 发送信封字段容器
u16 process = 0;     // 进度条分母＝总包数，最大 400 包，必须 u16（u8 会截断，且 count=256 时会除零）

/* ---- OTA 下载上下文（跨包保留，gprs_download_context_reset 统一清零） ---- */
static u32 s_dl_addr = ST32_A2_SADDR;        // 下一包的 A2 写入字节地址
static u32 s_dl_lastaddr = ST32_A2_SADDR;    // 当前包的 A2 写入字节地址
static u16 s_dl_last_offset = 0;             // 最近一次 CRC 失败的包序号（失败计数按序号分段）
static u8 s_dl_crc_retry_cnt = 0;            // 同一包的 CRC 连续失败计数（上限 5）
static u16 s_dl_expected_offset = 0;         // 会话内期望收到的包序号
static u32 s_dl_written_bytes = 0;           // 会话内已写入 A2 的字节数
static u8 s_dl_seq_retry_cnt = 0;            // 序号不符（重复/乱序包）的连续重发次数

/* 掉线重连窗口（等 AT / 等网络上线）里到达的 putVersion 回执：先暂存，等状态机
   回到 HANDLE_REPLY 再解析。原实现把这类帧整帧丢弃，导致"请求已发、回包刚到就掉线"
   时平台回包白丢一次（现场表现为重连后一直 request timeout）。 */
#define GPRS_DEFER_BUF_LEN 512
static char s_defer_buf[GPRS_DEFER_BUF_LEN];
static u16 s_defer_len = 0;

// 函数功能：掉线窗口是否暂存着一个平台回包（main 在 ONLINE 态据此直接进 HANDLE_REPLY）
u8 gprs_rx_defer_pending(void)
{
    return (s_defer_len != 0) ? 1u : 0u;
}

// 函数功能：清零下载上下文 + 丢弃掉线窗口留下的旧回包（下载会话开始/收尾调用）
void gprs_download_context_reset(void)
{
    s_dl_addr = ST32_A2_SADDR;
    s_dl_lastaddr = ST32_A2_SADDR;
    s_dl_last_offset = 0;
    s_dl_crc_retry_cnt = 0;
    s_dl_expected_offset = 0;
    s_dl_written_bytes = 0;
    s_dl_seq_retry_cnt = 0;
    s_defer_len = 0;                  // 会话边界：旧回执不再有效
    gprs_rx_frame_reset();
}

// 函数功能：是否有一轮下载在途（收到过包、且会话尚未收尾）
// 说    明：以 expected_offset != 0 为标志——第 0 包请求发出即置起，收尾/复位清零
u8 gprs_download_pending(void)
{
    return (s_dl_expected_offset != 0) ? 1 : 0;
}

/* ---- 取帧：从环形缓冲中只取一个完整帧，其余字节留给下一次调用 ----
   USART2 的 DMA 空闲帧不等于一条完整 OTA 报文。4G 模组可能把 JSON、512B
   固件块和 CRC 拆成多次空闲中断，也可能把连续报文一次送入。 */
#define GPRS_RX_FRAME_MAX       1024
#define GPRS_RX_PREFIX_SCAN     32
#define GPRS_RX_TAKE_NONE       0
#define GPRS_RX_TAKE_READY      1
#define GPRS_RX_TAKE_ERROR     -1

static char s_uart_rx2_frame[GPRS_RX_FRAME_MAX];   // 帧组装缓冲
static char s_uart_rx2_json[GPRS_RX_FRAME_MAX];    // JSON 抽取缓冲
static u32 s_rx_last_overflow_count = 0;           // 上次见到的环形缓冲溢出累计
static u16 s_rx_last_wait_need = 0;                // 上次打印的期望帧长（去重打印用）
static u16 s_rx_last_wait_have = 0;                // 上次打印的已有字节数（去重打印用）
static u16 s_rx_frame_diag_count = 0;              // 帧就绪诊断打印限流计数
static u16 s_rx_resync_count = 0;                  // 噪声字节重对齐累计

// 函数功能：清空环形缓冲并复位等待状态（发新请求前调用，防止旧字节拼进新帧）
void gprs_rx_frame_reset(void)
{
    Usart2_RxClear();
    s_rx_last_wait_need = 0;
    s_rx_last_wait_have = 0;
}

// 函数功能：在帧头前缀里找 JSON 起点 '{'
// 返 回 值：起始下标；未找到返回 (u16)-1（只扫描前 32 字节，避免大缓冲全扫）
static u16 gprs_rx_find_json_start(const char *buf, u16 len)
{
    u16 i;
    u16 limit = (len < GPRS_RX_PREFIX_SCAN) ? len : GPRS_RX_PREFIX_SCAN;

    if (buf == NULL)
        return (u16)-1;
    for (i = 0; i < limit; i++)
    {
        if (buf[i] == '{')
            return i;
    }
    return (u16)-1;
}

// 函数功能：解析 JSON 信封，推算完整帧期望长度（JSON + 可选固件块 + 2 字节 CRC）
// 参    数：buf/available —— 帧缓冲与已有字节数；json_offset/json_len —— JSON 位置
// 返 回 值：0 = 成功（expected_len/frame_offset/is_firmware 填写）；-1 = JSON 非法
// 推算规则：OTACode=getFirmware 时按 paras.size 补上固件块与 CRC 的长度；
//           非法 size 只等 JSON 本身，交给 cjson_download_reply 返回明确错误
static int gprs_rx_expected_len(
    const char *buf,
    u16 available,
    u16 json_offset,
    u16 json_len,
    u16 *expected_len,
    u16 *frame_offset,
    u8 *is_firmware)
{
    cJSON *root;
    cJSON *body;
    cJSON *ota_code;
    cJSON *paras;
    cJSON *size_item;
    cJSON *offset_item;
    u16 expected;

    *expected_len = 0;
    *frame_offset = 0;
    *is_firmware = 0;

    if (json_len == 0 || json_len >= sizeof(s_uart_rx2_json))
        return -1;
    if ((u32)json_offset + json_len > available)
        return -1;

    memcpy(s_uart_rx2_json, buf + json_offset, json_len);
    s_uart_rx2_json[json_len] = '\0';
    root = cJSON_Parse(s_uart_rx2_json);
    if (root == NULL)
        return -1;

    expected = (u16)(json_offset + json_len);
    body = cJSON_GetObjectItem(root, "body");
    ota_code = (body != NULL) ? cJSON_GetObjectItem(body, "OTACode") : NULL;
    if (ota_code != NULL && cJSON_IsString(ota_code) && ota_code->valuestring != NULL &&
        strcmp(ota_code->valuestring, "getFirmware") == 0)
    {
        *is_firmware = 1;
        paras = cJSON_GetObjectItem(body, "paras");
        size_item = (paras != NULL) ? cJSON_GetObjectItem(paras, "size") : NULL;
        offset_item = (paras != NULL) ? cJSON_GetObjectItem(paras, "offset") : NULL;
        if (offset_item != NULL && cJSON_IsNumber(offset_item))
            *frame_offset = (u16)cJSON_GetNumberValue(offset_item);

        /* 正常固件块长度固定为 1..512。非法长度只等 JSON 本身，随后交给
           cjson_download_reply 返回明确错误，避免为了不存在的负载无限等待。 */
        if (size_item != NULL && cJSON_IsNumber(size_item))
        {
            double size_value = cJSON_GetNumberValue(size_item);
            if (size_value > 0.0 && size_value <= BYTESIZE)
                expected = (u16)(expected + (u16)size_value + 2);
        }
    }

    cJSON_Delete(root);
    if (expected > GPRS_RX_FRAME_MAX)
        return -1;
    *expected_len = expected;
    return 0;
}

// 函数功能：从环形缓冲取一个完整帧（溢出重对齐 → 找 JSON → 推算帧长 → 等齐/取出）
// 返 回 值：TAKE_NONE = 暂无完整帧；TAKE_READY = 取到（frame_len 为帧总长）；
//           TAKE_ERROR = 帧非法已丢弃
static int gprs_rx_take_frame(
    u16 *frame_len,
    u16 *json_offset_out,
    u16 *expected_out,
    u16 *offset_out,
    u8 *is_firmware)
{
    u16 available;
    u16 peek_len;
    u16 json_offset;
    u16 json_len;
    u16 expected;
    u16 frame_offset;
    char *json_end;
    u32 overflow_count;

    *frame_len = 0;
    *json_offset_out = 0;
    *expected_out = 0;
    *offset_out = 0;
    *is_firmware = 0;

    /* 环形缓冲溢出说明字节已丢：清缓冲重新对齐（丢弃比错位安全） */
    overflow_count = Usart2_RxOverflowCount();
    if (overflow_count != s_rx_last_overflow_count)
    {
        s_rx_last_overflow_count = overflow_count;
        Usart2_RxClear();
        s_rx_last_wait_need = 0;
        s_rx_last_wait_have = 0;
        printf("rx ring overflow: total=%u, resync\r\n", (unsigned)overflow_count);
        return GPRS_RX_TAKE_NONE;
    }

    available = Usart2_RxAvailable();
    if (available == 0)
        return GPRS_RX_TAKE_NONE;

    peek_len = (available > sizeof(s_uart_rx2_frame)) ?
        sizeof(s_uart_rx2_frame) : available;
    peek_len = Usart2_RxCopy(s_uart_rx2_frame, peek_len);   // 拷出观察，不消费
    s_uart_rx2_frame[peek_len] = '\0';

    json_offset = gprs_rx_find_json_start(s_uart_rx2_frame, peek_len);
    if (json_offset == (u16)-1)
    {
        /* 只收到模组前缀时继续等待；明显是 URC/AT 噪声时逐字节重新对齐 */
        if (peek_len > 8)
        {
            Usart2_RxConsume(1);
            s_rx_resync_count++;
            if (s_rx_resync_count == 1 || (s_rx_resync_count % 64) == 0)
                printf("rx resync: dropped=%u, available=%u\r\n",
                    s_rx_resync_count, available);
        }
        return GPRS_RX_TAKE_NONE;
    }

    json_end = find_json_end_by_bracket_matching(
        s_uart_rx2_frame + json_offset,
        (u16)(peek_len - json_offset));
    if (json_end == NULL)
        return GPRS_RX_TAKE_NONE;        // JSON 未闭合：还在传输中，继续等

    json_len = (u16)(json_end - (s_uart_rx2_frame + json_offset) + 1);
    if (gprs_rx_expected_len(
        s_uart_rx2_frame,
        peek_len,
        json_offset,
        json_len,
        &expected,
        &frame_offset,
        is_firmware) != 0)
    {
        Usart2_RxConsume((u16)(json_offset + json_len));   // JSON 非法：丢弃 JSON 段
        printf("rx frame invalid: prefix=%u, json=%u, available=%u\r\n",
            json_offset, json_len, available);
        return GPRS_RX_TAKE_ERROR;
    }

    if (peek_len < expected)
    {
        /* 帧未到齐：打印去重（同长度同存量不重复刷屏），继续等 */
        if (expected != s_rx_last_wait_need || available != s_rx_last_wait_have)
        {
            s_rx_last_wait_need = expected;
            s_rx_last_wait_have = available;
            printf("rx frame wait: prefix=%u, need=%u, have=%u, offset=%u\r\n",
                json_offset, expected, available, frame_offset);
        }
        return GPRS_RX_TAKE_NONE;
    }

    s_uart_rx2_frame[expected] = '\0';
    Usart2_RxConsume(expected);          // 帧到齐：整帧消费
    s_rx_last_wait_need = 0;
    s_rx_last_wait_have = 0;

    if (s_rx_frame_diag_count < 10 || *is_firmware == 0 || frame_offset == 5)
    {
        printf("rx frame ready: prefix=%u, expected=%u, available=%u, offset=%u\r\n",
            json_offset, expected, available, frame_offset);
    }
    s_rx_frame_diag_count++;

    *frame_len = expected;
    *json_offset_out = json_offset;
    *expected_out = expected;
    *offset_out = frame_offset;
    return GPRS_RX_TAKE_READY;
}

// 函数功能：在握手窗口识别 AT 应答（只搜 "OK"，命中即消费到 OK 末尾）
// 返 回 值：1 = 识别到 OK；0 = 无应答或未见 OK
static u8 gprs_rx_consume_at_ok(void)
{
    u16 available = Usart2_RxAvailable();
    u16 len;
    char *ok;

    if (available == 0)
        return 0;
    len = (available > 256) ? 256 : available;
    len = Usart2_RxCopy(s_uart_rx2_frame, len);
    s_uart_rx2_frame[len] = '\0';
    ok = strstr(s_uart_rx2_frame, "OK");
    if (ok == NULL)
        return 0;

    Usart2_RxConsume((u16)((ok - s_uart_rx2_frame) + 2));
    return 1;
}

// 函数功能：GPRS 接收主入口——每轮最多处理一个完整帧，返回 OTA_RX_* 交 main 判定
// 分派规则：OTACode=putVersion → cjson_upgrade（升级判定）；
//           OTACode=getFirmware → cjson_download_reply（闸门 + 写 A2）；
//           握手窗口（WAIT_NET/AT_OK）只识别 AT OK 与暂存 putVersion 回包，不解析
int gprs_rx_datas(void)
{
    char *pos;
    u16 u2rxcnt = 0;
    u16 json_offset = 0;
    u16 expected = 0;
    u16 frame_offset = 0;
    u8 is_firmware = 0;
    cJSON *root;
    u16 cjson_len = 0;
    u16 message_len = 0;
    int txsta;
    int take_result;
    int rx_ret = OTA_RX_RET_PARSE_FAILED;
    cJSON *body_item;
    cJSON *ota_code;
    u8 from_defer = 0;
    u16 i;

    /* 掉线重连窗口暂存的 putVersion 回包优先处理（已回到在途态且无下载会话时） */
    if (s_defer_len != 0 && (gprs_download_pending() == 0)
        && g_ota_state != OTA_STATE_WAIT_NET && g_ota_state != OTA_STATE_AT_OK)
    {
        u2rxcnt = s_defer_len;
        s_defer_len = 0;
        from_defer = 1;
        memcpy(s_uart_rx2_frame, s_defer_buf, u2rxcnt);
        json_offset = gprs_rx_find_json_start(s_uart_rx2_frame, u2rxcnt);
        printf("parse stashed downlink frame (%u bytes)\r\n", u2rxcnt);
    }
    else
    {
        /* 握手窗口：识别 AT OK；WAIT_NET 命中回报 AT_OK，AT_OK 态命中只清字节 */
        if (g_ota_state == OTA_STATE_WAIT_NET || g_ota_state == OTA_STATE_AT_OK)
        {
            if (gprs_rx_consume_at_ok())
            {
                if (g_ota_state == OTA_STATE_WAIT_NET)
                {
                    g_ota_state = OTA_STATE_AT_OK;
                    printf("Received AT OK\r\n");
                    return OTA_RX_RET_AT_OK;
                }
                return OTA_RX_RET_NO_DATA;
            }
        }

        take_result = gprs_rx_take_frame(
            &u2rxcnt, &json_offset, &expected, &frame_offset, &is_firmware);
        if (take_result == GPRS_RX_TAKE_NONE)
            return OTA_RX_RET_NO_DATA;
        if (take_result == GPRS_RX_TAKE_ERROR)
            return OTA_RX_RET_PARSE_FAILED;
    }

    if (json_offset == (u16)-1 || json_offset >= u2rxcnt)
        return OTA_RX_RET_PARSE_FAILED;

    /* 握手期间仅暂存 putVersion 回包；完整下载帧在下载会话中正常处理 */
    if (g_ota_state == OTA_STATE_WAIT_NET || g_ota_state == OTA_STATE_AT_OK)
    {
        char head[17];
        u16 n = (u2rxcnt > 16) ? 16 : u2rxcnt;

        for (i = 0; i < n; i++)
            head[i] = s_uart_rx2_frame[i];
        head[n] = '\0';
        printf("rx handshake-window frame (%u bytes): %s\r\n", u2rxcnt, head);

        if (!from_defer && str_contains(s_uart_rx2_frame, "putVersion"))
        {
            if (s_defer_len == 0 && u2rxcnt <= sizeof(s_defer_buf))
            {
                memcpy(s_defer_buf, s_uart_rx2_frame, u2rxcnt);
                s_defer_len = u2rxcnt;
                printf("stash putVersion reply (%u bytes), parse after reconnect\r\n", u2rxcnt);
            }
            else
            {
                printf("stash busy, drop this putVersion reply\r\n");
            }
        }
        return OTA_RX_RET_NO_DATA;
    }

    pos = find_json_end_by_bracket_matching(
        s_uart_rx2_frame + json_offset,
        (u16)(u2rxcnt - json_offset));
    if (pos == NULL)
    {
        printf("s->c back JSON validation failed: offset=%u, bytes=%u\r\n",
            json_offset, u2rxcnt);
        return OTA_RX_RET_PARSE_FAILED;
    }

    cjson_len = (u16)(pos - (s_uart_rx2_frame + json_offset) + 1);
    if (cjson_len >= sizeof(s_uart_rx2_json))
    {
        printf("json payload too long: %u\r\n", cjson_len);
        return OTA_RX_RET_PARSE_FAILED;
    }
    memcpy(s_uart_rx2_json, s_uart_rx2_frame + json_offset, cjson_len);
    s_uart_rx2_json[cjson_len] = '\0';
    root = cJSON_Parse(s_uart_rx2_json);
    if (root == NULL)
    {
        printf("cJSON_Parse failed: prefix=%u, json=%u\r\n", json_offset, cjson_len);
        return OTA_RX_RET_PARSE_FAILED;
    }

    printf("rx json: %s\r\n", s_uart_rx2_json);
    body_item = cJSON_GetObjectItem(root, "body");
    ota_code = (body_item != NULL) ? cJSON_GetObjectItem(body_item, "OTACode") : NULL;
    if (ota_code != NULL && cJSON_IsString(ota_code) && ota_code->valuestring != NULL &&
        strcmp(ota_code->valuestring, "putVersion") == 0)
    {
        txsta = cjson_upgrade(root);
        if (txsta == 1)
        {
            ProgressBar_Setup();    // 进度条装配（main 收到 START_UPDATE 后还会调一次，幂等）
            printf("version: Firmware upgrade started, requesting first data block\r\n");
            rx_ret = OTA_RX_RET_START_UPDATE;
        }
        else if (txsta == 0)
        {
            printf("version: Version is same or upgrade not needed\r\n");
            rx_ret = OTA_RX_RET_NO_UPDATE;
        }
        else
        {
            rx_ret = txsta;         // 负值错误码透传（-30：信封字段非法）
        }
    }
    else if (ota_code != NULL && cJSON_IsString(ota_code) && ota_code->valuestring != NULL &&
        strcmp(ota_code->valuestring, "getFirmware") == 0)
    {
        message_len = (u16)(u2rxcnt - (u16)((pos + 1) - s_uart_rx2_frame));  // JSON 之后 = 块 + CRC
        txsta = cjson_download_reply(root, pos + 1, message_len);
        if (txsta == 1)
        {
            printf("download: Firmware download completed, ready to restart\r\n");
            rx_ret = OTA_RX_RET_DOWNLOAD_DONE;
        }
        else if (txsta == 0)
        {
            printf("download: Data block written successfully, continue downloading next block\r\n");
            rx_ret = OTA_RX_RET_DOWNLOAD_CONTINUE;
        }
        else
        {
            rx_ret = txsta;
        }
    }

    (void)expected;
    (void)frame_offset;
    (void)is_firmware;
    cJSON_Delete(root);
    return rx_ret;
}

/* ---- 服务器 -> 客户端 ----
   固件升级中只有客户端主动请求、服务器才回数据这一种模式，
   不存在服务器主动推送的情况。 */

// 函数功能：putVersion 回包处理——平台允许 ∧ 版本闸不命中 → 换算包数 → 开始下载
// 返 回 值：1 = 开始升级（已请求第 0 包）；0 = 无需升级；负值 = 信封非法（-30）
// 升级判定 = 平台标志 ∧ 版本闸：设备级 UPDATE_flag 已在 main 进状态机前判过
// （为 0 根本不会发 putVersion），此处不再重复判断；三个条件分别打印便于现场排查
int cjson_upgrade(cJSON * root)
{
    cJSON *para, *size, *ver, *upgrade, *body;
    int result;
    size_t ver_len;
    u8 upgrade_flag = 0;   /* 条件1：平台标志（putVersion 回包 upgrade） */
    u8 version_same = 0;   /* 条件2：版本闸（本机 OTA_ver 是否已等于回包 version） */

    body = cJSON_GetObjectItem(root, "body");
    para = (body != NULL) ? cJSON_GetObjectItem(body, "paras") : NULL;
    size = (para != NULL) ? cJSON_GetObjectItem(para, "size") : NULL;
    ver = (para != NULL) ? cJSON_GetObjectItem(para, "version") : NULL;
    upgrade = (para != NULL) ? cJSON_GetObjectItem(para, "upgrade") : NULL;

    if (body == NULL || para == NULL || size == NULL || ver == NULL || upgrade == NULL
        || !cJSON_IsString(ver) || ver->valuestring == NULL
        || !cJSON_IsNumber(size) || !cJSON_IsNumber(upgrade))
    {
        printf("cjson_upgrade: body/paras/size/version/upgrade invalid\r\n");
        return -30;    // 信封字段缺失/类型不对（main 计入 rx 错误）
    }

    upgrade_flag = (cJSON_GetNumberValue(upgrade) != 0) ? 1 : 0;
    version_same = (strcmp((char *)OTA_Info.OTA_ver, ver->valuestring) == 0) ? 1 : 0;

    printf("cjson_upgrade: [1/3] platform upgrade = %d %s\r\n",
           (int)upgrade_flag,
           upgrade_flag ? "(allowed)" : "(not allowed: UpdateHardware=0 or fw not bound/loaded)");
    printf("cjson_upgrade: [2/3] ver->valuestring = %s (target version)\r\n", ver->valuestring);
    printf("cjson_upgrade: [3/3] OTA_Info.OTA_ver = %s (running version, %s target)\r\n",
           (char *)OTA_Info.OTA_ver, version_same ? "same as" : "differs from");
    printf("cjson_upgrade: size=%d\r\n", (int)cJSON_GetNumberValue(size));

    if (upgrade_flag && !version_same)
    {
        OtaParas.size = cJSON_GetNumberValue(size);
        memset(OtaParas.ver, 0, 32);
        ver_len = strlen(ver->valuestring);
        strncpy(OtaParas.ver, ver->valuestring, (ver_len < 31) ? ver_len : 31);
        OtaParas.ver[31] = '\0';

        /* 固件大小合法性：非 0，且不超过 A2 区容量（100 页 × 2KB = 200KB） */
        if (OtaParas.size == 0 || OtaParas.size > (u32)(ST32_A2_PAGE_NUM * ST32_PAGE_SIZE))
        {
            printf("invalid firmware size: %u, abort upgrade\r\n", OtaParas.size);
            return 0;   // 视为无需升级 → main 跳回 A1（旧程序）
        }

        /* offset = 包序号，count = 总包数：按 512 字节/包换算 */
        if ((OtaParas.size % BYTESIZE) != 0)
        {
            OtaParas.count = (u16)(OtaParas.size / BYTESIZE + 1);
            OtaParas.remaining = (u16)(OtaParas.size % BYTESIZE);
        }
        else
        {
            OtaParas.count = (u16)(OtaParas.size / BYTESIZE);
            OtaParas.remaining = BYTESIZE;
        }
        printf("upgrade is start -- size = %u bytes, count = %u packets, last = %u bytes\r\n",
               OtaParas.size, OtaParas.count, OtaParas.remaining);

        /* 新一轮下载会话：复位写指针与进度
           （掉线重连不会走到这里：ONLINE 态会先重发在途请求，见 main） */
        gprs_download_context_reset();

        process = OtaParas.count;
        ProgressBar_Update(0);   // 0% 基准（随后 main 的 Setup 会重画底框，幂等）
        delay_ms(100);

        printf("cjson_upgrade: DECISION = START_UPDATE (upgrade=1 and version differs)\r\n");
        cjson_pub_getbin(0);     // 请求第 0 包
        return 1;
    }
    printf("cjson_upgrade: DECISION = NO_UPDATE (%s)\r\n",
           upgrade_flag ? "version already up to date" : "platform upgrade=0, upgrade not allowed");
    return 0;
}

// 函数功能：getFirmware 固件块处理——四道闸门 → CRC 校验 → 写 A2 → 请求下一包 / 收尾落盘
// 参    数：root —— JSON 信封；message —— JSON 之后的原始字节（块 + 2 字节 CRC）
// 返 回 值：1 = 全部收完且收尾校验通过；0 = 本包写入成功；
//           负值 = 各闸门/校验错误码（main 计数，超限放弃升级）
u8 cjson_download_reply(cJSON * root, char *message, u16 message_len)
{
    cJSON *para, *bOffset, *bsize, *body;
    u16 Offset = 0, Size = 0;
    u16 crc = 0;
    int result;
    size_t ota_ver_len;
    u16 orig_size;

    body = cJSON_GetObjectItem(root, "body");
    para = (body != NULL) ? cJSON_GetObjectItem(body, "paras") : NULL;
    bsize = (para != NULL) ? cJSON_GetObjectItem(para, "size") : NULL;
    bOffset = (para != NULL) ? cJSON_GetObjectItem(para, "offset") : NULL;

    Offset = cJSON_GetNumberValue(bOffset);
    Size = cJSON_GetNumberValue(bsize);

    /* 闸门一（序号）：只接受本会话期望的那一包；重复包/乱序包一律丢弃，
       然后"重新申请同一个请求"（不是从 0 重来） */
    if (Offset != s_dl_expected_offset)
    {
        printf("packet offset mismatch: got %u, expect %u\r\n", Offset, s_dl_expected_offset);
        if (s_dl_seq_retry_cnt < 5)
        {
            s_dl_seq_retry_cnt++;
            cjson_pub_getbin(s_dl_expected_offset);
            return OTA_RX_RET_DOWNLOAD_CONTINUE;   // 主流程按"仍在下载"处理，不报错
        }
        printf("packet offset mismatch too many times\r\n");
        return OTA_RX_ERR_SEQ_RETRY_EXCEEDED;      // 交由 main 计数并最终放弃
    }
    s_dl_seq_retry_cnt = 0;

    /* 闸门二（包长）：单包不得为 0，且不得超过 512 字节 */
    if (Size == 0 || Size > BYTESIZE)
    {
        printf("invalid packet size: %u\r\n", Size);
        return OTA_RX_ERR_PACKET_SIZE;
    }

    /* 闸门三（累计）：已写字节 + 本包不得越过协议声明的固件大小 */
    if ((s_dl_written_bytes + Size) > OtaParas.size)
    {
        printf("download overflow: %u + %u > %u\r\n", s_dl_written_bytes, Size, OtaParas.size);
        return OTA_RX_ERR_LENGTH_OVERFLOW;
    }

    /* 闸门四（帧长）：JSON 之后必须真有 Size 字节块 + 2 字节 CRC */
    if ((u32)Size + 2 > message_len)
    {
        printf("download packet too short: need %u, have %u\r\n", (u16)(Size + 2), message_len);
        return OTA_RX_ERR_PACKET_SHORT;
    }

    printf("Offset = %d,size = %d\r\n", Offset, Size);

    /* CRC 校验（CCITT_FALSE）：低字节在前；失败则重申请本包（同一包连续失败限 5 次） */
    crc = CRC16_CCITT_FALSE((u8 *)&message[0], Size);
    if (((crc >> 8) & 0xff) != message[Size + 1] || (crc & 0xff) != message[Size])
    {
        if (s_dl_last_offset != Offset)     // 换了包序号：失败计数重新分段
        {
            s_dl_crc_retry_cnt = 0;
            s_dl_last_offset = Offset;
        }
        else
        {
            s_dl_crc_retry_cnt++;
        }
        if (s_dl_crc_retry_cnt < 5)
            cjson_pub_getbin(Offset);
        else
            s_dl_crc_retry_cnt = 5;
        printf("download data error %d\n", s_dl_crc_retry_cnt);
        return OTA_RX_ERR_CRC_RETRY_EXCEEDED;
    }

    orig_size = Size;                // 保存原始字节数（累计与打印用）
    if ((Size % 2) != 0)             // 字节数 → 半字数（奇数补足 1 个半字）
        Size = Size / 2 + 1;
    else
        Size = Size / 2;

    /* 写 flash A2 区。Flash_Write 内部自带按页擦除；写地址按包推进必然跨页，
       硬依赖《STMFLASH与分区布局重构.md》§8 的跨页写单位修正——
       未落盘修正版（WriteAddr += secremain * 2）时此处会把镜像整段写错位。 */
    printf("write A2 zone-- %d --\r\n", Offset);
    s_dl_addr += Size * 2;           // Size 此时是半字数：×2 = 本包字节数，推进到下一包起点
    Flash_Write(s_dl_lastaddr, (u16 *)&message[0], Size);
    s_dl_lastaddr = s_dl_addr;
    s_dl_written_bytes += orig_size; // 累计已写入字节数
    s_dl_expected_offset++;          // 推进会话进度：下一包必须是这个序号

    if (Offset < (OtaParas.count - 1))       // 非末包：请求下一包 + 刷进度条
    {
        cjson_pub_getbin(Offset + 1);
        ProgressBar_Update(Offset * 100 / process);
        delay_ms(200);
        printf("request next data --\r\n");
    }
    else if (Offset == (OtaParas.count - 1)) // 末包：收尾
    {
        /* 收尾一致性校验：累计写入必须等于协议声明的固件大小，
           否则不落盘 OTA_flag，交由 main 计数并放弃（引导旧程序） */
        if (s_dl_written_bytes != OtaParas.size)
        {
            printf("download size mismatch: written %u, expect %u\r\n",
                   s_dl_written_bytes, OtaParas.size);
            return OTA_RX_ERR_LENGTH_OVERFLOW;
        }

        /* 状态块落盘顺序：先写 OTA_size、再写 OTA_ver，最后才写 OTA_flag。
           24C02 是逐字节 I2C 写，中途掉电只会停在"flag 仍是 A1"的安全状态；
           若先写 flag，就可能留下 flag=A2 而 size=0 的坏状态，引导侧会搬运垃圾
           （护栏2 会拦，但风险不该留给引导侧）。 */
        gprs_download_context_reset();

        memset(&OTA_Info.OTA_ver, 0, 32);
        ota_ver_len = strlen(OtaParas.ver);
        strncpy((char *)OTA_Info.OTA_ver, OtaParas.ver, (ota_ver_len < 31) ? ota_ver_len : 31);
        OTA_Info.OTA_ver[31] = '\0';
        OTA_Info.OTA_size = OtaParas.size;
        printf("OTA_size = %d \r\n ", OTA_Info.OTA_size);

        if (AT24CXX_Write(OTA_ADDR + OTA_OFF_SIZE, (uint8_t *)&OTA_Info.OTA_size, 4) != AT24CXX_OK)
        {
            printf("write ota size to eeprom error --\r\n");
            return OTA_RX_ERR_EEPROM_WRITE;
        }
        if (AT24CXX_Write(OTA_ADDR + OTA_OFF_VER, (uint8_t *)&OTA_Info.OTA_ver[0], 32) != AT24CXX_OK)
        {
            printf("write ota ver to eeprom error --\r\n");
            return OTA_RX_ERR_EEPROM_WRITE;
        }

        OTA_Info.OTA_flag = OTA_A2_FLAG;     // 最后写标志：此刻起 A2 才算"待安装"
        if (AT24CXX_Write(OTA_ADDR + OTA_OFF_FLAG, (uint8_t *)&OTA_Info.OTA_flag, 4) != AT24CXX_OK)
        {
            printf("write ota flag to eeprom error --\r\n");
            return OTA_RX_ERR_EEPROM_WRITE;
        }

        cjson_pub_putError();                // 上报升级结果（otacode=otaError, code=success）
        printf("update is ok --\r\n");
        ProgressBar_Update(100);
        delay_ms(200);
        return 1;                            // main 收到 DOWNLOAD_DONE 后复位进安装分支
    }

    return 0;
}

/* ---- 客户端 -> 服务器 ---- */

// 函数功能：上报已安装版本（putVersion），服务器据此决定是否允许升级
// 版本来源：只有 OTA_flag==A1（A1 已就绪）时 OTA_ver 才代表本机运行版本；
//           待安装期间（flag=A2）OTA_ver 是"目标版本"，不能当成运行版本上报
void cjson_pub_version(void)
{
    snprintf(CParas.ver, bver, "%s", "v1.0");
    snprintf(CParas.productKey, bproductKey, "%s", g_mqtt_productKey);  // 与 A1 同源（24C02）
    snprintf(CParas.deviceKey, bdeviceKey, "%s", g_mqtt_deviceKey);
    snprintf(CParas.type, btype, "%s", "OTA");
    snprintf(CParas.OTACode, bOTACode, "%s", "putVersion");
    /* id / ts 由 gprs_send_datas() 每次发送时重新生成（含超时重发） */
    if (OTA_Info.OTA_flag == OTA_A1_FLAG && OTA_Info.OTA_ver[0] != '\0')
        snprintf(CParas.version, bversion, "%s", (char *)OTA_Info.OTA_ver);
    else
        snprintf(CParas.version, bversion, "%s", "v0.0.0");
    gprs_send_datas(SVERSION, CParas);
}

// 函数功能：请求固件块（getFirmware），offset 为包序号
void cjson_pub_getbin(u16 offset)
{
    CParas.offset = offset;
    snprintf(CParas.ver, bver, "%s", "v1.0");
    snprintf(CParas.productKey, bproductKey, "%s", g_mqtt_productKey);
    snprintf(CParas.deviceKey, bdeviceKey, "%s", g_mqtt_deviceKey);
    snprintf(CParas.type, btype, "%s", "OTA");
    snprintf(CParas.OTACode, bOTACode, "%s", "getFirmware");
    /* id / ts 由 gprs_send_datas() 每次发送时重新生成 */
    gprs_send_datas(UPGRADE, CParas);
}

// 函数功能：OTA 结果回报（otaError，code=success / message=OTA IS OK）
// 说    明：函数名沿 boot 原名"putError"，实际是升级成功的结果上报，末包收尾时调用
void cjson_pub_putError(void)
{
    snprintf(CParas.ver, bver, "%s", "v1.0");
    snprintf(CParas.productKey, bproductKey, "%s", g_mqtt_productKey);
    snprintf(CParas.deviceKey, bdeviceKey, "%s", g_mqtt_deviceKey);
    snprintf(CParas.type, btype, "%s", "OTA");
    snprintf(CParas.OTACode, bOTACode, "%s", "otaError");
    /* id / ts 由 gprs_send_datas() 每次发送时重新生成 */
    snprintf(CParas.code, bcode, "%s", "success");
    snprintf(CParas.message, bmessage, "%s", "OTA IS OK");
    gprs_send_datas(PUTERROR, CParas);
}

/* ---- 工具函数 ---- */

// 函数功能：括号匹配法找完整 JSON 的结束位置（'{'/'}' 深度归零即结束）
// 参    数：start —— 指向 '{' 的指针；max_len —— 最大搜索长度
// 返 回 值：指向 '}' 的指针；未闭合/参数无效返回 NULL
// 注意：不处理字符串内部的括号字面量——本协议的字符串值不含花括号，适用
char *find_json_end_by_bracket_matching(char *start, u16 max_len)
{
    int depth = 0;
    char *p = start;
    char *end = start + max_len;

    if (start == NULL || *p != '{')
        return NULL;

    depth = 1;
    p++;
    while (p < end && *p != '\0')
    {
        if (*p == '{')
            depth++;
        else if (*p == '}')
        {
            depth--;
            if (depth == 0)
                return p;
        }
        p++;
    }
    return NULL;
}

// 函数功能：子串检查（strstr 包装，NULL 安全）
u8 str_contains(const char *str, const char *substr)
{
    if (str == NULL || substr == NULL)
        return 0;
    return (strstr(str, substr) != NULL) ? 1 : 0;
}
```

## 7. 数据流（一次完整 OTA 下载）

```text
平台 MQTT ──4G 模组透传──> USART2 DMA 环形缓冲
   └─ gprs_rx_datas()：溢出检查 → 前缀扫描找 '{' → 括号匹配定 JSON → 按包长补齐取整帧
        ├─ OTACode=putVersion → cjson_upgrade：平台 upgrade ∧ 版本闸≠
        │     → 换算 count/remaining → 清下载上下文 → cjson_pub_getbin(0)
        └─ OTACode=getFirmware → cjson_download_reply：序号闸 → 包长闸 → 累计闸 → 帧长闸
              → CRC16_CCITT_FALSE 校验 → Flash_Write 写 A2 → expected_offset++
                    ├─ 非末包：cjson_pub_getbin(offset+1) + ProgressBar_Update
                    └─ 末包：累计==size 校验 → EEPROM 落盘（size → ver → flag=A2）
                          → cjson_pub_putError → 返回 DOWNLOAD_DONE

设备请求（gprs_tx.c）：cjson_pub_version / cjson_pub_getbin / cjson_pub_putError
   → gprs_send_datas：刷新 id/ts（重发≠同一条）→ 半帧清理 → 缓存（版本/下载请求）
   → Usart2_Send ──4G 模组──> 平台

掉线窗口（WAIT_NET/AT_OK）：putVersion 回包 → s_defer_buf 暂存
   → 重连后 ONLINE 态：gprs_rx_defer_pending()=1 → main 直接进 HANDLE_REPLY 消费
```

四道闸门与两次自愈重试的预算：序号不符重发 5 次（`s_dl_seq_retry_cnt`）、同包 CRC 失败
5 次（`s_dl_crc_retry_cnt`）——均由 main 的全局计数兜底放弃；下载上下文在会话边界
（START_UPDATE / 收尾）统一清零。

## 8. 验证 main 与验证方法

> 本阶段 main 替换合并版验证 main；阶段 12 再按《Main重构.md》§4 换成正式状态机。
> 精简版没有超时预算与放弃路径——超时观察人工看串口，正式行为以《Main重构.md》为准。

```c
/* USER/main.c —— 阶段 10 验证：连 MQTT → putVersion 往返 → 固件下载写 A2 → 回读核对 */
#include "stm32f10x.h"
#include "sys.h"
#include "delay.h"
#include "led.h"
#include "usart.h"
#include "24cxx.h"
#include "reg.h"
#include "stmflash.h"
#include "flash_layout.h"
#include "4G.h"
#include "wdg.h"
#include "ds1302.h"
#include "Display.h"
#include "gprs.h"
#include "main.h"

static u32 blink_cnt = 0;    // 心跳计数：每次喂狗 +1（LED_ERR 同拍翻转）

/* 分片等待：≤1s/片，每片喂狗 + 翻灯（同《LCD重构.md》§7 实机验证版） */
static void s_WaitFeed(u16 ms)
{
    while (ms >= 1000u) { delay_ms(1000); WDG_Feed(); LED_ERR = !LED_ERR; blink_cnt++; ms -= 1000u; }
    if (ms > 0u)        { delay_ms(ms);   WDG_Feed(); LED_ERR = !LED_ERR; blink_cnt++; }
}

int main(void)
{
    int ret;

    delay_init();
    NVIC_Configuration();
    uart1_init(115200);
    uart2_init(115200);
    LED_Init();
    LED_CLOSE();
    AT24CXX_Init();
    ds1302_init();               // gprs_tx 时间戳源
    Reg_ReadOtaInfo();           // 状态块读入（版本闸要用 OTA_ver）
    LCD12864_Init();
    Gprs4G_Init();
    WDG_Init();
    printf("gprs test start, wdg reset cause=%u\r\n", (unsigned int)g_wdg_reset_cause);

    g_ota_state = OTA_STATE_WAIT_NET;
    Lcd_ShowWaitNet();           // 第1行 wait net

    while (1)
    {
        WDG_Feed();
        ret = gprs_rx_datas();

        if (g_ota_state == OTA_STATE_WAIT_NET)
        {
            if (ret == OTA_RX_RET_AT_OK)
            {
                g_ota_state = OTA_STATE_AT_OK;      // 握手通：等 LINK
            }
            else
            {
                At_init();
                s_WaitFeed(300);
            }
            continue;
        }
        if (g_ota_state == OTA_STATE_AT_OK)
        {
            if (GPRS_LINK_STATUS == 1)
            {
                g_ota_state = OTA_STATE_ONLINE;
                printf("GPRS is online!\r\n");
            }
            s_WaitFeed(200);
            continue;
        }
        if (g_ota_state == OTA_STATE_ONLINE)
        {
            Ota_ShowRequestVer();        // 「连接成功 / 申请最新固件」
            cjson_pub_version();         // 上报版本：平台将回 putVersion
            g_ota_state = OTA_STATE_HANDLE_REPLY;
            continue;
        }
        /* OTA_STATE_HANDLE_REPLY：只看返回码，预算与放弃路径由正式 main 承担 */
        if (ret == OTA_RX_RET_START_UPDATE)
        {
            printf("start update: %u bytes, %u packets, last=%u\r\n",
                   OtaParas.size, OtaParas.count, OtaParas.remaining);
        }
        else if (ret == OTA_RX_RET_DOWNLOAD_DONE)
        {
            u16 i;
            u8 head[16];

            Flash_Read((u32)(ST32_A2_SADDR), (u16 *)head, 8);   // 回读 A2 头 16 字节
            printf("A2 head:");
            for (i = 0; i < 16; i++) printf(" %02x", head[i]);
            printf("\r\n");
            printf("eeprom: flag=%08x size=%u ver=%s\r\n",
                   OTA_Info.OTA_flag, OTA_Info.OTA_size, OTA_Info.OTA_ver);
            NVIC_SystemReset();          // 复位后走 Branch 安装分支（阶段 11 衔接）
        }
        else if (ret < 0 && ret != OTA_RX_RET_NO_DATA)
        {
            printf("rx error: %d\r\n", ret);    // 闸门/CRC 错误码人工观察
        }
    }
}
```

**测试方法**（对应迁移顺序 §2.10 的三步）：

1. **连上 MQTT**：上电 → `Received AT OK` → 屏显「连接成功/申请最新固件」前串口出
   `GPRS is online!`（LINK 由 0 跳 1）；
2. **putVersion 往返**：串口打印发出的 `cJSON: N, {...putVersion...}`（核对 id/ts 为实时
   时间戳、productKey/deviceKey 与 24C02 一致）→ 平台回包 → `rx json: {...}` →
   `cjson_upgrade: [1/3]~[3/3]` 三条判定 → `start update: size/count/last`；
3. **假固件包下载写 A2 并回读**：平台绑定一份任意固件（版本号 ≠ 本机 OTA_ver）→
   每包 `write A2 zone-- offset --` + 进度条推进 → 末包 `OTA_size = ...` →
   `A2 head:` 16 字节与平台固件文件头比对 → `eeprom: flag=a2a2a2a2 size=... ver=...`；
4. **异常路径人工观察**：下载中途拔天线/关服务器 → `link lost`（正式 main 才有预算逻辑，
   本阶段只观察 rx 停止推进）；构造坏包平台回包 → `rx error: -4x`；
5. **看门狗**：全程无复位（下载循环每圈一喂、发包节奏内部 delay 均 <2.12s）。

## 9. 落盘前提与跨文档联动

**硬前置：《STMFLASH与分区布局重构.md》的跨页修正必须先落盘且 `flash cross-page PASS`**
（gprs_rx 按包推进字节地址必然跨页，未修正时镜像整段错位）；ds1302.c 为新增入库文件。

| 依赖 | 属主文档 | 状态 |
|---|---|---|
| usart2.c 环形缓冲（Usart2_* 六 API） | 《USART重构.md》 | ✅ 已落盘 |
| 4G.h（GPRS_LINK_STATUS 宏） | 《4G模块重构.md》 | ✅ 宏已在落盘版（合并版 main 已引用） |
| flash_layout.h / Flash_Write | 《STMFLASH与分区布局重构.md》 | ⚠️ 落盘时必须含 §4/§8 跨页修正 |
| reg.h/reg.c（OTA_Info/OTA_ADDR/OTA_OFF_*/OTA_A*_FLAG/g_mqtt_*） | 《REG重构.md》 | ⬜ 待核对落盘状态 |
| 24cxx（AT24CXX_Write/AT24CXX_OK） | 《EEPROM硬件访问层重构.md》 | ✅ 已落盘 |
| wdg.h / ds1302.c / CRC16 / cJSON / mymalloc | 《WDG重构.md》+《叶子模块重构.md》 | wdg ✅；其余按叶子文档落盘 |
| Display.h（Lcd_ShowWaitNet/Ota_ShowRequestVer） / ProgressBar | 《LCD重构.md》 | ✅ 已落盘 |
| main.h（OTA_STATE_* 0~4 新编号） | 《Main重构.md》§3 | ⬜ 随正式 main 或提前落盘（gprs_rx 依赖新编号） |

**阶段 9 叶子入库清单**（cJSON/mymalloc/DS1302 三项按《叶子模块重构.md》落盘，CRC16 原样入库）：

| boot 文件 | 入库位置 |
|---|---|
| `HARDWARE/CRC16/crc16.c + crc16.h` | `HARDWARE/CRC16/` |
| `HARDWARE/cJSON/cJSON.c + cJSON.h` | `HARDWARE/cJSON/` |
| `HARDWARE/MALLOC/malloc.c + mymalloc.h` | `HARDWARE/MALLOC/`（.c 改名 `mymalloc.c`） |
| `HARDWARE/DS1302/ds1302.c + ds1302.h` | `HARDWARE/DS1302/`（gprs_tx 时间戳依赖） |

**跨文档联动**：

| 文件 | 改动 |
|---|---|
| 《Main重构.md》§4 | 占位接口逐名核对——本文全部定案（`gprs_rx_datas/At_init/cjson_pub_version/gprs_rx_defer_pending/gprs_download_pending/gprs_resend_last_request/gprs_download_context_reset/OTA_RX_*` 与占位一致，无需改写）；GPRS 落盘后 main 即可编译 |
| 《模块迁移顺序.md》 | 阶段 10 行指向本文档（本次同步更新） |
| Keil 工程 | 新增分组：HARDWARE/GPRS（gprs_tx.c/gprs_rx.c）、HARDWARE/MALLOC、HARDWARE/CRC16、HARDWARE/cJSON、HARDWARE/DS1302；gprs.h 头文件路径加入 Include Paths |
