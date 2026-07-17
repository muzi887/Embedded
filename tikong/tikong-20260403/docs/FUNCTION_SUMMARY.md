# 项目功能概要报告

## 项目基本信息

- **项目名称**: 梯控系统控制器
- **硬件平台**: STM32F407VET6 (Cortex-M4, 512KB Flash, 192KB RAM)
- **开发环境**: Keil MDK-ARM
- **编译器**: ARM Compiler 5
- **项目类型**: 嵌入式梯控访问控制系统

---

## 系统架构概述

### 架构层次

```
┌─────────────────────────────────────┐
│        应用层 (USER/)                │
│  - main.c: 主程序入口                │
│  - 命令处理流程                      │
├─────────────────────────────────────┤
│        业务逻辑层 (SYSTEM/utils/)    │
│  - cmd.c: 命令处理函数               │
├─────────────────────────────────────┤
│        硬件抽象层 (HARDWARE/)        │
│  - 数据处理、黑名单、加密等          │
├─────────────────────────────────────┤
│        驱动层 (SYSTEM/)              │
│  - 串口驱动、延时、系统配置          │
├─────────────────────────────────────┤
│        标准库层 (FWLIB/)             │
│  - STM32标准外设库                   │
├─────────────────────────────────────┤
│        核心层 (CORE/)                │
│  - ARM Cortex-M4核心文件             │
└─────────────────────────────────────┘
```

---

## 核心功能模块

### 1. 串口通信模块 (SYSTEM/usart/)

#### 1.1 USART1 - PC调试串口
- **功能**: 调试信息输出、PC通信
- **波特率**: 115200
- **引脚**: PA9(TX), PA10(RX)
- **特性**: 
  - DMA接收（DMA2 Stream2）
  - 空闲中断检测帧结束
  - `PCProcess()`: 处理接收数据并打印

#### 1.2 USART2 - RS485梯控板通信
- **功能**: 与梯控板通信，发送控制指令
- **波特率**: 9600
- **引脚**: PA2(TX), PA3(RX), PA1(收发控制)
- **特性**:
  - DMA接收（DMA1 Stream5）
  - RS485收发控制
  - `Rs485Process()`: 处理RS485回执（当前注释）

#### 1.3 USART3 - 4G模块通信
- **功能**: 与云平台通信，接收指令和上报数据
- **波特率**: 115200
- **引脚**: PB10(TX), PB11(RX)
- **特性**:
  - DMA接收（DMA1 Stream1）
  - 接收云平台下发的JSON指令
  - `G4GProcess()`: 处理4G数据，调用`parseSerialData()`解析
  - `USART3_SendData()`: 发送JSON数据到云平台

#### 1.4 UART4 - 二维码扫描模块
- **功能**: 接收二维码扫描数据
- **波特率**: 9600
- **引脚**: PC10(TX), PC11(RX)
- **特性**:
  - DMA接收（DMA1 Stream2）
  - `QRProcess()`: 二维码处理主函数
  - 数据提取和格式验证
  - 命令分发到DoCmd1-6

#### 1.5 UART5 - 备用串口
- **功能**: 预留接口（当前未启用）
- **波特率**: 19200（注释状态）

---

### 2. 命令处理模块 (SYSTEM/utils/cmd.c)

#### 2.1 DoCmd1 - 新增黑名单
**功能流程**:
1. 从`valid_data`构建待签名JSON（`BuildCvjJsonFromValidData12`）
2. SHA1哈希计算
3. 签名验证（`VerifySignature`）
4. 执行：调用`AddBlacklistIds_FromPacket()`添加黑名单ID
5. 上报成功事件到云平台（USART3）

**返回码**:
- `1`: 成功
- `-1`: 验签失败
- `-2`: 构建JSON失败

#### 2.2 DoCmd2 - 删除黑名单
**功能流程**:
1. 构建待签名JSON（同DoCmd1）
2. SHA1哈希计算
3. 签名验证
4. 执行：调用`DelBlacklistIds_FromPacket()`删除黑名单ID
5. 上报成功事件

**返回码**: 同DoCmd1

#### 2.3 DoCmd3 - 梯控权限访问
**功能流程**:
1. 从`valid_data`提取ID
2. **黑名单验证**: 检查ID是否在黑名单中
3. 构建加签数据（`BuildCvjJsonFromValidData3`）
4. SHA1哈希计算
5. 签名验证
6. **时间有效性验证**: `CheckValidPeriod_WithNow()`
7. 提取22字符HEX字段（楼层权限信息）
8. 字节反转处理
9. 转换为7字节楼层数组
10. **构建梯控指令帧**:
    - 单层：`build_frame_A2()`（直达指令）
    - 多层：`build_frame_A1()`（开放权限指令）
11. 添加校验和和结束符（0xFA）
12. 通过RS485发送指令到梯控板
13. 上报成功事件

**返回码**:
- `1`: 成功
- `-1`: 构建JSON失败
- `-2`: 帧缓冲不足
- `-3`: HEX转换失败
- `-4`: HEX字段提取失败
- `-5`: 时间验证失败
- `-6`: 验签失败
- `-7`: ID在黑名单中

#### 2.4 DoCmd4 - 更新公钥
**功能流程**:
1. 解析参数：`sscanf(valid_data, "4,id,user,pass,pk_new")`
2. 验证账号密码：`VerifyDeviceCredentials(user, pass)`
3. 执行：`SetDevicePublicKey(pk_new)`更新设备公钥
4. 上报成功事件

**返回码**:
- `1`: 成功
- `-1`: 账号密码验证失败
- `-2`: 参数解析失败

#### 2.5 DoCmd5 - 更新时间
**功能流程**:
1. 解析参数：`sscanf(valid_data, "5,username,password,timestamp")`
2. 验证账号密码
3. 解析时间戳（格式：YYYYMMDDHHmmss）
4. 计算星期几
5. 执行：`DS3231_SetTime()`设置RTC时间
6. 上报成功事件

**返回码**:
- `1`: 成功
- `-1`: 账号密码验证失败
- `-2`: 参数解析失败

#### 2.6 DoCmd6 - 清空黑名单
**功能流程**:
1. 解析参数：`sscanf(valid_data, "6,user,pass")`
2. 验证账号密码
3. 执行：`clearPublicKeySpace()`清空所有黑名单
4. 上报成功事件

**返回码**:
- `1`: 成功
- `-1`: 账号密码验证失败或参数解析失败

---

### 3. 数据处理模块 (HARDWARE/data_handler.c)

#### 3.1 数据提取和验证
- **`ExtractValidData_Bytes()`**: 从原始数据中提取有效数据（去除表头表尾乱码）
- **`ExtractIdFromValidData()`**: 从valid_data中提取ID字符串
- **`IsCvjFormat()`**: 判断是否为CVJ格式的新指令
- **`IsCmd()`**: 识别命令类型（返回'1'-'6'）

#### 3.2 JSON构建函数
- **`BuildCvjJsonFromValidData12()`**: 构建命令1/2的待签名JSON
- **`BuildCvjJsonFromValidData3()`**: 构建命令3的待签名JSON
- **`BuildCvjJsonFromValidData4/5/6()`**: 构建其他命令的JSON

#### 3.3 签名验证
- **`VerifySignature()`**: 验证SHA1签名是否匹配
- **`CheckValidPeriod_WithNow()`**: 检查时间有效性（命令3使用）

#### 3.4 梯控指令构建
- **`hex14_to_bytes7()`**: 将14字符HEX转换为7字节数组
- **`build_frame_A1()`**: 构建A1指令帧（多层权限）
- **`build_frame_A2()`**: 构建A2指令帧（单层直达）
- **`sum_checksum()`**: 计算校验和
- **`IsSingleFloor()`**: 判断是否为单层

#### 3.5 设备凭证管理
- **`SetDeviceCredentials()`**: 设置设备账号密码
- **`VerifyDeviceCredentials()`**: 验证账号密码
- **`SetDevicePublicKey()`**: 设置设备公钥
- **`GetDevicePublicKey()`**: 获取设备公钥

---

### 4. 黑名单管理模块 (HARDWARE/blackList.c)

#### 4.1 数据结构
```c
typedef struct {
    uint8_t data[11];   // 数据（黑名单ID）
    uint8_t isEmpty;    // 空标志（1=空，0=非空）
} DataEntry;

DataEntry dataArray[MAX_ARRAY_SIZE]; // MAX_ARRAY_SIZE = 200
```

#### 4.2 核心功能
- **`loadDataFromEEPROM()`**: 从EEPROM加载黑名单到内存
- **`addDataToBlacklist()`**: 添加数据到黑名单
- **`deleteDataFromBlacklist()`**: 从黑名单删除数据
- **`clearPublicKeySpace()`**: 清空所有黑名单
- **`AddBlacklistIds_FromPacket()`**: 从数据包中提取并添加黑名单ID
- **`DelBlacklistIds_FromPacket()`**: 从数据包中提取并删除黑名单ID

#### 4.3 存储
- 黑名单存储在EEPROM中（地址0-2200）
- 每个条目11字节
- 最大200条记录

---

### 5. 实时时钟模块 (HARDWARE/DS3231.c)

#### 5.1 功能
- **`DS3231_GetTime()`**: 读取当前时间
- **`DS3231_SetTime()`**: 设置时间
- **`DS3231_To_MillisTimestamp()`**: 转换为毫秒时间戳

#### 5.2 应用
- 为所有上报事件提供时间戳
- 验证二维码的有效期（命令3）
- 系统时间管理

---

### 6. 加密模块 (HARDWARE/sha1.c)

#### 6.1 功能
- **`sha1_hash()`**: SHA1哈希计算
- 用于数据签名验证

---

### 7. 存储模块 (HARDWARE/eeprom.c)

#### 7.1 功能
- EEPROM读写操作
- 存储黑名单数据
- 存储设备配置信息（公钥、账号密码等）

---

### 8. JSON处理模块 (HARDWARE/cJSON.c)

#### 8.1 功能
- JSON解析和构建
- 处理云平台下发的JSON指令
- 构建上报事件的JSON数据

---

## 主程序流程 (USER/main.c)

### 初始化流程
1. 配置中断优先级（NVIC_PriorityGroup_2）
2. 初始化延时函数
3. 初始化串口：
   - USART1: PC调试串口（115200）
   - USART2: RS485（9600）
   - USART3: 4G模块（115200）
   - UART4: 二维码模块（9600）
4. 初始化蜂鸣器
5. 初始化I2C（用于RTC和EEPROM）
6. 初始化定时器：
   - TIM3: 500ms定时
   - TIM4: 10ms定时
7. 从EEPROM读取密钥和配置
8. 加载黑名单到内存

### 主循环
```c
while (1) {
    PCProcess();      // 处理PC调试串口
    // Rs485Process(); // RS485处理（当前注释）
    G4GProcess();     // 处理4G模块数据
    QRProcess();      // 处理二维码扫描
}
```

---

## 数据流向

### 二维码处理流程
```
二维码扫描 → UART4接收 → ExtractValidData_Bytes() 
→ IsCvjFormat()验证 → IsCmd()识别命令 
→ DoCmd1-6处理 → 执行操作 → 上报事件(USART3)
```

### 4G模块处理流程
```
4G模块接收 → USART3接收 → parseSerialData()解析 
→ 识别命令类型 → 执行相应操作 → 回执(USART3)
```

### 梯控指令流程（命令3）
```
二维码扫描 → 黑名单验证 → 时间验证 → 签名验证 
→ 提取楼层信息 → 构建指令帧 → RS485发送 → 上报事件
```

---

## 通信协议

### 二维码数据格式
- **CVJ格式**: `命令号,ID,其他字段...`
- **命令1**: `1,id,其他字段...` - 新增黑名单
- **命令2**: `2,id,其他字段...` - 删除黑名单
- **命令3**: `3,id,其他字段,validBegin,validEnd,hex22,签名` - 梯控访问
- **命令4**: `4,id,user,pass,pk_new` - 更新公钥
- **命令5**: `5,username,password,timestamp` - 更新时间
- **命令6**: `6,user,pass` - 清空黑名单

### 上报事件格式（JSON）
```json
1,{
  "messageId": 时间戳,
  "body": [{
    "key": "qr_code_report",
    "ts": 时间戳,
    "info": [{
      "key": "qr_code_data",
      "value": "原始二维码数据"
    }, {
      "key": "qr_code_time",
      "value": 时间戳
    }, {
      "key": "handle_result",
      "value": 1或0  // 1=成功，0=失败
    }]
  }]
}
```

### RS485梯控指令格式
- **A1指令**: 多层权限开放（26/27字节）
- **A2指令**: 单层直达（26/27字节）
- 格式：帧头 + 数据 + 校验和 + 0xFA

---

## 安全机制

### 1. 签名验证
- 所有命令（1-3）都需要SHA1签名验证
- 使用设备公钥验证签名有效性

### 2. 账号密码验证
- 命令4/5/6需要验证管理员账号密码
- 本地验证，不依赖网络

### 3. 时间有效性验证
- 命令3（梯控访问）验证二维码的有效期
- 检查当前时间是否在validBegin和validEnd之间

### 4. 黑名单机制
- 命令3检查ID是否在黑名单中
- 黑名单中的ID无法通过验证

---

## 资源使用情况

### 内存分配
- **Flash**: 约512KB（STM32F407VET6）
- **RAM**: 约192KB
- **栈空间**: 需要注意大数组定义位置

### 缓冲区大小
- `USART_RX_BUF`: 200字节（USART1）
- `RS485_RX_BUF`: 128字节（USART2）
- `USART3_RX_BUF`: 1024字节（USART3）
- `UART4_RX_BUF`: 由UART4_REC_LEN定义
- `received_data`: 512字节（MAX_RECEIVE_LEN）
- `dataArray`: 200条 × 11字节 = 2200字节（黑名单）

---

## 中断优先级配置

- **DMA中断**: 优先级1（DMA接收）
- **USART1中断**: 优先级2（空闲中断）
- **USART3中断**: 优先级2（空闲中断）
- **定时器中断**: TIM3/TIM4

---

## 关键全局变量

- `currentTime`: DS3231时间结构体
- `json`: 待加签JSON字符串指针
- `valid_data`: 提取的有效数据指针
- `hash_str[80]`: SHA1哈希结果
- `dataArray[]`: 黑名单数组（200条）
- `received_data[]`: 接收缓冲区（512字节）
- `g_device_user/pass/publicKey`: 设备凭证

---

## 代码统计

### 文件数量
- **C源文件**: 约50+个
- **头文件**: 约50+个
- **主要模块**:
  - 串口驱动: 5个文件
  - 硬件驱动: 15+个文件
  - 命令处理: 2个文件
  - 数据处理: 2个文件

### 代码行数（估算）
- `cmd.c`: ~555行（命令处理）
- `data_handler.c`: ~1800行（数据处理）
- `uart4.c`: ~292行（二维码处理）
- `usart3.c`: ~305行（4G处理）
- `blackList.c`: 黑名单管理
- 总计: 约10000+行代码

---

## 开发建议

### 1. 代码优化
- **提取公共函数**: DoCmd1和DoCmd2可以合并
- **统一上报函数**: 所有命令的上报代码可以提取为公共函数
- **减少重复代码**: 签名验证流程可以提取

### 2. 错误处理
- 统一错误码定义
- 增强错误日志记录
- 添加异常恢复机制

### 3. 内存管理
- 注意栈溢出风险
- 合理使用静态变量
- 避免大数组定义在栈上

### 4. 可维护性
- 添加更多注释
- 统一代码风格
- 模块化设计

---

## 测试建议

### 1. 功能测试
- 各命令功能验证
- 边界条件测试
- 异常情况处理

### 2. 性能测试
- 响应时间测试
- 内存使用监控
- 中断响应时间

### 3. 稳定性测试
- 长时间运行测试
- 异常数据测试
- 并发处理测试

---

## 版本信息

- **创建日期**: 2025-01-15
- **最后更新**: 2025-01-15
- **文档版本**: 1.0

---

## 附录

### 相关文档
- [梯控固件项目介绍.md](./梯控固件项目介绍.md): 工程总览
- [梯控固件入门规划.md](./梯控固件入门规划.md): 接手日程
- [PROJECT_STRUCTURE.md](./PROJECT_STRUCTURE.md): 项目结构说明
- [G4GProcess.md](./G4GProcess.md): 4G处理流程说明
- [main_flowchart.mmd](./main_flowchart.mmd): 主流程图
- [文件说明.md](./文件说明.md): docs 目录索引
- 工程根目录 `.cursorrules`: Cursor 开发规则

### 关键函数索引

#### 串口处理
- `PCProcess()`: PC串口处理
- `G4GProcess()`: 4G模块处理
- `QRProcess()`: 二维码处理
- `Rs485Process()`: RS485处理

#### 命令处理
- `DoCmd1()`: 新增黑名单
- `DoCmd2()`: 删除黑名单
- `DoCmd3()`: 梯控权限访问
- `DoCmd4()`: 更新公钥
- `DoCmd5()`: 更新时间
- `DoCmd6()`: 清空黑名单

#### 数据处理
- `ExtractValidData_Bytes()`: 提取有效数据
- `IsCvjFormat()`: 格式验证
- `IsCmd()`: 命令识别
- `parseSerialData()`: 串口数据解析

---

**文档结束**
