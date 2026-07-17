# parseSerialData 函数功能分析

## 📋 函数概述

**函数位置**：`HARDWARE/Live_data_r.c` (第626-760行)

**函数签名**：
```c
void parseSerialData(const char *data)
```

**调用位置**：`SYSTEM/usart/usart3.c` 的 `G4GProcess()` 函数中（第299行）

**功能描述**：解析来自4G模块（云平台）的串口数据，根据命令类型分发到相应的处理函数执行，并构造回复消息。

---

## 🔍 函数详细分析

### 1. 数据格式

**输入数据格式**：`"数字,JSON字符串"`

**示例**：
- `"1,{\"key\":\"value\"}"` - 命令类型1，后面是JSON数据
- `"5,{\"deviceKey\":\"xxx\",\"qrCodeIds\":[...]}"` - 命令类型5，增加黑名单

**格式说明**：
- 第一个逗号前的数字：命令类型编号（caseNumber）
- 第一个逗号后的内容：JSON格式的命令数据

---

### 2. 处理流程

#### 阶段1：数据格式验证与解析

```626:673:HARDWARE/Live_data_r.c
void parseSerialData(const char *data)
{
    int dataLength, ret;
    const char *jsonStr;

    cJSON *jsonData;
    char numberStr[2] = {0}; // 假设数字只有1位

    // 查找第一个逗号的位置
    char *commaPos = strchr(data, ',');
    if (commaPos == NULL)
    {
        printf("Invalid data format: no comma found\n");
        return;
    }

    // 提取数字部分
    dataLength = commaPos - data;

    // 末尾
    if (dataLength > 0 && dataLength < sizeof(numberStr))
    {
        strncpy(numberStr, data, dataLength);
        numberStr[dataLength] = '\0';
    }
    else
    {
        printf("Invalid number format\n");
        return;
    }

    // 提取JSON部分
    jsonStr = commaPos + 1;

    // 将数字字符串转换为整数
    caseNumber = atoi(numberStr);

    // 解析JSON
    jsonData = cJSON_Parse(jsonStr);
    if (jsonData == NULL)
    {
        const char *errorPtr = cJSON_GetErrorPtr();
        if (errorPtr != NULL)
        {
            printf("JSON parse error: %s\n", errorPtr);
        }
        return;
    }
```

**步骤说明**：
1. **查找分隔符**：使用 `strchr()` 查找第一个逗号位置
2. **提取命令编号**：从字符串开头到逗号之间的数字（限制为1位）
3. **提取JSON字符串**：逗号后的所有内容
4. **转换命令编号**：使用 `atoi()` 将字符串转换为整数
5. **解析JSON**：使用 `cJSON_Parse()` 解析JSON字符串
6. **错误处理**：如果格式错误或JSON解析失败，打印错误信息并返回

#### 阶段2：初始化公共数据

```674:678:HARDWARE/Live_data_r.c
    DS3231_GetTime(&currentTime1);
    messageId = (long long)DS3231_To_MillisTimestamp(&currentTime1);

    /* 转成指针数组 items[] */
    item_count = BuildItemsFromUuidList1(items, 500);
```

**步骤说明**：
1. **获取当前时间**：从DS3231实时时钟芯片读取当前时间
2. **生成消息ID**：将当前时间转换为毫秒时间戳作为消息ID
3. **构建UUID列表**：调用 `BuildItemsFromUuidList1()` 构建UUID指针数组（用于某些命令处理）

#### 阶段3：命令分发处理（Switch-Case）

```681:757:HARDWARE/Live_data_r.c
    // 使用switch-case处理不同的情况
    switch (caseNumber)
    {
    case 1: // RS485透传
        printf("Processing case 1\n");
        ret = parse_rs485_json(jsonData); // requestTimestamp_1
        if (ret)
            BeepNum = 2;
        else
            BeepNum = 1;

        // 转换数据
        dataLen = hexStringToBytes(data_1, buffer_data, sizeof(buffer_data));
        // 发送数据
        RS485_SendData(buffer_data, dataLen);
        First_Reply(messageId, requestId_1, requestTimestamp_1);
        break;

    case 4: // 提取黑名单
        printf("Processing case 4\n");
        ret = parse_tiqublack_json(jsonData); // requestId_3  requestTimestamp_3
        if (ret)
            BeepNum = 2;
        else
            BeepNum = 1;

        /* 发送 JSON */
        Four_Reply(messageId, requestId_3, requestTimestamp_3);
        break;

    case 5: // 增加黑名单
        printf("Processing case 5\n");
        ret = parse_addblack_json(jsonData);
        if (ret)
            BeepNum = 2;
        else
            BeepNum = 1;

        Fifth_Reply(messageId, requestId_4, requestTimestamp_4b);
        loadDataFromEEPROM(); // 读取黑名单
        break;

    case 6: // 减少黑名单
        printf("Processing case 6\n");
        ret = parse_delblack_json(jsonData);
        if (ret)
            BeepNum = 2;
        else
            BeepNum = 1;

        Sixth_Reply(messageId, requestId_5, requestTimestamp_5b);
        break;

    case 7: // 更新公钥
        printf("Processing case 7\n");
        ret = parse_upgongyao_json(jsonData);
        if (ret)
            BeepNum = 2;
        else
            BeepNum = 1;

        Seventh_Reply(messageId, requestId_6, requestTimestamp_6b);
        break;

    case 8: // 在线更新时间
        printf("Processing case 8\n");
        ret = parse_uptime_json(jsonData);
        if (ret)
            BeepNum = 2;
        else
            BeepNum = 1;

        break;

    default:
        printf("Unknown case number: %d\n", caseNumber);
        break;
    }
    // 释放JSON对象
    cJSON_Delete(jsonData);
}
```

---

## 📊 支持的命令类型

| Case | 命令名称 | 处理函数 | 回复函数 | 功能描述 |
|------|---------|---------|---------|---------|
| **1** | RS485透传 | `parse_rs485_json()` | `First_Reply()` | 解析云平台下发的RS485指令，转换为HEX格式后通过RS485串口发送给梯控板 |
| **4** | 提取黑名单 | `parse_tiqublack_json()` | `Four_Reply()` | 从云平台提取黑名单数据，同步到本地设备 |
| **5** | 增加黑名单 | `parse_addblack_json()` | `Fifth_Reply()` | 添加新的黑名单条目到EEPROM，并更新内存 |
| **6** | 减少黑名单 | `parse_delblack_json()` | `Sixth_Reply()` | 从EEPROM删除指定的黑名单条目 |
| **7** | 更新公钥 | `parse_upgongyao_json()` | `Seventh_Reply()` | 更新设备的公钥信息 |
| **8** | 在线更新时间 | `parse_uptime_json()` | 无回复 | 根据云平台时间更新DS3231实时时钟 |

---

## 🔄 处理流程详解

### Case 1: RS485透传

**功能**：将云平台下发的梯控指令转发给RS485设备（梯控板）

**处理步骤**：
1. 调用 `parse_rs485_json()` 解析JSON，提取：
   - 波特率 (`baud_rate_data`)
   - 数据长度 (`data_length_1`)
   - 起始位/停止位长度
   - 校验方式 (`verify_1`)
   - HEX格式的数据 (`data_1`)
   - 设备密钥、产品密钥等认证信息
   - 请求ID (`requestId_1`) 和时间戳 (`requestTimestamp_1`)
2. 根据解析结果设置蜂鸣器提示（成功=2声，失败=1声）
3. 将HEX字符串转换为字节数组 (`hexStringToBytes()`)
4. 通过RS485串口发送数据 (`RS485_SendData()`)
5. 构造并发送回复消息 (`First_Reply()`)

**数据流向**：
```
云平台 → 4G模块 → USART3 → parseSerialData → parse_rs485_json → RS485串口 → 梯控板
```

### Case 4: 提取黑名单

**功能**：从云平台拉取黑名单数据

**处理步骤**：
1. 调用 `parse_tiqublack_json()` 解析JSON，提取：
   - 设备密钥、产品密钥
   - 请求ID (`requestId_3`) 和时间戳 (`requestTimestamp_3`)
2. 设置蜂鸣器提示
3. 构造并发送回复消息 (`Four_Reply()`)，回复中包含黑名单数据

### Case 5: 增加黑名单

**功能**：添加新的黑名单条目

**处理步骤**：
1. 调用 `parse_addblack_json()` 解析JSON，提取：
   - 设备密钥、产品密钥
   - 请求ID (`requestId_4`) 和时间戳 (`requestTimestamp_4b`)
   - 二维码ID数组 (`qrCodeIds_4[]`)
2. 验证签名和权限
3. 将黑名单数据写入EEPROM
4. 更新内存中的黑名单
5. 设置蜂鸣器提示
6. 构造并发送回复消息 (`Fifth_Reply()`)
7. **重新加载黑名单**：调用 `loadDataFromEEPROM()` 确保内存同步

### Case 6: 减少黑名单

**功能**：删除指定的黑名单条目

**处理步骤**：
1. 调用 `parse_delblack_json()` 解析JSON，提取：
   - 设备密钥、产品密钥
   - 请求ID (`requestId_5`) 和时间戳 (`requestTimestamp_5b`)
   - 要删除的二维码ID数组
2. 验证签名和权限
3. 从EEPROM删除指定条目
4. 更新内存中的黑名单
5. 设置蜂鸣器提示
6. 构造并发送回复消息 (`Sixth_Reply()`)

### Case 7: 更新公钥

**功能**：更新设备的公钥信息

**处理步骤**：
1. 调用 `parse_upgongyao_json()` 解析JSON，提取：
   - 设备密钥、产品密钥
   - 请求ID (`requestId_6`) 和时间戳 (`requestTimestamp_6b`)
   - 新的公钥数据
2. 验证签名和权限
3. 更新EEPROM中的公钥
4. 设置蜂鸣器提示
5. 构造并发送回复消息 (`Seventh_Reply()`)

### Case 8: 在线更新时间

**功能**：根据云平台时间同步DS3231实时时钟

**处理步骤**：
1. 调用 `parse_uptime_json()` 解析JSON，提取时间信息
2. 更新DS3231实时时钟芯片的时间
3. 设置蜂鸣器提示
4. **注意**：此命令不发送回复消息

---

## 🎯 关键特性

### 1. 错误处理机制

- **格式验证**：检查逗号分隔符是否存在
- **长度限制**：命令编号限制为1位数字（0-9）
- **JSON解析**：使用cJSON库解析，失败时打印错误信息
- **返回值检查**：每个子函数返回成功/失败状态，用于设置蜂鸣器提示

### 2. 蜂鸣器反馈

所有命令处理完成后，根据执行结果设置蜂鸣器：
- **成功**：`BeepNum = 2`（2声提示）
- **失败**：`BeepNum = 1`（1声提示）

### 3. 时间戳管理

- 使用DS3231实时时钟获取当前时间
- 将时间转换为毫秒时间戳作为消息ID (`messageId`)
- 每个命令的回复消息都包含此消息ID

### 4. 内存管理

- 使用cJSON库解析JSON，需要手动释放：`cJSON_Delete(jsonData)`
- 黑名单操作后调用 `loadDataFromEEPROM()` 同步内存

---

## 📝 调用关系图

```
G4GProcess() [usart3.c]
    ↓
parseSerialData() [Live_data_r.c]
    ↓
    ├─→ parse_rs485_json() → RS485_SendData() → First_Reply()
    ├─→ parse_tiqublack_json() → Four_Reply()
    ├─→ parse_addblack_json() → loadDataFromEEPROM() → Fifth_Reply()
    ├─→ parse_delblack_json() → Sixth_Reply()
    ├─→ parse_upgongyao_json() → Seventh_Reply()
    └─→ parse_uptime_json() → DS3231_SetTime()
```

---

## ⚠️ 注意事项

### 1. 数据格式限制

- **命令编号**：仅支持1位数字（0-9），不支持多位数
- **JSON格式**：必须是有效的JSON字符串
- **缓冲区大小**：`numberStr[2]` 限制了命令编号的长度

### 2. 错误处理

- 格式错误时直接返回，不发送任何回复
- JSON解析失败时打印错误但不发送错误回复给云平台
- 子函数返回值的处理可能不够完善（某些case未检查返回值）

### 3. 线程安全

- 函数在主循环中调用，非线程安全
- 如果中断中调用可能存在问题
- 全局变量 `caseNumber`、`messageId` 等可能被并发访问

### 4. 资源管理

- JSON对象在函数结束时统一释放（正确）
- 但某些子函数可能分配了内存未释放（需要检查子函数实现）

---

## 🔧 改进建议

### 1. 支持多位数命令编号

```c
// 当前实现
char numberStr[2] = {0}; // 只支持1位

// 建议改进
char numberStr[16] = {0}; // 支持更多位数
```

### 2. 增强错误处理

- JSON解析失败时发送错误回复给云平台
- 添加更详细的错误日志
- 统一错误码定义

### 3. 添加超时机制

- 某些操作（如RS485通信）可能需要超时保护
- 避免长时间阻塞主循环

### 4. 代码优化

- 提取公共的错误处理逻辑
- 统一回复消息的构造方式
- 减少代码重复

---

## 📚 相关文件

- **函数定义**：`HARDWARE/Live_data_r.c`
- **函数声明**：`HARDWARE/Live_data_r.h`
- **调用位置**：`SYSTEM/usart/usart3.c` (G4GProcess函数)
- **子函数实现**：`HARDWARE/Live_data_r.c`
- **回复函数**：`HARDWARE/Live_data_r.c` (First_Reply, Four_Reply等)

---

**最后更新**：2025-01-15
