# GPRS 离线探询与模块无回包排查

> 适用工程：`qibiji_zxl`（重构版）
> 关联文档：[GPRS重构操作建议.md](./GPRS重构操作建议.md)、[GPRS重构操作建议_阶段二起.md](./GPRS重构操作建议_阶段二起.md)

## 1. 问题现象

设备上电后调试串口只打印一行：

```
[GPRS] State1: waiting (link=0 atflag=0 down=0)
```

之后既没有 `send at-->`，也没有任何 `RX[n]`，模块身份（ICCID / MQTTCID）当然也拿不到。

## 2. 原因分析

原状态机的出发条件是：

```c
if (s_link_state && (g_atflag_online || s_link_down_occurred))   /* -> ST_AT_PUSH */
```

三个量全为 0 时形成**自锁**：

| 量 | 含义 | 为 0 的后果 |
|---|---|---|
| `s_link_state` | LINKA(PC5) 去抖后的链路状态 | 模块未注网 / 未连服务器 |
| `g_atflag_online` | 是否收到模块启动 banner `WH-LTE-7S1` | 串口上没收到过模块任何字节 |
| `s_link_down_occurred` | 是否发生过掉线边沿 | 首次上电不会置位 |

即：**想查“模块里是哪台设备”，却要求模块先连上服务器才允许发 AT**。加上 banner 可能因初始化顺序被错过，就会出现“永远不发 AT → 永远没有回包 → 永远看不到设备信息”。

## 3. 变更清单

### 3.1 `USER/main.c`：串口先于复位

模块复位后会立刻打印 banner，`GPRS_4G_Init()` 内部有 PA1 高 500 ms 的复位脉冲。原来先复位后初始化 USART2，banner 期间串口还没起来，`atflag_online` 永远为 0。

```c
    GPRS_PORT_Init(115200);  /* USART2 to 4G module */
    GPRS_4G_Init();          /* module reset + LINKA EXTI -- after UART so the boot banner is captured */
```

> 原工程 `qibiji/USER/main.c` 同样是 `uart2_init(115200)`（L33）在 `GM4G_Init()`（L44）之前，此改动是把重构版拉回与原工程一致。

### 3.2 `HARDWARE/GPRS/gprs_config.h`：新增裸 AT 宏

用于区分“模块在 AT 命令模式”还是“还在透传模式”：

```c
/* 裸 AT：模块若本来就处于 AT 命令模式，回 OK；用它与带前缀的命令做区分 */
#define GPRS_AT_PING       "AT\r\n"
#define GPRS_AT_PING_LEN   4u
```

### 3.3 `HARDWARE/GPRS/gprs_at.c`：探询周期宏

```c
#define OFFLINE_PROBE_MS      5000u                   /* 离线时主动探询模块的周期 */
```

### 3.4 `HARDWARE/GPRS/gprs_at.c`：回调打印

`ICCID` / `MQTTCID` 回调原来只置标志，AT 回包内容被丢弃；补上打印（`GPRS_Printf` 底层是 `vsnprintf`，`%.*s` 可用，且不要求缓冲区以 `\0` 结尾）：

```c
static void s_on_iccid(char *data, uint16_t len)
{
    if (strstr(data, "+ICCID") != NULL)
        g_at_flag_iccid = 1u;
    GPRS_Printf("[GPRS] ICCID: %.*s\r\n", (int)len, data);
}

static void s_on_mqttcid(char *data, uint16_t len)
{
    if (strstr(data, "+MQTTCID") != NULL)
        g_at_flag_mqttcid = 1u;
    GPRS_Printf("[GPRS] MQTTCID: %.*s\r\n", (int)len, data);
}

static void s_on_csq(char *data, uint16_t len)
{
    if (strstr(data, "+CSQ") != NULL)
        g_at_flag_csq = 1u;
    GPRS_Printf("[GPRS] CSQ: %.*s\r\n", (int)len, data);
}
```

### 3.5 `HARDWARE/GPRS/gprs_at.c`：离线探询函数（核心）

不等 LINKA，每 5 s 主动问模块一条命令；同时打印 PC4/PC5 原始电平与 banner 标志，用于顺带核对 LINKA 引脚定义：

```c
/**
 * @brief 离线探询：LINKA 为低时状态机不会发任何 AT，导致永远看不到模块信息。
 * 每 OFFLINE_PROBE_MS 轮询一条命令，先确认串口/波特率/模块是否在线，
 * 同时打印 PC4/PC5 电平，用于判断 LINKA 引脚定义是否与实际硬件一致。
 */
static void s_offline_probe(void)
{
    static uint8_t  s_probe_idx = 0u;
    static uint32_t s_probe_tick = 0u;

    if ((uint32_t)(g_sys_ms - s_probe_tick) < OFFLINE_PROBE_MS)
        return;
    if (!UartTx_IsEmpty())
        return;

    s_probe_tick = g_sys_ms;
    GPRS_Printf("[GPRS] probe %u: pc4=%u pc5=%u banner=%u\r\n",
                (unsigned)s_probe_idx,
                (unsigned)GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_4),
                (unsigned)GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_5),
                (unsigned)g_atflag_online);

    switch (s_probe_idx)
    {
    case 0u:   /* 裸 AT：回 OK 说明串口通、模块在 AT 命令模式 */
        (void)UartTx_Push_AT(GPRS_AT_PING, GPRS_AT_PING_LEN, "OK", NULL, 2000u, 0u);
        break;
    case 1u:
        (void)UartTx_Push_AT(GPRS_AT_MQTTCID, GPRS_AT_MQTTCID_LEN, "+MQTTCID",
                             s_on_mqttcid, 2000u, 0u);
        break;
    case 2u:
        (void)UartTx_Push_AT(GPRS_AT_ICCID, GPRS_AT_ICCID_LEN, "+ICCID",
                             s_on_iccid, 2000u, 0u);
        break;
    default:
        (void)UartTx_Push_AT(GPRS_AT_CSQ, GPRS_AT_CSQ_LEN, "+CSQ",
                             s_on_csq, 2000u, 0u);
        break;
    }
    s_probe_idx = (uint8_t)((s_probe_idx + 1u) % 4u);
}
```

设计要点：

- 只在 `ST_OFFLINE` 调用，进入 `ST_NORMAL` 后自然停止，不影响正式流程；
- `UartTx_IsEmpty()` 作为前置条件，保证一次只有一条 AT 在飞；
- `timeout_ms = 2000`、`max_retry = 0`，避免无回包时长时间占住队列（默认配置是 1000 ms × 2 次重试）。

### 3.6 `HARDWARE/GPRS/gprs_at.c`：状态机调用点

```c
    case ST_OFFLINE:
        if (s_offline_entry)
        {
            s_offline_entry = 0u;
            GPRS_Printf("[GPRS] State1: waiting (link=%u atflag=%u down=%u)\r\n",
                        (unsigned)s_link_state, (unsigned)g_atflag_online,
                        (unsigned)s_link_down_occurred);
        }
        s_offline_probe();   /* 不等 LINKA：先把模块的真实回包抓出来 */
```

### 3.7 `HARDWARE/GPRS/gprs_at.c`：打印原始帧内容

`GPRS_AT_RxPump()` 里原来只打长度，收不到内容就无法判断“有没有数据 / 是不是乱码”。现在直接打内容，且因为 `GPRS_Printf` 内部缓冲只有 256 B（`+MQTTPUBTP` 回包有 428 B），超过 200 B 的帧按 200 B 分块输出：

```c
        if (len > 200u)
        {
            /* [GPRS] RX[428] +++  /  200 B 一块  /  --- end --- */
        }
        else
        {
            GPRS_Printf("[GPRS] RX[%u] %.*s\r\n", (unsigned)len, (int)len, buf);
        }
```

## 4. 日志判读表

| 现象 | 结论 |
|---|---|
| 有 `RX[n] ...` 且裸 AT 回 `OK` | 串口通，但 `usr.cn#` 前缀不适用，应改用裸 `AT+MQTTCID?` 等 |
| 有 `RX[n]` 但内容乱码 | 波特率不匹配（模块可能被配成 9600），改 `GPRS_PORT_Init()` 实参 |
| 完全没有 `RX[n]` | 模块没上电、TX/RX 接线断、或一直处于复位状态 —— 与代码参数无关 |
| `pc4=1 pc5=0` 且模块已联网 | LINK 实际接在 PC4，需把 `GPRS_LINKA` 改成 `GPIO_Pin_4`（当前硬件为 PC5，见下节） |
| 出现 `[GPRS] MQTTCID: ...` | 拿到模块内真实 ClientID，可与平台设备列表核对 |
| 出现 `[GPRS] State3->4` | 链路与 AT 初始化全部完成，进入 NORMAL |

## 5. 与原工程的一致性核对

| 项目 | 原工程 `qibiji` | 重构版 `qibiji_zxl` | 结论 |
|---|---|---|---|
| 波特率 | `uart2_init(115200)` | `GPRS_PORT_Init(115200)` | 一致 |
| 串口引脚 | PA2 TX / PA3 RX | PA2 / PA3 | 一致 |
| LINKA | `PCin(5)` | `GPIO_Pin_5` | 一致（PC4 = LINKB） |
| 复位 | PA1 高 500 ms → 低 | 相同 | 一致（PA1 高 → Q3 导通 → RESET 低有效） |
| AT 命令 | `usr.cn#AT+ICCID?/MQTTCID?/CSQ?` | 完全相同 | 一致 |
| 进 NORMAL 条件 | `link && (atflag \|\| down)` | 相同 | 一致 |
| RX 通路 | DMA1_CH6 + IDLE + 双槽暂存 | 相同 | 一致 |
| banner 字符串 | `WH-LTE-7S1` | 相同 | 一致 |
| 初始化顺序 | uart2 先于 GM4G_Init | 本次已同步 | 一致 |

硬件侧（`qibiji/hard/hardware_report_assets/03_4g_uart_reset.svg`）：

- 模块 U30 = WH-LTE-7S1，主电源 **pin16 +3.8V**（与 MCU 3.3V 不是同一路）；
- `PA1 high → Q3 ON → RESET low`，即高电平复位、低电平运行；
- `POWERKEY / WORK / LED_NET 未连接`，模块上电自启；
- 原理图未体现外部 SIM 网络，若实板确实无 SIM，则 LINKA 永远不会拉高，原工程与重构版都会停在 State1。

## 6. 结论与后续

代码参数（波特率、引脚、复位时序、AT 命令、banner 字符串）与原工程逐项一致，重构版这一层没有配错。若探询仍是“零回包”，问题在模块侧：

1. 量模块 pin16 是否有 3.8V；
2. 量模块 AT_TXD(pin6) 在复位后是否有输出；
3. 确认 SIM 是否接入。

探询代码属于**排障手段**，正式版本可选择：

- 保留（模块信息可周期性自检，代价是离线时每 5 s 一条 AT）；
- 或加编译开关，例如 `#if GPRS_OFFLINE_PROBE`，发布版关闭。
