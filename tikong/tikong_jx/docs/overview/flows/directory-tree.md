# tikong_jx 目标目录树（按文件作用）

更新时间：2026-07-17  
来源：由「江协风格目录重构」目标树抽出，并结合当前工程实际文件。  
说明：只展示现行目录与文件职责；**不出现旧工程路径对照**。

---

```text
tikong_jx/
│
├── User/                              # 入口、板级编排、中断、Keil 工程
│   ├── main.c / main.h                # 程序入口；主循环（喂狗 / AT 序列 / app_poll）
│   ├── board_init.c / board_init.h    # 上电外设初始化（串口、定时器、RTC、EEPROM、4G…）
│   ├── app_run.c / app_run.h          # app_init / app_poll；当前时间缓存与刷新
│   ├── app_config.h                   # 应用侧配置宏 / 公共常量
│   ├── stm32f4xx_it.c / stm32f4xx_it.h # 中断服务函数总表（与启动文件衔接）
│   ├── stm32f4xx_conf.h               # SPL 外设头文件开关配置
│   ├── stm32f4xx.h                    # 芯片寄存器与器件头（工程本地拷贝）
│   └── tikong.uvprojx                 # Keil MDK 工程文件
│
├── System/                            # 与板无关的基础支撑
│   ├── delay/
│   │   └── delay.c / delay.h          # 毫秒 / 微秒延时
│   └── sys/
│       └── sys.c / sys.h              # 位带、系统级类型与常用底层宏
│
├── Hardware/
│   ├── Serial/                        # 串口驱动（仅 DMA/ISR/缓冲，不含业务 Process）
│   │   ├── usart1.c / usart1.h        # USART1：PC 调试口（printf、收包缓冲）
│   │   ├── usart2.c / usart2.h        # USART2：RS485 驱动与收发缓冲
│   │   ├── usart3.c / usart3.h        # USART3：4G 模组口驱动与大缓冲
│   │   ├── uart4.c / uart4.h          # UART4：读头 1 驱动（DMA + 空闲定帧）
│   │   └── uart5.c / uart5.h          # UART5：读头 2 驱动（DMA + 空闲定帧）
│   ├── Storage/                       # 片外存储驱动
│   │   ├── iic.c / iic.h              # 软件模拟 I2C 时序
│   │   ├── eeprom.c / eeprom.h        # EEPROM 业务读写封装（密钥、黑名单等布局）
│   │   └── 24cxx.c / 24cxx.h          # AT24Cxx 底层页读写
│   ├── Time/                          # 实时时钟
│   │   ├── RTC.c / RTC.h              # RTC 抽象接口（RtcChip_*，对上屏蔽芯片差异）
│   │   └── ds1302.c / ds1302.h        # DS1302 芯片驱动
│   ├── Board/                         # 本板外设与逻辑
│   │   ├── timer.c / timer.h          # TIM 周期任务（校时/限层/蜂鸣等标志）
│   │   ├── bsp_hc595.c / bsp_hc595.h  # 74HC595 移位输出
│   │   ├── ext_io.c / ext_io.h        # 扩展 IO
│   │   ├── floor_ctrl.c / floor_ctrl.h # 楼层权限 / 限层控制
│   │   ├── led.c / led.h              # LED 指示
│   │   ├── rly.c / rly.h              # 继电器输出
│   │   ├── wdg.c / wdg.h              # 独立看门狗 IWDG
│   │   └── malloc.c / malloc.h        # 板级动态内存分配
│   └── Modem/                         # 通信模组驱动（非云业务解析）
│       └── 4G.c / 4G.h                # 4G 模组上电 / 复位 / 基础控制
│
├── App/
│   ├── Pass/                          # 通行主路径
│   │   ├── qr_comm.c / qr_comm.h      # 读头轮询：QRProcessUart4 / Uart5
│   │   ├── cmd.c / cmd.h              # CommControl 与设置/权限/校时/限层等命令
│   │   └── data_handler.c / data_handler.h # 验签、密钥、设备参数等数据处理
│   ├── Cloud/                         # 云端 / 4G 业务
│   │   ├── g4g_comm.c                 # G4GProcess：USART3 收包入口
│   │   ├── Live_data.c / Live_data.h  # 上报侧物模型 / 属性组帧
│   │   ├── Live_data_r.c / Live_data_r.h # 下行解析：parseSerialData 等
│   │   └── uart3_at_sequence.c / .h   # 条件触发的 4G AT 配网序列状态机
│   ├── Store/                         # 存储相关业务（非底层驱动）
│   │   └── blackList.c / blackList.h  # 黑名单内存表与增删查
│   └── Link/                          # 其它通道轮询
│       ├── pc_comm.c                  # PCProcess：调试串口业务轮询
│       ├── rs485_comm.c               # Rs485Process：RS485 通道处理
│       └── app_comm.h                 # 通道 Process 函数声明汇总
│
├── Middlewares/                       # 第三方库（尽量少改）
│   ├── cJSON.c / cJSON.h              # JSON 解析与生成
│   └── sha1.c / sha1.h                # SHA1 摘要（通行验签）
│
├── Library/                           # STM32F4 标准外设库（SPL）
│   ├── inc/                           # 外设头文件
│   └── src/                           # 外设源文件
│
├── Start/                             # 启动与 CMSIS
│   ├── startup_stm32f40_41xxx.s       # 复位向量与启动汇编
│   ├── system_stm32f4xx.c / .h        # 系统时钟与芯片初始化
│   ├── core_cm4.h                     # Cortex-M4 内核寄存器定义
│   ├── core_cm4_simd.h                # SIMD 相关内联
│   ├── core_cmFunc.h                  # 内核函数内联
│   └── core_cmInstr.h                 # 内核指令内联
│
└── docs/                              # 工程说明（不参与编译）
    ├── README.md                      # 文档总索引
    ├── overview/                      # 总览、入门、清单、主流程图
    ├── pass/ / cloud/ / store/ …      # 按主题域的专文
    └── superpowers/                   # 设计规格与实施计划
```

---

## 依赖方向（速记）

```text
User        → App, Hardware, System
App         → Hardware, Middlewares, System（经 Serial 头缓冲）
Hardware    → System, Library（按需）
System      → Library
Middlewares → 不依赖 App
Hardware/Serial 不得依赖 App
```
