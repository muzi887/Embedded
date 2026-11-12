# setpage() 功能详细说明

## 一、功能概述

`setpage()` 是LCD显示系统中的**设置页面函数**，用于配置系统的三个关键参数：
1. **闸位最大值** (gate_maxheight)
2. **堵转电流上限** (motor_warning_curr_upper)
3. **校准模式** (自动执行/关闭校准)

该函数支持**两种工作模式**：浏览模式和编辑模式。

---

## 二、函数结构

### 2.1 函数签名
```c
void setpage(void)
```

### 2.2 静态变量
```c
static u8 Num = 0x30;        // 当前编辑的数字字符 (ASCII '0'-'9')
static u8 cnt = 0;           // 输入位置计数器
static u8 flag = 0;          // 闪烁标志 (0:显示, 1:隐藏)
static u8 row3flag = 0;      // 第三行标志 (用于校准选项)
```

### 2.3 全局变量依赖
- `setParaflag`: 模式标志 (0=浏览模式, 1=编辑模式)
- `SetPageNum`: 当前选中的设置项 (0/1/2)
- `KeyNum`: 按键值 (KEY_UP, KEY_DOWN, KEY_ENTER)
- `armReg.gate_maxheight`: 闸位最大值寄存器
- `armReg.motor_warning_curr_upper`: 堵转电流上限寄存器
- `autoFlag`: 自动校准标志

---

## 三、工作模式详解

### 3.1 模式切换流程
```
进入setpage页面
  └─> setParaflag == 0 (浏览模式)
      └─> 按KEY_ENTER
          └─> setParaflag = 1 (进入编辑模式)
              └─> 编辑完成后
                  └─> setParaflag = 0 (返回浏览模式)
```

### 3.2 浏览模式 (setParaflag == 0)

#### 功能描述
- 显示当前三个参数的数值
- 通过上下键选择要编辑的参数项
- 选中的行会高亮显示（反选）
- 按确认键进入编辑模式

#### 按键处理
```c
KEY_UP:   SetPageNum--  // 向上选择 (最小为0)
KEY_DOWN: SetPageNum++  // 向下选择 (最大为SetP_Num=2)
KEY_ENTER: setParaflag = 1  // 进入编辑模式
```

#### 显示内容
1. **第一行 (0x90)**: "闸位：XXXX mm"
   - 显示 `armReg.gate_maxheight` 的值
   - 使用 `DisplayInt()` 格式化

2. **第二行 (0x88)**: "堵转：XX.X A"
   - 显示 `armReg.motor_warning_curr_upper` 的值
   - 使用 `DisplayFloat()` 格式化

3. **第三行 (0x98)**: "校准：自动执行" 或 "校准：关闭"
   - 显示 `autoFlag` 的状态
   - 使用 `fjiaozhun()` 格式化

4. **边框和图标**: 使用 `WRGDRAM1()` 和 `WRGDRAM()` 绘制

#### 高亮显示逻辑
```c
SetPageNum == 0: 第二行高亮 (闸位)
SetPageNum == 1: 第三行高亮 (堵转)
SetPageNum == 2: 第四行高亮 (校准)
```

---

### 3.3 编辑模式 (setParaflag == 1)

#### 功能描述
- 根据 `SetPageNum` 编辑对应的参数
- 支持数字输入（闸位、堵转）或选项选择（校准）
- 输入过程中有闪烁效果提示当前位置
- 完成输入后自动保存到EEPROM

#### 按键处理

**通用按键**:
- `KEY_UP`: 数字递增 (0x30-0x39, 即'0'-'9')
- `KEY_DOWN`: 数字递减 (0x30-0x39)
- `KEY_ENTER`: 确认当前输入，移动到下一位

---

## 四、三个设置项详细说明

### 4.1 设置项0：闸位最大值 (zhaweimax)

#### 数据结构
```c
uint8_t zhaweimax[16] = {
    0xc8, 0xab, 0xbf, 0xaa, 0x3a, 0x20,  // "全开："
    0x31, 0x31, 0x32, 0x36,              // 数值部分 (4位数字)
    0x20, 0x6d, 0x6d, 0x20, 0x20, 0x20  // " mm"
};
```

#### 编辑流程
```
1. 初始化: cnt = 6 (从第6个字节开始，即数值部分)
2. Num = zhaweimax[6] (当前位的值)
3. 按键处理:
   - KEY_UP/DOWN: 修改Num (0x30-0x39)
   - KEY_ENTER: 
     ├─> zhaweimax[cnt-1] = Num  // 保存当前位
     ├─> cnt++                   // 移动到下一位
     └─> if (cnt >= 10)          // 输入完成(4位数字)
         ├─> 转换ASCII为整数: armReg.gate_maxheight = asciitoInt(&zhaweimax[6], 4)
         ├─> 保存到EEPROM: AT24CXX_WriteWord(0x0006, armReg.gate_maxheight)
         └─> setParaflag = 0     // 退出编辑模式
```

#### 显示效果
- 当前编辑位会闪烁（flag切换）
- 闪烁时显示空格(0x20)，不闪烁时显示数字

#### 数据保存
- **寄存器**: `armReg.gate_maxheight` (单位: mm)
- **EEPROM地址**: 0x0006
- **数据格式**: 4位整数，ASCII转十进制

---

### 4.2 设置项1：堵转电流上限 (duzhuan)

#### 数据结构
```c
uint8_t duzhuan[16] = {
    0xb6, 0xc2, 0xd7, 0xaa, 0x3a, 0x20,  // "堵转："
    0x30, 0x30, 0x2e, 0x30,              // 数值部分 (XX.X格式)
    0x20, 0x41, 0x20, 0x20, 0x20, 0x20  // " A"
};
```

#### 编辑流程
```
1. 初始化: cnt = 6 (从第6个字节开始)
2. Num = duzhuan[6] (当前位的值)
3. 按键处理:
   - KEY_UP/DOWN: 修改Num
   - KEY_ENTER:
     ├─> duzhuan[cnt-1] = Num
     ├─> if (cnt == 8) cnt++  // 跳过小数点位置(第8位)
     ├─> cnt++
     └─> if (cnt >= 10)       // 输入完成
         ├─> 转换: armReg.motor_warning_curr_upper = asciitoInt(&duzhuan[6], 4)
         ├─> 保存: AT24CXX_WriteWord(0x0004, armReg.motor_warning_curr_upper)
         └─> setParaflag = 0
```

#### 特殊处理
- **小数点跳过**: 当cnt==8时，自动跳过小数点位置，不编辑小数点
- **数据格式**: XX.X (2位整数 + 1位小数)

#### 数据保存
- **寄存器**: `armReg.motor_warning_curr_upper` (单位: A)
- **EEPROM地址**: 0x0004
- **数据格式**: 4位数字(包含小数点)，转换为整数存储

---

### 4.3 设置项2：校准模式 (jiaozhun)

#### 数据结构
```c
uint8_t jiaozhun[16] = {
    0xd0, 0xa3, 0xd7, 0xbc, 0x3a, 0x20,  // "校准："
    0xb9, 0xd8, 0xb1, 0xd5,              // 状态文字 (8字节)
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20  // 空格
};
```

#### 编辑流程
```
1. 初始化: cnt = 9
2. 根据autoFlag设置row3flag:
   - autoFlag == 0: row3flag = 0 (关闭)
   - autoFlag == 1: row3flag = 1 (自动执行)
3. 按键处理:
   - KEY_UP: row3flag = 1 (选择"自动执行")
   - KEY_DOWN: row3flag = 0 (选择"关闭")
   - KEY_ENTER:
     ├─> autoFlag = row3flag
     ├─> fjiaozhun(&jiaozhun[6], row3flag)  // 更新显示文字
     └─> setParaflag = 0                    // 退出编辑模式
```

#### 选项说明
- **row3flag = 0**: "关闭" (GB2312编码: 0xb9,0xd8,0xb1,0xd5)
- **row3flag = 1**: "自动执行" (GB2312编码: 0xc1,0xa2,0xbc,0xb4,0xd6,0xb4,0xd0,0xd0)

#### 显示效果
- 选择时文字会闪烁
- 确认后立即更新显示

#### 数据保存
- **寄存器**: `autoFlag` (全局变量)
- **EEPROM**: 不直接保存，可能在其他地方保存
- **数据格式**: 布尔值 (0/1)

---

## 五、显示刷新机制

### 5.1 闪烁效果实现
```c
// 在编辑模式下，当前编辑位会闪烁
if (flag == 0) {
    // 显示空格 (隐藏)
    zhaweimax[cnt] = 0x20;
} else {
    // 显示数字 (显示)
    zhaweimax[cnt] = Num;
}
flag = !flag;  // 每次刷新切换状态
```

### 5.2 刷新触发
- 每次调用 `setpage()` 时都会刷新显示
- 在主循环中通过 `LcdDisplay()` -> `ViewPages()` -> `DisplayViewPages()` 调用
- 刷新频率：500ms定时 + 按键事件触发

---

## 六、数据转换函数

### 6.1 ASCII转整数 (asciitoInt)
```c
int asciitoInt(u8 *src, u8 len)
```
- **功能**: 将ASCII字符串转换为整数
- **参数**: 
  - `src`: ASCII字符串起始地址
  - `len`: 字符串长度
- **示例**: "1126" -> 1126

### 6.2 整数显示格式化 (DisplayInt)
```c
void DisplayInt(u8 *src, u16 val, u8 style)
```
- **功能**: 将整数转换为4位ASCII字符串
- **参数**:
  - `src`: 输出缓冲区起始地址
  - `val`: 要转换的整数值
  - `style`: 样式 (0=自动补空格, 1=不补空格)

### 6.3 浮点数显示格式化 (DisplayFloat)
```c
void DisplayFloat(u8 *src, u16 val, u8 style)
```
- **功能**: 将整数转换为XX.X格式的ASCII字符串
- **参数**:
  - `src`: 输出缓冲区起始地址
  - `val`: 要转换的值 (实际存储为整数，显示时加小数点)
  - `style`: 样式

---

## 七、完整操作流程示例

### 7.1 设置闸位最大值示例
```
1. 进入设置页面 (ViewNum = 1)
   └─> setpage() 被调用
       └─> setParaflag = 0 (浏览模式)
           └─> 显示当前值，第二行高亮

2. 按KEY_ENTER进入编辑模式
   └─> setParaflag = 1
       └─> cnt = 6, Num = zhaweimax[6]

3. 编辑第一位数字
   └─> KEY_UP/DOWN: 修改Num (0-9)
   └─> KEY_ENTER: 保存第一位，cnt++

4. 编辑后续位 (重复步骤3)

5. 输入完成 (cnt >= 10)
   └─> 转换为整数
   └─> 保存到EEPROM
   └─> setParaflag = 0 (返回浏览模式)
```

### 7.2 设置校准模式示例
```
1. 进入设置页面，选择第三项 (SetPageNum = 2)
   └─> 第四行高亮显示

2. 按KEY_ENTER进入编辑模式
   └─> setParaflag = 1

3. 选择选项
   └─> KEY_UP: 选择"自动执行" (row3flag = 1)
   └─> KEY_DOWN: 选择"关闭" (row3flag = 0)

4. 按KEY_ENTER确认
   └─> autoFlag = row3flag
   └─> 更新显示文字
   └─> setParaflag = 0 (返回浏览模式)
```

---

## 八、关键代码逻辑

### 8.1 浏览模式按键处理
```c
switch (KeyNum) {
    case KEY_UP:
        if (SetPageNum > 0) SetPageNum--;  // 向上选择
        break;
    case KEY_DOWN:
        if (SetPageNum < SetP_Num) SetPageNum++;  // 向下选择
        break;
    case KEY_ENTER:
        setParaflag = 1;  // 进入编辑模式
        // 初始化编辑状态
        switch (SetPageNum) {
            case 0: cnt = 6; Num = zhaweimax[6]; break;
            case 1: cnt = 6; Num = duzhuan[6]; break;
            case 2: cnt = 9; break;
        }
        break;
}
```

### 8.2 编辑模式按键处理
```c
switch (KeyNum) {
    case KEY_UP:
        if (Num < 0x39) Num++;  // 数字递增
        if (SetPageNum == 2) row3flag = 1;  // 校准模式选择
        break;
    case KEY_DOWN:
        if (Num > 0x30) Num--;  // 数字递减
        if (SetPageNum == 2) row3flag = 0;  // 校准模式选择
        break;
    case KEY_ENTER:
        // 根据SetPageNum处理不同的保存逻辑
        // 完成后 setParaflag = 0
        break;
}
```

---

## 九、注意事项

### 9.1 数据范围限制
- **闸位最大值**: 4位数字，范围 0-9999 mm
- **堵转电流**: XX.X格式，范围 0.0-99.9 A
- **校准模式**: 二选一 (关闭/自动执行)

### 9.2 输入验证
- 数字输入范围: 0x30-0x39 (ASCII '0'-'9')
- 小数点位置固定，不可编辑
- 输入完成后自动验证和保存

### 9.3 状态保持
- 使用静态变量保持编辑状态
- 页面切换时状态会重置
- EEPROM保存确保数据持久化

### 9.4 用户体验
- 闪烁效果提示当前编辑位置
- 高亮显示当前选中的设置项
- 输入完成后自动退出编辑模式

---

## 十、函数调用关系

```
main() -> while(1)
  └─> Lcd_Process()
      └─> LcdDisplay()
          └─> ViewPages(KeyNum)
              └─> DisplayViewPages(ViewNum)
                  └─> setpage()  // ViewNum == 1时
                      ├─> LCDInit()
                      ├─> DisplayInt()
                      ├─> DisplayFloat()
                      ├─> fjiaozhun()
                      ├─> ShowQQCharH()
                      ├─> WRGDRAM1()
                      └─> WRGDRAM()
```

---

## 十一、总结

`setpage()` 函数是一个功能完整的**参数设置界面**，具有以下特点：

1. **双模式设计**: 浏览模式和编辑模式分离，用户体验良好
2. **多参数支持**: 支持3种不同类型的参数设置
3. **数据持久化**: 设置完成后自动保存到EEPROM
4. **交互友好**: 闪烁提示、高亮显示、自动退出等
5. **数据验证**: 输入范围限制和格式验证

该函数是LCD显示系统中重要的用户交互接口，用于配置系统的关键运行参数。
