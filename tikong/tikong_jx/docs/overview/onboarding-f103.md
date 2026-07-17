# 梯控固件入门规划（基于 F103 基础）

更新时间：2026-07-17

> **面向谁**：已按 Embedded 仓库学过 **F103 入门（时钟 / GPIO / 串口 / 中断）**，并读过 **USART、I2C** 专文，准备接手本工程的开发者。  
> **工程路径**：`tikong/tikong_jx/`（当前工作区；Keil：`User/tikong.uvprojx`）  
> **总览**：[project-overview.md](./project-overview.md)  
> **目录约定**：仓库根 `.cursorrules`  
> **索引**：[docs/README.md](../README.md)

---

## 一、一句话目标

在约 **1～2 周业余（或 5～7 个工作日）** 内：能编译烧录 F407 工程 → 对照已有串口/I2C 知识看懂多路 USART + DMA → 跟通 **扫码/刷卡通行（`CommContrl` / 权限命令）** 与 **4G/云端一条服务** → 知道黑名单 / 验签 / RTC / 限层落在哪。

```text
你现在的底座（F103 学习）
  时钟 · GPIO · USART 帧/状态机 · I2C 开漏与读写时序 · 中断/NVIC · Keil+SPL
        ↓ 迁移
梯控工程（F407 / tikong_jx）
  多路 USART+DMA · RS485 · 软件 I2C(RTC/EEPROM) · App 业务层 · JSON/MQTT 经 4G
```

**当前分层（接手时先记住）**：

| 目录 | 职责 |
| --- | --- |
| `Hardware/Serial/` | 串口驱动：初始化、DMA、ISR、缓冲；**不含**业务 `*Process` |
| `App/Pass/` | 通行：`QRProcessUart4/5`、`CommContrl` / `cmd`、`data_handler` |
| `App/Cloud/` | 云端：`G4GProcess`、`Live_data_r`、`uart3_at_sequence` |
| `App/Store/` | 黑名单业务 |
| `App/Link/` | `PCProcess`、RS485 通道相关 |
| `Hardware/Storage|Time|Board|Modem/` | EEPROM/I2C、RTC/DS1302、板级 IO/定时/限层、4G 驱动 |
| `Middlewares/` | cJSON、SHA1（尽量少改） |
| `User/` | `main`、`board_init`、`app_run`、Keil 工程 |

---

## 二、你已有基础 ↔ 本工程缺口

### 2.1 已掌握（可直接迁移）

对照 Embedded 文档（路径相对仓库根 `Embedded/docs/`）：

| 你已学的 | 文档 | 在梯控里怎么用 |
| --- | --- | --- |
| 通信 / 协议 / 接口；TTL；TX↔RX | [STM32-串口USART说明.md](../../../docs/STM32-串口USART说明.md) | USART1 调试、UART4/5 读头、USART3 4G 都是「串口字节流」 |
| USB 口 / COM / USART 分层 | 同上 §2.3 | PC 上看 USART1 的 COMx；业务口不经过 USB-TTL |
| 异步帧、波特率、收包思路 | 同上 | 本工程多用 **DMA + 空闲中断** 收一整帧，再进 `App/*` 业务函数 |
| RS-485 电平与长线 | 同上 §2.2 | **USART2 + PA1 方向脚** ↔ 梯控板 |
| SCL/SDA、开漏、指定地址读写、Sr | [STM32-I2C说明.md](../../../docs/STM32-I2C说明.md)、本工程 [eeprom.md](../store/eeprom.md) | `Hardware/Storage/iic.c` + **EEPROM**；RTC 经 `Hardware/Time/RTC.c`（**DS1302**） |
| GPIO / NVIC / 定时器概念 | GPIO / 中断 / Timer 专文 | 蜂鸣器、TIM1～5 周期任务、限层检查标志 |
| Keil + SPL | [STM32入门环境与工程配置备忘.md](../../../docs/STM32入门环境与工程配置备忘.md) | 本工程仍是 **SPL**（F4 库，在 `Library/`） |

### 2.2 缺口（入门规划要补的）

| 缺口 | 说明 | 本规划何时补 |
| --- | --- | --- |
| **F103 → F407** | Cortex-M4、168 MHz、引脚与 RCC 不同；SPL API 名字相近 | 阶段 A |
| **多路 USART + DMA + 空闲中断** | 入门常轮询 RXNE；这里驱动与业务拆开：缓冲在 `Hardware/Serial`，`*Process` 在 `App` | 阶段 A～B |
| **业务协议** | 读头 JSON 帧 → `CommContrl`；按设备类型走门禁/单元/梯控权限 | 阶段 C |
| **云端 JSON / MQTT** | 经 4G 模组透传；`G4GProcess` → `parseSerialData` | 阶段 D |
| **SHA1 验签、黑名单、口令、限层** | 安全与 EEPROM 持久化；另有楼层限时逻辑 | 阶段 E |

### 2.3 芯片与工具差异（先记住）

| 项 | 入门套件（你熟悉的） | 本工程 |
| --- | --- | --- |
| MCU | STM32F103C8T6 | **STM32F407VET6** |
| 主频 | 常 72 MHz | 常 **168 MHz**（`delay_init(168)`） |
| 调试器 | ST-Link / SWD | **J-Link** |
| 工程 | `project/test.uvprojx` 等 | `User/tikong.uvprojx` |
| 串口数量 | 调试 1 路为主 | **USART1～3 + UART4/5** 同时工作 |
| I2C / 时间 | OLED 软件 I2C 常见 | EEPROM（`AT24C32_I2C_Init`）+ RTC（`RtcChip_*` / DS1302） |

---

## 三、阶段总览与验收

| 阶段 | 建议时长 | 目标 | 验收 |
| --- | --- | --- | --- |
| **A** 迁移热身 | 1～2 天 | F4 工程能编、能下、能打日志 | Keil 全编译通过；USART1 有 `printf` |
| **B** 骨架 | 1 天 | 说清五路串口分工与主循环 | 口述：谁收读头 / 谁发 485 / 谁连云 / 谁打日志；能指到 `app_poll` |
| **C** 通行主路径 | 1～2 天 | 权限命令跟到执行点 | 能画：`QRProcessUart4/5` → `CommContrl` → `Cmd_Permission_*` |
| **D** 云端路径 | 1～2 天 | `G4GProcess` → 一条服务 | 能指到 JSON 分支与回执发送点 |
| **E** 存储与联调 | 1～2 天 | 黑名单 / RTC / 密钥 / 限层 + 最小联调 | 通行或黑名单任一条跑通（含日志） |

合计约 **5～9 天**（已有基础可压到 **5 个工作日**）。

---

## 四、详细日程

### 阶段 A — 工程与 F1→F4 迁移（Day 1～2）

1. 读 [project-overview.md](./project-overview.md) §一～三（定位、架构、串口表）；对照 `.cursorrules` 浏览 `User/`、`App/`、`Hardware/`、`System/`。
2. Keil 打开 `User/tikong.uvprojx`，目标选 **STM32F407VETx**，确认 Include Path 覆盖业务与驱动目录，全编译。
3. J-Link 下载；USART1 @ **115200** 开串口助手，确认启动日志（`sys init ok`、RTC 时间打印等）。
4. **迁移笔记**（建议自写半页）：
   - `board_init()` 里 `uart_init` / `RS485_Init` / `uart3_init` / `uart4_init` / `uart5_init` 与 F103 单路 `Serial_Init` 的对应关系
   - `delay_init(168)` 与蓝 pill `72` 的差别
   - 驱动在 `Hardware/Serial`，业务在 `App/*`（不要在驱动 `.c` 里找 `QRProcess`）

**复用你的知识**：USART1 仍是「MCU USART ↔ USB-TTL/板载转串 ↔ COM」那条链；见串口专文 §2.3。

---

### 阶段 B — 主循环与串口分工（Day 2～3）

1. 精读入口链：
   - `User/main.c`：`board_init` → `app_init` → `while(1)`（看门狗、可选 4G AT 序列、否则 `app_poll`）
   - `User/board_init.c`：外设与板级初始化顺序
   - `User/app_run.c`：`app_init`（EEPROM/RTC/限层）与 `app_poll`
2. **当前源码**（`app_poll` 已启用业务轮询）：

```c
// User/app_run.c — app_poll()
PCProcess();       // USART1 调试
QRProcessUart5();  // 读头 UART5
QRProcessUart4();  // 读头 UART4
G4GProcess();      // USART3 4G / 云
```

另：`main` 在 `g_uart3_at_sequence_request` 置位时走 **4G AT 配置序列**（配网/写密钥），完成后复位；正常业务在 `else` 分支的 `app_poll()`。

3. 对照串口表，打开：

| 文件 | 角色 | 你熟悉的概念 |
| --- | --- | --- |
| `Hardware/Serial/usart1.c` | USART1 驱动 | `printf`、COM |
| `App/Link/pc_comm.c` | `PCProcess` | 调试口业务轮询 |
| `Hardware/Serial/usart2.c` | RS485 驱动 | TTL→485、方向脚 |
| `Hardware/Serial/usart3.c` + `App/Cloud/g4g_comm.c` | 4G / 云 | 大缓冲 JSON → `parseSerialData` |
| `Hardware/Serial/uart4.c` + `App/Pass/qr_comm.c` | 读头 UART4 | 9600 / DMA → `QRProcessUart4` |
| `Hardware/Serial/uart5.c` + `App/Pass/qr_comm.c` | 读头 UART5 | 双读头并行 |

4. 流程图：[main-flowchart.mmd](./main-flowchart.mmd)。
5. 抽查一路：DMA 收满 / 空闲中断如何置 `RX_Complete`，再进 `*Process()`。完整路径见 [uart4-dma-rx.md](../serial/uart4-dma-rx.md)（入口为 `QRProcessUart4`，而非旧名 `QRProcess`）。

**应能回答**：哪两路收读头、哪一路发梯控、哪一路连云、哪一路打日志；`app_poll` 与 AT 序列互斥关系。

---

### 阶段 C — 扫码/刷卡通行（Day 3～4）

1. 路径：
   - `QRProcessUart4` / `QRProcessUart5`（`App/Pass/qr_comm.c`）
   - 累积 JSON 花括号帧 → 解析字段
   - 调用 `CommContrl(...)`（`App/Pass/cmd.c`）
2. 权限校验链（梯控设备类型时，理解顺序即可）：

```text
验签 → 黑名单 → RTC 有效期 → 按设备类型：
  院门禁(Gate) / 楼门禁(Unit) / 梯控(Elevator) → 本地动作（含 RS485 / 继电器等）
```

入口函数名：`Cmd_Permission` → `Cmd_Permission_ProcessElevator`（等）。

3. **动手**：
   - 用扫码枪或串口助手向 **UART4 或 UART5 @ 9600** 送完整 `{...}` JSON 样例
   - USART1 看日志（收包打印、csv 字段、结果码）
   - 有条件则抓 RS485 / 继电器动作；无硬件时至少确认走到 `Cmd_Permission_*` 与组帧/发送调用

**复用你的知识**：读头侧是「串口字节流 + 应用层 JSON 定帧」；RS485 发送前注意方向脚。

---

### 阶段 D — 4G / 平台路径（Day 4～5）

1. 源码入口：`App/Cloud/g4g_comm.c` → `App/Cloud/Live_data_r.c` 的 `parseSerialData`。
2. AT 配网：`App/Cloud/uart3_at_sequence.*`；模组驱动：`Hardware/Modem/4G.*`。
3. 任选一条服务（黑名单新增或 RS485 透传），从 JSON 入口追到本地函数与回执（USART3 发送）。
4. 联调需 4G 模组或串口模拟 JSON；注意主循环若在跑 AT 序列，则不会进入 `app_poll`。

**复用你的知识**：USART3 仍是串口；MQTT Topic 在模组/协议层，固件侧主要是 **收 JSON 字符串 → 解析 → 执行 → 回 JSON**。

---

### 阶段 E — 存储、安全与最小联调（Day 5～7）

1. **I2C / RTC 落地**：
   - EEPROM：`Hardware/Storage/eeprom.c`（上电 `loadDataFromEEPROM`、`ReadKey`）
   - 时间：`Hardware/Time/RTC.c` 的 `RtcChip_GetTime` / `SetTime`（芯片驱动 `ds1302.c`）
   - 通行有效期依赖 RTC：时间不准 → 权限失败
2. **黑名单**：`App/Store/blackList.c` + 上电 `loadDataFromEEPROM`
3. **密钥/口令**：`ReadKey`、公钥与账号密码（`App/Pass/data_handler` / EEPROM）；配置类命令可能经 AT 成功后再写 EEPROM（见 `Cmd_Setting_OnAtSequenceDone`）
4. **限层**：`Hardware/Board/floor_ctrl.c` + `main` / `app_init` 中的限层时间检查
5. **联调清单（任选完成）**：
   - UART4 或 UART5 通行成功 / 失败各一次（日志含结果码）
   - 平台下发一条服务并看到设备回执

---

## 五、推荐阅读顺序

```text
【建立全局】
1. overview/project-overview.md
2. 仓库根 .cursorrules（分层与依赖）
3. 本文 §二（基础 ↔ 缺口）

【对照已学专文（按需翻）】
4. Embedded/docs/STM32-串口USART说明.md
5. Embedded/docs/STM32-I2C说明.md

【运行时骨架】
6. User/main.c + board_init.c + app_run.c + overview/main-flowchart.mmd

【两条业务主路径】
7. App/Pass/qr_comm.c → App/Pass/cmd.c（读头通行）
8. App/Cloud/g4g_comm.c → App/Cloud/Live_data_r.c（云端）

【联调细节】
9. docs/serial/uart4-dma-rx.md、docs/store/eeprom.md 等按需
```

完整索引见 [docs/README.md](../README.md)。

---

## 六、源码入口速查

| 想搞清楚… | 先打开 |
| --- | --- |
| 上电与主循环 | `User/main.c` → `board_init.c` → `app_run.c` |
| 读头业务 | `App/Pass/qr_comm.c` → `App/Pass/cmd.c`（`CommContrl`） |
| 云端业务 | `App/Cloud/g4g_comm.c` → `App/Cloud/Live_data_r.c`（`parseSerialData`） |
| 4G AT 配网 | `App/Cloud/uart3_at_sequence.c`、`Hardware/Modem/4G.c` |
| 验签 / 数据处理 | `App/Pass/data_handler.c`、`Middlewares/sha1.c` |
| 黑名单 | `App/Store/blackList.c`、`Hardware/Storage/eeprom.c` |
| 时间 | `Hardware/Time/RTC.c`、`Hardware/Time/ds1302.c` |
| 限层 | `Hardware/Board/floor_ctrl.c` |
| RS485 驱动 | `Hardware/Serial/usart2.c` |
| 调试打印 | `Hardware/Serial/usart1.c` + `App/Link/pc_comm.c` |

---

## 七、学习节奏建议（结合你的基础）

| 节奏 | 适合 | 安排 |
| --- | --- | --- |
| **压缩 5 日** | 串口/I2C 已扎实，每天可上机 | A+B 合并 1 天；C、D、E 各 1～1.5 天 |
| **标准 7～9 日** | 业余每晚 2 小时 | 严格按阶段 A→E，每阶段写 5 行笔记 |
| **先补再进** | DMA/多串口仍生疏 | 先用 F103 工程加一路 DMA 空闲收包 Demo，再开本工程阶段 B |

不必先学完整 FreeRTOS / HAL；本工程是 **裸机轮询 + 中断/DMA**，与入门路线一致。

---

## 八、常见注意点

- **业务已在 `app_poll` 中启用**：不必再找「取消注释 `QRProcess`」；若无反应，查读头 JSON 是否成帧、UART 口与波特率、以及是否正卡在 AT 序列分支。
- **双读头**：UART4 与 UART5 均 @ 9600，逻辑对称，联调时确认线接的是哪一路。
- **RAM 约 192KB**：USART3 等缓冲较大；少在栈上开巨型数组。
- **RS485**：发送前拉高/拉低 **PA1** 方向脚，发完再切回接收。
- **验签 / 口令**：权限与黑名单相关命令依赖公钥或本地账号密码——联调前确认 EEPROM 已有凭证（`ReadKey` 等）。
- **有效期** 依赖 RTC（`RtcChip_*`）：校时优先于纠结业务逻辑。
- **看门狗**：主循环有 `IWDG_Feed`；调试断点过久可能复位。
- F407 引脚与蓝 pill **不要混着对丝印**；以本板原理图 / [project-overview.md](./project-overview.md) 串口表为准。
- 文档路径一律以 **本仓库实际目录** 与 `.cursorrules` 为准；旧工程 `tikong/tikong` 仅作对照。

---

## 九、阶段自检

1. F103 的 USART 知识如何对应到本工程多路业务串口？  
   → 仍是字节流；差别在 **多实例 + DMA 收包 + 驱动/业务拆分 + 不同波特率/角色**。
2. EEPROM / RTC 为什么要懂 I2C 开漏与指定地址读？  
   → 密钥、黑名单与时间都走片外器件。
3. 通行失败时优先查哪几件事？  
   → 黑名单、签名/公钥、RTC 时间窗、设备类型分支（Gate/Unit/Elevator）。
4. 云端路径固件侧入口函数是谁？  
   → `G4GProcess`（`App/Cloud/g4g_comm.c`）→ `parseSerialData`。
5. 当前 `main` 若读头无反应，先看什么？  
   → 是否卡在 `g_uart3_at_sequence_request`；`app_poll` 是否在跑；UART4/5 波特率是否 9600；JSON 是否以 `{`…`}` 成帧。

---

## 十、与长期路线的关系

| 材料 | 角色 |
| --- | --- |
| [STM32短期入门规划.md](../../../docs/STM32短期入门规划.md) | **已完成或等价完成** → 再进本文 |
| [STM32嵌入式学习路线与能力规划.md](../../../docs/STM32嵌入式学习路线与能力规划.md) | 平台 MQTT / Modbus 等可与阶段 D 对照加深 |
| 仓库根 `.cursorrules` | 当前目录与依赖方向的权威说明 |
| 本文 | **带 F103 基础迁移说明** 的产品固件接手路径 |

学完本文验收项后，下一步建议：独立改一条业务（例如日志字段、黑名单上限检查）并得到可编译的小改动，而不是继续只读文档。
