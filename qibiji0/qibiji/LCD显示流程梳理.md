# LCD 12864 显示流程梳理

## 一、系统架构

LCD显示系统分为三层：
1. **硬件驱动层** (`Lcd12864.C`) - 底层SPI通信和LCD控制
2. **显示逻辑层** (`Dispaly.c`) - 页面管理和显示控制
3. **数据处理层** (`DispalyPara.c`) - 数据格式化和参数处理

---

## 二、初始化流程

### 2.1 系统启动初始化
```
main() 
  └─> Lcd_Init()  // 在main函数中调用，系统启动时执行一次
```

### 2.2 LCD初始化详细流程
```
Lcd_Init()
  ├─> Lcd_Gpio_Init()      // GPIO初始化
  │   ├─> 使能GPIOE和GPIOA时钟
  │   ├─> 配置PE3(CS), PE4(SID), PE5(SCLK)为推挽输出
  │   ├─> 配置PA0(BackLight)为推挽输出
  │   └─> 初始化引脚状态：CS=0, SID=0, SCLK=0, BackLight=1(关闭背光)
  │
  ├─> delay_ms(1500)        // 等待屏幕启动时间
  │
  ├─> CLEARGDRAMH(0x00)     // 清除显示区域(GDRAM)
  │
  └─> homepage()            // 显示首页
      ├─> LCDInit()          // LCD基础初始化
      │   ├─> WRCommandH(0x30)  // 功能指令集,8位接口
      │   ├─> WRCommandH(0x06)  // 初始设定，光标移动方向
      │   ├─> WRCommandH(0x0C)  // 显示状态开关
      │   └─> WRCommandH(0x02)  // 地址归位
      │
      ├─> DisplayFloat()     // 格式化电机电流和电压数据
      ├─> kaiduVal()         // 格式化开度值和状态
      ├─> ShowQQCharH()      // 显示汉字字符串
      ├─> WRGDRAM1()         // 画边框
      └─> WRGDRAM()          // 显示图标
```

---

## 三、主循环显示流程

### 3.1 主循环调用
```
main() -> while(1)
  └─> Lcd_Process()  // 在主循环中周期性调用
```

### 3.2 显示处理流程
```
Lcd_Process()
  ├─> BLight_Ctrl()         // 背光控制
  │   ├─> 检测按键事件(KeyNum)
  │   ├─> 有按键：打开背光，重置计数器
  │   └─> 无按键：300ms定时器计数，100次(30秒)后关闭背光
  │
  └─> LcdDisplay()          // LCD显示刷新
      ├─> 检测刷新条件：flag500ms || KeyNum
      │   └─> flag500ms: 500ms定时标志(定时刷新)
      │   └─> KeyNum: 按键事件(立即刷新)
      │
      └─> ViewPages(KeyNum)  // 页面视图处理
```

---

## 四、页面管理流程

### 4.1 页面切换逻辑
```
ViewPages(KeyNum)
  ├─> 按键处理
  │   ├─> KEY_LEFT:  ViewNum-- (上一页)
  │   ├─> KEY_RIGHT: ViewNum++ (下一页，循环)
  │   └─> KEY_ENTER: 返回首页(ViewNum=0)
  │
  └─> DisplayViewPages(ViewNum)  // 根据页面号显示对应页面
```

### 4.2 页面显示路由
```
DisplayViewPages(PageNum)
  ├─> case 0: homepage()      // 首页
  ├─> case 1: setpage()       // 设置页面
  ├─> case 2: G4gpage()       // 4G页面
  ├─> case 3: Devicepage()    // 设备页面
  └─> case 4: OperatePage()   // 操作页面
```

---

## 五、各页面显示流程

### 5.1 首页 (homepage)
```
homepage()
  ├─> LCDInit()              // LCD初始化
  ├─> DisplayFloat()         // 格式化电机电流(armReg.motor_curr)
  ├─> DisplayFloat()         // 格式化电机电压(g_adc_val[2])
  ├─> kaiduVal()             // 格式化开度值和状态
  ├─> ShowQQCharH()          // 显示"开度："字符串
  ├─> ShowQQCharH()          // 显示"电机："字符串
  ├─> ShowQQCharH()          // 显示"流量"字符串
  ├─> WRGDRAM1()             // 画边框
  └─> WRGDRAM()              // 显示图标(电池、信号等)
```

### 5.2 设置页面 (setpage)
```
setpage()
  ├─> LCDInit()              // LCD初始化
  │
  ├─> if (setParaflag)       // 设置参数模式
  │   ├─> 按键处理(KEY_UP/DOWN/ENTER)
  │   ├─> 数字输入处理
  │   ├─> DisplayInt()       // 显示闸位最大值
  │   ├─> DisplayFloat()     // 显示堵转电流
  │   └─> fjiaozhun()         // 显示校准状态
  │
  └─> else                   // 浏览模式
      ├─> DisplayInt()       // 显示闸位最大值
      ├─> DisplayFloat()     // 显示堵转电流
      ├─> fjiaozhun()         // 显示校准状态
      ├─> ShowQQCharH()       // 显示字符串
      ├─> WRGDRAM1()          // 画边框
      └─> WRGDRAM128X8()      // 高亮选中行
```

### 5.3 其他页面
- **G4gpage()**: 显示4G信号和CCID信息
- **Devicepage()**: 显示设备编号、状态、光伏电压
- **OperatePage()**: 显示上限位和下限位状态

---

## 六、底层驱动流程

### 6.1 字符显示流程
```
ShowQQCharH(addr, hanzi, count)
  ├─> for (i = 0; i < count; i++)
  │   ├─> WRCommandH(addr)    // 设定DDRAM地址
  │   ├─> WRDataH(hanzi[i*2])     // 写入高字节
  │   └─> WRDataH(hanzi[i*2+1])   // 写入低字节
  └─> addr++
```

### 6.2 命令/数据写入流程
```
WRCommandH(CMD) / WRDataH(Data)
  ├─> CS = 0 -> CS = 1       // 片选信号
  ├─> SPIWH(Data, WRS)       // SPI写入
  │   ├─> SendByteLCDH(0xf8 + (WRS<<1))  // 寄存器选择
  │   ├─> SendByteLCDH(Data & 0xf0)      // 高4位
  │   └─> SendByteLCDH((Data<<4) & 0xf0) // 低4位
  │
  └─> delay_us(100/20)       // 延时
```

### 6.3 SPI字节发送流程
```
SendByteLCDH(WLCDData)
  ├─> for (i = 0; i < 8; i++)
  │   ├─> SCLK = 0           // 时钟拉低
  │   ├─> SID = (WLCDData<<i) & 0x80 ? 1 : 0  // 数据位
  │   └─> SCLK = 1           // 时钟拉高
  └─> 完成8位数据传输
```

---

## 七、数据格式化流程

### 7.1 整数显示格式化
```
DisplayInt(src, val, style)
  ├─> 将val分解为千、百、十、个位
  ├─> 转换为ASCII码(0x30-0x39)
  └─> if (style == 0) 处理前导空格
```

### 7.2 浮点数显示格式化
```
DisplayFloat(src, val, style)
  ├─> 将val分解为整数部分和小数部分
  ├─> 转换为ASCII码
  ├─> 插入小数点(0x2e)
  └─> if (style == 0) 处理前导空格
```

### 7.3 开度值格式化
```
kaiduVal(val, src)
  ├─> sprintf() 将kaival转为字符串
  ├─> 根据长度填充到val数组
  └─> 根据电机状态(sys_moter_state)设置src
      ├─> MOTOR_UP:   "上升"
      ├─> MOTOR_DOWN: "下降"
      ├─> MOTOR_STOP: "停止"/"全开"/"全关"
      └─> MOTOR_RESET: "复位"
```

---

## 八、关键变量说明

### 8.1 页面状态变量
- `ViewNum`: 当前查看页面编号 (0-4)
- `SetPageNum`: 设置页面中的项目编号
- `setParaflag`: 设置参数标志 (0:浏览模式, 1:编辑模式)
- `PageSta`: 页面状态

### 8.2 定时标志
- `flag500ms`: 500ms定时标志，用于定时刷新显示
- `flag300ms`: 300ms定时标志，用于背光控制

### 8.3 按键变量
- `KeyNum`: 按键编号 (KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_ENTER)

---

## 九、显示刷新机制

### 9.1 刷新触发条件
1. **定时刷新**: flag500ms置位时刷新 (500ms周期)
2. **按键刷新**: KeyNum非零时立即刷新
3. **初始化刷新**: 页面切换时调用页面函数刷新

### 9.2 刷新流程
```
刷新触发
  └─> LcdDisplay()
      └─> ViewPages(KeyNum)
          └─> DisplayViewPages(ViewNum)
              └─> 调用对应页面函数
                  └─> LCDInit() + 显示内容
```

---

## 十、背光控制机制

### 10.1 背光控制逻辑
- **有按键**: 立即打开背光，重置计数器
- **无按键**: 
  - 每300ms计数一次
  - 计数达到100次(30秒)后关闭背光
  - 60秒无操作返回到首页并关闭背光

### 10.2 背光控制流程
```
BLight_Ctrl()
  ├─> if (KeyNum) 
  │   └─> BackLight = 1, cnt = 0
  │
  └─> else
      └─> if (flag300ms & 0x01)
          └─> cnt++
              └─> if (cnt >= 100)
                  └─> BackLight = 0
```

---

## 十一、总结

LCD显示系统采用**分层架构**和**事件驱动**的设计：

1. **初始化阶段**: 系统启动时完成GPIO配置和LCD初始化，显示首页
2. **运行阶段**: 主循环中周期性调用Lcd_Process()处理显示和背光
3. **刷新机制**: 通过定时器和按键事件触发显示刷新
4. **页面管理**: 通过ViewNum管理多个页面，支持左右切换
5. **数据格式化**: 将原始数据转换为LCD可显示的格式
6. **底层驱动**: 通过SPI接口与LCD硬件通信

整个流程设计合理，层次清晰，便于维护和扩展。
