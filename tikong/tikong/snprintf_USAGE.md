# snprintf 函数用法详解

## 📋 函数概述

**函数原型**：
```c
int snprintf(char *str, size_t size, const char *format, ...);
```

**头文件**：`<stdio.h>`

**功能**：将格式化字符串写入到指定缓冲区，**最多写入 `size-1` 个字符**，并在末尾自动添加 `'\0'` 终止符。

**返回值**：
- **成功**：返回**应该写入的字符数**（不包括终止符），即使被截断也返回完整长度
- **失败**：返回负值

---

## 🔑 核心特性

### 1. 安全性优势

`snprintf` 相比 `sprintf` 的最大优势是**缓冲区溢出保护**：

| 函数 | 安全性 | 缓冲区保护 |
|------|--------|-----------|
| `sprintf()` | ❌ 不安全 | 无长度限制，可能溢出 |
| `snprintf()` | ✅ 安全 | 有长度限制，防止溢出 |

### 2. 自动截断机制

- 如果格式化后的字符串长度 **≥ size**：只写入前 `size-1` 个字符，末尾添加 `'\0'`
- 如果格式化后的字符串长度 **< size**：完整写入，末尾添加 `'\0'`

**重要**：返回值始终是**应该写入的完整长度**，即使被截断也是如此！

---

## 📝 基本用法

### 语法格式

```c
int snprintf(char *str,        // 目标缓冲区
             size_t size,      // 缓冲区大小（包括 '\0'）
             const char *format, // 格式化字符串
             ...);             // 可变参数列表
```

### 简单示例

```c
char buffer[100];
int num = 42;
char name[] = "STM32";

int len = snprintf(buffer, sizeof(buffer), "Number: %d, Name: %s", num, name);
// buffer = "Number: 42, Name: STM32"
// len = 26 (实际写入的字符数，不包括 '\0')
```

---

## 💡 实际代码示例

### 示例1：构建JSON字符串（来自 uart4.c）

```221:239:SYSTEM/usart/uart4.c
				snprintf(jsonStr, sizeof(jsonStr),
						 "1,{"
						 "\"messageId\": %lld,"
						 "\"body\": [{" 
						 "\"key\": \"qr_code_report\","
						 "\"ts\": %lld,"
						 "\"info\": [{"
						 "\"key\": \"qr_code_data\","
						 "\"value\": \"%s\""
						 "}, {"
						 "\"key\": \"qr_code_time\","
						 "\"value\": %lld"
						 "}, {"
						 "\"key\": \"handle_result\","
						 "\"value\": 0"
						 "}]"
						 "}]"
						 "}",
						 messageId111, ts1, received_data, qr_code_time);
```

**解析**：
- **目标缓冲区**：`jsonStr`（需要提前定义，如 `char jsonStr[2048];`）
- **缓冲区大小**：`sizeof(jsonStr)` - 自动计算数组大小
- **格式化字符串**：多行字符串拼接，包含：
  - `%lld` - 长整型（64位）
  - `%s` - 字符串
- **参数**：`messageId111`, `ts1`, `received_data`, `qr_code_time`

**生成的JSON示例**：
```json
1,{"messageId": 1755139316330,"body": [{"key": "qr_code_report","ts": 1755139316330,"info": [{"key": "qr_code_data","value": "ABC123"}, {"key": "qr_code_time","value": 1755139316330}, {"key": "handle_result","value": 0}]}]}
```

### 示例2：格式化数字和字符串（来自 cmd.c）

```48:66:SYSTEM/utils/cmd.c
			snprintf(jsonStr, sizeof(jsonStr),
					 "1,{"
					 "\"messageId\": %lld,"
					 "\"body\": [{"
					 "\"key\": \"qr_code_report\","
					 "\"ts\": %lld,"
					 "\"info\": [{"
					 "\"key\": \"qr_code_data\","
					 "\"value\": \"%s\""
					 "}, {"
					 "\"key\": \"qr_code_time\","
					 "\"value\": %lld"
					 "}, {"
					 "\"key\": \"handle_result\","
					 "\"value\": 1"
					 "}]"
					 "}]"
					 "}",
					 messageId111, ts1, received_data, qr_code_time);
```

---

## 🎯 常用格式化说明符

| 说明符 | 类型 | 示例 | 输出 |
|--------|------|------|------|
| `%d` | 整数（int） | `snprintf(buf, 100, "%d", 42)` | `"42"` |
| `%lld` | 长整型（long long） | `snprintf(buf, 100, "%lld", 1234567890LL)` | `"1234567890"` |
| `%u` | 无符号整数 | `snprintf(buf, 100, "%u", 42)` | `"42"` |
| `%x` / `%X` | 十六进制（小写/大写） | `snprintf(buf, 100, "%x", 255)` | `"ff"` / `"FF"` |
| `%02X` | 十六进制，2位，前导0 | `snprintf(buf, 100, "%02X", 15)` | `"0F"` |
| `%s` | 字符串 | `snprintf(buf, 100, "%s", "Hello")` | `"Hello"` |
| `%c` | 字符 | `snprintf(buf, 100, "%c", 'A')` | `"A"` |
| `%f` | 浮点数 | `snprintf(buf, 100, "%f", 3.14)` | `"3.140000"` |
| `%.2f` | 浮点数，2位小数 | `snprintf(buf, 100, "%.2f", 3.14159)` | `"3.14"` |
| `%%` | 百分号 | `snprintf(buf, 100, "%%")` | `"%"` |

---

## ⚠️ 重要注意事项

### 1. 缓冲区大小计算

```c
char buffer[100];
snprintf(buffer, sizeof(buffer), "...");  // ✅ 正确：自动计算大小

char *buffer = malloc(100);
snprintf(buffer, 100, "...");  // ✅ 正确：手动指定大小

char buffer[100];
snprintf(buffer, 100, "...");  // ✅ 正确：手动指定大小（与 sizeof 相同）
```

**错误示例**：
```c
char buffer[100];
snprintf(buffer, sizeof(buffer) + 10, "...");  // ❌ 错误：可能溢出
```

### 2. 返回值检查

```c
char buffer[100];
int len = snprintf(buffer, sizeof(buffer), "Very long string...");

if (len >= sizeof(buffer)) {
    // 字符串被截断了！
    printf("Warning: String was truncated. Required: %d bytes\n", len);
} else {
    // 字符串完整写入
    printf("Success: Wrote %d bytes\n", len);
}
```

### 3. 多行字符串拼接

C语言支持字符串字面量自动拼接：

```c
snprintf(buffer, sizeof(buffer),
    "Line 1: %d\n"
    "Line 2: %s\n"
    "Line 3: %lld",
    num, str, timestamp);
```

等价于：
```c
snprintf(buffer, sizeof(buffer), "Line 1: %d\nLine 2: %s\nLine 3: %lld", 
         num, str, timestamp);
```

### 4. 转义字符处理

在格式化字符串中使用特殊字符：

```c
// JSON字符串中的引号需要转义
snprintf(json, sizeof(json), "{\"key\": \"%s\"}", value);
// 输出: {"key": "value"}

// 换行符
snprintf(msg, sizeof(msg), "Line 1\nLine 2");
// 输出: Line 1
//      Line 2
```

---

## 🔄 snprintf vs sprintf 对比

### sprintf（不安全）

```c
char buffer[10];
sprintf(buffer, "This is a very long string");  // ❌ 缓冲区溢出！
// buffer只有10字节，但写入了26字节
```

### snprintf（安全）

```c
char buffer[10];
int len = snprintf(buffer, sizeof(buffer), "This is a very long string");
// ✅ 安全：只写入9个字符 + '\0'
// buffer = "This is a"
// len = 26 (应该写入的长度，即使被截断)
```

---

## 📊 实际应用场景

### 场景1：构建JSON消息

```c
char jsonStr[2048];
long long messageId = 1755139316330;
long long timestamp = 1755139316330;
char qrData[] = "ABC123";

snprintf(jsonStr, sizeof(jsonStr),
    "1,{\"messageId\": %lld, \"data\": \"%s\", \"ts\": %lld}",
    messageId, qrData, timestamp);

// 结果: 1,{"messageId": 1755139316330, "data": "ABC123", "ts": 1755139316330}
```

### 场景2：格式化调试信息

```c
char debugMsg[256];
int errorCode = 404;
const char *fileName = "config.json";

snprintf(debugMsg, sizeof(debugMsg),
    "[ERROR] Code: %d, File: %s, Line: %d",
    errorCode, fileName, __LINE__);

printf("%s\n", debugMsg);
// 输出: [ERROR] Code: 404, File: config.json, Line: 42
```

### 场景3：构建文件路径

```c
char filePath[256];
int fileIndex = 5;

snprintf(filePath, sizeof(filePath), "/data/file_%03d.dat", fileIndex);
// 结果: /data/file_005.dat
```

### 场景4：十六进制转字符串

```c
uint8_t data[] = {0x12, 0x34, 0xAB, 0xCD};
char hexStr[32];
int pos = 0;

for (int i = 0; i < 4; i++) {
    pos += snprintf(hexStr + pos, sizeof(hexStr) - pos, "%02X", data[i]);
}
// 结果: hexStr = "1234ABCD"
```

---

## 🐛 常见错误

### 错误1：缓冲区大小错误

```c
char buffer[100];
snprintf(buffer, 100, "...");  // ✅ 正确
snprintf(buffer, sizeof(buffer), "...");  // ✅ 更推荐
snprintf(buffer, 200, "...");  // ❌ 错误：缓冲区只有100字节
```

### 错误2：忘记检查返回值

```c
char buffer[100];
snprintf(buffer, sizeof(buffer), very_long_string);
// ❌ 没有检查是否被截断
```

**正确做法**：
```c
char buffer[100];
int len = snprintf(buffer, sizeof(buffer), very_long_string);
if (len >= sizeof(buffer)) {
    // 处理截断情况
}
```

### 错误3：使用 sprintf 替代 snprintf

```c
char buffer[100];
sprintf(buffer, user_input);  // ❌ 危险：如果user_input很长会溢出
```

**正确做法**：
```c
char buffer[100];
snprintf(buffer, sizeof(buffer), "%s", user_input);  // ✅ 安全
```

---

## ✅ 最佳实践

### 1. 总是使用 sizeof() 计算缓冲区大小

```c
char buffer[256];
snprintf(buffer, sizeof(buffer), "...");  // ✅ 推荐
```

### 2. 检查返回值

```c
int len = snprintf(buffer, sizeof(buffer), "...");
if (len < 0) {
    // 格式化错误
} else if (len >= sizeof(buffer)) {
    // 被截断，需要更大的缓冲区
}
```

### 3. 使用足够大的缓冲区

```c
// 估算JSON长度：基础结构 + 数据长度
char jsonStr[2048];  // 足够大，避免截断
snprintf(jsonStr, sizeof(jsonStr), "...");
```

### 4. 避免在循环中重复分配

```c
// ✅ 好：在循环外分配
char buffer[256];
for (int i = 0; i < 100; i++) {
    snprintf(buffer, sizeof(buffer), "Item %d", i);
    // 使用 buffer
}

// ❌ 差：每次循环都分配
for (int i = 0; i < 100; i++) {
    char buffer[256];  // 栈上分配，效率低
    snprintf(buffer, sizeof(buffer), "Item %d", i);
}
```

---

## 📚 相关函数对比

| 函数 | 安全性 | 长度限制 | 用途 |
|------|--------|---------|------|
| `sprintf()` | ❌ 不安全 | 无 | 格式化字符串（已废弃） |
| `snprintf()` | ✅ 安全 | 有 | **推荐使用** |
| `vsnprintf()` | ✅ 安全 | 有 | 可变参数版本 |
| `asprintf()` | ✅ 安全 | 自动分配 | 动态分配内存（GNU扩展） |

---

## 🔗 相关代码文件

- **使用示例**：
  - `SYSTEM/usart/uart4.c` (第221行, 260行)
  - `SYSTEM/utils/cmd.c` (第48行, 119行, 285行, 356行, 456行, 517行)
  - `SYSTEM/usart/usart2.c` (第220行，已注释)

- **对比参考**：
  - `HARDWARE/Live_data.c` - 使用 `sprintf()`（可考虑改为 `snprintf()`）

---

**最后更新**：2025-01-15
