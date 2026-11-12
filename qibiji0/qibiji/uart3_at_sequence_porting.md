# uart3_at_sequence.c 迁移文档

## 模块概述

`uart3_at_sequence.c` 实现了基于 USART3 的 4G 模块 AT 命令配置序列。该模块通过轮询方式依次发送一组初始化 AT 指令，验证结果并在全部成功后触发系统重启。

主要用途：
- 将 4G 模块切换到 MQTT 模式
- 配置 MQTT 服务器地址、用户、密码、客户端ID
- 启用 MQTT 模块
- 关闭心跳
- 订阅必需的阿里云物联网主题
- 注册必要的上行/下行话题
- 发送 `AT+S` 结束配置序列

## 公共接口

定义在 `uart3_at_sequence.h`：

```c
extern volatile u8 g_uart3_at_sequence_request;

void uart3_at_sequence_request_set(u8 en);
u8 uart3_at_sequence_should_run(void);
u8 uart3_at_sequence_poll(void);
```

返回值说明：
- `UART3_AT_SEQ_BUSY`：序列仍在执行中
- `UART3_AT_SEQ_DONE_ALL_OK`：全部 AT 指令执行成功
- `UART3_AT_SEQ_ABORTED`：执行失败或序列被中止

## 工作流程

1. 由外部设置 `g_uart3_at_sequence_request = 1` 或调用 `uart3_at_sequence_request_set(1)` 启动序列。
2. 主循环调用 `uart3_at_sequence_poll()`。
3. `uart3_at_sequence_poll()` 入口检查请求标志：
   - 若为 0，则返回 `UART3_AT_SEQ_BUSY` 并保持空闲状态
   - 若为 1，进入状态机执行
4. 状态机执行过程：
   - `AT_ST_IDLE`：构建 AT 命令表，重置 USART3 RX 标志和 DMA，转入 `AT_ST_SEND`
   - `AT_ST_SEND`：发送当前命令，记录超时时间，转入 `AT_ST_WAIT`
   - `AT_ST_WAIT`：等待 `USART3_RX_Complete` 完成或超时；检验接收字符串是否包含 `OK`、是否出现 ERROR；若成功则转入 `AT_ST_GAP`
   - `AT_ST_GAP`：等待固定间隔后回到 `AT_ST_SEND` 发送下一条命令
5. 若任一步骤失败，调用 `seq_abort()`：
   - 清除请求标志
   - 重置状态机
   - 重新初始化 USART3 DMA
6. 全部命令成功后，`uart3_at_sequence_poll()` 返回 `UART3_AT_SEQ_DONE_ALL_OK`。

## 关键配置和命令表

常量：
- `AT_CMD_COUNT = 19`：总命令数
- `AT_CMD_LINE_MAX = 320`：每条 AT 命令最大缓冲长度
- `AT_WAIT_OK_MS = 2000`：等待 OK 的超时时间（毫秒）
- `AT_CMD_GAP_MS = 50`：两条命令间隔时间（毫秒）

命令表内容（按顺序）：

1. `usr.cn#AT+WKMOD=MQTT,NOR`
2. `usr.cn#AT+MQTTSVR=<host,port>`
3. `usr.cn#AT+MQTTUSER=<deviceKey>`
4. `usr.cn#AT+MQTTPSW=<deviceSecret>`
5. `usr.cn#AT+MQTTCID=<productKey>|<deviceKey>`
6. `usr.cn#AT+MQTTMOD=1`
7. `usr.cn#AT+HEARTEN=OFF`
8. `usr.cn#AT+MQTTSUBTP=1,1,/sys/<pk>/<dk>/system/time/reply,0`
9. `usr.cn#AT+MQTTSUBTP=2,1,/sys/<pk>/<dk>/thing/event/post_reply,0`
10. `usr.cn#AT+MQTTSUBTP=3,1,/sys/<pk>/<dk>/thing/service/invoke/device_config,0`
11. `usr.cn#AT+MQTTSUBTP=4,1,/sys/<pk>/<dk>/thing/service/invoke/get_device_info,0`
12. `usr.cn#AT+MQTTSUBTP=5,1,/sys/<pk>/<dk>/thing/service/invoke/call_elevator,0`
13. `usr.cn#AT+MQTTSUBTP=6,1,/sys/<pk>/<dk>/thing/service/invoke/open_door,0`
14. `usr.cn#AT+MQTTSUBTP=7,1,/sys/<pk>/<dk>/thing/property/post_reply,0`
15. `usr.cn#AT+MQTTPUBTP=1,1,/sys/<pk>/<dk>/system/time/get,0,0`
16. `usr.cn#AT+MQTTPUBTP=2,1,/sys/<pk>/<dk>/thing/event/post,0,0`
17. `usr.cn#AT+MQTTPUBTP=3,1,/sys/<pk>/<dk>/thing/service/reply,0,0`
18. `usr.cn#AT+MQTTPUBTP=4,1,/sys/<pk>/<dk>/thing/property/post,0,0`
19. `usr.cn#AT+S`

## 外部依赖

### 全局变量

来自 `data_handler.h`：
- `g_mqtt_addr`
- `g_mqtt_productKey`
- `g_mqtt_deviceKey`
- `g_mqtt_deviceSecret`

来自 `timer.h`：
- `g_tim4_ms_tick`

来自 `usart3.h`：
- `USART3_RX_BUF`
- `USART3_RX_CNT`
- `USART3_RX_Complete`
- `USART3_REC_LEN`

### USART3 功能

需要以下 USART3 接口：
- `USART3_SendData(uint8_t *data, uint16_t length)`：发送 AT 命令
- `USART3_DMA_ReInit(void)`：重置 DMA 接收状态

### 其他依赖

- `printf()`：用于调试日志输出，可移植时替换为目标平台日志函数
- 标准库字符串函数：`strlen`, `memcpy`, `strchr`, `strrchr`, `strstr` 等

## 重要逻辑点

### MQTT 服务器地址格式转换

`format_mqtt_svr()` 支持以下两种输入格式：
- `host,port`
- `host:port`

输出始终为 `host,port`。
失败条件：
- 输入为空
- 没有端口部分
- 端口为空
- 输出缓冲区不足

### AT 命令构建检查

`snprintf_chk()` 用于避免缓冲区溢出。若生成字符串超长或失败，则 `uart3_at_build_cmd_table()` 返回错误，整个序列中止。

### 响应判定规则

- 如果接收缓冲区包含 `\r\nERROR`、`ERROR\r\n` 或 `+CME ERROR`，判定为错误
- 如果接收缓冲区不包含 `OK`，判定为失败
- 如果超时 `AT_WAIT_OK_MS` 毫秒，则判定为失败

### 状态机

状态机共有 4 个状态：
- `AT_ST_IDLE`
- `AT_ST_SEND`
- `AT_ST_WAIT`
- `AT_ST_GAP`

`uart3_at_sequence_poll()` 采用非阻塞轮询模型，每次调用只执行当前状态的一步。

## 主循环集成方式

在 `USER/main.c` 中的集成方式如下：

```c
if (g_uart3_at_sequence_request)
{
    switch (uart3_at_sequence_poll())
    {
    case UART3_AT_SEQ_BUSY:
        break;
    case UART3_AT_SEQ_DONE_ALL_OK:
        Cmd_Setting_OnAtSequenceDone();
        GM4G_Restart();
        NVIC_SystemReset();
        break;
    case UART3_AT_SEQ_ABORTED:
        Cmd_Setting_OnAtSequenceAbort();
        g_uart3_at_sequence_request = 0;
        break;
    }
}
else
{
    app_poll();
    // 其他正常业务逻辑
}
```

触发点示例：`app/utils/cmd.c` 中将 `g_uart3_at_sequence_request = 1` 置位。

## 移植建议

### 1. 保留非阻塞轮询结构

该模块设计为每次主循环调用 `uart3_at_sequence_poll()` 处理一次步骤。移植时建议保持相同结构，避免将其变为长时间阻塞函数。

### 2. 替换串口驱动接口

移植到其他平台需提供等效：
- `SendData` 接口
- DMA/接收完成标志
- 接收缓冲区及计数器

如果目标平台不使用 DMA，可改为在串口接收回调中设置 `RX_Complete` 和 `RX_CNT`。

### 3. 提供定时基准

`g_tim4_ms_tick` 作为毫秒单调计时器。移植时保证该变量稳增且不会回绕在超时期间误判。

### 4. 提供配置参数

需填充以下字符串变量：
- `g_mqtt_addr`
- `g_mqtt_productKey`
- `g_mqtt_deviceKey`
- `g_mqtt_deviceSecret`

建议在调用序列前先验证这些值已正确加载。

### 5. 日志与错误处理

当前实现大量依赖 `printf()` 输出调试信息。移植时可替换成目标平台日志函数或删除无用日志。

### 6. 命令表扩展

如果需要适配不同的模块或主题，命令表构建函数 `uart3_at_build_cmd_table()` 是唯一需要修改的地方。

## 迁移优先级

1. 实现与 `USART3_SendData()` 等价的串口发送模块
2. 提供接收完成判定与数据计数
3. 提供毫秒计时变量
4. 映射 `g_mqtt_*` 配置变量
5. 使用主循环或任务定期调用 `uart3_at_sequence_poll()`
6. 在成功后执行设备重启或后续处理

## 可能的改造点

- 将 `g_uart3_at_sequence_request` 改为函数调用触发接口
- 将 `USART3_RX_*` 全局变量包装进结构体，便于复用多路串口
- 将命令表改成动态数组，支持不同产品主题
- 将 `AT_WAIT_OK_MS` 和 `AT_CMD_GAP_MS` 参数化为可配置值

---

文档基于 `app/comm/uart3_at_sequence.c` 与相关头文件分析生成，适合作为移植方案参考。