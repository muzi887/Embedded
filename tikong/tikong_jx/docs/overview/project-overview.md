# 梯控固件项目介绍

更新时间：2026-07-17

工程路径：`tikong/tikong_jx/`（江协风格重建；旧工程对照 `tikong/tikong`）  
文档索引：[docs/README.md](../README.md)  
目录约定：仓库根 `.cursorrules`  
接手日程：[onboarding-f103.md](./onboarding-f103.md)

---

## 一、项目定位

本工程是 **电梯门禁 / 梯控访问控制系统** 的主控固件：扫码或刷卡后，在本地完成验签、黑名单与有效期校验，再按设备类型执行院门禁 / 楼门禁 / 梯控权限（含 **RS485** 下发楼层等）；同时经 **4G 模块** 与物联网平台交互（MQTT 物模型：属性、事件、服务）。

| 项 | 说明 |
| --- | --- |
| 项目类型 | 嵌入式梯控 / 门禁访问控制 |
| MCU | STM32F407VET6（Cortex-M4，512KB Flash，192KB RAM） |
| IDE / 编译器 | Keil MDK-ARM / ARM Compiler 5 |
| 调试下载 | J-Link |
| 工程文件 | `User/tikong.uvprojx` |
| 运行模型 | 裸机轮询 + 中断/DMA（无 FreeRTOS） |

---

## 二、系统架构

```text
┌─────────────────────────────────────────────────────────────┐
│  云平台（MQTT）                                              │
│  属性设置 / 事件上报 / 服务调用（黑名单、透传、公钥等）        │
└───────────────────────────┬─────────────────────────────────┘
                            │ USART3 @ 115200（4G 模块）
┌───────────────────────────▼─────────────────────────────────┐
│  STM32F407 主控                                              │
│  · 读头 JSON → CommContrl（设置 / 权限 / 校时 / 限层等）      │
│  · SHA1 验签 / 黑名单 / RTC 有效期 / 限层                    │
│  · JSON 上报与服务回执；可选 4G AT 配网序列                   │
└─┬───────────┬───────────┬───────────┬───────────────────────┘
  │ UART4     │ UART5     │ USART2    │ USART1
  │ @ 9600    │ @ 9600    │ @ 9600    │ @ 115200
  ▼           ▼           ▼           ▼
读头 1      读头 2     RS485 梯控    PC 调试
```

分层与目录对应关系（权威：仓库根 `.cursorrules`）：

| 层级 | 目录 | 职责 |
| --- | --- | --- |
| 入口 / 板级编排 | `User/` | `main`、`board_init`、`app_run`、中断、Keil 工程 |
| 通行业务 | `App/Pass/` | `qr_comm`、`cmd`、`data_handler` |
| 云端业务 | `App/Cloud/` | `g4g_comm`、`Live_data*`、`uart3_at_sequence` |
| 存储业务 | `App/Store/` | `blackList` |
| 通道轮询 | `App/Link/` | `pc_comm`、`rs485_comm`、`app_comm.h` |
| 串口驱动 | `Hardware/Serial/` | `usart1/2/3`、`uart4/5`（仅 DMA/ISR/缓冲，**不含** `*Process`） |
| 片外存储 | `Hardware/Storage/` | `iic`、`eeprom`、`24cxx` |
| 时间 | `Hardware/Time/` | `RTC` 抽象、`ds1302` |
| 板级资源 | `Hardware/Board/` | 定时器、HC595、扩展 IO、`floor_ctrl`、led、rly、wdg… |
| 4G 模组 | `Hardware/Modem/` | `4G.c` / `4G.h` |
| 基础 | `System/` | `delay`、`sys` |
| 第三方 | `Middlewares/` | cJSON、SHA1 |
| SPL / 启动 | `Library/`、`Start/` | 标准外设库、CMSIS、启动文件 |

**约定**：`Hardware/Serial` **不得**依赖 `App`；业务只通过各 UART 头文件中的缓冲与 `RX_Complete` 标志取数。

---

## 三、硬件接口一览

| 接口 | 用途 | 波特率 | 引脚（要点） |
| --- | --- | --- | --- |
| USART1 | PC 调试 | 115200 | PA9 TX / PA10 RX；DMA + 空闲中断 |
| USART2 | RS485 ↔ 梯控板 | 9600 | PA2 TX / PA3 RX，PA1 收发控制 |
| USART3 | 4G ↔ 云平台 | 115200 | PB10 TX / PB11 RX；接收 JSON / 上报；AT 配网 |
| UART4 | 读头 1（扫码/刷卡） | 9600 | PC10 TX / PC11 RX |
| UART5 | 读头 2（扫码/刷卡） | 9600 | PC12 TX / PD2 RX |
| 软件 I2C / 三线 | EEPROM、RTC | — | EEPROM 持久化密钥/黑名单等；RTC 经 `RtcChip_*`（实现为 **DS1302**） |
| TIM1～5 | 周期任务 | — | 校时标志、限层检查、蜂鸣等（见 `Hardware/Board/timer.c`） |
| 蜂鸣器 / 继电器 / HC595 | 本地提示与输出 | — | `User/board_init.c` 中初始化 |

---

## 四、核心能力

### 4.1 读头命令（本地主路径）

读头数据经 **UART4 或 UART5** 累积为 `{...}` JSON 帧，解析后调用 `CommContrl`（`App/Pass/cmd.c`）。CSV 载荷首字符为命令号（以源码为准）：

| 命令字 | 功能 | 说明 |
| --- | --- | --- |
| `1` | 设备参数设置 | `Cmd_Setting`；常触发 4G AT 序列，成功后再写 EEPROM 并复位 |
| `2` | 权限访问 | `Cmd_Permission`：验签 → 黑名单 → 时间窗 → 按 `g_device_type` 走 Gate / Unit / Elevator |
| `6` | 设置时间及漂移 | `Cmd_Set_Time` |
| `7` | 常开 / 限层设置 | `Cmd_Set_OpenLimit` → `floor_ctrl` / EEPROM |

权限成功时：扫码 / 刷卡走不同回复路径；梯控路径可经 RS485 下发楼层，并经 USART3 上报平台。

黑名单增删等亦可由云端 JSON 服务触发（`App/Store/blackList.*`）。

### 4.2 云端联动（4G / MQTT）

USART3 收包后：`G4GProcess`（`App/Cloud/g4g_comm.c`）→ `parseSerialData`（`App/Cloud/Live_data_r.c`），与读头路径共用业务能力，结果 JSON 回传平台。

- AT 配网序列：`App/Cloud/uart3_at_sequence.*`
- 4G 模组驱动：`Hardware/Modem/4G.*`

主要包括：属性设置 / 上报应答、事件上报、服务（RS485 透传、黑名单、公钥、校时等）。

### 4.3 安全与存储

- **SHA1**：权限等业务数据验签（`Middlewares/sha1`）
- **黑名单**：内存表 + EEPROM（`App/Store/blackList`、`Hardware/Storage/eeprom`）
- **时间窗**：`RtcChip_GetTime` 对照载荷有效期（时间不准则通行失败）
- **限层**：`Hardware/Board/floor_ctrl` + 主循环定时检查限层截止时间
- **配置写回**：部分设置命令等 AT 全部成功后再落 EEPROM（`Cmd_Setting_OnAtSequenceDone`）

---

## 五、主程序流程

初始化：

1. `NVIC_PriorityGroupConfig`
2. `board_init()`：延时、USART1～5、定时器、DS1302、继电器、4G、EEPROM/`ReadKey`、HC595 等
3. `app_init()`：`loadDataFromEEPROM`、RTC、限层状态

主循环（`User/main.c`）：

```text
while (1) {
    IWDG_Feed();
    if (g_uart3_at_sequence_request)
        uart3_at_sequence_poll();   // 4G AT 配网；成功则写 EEPROM 并复位
    else {
        app_poll();                 // 正常业务
        // TIM 标志：GetNetTime / 限层时间检查 …
    }
}
```

`app_poll()`（`User/app_run.c`）当前为：

```text
PCProcess();        // USART1（App/Link/pc_comm.c）
QRProcessUart5();   // 读头 UART5（App/Pass/qr_comm.c）
QRProcessUart4();   // 读头 UART4
G4GProcess();       // USART3 云端（App/Cloud/g4g_comm.c）
```

说明：`App/Link/rs485_comm.c` 提供 RS485 通道处理；是否在 `app_poll` 中轮询以当前源码为准（发梯控指令多在权限路径内调用驱动）。

### 数据流（摘要）

```text
读头通行：
  UART4/5 → QRProcessUart* → CommContrl(cmd '2')
    → 验签/黑名单/时间窗 → Gate|Unit|Elevator → 本地动作 / RS485 → 上报(USART3)

云端指令：
  4G → USART3 → G4GProcess → parseSerialData → 业务 → JSON 回执

设备配置：
  读头 cmd '1' 或平台 → Cmd_Setting → AT 序列 → EEPROM → 复位

黑名单 / 限层：
  云端服务或本地命令 → EEPROM + 内存 / floor_ctrl
```

流程图：[main-flowchart.mmd](./main-flowchart.mmd)。

---

## 六、工程目录

```text
tikong_jx/
├── User/              # main、board_init、app_run、中断、tikong.uvprojx
├── App/
│   ├── Pass/          # 通行：qr_comm、cmd、data_handler
│   ├── Cloud/         # 云：g4g_comm、Live_data*、uart3_at_sequence
│   ├── Store/         # blackList
│   └── Link/          # pc_comm、rs485_comm
├── Hardware/
│   ├── Serial/        # usart1/2/3、uart4/5（驱动）
│   ├── Storage/       # iic、eeprom、24cxx
│   ├── Time/          # RTC、ds1302
│   ├── Board/         # timer、HC595、floor_ctrl、wdg…
│   └── Modem/         # 4G
├── System/            # delay、sys
├── Middlewares/       # cJSON、sha1
├── Library/           # STM32 SPL
├── Start/             # 启动 + CMSIS + system_stm32f4xx
└── docs/              # 文档（主题域见 README.md）
```

编译：Keil 打开 `User/tikong.uvprojx`，目标芯片 STM32F407VETx；Include Path 需覆盖 `App/*`、`Hardware/*`、`Middlewares`、`System`、`Library/inc`、`Start` 等（以工程实际配置为准）。生成 HEX/BIN 后经 J-Link 下载。

---

## 七、详细文档

本文件只做工程级总览。实现细节见：

| 文档 | 内容 |
| --- | --- |
| [docs/README.md](../README.md) | 文档索引 |
| [onboarding-f103.md](./onboarding-f103.md) | 接手日程（阶段 A～E） |
| [uart4-dma-rx.md](../serial/uart4-dma-rx.md) | UART4 DMA + IDLE 定帧 |
| [eeprom.md](../store/eeprom.md) | EEPROM 布局与 API |
| [setup/rebuild-jiangxie.md](../setup/rebuild-jiangxie.md) | 江协风格重建 / 拷贝对照 |
| 仓库根 `.cursorrules` | 分层、依赖方向、开发约定 |
