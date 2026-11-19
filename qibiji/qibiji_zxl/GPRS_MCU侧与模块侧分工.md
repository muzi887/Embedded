# MCU 侧与模块侧文件分工

## 先澄清一个关键概念

**工程里所有 `.c/.h` 文件都是 MCU 侧**——它们编译后烧到 STM32F103 里。

**模块侧 = WH-LTE-7S1 模块内部的固件**（厂商烧好的黑盒），**不在这个工程里**，看不到也改不了它的源码。MCU 通过 USART2 发 AT 命令让模块干活。

---

## MCU 侧文件分层（按"离硬件远近"排序）

### 第 1 层：串口物理层（最轻，只搬字节）

| 文件 | 干什么 |
|---|---|
| `gprs_port.c/h` | USART2 GPIO/波特率配置、字节收发、`GPRS_Printf` 调试输出 |
| `uart_queue.c/h` | AT 命令队列（推一条等 OK 再推下一条）、DMA RX、超时/重试 |

### 第 2 层：AT 命令层（组织"问模块什么"）

| 文件 | 干什么 |
|---|---|
| `gprs_at.c/h` | **GPRS 状态机**：State1 探测 → State2 推 AT init → State3 等 OK → State4 NORMAL；probe 周期性问 MQTTSVR/MQTTMOD/CSQ/... |
| `gprs_config.h` | AT 命令字符串宏：`"usr.cn#AT+MQTTSVR?\r\n"` 等 |
| `gprs_4g.c/h` | 读 LINKA 引脚（PC5）、模块电源/复位控制 |

### 第 3 层：应用协议层（拼业务数据）

| 文件 | 干什么 |
|---|---|
| `gprs_json.c/h` | 拼 timeSync JSON、拼心跳 `{}`、timeSync 回包解析 |
| `property_report.c/h` | 拼属性上报 JSON（`Opening`/`MotorCurrent`） |
| `event_pub.h` | 事件上报接口 |

### 第 4 层：应用支撑（RTC/凭据/活性）

| 文件 | 干什么 |
|---|---|
| `gprs_rtc.c/h` | 维护本地 RTC，靠 timeSync 回包校准 |
| `net_liveness.c/h` | 心跳 miss 计数、timeSync 等待超时 |
| `gprs_config_store.c/h` | EEPROM 读写 MQTT 凭据（pk/dk/secret/addr） |
| `gprs.c/h` | 对外统一 API |

### 第 5 层：第三方库

| 文件 | 干什么 |
|---|---|
| `cJSON.c/h` | JSON 字符串解析 |
| `malloc.c` / `mymalloc.h` | 动态内存管理 |

---

## 模块侧（黑盒，不在工程里）

WH-LTE-7S1 模块内部固件负责所有**重活**：

| 重活 | 触发方式 | 谁实现 |
|---|---|---|
| 4G 基站注册、PDP 激活 | 上电自动 | 模块 |
| TCP 连接建立（Socket A） | `AT+MQTTSVR=...` + `AT+MQTTMOD=1` | 模块 |
| MQTT CONNECT / SUBSCRIBE | 模块自动 | 模块 |
| MQTT keep-alive（PINGREQ/PINGRESP） | 模块自动 | 模块 |
| MQTT 断线重连 | 模块自动 | 模块 |
| LINKA 拉高/拉低 | 模块内部状态映射到 PC5 | 模块 |

---

## 数据流：MCU 和模块怎么配合

```
┌─────────────────────────────────────────────────────────────┐
│  MCU 侧（本工程所有 .c）                                     │
│                                                              │
│  property_report.c ──拼JSON──> gprs_json.c                  │
│                                     │                        │
│                                     ▼                        │
│                            uart_queue.c (排队)               │
│                                     │                        │
│                                     ▼                        │
│                            gprs_port.c (USART2 字节流)       │
└──────────────────────────────────────┬──────────────────────┘
                                       │ 串口 TX
                                       ▼
                          ┌─────────────────────────┐
                          │  WH-LTE-7S1 模块（黑盒） │
                          │  - MQTT publish         │
                          │  - TCP Socket A         │
                          │  - 4G 基站连接          │
                          │  - keep-alive / 重连    │
                          └────────────┬────────────┘
                                       │ 4G 空口
                                       ▼
                                  云服务器
```

**MCU 干的事 = 拼 JSON 字符串 + 排队推串口 + 读 LINKA 引脚电平。**
**模块干的事 = 把串口收到的字节按 MQTT 协议发到 4G 网络，并自己维护连接。**
