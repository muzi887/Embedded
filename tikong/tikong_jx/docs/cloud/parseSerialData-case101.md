# `parseSerialData` 与 case 101（RS485 透传）

更新时间：2026-07-18

> **源码**：`App/Cloud/Live_data_r.c`（`parseSerialData`、`parse_rs485_json`）  
> **回执**：`App/Cloud/Live_data.c` → `First_Reply`  
> **入口**：`App/Cloud/g4g_comm.c` → `G4GProcess`（`USART3_RX_Complete` 后调用）  
> **相关**：[app-poll-flow.md](../overview/flows/app-poll-flow.md)、[at-config-flow.md](./4g/at-config-flow.md)（配网时不进本路径）

---

## 一、一句话结论

云端经 4G/USART3 下发一帧字符串，格式为：

```text
数字,JSON对象
```

`parseSerialData` 先找 **第一个逗号**，左边 `atoi` 成 `caseNumber`，右边 `cJSON_Parse`；再 `switch` 分发。  
**case 101** = RS485 透传：从 JSON 取出十六进制 `data` → 转字节 → `RS485_SendData` → `First_Reply` 经 USART3 回平台。

---

## 二、谁调用、何时跑

```text
app_poll()
  └─ G4GProcess()
        if (USART3_RX_Complete)
          缓冲补 '\0' → 打印
          parseSerialData(USART3_RX_BUF)
          清 Complete / CNT
```

仅在 `**g_uart3_at_sequence_request == 0**` 时 `app_poll` 才会跑到这里；AT 配网期间本函数不会被调用。

---

## 三、帧格式：先找逗号

```c
/* Live_data_r.c — parseSerialData 开头逻辑 */
commaPos = strchr(data, ',');     /* 无逗号 → 直接 return */
numberStr ← data[0 .. comma前)   /* 数字前缀 */
jsonStr   ← commaPos + 1         /* JSON 文本 */
caseNumber = atoi(numberStr);
jsonData = cJSON_Parse(jsonStr); /* 失败 → return */
messageId = 当前 RTC 毫秒时间戳; /* 回执用 */
switch (caseNumber) { ... }
cJSON_Delete(jsonData);
```

示意：

```text
101,{"arguments":{...},"requestId":"1",...}
^^^                                    ^^^^^^^^^^^^^^^^
caseNumber                             jsonStr → cJSON
```

### 实现注意（读源码时）

当前 `numberStr` 数组长度为 **2**，且要求逗号前长度 `< 2`，即校验上只允许 **1 位**数字前缀（如 `1,{...}`）。  
`switch` 里却写有 `101`、`104` 等三位数分支——若平台实际下发 `101,{...}`，会先打出 `Invalid number format` 进不了 case。联调以 USART1 日志为准；若要对齐三位数，需加大前缀缓冲与长度判断。

下文按 **进入 `case 101` 之后的逻辑** 说明（即 `caseNumber == 101` 时的本地动作与回执）。

---

## 四、`switch` 分支一览


| caseNumber | 含义（注释）   | 解析函数                       | 回执（典型）            |
| ---------- | -------- | -------------------------- | ----------------- |
| **101**    | RS485 透传 | `parse_rs485_json`         | **`First_Reply`** |
| 104        | 提取黑名单    | `parse_tiqublack_json`     | `Four_Reply`      |
| 105        | 增加黑名单    | `parse_addblack_json`      | `Fifth_Reply`     |
| 106        | 删除黑名单    | `parse_delblack_json`      | `Sixth_Reply`     |
| 107        | 更新公钥     | `parse_upgongyao_json`     | （回执调用现注释）         |
| 1          | 在线校时     | `parse_uptime_json`        | —                 |
| 4          | 读设备配置    | `parse_readsetting_json`   | —                 |
| 5          | 呼梯       | `parse_call_elevator_json` | —                 |
| 6          | 开门       | `parse_open_door_json`     | —                 |
| default    | 未知       | —                          | 打印后结束             |


---

## 五、追 case 101：本地动作 + 回执

### 5.1 流程图

```text
parseSerialData 进入 case 101
        │
        ▼
parse_rs485_json(jsonData)
  取出 arguments.data（十六进制字符串）→ data_1
  取出 requestId / requestTimestamp → requestId_1 / requestTimestamp_1
        │
        ▼
蜂鸣：ret!=0 → Beep(2)；否则 Beep(1)
        │
        ▼
hexStringToBytes(data_1 → buffer_data)     ← 本地：Hex 文本 → 二进制
        │
        ▼
RS485_SendData(buffer_data, dataLen)       ← 本地：USART2 发往梯控/485 总线
        │
        ▼
First_Reply(messageId, requestId_1, requestTimestamp_1)
        │                                  ← 回执：组 "2,{...}" 经 USART3 回云
        └─ break → cJSON_Delete
```

### 5.2 解析：`parse_rs485_json`

期望 JSON 形态（源码注释示例）：

```json
{
  "arguments": {
    "baud_rate": 19200,
    "data_length": 8,
    "data_start_length": 1,
    "data_stop_length": 1,
    "verify": "N",
    "data": "7E1A0001010ACC010F0202000000011000000000000000000000BD0D"
  },
  "deviceKey": "...",
  "key": "data_transmit_RS485",
  "productKey": "...",
  "requestId": "1",
  "requestTimestamp": 1755141246512
}
```


| 字段                               | 用途                                            |
| -------------------------------- | --------------------------------------------- |
| `arguments.data`                 | **真正透传载荷**（Hex 字符串）→ 全局 `data_1`              |
| `requestId` / `requestTimestamp` | 回执原样带回 → `requestId_1` / `requestTimestamp_1` |
| 其它 arguments（波特率等）               | 读入局部/全局数值；**本 case 后续未用它们改 USART2 波特率**       |


缺必要字段 → 返回 `-1`；成功 → 返回 `0`。

注意：`case 101` 里 **无论 `ret` 成败**，后面仍会执行 `hexStringToBytes` → `RS485_SendData` → `First_Reply`（失败时可能用到旧的 `data_1`）。蜂鸣仅区分 `ret`。

### 5.3 本地动作

1. `**hexStringToBytes`**：每两个 Hex 字符合成一字节，写入 `buffer_data`，返回 `dataLen`。
2. `**RS485_SendData(buffer_data, dataLen)**`（`Hardware/Serial/usart2`）：经 RS485 发到外设（梯控板等）。

这就是「透传」：云端给的十六进制帧，原样变成总线上的二进制帧。

### 5.4 回执：`First_Reply`

定义于 `Live_data.c`。组一帧再 `USART3_SendData`：

```json
2,
{
  "code": "200",
  "msg": "操作成功",
  "data": {
    "messageId": "1755163708121",
    "requestId": "1",
    "requestTimestamp": "1755163708107",
    "body": {
      "msg": "SUCCESS",
      "code": "200",
      "data": {
        "result": "240A0ACC010001010F00F40D"
      }
    }
  }
}

```


| 部分                               | 含义                                   |
| -------------------------------- | ------------------------------------ |
| 前缀 `2,`                          | 上行通道/类型数字（与下行 `数字,JSON` 对称）          |
| `messageId`                      | `parseSerialData` 里用当前 RTC 生成的毫秒戳    |
| `requestId` / `requestTimestamp` | 来自下行 JSON（经 `parse_rs485_json`）      |
| `body.data.result`               | 当前实现为 **写死的示例 Hex 串**，并非把 485 实收内容回传 |


因此 101 的「回执」= **告知平台服务已处理（固定 SUCCESS 模板）**，不是完整的 485 应答镜像。

---

## 六、和读头 `CommControl` 路径对比


|     | 云 case 101                       | 读头通行                            |
| --- | -------------------------------- | ------------------------------- |
| 入口  | `G4GProcess` → `parseSerialData` | `QRProcessUart`* → `CommControl` |
| 载荷  | `数字,JSON`                        | 读头 JSON 的 `data` CSV            |
| 本地  | Hex → `RS485_SendData`           | 验签/权限/楼层等                       |
| 回云  | `First_Reply`（USART3）            | 权限路径里另有上报（如扫码/刷卡回复）             |


两条路都可能动 RS485，但 **触发源与组帧方式不同**。

---

## 七、联调步骤（只追 101）

1. 确认未在 AT 配网（`request == 0`）。
2. USART1 应看到 `G4GProcess` 打印的 `rx->` 整帧。
3. 能进 101 时有 `Processing case 1`（源码 printf 文案如此，实际是 case 101）。
4. 示波器/逻辑分析仪或对端设备看 USART2/RS485 是否出现与 `arguments.data` 对应的字节。
5. USART3 上看是否发出以 `2,{` 开头的 `First_Reply`。
6. 若只有 `Invalid number format`：检查逗号前数字位数与 `numberStr` 长度限制。

---

## 八、源码索引


| 符号 / 文件                                         | 职责                              |
| ----------------------------------------------- | ------------------------------- |
| `G4GProcess`                                    | 取 USART3 帧，调用 `parseSerialData` |
| `parseSerialData`                               | 拆 `数字,JSON` + `switch`          |
| `parse_rs485_json`                              | 101：抽出 Hex `data` 与 request 元数据 |
| `hexStringToBytes`                              | Hex 文本 → 字节数组                   |
| `RS485_SendData`                                | 本地 485 发送                       |
| `First_Reply`                                   | 经 USART3 回平台 SUCCESS 模板         |
| `data_1` / `requestId_1` / `requestTimestamp_1` | 101 用的全局暂存                      |


