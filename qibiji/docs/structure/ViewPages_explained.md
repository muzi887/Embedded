# ViewPages() 及相关函数代码解读

> 对象：闸门控制器 STM32 工程 `qibiji/HARDWARE/Lcd12864/Dispaly.c` 的 LCD 页面导航链
> 涉及：`ViewPage`（宏）、`ViewPages()`、`DisplayViewPages()`、`homepage()` / `setpage()` / `G4gpage()` / `Devicepage()` / `OperatePage()`、`LcdDisplay()`、`BLight_Ctrl()`
> 说明：本文只做静态代码解读，**未修改任何 `.c` / `.h` 源码**。页面绘制的逐行细节另见 `docs/Lcd_Process_explained.md` §7–§8，本文聚焦「导航与分发」这条链。

---

## 0. 一句话总览

`ViewPages()` 是 LCD 的**页面调度员**：拿到本轮按键 `KeyNum`，决定「现在该显示第几页」，再叫 `DisplayViewPages()` 把这一页真正画到屏上，最后把「当前不在首页」这件事同步给电机模块。它自己不写屏、不读传感器、不碰参数，只管**翻页 + 分发 + 状态同步**三件事。

整套代码的最终目的只有一个：让现场人员用 5 个按键，在 5 个页面之间查看状态、修改两个整定参数、触发一次自动校准。

---

## 1. 先澄清命名：三个长得像的东西不是一回事

用户问的 `viewPage()` 在工程里并不存在，实际是下面三者：

| 名字 | 类型 | 定义处 | 是什么 |
|---|---|---|---|
| `ViewPage` | **宏常量 0** | `Display.h:5` | 旧版留下的页面编号常量，全工程只在 `ViewPages()` 最后 `return ViewPage;` 用了一次 |
| `ViewPages(uint8_t KeyNum)` | **函数** | `Dispaly.c:346`，声明 `Display.h:65` | 页面导航 + 按键分发 + `PageSta` 同步，本文主角 |
| `DisplayViewPages(uint8_t PageNum)` | **函数** | `Dispaly.c:394`，声明 `Display.h:66` | 页面号 → 页面绘制函数的 switch 分发器 |

配套还有一组**同批定义但已完全没人用**的宏（`Display.h:6-14`）：`SetPage=2`、`SetPara=3`、`SetVal=4`、`SetParaN=2`、`Home_Num=3`。它们和当前 `ViewNum` 的 0~4 编号体系对不上，是旧版页面编号的残留。真正在用的只有两个：

```c
#define View_Num 4   /* 最大页号 → 共 5 页：0,1,2,3,4 */
#define SetP_Num 2   /* 设置页最多选中项 → 共 3 项：0,1,2 */
```

**一句话**：`ViewPage` 是个数字，`ViewPages()` 是调度员，`DisplayViewPages()` 是接线员。

---

## 2. 它在系统里干什么（业务定位）

### 2.1 一句人话 + 一个比喻

闸门控制器有一块 12864 液晶屏和 5 个按键，但按键要干的事很多：控制电机升降、翻页、改参数、启停自动校准。**物理按键只有一套，语义却随页面变化**——`ViewPages()` 就是那个「根据当前页面解释按键」的分发中枢。

比喻：它像老式电视机的**频道旋钮 + 频道表**。旋钮（LEFT/RIGHT）只决定「现在看几频道」，频道表（`DisplayViewPages`）决定「这个频道播什么内容」；而同一个「确认」键在首页是关电机、在设置页是改数字、在其他页是回首页，全靠当前频道号 `ViewNum` 来解释。

### 2.2 它管五个页面

| `ViewNum` | 页面函数 | 屏幕上是什么 | 顶部进度条 |
|---|---|---|---|
| 0 | `homepage()` | 首页：电机电流 / 电压 / 开度 / 状态 / 流量（预留） | `GenerateProgressBar1(1,5,...)` |
| 1 | `setpage()` | 设置：闸位最大高度、堵转电流阈值、自动校准开关 | `GenerateProgressBar1(2,5,...)` |
| 2 | `G4gpage()` | 4G：信号 CSQ、CCID、在线状态（三态） | `GenerateProgressBar1(3,5,...)` |
| 3 | `Devicepage()` | 设备 / OTA：编号、版本、升级允许标志 | `GenerateProgressBar1(4,5,...)` |
| 4 | `OperatePage()` | 操作：上限位 / 下限位开关实时反白指示 | `GenerateProgressBar1(5,5,...)` |

第二个参数固定为 5，即「五个页签，第 N 个高亮」。

### 2.3 它不干什么

- **不直接写 GDRAM**：所有 `ShowQQCharH` / `WRGDRAM` / `WRGDRAM128X8` 都在页面函数里。
- **不决定何时刷新**：刷新时机由上游 `LcdDisplay()` 的五个触发源决定，`ViewPages()` 只是被叫到才干活。
- **不写 EEPROM、不发 JSON**：只有 `setpage()` 在参数确认时会直接落盘（见 §10）。
- **不驱动电机**：它通过 `PageSta` 间接影响 `dcmotor_keycheck()` 是否允许按键控制电机（见 §8）。

---

## 3. 调用上下文：谁在什么节拍下叫它

### 3.1 主循环位置（`USER/main.c` L58–L84）

```
while(1):
  IWDG_Feed()
  sbf20_poll()                    // 蓝牙
  uart3_at_sequence_main_loop_tick() ? continue   // 近场 AT 配置期间跳过整轮
  KEY_Process()                   // ← KeyNum 在这里被刷新/清零
  AD_Process()
  Dcmotor_Process()               // ← 先读 KeyNum 决定是否启停电机
  Lcd_Process()                   // ← 后调用 LCD 链
  PropertyReport_Process()
  Gprs_Process()
  Eeprom_Wpara()
  manage485()
```

`Lcd_Process()`（`Dispaly.c:188-192`）只有两行：`BLight_Ctrl(); LcdDisplay();`。完整调用链是：

```
Lcd_Process()
  ├─ BLight_Ctrl()            背光：有按键亮，无操作 30s 灭
  └─ LcdDisplay()             判断「这轮要不要刷屏」
       └─ ViewPages(KeyNum)  ← 只有 need_refresh 时才调用
            ├─ 按 KeyNum 改 ViewNum
            ├─ DisplayViewPages(ViewNum)
            │    └─ homepage / setpage / G4gpage / Devicepage / OperatePage
            └─ 同步 PageSta
```

**关键点：`ViewPages()` 不是每轮主循环都跑。** 它被 `LcdDisplay()` 的 `need_refresh` 门控，是为了避免 12864 的 SPI 串行重绘把主循环拖慢（一次整屏重发约几毫秒）。

### 3.2 `LcdDisplay()` 的五个刷新触发源（`Dispaly.c:231-343`）

| # | 触发条件 | 代码 | 业务含义 |
|---|---|---|---|
| 1 | 页面切换 | `displayCache.page_last != ViewNum` (L242) | 翻页后必须整页重画 |
| 2 | 有按键 | `if (KeyNum)` (L248) | 按键要立刻看到反馈（含数字编辑闪烁） |
| 3 | 当前页数据变化 | L254–L322，按 `ViewNum` 分 5 支比对 `displayCache` | 电流/开度/信号/CCID/限位等变了才刷 |
| 4 | 500ms 兜底 | `if (flag500ms)` (L325–L334) | 无条件每 500ms 刷一次 |
| 5 | 刷新执行 | `if (need_refresh) { ViewPages(KeyNum); displayCache.page_last = ViewNum; }` (L337–L342) | 刷完回写缓存 |

第 4 条注释写「降低频率，仅作为兜底机制，0.5 秒刷新一次」，但下面 `if (refresh_cnt >= 10) // 5秒` 那一层**被注释掉了**，所以实际是每 500ms 必刷。这意味着第 1/3 条的"事件驱动"优化在稳态下基本被兜底冲掉，只在两次 500ms 之间起到提前刷新的作用。它同时也是**设置页编辑光标闪烁的唯一驱动**（见 §7.3）。

### 3.3 按键从哪来、活多久（`HARDWARE/KEY/key.c`）

`KEY_Process()` 每轮主循环执行 `Get_KeyVal(); KeyNum = KEY_Scan();`，`KEY_Scan()` 从 `keyvalue[5]` 里取一个非零值并**顺手清零**，无按键返回 0。所以：

- `KeyNum` 是**一次性事件**，一轮主循环内有效，下一轮必被覆盖。
- 键值（`key.h:11-15`）：`KEY_UP=1`、`KEY_DOWN=2`、`KEY_ENTER=3`、`KEY_LEFT=4`、`KEY_RIGHT=5`。
- 同一轮里 **`KeyNum` 会被三处先后读到**：`Dcmotor_Process()` → `LcdDisplay()` → `ViewPages()` / `setpage()`。详见 §8。

---

## 4. `ViewPages()` 逐行解读（`Dispaly.c:346-391`）

```c
uint8_t ViewPages(uint8_t KeyNum)
{
    switch (KeyNum) {
    case KEY_LEFT:                          // 左：上一页
        if (ViewNum > 0) ViewNum--;
        else             ViewNum = View_Num;   // 0 → 4，环绕
        HomeNum = 0;
        break;
    case KEY_RIGHT:                         // 右：下一页
        if (ViewNum < View_Num) ViewNum++;
        else                    ViewNum = 0;  // 4 → 0，环绕
        HomeNum = 0;
        break;
    case KEY_ENTER:                         // 确认：回首页
        if (ViewNum != 1) {                  // ★ 设置页豁免
            ViewNum = 0;
            HomeNum = 0;
        }
        break;
    default: break;                         // KEY_UP / KEY_DOWN 不参与导航
    }

    DisplayViewPages(ViewNum);              // 真正画页面
    PageSta = (ViewNum == 0) ? 0 : ViewNum; // 同步给电机模块
    return ViewPage;                        // 恒为 0
}
```

### 4.1 导航状态机

```
      KEY_RIGHT (或 0 按 LEFT 环绕)
  0 ─────► 1 ─────► 2 ─────► 3 ─────► 4 ──┐
  ▲                                        │
  └────────────────────────────────────────┘  KEY_RIGHT 从 4 回 0
  任意非设置页 + KEY_ENTER ──► 0
  设置页(1) + KEY_ENTER ──► 保持 1（让给 setpage 用）
```

`ViewNum` 是全局 `u8`（`Dispaly.c:119`），初值 0（首页）。左右都做了**边界环绕**，所以按键不会把页号顶出 0~4。`HomeNum` 在三个分支里被清零，但全工程没有任何有效读取（只在 L849 以下注释掉的旧 `homepage()` 里出现）——**死变量**。

### 4.2 三个必须记住的设计点

1. **`KEY_ENTER` 在设置页故意不回首页**（L372 `ViewNum != 1`）。设置页需要 ENTER 做「选中项 / 确认数字 / 完成编辑」，导航语义必须让位给编辑语义。其余 4 个页面按 ENTER 一律回首页。
2. **`PageSta` 在 `DisplayViewPages()` 之后才同步**（L384-389），即「页面真画完了」才对外宣告当前页。它是给 `dcmotor_keycheck()` 看的「我是否在首页」信号（见 §8）。
3. **`return ViewPage` 是死代码**：`ViewPage` 宏等于 0，返回值恒定，且唯一调用点 `LcdDisplay()` L339 直接忽略返回值。函数签名完全可以改成 `void`。

### 4.3 一个容易误读的地方

`ViewPages()` 无论有没有按键，只要被调用就会执行 `DisplayViewPages(ViewNum)`。也就是说 **500ms 兜底刷新、数据变化刷新都会重画当前页**，不只是按键时才画。按键在这里的角色只是「顺便改一下 `ViewNum`」。这也是为什么设置页的光标闪烁能靠兜底刷新驱动——`setpage()` 每次被调用都会翻转一次 `flag`。

---

## 5. `DisplayViewPages()` 逐 case 解读（`Dispaly.c:394-433`）

```c
void DisplayViewPages(uint8_t PageNum)
{
    switch (PageNum) {
    case 0: homepage();   清三行反白; break;
    case 1: setpage();                        break;
    case 2: G4gpage();    清三行反白; break;
    case 3: Devicepage(); 清三行反白; break;
    case 4: OperatePage();                 break;
    default: break;
    }
}
```

### 5.1 「清三行反白」是什么

case 0/2/3 后面紧跟三条：

```c
WRGDRAM128X8(0, 0x10, 0x00);   // 第二行
WRGDRAM128X8(8, 0,    0x00);   // 第三行
WRGDRAM128X8(8, 0x10, 0x00);   // 第四行
```

12864 的 GDRAM 是绘图层，`0xff` 表示该行**反白**（黑底白字＝选中高亮），`0x00` 表示取消反白。设置页 `setpage()` 会用 `0xff` 把选中的那一行反白（L693/702/714），如果切到别的页时不擦掉，高亮就会残留在新页面上。所以这三条是**「从设置页回来时把选中痕迹擦干净」**的收尾动作。

**补充（本轮对话）：三个词别混。** ①「反选」在这里是 **inverse highlight（反白高亮）**，不是 UI 里「取消选择 unselect」的意思。② 注释是**坐标标签**——三句注释一字不改都写「第二行/第三行/第四行反选」，标的是「这句负责哪一行」，传 `0x00` 实际是**清除**该行高亮，读作「把第二行的反选清掉」更准确。③ 三个参数里 `(x1,y1)` 决定「第几行」：`0` / `8` 选 GDRAM 上/下半屏，`0x00` / `0x10` 选半屏内的上/下 16 行——所以第二行是 `(0,0x10)`、第三行反而退回 `(8,0)`（换了半屏重新数）。四个屏幕行与 DDRAM 地址、`d1` 的完整对照，以及进度条「8 个物理格 vs 5 个逻辑份」的辨析见 `docs/Lcd_Process_explained.md` §2.4。

case 4（操作页）不清，因为它自己用 `WRGDRAM128X3` 做局部反白并且每轮按限位状态重画（L828-846），不依赖全局清理。

### 5.2 一堆被注释掉的状态重置

L403-404、L407-408、L416-417 有三组注释：

```c
// SetPageNum = 0;
// setParaflag = 0;
```

原本想在离开设置页时清空编辑状态，现在改成由 `setpage()` 自己在「首次进入/切回来」时重置（L480-498：`SetPageNum = 0xff; setParaflag = 0;`）。**效果上等价，但责任从分发器转移到了页面函数**，读代码时不要以为编辑状态没清。

### 5.3 三个工程的实现不一样

`qibiji_boot` 里 `DisplayViewPages` 是 **2 字节空函数**（`boot.htm:924` 标 `UNUSED`），因为 bootloader 不需要界面；`qibiji` 主工程是 206 字节、`PWM.map` 那份是 144 字节。同函数名跨工程行为不同，别拿 boot 的符号表推主工程逻辑。

---

## 6. 五个页面函数速查

五个函数共用同一个模板，理解一个就理解全部：

```c
void Xxxpage(void)
{
    static u8 page_init_flag = 0;
    if (page_init_flag == 0 || displayCache.page_last != N) {
        LCDInit();  delay_us(500);
        画标题点阵 / 边框 / 进度条 / 静态文字;      // 只画一次
        page_init_flag = 1;
    }
    更新动态数据缓冲 + ShowQQCharH(...) 重发三行;   // 每次刷新都做
}
```

**「静态只画一次」是靠 `displayCache.page_last` 实现的**，而 `page_last` 由 `LcdDisplay()` 在 `ViewPages()` **返回之后**才写入（L341）。所以页面函数执行时看到的 `page_last` 永远是**上一轮**的页号：从 A 页切到 B 页时，B 看到 `page_last == A` → 触发重绘；下一轮仍在 B，看到 `page_last == B` → 只刷动态部分。这个「慢一拍」不是 bug，正是分层重绘的实现基础。

| 页面 | 静态内容 | 每次刷新的动态内容 | 特别说明 |
|---|---|---|---|
| `homepage()` L436 | 标题「控制」、边框、进度条 1/5 | 电流、电压、开度+状态字、流量行 | 第四行「流量 0.0 m³/s」是**纯静态预留**，`liuliang` 缓冲从未被更新 |
| `setpage()` L470 | 标题「配置」、边框、进度条 2/5 | 三个参数值 + 选中行反白 + 编辑位闪烁 | 全文件最复杂，见 §7 |
| `G4gpage()` L724 | 标题「网络」、边框、进度条 3/5 | 信号 `xinhao`、CCID、状态三态 | 注释说「主要依赖页面切换刷新」，但 `LcdDisplay` L283-298 有完整数据变化检测，注释与实现不符 |
| `Devicepage()` L754 | 标题「OTA 参数」、编号/版本/升级、进度条 4/5 | **无** | 动态部分 L780-784 全被注释；`DisplayOTAenable()` 只在初始化分支调用，云端改了 `OTA_Enable` 不刷新 |
| `OperatePage()` L788 | 标题「限位」、"上限位 下限位"、进度条 5/5 | 按限位标志局部反白 | `positionState` 三态映射在 L794-805 和 `LcdDisplay` L311-321 各写了一遍，改判据要改两处 |

> 更细的每行文本、GB2312 缓冲下标、`kaiduVal()` / `DisplayState()` 的格式化规则，见 `docs/Lcd_Process_explained.md` §7。

---

## 7. `setpage()`：下游最复杂的相关函数（`Dispaly.c:470-721`）

它是 `ViewPages()` 分发到的第 1 页，也是唯一会**写参数、写 EEPROM、启停自动校准**的页面，所以必须单独讲。

### 7.1 两个模式、三个选中项

```
非编辑态 (setParaflag == 0)                 编辑态 (setParaflag == 1)
┌──────────────────────────────┐      ┌──────────────────────────────────┐
│ UP/DOWN: 移动 SetPageNum      │      │ UP/DOWN: 改数字 Num (0x30~0x39)  │
│   0 = 闸位最大高度 (第二行)   │ ENTER│ ENTER: 确认当前位, cnt 前进一位  │
│   1 = 堵转电流     (第三行)   │ ───► │ 编辑满 → 自动退出 + 换算 + 落盘   │
│   2 = 自动校准开关 (第四行)   │ ◄─── │ 当前位在空格/数字间交替 = 闪烁    │
│ ENTER: 首次选中(0xff→0),      │ 完成 │                                  │
│        再次才进入编辑         │      │                                  │
└──────────────────────────────┘      └──────────────────────────────────┘
```

- 页面初始化（L480-498）把 `SetPageNum = 0xff`（未选中）、`setParaflag = 0`，画边框/标题。
- **首次 ENTER 只是选中第一项**（L661-663 `0xff → 0`），**第二次 ENTER 才把 `setParaflag` 置 1**（L664-665）。
- 选中行反白在 `SetPageNum` 变化时才做（L685-719），靠 `lastSetPageNum` 去重，避免每轮刷屏都重写 GDRAM。

### 7.2 三个编辑项的差异（KEY_ENTER 分支 L520-571）

| 项 | 缓冲 | 位数 / 特殊处理 | 完成动作 |
|---|---|---|---|
| 0 闸位最大高度 | `zhaweimax[6..9]` | 4 位（cnt 6→10） | `asciitoInt(&zhaweimax[6],4)` → `armReg.gate_maxheight` → `AT24CXX_WriteWord(0x0006, ...)` |
| 1 堵转电流 | `duzhuan[6..9]` | 4 位，**`cnt==8` 时跳过一位（小数点）** | `asciitoInt(&duzhuan[6],4) * 100u`（显示 A → 内部 mA），夹到 `0xFFFF` → `armReg.motor_warning_curr_upper` → `AT24CXX_WriteWord(0x0004, ...)` |
| 2 自动校准 | `jiaozhun[6..13]` | 不用数字，用 `row3flag`（UP=1 / DOWN=0） | 置 `autoFlag = 1/0`，**不写 EEPROM** |

例：屏上输入 `35.0` → `asciitoInt` 得 35 → `×100 = 3500 mA` 存进 `armReg.motor_warning_curr_upper`，供 `ADC1_Read_filter()` 做堵转判据。这是 LCD 现场整定阈值的唯一入口。

### 7.3 闪烁从哪里来

`flag` 静态变量在 L583-592 / L601-610 / L619-636 三个 case 里交替把当前编辑位写成 `0x20`（空格）或 `Num`（数字）。因为 `LcdDisplay()` 每 500ms 无条件刷新一次 → `ViewPages()` → `DisplayViewPages(1)` → `setpage()` 被再调一次 → `flag` 翻转，**两次刷新 = 一个完整亮灭周期 = 1s**，视觉上是 1Hz 编辑光标。数据变化检测那一路在编辑时基本不触发（电机静止、电流不变），所以 500ms 兜底实际就是闪烁的唯一驱动。

---

## 8. 关键状态量与「一键三方消费」

### 8.1 状态量总表

| 变量 | 定义 | 谁写 | 谁读 | 含义 |
|---|---|---|---|---|
| `ViewNum` | `Dispaly.c:119` | `ViewPages()` | `LcdDisplay()`、`DisplayViewPages()` | 当前页号 0~4（内部导航态） |
| `PageSta` | `Dispaly.c:122` | `ViewPages()` L384-389 | `dc_motor.c:256` `dcmotor_keycheck()` | 对外信号：`0`＝在首页，允许按键控电机 |
| `SetPageNum` | `Dispaly.c:120` | `setpage()` | `setpage()` | 设置页选中项 0~2 / `0xff` 未选中 |
| `setParaflag` | `Dispaly.c:16` | `setpage()` | `setpage()` | 0＝浏览、1＝编辑 |
| `autoFlag` | `Dispaly.c:15` | `setpage()` L562/566 | `dcmotor_auto()`、`event_pub.c`、`property_report.c` | 自动校准开关；为 1 时抑制自动上报 |
| `KeyNum` | `key.c:8` | `KEY_Process()` | 电机模块 + `LcdDisplay()` + `ViewPages()` + `setpage()` | 本轮按键事件，一次性 |
| `displayCache` | `Dispaly.c:125` | `LcdDisplay()` L341 等 | 五个页面函数、`LcdDisplay()` | 上一轮显示值，用于变化检测与静态重绘判定 |

`ViewNum` 与 `PageSta` 是同一信息的两份表达：前者给 LCD 内部用，后者专门给电机模块当「页面闸门」。

### 8.2 三个消费者怎么不打架

主循环里 `Dcmotor_Process()` 在 `Lcd_Process()` **之前**，同一轮 `KeyNum` 会被依次看到：

| 场景 | `dcmotor_keycheck()`（先） | `ViewPages()` / `setpage()`（后） | 结果 |
|---|---|---|---|
| 首页按 UP/DOWN | `PageSta==0` → 启动电机 | `ViewPages` 对 UP/DOWN 走 `default`，不处理 | 按键 = 升降 ✓ |
| 设置页按 UP/DOWN | `PageSta!=0` → **不动电机** | `setpage()` 用它移动选中项 / 改数字 | 按键 = 编辑 ✓ |
| 设置页按 ENTER | `PageSta!=0` → 不动电机 | `ViewPages` 故意跳过回首页；`setpage()` 用它确认 | 按键 = 确认 ✓ |
| 其他页按 ENTER | `PageSta!=0` → 不动电机 | `ViewPages` 回首页 | 按键 = 回首页 ✓ |
| 按 LEFT/RIGHT | 不匹配 UP/DOWN/ENTER | `ViewPages` 翻页 | 按键 = 翻页 ✓ |

**翻页键与电机键键值不重叠**，加上 `PageSta` 这道闸门，才让「同一组 5 个按键、不同页面不同语义」成立。代价是**一拍延迟**：按 LEFT 离开首页那一轮，`dcmotor_keycheck()` 看到的还是上一轮的 `PageSta==0`；但 LEFT 本身不触发电机动作，所以无害。反向耦合也存在：显示模块导出了 `PageSta` 和 `autoFlag` 两个控制量给电机模块。

---

## 9. 一次翻页的完整时序

以「首页按 KEY_RIGHT 进设置页」为例（`KEY_Process()` 已把 `KeyNum=5`）：

| 步 | 位置 | 动作 |
|---|---|---|
| 1 | `KEY_Process()` | `KeyNum = KEY_RIGHT(5)` |
| 2 | `Dcmotor_Process()` | `dcmotor_keycheck()`：`PageSta` 仍为 0，但 RIGHT 不匹配 UP/DOWN/ENTER → 无动作 |
| 3 | `BLight_Ctrl()` | 见 `KeyNum` 非零 → 开背光、`cnt=0` |
| 4 | `LcdDisplay()` L242/L248 | `page_last(0) != ViewNum(0)` 为假，但 `KeyNum` 非零 → `need_refresh=1` |
| 5 | `ViewPages(5)` | `ViewNum: 0→1`，`HomeNum=0` |
| 6 | `DisplayViewPages(1)` | 调 `setpage()`；此时 `page_last` **仍是 0**（还没回写）→ 命中初始化分支，重画标题/边框/进度条，并 `SetPageNum=0xff`、`setParaflag=0` |
| 7 | `ViewPages()` L384-389 | `PageSta = 1` |
| 8 | `LcdDisplay()` L341 | `displayCache.page_last = 1` |
| 9 | 下一轮（无按键） | 数据变化检测（ViewNum==1 分支）比对 `gate_max`/`warning_curr`/`autoFlag`；500ms 兜底到达时再刷一次，此时 `page_last==1`，`setpage()` 只更新动态值、不重画静态 |

第 6 步和第 8 步的先后顺序是理解「静态只画一次」的关键：**页面函数看到的是上一轮的页号**。

---

## 10. 与 EEPROM 写入的关系（最容易踩的坑）

`setpage()` 是四条 EEPROM 落盘入口之一，也是唯一从「屏幕输入」触发的。它直接调用：

```c
AT24CXX_WriteWord(0x0006, armReg.gate_maxheight);            // L531
AT24CXX_WriteWord(0x0004, armReg.motor_warning_curr_upper);  // L550
```

这里的 `0x0006` / `0x0004` 是**真实的 EEPROM 字节地址**（对应 `GATE_MAXHEIGHT_WORD_IDX` / `MOTOR_WARNING_CURR_UPPER`）。

而 `Eeprom_Wpara()`（`reg.c:203-225`）里也有 `EepromAddr = 0x0004` / `0x0006`，但那是**命令码**，实际写到 `0x0008`（`gate_maxturns`）和 `0x000C`（`gate_kaidu_count`），规律是「目标地址 = 命令码 × 2」。

| 写法 | 出现处 | 数值含义 | 实际写入地址 |
|---|---|---|---|
| `AT24CXX_WriteWord(0x0004, ...)` | `setpage()` L550 | EEPROM 字节地址 | `0x0004` 堵转阈值 |
| `EepromAddr = 0x0004` | `JudeDir()` L202 | 命令码 | `0x0008` 全行程圈数 |
| `AT24CXX_WriteWord(0x0006, ...)` | `setpage()` L531 | EEPROM 字节地址 | `0x0006` 最大高度 |
| `EepromAddr = 0x0006` | `JudeKaidu()` L169 | 命令码 | `0x000C` 断电位置 |

**四个数值两两撞车，语义完全不同**，是这套代码里最容易误判的地方。另外 `setpage()` 这条路径**绕过了** `Eeprom_Wpara()` 的命令码机制，直接同步写 EEPROM（一次 `WriteWord` ≈ 20ms 主循环停摆），也**没有读回校验**——云端 `config.push` 走的是 `Config_WriteWordWithVerify()` 带回滚，现场按键这条没有。详见 `docs/Eeprom_Wpara_explained.md` §8。

---

## 11. 代码审查发现（仅记录，未修改）

| # | 位置 | 问题 | 影响 |
|---|---|---|---|
| 1 | `Dispaly.c:390` | `return ViewPage` 恒为 0、无人消费；函数签名是 `uint8_t` | 死代码，建议改 `void` |
| 2 | `Display.h:6-14` | `SetPage`/`SetPara`/`SetVal`/`SetParaN`/`Home_Num` 五个宏全无引用；`ViewPage` 只用在 return | 旧版页面编号残留，容易误导读者以为有第二套页号体系 |
| 3 | `Dispaly.c:358/368/375` | `HomeNum` 只写不读（唯一读取在 L849 以下整段注释里） | 死变量 |
| 4 | `Dispaly.c:325-334` | 500ms 兜底无条件 `need_refresh=1`，「5 秒」那层被注释掉 | 事件驱动刷新优化在稳态被冲掉；同时是光标闪烁的唯一驱动，删兜底会让光标停住 |
| 5 | `Dispaly.c:199-215` vs `Lcd_Gpio_Init()` L184 | 背光注释互相矛盾：L184 `BackLight=1 // 关闭背光`、L201 `=1 // 打开背光`、L212 `=0 // 关闭背光` | 注释不可信，需查硬件极性（PA0 驱动） |
| 6 | `Dispaly.c:25` L195-216 | 注释写「60s 无操作返回到首页并关闭背光」，实际 `100 × 300ms = 30s`，且**没有任何回首页的代码** | 行为与注释不符；回首页得靠人工按 ENTER |
| 7 | `Dispaly.c:746` | `G4gpage()` 注释「主要依赖页面切换刷新」，但 `LcdDisplay` L283-298 有完整变化检测 | 注释与实现不符 |
| 8 | `Dispaly.c:780-784` | `Devicepage()` 动态更新全注释；`LcdDisplay` L299-308 仍监测 `PV_last` | 监测空转，触发的刷新画不出新内容；`OTA_Enable` 变化也不刷新 |
| 9 | `Dispaly.c:311-321` + L794-805 | `positionState` 三态映射写了两遍 | 改判据要同步改两处，已出现漂移风险 |
| 10 | `Dispaly.c:531/550` | 现场按键落盘无读回校验、无 `volatile` 保护；且与 `EepromAddr` 命令码数值撞车 | 见 §10 |
| 11 | `Dispaly.c:485` | `SetPageNum = 0xff` 用 `u8` 存哨兵值，与 `SetP_Num=2` 的正常范围混在一起 | 靠 L661 特判兜住，可读性差 |
| 12 | `qibiji_boot` | 同名 `DisplayViewPages` 是 2 字节 `UNUSED` 空函数 | 跨工程同名不同实现，勿据 boot 符号表推断主工程 |

---

## 12. 易混点与一句话串起来

**三个最容易混的点：**

1. **`ViewPage` ≠ `ViewPages()` ≠ `DisplayViewPages()`**——一个是常量 0，一个管导航，一个管分发。
2. **`page_last` 慢一拍**——`LcdDisplay()` 在 `ViewPages()` 返回后才写它，页面函数看到的是上一轮页号；这是「静态只画一次」的实现基础，不是 bug。
3. **`0x0004` / `0x0006` 有两套含义**——`setpage()` 里是 EEPROM 地址，`EepromAddr` 里是命令码（×2 才是地址）。

**一句话串起来**：

> `KEY_Process()` 每轮给出一个一次性的 `KeyNum` → `LcdDisplay()` 用「切页 / 有按键 / 数据变 / 500ms 兜底」四选一决定要不要刷屏 → 要刷就调 `ViewPages(KeyNum)`，它按 LEFT/RIGHT/ENTER 把 `ViewNum` 在 0~4 之间挪一下（设置页的 ENTER 特意让给编辑），同步 `PageSta` 给电机模块 → `DisplayViewPages(ViewNum)` 把页号接到五个页面函数之一，顺手擦掉上一页残留的反白 → 页面函数靠「`page_last` 还是旧页号」判断只画静态框架还是也刷动态数据；设置页 `setpage()` 额外承担参数编辑、1Hz 光标闪烁、以及把 `35.0` 换算成 `3500 mA` 后直接写进 EEPROM。

---

## 附录：出处对照

| 主题 | 文件与行号 |
|---|---|
| 宏定义与声明 | `HARDWARE/Lcd12864/Display.h:5-14, 59-66` |
| 全局状态量 | `HARDWARE/Lcd12864/Dispaly.c:15-16, 118-125` |
| `DisplayCache_Init()` | `Dispaly.c:128-144` |
| `Lcd_Init()` / `Lcd_Gpio_Init()` | `Dispaly.c:147-185` |
| `Lcd_Process()` / `BLight_Ctrl()` | `Dispaly.c:188-217` |
| `LcdDisplay()` 五触发源 | `Dispaly.c:231-343` |
| **`ViewPages()`** | `Dispaly.c:346-391` |
| **`DisplayViewPages()`** | `Dispaly.c:394-433` |
| `homepage()` | `Dispaly.c:436-467` |
| **`setpage()`** | `Dispaly.c:470-721` |
| `G4gpage()` | `Dispaly.c:724-751` |
| `Devicepage()` | `Dispaly.c:754-785` |
| `OperatePage()` | `Dispaly.c:788-847` |
| 键值与 `KeyNum` 生命周期 | `HARDWARE/KEY/key.h:11-17` · `key.c:8, 37-59` |
| 主循环顺序 | `USER/main.c:58-84` |
| `PageSta` 的消费方 | `HARDWARE/DC_MOTOR/dc_motor.c:24, 254-276` |
| `autoFlag` 的消费方 | `HARDWARE/DC_MOTOR/dc_motor.c:125-195` · `GPRS/event_pub.c:78,112,125` · `GPRS/property_report.c:124,151` |
| EEPROM 命令码对照 | `HARDWARE/REG/reg.c:203-225` · `HARDWARE/SWITCH/switch.c:169, 202` |
| 显示格式化函数 | `HARDWARE/Lcd12864/DispalyPara.c`（`kaiduVal` L94、`DisplayState` L213、`DisplayOTAenable` L236） |
| 链接器栈深 / 空函数证据 | `OBJ/QIBIJI.htm:2199-2235` · `USER/QIBIJI.map:1684-1685` · `qibiji_boot/OBJ/boot.htm:924` |

**关联文档**：`docs/Lcd_Process_explained.md`（整条 LCD 链与页面绘制细节）、`docs/Eeprom_Wpara_explained.md`（落盘命令码与四条写入入口）、`docs/ADC1_Read_filter_explained.md`（堵转阈值 `motor_warning_curr_upper` 的消费方）、`docs/Dcmotor_Process_explained.md`（`PageSta` 与自动校准）。
