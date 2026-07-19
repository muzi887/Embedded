# 4G 模块 AT 指令配置流程（整流程说明）

更新时间：2026-07-18

> **源码主文件**：`App/Cloud/uart3_at_sequence.c` / `.h`  
> **触发**：`App/Pass/cmd.c` → `Cmd_Setting`（读头设置命令，含 MQTT 字段）  
> **调度**：`User/main.c` 主循环  
> **串口**：`Hardware/Serial/usart3.*`（USART3 @ 115200）  
> **模组脚**：`Hardware/Modem/4G.*`（RST / LINKA，不发 AT 字节）  
> **对照**：[source-map.md](./source-map.md)、`docs/test/AT.txt`、`docs/cloud/4G/MQTT_AT_SET.txt`

---

## 一、一句话结论

读头下发 **带 MQTT 参数的设备设置**（命令字 `'1'`，CSV 共 **13 列**）后，固件：

1. 先设 RTC、把 MQTT 参数放进全局缓冲，并 **暂存** 待写 EEPROM 的快照  
2. 置 `g_uart3_at_sequence_request = 1`，主循环 **暂停** `app_poll`（读头/云 JSON 都不跑）  
3. 状态机经 USART3 顺序发送 **19 条 AT**，每条等模组回复含 `OK`（超时 2s）  
4. 全部成功 → 写 EEPROM → `GM4G_Restart` → **系统复位**；失败则 abort，清标志，恢复日常业务  

日常联网后的平台 JSON **不走本流程**，走 `G4GProcess` → `parseSerialData`（与 AT 互斥）。

---

## 二、端到端总览

```text
读头 JSON type=0/1, data 以 '1' 开头（设置）
        │
        ▼
QRProcessUart4/5 → CommContrl → Cmd_Setting
        │
        ├─ CSV 仅 9 列：简化写 EEPROM，不启 AT，返回
        │
        └─ CSV 13 列：拷贝 MQTT → 快照 pending → request=1
                │
                ▼
main while(1)
  if (request) ──────────────────────────────┐
        uart3_at_sequence_poll()               │  不跑 app_poll
          IDLE → 建 19 条命令表                  │
          SEND → WAIT(OK) → GAP → …            │
          全 OK → DONE_ALL_OK                  │
          失败  → ABORTED                      │
  else → app_poll() …                          │
                                               ▼
                    ┌─ DONE：Cmd_Setting_OnAtSequenceDone（写 EEPROM）
                    │         GM4G_Restart()；NVIC_SystemReset()
                    └─ ABORT：Cmd_Setting_OnAtSequenceAbort（丢弃 pending）
                              request=0，恢复 app_poll
```

---

## 三、触发条件（何时进入 AT 配置）

### 3.1 读头侧

| 条件 | 说明 |
| --- | --- |
| `QRProcessUart*` 凑齐 `{...}` | 见 [qr-process-uart45.md](../../pass/qr-process-uart45.md) |
| JSON `type` 为 `'0'`/`'1'` | 才会进 `CommContrl` |
| `data[0] == '1'` | `CommContrl` 调 `Cmd_Setting` |

### 3.2 `Cmd_Setting` 内

| CSV 列数 | 行为 |
| --- | --- |
| **9** | 简化设置：写部分 EEPROM，**不**启 AT 序列 |
| **13** | 含 MQTT 四字段（addr / productKey / deviceKey / deviceSecret）→ **启动 AT** |
| 其它 | 返回失败 |

13 列时关键动作（顺序）：

1. 解析系统时间并 `Set_CurrentTime`  
2. `strncpy` → `g_mqtt_addr` / `g_mqtt_productKey` / `g_mqtt_deviceKey` / `g_mqtt_deviceSecret`  
3. `s_cmd_setting_eeprom_pending = 1` + `Cmd_Setting_SnapshotForEeprom()`（设备账号等快照，**等 AT 全成功再写盘**）  
4. `g_uart3_at_sequence_request = 1`

日志示例：`[Cmd_Setting] 4G AT sequence started; EEPROM after all OK`

---

## 四、主循环互斥（为何配网时扫码无反应）

```c
/* User/main.c 示意 */
if (g_uart3_at_sequence_request) {
    switch (uart3_at_sequence_poll()) { ... }
} else {
    app_poll();   /* 含 QRProcess / G4GProcess */
}
```

| 标志 | 主循环 | USART3 数据含义 |
| --- | --- | --- |
| `request == 1` | 只跑 AT 状态机 | 等 AT 回复里的 `OK` / `ERROR` |
| `request == 0` | `app_poll` | 平台 `数字,JSON` → `parseSerialData` |

同一套 `USART3_RX_BUF` / `USART3_RX_Complete`，**谁读取决于当前分支**。

---

## 五、状态机逻辑（`uart3_at_sequence_poll`）

### 5.1 状态

| 状态 | 行为 |
| --- | --- |
| `IDLE` | `uart3_at_build_cmd_table()` 填表；清 USART3 DMA 缓冲；转入 SEND（同次 poll 可 fall-through） |
| `SEND` | `USART3_SendData` 发当前条；记时；进 WAIT；返回 `BUSY` |
| `WAIT` | 未超时且无收包 → `BUSY`；有包则查 ERROR / 必须含 `OK`；成功则下标+1 或全部完成 |
| `GAP` | 指令间隔 **50ms**，再回 SEND |

### 5.2 等待判定（每条）

| 结果 | 条件 | 返回 |
| --- | --- | --- |
| 继续 | 未满 2000ms 且尚无 `USART3_RX_Complete` | `BUSY` |
| 失败 | 超时 / 含 ERROR / 无子串 `OK` | `ABORTED`（`seq_abort`） |
| 下一条 | 有 `OK` 且还有命令 | `BUSY`（经 GAP） |
| 成功 | 19 条都 OK | `DONE_ALL_OK` |

时间基准：`g_tim4_ms_tick`（TIM4）。

### 5.3 `main` 对返回值的处理

| 返回值 | `main` 动作 |
| --- | --- |
| `UART3_AT_SEQ_BUSY (0)` | 下圈继续 poll |
| `UART3_AT_SEQ_DONE_ALL_OK (1)` | `Cmd_Setting_OnAtSequenceDone()` → `GM4G_Restart()` → `NVIC_SystemReset()` |
| `UART3_AT_SEQ_ABORTED (2)` | `Cmd_Setting_OnAtSequenceAbort()`；`request = 0` |

成功路径上 **EEPROM 在复位前写入**；复位后模组按已保存 AT 配置联网，业务再走 JSON 路。

---

## 六、19 条 AT 命令表（建表内容）

建表函数：`uart3_at_build_cmd_table()`。  
前置：pk / dk / secret 非空；`g_mqtt_addr` 可格式化为 `host,port`（支持 `host:port` → 逗号）。

命令前缀为模组透传风格：`usr.cn#` + AT 正文 + `\r\n`。

| 序号 | 命令意图（摘要） |
| --- | --- |
| 0 | `AT+WKMOD=MQTT,NOR` 工作模式 |
| 1 | `AT+MQTTSVR=host,port` 服务器 |
| 2 | `AT+MQTTUSER=` deviceKey |
| 3 | `AT+MQTTPSW=` deviceSecret |
| 4 | `AT+MQTTCID=pk\|dk` ClientID |
| 5 | `AT+MQTTMOD=1` |
| 6 | `AT+HEARTEN=OFF` |
| 7～13 | `AT+MQTTSUBTP=…` 订阅物模型相关主题（时间回复、事件回复、若干 service、属性回复等） |
| 14～17 | `AT+MQTTPUBTP=…` 发布主题（校时请求、事件、服务回复、属性上报等） |
| 18 | `AT+S` 保存配置 |

具体 topic 路径含 `/sys/{productKey}/{deviceKey}/...`，与平台物模型约定一致；手册级原文可对照 `docs/test/AT.txt`、`MQTT_AT_SET.txt`（示例参数可能与现场不同，以 EEPROM/读头下发为准）。

建表失败（缺参、地址非法）→ 直接 `ABORTED`，不发串口。

---

## 七、硬件与驱动分工

| 模块 | 职责 |
| --- | --- |
| `board_init`：`uart3_init(115200)` | 打开与模组通信的串口 |
| `board_init`：`GM4G_Init()` | RST / LINKA GPIO |
| `usart3` | DMA+IDLE 收包；`USART3_SendData` 发 AT |
| `uart3_at_sequence` | 组命令、发、等 OK、状态机 |
| `4G.c`：`GM4G_Restart` | AT 全成功后硬件复位模组（注释：否则网络可能异常） |

**`4G.c` 不解析 AT 文本。**

---

## 八、成功 / 失败后 EEPROM

| 时机 | 函数 | 行为 |
| --- | --- | --- |
| AT 进行中 | — | 设备密钥等 **尚未** 因本次设置写入（仅内存快照 pending） |
| 全 OK | `Cmd_Setting_OnAtSequenceDone` | 若 pending：把快照写入 EEPROM |
| 失败/中止 | `Cmd_Setting_OnAtSequenceAbort` | 清 pending，**不写** 本次设置快照 |

因此：配网中途掉电或 AT 失败，不会把「半套」MQTT/设备参数当成已生效配置落盘（相对 13 列完整路径而言）。

---

## 九、联调与排障

1. **USART1 看 `[AT_SEQ]`**：`start` / `TX n/19` / `OK step` / `FAIL: timeout|ERROR|no OK`。  
2. **扫码无反应**：查是否 `request==1`（互斥属预期）。  
3. **建表即 abort**：查读头是否下发了完整 MQTT 四字段与合法地址。  
4. **某步超时**：模组未进 AT 模式、波特率、接线、供电；或回复不含字面 `OK`。  
5. **全 OK 后仍不上云**：看复位是否执行、模组 LINKA、平台侧 topic/密钥是否与写入一致。  
6. **与 JSON 路混淆**：配网成功复位后，日常应走 `G4GProcess`，不应再长期卡在 AT 分支。

---

## 十、源码索引

| 文件 | 与本流程关系 |
| --- | --- |
| `App/Pass/cmd.c` | `Cmd_Setting` 触发；Done/Abort 写盘策略 |
| `App/Cloud/uart3_at_sequence.c` | 19 步状态机 |
| `User/main.c` | 互斥调度与复位 |
| `Hardware/Serial/usart3.c` | 收发管道 |
| `Hardware/Modem/4G.c` | 复位脚 |
| `App/Pass/data_handler.c` | `g_mqtt_*` 全局缓冲定义处 |

阅读顺序建议：先本文总览 → [source-map.md](./source-map.md) 对着 `main` 分叉点文件 → 再对照 `AT.txt` 与 `BC(n, ...)` 逐条。
