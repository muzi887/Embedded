# 4G 目录 PDF / 资料阅读顺序（阶段 D）

更新时间：2026-07-18

> **对齐**：[onboarding-f103.md](../../overview/onboarding-f103.md) 阶段 D（4G / 平台路径）  
> **源码**：`Hardware/Modem/4G.*`、`Hardware/Serial/usart3.*`、`App/Cloud/uart3_at_sequence.*`、`App/Cloud/g4g_comm.c`、`App/Cloud/Live_data_r.c`  
> **物模型（TSL）**：[thing-model-v2.md](../thing-model-v2.md)（表格）+ [thing-model-v2.json](../thing-model-v2.json)（完整 JSON）

目标：在约 **0.5～1 天** 内把「USART3 → AT 配网 → MQTT 透传 → `G4GProcess` → JSON」读通。  
**不必通读本目录全部 PDF。**

核心认知：

- MQTT 连接与 Topic 在 **4G 模组侧**；MCU 侧主要是 **收/发 JSON 字符串**。
- 平台功能定义是 **TSL 物模型**（`properties` / `events` / `services`）；固件侧入口是 `parseSerialData` 的 `数字,JSON`。二者用 **key** 对照，不要把 case 号当成物模型字段名。

说明书里「双排插针采用防呆设计」见 [foolproof-header.md](./foolproof-header.md)（装配防反插，与 AT 无关）。

---

## 一、本目录资料角色


| 文件                                           | 阶段 D       | 用途                                         |
| -------------------------------------------- | ---------- | ------------------------------------------ |
| `WH-LTE-7S1-CT-N40_说明书V1.0.0.pdf`            | **必读**（扫读） | 模组是什么、串口/指示、工作模式（含 MQTT）                   |
| `WH-LTE-7S1-CT-N40 AT指令集_V1.0.0.pdf`         | **必读**（检索） | 只查本工程用到的 `AT+WKMOD` / `MQTT*` / `AT+S` 等   |
| `AT.txt`                                     | **必读**     | 与固件一致的 MQTT 配网指令清单（精简）                     |
| `MQTT_AT_SET.txt`                            | **必读**     | 串口实操收发日志，对照「发什么、回什么」                       |
| [`../thing-model-v2.md`](../thing-model-v2.md) / [`.json`](../thing-model-v2.json) | **必读**（检索） | 网关 V2 **TSL 物模型**：事件/服务 key 与字段；追服务时先锁 key |
| `WH-LTE-7S1-CT-N40 硬件设计手册-V1.0.0.pdf`        | 选读         | 对照 RST / LINKA 与 `Hardware/Modem/4G.c` 时再翻 |
| `梯控门禁网关V2_20260606.docx`                     | 参考         | 产品侧 Topic / 协议原文；TSL 已拆出时优先用上面的 md/json    |
| `ASR1606_Series_MQTT 操作指南V1.0.1.pdf`         | **可跳过**    | 非本板主路径                                     |
| `G771&G780s标准AT指令集V1.0.6-20240626163404.pdf` | **可跳过**    | 非本板主路径                                     |


本工程 AT 风格对齐 **WH-LTE-7S1-CT-N40**（见 `uart3_at_sequence.c` 中 `AT+WKMOD=MQTT` 等）。ASR1606 / G771 手册阶段 D 不必读。

---

## 二、推荐阅读顺序

按序做；每步以「能对照源码」为准，不要先啃完整手册。

### 1. 说明书扫读（约 30～45 分钟）

打开 `WH-LTE-7S1-CT-N40_说明书V1.0.0.pdf`，只抓：

- 串口参数、上电/复位、联网指示（与板级 LINKA 等概念相关即可）
- 工作模式里与 **MQTT**、透传相关的说明

板级对照：

- `board_init()`：`uart3_init(115200)` 注释为 4G；`GM4G_Init()` 初始化 RST / LINKA
- `Hardware/Modem/4G.c`：PB13 复位、PB14 LINKA 中断 → LED

### 2. AT 手册按清单检索（约 45～60 分钟）

打开 `WH-LTE-7S1-CT-N40 AT指令集_V1.0.0.pdf`，**不要通读**。  
以 `AT.txt` 与下表为目录，在 PDF 里搜对应章节即可。


| 顺序  | 指令（概念）                                      | 在工程中的位置                       |
| --- | ------------------------------------------- | ----------------------------- |
| 1   | `AT+WKMOD=MQTT,NOR`                         | `uart3_at_sequence.c` 组包第 0 步 |
| 2   | `AT+MQTTSVR`                                | 同上；地址来自 `g_mqtt_addr`         |
| 3   | `AT+MQTTUSER` / `AT+MQTTPSW` / `AT+MQTTCID` | deviceKey / secret / `pk|dk`  |
| 4   | `AT+MQTTMOD` / `AT+HEARTEN`                 | 模式与心跳                         |
| 5   | `AT+MQTTSUBTP` / `AT+MQTTPUBTP`             | 订阅 / 发布 Topic 模板              |
| 6   | `AT+S`                                      | 保存并生效                         |


旁路读 `MQTT_AT_SET.txt`：看每条 `OK` / 错误形态，便于理解 `uart3_at_sequence_poll` 的等待逻辑。

### 3. 源码对照（约 1～2 小时）

五个文件不要从上到下硬读——容易对不上。改用专文，按 **main 分叉 → 路①配网 / 路② JSON** 走：

→ **[source-map.md](./source-map.md)**（推荐：总图 + 分步打勾）  
→ **[at-config-flow.md](./at-config-flow.md)**（AT 触发、19 条命令、成功后写 EEPROM / 复位）

要点（读专文前可先记）：

- 共用 USART3 缓冲；`4G.c` 只管 RST/LINKA GPIO。  
- `g_uart3_at_sequence_request != 0` → 只跑 AT，**不进** `app_poll` / `G4GProcess`。  
- request 清零后 → `app_poll` → `G4GProcess` → `parseSerialData`（追一个 case 即可）。

### 4. 物模型 TSL → 任选一条服务追到底（阶段 D 验收）

1. 打开 [thing-model-v2.md](../thing-model-v2.md)，锁定一个服务 **key**（如 `blacklist_add`、`call_elevator`、`open_door`）。  
2. 需要字段细节时查 [thing-model-v2.json](../thing-model-v2.json)（完整 TSL）。  
3. 在 `parseSerialData` 里找到对应分支，追到本地处理与 USART3 回执。  
   - 想按「一条完整样例」走：可读 [parseSerialData-case101.md](../parseSerialData-case101.md)（RS485 透传）。  
4. Topic / 协议原文仍不清时，再翻 `梯控门禁网关V2_20260606.docx`（按 key 检索，不必通读）。

硬件设计手册仅在需要核对复位脚、链路指示脚时查阅。

---

## 三、与源码对照速查


| 关注点          | 文件 / 符号                                                        |
| ------------ | -------------------------------------------------------------- |
| 4G 串口初始化     | `User/board_init.c` → `uart3_init(115200)`                     |
| 模组复位 / LINKA | `Hardware/Modem/4G.c` → `GM4G_Init` / `GM4G_Restart`           |
| AT 配网序列      | `App/Cloud/uart3_at_sequence.c` → `uart3_at_sequence_poll`     |
| 触发 AT 模式     | `g_uart3_at_sequence_request` / `uart3_at_sequence_should_run` |
| 云端收包入口       | `App/Cloud/g4g_comm.c` → `G4GProcess`                          |
| JSON 解析入口    | `App/Cloud/Live_data_r.c` → `parseSerialData`                  |
| 物模型 TSL       | `docs/cloud/thing-model-v2.md` / `thing-model-v2.json`         |
| 精简指令清单       | 本目录 `AT.txt`                                                   |
| 实操收发样例       | 本目录 `MQTT_AT_SET.txt`                                          |


---

## 四、阶段 D 完成判据

能做到下面三点即可进入阶段 E，无需背完整 AT 手册：

1. 能口述：**模组做 MQTT，MCU 经 USART3 收发 JSON**；平台功能表是 **TSL**（`thing-model-v2.json`）。
2. 能指到：`uart3_at_sequence` 组包的 MQTT AT，以及 `G4GProcess` → `parseSerialData` 入口；并能说清一个服务 **key** 与固件分支的对应。
3. 能说明：AT 序列运行时 **不进 `app_poll`**，故通行 / 云端业务此时都不跑。

---

## 五、刻意不读什么（阶段 D）

- ASR1606 MQTT 操作指南、G771/G780s AT 指令集全文  
- WH-LTE AT 手册中与本工程序列无关的 SOCK / HTTP / 其它工作模式章节  
- 硬件设计手册的射频/天线完整章节（非阶段 D 阻塞项）  
- 网关 docx 全文（已有 `thing-model-v2` TSL 时，按 key 检索即可）

