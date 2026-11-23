# GPRS 热重启后 MQTT 必卡 53 秒 —— 根因与修复

> 适用工程：`qibiji_zxl`。模块 USR-WH-LTE-7S1（4G Cat.1），clientID `dev001`，broker `www.huanghe-ceshi.top:59165`。
> 现象确定性复现、根因已定位、修复已实测通过。本文留档备查。

## 现象

同一天内多次上电，出现极整齐的规律：长时间断电后的首次上电很快连上，之后每次热重启（仅复位 MCU，重新烧录 / NRST）首次 MQTT 连接必定卡死——banner 出现后 LINKA 迟迟不拉高，撑满约 53–54 秒的 `offline long` 窗口，触发硬复位，复位后 19–20 秒才连上。

| 上电时刻 | banner 出现 | LINKA 拉高 | 总用时 | 是否硬复位 |
|---|---|---|---|---|
| 09:49（长时间断电后首次） | 09:49:39 | 09:49:50 | 11s | 否 |
| 16:47 热重启 | 16:47:48 | 16:49:01 | 73s | 是（53s 触发） |
| 17:23 热重启 | 17:23:30 | 17:24:43 | 73s | 是（53s 触发） |
| 17:39 热重启 | 17:39:52 | 17:41:05 | 73s | 是（54s 触发） |

三次复位的恢复时间几乎一致（19/20/20s），失败窗口也一致（53/53/54s）。**这种一致性本身不是故障，而是指纹**——它说明这是确定性时序，不是随机弱信号或偶发抖动。

## 关键硬件事实

供电拓扑是本次定性的前提：

- 模块走独立的 TPS563201 buck，+3.8V（实测约 3.76V），只供 WH-LTE-7S1 及其指示灯，前端 470µF + 22µF + 10µF + 3.3µH。
- MCU、存储、RTC、LCD、RS-485 走 AMS1117-3.3。
- 两条轨完全独立。**MCU 热重启时模块从不断电**；MCU 只能通过 PA1 给模块一个 RST 脉冲（热复位模块），无法对模块冷启动。真正的冷启动只有人工断 BAT+ / SW1，那时两轨一起掉，MCU 和模块同时冷启动。

模块侧配置（用有人配置工具读出）：

| 配置项 | 含义 | 当前值 |
|---|---|---|
| Interval of Reconnection | MQTT 断开后到下次重连的间隔 | 5 秒 |
| MQTT heartbeat time | MQTT 协议层 Keep Alive | 60 秒 |
| 无连接重启时间 | 长时间无连接后模块自我重启 | 1800 秒 |

## 被排除的假设

排查过程中有几条看似合理、但被证据否掉的方向，记录于此避免重走：

**「模块首次 connect 失败后不重试」（最初倾向的假设）——被配置推翻。** Interval of Reconnection = 5s 意味着模块每 5 秒会重连一次。53 秒里本该重试约 10 次却一次没成，所以问题不是「模块不重试」，而是「重试一直被挡」。靠调模块重连间隔解决不了。

**「用 `AT+SOCKALK` 查连接状态来区分假设」——方法错误。** 既有文档《GPRS偶发掉线排查与修复》早已实测：MQTT 模式下 SOCKALK 查的不是 MQTT 会话用的 Socket，永远返回 Disconnected，不能反映 MQTT 会话状态。且本工程实际用 PC5 = LINKA，查 Socket A 状态无意义。不要再走这条路。

**「离线探询 AT 干扰了模块 connect」——被日志排除。** `s_offline_probe()` 在整个卡死窗口每 5 秒发 AT，但硬复位后那 20 秒里它照发、照样连上，所以探询并未阻断连接。

**「复位脉冲不够宽 / 复位方式不对」——被对照排除。** 上电复位与运行时复位都是拉同一根 RST 引脚，手段相同，结果却差 4 倍（53s vs 12s），唯一变量是「距上次在线多久」，与脉冲宽度无关。

## 根因

把上面的证据串起来，根因落在 **MCU 热重启时无条件复位了仍在线的模块**：

1. MCU 热重启（重新烧录 / NRST）时，模块独立供电、MQTT 会话健在（60s 心跳由模块自维持，不依赖 MCU）。
2. 旧代码 `GPRS_4G_Init()` 一上电就无条件给 PA1 一个 500ms 复位脉冲，**亲手把这个健康会话杀掉了**——这就是每次 MCU 启动后都会冒出 `WH-LTE-7S1` banner 的原因。
3. 模块重启后重新发起 MQTT 连接，但 broker 侧同 clientID（dev001）的旧会话尚未过期（keepalive 60s，过期点约在 1.5×60≈90s 内），新连接在旧会话过期前一直被挡。
4. LINKA 迟迟不拉高，撑满 `OFFLINE_HARD_RESET_MS = 60s`，触发硬复位。
5. 硬复位执行时旧会话刚好过期，于是「复位后 12 秒连上」——**这不是复位的功劳，是时间刚好把过期耗完了**。

旁证自洽：冷启动（SW1 双轨断电、无旧会话）11 秒连上，与硬复位后的 12 秒几乎相同——那才是模块正常的启动 + 注册 + 连接耗时。只有「紧接着 MCU 热重启」这一种情况存在未过期的旧会话，所以只有它卡 53 秒。冷 / 热不对称到此完全解释。

## 修复

原则一句话：**模块还连着，就不要碰它。** 只在 LINKA 为低（模块确实未连接）时才发上电复位脉冲。

### 1. 拆出纯引脚初始化（`gprs_4g.c`）

把 `GPRS_4G_Init()` 里的 GPIO / EXTI / NVIC 配置原样搬入静态函数 `GPRS_4G_GpioInit()`，它只配引脚、不产生任何复位脉冲。

### 2. 上电复位改为条件触发（`GPRS_4G_Init`）

```c
void GPRS_4G_Init(void)
{
    GPRS_4G_GpioInit();

    /* 模块由独立 +3.8V 供电，MCU 热重启时模块可能仍在线、MQTT 会话健在。
       此时拉复位会杀掉好会话，之后要等 ~50s 陈旧会话过期才能重连。
       只在 LINKA 为低（模块确实未连接）时才发上电复位脉冲。 */
    GPRS_RST_OFF();
    delay_ms(10);
    if (GPRS_LINKA == 0)
    {
        GPRS_RST_ON();
        delay_ms(500);
        GPRS_RST_OFF();
        WDG_Feed();
    }
}
```

### 3. 运行时复位不再重复脉冲（`GPRS_4G_Reset`）

原来 `GPRS_4G_Reset()` 先拉 1s、再调 `GPRS_4G_Init()` 又补 500ms，实际是 1.5s 双脉冲。改为调用 `GPRS_4G_GpioInit()`，只保留 1s 单脉冲：

```c
void GPRS_4G_Reset(void)
{
    s_last_reset_ms = g_sys_ms;
    s_reset_ever = 1u;
    GPRS_RST_ON();
    delay_ms(1000);
    GPRS_RST_OFF();
    WDG_Feed();
    GPRS_4G_GpioInit();   /* 不再调用 Init，避免第二段 500ms 脉冲 */
}
```

### 4. State1 放行条件补「链路已知有效」（`gprs_at.c`）

跳过复位后**不会出现 banner**，而 `g_atflag_online` 只由 banner 置位。State1 的放行条件是：

```c
if (s_link_state && (g_atflag_online || s_link_down_occurred))
```

热重启时 LINKA 直接为高，但 `g_atflag_online = 0`、`s_link_down_occurred` 初值也是 0，条件为假会卡住。因此要在 ST_OFFLINE 分支里补一刀：**LINKA 去抖为高即视为链路有效**。

这里有一个必须避开的坑：`s_link_state` 由 `s_link_debounce()` 维护，要 LINKA **持续高满 `LINK_UP_DEBOUNCE_MS`(500ms)** 才置 1，且 `s_link_raw_prev` 初值为 0，第一次去抖只是「记录变高起点」。所以补刀**不能放在 `s_offline_entry` 里只做一次**——那次 `s_link_state` 必然还是 0，补不上，entry 随即清零，之后不再补，会照样卡满 60s。正确做法是每次循环都判断：

```c
case ST_OFFLINE:
    if (s_offline_entry)
    {
        s_offline_entry = 0u;
        GPRS_Printf("[GPRS] State1: waiting (link=%u atflag=%u down=%u)\r\n",
                    (unsigned)s_link_state, (unsigned)g_atflag_online,
                    (unsigned)s_link_down_occurred);
    }
    /* 热重启：模块未断电仍在线、无 banner；LINKA 去抖为高即视为链路有效 */
    if (s_link_state)
        s_link_down_occurred = 1u;
    s_offline_probe();
    if (s_link_state && (g_atflag_online || s_link_down_occurred))
    { /* 进 State2 */ }
```

冷启动时 LINKA 变高同理补刀，且 banner 也在，双保险；语义未破坏——只有 LINKA 真正稳定为高才补，低时不会误补。

## 验证

实测「冷启动 → 连续 3 次重新烧录（热重启）」，结果干净：

| 段 | 类型 | State1 entry → link up | banner | 硬复位 | 复位 → timeSync ok |
|---|---|---|---|---|---|
| 10:19:33 | 冷启动 | 20s（含模块启动+注册） | 有 | 无 | ~24s |
| 10:20:38 | 热重启 load1 | **0.52s** | **无** | 无 | ~4s |
| 10:21:18 | 热重启 load2 | **0.52s** | **无** | 无 | ~4s |
| 10:21:44 | 热重启 load3 | **0.52s** | **无** | 无 | ~4s |

- 冷启动走原路径：pc5 从 0 起步 → 出 banner → 注册连 MQTT → banner 后 11.5s link up，正常。
- 三次热重启均 **0.52s** link up（≈ 500ms 去抖），**无 banner、无 probe 等待、无硬复位**，4 秒内 timeSync、心跳、属性上报全部完成，心跳 `miss=0`，serverTime 连续。

对比修复前热重启约 73 秒，现在 **0.5s link up / 约 4s 全链路**，三次分毫不差，确定性证明修复生效，也反过来坐实了根因诊断。

## 边界与遗留

- **90 秒内再次冷启动仍可能卡**：冷启动把模块也断电了，模块必须重新连 broker，若旧会话（keepalive 60s）未过期仍会撞闸门。这属模块 + broker 层行为，MCU 无法避免，不在本次修复范围。本次只解决「热重启（模块不断电）」这条路。
- **该复位时仍会复位**：LINKA 低（无信号 / 模块未连接）时条件复位照常发脉冲，60s 硬复位兜底也保留，未削弱任何原有自愈能力。
- **唯一新增风险**：若模块「假装在线」（LINKA 高但 socket 半死），跳过复位会让它多撑一会儿；现有 `LINK_DOWN_DEBOUNCE_MS = 2000` + 60s 硬复位仍能兜底，可接受。

## 可选的后续验证

- 真断电冷启动（SW1 双轨断）再确认一次首次上电路径。
- 拔天线 / 屏蔽信号，确认 LINKA 低时条件复位仍发脉冲、60s 硬复位兜底在。
- 长时间在线运行数小时，确认条件复位不会在不该发脉冲时误发（只有 `GPRS_4G_Init` 走该判断，运行期恢复仍走 `GPRS_4G_Reset`）。

## 代码锚点

| 位置 | 内容 |
|---|---|
| `gprs_4g.h:15-17` | `GPRS_LINKA`（PC5）、`GPRS_RST_ON/OFF`（PA1）引脚宏 |
| `gprs_4g.c:18-51` | `GPRS_4G_GpioInit()`：纯 GPIO/EXTI/NVIC 初始化，无脉冲 |
| `gprs_4g.c:53-70` | `GPRS_4G_Init()`：条件上电复位（LINKA 低才发 500ms 脉冲） |
| `gprs_4g.c:73-83` | `GPRS_4G_Reset()`：运行期 1s 单脉冲 + 调 GpioInit |
| `gprs_at.c:95-118` | `s_link_debounce()`：LINKA 去抖，up 500ms / down 2000ms |
| `gprs_at.c` ST_OFFLINE 分支 | 补刀「LINKA 去抖为高即置 `s_link_down_occurred`」+ 放行条件 |
