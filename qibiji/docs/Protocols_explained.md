# 项目协议现状：Modbus RTU / MQTT JSON / SL651 核查

> 适用范围：`qibiji/HARDWARE/485/485.c`、`HARDWARE/CRC16/crc16.c`、`HARDWARE/MODBUS/*`（未编译）、`HARDWARE/USART/usart4.c`、`HARDWARE/GPRS/*`、`USER/SRTUV10.uvprojx`、`USER/QIBIJI.map`，以及 `docs/嵌入式节点通讯协议应用规范(基于SL651 ASCII模式).md`。
> 本文只做静态解读，未修改任何 `.c` / `.h` 源码或既有文档。
> 配套阅读：`docs/Gprs_Process_explained.md`（MQTT/JSON 上行七步流水线）。

---

## 0. 一句话总览

**这个工程对外只有一条真的在跑的协议链路：4G Cat.1 模块内置的 MQTT + 私有 JSON 上行。RS485 上有一条形似 Modbus RTU 主站的轮询，但「只问不用」；`HARDWARE/MODBUS/` 那套从站代码根本没编进工程；SL 651-2014 只在一份归档的方案稿里出现，源码零实现。**

打比方：正门是 MQTT（货车正常送货）；侧门 RS485 每秒往电源板喊一句「报一下电表读数」，对方也回了一长串数字，收件人只核对了信封签名（CRC）就把信扔了；院子里还堆着一套没接电的旧分拣设备（`MODBUS/`）；而 `docs/` 里那份 SL651 规范，是隔壁产品线的图纸，被顺手放进了这个项目的资料柜。

---

## 1. 上行主链路：MQTT + 私有 JSON（唯一在用）

4G 模块 WH-LTE-7S1 **内置 MQTT 协议栈**，MCU 不自己实现 TCP/MQTT，只通过 AT 把 topic 配置成编号槽位：

- `AT+MQTTPUBTP=1..8` 定义 `iot/v1/{pk}/{dk}/up/...` 八个 topic 槽位（`uart3_at_sequence.c`），槽位宏在 `gprs.h`（`Topic_Pub=1` / `Topic_propertyPub=3` / `Topic_evenPub=4` / `Topic_ServiceReply=6` / `Topic_RequestInvoke=8`）。
- MCU 发送时只发 `"<槽位号>,<JSON>"`，例如心跳是紧凑帧 `2,{}`（`net_liveness.c`）。
- 载荷是属性、事件、服务回执、`requestCode`（`sys.timeSync` 等），全是私有 JSON。

这套通道模型跟 SL651 那种 ASCII 裸字节流**完全不在一个层面**——SL651 报文没法直接塞进 MQTT 的 JSON 载荷。详见 `docs/Gprs_Process_explained.md`。

---

## 2. RS485：一条「只问不用」的半成品 Modbus RTU 主站

### 2.1 位置与节拍

`manage485()` 挂在主循环**最后一步**（`main.c:83`），触发靠 TIM2 每 5ms 中断里 `cnt % 200 == 0` 置的 `flag1s`（`timer.c:245-246`），即 **1 秒一次**。

物理层是 UART4（`uart4_init(9600)`，`main.c:35`），收发器 U7 = MAX3485ESA，PD6（`U4CE`）控制方向：发之前拉高、等最后一字节 TC 后拉低（`usart4.c:87-98`）。接收走 DMA2_Channel3 + IDLE 中断，缓冲 `DMA_Rece_Buf4[256]`——是 USART2 那套「DMA → 中断 → 主循环」的**简化版**，没有乒乓暂存槽、没有临界区、没有帧队列。

### 2.2 发出去的到底是什么（`485.c:16`）

```c
Rs485_Tx_data(0x01, 0x04, 0x30, 0x33, 0x20);
```

按 `Rs485_Tx_data()` 拼成 8 字节标准 Modbus RTU 请求：

| 字节 | 值 | 含义 |
|---|---|---|
| 从站地址 | `01` | 电源板 |
| 功能码 | `04` | **Read Input Registers**（读输入寄存器） |
| 起始地址 | `30 33` | `0x3033` |
| 数量 | `00 20` | 读 **32** 个寄存器 |
| CRC | `0E DD` | CRC16 |

用 `crc16.c` 里那两张表实算过：请求帧是 **`01 04 30 33 00 20 0E DD`**。这里有个容易讲错的点：项目 `crc16()` 返回 `0x0EDD`，代码按 `crc>>8` 先发 `0E`；而标准 Modbus CRC（多项式 `0xA001`）参考值是 `0xDD0E`，规范「低字节先发」在线上也是 `0E DD`——**两者完全一致，项目符合标准 RTU，并没有反**。`crc16.c:4` 写「低位在前」、`modbus.c:13` 写「高字节在前」，看似矛盾，其实描述的是同一条线上顺序的两种角度。

### 2.3 为什么起始地址 `0x3033`、为什么读 32 个

被注释的解析代码右侧标了 `33/36/45/46/47/4E/4F`，按「第 N 个寄存器地址 = `0x3033 + (N-1)`」正好全部对上：

| 注释里的量 | 地址 | 数组下标 | `reg.h` 字段 |
|---|---|---|---|
| 13 电池状态 | `0x3033` | buf[3,4] | `bat_state` |
| 16 环境温度 | `0x3036` | buf[9,10] | `temperature` |
| 12 电池容量 | `0x3045` | buf[39,40] | `bat_cap` |
| 10 电池电压 | `0x3046` | buf[41,42] | `bat_vol` |
| 11 电池电流 | `0x3047` | buf[43,44] | `bat_curr` |
| 15 太阳能电压 | `0x304E` | buf[57,58] | `solar_vol` |
| 14 太阳能电流 | `0x304F` | buf[59,60] | `solar_curr` |

字段编号 10~16 与 `reg.h:31-37` 一一对应。应答帧是 `01 04 40` + 64 字节数据 + CRC = **69 字节**，数据从 `rs485_rx_buf[3]` 起，所以读 32 个（覆盖到 `0x3052`）刚好包住这 7 个量还有余量。业务意图很清楚：**轮询外部太阳能/电池电源板遥测，填进 `armReg` 给 LCD 和属性上报用**。

### 2.4 断在哪：只校验，不解析

`Rs485_Rx_Data()`（`485.c:42-98`）做了三件事：把 `Usart4_Rec_Cnt` 长度的数据从 DMA 缓冲拷进局部数组 → `crc16(buf, len-2)` 与 `CharToInt(&buf[len-2])` 比对 → **通过就直接走到函数结尾**。L72–97 那 26 行赋值全是注释。结果是这 7 个 `armReg` 字段**永远保持上电初值**。

应答唯一真正生效的去处是 `UART4_IRQHandler()` 的 `usart4.c:108`：

```c
Usart1_Send((char *)DMA_Rece_Buf4, Usart4_Rec_Cnt); // GPRS
```

USART1 是 `uart1_init(115200) // 与PC通讯` 的**调试打印口**（`main.c:32`，`printf` 也重定向到它），注释里的 "GPRS" 是过时标注。也就是说，现场唯一能看到电源板数据的地方是串口调试助手。

---

## 3. 两套代码只有一套活着：`HARDWARE/MODBUS/` 没编进工程

`HARDWARE/MODBUS/` 那套容易让人误以为是「Modbus 从站实现」，实际**没参与编译**：

- `SRTUV10.uvprojx` 的 HARDWARE 组只有 `485.c`（L519）和 `crc16.c`（L524），没有 `modbus.c`；`QIBIJI.map` 只有 `485.o` / `crc16.o`，交叉引用 `main.o → 485.o for manage485`、`485.o → crc16.o for crc16`。
- `modbus.h` 声明的 `Modbus_03/06/10_Slave()`、`datas_fill()`、`read_reg()`、`checkvalvesta()` **全工程没有任何实现**；`modbus.c` 里只有 CRC 表、`Rs485_TxData()`（按 `buf[1]` 选 uart1/4/5 透传）、`Rs485_RxData()`（塞 `sq` 环形队列）和 `resend()`（4s/10s 重发），还引用了主工程不存在的 `platform_config.h` / `gprs_lan_process.h`，注释有 GBK 乱码。
- 它重复定义了一份 `crc16()` 却不产生链接冲突，正是「没参与编译」的旁证。这是早期「GPRS 网关 / 阀门控制器」版本残留，`IncludePath` 还留着目录但文件已弃用。
- `crc16.c` 里的 `CRC16_CCITT_FALSE()` 也全库无调用。

---

## 4. SL 651-2014 核查结论：没用

**这套 qibiji 闸门控制器固件里，SL 651-2014 一行实现都没有。** `docs/嵌入式节点通讯协议应用规范(基于SL651 ASCII模式).md` 不是本项目的协议说明，而是一份被归档进 `docs/` 的**另一台设备的设计蓝图**。

### 4.1 这份文档讲的是什么

V4.0 方案设计稿，描述的是一个**「多协议转 SL651 网关」**产品：下行用 Modbus 采集各种水文传感器，上行按《SL 651-2014 水文监测数据通信规约》6.4 节的 **ASCII 模式**把降水量、水位等要素封成帧发给中心站。通篇措辞是「网关需支持 / 单片机需内置 / 上位机必须」，全是**要求**，不是「已实现」的说明。

SL651 的业务含义：水利行业遥测站跟中心站对话的**国标报文格式**——ASCII 字符帧，用 SOH/STX/ETX 定界，带遥测站地址、流水号、观测时间、要素标识符（TT/PJ/VT…）、`N(D,d)` 数值格式和 CRC。编号说明：用户曾写作 `SL 161`，是笔误，水利部标准号是 **SL 651-2014**（部分平台记 SL/T 651-2014）。

### 4.2 为什么敢说「没落地」

复核过两遍，源码侧是干净的：

- 全工程搜 `SL651 / 651-2014 / 水文监测数据通信规约`，**只命中那一份 md**；所有 `.c` / `.h` 零命中。
- 规约特征在源码里一个都没有：没有 SOH/STX/ETX/ETB 帧定界、没有要素标识符、没有遥测站地址与 BCD 观测时间、没有 ASCII 封包与动态长度/CRC 回填。方案稿里的 `meta_config` / `protocol_mode` / `segments` / `HEX_CTRL` / `GlobalDataPool` / `message.json` / `N(5,1)` / `ui_rules` 同样只存在于 md 里，工程内连 `message.json` 文件都没有，编译清单里也没有 SL651 封包模块、M1~M4 状态机或数据池。
- 工程真正编进来的协议代码只有 `485.c` + `crc16.c`，那是 §2 的 Modbus RTU 主站，不是 SL651。

### 4.3 唯一交集，别误当成「半个网关」

两边确实都提到「下行 Modbus」，但：

| | 方案稿里的网关 | 本项目 `485.c` |
|---|---|---|
| 下行对象 | 异构水文传感器 | 太阳能/电池电源板 |
| 数据去向 | 数据池 → SL651 要素编码 → 上行 | **应答解析整段被注释，只做 CRC 校验**，数据不进 `armReg` |
| 上行 | SL651 ASCII → 中心站 | MQTT JSON → 云平台 |

所以本项目连「已经做了网关的下行半边」都算不上。这份稿子更可能是同一产品线另一个网关产品的设计，或是对本平台将来扩展 SL651 上报的规划。若真要接，得先把上行从「模块内 MQTT」改成 TCP/串口透传，再实现 segments 拼包引擎、`N(D,d)` 数值格式化、动态长度与 CRC 回填、M1~M4 链路状态机、要素编码与遥测站地址配置——目前**一项都没有**。

---

## 5. 三条链路对照速查

| | MQTT/JSON（在用） | Modbus RTU（半成品） | SL651 ASCII（仅文档） |
|---|---|---|---|
| 物理/通道 | 4G Cat.1 模块内 MQTT | UART4 + MAX3485，9600 | 方案稿设想，未指定落地 |
| 角色 | MCU → 云平台（客户端） | MCU 主站 → 电源板从站 | 遥测站 → 中心站 |
| 报文 | `<槽位>,<JSON>` | `01 04 ... + CRC` | SOH/STX…ETX + 要素编码 |
| 数据去向 | 属性/事件/心跳/对时 | **只校验 CRC，解析全注释** | 无 |
| 编进工程 | 是 | `485.c` + `crc16.c` 是，`MODBUS/` 否 | 否 |

---

## 6. 代码审查发现（仅记录，未修改）

RS485 这条链路的静态问题，按影响排序：

1. **接收缓冲无边界**：`u8 rs485_rx_buf[100]` 对上 `DMA_Rec4_Len = 256`，`485.c:59-62` 直接按 `len` 拷贝，超长帧栈溢出；`len < 2` 时 `len-2` 变 65534，`crc16()` 会读越界。
2. **不认帧**：不校验从站地址、功能码，也不处理异常响应（`0x84`），只要 CRC 碰巧对上就当有效帧。
3. **标志位竞态**：`manage485()` 在发请求前 `Usart4_Rec_Cnt = 0`（`485.c:15`），会抹掉此刻刚到的应答；这个变量还被「中断转发（不清）」和「主循环解析（清）」两个消费者共用，且没加 `volatile`（`flag1s` 同样缺）。
4. **中断里干重活**：`UART4_IRQHandler()` 用逐字节轮询 TC 的 `Usart1_Send()` 转发 69 字节，115200 下约 6ms；且转发写在 `MYDMA_Enable4()`（`usart4.c:110`）**之前**，这段时间 DMA 没重新武装，来的字节会丢。
5. **无超时重试**：发完不等应答，1s 到点无条件再发；漏帧、从站掉线都无感知（旧版 `resend()` 没编译进来）。
6. **一函数两语义**：`Rs485_Tx_data` 第 5 参数叫 `val`，在 `0x04` 下是「寄存器数量」，换成 `0x06/0x10` 就变成「写入值」；`err`、`temp`、`val_h`、`val_l` 全是未使用变量。

---

## 7. 一句话串起来

真正跑业务的是 4G 模块内 MQTT + 私有 JSON；RS485 每秒发一帧 `01 04 30 33 00 20 0E DD` 问电源板遥测，回来只核对 CRC 就把 7 个字段丢掉、只往调试串口转发一份；`HARDWARE/MODBUS/` 那套从站代码和 `docs/` 里的 SL651 规范稿，一个没编译、一个没实现，都不是这个工程正在用的协议。

---

## 附录：出处对照

| 结论 | 源码位置 |
|---|---|
| `manage485()` / 请求帧 / 应答空解析 | `qibiji/HARDWARE/485/485.c`（帧 L16、`Rs485_Rx_Data` L42-98、注释赋值 L72-97）；`485.h` |
| CRC 表 / `crc16()` / `CharToInt()` | `qibiji/HARDWARE/CRC16/crc16.c`；`CRC16_CCITT_FALSE()` 全库无调用 |
| 未编译的旧从站代码 | `qibiji/HARDWARE/MODBUS/modbus.c` / `modbus.h` |
| UART4 底层 / 方向控制 / 中断转发 | `qibiji/HARDWARE/USART/usart4.c:87-98, 108, 110`；`usart.h:53-54`；`usart1.c` |
| 7 个遥测字段 | `qibiji/HARDWARE/REG/reg.h:31-37` |
| 主循环与串口初始化 | `qibiji/USER/main.c:32-36, 83` |
| 1s 节拍 | `qibiji/HARDWARE/TIMER/timer.c:245-246` |
| 编译/链接清单 | `qibiji/USER/SRTUV10.uvprojx:519-526`；`USER/QIBIJI.map:33, 226, 1696-1701` |
| 硬件收发器 | `hard/hardware_report_assets/元件-网络明细.csv:258`（U7 MAX3485ESA、U4CE） |
| MQTT topic 槽位 / JSON 上行 | `qibiji/HARDWARE/GPRS/gprs.h`、`uart3_at_sequence.c`；详见 `docs/Gprs_Process_explained.md` |
| SL651 方案稿（未落地） | `docs/嵌入式节点通讯协议应用规范(基于SL651 ASCII模式).md` |
