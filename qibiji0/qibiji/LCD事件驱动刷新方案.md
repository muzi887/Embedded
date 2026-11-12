# LCD液晶事件驱动刷新方案

## 一、方案概述

### 1.1 设计目标
将当前LCD显示系统从**定时循环刷新**改为**事件驱动刷新**，实现：
- 只在数据变化时刷新
- 只在页面切换时刷新
- 只在按键操作时刷新
- 降低CPU占用和SPI通信量
- 提升系统整体性能

### 1.2 当前问题
- **问题1**：每500ms定时刷新，即使数据未变化也刷新
- **问题2**：每次刷新都调用`LCDInit()`，重复发送初始化命令
- **问题3**：静态内容反复绘制，造成不必要的开销
- **问题4**：没有差异检测机制，无法只更新变化部分

### 1.3 优化效果
| 指标 | 优化前 | 优化后 | 提升 |
|------|--------|--------|------|
| 刷新频率 | 每500ms | 数据变化时 | 减少90%+ |
| LCDInit调用 | 每次刷新 | 页面切换时 | 减少90%+ |
| CPU占用 | 高 | 低 | 显著降低 |
| SPI通信量 | 高 | 低 | 显著减少 |

---

## 二、技术方案

### 2.1 显示缓存结构体

**文件位置**：`HARDWARE/Lcd12864/Display.h`

```c
// 显示缓存结构体 - 用于事件驱动刷新
typedef struct {
    u8 page_last;                   // 上次显示的页面
    u16 motor_curr_last;             // 上次显示的电机电流
    u16 motor_vol_last;              // 上次显示的电机电压
    u8 kaidu_last;                   // 上次显示的开度值
    u8 motor_state_last;             // 上次显示的电机状态
    u16 gate_max_last;               // 上次显示的闸位最大值
    u16 motor_warning_curr_last;     // 上次显示的堵转电流
    u8 autoFlag_last;                // 上次显示的校准标志
    u16 PV_last;                     // 上次显示的光伏电压
    u8 gprsonlineflag_last;          // 上次显示的在线状态
    u8 positionState_last;           // 上次显示的位置状态
} DisplayCache;

extern DisplayCache displayCache;
void DisplayCache_Init(void); // 初始化显示缓存
```

### 2.2 显示缓存初始化函数

**文件位置**：`HARDWARE/Lcd12864/Dispaly.c`

```c
// 显示缓存 - 用于事件驱动刷新
DisplayCache displayCache;

// 初始化显示缓存
void DisplayCache_Init(void)
{
    displayCache.page_last = 0xFF;
    displayCache.motor_curr_last = 0xFFFF;
    displayCache.motor_vol_last = 0xFFFF;
    displayCache.kaidu_last = 0xFF;
    displayCache.motor_state_last = 0xFF;
    displayCache.gate_max_last = 0xFFFF;
    displayCache.motor_warning_curr_last = 0xFFFF;
    displayCache.autoFlag_last = 0xFF;
    displayCache.PV_last = 0xFFFF;
    displayCache.gprsonlineflag_last = 0xFF;
    displayCache.positionState_last = 0xFF;
}
```

### 2.3 优化后的LcdDisplay函数

**文件位置**：`HARDWARE/Lcd12864/Dispaly.c`

**替换原有函数**：

```c
//----------------------------------------------------------------------
// 液晶显示 - 事件驱动刷新优化版本
void LcdDisplay()
{
    static u16 refresh_cnt = 0; // 定时刷新计数器
    u8 need_refresh = 0;         // 是否需要刷新标志
    
    if (KeyNum)
    {
        printf("KeyNum = %d\n", KeyNum);
    }
    
    // 1. 页面切换检测 - 页面切换时强制刷新
    if (displayCache.page_last != ViewNum)
    {
        need_refresh = 1;
    }
    
    // 2. 按键事件检测 - 有按键时强制刷新
    if (KeyNum)
    {
        need_refresh = 1;
    }
    
    // 3. 数据变化检测 - 根据当前页面检测对应数据变化
    if (ViewNum == 0) // 首页数据变化检测
    {
        extern u8 kaival;           // 开度值
        extern u8 sys_moter_state;  // 电机状态
        
        u16 motor_curr = armReg.motor_curr / 100;
        u16 motor_vol = g_adc_val[2];
        
        if (displayCache.motor_curr_last != motor_curr ||
            displayCache.motor_vol_last != motor_vol ||
            displayCache.kaidu_last != kaival ||
            displayCache.motor_state_last != sys_moter_state)
        {
            need_refresh = 1;
            displayCache.motor_curr_last = motor_curr;
            displayCache.motor_vol_last = motor_vol;
            displayCache.kaidu_last = kaival;
            displayCache.motor_state_last = sys_moter_state;
        }
    }
    else if (ViewNum == 1) // 设置页面数据变化检测
    {
        if (displayCache.gate_max_last != armReg.gate_maxheight ||
            displayCache.motor_warning_curr_last != armReg.motor_warning_curr_upper ||
            displayCache.autoFlag_last != autoFlag)
        {
            need_refresh = 1;
            displayCache.gate_max_last = armReg.gate_maxheight;
            displayCache.motor_warning_curr_last = armReg.motor_warning_curr_upper;
            displayCache.autoFlag_last = autoFlag;
        }
    }
    else if (ViewNum == 3) // 设备页面数据变化检测
    {
        extern u8 gprsonlineflag;
        u16 PV_val = g_adc_val[1];
        
        if (displayCache.PV_last != PV_val ||
            displayCache.gprsonlineflag_last != gprsonlineflag)
        {
            need_refresh = 1;
            displayCache.PV_last = PV_val;
            displayCache.gprsonlineflag_last = gprsonlineflag;
        }
    }
    else if (ViewNum == 4) // 操作页面数据变化检测
    {
        extern u8 FLAG_OPENALL, FLAG_CLOSEALL;
        u8 positionState = 0;
        if (FLAG_OPENALL)
            positionState = 1;
        else if (FLAG_CLOSEALL)
            positionState = 2;
        
        if (displayCache.positionState_last != positionState)
        {
            need_refresh = 1;
            displayCache.positionState_last = positionState;
        }
    }
    
    // 4. 定时刷新（降低频率，仅作为兜底机制，5秒刷新一次）
    if (flag500ms)
    {
        flag500ms = 0;
        refresh_cnt++;
        if (refresh_cnt >= 10) // 10 * 500ms = 5秒
        {
            need_refresh = 1;
            refresh_cnt = 0;
        }
    }
    
    // 5. 只在需要时刷新
    if (need_refresh)
    {
        ViewPages(KeyNum);
        // 页面刷新完成后，更新page_last
        displayCache.page_last = ViewNum;
    }
}
```

### 2.4 优化各页面函数

#### 2.4.1 首页优化

**文件位置**：`HARDWARE/Lcd12864/Dispaly.c`

**替换原有`homepage()`函数**：

```c
// 首页显示 - 优化版本（事件驱动刷新）
void homepage()
{
    static u8 page_init_flag = 0; // 页面初始化标志
    
    // 只在首次显示或页面切换时初始化LCD和绘制静态内容
    if (page_init_flag == 0 || displayCache.page_last != 0)
    {
        LCDInit();      // 初始化
        delay_us(500);  // 等待时间
        
        // 绘制静态内容（只绘制一次）
        ShowQQCharH(0x98, liuliang, 8); // 显示'流量'
        WRGDRAM1(0xff, 0xc0, 0x03);     // 画边框
        WRGDRAM(3, &code[0][0]);
        WRGDRAM(4, &code[1][0]);
        
        page_init_flag = 1;
    }
    
    // 更新动态数据（每次刷新都更新）
    DisplayFloat(&motor[5], armReg.motor_curr / 100, 0);
    DisplayFloat(&motor[11], g_adc_val[2], 0);
    kaiduVal(&kaidu[6], &kaidu[12]);
    
    ShowQQCharH(0x90, kaidu, 8); // 显示'开度：'
    ShowQQCharH(0x88, motor, 8);  // 显示'电机：'
}
```

#### 2.4.2 设置页面优化

**文件位置**：`HARDWARE/Lcd12864/Dispaly.c`

**修改`setpage()`函数开头和浏览模式部分**：

```c
// 设置页面
void setpage()
{
    static u8 Num = 0x30;
    static u8 cnt = 0, flag = 0, row3flag = 0;
    static u8 lastSetPageNum = 0xFF;
    static u8 page_init_flag = 0; // 页面初始化标志
    
    // 只在首次显示或页面切换时初始化LCD
    if (page_init_flag == 0 || displayCache.page_last != 1)
    {
        LCDInit();      // 初始化
        delay_us(500);  // 等待时间
        page_init_flag = 1;
    }
    
    if (setParaflag) // 设置参数
    {
        // ... 原有编辑模式代码保持不变 ...
    }
    else // 按键处理逻辑
    {
        static u8 browse_init_flag = 0; // 浏览模式初始化标志
        
        switch (KeyNum)
        {
        case KEY_UP:
            if (SetPageNum > 0)
            {
                SetPageNum--;
            }
            break;
        case KEY_DOWN:
            if (SetPageNum < SetP_Num)
            {
                SetPageNum++;
            }
            break;
        case KEY_ENTER:
            setParaflag = 1;
            break;
        default:
            break;
        }
        
        // 只在首次显示或页面切换时绘制静态内容
        if (browse_init_flag == 0 || displayCache.page_last != 1)
        {
            WRGDRAM1(0xff, 0xc0, 0x03); // 画边框
            WRGDRAM(3, &code[2][0]);
            WRGDRAM(4, &code[3][0]);
            browse_init_flag = 1;
        }
        
        // 更新动态数据
        DisplayInt(&zhaweimax[6], armReg.gate_maxheight, 1);
        DisplayFloat(&duzhuan[6], armReg.motor_warning_curr_upper, 1);
        fjiaozhun(&jiaozhun[6], autoFlag);
        ShowQQCharH(0x90, zhaweimax, 8);
        ShowQQCharH(0x88, duzhuan, 8);
        ShowQQCharH(0x98, jiaozhun, 8);
        
        // 按键行显示 - 仅当SetPageNum改变时执行
        if (lastSetPageNum != SetPageNum)
        {
            lastSetPageNum = SetPageNum;
            switch (SetPageNum)
            {
            case 0:
                cnt = 6;
                Num = zhaweimax[6];
                WRGDRAM128X8(0, 0x10, 0xff); // 第二行反选
                WRGDRAM128X8(8, 0, 0x00);    // 第三行反选
                WRGDRAM128X8(8, 0x10, 0x00); // 第四行反选
                break;
            case 1:
                cnt = 6;
                Num = duzhuan[6];
                WRGDRAM128X8(0, 0x10, 0x00); // 第二行反选
                WRGDRAM128X8(8, 0, 0xff);     // 第三行反选
                WRGDRAM128X8(8, 0x10, 0x00);  // 第四行反选
                break;
            case 2:
                cnt = 9;
                if (autoFlag == 0)
                    row3flag = 0;
                else
                    row3flag = 1;
                WRGDRAM128X8(0, 0x10, 0x00); // 第二行反选
                WRGDRAM128X8(8, 0, 0x00);    // 第三行反选
                WRGDRAM128X8(8, 0x10, 0xff); // 第四行反选
                break;
            default:
                break;
            }
        }
    }
}
```

#### 2.4.3 4G页面优化

**文件位置**：`HARDWARE/Lcd12864/Dispaly.c`

**替换原有`G4gpage()`函数**：

```c
// 4G页面 - 优化版本（事件驱动刷新）
void G4gpage()
{
    static u8 page_init_flag = 0; // 页面初始化标志
    
    // 只在首次显示或页面切换时初始化LCD和绘制静态内容
    if (page_init_flag == 0 || displayCache.page_last != 2)
    {
        LCDInit();      // 初始化
        delay_us(500);  // 等待时间
        
        ShowQQCharH(0x90, xinhao, 8); // 显示'信号：'
        ShowQQCharH(0x88, ccid, 8);   // 显示'ccid：'
        ShowQQCharH(0x98, empty, 8);  // 显示'空行'
        
        WRGDRAM1(0xff, 0xc0, 0x03); // 画边框
        WRGDRAM(3, &code[11][0]);
        WRGDRAM(4, &code[12][0]);
        
        page_init_flag = 1;
    }
    // 4G页面数据变化较少，主要依赖页面切换刷新
}
```

#### 2.4.4 设备页面优化

**文件位置**：`HARDWARE/Lcd12864/Dispaly.c`

**替换原有`Devicepage()`函数**：

```c
// 设备页面 - 优化版本（事件驱动刷新）
void Devicepage()
{
    static u8 page_init_flag = 0; // 页面初始化标志
    
    // 只在首次显示或页面切换时初始化LCD和绘制静态内容
    if (page_init_flag == 0 || displayCache.page_last != 3)
    {
        LCDInit();      // 初始化
        delay_us(500);  // 等待时间
        
        ShowQQCharH(0x90, order, 8); // 显示'编号：'
        WRGDRAM1(0xff, 0xc0, 0x03);  // 画边框
        WRGDRAM(3, &code[13][0]);
        WRGDRAM(4, &code[14][0]);
        
        page_init_flag = 1;
    }
    
    // 更新动态数据
    DisplayFloat(&PV[6], g_adc_val[1], 0);
    DisplayState(&dstate[6]);
    ShowQQCharH(0x88, dstate, 8); // 显示'状态：'
    ShowQQCharH(0x98, PV, 8);      // 显示'光伏：'
}
```

#### 2.4.5 操作页面优化

**文件位置**：`HARDWARE/Lcd12864/Dispaly.c`

**替换原有`OperatePage()`函数**：

```c
// 操作页面 - 优化版本（事件驱动刷新）
void OperatePage()
{
    static u8 page_init_flag = 0; // 页面初始化标志
    extern u8 FLAG_OPENALL, FLAG_CLOSEALL;
    int positionState = 0;
    
    if (FLAG_OPENALL)
        positionState = 1;
    else if (FLAG_CLOSEALL)
        positionState = 2;
    
    // 只在首次显示或页面切换时初始化LCD和绘制静态内容
    if (page_init_flag == 0 || displayCache.page_last != 4)
    {
        LCDInit();      // 初始化
        delay_us(500);  // 等待时间
        
        ShowQQCharH(0x90, empty, 8);             // 显示'空'
        ShowQQCharH(0x88, shang_xia_xianwei, 8); // 显示'上限位  下限位'
        ShowQQCharH(0x98, empty, 8);             // 显示'空'
        
        WRGDRAM1(0xff, 0xc0, 0x03); // 画边框
        WRGDRAM(3, &code[17][0]);
        WRGDRAM(4, &code[18][0]);
        
        page_init_flag = 1;
    }
    
    // 按键行显示（根据位置状态更新）
    switch (positionState)
    {
    case 0:
        WRGDRAM128X3(8, 0, 0x00);  // 上限位不反选
        WRGDRAM128X3(13, 0, 0x00); // 下限位不反选
        break;
    case 1:
        WRGDRAM128X3(8, 0, 0xff);  // 上限位反选
        WRGDRAM128X3(13, 0, 0x00); // 下限位不反选
        break;
    case 2:
        WRGDRAM128X3(8, 0, 0x00);  // 上限位不反选
        WRGDRAM128X3(13, 0, 0xff); // 下限位反选
        break;
    default:
        break;
    }
}
```

### 2.5 初始化函数修改

**文件位置**：`HARDWARE/Lcd12864/Dispaly.c`

**修改`Lcd_Init()`函数**：

```c
// io初始化
void Lcd_Init(void)
{
    Lcd_Gpio_Init();
    delay_ms(1500);      // 等待屏幕启动时间
    CLEARGDRAMH(0x00);   // 清除显示区域
    DisplayCache_Init(); // 初始化显示缓存
    homepage();
}
```

---

## 三、实现步骤

### 步骤1：添加显示缓存结构体
1. 打开 `HARDWARE/Lcd12864/Display.h`
2. 在 `#define BackLight PAout(0)` 之后添加 `DisplayCache` 结构体定义
3. 添加 `extern DisplayCache displayCache;` 和函数声明

### 步骤2：实现初始化函数
1. 打开 `HARDWARE/Lcd12864/Dispaly.c`
2. 在全局变量定义区域添加 `DisplayCache displayCache;`
3. 添加 `DisplayCache_Init()` 函数实现

### 步骤3：优化LcdDisplay函数
1. 找到 `LcdDisplay()` 函数
2. 替换为优化版本的事件驱动刷新逻辑

### 步骤4：优化各页面函数
1. 依次优化 `homepage()`、`setpage()`、`G4gpage()`、`Devicepage()`、`OperatePage()`
2. 添加页面初始化标志
3. 分离静态内容和动态数据更新

### 步骤5：修改初始化调用
1. 在 `Lcd_Init()` 函数中添加 `DisplayCache_Init()` 调用

---

## 四、工作原理

### 4.1 刷新触发机制

```
刷新触发条件（优先级从高到低）：
1. 页面切换 → 强制刷新 + 重新初始化LCD
2. 按键操作 → 强制刷新
3. 数据变化 → 检测到变化时刷新
4. 定时兜底 → 5秒刷新一次（防止遗漏）
```

### 4.2 数据变化检测流程

```
LcdDisplay() 调用
    ↓
检查页面切换？
    ├─ 是 → 设置need_refresh = 1
    └─ 否 → 继续检查
        ↓
检查按键事件？
    ├─ 是 → 设置need_refresh = 1
    └─ 否 → 继续检查
        ↓
根据当前页面检测数据变化
    ├─ 首页 → 检测电机电流、电压、开度值、电机状态
    ├─ 设置页 → 检测闸位最大值、堵转电流、校准标志
    ├─ 设备页 → 检测光伏电压、在线状态
    └─ 操作页 → 检测位置状态
        ↓
数据变化？
    ├─ 是 → 设置need_refresh = 1，更新缓存
    └─ 否 → 继续检查
        ↓
检查定时刷新（5秒）
    ├─ 到时间 → 设置need_refresh = 1
    └─ 未到 → 跳过
        ↓
need_refresh == 1？
    ├─ 是 → 调用ViewPages()刷新显示
    └─ 否 → 跳过本次刷新
```

### 4.3 页面函数优化原理

```
页面函数调用
    ↓
检查页面初始化标志
    ├─ 首次显示 → 初始化LCD + 绘制静态内容
    ├─ 页面切换 → 重新初始化LCD + 绘制静态内容
    └─ 同一页面 → 跳过初始化
        ↓
更新动态数据（每次刷新都执行）
    ↓
完成显示
```

---

## 五、注意事项

### 5.1 外部变量声明
确保在 `Dispaly.c` 文件开头添加必要的外部变量声明：

```c
extern u8 kaival;           // 开度值
extern u8 sys_moter_state; // 电机状态
extern u8 gprsonlineflag;  // GPRS在线标志
extern u8 FLAG_OPENALL;    // 全开标志
extern u8 FLAG_CLOSEALL;   // 全关标志
```

### 5.2 页面切换检测
- `displayCache.page_last` 在页面函数**调用后**更新
- 页面函数通过检查 `displayCache.page_last != ViewNum` 来判断页面切换
- 确保页面切换时能正确重新初始化

### 5.3 静态变量作用域
- 每个页面函数的 `page_init_flag` 是独立的静态变量
- 只在各自函数内有效，不会相互影响

### 5.4 编辑模式处理
- 设置页面的编辑模式（`setParaflag == 1`）保持原有逻辑不变
- 只在浏览模式（`setParaflag == 0`）时应用优化

---

## 六、测试验证

### 6.1 功能测试
- [ ] 页面切换正常显示
- [ ] 按键操作响应正常
- [ ] 数据变化时正确刷新
- [ ] 静态内容不重复绘制
- [ ] 编辑模式功能正常

### 6.2 性能测试
- [ ] CPU占用率降低
- [ ] SPI通信量减少
- [ ] 无数据变化时不刷新
- [ ] 定时兜底机制正常工作

### 6.3 边界测试
- [ ] 首次启动显示正常
- [ ] 快速切换页面无异常
- [ ] 长时间运行无内存泄漏
- [ ] 数据快速变化时刷新正常

---

## 七、扩展建议

### 7.1 进一步优化
1. **区域刷新**：只刷新变化的显示区域，而不是整屏
2. **双缓冲机制**：对比前后帧，只更新差异部分
3. **智能刷新策略**：根据数据类型设置不同的刷新阈值

### 7.2 调试支持
1. 添加刷新计数器，统计刷新次数
2. 添加调试宏，可选择性启用/禁用优化
3. 添加性能监控，记录刷新耗时

---

## 八、版本信息

- **版本**：v1.0
- **日期**：2024年
- **作者**：系统优化方案
- **状态**：待实现

---

## 附录：文件修改清单

### A.1 Display.h 修改
- [ ] 添加 `DisplayCache` 结构体定义
- [ ] 添加 `extern DisplayCache displayCache;`
- [ ] 添加 `void DisplayCache_Init(void);` 函数声明

### A.2 Dispaly.c 修改
- [ ] 添加外部变量声明
- [ ] 添加 `DisplayCache displayCache;` 全局变量
- [ ] 添加 `DisplayCache_Init()` 函数实现
- [ ] 修改 `LcdDisplay()` 函数
- [ ] 修改 `Lcd_Init()` 函数
- [ ] 优化 `homepage()` 函数
- [ ] 优化 `setpage()` 函数
- [ ] 优化 `G4gpage()` 函数
- [ ] 优化 `Devicepage()` 函数
- [ ] 优化 `OperatePage()` 函数

---

**文档结束**
