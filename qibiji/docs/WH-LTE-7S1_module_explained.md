# WH-LTE-7S1 4G Cat.1 模块详解（含 5 个指示引脚与本项目使用情况）

> 适用范围：`qibiji/HARDWARE/4G/4G.c`、`4G.h`、`qibiji/HARDWARE/GPRS/gprs_AT.c`、`uart3_at_sequence.c`、`USER/main.c`，以及 `qibiji/hard/hardware_report_assets/元件-网络明细.csv`、`QIBIJI_RTU_V1.1_硬件分析报告.html`。
> 本文只做静态解读与资料核对，**未修改任何 `.c` / `.h` 源码**。
> 原厂资料：《WH-LTE-7S1 / WH-LTE-7S1-GN 说明书》V1.0.5 §3.2.11 表 17、§3.2.12 表 18；《WH-LTE-7S1-GN 硬件设计手册》V1.0.1 §3.5 表 8。
> 配套阅读：`docs/Gprs_Process_explained.md`（MQTT/JSON 上行七步流水线、banner 与四层证据）、`docs/Protocols_explained.md`（三条协议链路对照）、`docs/长连接在线保证说明.md`（心跳与自愈时序）。

---

## 0. 一句话总览

**WH-LTE-7S1 是有人物联网的 4G Cat.1 通信模块，本工程里它包办全部对外通信——MQTT 上行、AT 交互、SIM 卡特情都发生在模块内部，MCU 只通过串口发 AT 命令，不自己实现 TCP/MQTT。板子上从它引出 6 根有用的线（RESET、LINKA、LINKB、AT_TXD、AT_RXD、电源/地），5 个指示引脚里只接了 LINKA 和 LINKB，另外 3 个（NET / WORK / DATA）在 BOM 里连脚都没引出。**

打比方：模块是「专职司机」，MCU 是「发货员」。发货员只说「把这箱货发到 3 号槽位」，找路、开车、确认送达全是司机的事。司机车头有 5 盏灯，但驾驶室（MCU）只接了其中两盏的线。

---

## 1. 模块是什么、在干什么

| 项 | 内容 |
|---|---|
| 型号 | WH-LTE-7S1（有人物联网 / USR IOT，BOM 位号 U30） |
| 类别 | 4G Cat.1 无线通信模块，DIP 23 pin |
| 制式 | FDD-LTE Band 1/3/5/8、TDD-LTE Band 38/39/40/41，兼容 GSM 900/1800 |
| 供电 | 3.4~4.2V（板上 +3.8V），AT 串口 TTL 3.0V |
| 网络能力 | TCP/UDP/DNS/FTP/HTTP/SMS 透传，最多 4 路 Socket；本项目用它的 **MQTT** 能力 |
| 配置方式 | AT 命令（`usr.cn#` 前缀为有人私有扩展） |
| BOM 固件库 | `USR-GPRS232-7S3_LIB_V1.0`（通用库命名，含 7S3 字样） |

在本工程里它的角色是**上行主链路的唯一出口**：

- MCU 通过 `AT+MQTTPUBTP=1..8` 把 `iot/v1/{pk}/{dk}/up/...` 配成 8 个 topic 槽位（`uart3_at_sequence.c`），槽位宏在 `gprs.h`（`Topic_Pub=1` / `Topic_propertyPub=3` / `Topic_evenPub=4` / `Topic_ServiceReply=6` / `Topic_RequestInvoke=8`）。
- 发业务数据时 MCU 只发 `"<槽位号>,<JSON>"`，心跳是紧凑帧 `2,{}`（`net_liveness.c`）。
- 所以 MCU 侧串口活很轻，重活（TCP 连接、MQTT 会话、心跳、重连）全在模块内部。

---

## 2. 模块与 MCU 的接线总览

| 信号 | 模块脚 | MCU 脚 | 方向 | 用途 | 本项目状态 |
|---|---|---|---|---|---|
| RESET | pin 19 | PA1（经 Q3 反相） | MCU → 模块 | 复位脉冲（低有效，Q3 反相） | 在用 |
| LINKA | pin 1 | PC5 | 模块 → MCU | Socket A 连接状态指示 | 在用 |
| LINKB | pin 2 | PC4 | 模块 → MCU | Socket B 连接状态指示 | 初始化了但未读取 |
| AT_TXD | pin 6 | USART2 RX | 模块 → MCU | AT 回执 / 业务 JSON 下行 | 在用 |
| AT_RXD | pin 7 | USART2 TX | MCU → 模块 | AT 命令 / 业务 JSON 上行 | 在用 |
| VCC | pin 16 | +3.8V | — | 模块供电 | 在用 |
| GND | pin 11 / 12 | GND | — | 地 | 在用 |
| NET | pin 15 | — | — | 网络注册/驻网指示 | **未引出** |
| WORK | pin 9 | — | — | 模块工作指示 | **未引出** |
| DATA | — | — | — | 数据传输指示 | **未引出** |

复位电路：PA1 高电平使 Q3 导通、把模块 RESET 拉低；PA1 低/高阻时 R13 把 RESET 拉高。C6(100nF) + R13(100kΩ) 约 10ms 时间常数，复位释放较缓。**注意 RESET 是低有效**，PA1 到 RESET 之间是反相关系（`QIBIJI_RTU_V1.1_硬件分析报告.html` §5.3）。

AT 通道走 USART2（`uart2_init(115200)`），配置序列代码在 `uart3_at_sequence.c`；USART1 是 PC 调试口（115200，`printf` 重定向到它）；UART4 是 RS485 Modbus 链路，与 4G 模块无关。

---

## 3. AT 命令体系：MCU 怎么跟模块说话

### 3.1 一句话总览

**所有 AT 都走 USART2（115200），而且每条都带 `usr.cn#` 前缀**——这是有人物联的私有扩展，作用是「在透传/MQTT 数据模式下也能直接下发 AT，不必先复位等 boot banner」。据此，工程里的 AT 分成两拨：

- **近场配置序列**（`uart3_at_sequence.c`）：把 MQTT 参数一次性烧进模块，共 **23 条**，只在需要重配时跑。
- **运行时探活**（`gprs_AT.c` 状态机）：每次联网后进 NORMAL 前查 **3 条**，全过才放行业务。

一句人话：前者是「装宽带时把账号密码和常用网址存进路由器」，后者是「每次开机先问一下路由器通不通、信号好不好，通了才让发数据」。

### 3.2 为什么加 `usr.cn#` 前缀

标准 AT 命令要求模块处于「命令模式」；一旦进了透传或 MQTT 数据模式，串口收到的字节会被当成业务数据转发，AT 就失效了。`usr.cn#` 前缀让模块在**数据流里也能识别出「这是给我下的命令」**，所以 `uart3_at_sequence.c:368` 才能写「不必先 RST 等 boot banner」就直接压命令。

这也解释了 banner（模块开机主动吐的启动信息）的价值边界：它证明的是「模块端已就绪」，而不是「能不能发 AT」——后者靠 `usr.cn#` 前缀本身就解决了。

### 3.3 近场配置序列：23 条命令一次性配好 MQTT

**触发与参数来源**：`uart3_at_sequence_request_set()` 置请求标志，主循环轮询 `uart3_at_sequence_poll()` 执行。productKey / deviceKey / deviceSecret / addr 四个参数从 EEPROM 读（各 32 字节），任何一个为空（或全 `0xFF`）整条序列放弃，不会带着半成品参数往下发。

23 条按功能分 6 组：

| 组 | 序号 | 命令 | 作用 |
|---|---|---|---|
| 探活 | 0 | `usr.cn#AT+CSQ?` | 先确认串口链路通、模块能应答 |
| 工作模式 | 1 | `usr.cn#AT+WKMOD=MQTT,NOR` | 切到 MQTT 模式 |
| 连接参数 | 2–5 | `AT+MQTTSVR` / `AT+MQTTUSER` / `AT+MQTTPSW` / `AT+MQTTCID` | 服务器、用户名、密码、客户端 ID |
| MQTT 开关 | 6–7 | `AT+MQTTMOD=1` / `AT+HEARTEN=OFF` | 开 MQTT / 关模块自带心跳（心跳改由 MCU 控制） |
| 发布槽 | 8–15 | `AT+MQTTPUBTP=1..8` | 定义 8 个上行 topic 槽位 |
| 订阅槽 | 16–20 | `AT+MQTTSUBTP=1..5` | 定义 5 个下行 topic 槽位 |
| 收尾 | 21 | `AT+CACHEN=OFF`（可选） | 关模块串口缓存 |
| 收尾 | 22 | `AT+S` | 存盘并重启 |

**槽位号机制（关键点）**：完整 topic 路径**只在配置时下发这一次**，之后 MCU 发业务数据只发「槽位号,JSON」，模块自己按槽位查表还原完整 topic。省带宽，也让业务代码不用拼长字符串。

8 个发布槽位与 `gprs.h` 的宏对应：

| 槽 | topic 尾 | 代码宏 | 用途 |
|---|---|---|---|
| 1 | `up/sys/online` | `Topic_Pub` | 上线 |
| 2 | `up/sys/heartbeat` | — | 心跳，实际帧就是 `2,{}` |
| 3 | `up/property/post` | `Topic_propertyPub` | 属性上报 |
| 4 | `up/event/post` | `Topic_evenPub` | 事件 |
| 5 | `up/alarm/post` | — | 告警 |
| 6 | `up/service/reply` | `Topic_ServiceReply` | 服务回执 |
| 7 | `up/ota/request` | — | OTA 请求 |
| 8 | `up/request/invoke` | `Topic_RequestInvoke` | 服务调用 |

5 个订阅槽位：`down/service/invoke`、`down/config/push`、`down/property/get`、`down/ota/reply`、`down/request/reply`。

**执行时序与失败处理**（`uart3_at_sequence.c:345-417`）：

- 每条命令发完等 **2000ms**（`AT_WAIT_OK_MS`），命令之间隔 **50ms**，接收方静默 **40ms** 认一帧。
- 应答里含 `ERROR` / `+CME ERROR` 判为模块拒绝；不含 `OK` 判为「no OK」；超时也算失败。
- 失败处理分两种：`AT+CACHEN=OFF` 是「尽力而为」步骤（`BC_OPT` 定义），模块不支持只告警跳过；其余任何一条失败就整条序列 `abort`，不会带着半套配置继续。
- 23 条全过才 `mqtt_keys_save_to_eeprom()` 保存参数并打印 `all commands OK -> system reset`。

**`AT+CACHEN=OFF` 为什么值得单列**：关掉的是模块的「串口数据缓存」。不关的话，服务器不可用期间的上行会被模块缓存，链路恢复后成批重放——现场表现是「一次收到 20 多条旧对时回执」。所以这条虽是可选的，语义上却很关键。

### 3.4 运行时探活：进 NORMAL 前的 3 条 AT

`gprs_AT.c` 状态机进入状态 2 时，依次往发送队列入队 3 条（`gprs_AT.c:209-229`）：

| 命令 | 期望响应关键字 | 处理函数 | 置位标志 |
|---|---|---|---|
| `usr.cn#AT+ICCID?` | `+ICCID` | `handleAT_ICCID()` | `ATflag_ICCID` |
| `usr.cn#AT+MQTTCID?` | `+MQTTCID` | `handleAT_MQTTCID()` | `ATflag_MQTTCID` |
| `usr.cn#AT+CSQ?` | `+CSQ` | `handleAT_CSQ()` | `ATflag_CSQ` |

三条的 `ATflag_*` 全部置 1（`gprs_AT.c:240`）才允许进状态 4 NORMAL。进 NORMAL 后 CSQ 每 **10 分钟**再查一次作为周期信号质量监测。

注意这里的 CSQ 和第 0 条配置序列里的 CSQ 是**两回事**：配置序列那条是「探活，确认串口能对话」，状态机这条是「进业务前的三项检查之一」。

### 3.5 一条完整的上下行

以心跳为例：`net_liveness.c` 组织出 `2,{}` → 进 UartTx 队列 → USART2 TX 发给模块 → 模块按槽位 2 查表还原出 `iot/v1/{pk}/{dk}/up/sys/heartbeat` → MQTT 发布。

下行反过来：模块收到订阅 topic 的数据 → USART2 RX → DMA + IDLE 中断 → `UartRx_Pop()` 补成 C 字符串 → 主循环 `Parse_Process()` 按 JSON 分支处理。`AT` 回执和 JSON 业务数据走的是同一个串口、同一条接收链，靠「JSON 提取是否成功」来分流（详见 `docs/Gprs_Process_explained.md` §2.2）。

### 3.6 四条易混淆命令的拼写核实（WKMOD / HEARTEN / SOCKASL / SOCKALK）

这四条命令名**拼写都正确**，都是 WH-LTE-7S1 原厂 AT 指令集里的标准指令。它们分属不同功能组，别因为名字相近就当成一类：

| 命令 | 功能组 | 功能 | 参数 / 取值 | 核实 |
|---|---|---|---|---|
| `AT+WKMOD` | 通用指令 | 查询 / 设置工作模式 | `NET`（透传）、`MQTT`、`SMS` 等；`MQTT,NOR` 为 MQTT 正常分发模式 | ✅ 原厂标准 |
| `AT+HEARTEN` | 心跳包指令 | 查询 / 设置心跳包使能 | `ON` / `OFF` | ✅ 原厂标准 |
| `AT+SOCKASL` | 网络参数指令 | 查询 / 设置 Socket A 连接方式（长 / 短连接） | `LONG` / `SHORT` | ✅ 原厂标准 |
| `AT+SOCKALK` | 网络参数指令 | 查询 Socket A 连接状态 | 只读，返回连接与否 | ✅ 原厂标准 |

要点：

- **`WKMOD` 决定模块「按哪套协议跑」**：切到 `MQTT` 后由模块内置 MQTT 客户端接管对外通信，`NET` 才是简单透传。它是整个配置的第一步。
- **`HEARTEN` 管的是模块自带心跳包**（透传模式下按间隔往网络 / 串口发固定字符串），跟 MQTT 协议层的保活不是一回事。
- **`SOCKASL` 与 `SOCKALK` 一字之差、用途完全不同**：`SOCKASL` 是**设置**项（长连接还是短连接，默认 `LONG`）；`SOCKALK` 是**查询**项（此刻 Socket A 连没连上，只读）。原厂不同版本手册对 `SOCKASL` 的措辞略有差异——V1.0.0 写「短连接使能」、V1.0.8 写「连接方式 `LONG`」，指的是同一件事。
- 这几条命令**本体是 `AT+xxx`**；工程下发时前面加的 `usr.cn#` 是私有前缀（见 §3.2），属于「透传 / MQTT 状态下仍能识别命令」的唤醒机制，不是命令名的一部分。

---

## 4. 五个指示引脚（重点）

原厂说明书 §3.2.11 明确：WH-LTE-7S1 有 **5 个指示灯引脚**——WORK、NET、LINKA、LINKB、DATA，**点亮都是高电平有效**。硬件手册表 8 另注明：引出的指示灯引脚为 LINKA(pin 1)、LINKB(pin 2)、WORK(pin 9)、NET(pin 15)，电平 1.8V（7S1-GN 版）；7S1-E 版为 3.0V。本项目硬件报告实测逻辑高约 **3.0V**，与 3.3V MCU 兼容。

### 4.1 总表

| 引脚 | 原厂定义 | 电平行为 | 是否闪烁 | 本项目 |
|---|---|---|---|---|
| `WORK` | 模块工作正常指示 | 正常工作时 1s 高 / 1s 低 | **闪**（慢） | 未引出 |
| `NET` | 网络状态指示（驻网指示） | **驻网后闪烁**：4G 闪 4 次、2G 闪 2 次 | **闪**（一次性节奏） | 未引出 |
| `LINKA` | **Socket A 连接指示** | 连接建立→**稳定高**，断开→低 | **不闪** | **接 PC5，在用** |
| `LINKB` | **Socket B 连接指示** | 同上 | **不闪** | 接 PC4，只配置未读取 |
| `DATA` | 数据传输指示 | 串口或网络有数据发送时高、发送完成低 | **闪**（随数据流） | 未引出 |

一句话记：**NET 是有没有信号，LINKA 是路通没通，DATA 是路上有没有车。**

### 4.2 WORK —— 模块工作指示

- 原厂定义：模块正常工作时输出 1s 高、1s 低的方波，相当于「心跳灯」，证明模块主芯片在跑。
- 本项目：**BOM 里 U30 没有 pin 9，未引出**。MCU 无从得知模块工作状态，只能靠 AT 是否有回执间接判断。

### 4.3 NET —— 网络注册 / 驻网指示

- 原厂定义：网络状态指示。**连接到网络后闪烁，4G 闪 4 次、2G 闪 2 次**，用来告诉使用者「注册上基站了」。
- 这是真正的「网络注册指示」，但它**是闪烁信号，不是稳定电平**。
- 本项目：**BOM 里 U30 没有 pin 15，未引出**。所以文档里如果说「LINKA 证明模块跟基站注册上了」是**叫错了名字**——基站注册这件事，硬件上根本没有信号线给 MCU。

### 4.4 LINKA —— Socket A 连接指示（本项目核心）

- 原厂定义：**Socket A 的连接状态指示**。模块作为 TCP 客户端与对端建立连接后输出**稳定高电平**，连接断开则拉低。
- 它**不闪烁**。这是关键：正因为它稳定，才能用「低电平持续 2000ms 判掉线、高电平持续 500ms 判恢复」的软件去抖；如果它是发送时闪的数据灯，低电平根本持续不到 2 秒，去抖逻辑永远不会触发。
- 「Socket A」指的是模块内部 4 路 Socket 中的第 1 路。原厂手册只说「Socket A 连接」，并没写死对端一定是云服务器；但本项目把它配成 MQTT 上行通道，所以这里的高电平实际含义是「**到云平台的 TCP 连接建立了**」。
- 本项目：接 PC5，配下拉输入（`GPIO_Mode_IPD`），开 EXTI5 双边沿中断（`4G.c:56-59, 68-74`），沿中断里读电平置 `gprsonlineflag`（`4G.c:114-135`）。

### 4.5 LINKB —— Socket B 连接指示

- 原厂定义：Socket B 的连接状态指示，行为与 LINKA 相同（连接建立高、断开低）。
- 本项目：接 PC4，同样配下拉输入（`4G.c:51-54`），但**全工程没有任何地方读取它**——初始化了却没用。属于预留/冗余。

### 4.6 DATA —— 数据传输指示

- 原厂定义：串口或网络有数据发送时输出高电平，发送完成拉低。这才是「数据活动指示」——**随数据流闪烁**。
- 本项目：**未引出**。所以 MCU 看不到「正在发数据」这个物理层证据，只能靠发送队列状态和心跳回执间接推断。

### 4.7 电平兼容提醒

硬件报告指出：LINKA/LINKB 直接连 PC5/PC4，模块逻辑高约 3.0V，对 3.3V MCU 在有效高电平范围内。但原厂 7S1-GN 硬件手册写指示灯电平是 **1.8V**，7S1-E 版是 **3.0V**——两版不同。**若将来换料或替代型号，必须重新确认电平并做匹配**（原厂明确提示：用 7S3/7S4 兼容型号时，MCU 引脚不能与模块指示灯引脚直连，需做电平匹配电路）。

---

## 5. 本项目使用情况小结

**用了什么：**
- RESET（PA1）：软件复位模块，有 5 分钟节流、阻塞约 1 秒（`GM4G_Reset()`）。
- LINKA（PC5）：唯一的链路状态输入，经过去抖和状态机，决定能不能发业务数据。
- AT 串口（USART2）：AT 配置 + 业务 JSON 上下行。
- 模块内部 MQTT： topic 槽位 + 私有 JSON，全部在模块里跑。

**没用什么、有什么后果：**

| 未用信号 | 后果 |
|---|---|
| NET（驻网指示） | 拿不到「基站注册」的直接证据，只能靠 LINKA（TCP 连接）+ banner 间接推断 |
| WORK（工作指示） | 模块主芯片是否活着，只能靠 AT 有无回执判断 |
| DATA（数据活动指示） | 看不到物理层发数据动作，靠发送队列和心跳回执替代 |
| LINKB（已接但没读） | 纯冗余，第二路 Socket 状态从未被使用 |

---

## 6. LINKA 的完整处理链

从引脚电平到「业务放行」，一共四步：

**① 硬件采样**：PC5 配下拉输入，EXTI5 上升沿+下降沿都触发中断（`EXTI_Trigger_Rising_Falling`）。

**② 软件去抖**（`gprs_AT.c:72-101` `gprs_link_debounce_update()`）：

```
raw = GPRS_LINKA ? 1 : 0
若 raw 发生变化 → 记录 link_raw_change_tick，本轮不提交
若 raw 稳定：
    raw=1 且 link_state=0 且持续 >= 500ms  → link_state = 1（确认上线）
    raw=0 且 link_state=1 且持续 >= 2000ms → link_state = 0，返回 1（确认掉线）
```

宏定义在 `gprs_AT.c:44-45`：`GPRS_LINK_DOWN_DEBOUNCE_MS = 2000`、`GPRS_LINK_UP_DEBOUNCE_MS = 500`。

**③ 状态机响应**（`gprs_AT.c:113-145`）：确认掉线才返回 1，把状态机打回 `GPRS_STATE_OFFLINE`（状态 1），清掉 `atflag_online` 等标志、丢弃在途 AT/JSON 队列、终止对时。**注意：不立即硬复位模块**，先给模块自愈机会；离线持续 60 秒才允许硬复位一次，且两次硬复位间隔至少 5 分钟，避免复位风暴。

**④ 放行条件**（`gprs_AT.c:177`）：状态 1 要往下走，需要 `link_state && (atflag_online || link_down_occurred)`。掉线恢复时模块不会再吐一次 banner，所以用 `link_down_occurred` 那半边放行。

---

## 7. 四层证据：别把「联网了」当成一件事

现场排障必须逐层看，不能拿 LINKA 高就当「平台在线」：

| 层 | 靠什么证明 | 变量名 | 说明 |
|---|---|---|---|
| ① TCP 通道 | LINKA 去抖后的状态 | `link_state` | 只说明 Socket A 连上了 |
| ② 模块可对话 | 上电/复位后主动吐的 banner | `atflag_online` | 模块串口启动完、能收 AT |
| ③ 可发业务 | 三条 AT（ICCID / MQTTCID / CSQ）都过 | `gprs_is_ready()` = NORMAL | 状态机放行 |
| ④ 平台真收到 | 心跳回执 | 240 秒内有回执 | 云端确认 |

**banner 为什么重要**：`link_state`（LINKA 高）只证明 TCP 连接建立了，但模块串口可能还没启动完。此时若 MCU 一看见 LINKA 就压三条 AT，模块答不了 → AT 全部超时 → 重试耗尽 → 进 30 分钟长退避，现场表现就是「上电半小时不上线」。banner 是 MCU 能拿到的唯一一个「模块端已就绪」的主动证据。

---

## 8. 常见误解澄清

1. **「LINKA 是网络注册指示」——不对。** 网络注册是 NET 引脚（闪烁）的活，而且 NET 没接。LINKA 是 Socket A 的 TCP 连接状态。
2. **「LINKA 是数据活动指示，发送时闪」——不对。** 数据活动是 DATA 引脚的活，也没接。LINKA 是稳定电平，不闪。
3. **「LINKA 高 = 平台在线」——不准确。** 只到第一层（TCP 通道通），后面还有 banner、三条 AT、心跳回执三关。
4. **「函数名是 `s_link_debounce()`、宏是 `LINK_DOWN_DEBOUNCE_MS`」——与源码不符。** 实际是 `gprs_link_debounce_update()`（`gprs_AT.c:76`），宏是 `GPRS_LINK_DOWN_DEBOUNCE_MS` / `GPRS_LINK_UP_DEBOUNCE_MS`（`gprs_AT.c:44-45`）。

---

## 9. 代码审查发现（仅记录，未修改）

1. **去抖宏命名**：`GPRS_LINK_DOWN_DEBOUNCE_MS / UP` 只描述「链路」，没点明是 Socket A 连接；结合原厂语义，注释里应写清「Socket A 连接建立/断开」，避免再被误读成基站注册。
2. **LINKB 初始化后从不读取**：`4G.c:51-54` 配了 PC4 下拉输入，全工程无读取点，纯冗余。
3. **文档表述需订正**：`docs/Gprs_Process_explained.md` 和 `docs/Protocols_explained.md` 里把 LINKA 写成「模块跟基站注册上了 / 蜂窝注册」，应改为「Socket A（上行 MQTT TCP 通道）连接已建立」。
4. **换料电平风险**：原厂 7S1-GN 版指示灯 1.8V、7S1-E 版 3.0V，本项目按 3.0V 用；换型号必须重新核电平并做匹配电路。
5. **23 条 AT 序列里 `AT+S` 存盘后主动重启**：`seq_advance_or_done()` 全部成功后只打印 `system reset` 并保存参数，**并未真正调用复位**（`uart3_at_sequence.c:307-319`），模块要等下次统一复位或手动上电才加载新配置——「重启」字样与行为不符，容易误导排障。

---

## 附录：出处对照

| 结论 | 出处 |
|---|---|
| `AT+WKMOD` 工作模式、`AT+HEARTEN` 心跳使能、`AT+SOCKASL` Socket A 连接方式、`AT+SOCKALK` 连接状态 | 原厂《WH-LTE-7S1 说明书》V1.0.0 §3.1 通用指令表 / 心跳包指令表 / 网络参数指令表；V1.0.8 §3.2 表 11 |
| 5 个指示引脚定义、WORK/NET/LINKA/LINKB/DATA 行为 | 原厂《WH-LTE-7S1 说明书》V1.0.5 §3.2.11 表 17、§3.2.12 表 18 |
| 引脚号 pin1/2/9/15、电平 1.8V/3.0V、换料需电平匹配 | 原厂《WH-LTE-7S1-GN 硬件设计手册》V1.0.1 §3.5 表 8 |
| BOM：U30 型号、引出脚清单（无 pin 9/15） | `qibiji/hard/hardware_report_assets/元件-网络明细.csv:266` |
| RESET 低有效经 Q3 反相、LINKA 逻辑高约 3.0V | `qibiji/hard/hardware_report_assets/QIBIJI_RTU_V1.1_硬件分析报告.html` §5.3 |
| PC5/LINKA、PC4/LINKB、PA1/RESET 初始化与 EXTI5 双边沿中断 | `qibiji/HARDWARE/4G/4G.c:45-89`、`4G.h:7` |
| 沿中断置 `gprsonlineflag` | `qibiji/HARDWARE/4G/4G.c:113-136` |
| 去抖 2000ms/500ms、确认掉线返回 1、状态机与分级恢复 | `qibiji/HARDWARE/GPRS/gprs_AT.c:20-21, 31-47, 59-101, 113-145` |
| banner 放行条件 `link_state && (atflag_online \|\| link_down_occurred)` | `qibiji/HARDWARE/GPRS/gprs_AT.c:177`（详见 `docs/Gprs_Process_explained.md` §2.1） |
| `usr.cn#` 私有前缀、23 条配置序列、topic 槽位表、串口缓存 `AT+CACHEN=OFF` | `qibiji/HARDWARE/USART/uart3_at_sequence.c:31-41, 57-92, 127-232, 345-417`（BC(0)..BC(22)） |
| 运行时 3 条探活 AT（ICCID/MQTTCID/CSQ）与期望响应 | `qibiji/HARDWARE/GPRS/gprs_AT.c:209-240, 354-511` |
| AT 队列超时/重试参数 | `qibiji/HARDWARE/GPRS/uart_queue.h:22-23`（`UART_TX_AT_TIMEOUT_MS=1000`、`UART_TX_AT_MAX_RETRY=2`） |
| topic 槽位宏 `Topic_*` 与业务发帧 | `qibiji/HARDWARE/GPRS/gprs.h:28-33`、`event_pub.c`、`property_report.c`、`gprs_JSON.c`、`net_liveness.c` |
| 三条链路对照（MQTT/Modbus/SL651） | `docs/Protocols_explained.md` |
