# 梯控门禁网关 V2 — 物模型摘要（事件 / 服务）

更新时间：2026-07-18  
来源：通讯协议 `docs/cloud/4G/梯控门禁网关V2_20260606.docx` 中物模型 JSON（`productId: 30`）  
说明：本文把原始 JSON **改成表格**，便于查阅；实现以固件为准。

> **相关**：[parseSerialData-case101.md](./parseSerialData-case101.md)、[at-config-flow.md](./4g/at-config-flow.md)  
> **Topic（协议侧常见）**  
>
> - 事件上报：`/sys/{productKey}/{deviceKey}/thing/event/post`  
> - 服务调用：`/sys/{productKey}/{deviceKey}/thing/service/invoke/+`  
> - 服务回执：`/sys/{productKey}/{deviceKey}/thing/service/reply`

`properties`：本摘录为空数组（无属性定义）。

---

## 一、总览

### 1.1 事件（设备 → 平台，透传类）


| key                                 | 名称    | 固件组帧（典型）                    |
| ----------------------------------- | ----- | --------------------------- |
| `card_unvarnished_transmission`     | 刷卡透传  | `card_Reply`（`Live_data.c`） |
| `password_unvarnished_transmission` | 密码透传  | `pwd_Reply`                 |
| `qr_code_unvarnished_transmission`  | 二维码透传 | `qr_Reply`                  |


触发多在读头路径：`QRProcessUart*` → `CommContrl` / 密码处理 → 上述 Reply，经 USART3 上报。

### 1.2 服务（平台 → 设备，同步调用）


| key                | 名称     | 固件入口（典型）                                |
| ------------------ | ------ | --------------------------------------- |
| `blacklist_add`    | 黑名单新增  | `parseSerialData` **case 105**          |
| `blacklist_delete` | 黑名单删除  | **case 106**                            |
| `blacklist_get`    | 获取黑名单  | **case 104**                            |
| `call_elevator`    | 呼梯     | **case 5** → `parse_call_elevator_json` |
| `open_door`        | 开门     | **case 6** → `parse_open_door_json`     |
| `device_config`    | 设备配置   | AT 订阅有；读头设置也有 `Cmd_Setting`             |
| `get_device_info`  | 设备信息获取 | AT 订阅有；`parseSerialData` **case 4** 等   |


MCU 侧云端服务帧仍是 `数字,JSON`（见 `parseSerialData`）；具体数字与模组透传通道有关，以串口日志为准。

---

## 二、事件详表

### 2.1 `card_unvarnished_transmission` — 刷卡透传

将读取到的刷卡信息上报平台。


| 输出字段           | 说明        | 类型         |
| -------------- | --------- | ---------- |
| `cardData`     | 卡片数据      | text，≤4096 |
| `cardNumber`   | 卡号（物理卡号）  | text，≤64   |
| `cardTime`     | 刷卡时间      | text，≤32   |
| `handleResult` | 处理结果（结构体） | 见下         |


`**handleResult`**


| 字段                 | 说明                            |
| ------------------ | ----------------------------- |
| `code`             | `200` 放行；`400` 参数错误；`401` 无权限 |
| `msg`              | 消息内容，≤256                     |
| `error`            | 错误，≤128                       |
| `errorDescription` | 错误描述，≤256                     |


---

### 2.2 `password_unvarnished_transmission` — 密码透传

将读取到的密码信息上报平台。


| 输出字段           | 说明   | 类型       |
| -------------- | ---- | -------- |
| `passwordData` | 密码数据 | text，≤32 |
| `passwordTime` | 密码时间 | text，≤32 |
| `handleResult` | 处理结果 | 见下       |


`**handleResult**`


| 字段                                   | 说明                 |
| ------------------------------------ | ------------------ |
| `code`                               | `200` 放行；`401` 无权限 |
| `msg` / `error` / `errorDescription` | 同上                 |


---

### 2.3 `qr_code_unvarnished_transmission` — 二维码透传

将二维码识别结果上报平台（**无论是否解析成功**）。


| 输出字段           | 说明    | 类型                        |
| -------------- | ----- | ------------------------- |
| `qrCodeData`   | 二维码数据 | text，≤4096                |
| `qrCodeTime`   | 识别时间  | text，≤32                  |
| `handleResult` | 处理结果  | 同刷卡：`200` / `400` / `401` |


---

## 三、服务详表

### 3.1 `blacklist_add` — 黑名单新增

向设备增加黑名单。`callType`: SYNC。

**输入 `blacklist[]`**


| 字段           | 说明             |
| ------------ | -------------- |
| `cardNumber` | 卡号，text ≤10    |
| `type`       | `1` 二维码；`2` 卡片 |


**输出**：`code`（`200` 成功）、`msg`、`error`、`errorDescription`

---

### 3.2 `blacklist_delete` — 黑名单删除

删除设备黑名单。输入结构同新增；`code` 语义为「删除成功」。

---

### 3.3 `blacklist_get` — 获取黑名单

无输入。输出除 `code`/`msg`/`error`/`errorDescription` 外，另有：


| 字段       | 说明                                        |
| -------- | ----------------------------------------- |
| `data[]` | 黑名单列表：`cardNumber` + `type`（1 二维码 / 2 卡片） |


---

### 3.4 `call_elevator` — 呼梯

远程呼叫电梯。


| 输入                    | 说明                     |
| --------------------- | ---------------------- |
| `floorRelayNumbers[]` | 楼层继电器编号，int32 数组，最多 40 |


**输出 `code`**：`200` 成功；`400` 参数错误；`500` 设备错误（另有 msg/error/errorDescription）

固件：`parse_call_elevator_json` → `call_elevator_Reply`。

---

### 3.5 `open_door` — 开门

远程开门。


| 输入                   | 说明                    |
| -------------------- | --------------------- |
| `doorRelayNumbers[]` | 门禁继电器编号，int32 数组，最多 2 |


**输出 `code`**：同呼梯 `200` / `400` / `500`。

固件：`parse_open_door_json` → `call_open_door_Reply`。

---

### 3.6 `device_config` — 设备配置

配置设备基本信息。


| 输入字段           | 说明                         |
| -------------- | -------------------------- |
| `deviceType`   | `1` 大门门禁；`2` 单元门禁；`3` 单元电梯 |
| `deviceCode`   | 设备编码，≤64                   |
| `seedPassword` | 种子密码，≤64                   |
| `deviceTime`   | 设备时间，≤16                   |
| `doorConfig[]` | 门禁配置（最多 2 项），见下            |


`**doorConfig[]` 项**


| 字段                  | 说明                |
| ------------------- | ----------------- |
| `relayNumber`       | `0` / `1` 号继电器    |
| `defaultStatus`     | `0` 默认常开；`1` 默认常闭 |
| `defaultEnableTime` | 默认使能时间（秒）         |


输出：`code` / `msg` / `error` / `errorDescription`（code 为 text）。

---

### 3.7 `get_device_info` — 设备信息获取

无输入。输出：


| 字段               | 说明                   |
| ---------------- | -------------------- |
| `deviceAccount`  | 设备账号                 |
| `devicePassword` | 设备密码                 |
| `deviceType`     | 同配置：1/2/3            |
| `deviceCode`     | 设备编码                 |
| `seedPassword`   | 种子密码                 |
| `deviceTime`     | 设备时间                 |
| `doorConfig[]`   | 同 `device_config` 结构 |


---

## 四、对照固件怎么用本文

```text
平台物模型 key（本文）
        │
        ├─ 事件（刷卡/密码/二维码）→ Live_data.c 的 card_Reply / pwd_Reply / qr_Reply
        │                              读头路径触发，非 parseSerialData
        │
        └─ 服务（黑名单/呼梯/开门…）→ USART3 收包 → G4GProcess → parseSerialData
                                       再进对应 case / parse_*_json / *_Reply
```

建议：

1. 在本文锁定 **key** 与字段名。
2. 事件：打开 `Live_data.c` 搜同名 `\"key\": \"...\"`。
3. 服务：打开 `Live_data_r.c` 的 `switch`，再搜 `parse_`*。
4. 串口联调看 USART1 的 `rx->` / `*_Reply sent`。

---


## 五、原始 JSON

完整物模型见同目录：[`thing-model-v2.json`](./thing-model-v2.json)。

以下为摘录示意（字段已省略）：

```json
{
  "properties": [],
  "events": [ /* 刷卡透传 / 密码透传 / 二维码透传 */ ],
  "services": [ /* 黑名单新增 / 黑名单删除 / 获取黑名单 / 呼梯 / 设备配置 / 设备信息获取 / 开门 */ ]
}
```

