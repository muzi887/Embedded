# 项目结构说明

## 目录结构

```
tikong-20260403/
├── CORE/              # ARM Cortex-M4 核心文件
│   ├── core_cm4.h     # Cortex-M4 核心定义
│   └── startup_*.s    # 启动文件
│
├── FWLIB/             # STM32 标准外设库
│   ├── inc/           # 头文件（31个）
│   └── src/           # 源文件（36个）
│
├── HARDWARE/          # 硬件驱动层
│   ├── iic.c/h        # I2C 总线驱动
│   ├── eeprom.c/h     # EEPROM 存储驱动
│   ├── DS3231.c/h     # DS3231 实时时钟驱动
│   ├── cJSON.c/h      # JSON 解析库
│   ├── sha1.c/h       # SHA1 加密算法
│   ├── blackList.c/h  # 黑名单管理
│   ├── data_handler.c/h  # 数据处理
│   ├── Live_data.c/h  # 实时数据处理
│   ├── Live_data_r.c/h # 实时数据接收
│   ├── timer.c/h      # 定时器管理
│   └── malloc.c/h     # 内存管理
│
├── SYSTEM/            # 系统层
│   ├── delay/         # 延时函数
│   ├── sys/           # 系统配置
│   └── usart/         # 串口驱动（USART2/3/4/5）
│
├── USER/              # 用户应用层
│   ├── main.c/h       # 主程序
│   ├── stm32f4xx_it.c/h  # 中断服务函数
│   ├── system_stm32f4xx.c/h  # 系统初始化
│   └── Template.uvprojx  # Keil 项目文件
│
├── docs/              # 工程文档（介绍、入门规划、模块说明）
│
└── OBJ/               # 编译输出目录（已忽略）
```

## 主要功能模块

### 1. 串口通信模块
- **USART1**: 调试串口（115200波特率，PA9/PA10）
- **USART2/3/4/5**: 业务串口，用于与外部设备通信

### 2. 数据处理模块
- **data_handler**: 串口数据解析和处理
- **Live_data**: 实时数据处理
- **cJSON**: JSON 数据格式处理

### 3. 存储模块
- **EEPROM**: 非易失性数据存储
- **blackList**: 黑名单数据管理

### 4. 时间管理
- **DS3231**: 实时时钟芯片驱动
- **timer**: 定时器功能

### 5. 安全模块
- **sha1**: SHA1 哈希算法，用于数据签名

## 开发环境

- **IDE**: Keil MDK-ARM
- **MCU**: STM32F407VET6 (Cortex-M4, 512KB Flash, 192KB RAM)
- **编译器**: ARM Compiler 5
- **调试器**: J-Link

## 编译说明

1. 使用 Keil MDK 打开 `USER/Template.uvprojx`
2. 选择目标芯片：STM32F407VETx
3. 编译生成 HEX/BIN 文件
4. 通过 J-Link 下载到目标板

## 注意事项

- 注意内存限制（RAM 192KB）
- 串口缓冲区大小需要根据实际需求调整
- 中断优先级需要合理配置
- 注意数据处理的实时性要求
