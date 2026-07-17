# 项目结构说明

## 目录结构

```
tikong-260202/
├── CORE/                 # ARM Cortex-M4 核心与启动文件
├── FWLIB/                # STM32 标准外设库 (inc / src)
├── SYSTEM/               # 基础外设封装（延时、sys、UART 驱动层）
│   ├── delay/
│   ├── sys/
│   └── usart/            # USART1/2/3、UART4/5：初始化、DMA、ISR；不含业务 *Process
├── third_party/          # 第三方源码（尽量少改）
│   ├── cJSON.c / cJSON.h
│   └── sha1.c / sha1.h
├── drivers/              # 总线/片外器件驱动
│   ├── iic.c / iic.h
│   ├── eeprom.c / eeprom.h
│   └── DS3231.c / DS3231.h
├── board/                # 本板资源（IO 扩展、定时、蜂鸣器等）
│   ├── bsp_hc595.c / bsp_hc595.h
│   ├── ext_io.c / ext_io.h
│   ├── timer.c / timer.h
│   ├── floor_ctrl.c / floor_ctrl.h
│   └── malloc.c / malloc.h
├── app/                  # 应用与领域逻辑
│   ├── data_handler.* / blackList.* / Live_data.* / Live_data_r.*
│   ├── utils/            # 命令处理（二维码指令等）
│   │   └── cmd.c / cmd.h
│   └── comm/             # 各通信通道主循环处理（原 usart*.c 内 *Process）
│       ├── app_comm.h
│       ├── pc_comm.c     # PCProcess（USART1）
│       ├── g4g_comm.c    # G4GProcess（USART3）
│       ├── qr_comm.c     # QRProcess（UART4）
│       ├── rs485_comm.c  # Rs485Process（USART2）
│       ├── uart3_at_sequence.c/h  # 条件触发 USART3 AT 序列（占位变量请求）
│       └── AT_reference.txt       # AT 参考列表（不参与编译）
└── USER/                 # 工程入口与板级初始化
    ├── main.c            # NVIC 分组 + board_init + app_init；条件 AT 序列或 app_poll
    ├── main.h            # 仅保留 sys.h（按需在各 .c 中包含具体头文件）
    ├── board_init.c/h    # 外设与板级硬件初始化
    ├── app_run.c/h       # 应用初始化、主循环轮询、全局 currentTime
    ├── app_config.h      # 应用级宏（如 MAX_RECEIVE_LEN）
    ├── stm32f4xx_it.c/h
    ├── system_stm32f4xx.c/h
    └── Template.uvprojx
```

## 串口驱动与业务解耦（最小可见 API）

业务层（`app/comm`）通过各 UART 头文件中的 `extern` 缓冲与标志访问接收数据，不直接改驱动 `.c` 内部静态实现：

| 通道   | 头文件      | 接收缓冲 / 标志 |
|--------|-------------|-----------------|
| USART1 | `usart.h`   | `USART_RX_BUF`, `USART_RX_CNT`, `USART_RX_Complete` |
| USART2 | `usart2.h`  | `RS485_RX_BUF`, `RS485_RX_CNT`, `RS485_RX_Complete` |
| USART3 | `usart3.h`  | `USART3_RX_BUF`, `USART3_RX_CNT`, `USART3_RX_Complete` |
| UART4  | `uart4.h`   | `UART4_RX_BUF`, `UART4_RX_CNT`, `UART4_RX_Complete` |

## Keil 工程

- 打开 `USER/Template.uvprojx`。
- **Include Path** 需包含：`..\third_party`、`..\drivers`、`..\board`、`..\app`、`..\app\utils`、`..\app\comm`（已与 `SYSTEM`、`USER`、`FWLIB\inc` 等一并配置）。
- 源码分组：`THIRDPARTY`、`DRIVERS`、`BOARD`、`APP`、`APP_COMM`、`SYSTEM`、`USER` 等。
- 新增 `app/comm/uart3_at_sequence.c` 后，请在 Keil 工程 **APP_COMM** 分组中加入该文件并编译。

## 依赖方向（约定）

- `app/comm` → `SYSTEM/usart`（仅头文件中的缓冲声明）、`app`、`drivers`、`board`、`third_party`。
- `SYSTEM/usart` → 不依赖 `app`（避免循环依赖）。
- `board` → `drivers`、`SYSTEM`（按需）。

## 开发环境

- **IDE**: Keil MDK-ARM  
- **MCU**: STM32F407VET6  
- **说明**: `bsp_hc595`、`sha1` 等位于 `board/` 或 `third_party/`，以仓库内实际路径为准。
