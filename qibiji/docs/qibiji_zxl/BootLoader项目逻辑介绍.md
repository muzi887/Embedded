# qibiji_boot 项目逻辑介绍（2026-11-23）

> **本文定位**：通过本文件读懂这个 BootLoader 工程——每个环节都带**真实代码原文 + 文件行号**，行号均指当前工作副本（`D:\code2\software\vue\program\Embedded\qibiji\qibiji_boot`）。文中省略号处均为被注释掉的历史代码，为可读性做了删节。本文只做只读分析，未修改任何 `.c` / `.h`。

---

## 1. 它是什么：分区与关键常量

**一句话**：STM32F103VET6（512KB Flash，2KB/页）上一个「引导 + 联网升级 + 跳转」三合一的 **IAP BootLoader**，本身不含闸门/电机业务，业务在 A1 应用工程里。它只干两件事——把新固件弄进 Flash，然后把 CPU 交给应用。

Flash 分成三块，全部由宏定义推导，不写死地址（`HARDWARE/BOOT/boot.h:14-24`）：

```c
#define  ST32_FLASH_SADDR    0x08000000                                               //FLASH起始地址
#define  ST32_PAGE_SIZE      2048                                                     //FLASH页大小
#define  ST32_PAGE_NUM       256                                                      //FLASH总页数
#define  ST32_B_PAGE_NUM     28                                                       //B区页数  56k（0x08000000~0x0800E000）
#define  ST32_A1_PAGE_NUM    100                                                      //A1区页数  200k
#define  ST32_A1_START_PAGE  ST32_B_PAGE_NUM                                          //A1区起始页号
#define  ST32_A1_SADDR       (ST32_FLASH_SADDR + ST32_A1_START_PAGE * ST32_PAGE_SIZE) //A1区起始地址（当前 = 0x0800E000）

#define  ST32_A2_PAGE_NUM    100                                                      //A2区页数
#define  ST32_A2_START_PAGE  (ST32_A1_START_PAGE + ST32_A1_PAGE_NUM)                  //A2区起始页号
#define  ST32_A2_SADDR       (ST32_FLASH_SADDR + ST32_A2_START_PAGE * ST32_PAGE_SIZE) //A2区起始地址（当前 = 0x08040000）

#define  OTA_A1_FLAG        0xA1A1A1A1    //如果OTA_flag等于该值，说明运行A1区
#define  OTA_A2_FLAG        0xA2A2A2A2    //如果OTA_flag等于该值，说明运行A2区

#define  OTA_ADDR           200           //EEPROM 里 OTA 信息结构体的起始地址
```

即：**B 区 0x08000000/56KB 放本工程，A1 区 0x0800E000/200KB 跑应用，A2 区 0x08040000/200KB 暂存下载的固件**，片尾还剩 56KB 未分配。Keil 工程 `USER/BOOTV10.uvprojx:275-276` 的 IROM1 = `0x08000000 / 0xE000` 与 B 区严格一致。

OTA 信息存在 AT24C02 EEPROM 的 200 号地址起，结构体 44 字节（`boot.h:31-53`）：

```c
typedef struct {
    u32 OTA_flag;       //A1A1A1A1=A1 可运行；A2A2A2A2=A2 有待安装固件
    u8  OTA_ver[32];    //版本号字符串
    u32 OTA_size;       //固件总字节数
    u8  UPDATE_flag;    /* 升级标志：0 = 关闭（上电直接运行 A1，不联网），
                           1 = 开启（走联网 OTA）。出厂默认 1；安装成功后本工程自动置 0；
                           再次升级由平台经 A1 应用把它置回 1。 */
} OTA_InfoCB;

/* 各字段在 24C02 内的字节偏移（分字段写入时用），布局变化编译期报错 */
#define OTA_OFF_FLAG        0     /* u32 OTA_flag */
#define OTA_OFF_VER         4     /* u8  OTA_ver[32] */
#define OTA_OFF_SIZE        36    /* u32 OTA_size */
#define OTA_OFF_UPDATE      40    /* u8  UPDATE_flag */
```

工程用**负数组长度技巧做编译期断言**，把分区关系全部锁死——改任何一处分区导致关系破坏，编译直接失败（`boot.h:68-75`）：

```c
typedef char ota_layout_check[
    (ST32_A1_START_PAGE == ST32_B_PAGE_NUM &&                       // A1 紧接 B 区
     ST32_A2_START_PAGE == (ST32_A1_START_PAGE + ST32_A1_PAGE_NUM) && // A2 紧接 A1
     ((ST32_A2_START_PAGE + ST32_A2_PAGE_NUM) * ST32_PAGE_SIZE) <= (1024 * STM32_FLASH_SIZE) && // A2 不越片尾
     (ST32_A1_PAGE_NUM * ST32_PAGE_SIZE) == (200 * 1024) &&         // A1 固定 200KB（固件上限）
     (ST32_A2_PAGE_NUM * ST32_PAGE_SIZE) == (200 * 1024) &&         // A2 固定 200KB
     (ST32_B_PAGE_NUM * ST32_PAGE_SIZE) == 0xE000 &&                // B 区与 Keil IROM1 一致
     (OTA_RETRY_ADDR + 1) <= EE_ADDR) ? 1 : -1];                    // 自愈计数不越过 EE_ADDR(250)
```

其它关键数字：

| 项 | 值 | 出处 |
|---|---|---|
| 自愈重搬计数 | EEPROM `OTA_ADDR+44=244`，上限 3 次 | `boot.h:58-59` |
| 看门狗 | `IWDG_Init(4, 1990)` ≈ 3.18s | `USER/main.c:131` |
| 单包固件 | `BYTESIZE 512` 字节 + 2 字节 CRC16 | `HARDWARE/GPRS/gprs.h:31` |

---

## 2. 上电三条分支

### 2.1 流程总览

```text
上电 → delay/NVIC → USART1(调试) + USART2(4G) → 读 EEPROM → BootLoader_Brance()
  │
  ├─ ① OTA_flag != A1A1A1A1（A2 里有待安装固件）
  │     → 校验 size/镜像头 → Cal_Block → Update_A1 逐页搬 A2→A1
  │     → memcmp 比对通过 → 写 flag=A1、关 UPDATE_flag → LOAD_A(A1)   [boot.c:127-179]
  │
  ├─ ② flag=A1 但 A1 镜像头非法（烧写器整片擦除、EEPROM 没擦）
  │     → A2 仍有效则重搬（最多 3 次）→ 否则清 OTA_ver 强制重下   [boot.c:182-228]
  │
  └─ ③ 回到 main：UPDATE_flag == 1 ?
        ├─ 否 → JumpToApp() = LOAD_A(0x0800E000)   （IWDG 此时还没启动）
        └─ 是 → ota_step1() → IWDG_Init → 进 OTA 状态机大循环（见第 3 节）
```

### 2.2 main() 的初始化段（`USER/main.c:75-99`）

```c
delay_init();		  // 延时函数初始化
NVIC_Configuration(); // 设置NVIC中断分组2:2位抢占优先级，2位响应优先级

uart1_init(115200);  // 与PC通信
uart2_init(115200);  // 4G

AT24CXX_Init();		   // 初始化iic
AT24CXX_ReadOTAInfo(); // 读取eeprom  ← OTA_Info 整个结构体从这里进来
ds1302_init();
BootLoader_Brance();   // ← 分支①②在这里面，可能直接跳 A1 不返回

LED_Init();		 	 // LED端口初始化
LED_CLOSE();
Lcd_Gpio_Init();       // LCD初始化
GM4G_Init();			 // 4g初始化（PA1 复位脉冲）

CLEARGDRAMH(0x00);     // 清空显示图形
printf("bootloader is running!\r\n");
```

注意顺序：**BootLoader_Brance() 在 LCD 初始化之前执行**。这期间发现的错误不能马上显示，于是用一个全局变量登记，等 LCD 就绪后补显示（`main.c:101-116`）：

```c
/* 引导/EEPROM 检查阶段（LCD 初始化之前）登记的 OTA 错误：在这里补显示。
   hold=1 → 停 3 秒再尝试运行应用；hold=0（如 EEPROM 异常，持久性错误）→ 只显示不等。 */
if (g_ota_lcd_err != 0)
{
    ota_show_error(g_ota_lcd_err);
    if (g_ota_lcd_hold)
    {
        delay_ms(3000);
        g_ota_lcd_err = 0;
        JumpToApp();
    }
    else
    {
        g_ota_lcd_err = 0;
    }
}

//若不需要ota，直接跳转
if(OTA_Info.UPDATE_flag == 1)
{
    ota_step1();
    printf("OTA is running!\r\n");
}
else
{
    printf("jump to a1!\r\n");
    JumpToApp();           // ← 这条路径发生在 IWDG_Init 之前，看门狗还没开
}

IWDG_Init(4, 1990);        // 3180MS 看门狗（此后主循环每轮 IWDG_Feed()）
```

### 2.3 分支①②的完整实现：BootLoader_Brance()（`HARDWARE/BOOT/boot.c:117-230`）

```c
void BootLoader_Brance(void)
{
    /* ---------- 分支①：OTA_flag != A1A1A1A1，A2 里有待安装固件 ---------- */
    if (OTA_Info.OTA_flag != OTA_A1_FLAG)
    {
        /* 护栏1：OTA_size 必须是合法字节数。
           - size=0 会让 Cal_Block 算出 BlockNB=0，Update_A1 里 BlockNB-1 下溢成 65535，
             把 65535 页垃圾刷进 A1（变砖）；
           - size 曾按「块数」写入时会只搬几百字节（残包），也不允许。 */
        if (OTA_Info.OTA_size == 0 || OTA_Info.OTA_size > (u32)(ST32_A2_PAGE_NUM * ST32_PAGE_SIZE))
        {
            printf("bad OTA_size=%u\r\n", OTA_Info.OTA_size);
            OtaWriteFlag(OTA_A1_FLAG);
            OtaClearInstalledVersion();   /* 没有可安装的固件，别谎报"已是最新" */
            g_ota_lcd_err = OTA_ERR_SIZE;   /* 登记待显示：LCD 尚未初始化，交给 main 显示 */
            g_ota_lcd_hold = 1;
            return;
        }

        /* 护栏2：A2 里的镜像头必须是可运行镜像（SP/复位向量合法）。
           拦掉「上传的是 .hex 文本」「下载被截断」等坏包，避免把垃圾搬进 A1。 */
        if (!OtaA2ImageValid())
        {
            printf("A2 hdr invalid\r\n");
            OtaWriteFlag(OTA_A1_FLAG);
            OtaClearInstalledVersion();
            g_ota_lcd_err = OTA_ERR_IMAGE;
            g_ota_lcd_hold = 1;
            return;
        }

        printf("loading progarm file A1\r\n");
        Cal_Block();
        Update_A1();

        /* 搬运校验：A1 与 A2 内容一致才算安装成功 */
        if (!OtaCopyVerify())
        {
            OtaRetryCountSet(OtaRetryCount() + 1);   /* 计入失败次数，供自愈限流 */
            printf("A1 verify FAIL\r\n");
            g_ota_lcd_err = OTA_ERR_VERIFY;
            g_ota_lcd_hold = 1;
            return;      /* flag 保持 A2 → 下次开机重搬 */
        }

        if (!OtaWriteFlag(OTA_A1_FLAG))  // 写入标志位：此刻才算真正"已安装"
        {
            OtaRetryCountSet(0);
            OtaCloseUpdateGate();        /* 关闭升级标志：以后上电直接跑应用 */

            printf("write progarm file success\r\n");
            LOAD_A((u32)(ST32_A1_SADDR));  /* 跳 A1，不返回 */
        }
        else
            printf("write progarm file error\r\n");
        return;
    }

    /* ---------- 分支②：开机自愈。flag 说"A1 已最新"但 A1 实际不可运行 ----------
       常见于用烧写工具整片擦除后：EEPROM 不会被擦，flag 仍是 A1、OTA_ver 仍是
       上次的目标版本，设备以为自己已升级。 */
    if (!OtaA1ImageValid())
    {
        printf("A1 invalid, recover\r\n");

        /* A2 里还留着同一份镜像 → 直接重搬（同样校验通过才置 flag=A1） */
        if (OTA_Info.OTA_size > 0
            && OTA_Info.OTA_size <= (u32)(ST32_A2_PAGE_NUM * ST32_PAGE_SIZE)
            && OtaA2ImageValid())
        {
            /* 限流：连续失败达到上限就不再擦写 A1（保护 Flash 寿命） */
            if (OtaRetryCount() >= OTA_RETRY_MAX)
            {
                printf("recover limit\r\n");
                g_ota_lcd_err = OTA_ERR_VERIFY;
                g_ota_lcd_hold = 1;
                return;
            }

            printf("recover from A2\r\n");
            Cal_Block();
            Update_A1();
            if (OtaCopyVerify())
            {
                OtaWriteFlag(OTA_A1_FLAG);
                OtaRetryCountSet(0);
                OtaCloseUpdateGate();   /* 自愈成功同样关闭升级标志 */
                printf("recover OK, jump A1\r\n");
                LOAD_A((u32)(ST32_A1_SADDR));
                return;
            }
            OtaRetryCountSet(OtaRetryCount() + 1);
            printf("recover FAIL\r\n");
            g_ota_lcd_err = OTA_ERR_VERIFY;
            g_ota_lcd_hold = 1;
            return;
        }

        /* A2 也没有可用镜像 → 清版本标记，让版本闸失效，强制重新下载安装 */
        printf("A1/A2 invalid\r\n");
        OtaClearInstalledVersion();
        g_ota_lcd_err = OTA_ERR_IMAGE;
        g_ota_lcd_hold = 1;
    }
}
```

### 2.4 分支用到的四个校验/落盘函数（`boot.c:31-105`）

镜像头校验——**只看 SP 和复位向量是否落在合法区间**（`boot.c:31-42`）：

```c
static u8 OtaImageValidAt(u32 base)
{
    u32 sp = *(volatile u32 *)(base);    // 镜像前 4 字节 = 初始 SP
    u32 rv = *(volatile u32 *)(base + 4);// 镜像第 5~8 字节 = 复位向量

    if (sp < 0x20000000 || sp > 0x2000FFFF)   // SP 必须在 64KB RAM 内
        return 0;
    if (rv < (u32)(ST32_A1_SADDR)             // 复位向量必须落在 A1 区
        || rv >= ((u32)(ST32_A1_SADDR) + (u32)ST32_A1_PAGE_NUM * ST32_PAGE_SIZE))
        return 0;
    return 1;
}
```

> 原注释特别提醒：**地址宏整体必须加括号**。`ST32_A2_SADDR` 展开为「`0x08000000 + 0x3C000`」，若写成 `*(volatile u32 *)ST32_A2_SADDR`，转型只作用于 `0x08000000`，后面变成「指针 + 0x3C000 个 u32」，实际读 `0x080F0000`（越界），会把好固件判成坏包。

搬运校验——按固件长度 `memcmp` 两区（`boot.c:60-69`）：

```c
static u8 OtaCopyVerify(void)
{
    u32 len = OTA_Info.OTA_size;

    if (len == 0 || len > (u32)(ST32_A2_PAGE_NUM * ST32_PAGE_SIZE))
        return 0;
    if (memcmp((const void *)((u32)(ST32_A2_SADDR)), (const void *)((u32)(ST32_A1_SADDR)), len) != 0)
        return 0;
    return 1;
}
```

EEPROM 落盘辅助（`boot.c:71-105`）：

```c
/* 只写 OTA_flag（4 字节，偏移 OTA_OFF_FLAG） */
static u8 OtaWriteFlag(u32 flag)
{
    OTA_Info.OTA_flag = flag;
    return AT24CXX_Write(OTA_ADDR + OTA_OFF_FLAG, (uint8_t *)&OTA_Info.OTA_flag, 4);
}

/* 清掉 OTA_ver：表示「本机没有可用的已安装版本」。
   这样 putVersion 的版本闸不会再认为「已是最新」，下次联网会重新下载安装。 */
static void OtaClearInstalledVersion(void)
{
    memset(&OTA_Info.OTA_ver, 0, 32);
    AT24CXX_Write(OTA_ADDR + OTA_OFF_VER, (uint8_t *)&OTA_Info.OTA_ver[0], 32);
}

/* 安装成功 → 关闭升级标志（UPDATE_flag=0）：之后上电直接跑应用、不再联网核查 */
void OtaCloseUpdateGate(void)
{
    OTA_Info.UPDATE_flag = 0;
    AT24CXX_Write(OTA_ADDR + OTA_OFF_UPDATE, (uint8_t *)&OTA_Info.UPDATE_flag, 1);
}

/* 自愈重搬计数（1 字节 EEPROM）：成功清零，失败累加，达到 OTA_RETRY_MAX 后不再重搬 */
static u8 OtaRetryCount(void)
{
    u8 v = 0;
    AT24CXX_Read(OTA_RETRY_ADDR, &v, 1);
    return v;
}

static void OtaRetryCountSet(u8 v)
{
    AT24CXX_Write(OTA_RETRY_ADDR, &v, 1);
}
```

> 注意：`OtaCloseUpdateGate()` 只有分支①搬运成功（`boot.c:172`）和分支②自愈成功（`boot.c:211`）**两条成功路径**调用。`boot.h:102-104` 的注释仍写「main.c 在放弃升级时也调用它」，与当前代码不符（按约定只报告，未改注释）。

### 2.5 搬运：Cal_Block() + Update_A1()（`boot.c:280-343`）

```c
void Cal_Block(void) // 计算读取的块数量以及最后一块的字节数
{
    /* 护栏：size 非法时不计算（Update_A1 会跳过搬运），避免 BlockNB-1 下溢 */
    if (OTA_Info.OTA_size == 0 || OTA_Info.OTA_size > (u32)(ST32_A2_PAGE_NUM * ST32_PAGE_SIZE))
    {
        UpDataA.BlockNB = 0;
        UpDataA.size = 0;
        return;
    }

    if ((OTA_Info.OTA_size % ST32_PAGE_SIZE) == 0)   // 整页：size=每块半字数(1024)
    {
        UpDataA.BlockNB = OTA_Info.OTA_size / ST32_PAGE_SIZE;
        UpDataA.size = ST32_PAGE_SIZE / 2;
    }
    else                                             // 非整页：多算一块，按半字向上取整
    {
        UpDataA.BlockNB = OTA_Info.OTA_size / ST32_PAGE_SIZE + 1;
        UpDataA.size = OTA_Info.OTA_size % ST32_PAGE_SIZE;
        if ((UpDataA.size % 2) == 0)
            UpDataA.size = (UpDataA.size / 2);
        else
            UpDataA.size = (UpDataA.size / 2) + 1;
    }
}

// 更新A1区：逐页从 A2 读出、写入 A1
void Update_A1(void)
{
    u16 i = 0;
    u32 addrW = 0, addrR = 0;

    if (UpDataA.BlockNB == 0)   /* 护栏：没有有效块就不搬运 */
        return;

    for (i = 0; i < UpDataA.BlockNB-1; i++)   // 前 BlockNB-1 整页
    {
        addrR = ST32_A2_SADDR + i * ST32_PAGE_SIZE;
        STMFLASH_Read(addrR, (u16 *)&UpDataA.Updatabuff[0], ST32_PAGE_SIZE / 2);
        addrW = ST32_A1_SADDR + i * ST32_PAGE_SIZE;
        STMFLASH_Write(addrW, (u16 *)&UpDataA.Updatabuff[0], ST32_PAGE_SIZE / 2);
    }

    // 写最后一块（可能是残页）
    i = UpDataA.BlockNB - 1;
    addrR = ST32_A2_SADDR + i * ST32_PAGE_SIZE;
    STMFLASH_Read(addrR, (u16 *)&UpDataA.Updatabuff[0], UpDataA.size);
    addrW = ST32_A1_SADDR + i * ST32_PAGE_SIZE;
    STMFLASH_Write(addrW, (u16 *)&UpDataA.Updatabuff[0], UpDataA.size);
}
```

### 2.6 跳转：LOAD_A()（`boot.c:237-278`）

```c
__asm void MSR_MSP(uint32_t addr)
{
    MSR MSP, r0    // addr传入r0，写入MSP主栈指针
     BX r14        // 返回
}

void LOAD_A(uint32_t addr)
{
    uint32_t vtor;
    printf("A zone %x,%x,%x\r\n", (*(vu32 *)(addr)), addr, (*(vu32 *)(addr + 4)));
    if ((*(uint32_t *)addr >= 0x20000000) && (*(uint32_t *)addr <= 0x2000FFFF)) // SP 在 RAM 范围内才跳
    {
        vtor = SCB->VTOR;
        printf("vtor %x\n", vtor);              // ← 只打印不写回！向量表仍指向 B 区（见 §6.2）
        MSR_MSP(*(uint32_t *)addr);             // 设置 SP
        load_A = (load_a) * (uint32_t *)(addr + 4); // 函数指针指向 A 区复位向量
        printf("load progarm A is ok\r\n");
        BootLoader_Clear();                     // 清除 B 区使用的资源
        load_A();                               // 改变 PC，跳转到 A 区复位向量
    }
    else
        printf("load progarm A is fail\r\n");   // SP 非法：打印后返回（自愈入口）
}

void BootLoader_Clear(void)
{
    USART_DeInit(USART1);
    USART_DeInit(USART2);
    WRCommandH(0x01);   // LCD 清屏
}
```

**SP 非法时 LOAD_A 只打印并返回**，调用方落回 OTA 状态机继续重试——这就是「A1 烧坏了也能自愈/重下」的机制基础。

---

## 3. OTA 状态机（main 大循环）

### 3.1 状态与错误码定义

状态（`USER/main.h:25-31`）：

```c
#define OTA_STATE_WAIT_NET        0   // 等 4G 模组 AT 应答
#define OTA_STATE_ONLINE          1   // 网络在线，可发请求
#define OTA_STATE_SEND_VERSION    2   // 发 putVersion
#define OTA_STATE_HANDLE_REPLY    3   // 等并处理平台回包（下载也在这个态）
#define OTA_STATE_REBOOT          4
#define OTA_STATE_AT_OK           5   // AT 通了，等 MQTT 在线
#define OTA_STATE_MQTTCID_OK      6   // （已并入 AT_OK 流程，暂未使用）
```

LCD 错误类型（`HARDWARE/Lcd12864/Display.h:66-73`）：

```c
#define OTA_ERR_NET         0   /* 网络连接超时：AT 无应答 / 等待上线超时 */
#define OTA_ERR_TIMEOUT     1   /* 请求超时：等平台回包超时重试超限 */
#define OTA_ERR_LINK        2   /* 连接中断：升级过程中掉线超限 */
#define OTA_ERR_DATA        3   /* 数据校验失败：CRC / 长度 / 序号 */
#define OTA_ERR_IMAGE       4   /* 固件包无效：A2 镜像头非法 / A1、A2 均无可用镜像 */
#define OTA_ERR_VERIFY      5   /* 写入校验失败：搬运 memcmp 不一致 / 自愈校验失败 */
#define OTA_ERR_SIZE        6   /* 固件大小错误：OTA_size 非法 */
#define OTA_ERR_EEPROM      7   /* 存储异常：24C02 自检失败（OTA 被关闭） */
```

`gprs_rx_datas()` 的返回值（`HARDWARE/GPRS/gprs.h:47-66`），主循环全靠它驱动：

```c
#define GPRS_RX_RET_PARSE_FAILED         0     // 解析失败（注意：0 被主循环当"正常但无事"）
#define GPRS_RX_RET_NO_UPDATE            1     // 平台说无需升级 → 跳 A1
#define GPRS_RX_RET_START_UPDATE         2     // 开始下载
#define GPRS_RX_RET_DOWNLOAD_DONE        3     // 下载完成 → 复位生效
#define GPRS_RX_RET_DOWNLOAD_CONTINUE    4     // 当前包写入成功，继续
#define GPRS_RX_RET_AT_OK				 5     // 模组 AT 应答 OK
#define GPRS_RX_RET_MQTTCID_OK			 6
#define GPRS_RX_RET_NO_DATA              -100  // 完全没数据（唯一参与超时计时的返回值）

// 关键错误返回值
#define GPRS_RX_ERR_CRC_RETRY_EXCEEDED   -19   // 同一包 CRC 连续失败超限
#define GPRS_RX_ERR_EEPROM_WRITE         -20
#define GPRS_RX_ERR_PACKET_SHORT         -42
#define GPRS_RX_ERR_PACKET_SIZE          -43   // 单包长度非法（0 或超过 512）
#define GPRS_RX_ERR_LENGTH_OVERFLOW      -44   // 累计长度超出声明固件大小
#define GPRS_RX_ERR_SEQ_RETRY_EXCEEDED   -45   // 包序号不符重试超限
```

### 3.2 放弃升级的唯一出口：OtaGiveUpAndRunApp()（`main.c:32-51`）

所有「放弃升级」（AT 60s 超时、请求重试超限、掉线超限、CRC/接收错误超限）都汇到它：

```c
/* OTA 放弃升级：显示错误类型 → 停留 3 秒 → 直接跳 A1 应用（不返回）。
   ① 失败不改升级标志：UPDATE_flag 保持 1，平台/应用下次仍可继续升级。
      "关闭升级标志"只保留给两个成功路径（A2→A1 搬运校验通过、开机自愈成功，见 boot.c）。
   ② 不复位、不写 EEPROM：复位会让 B 区重新进入 OTA 状态机 → 再失败 → 再复位，
      平台长期不可用时设备永远进不了应用（复位循环）；直接跳转没有这个问题。
   ③ 3 秒拆两段并在中间喂狗，避开 3.18s 看门狗窗口，让现场看清错误类型。
   ④ 代价：不再复位 ⇒ GM4G_Init() 的 PA1 复位脉冲不执行，4G 模组不被硬复位（与既有
      "无需升级 → JumpToApp()"路径一致）；USART1/USART2 由 BootLoader_Clear() 复位、
      LCD 由 WRCommandH(0x01) 清屏，其余外设（DMA1_CH6/EXTI9_5/IWDG/SysTick）保持
      B 区状态，交由 A1 应用初始化。
   ⑤ LOAD_A 自带 SP 合法性保护：A1 不可运行时只打印 load progarm A is fail 并返回，
      于是落回 main 的 while(1) OTA 状态机继续联网重试（自愈语义保留）。 */
static void OtaGiveUpAndRunApp(uint8_t kind)
{
    ota_show_error(kind);      /* 先点亮错误类型：第3行"更新出错"、第4行类型 */
    delay_ms(1500);            /* 停留 3 秒，让现场看清原因（分段并喂狗） */
    IWDG_Feed();
    delay_ms(1500);
    LOAD_A(ST32_A1_SADDR);     /* 跳 A1：不关升级标志、不复位；函数不返回 */
}
```

**为什么不做「关标志 + 复位」**：`main.c:25-26` 注释写得很直接——复位后 `OTA_flag` 仍是 A1、`UPDATE_flag` 仍是 1，B 区会重新进入 OTA 状态机，设备永远进不了应用（复位循环）。

因此失败后 `UPDATE_flag` 保持 1：**之后每次上电都会重走 OTA 流程**，关闸只能靠平台下发 `OTAEnableFlagConfig=false`，或真实升级成功后自动置 0。

### 3.3 主循环逐段讲解（`main.c:134-348`）

主循环每轮先喂狗、收一次数据，然后先做**掉线检查**（`main.c:136-159`）：

```c
while (1)
{
    IWDG_Feed(); // 喂狗
    gprs_return=gprs_rx_datas();          // 收+解析一帧，返回值驱动整个状态机

    if(gprsonlineflag==0 && SYS_state!=OTA_STATE_WAIT_NET && SYS_state!=OTA_STATE_AT_OK)
    {
        // 升级过程中掉线：计入会话级预算，重连后从在途 offset 重发请求
        // （见 case OTA_STATE_ONLINE），掉线次数超限才放弃并引导旧程序
        if(SYS_state==OTA_STATE_SEND_VERSION || SYS_state==OTA_STATE_HANDLE_REPLY)
        {
            link_loss_count++;
            printf("link lost during update, retry %d/%d\r\n", link_loss_count, link_loss_max);
            if(link_loss_count>=link_loss_max)      // 默认 5 次
            {
                ota_step5();
                printf("link lost too many times, boot old program\r\n");
                OtaGiveUpAndRunApp(OTA_ERR_LINK);
            }
        }
        SYS_state = OTA_STATE_WAIT_NET;    // 退回等网状态，计数全部清零
        req_timeout_tick = 0;
        req_retry_count = 0;
        rx_err_count = 0;
        continue;
    }
```

**WAIT_NET 态**：反复发 AT，60s 无应答就放弃（`main.c:161-183`）：

```c
    if(SYS_state==OTA_STATE_WAIT_NET)
    {
        if(gprs_return==GPRS_RX_RET_AT_OK)
        {
            SYS_state = OTA_STATE_AT_OK;
            continue;
        }
        /* 模组无应答保护：未插卡 / 天线断 / 模组坏时不能无限等下去，
           否则应用永远起不来。60s 无 AT 应答 → 先尝试直接运行应用；
           若应用不可运行（LOAD_A 返回），约 30s 后再试一次。 */
        if(at_wait_tick >= at_wait_max)            // at_wait_max=200，200*300ms=60s
        {
            printf("AT timeout\r\n");
            OtaGiveUpAndRunApp(OTA_ERR_NET);
            at_wait_tick = at_wait_max - 100;      // 下次 30s 后再试
            delay_ms(500);                         // 限速，避免空转刷屏
            continue;
        }
        at_wait_tick++;
        At_init();       // 发 "usr.cn#AT\r\n"
        delay_ms(300);
        continue;
    }
```

**AT_OK 态**：等 MQTT 上线（`gprsonlineflag` 由 4G 模块 LINKA 引脚的 EXTI 中断置位），约 40s 等不到就放弃（`main.c:195-216`）：

```c
    if(SYS_state==OTA_STATE_AT_OK)
    {
        if(gprsonlineflag==0)
        {
            delay_ms(200);
            online_tick++;
            if(online_tick >= 200)                 // 200*200ms=40s
            {
                online_tick = 0;
                ota_step5();
                OtaGiveUpAndRunApp(OTA_ERR_NET);
            }
            continue;
        }
        if(gprsonlineflag==1)
        {
            SYS_state=OTA_STATE_ONLINE;
        }
    }
```

**switch 核心**（`main.c:217-347`），重点是 ONLINE 态的**断点续传**两个分支（`main.c:218-242`）：

```c
    switch (SYS_state){
        case OTA_STATE_ONLINE:
            // 掉线重连时若已在重连窗口暂存到平台回包（putVersion 回执），
            // 直接消费它：不重新协商、不重发 putVersion
            if(gprs_rx_defer_pending() && !gprs_download_pending())
            {
                printf("use stashed version reply\r\n");
                ota_step2();
                SYS_state=OTA_STATE_HANDLE_REPLY;
                break;
            }
            // 下载会话在途（例如掉线后重连）：直接重发在途的 offset 请求，
            // 不重新协商、不从 0 重来
            if(gprs_download_pending() && gprs_resend_last_request())
            {
                printf("resume pending offset request\r\n");
                SYS_state=OTA_STATE_HANDLE_REPLY;
            }
            else                                   // 全新会话：发版本询问
            {
                ota_step2();
                delay_ms(1000);
                SYS_state=OTA_STATE_SEND_VERSION;
            }
            break;
        case OTA_STATE_SEND_VERSION:
                cjson_pub_version();               // 发 putVersion
                SYS_state=OTA_STATE_HANDLE_REPLY;
            break;
```

HANDLE_REPLY 态是回包处理中枢，包含**三套独立计数**（`main.c:247-338`）：

```c
        case OTA_STATE_HANDLE_REPLY:
          if(gprs_return==GPRS_RX_RET_NO_DATA || gprs_return==GPRS_RX_RET_PARSE_FAILED)
          {
              req_timeout_tick++;                  // ① 请求超时计数：50*100ms=5s 一次重发
              if(req_timeout_tick>=req_timeout_tick_max)
              {
                  req_timeout_tick = 0;
                  req_retry_count++;
                  if(gprs_resend_last_request())   // 重发缓存的上一条请求
                      printf("request timeout, resend last request (%d/%d)\r\n", ...);
                  else
                      printf("request timeout, no cached request (%d/%d)\r\n", ...);

                  if(req_retry_count>=req_retry_max)   // 5 次超限 → 放弃
                      OtaGiveUpAndRunApp(OTA_ERR_TIMEOUT);
              }
              delay_ms(100);
              break;
          }
          else
          {
              req_timeout_tick = 0;
              req_retry_count = 0;
          }
          if(gprs_return==GPRS_RX_ERR_CRC_RETRY_EXCEEDED)
          {
              crc_fail_count++;                    // ② CRC 失败计数（上限 5）
              ota_show_error(OTA_ERR_DATA);
              if(crc_fail_count>=crc_fail_max)
                  OtaGiveUpAndRunApp(OTA_ERR_DATA);
              break;
          }
          else if(gprs_return==GPRS_RX_RET_DOWNLOAD_CONTINUE)
          {
              crc_fail_count = 0;                  // 成功一包 → 清 CRC 计数
          }
          if(gprs_return < 0 && gprs_return != GPRS_RX_RET_NO_DATA
             && gprs_return != GPRS_RX_ERR_CRC_RETRY_EXCEEDED)
          {
              rx_err_count++;                      // ③ 其它接收错误计数（上限 5）
              ota_show_error(OTA_ERR_DATA);
              if(rx_err_count >= rx_err_max)
                  OtaGiveUpAndRunApp(OTA_ERR_DATA);
              break;
          }
          else if(gprs_return >= 0)
          {
              rx_err_count = 0;
          }
          if(gprs_return==GPRS_RX_RET_NO_UPDATE)   // 平台说无需升级
          {
              ota_step4();
              delay_ms(1000);
              JumpToApp();                         // 跳 A1
          }
           if(gprs_return==GPRS_RX_RET_START_UPDATE)
           {
               link_loss_count = 0;                // 新一轮下载会话：重置掉线预算
               ota_step3();
               progressbar_Init();
           }
           if(gprs_return==GPRS_RX_RET_DOWNLOAD_DONE)
           {
               ota_step6();                        // 下载完成提示
               delay_ms(1000);
               ota_step7();
               delay_ms(500);
               NVIC_SystemReset();                 // 设备重启 → 下次上电走 §2 分支①生效
           }
          break;
      case OTA_STATE_REBOOT:
          gprs_download_context_reset();
          NVIC_SystemReset();
          break;
```

---

## 4. 升级协议与收发代码

### 4.1 协议帧格式

设备主动请求、服务器才应答。上行 JSON 帧由 `gprs_send_datas()` 组装，前缀是 MQTT topic 号：

```text
上行：{topic},{JSON}              例：1,{"id":"...","ver":"v1.0",...,"body":{...}}
下行（版本回执）：{topic},{JSON}
下行（固件包）：  7,{JSON} + 512B 二进制数据 + 2B CRC16   ← JSON 与二进制同帧！
```

三个上行消息（`gprs_tx.c`）：

| OTACode | 方向 | 作用 |
|---|---|---|
| putVersion | 设备→服务器 | 报当前版本，回执带 `{size, version, upgrade}` |
| getFirmware | 设备→服务器 | 请求第 offset 包，回执带 512B 数据+CRC |
| （PUTERROR） | 设备→服务器 | 上报错误码 |

### 4.2 发送侧：gprs_send_datas()（`HARDWARE/GPRS/gprs_tx.c:93-198`）

参数结构体 `CJsonPara`（`gprs.h:81-93`）装着信封所有字段。每次发送（含重发）都**重新生成 id/ts**——原实现 id 写死 `"test-001"`，5 次超时重发内容完全一样，平台侧无法区分重发：

```c
/* ---- 消息标识：id 与 ts 每次发送（含超时重发）都重新生成 ----
   协议要求 id 在单设备时间窗内不重复、平台回包原样带回该 id（升级记录里的 MsgId 也取它） */
void gprs_next_msg_id(char *buf, u16 buflen)
{
    static int64_t s_last_ms = 0;
    int64_t ms = gprs_unix_ms_now();     // 13 位 Unix 毫秒，由 DS1302 日历推算

    if (ms <= s_last_ms)                 // 同一秒内：只保证递增且不重复
        ms = s_last_ms + 1;
    s_last_ms = ms;
    snprintf(buf, buflen, "%llu", (unsigned long long)ms);
}
```

组包本体（节选，`gprs_tx.c:100-117` + `:133-152` + `:173-197`）：

```c
void gprs_send_datas(u8 sta,CJsonPara para)
{
   static char json_str[512] = {'\0'};   // 512B 组包缓冲不放在栈上

    /* 每次发送（含超时重发）都刷新 id / ts */
    if (sta == SVERSION || sta == UPGRADE || sta == PUTERROR)
    {
        gprs_next_msg_id(para.id, bid);
        para.ts = gprs_unix_ms_now();
    }

    /* 新请求/超时重发前丢弃可能卡住的半帧，避免旧数据被拼进下一帧。 */
    if (sta == SVERSION || sta == UPGRADE)
        gprs_rx_frame_reset();

    // 缓存最近一次可重发请求（版本请求/下载请求）
    if (sta == SVERSION || sta == UPGRADE)
    {
        g_last_sta = sta;
        g_last_para = para;
        g_last_valid = 1;
    }

    switch(sta)
    {
        case UPGRADE: //请求bin文件块序号（getFirmware）
            sprintf(json_str,
                "%d,{"
                "\"id\":\"%s\","
                "\"ver\":\"%s\","
                "\"ts\":%llu,"
                "\"productKey\":\"%s\","
                "\"deviceKey\":\"%s\","
                "\"type\":\"%s\","
                "\"body\":{"
                "\"OTACode\":\"%s\","
                "\"paras\":{"
                "\"offset\":%d"
                "}"
                "}"
                "}"
                ,7,para.id,para.ver,para.ts,para.productKey,para.deviceKey,para.type,para.OTACode,para.offset
             );
            break;

        case SVERSION: //版本号上报（putVersion）——模板同上，paras 换成 "\"version\":\"%s\""
            ...
    }

  length = strlen(json_str);
  Usart2_Send(json_str, length);    // 通过 USART2 发给 4G 模组
}
```

重发入口就是重放缓存（`gprs_tx.c:200-208`）：

```c
int gprs_resend_last_request(void)
{
    if (!g_last_valid)
        return 0;
    gprs_send_datas(g_last_sta, g_last_para);   // id/ts 会在 send_datas 里重新生成
    return 1;
}
```

两个请求的构造（`gprs_rx.c:771-819`）：`cjson_pub_version()` 上报版本（**只有 `OTA_flag==A1` 时 OTA_ver 才代表运行版本**，待安装期间上报 `v0.0.0`），`cjson_pub_getbin(offset)` 请求第 offset 包。

### 4.3 接收侧：整帧重组与掉线暂存（`HARDWARE/GPRS/gprs_rx.c`）

接收的难点：**USART2 的 DMA 空闲帧 ≠ 一条完整 OTA 报文**。模组可能把 JSON、512B 固件块和 CRC 拆成多次空闲中断，也可能把连续报文一次送入（`gprs_rx.c:64-68` 注释）。所以 `gprs_rx_datas()` 从环形缓冲里只取一个完整帧，靠括号匹配找 JSON 结尾，再按 JSON 里声明的 size/offset 算出整帧应长（含二进制+CRC）凑齐才处理；凑不齐就等下一轮，缓冲溢出则重同步。

**掉线暂存**——下载会话未建立时到达的 putVersion 回执先存起来（`gprs_rx.c:31-42, 355-380`）：

```c
/* 掉线重连窗口（等 AT / 等网络上线）里到达的 putVersion 回执：先暂存，等状态机
   回到 HANDLE_REPLY 再解析。原实现把这类帧整帧丢弃，导致"请求已发、回包刚到就掉线"
   时平台回包白丢一次（现场表现为重连后一直 request timeout）。 */
#define GPRS_DEFER_BUF_LEN 512
static char s_defer_buf[GPRS_DEFER_BUF_LEN];
static u16 s_defer_len = 0;

u8 gprs_rx_defer_pending(void)
{
    return (s_defer_len != 0) ? 1u : 0u;
}
```

```c
    /* 握手期间仅暂存 putVersion；完整下载帧在下载会话中正常处理。 */
    if (SYS_state == OTA_STATE_WAIT_NET || SYS_state == OTA_STATE_AT_OK)
    {
        ...
        if (!from_defer && str_contains(s_uart_rx2_frame, "putVersion"))
        {
            if (s_defer_len == 0 && u2rxcnt <= sizeof(s_defer_buf))
            {
                memcpy(s_defer_buf, s_uart_rx2_frame, u2rxcnt);
                s_defer_len = u2rxcnt;
                printf("stash putVersion reply (%u bytes), parse after reconnect\r\n", u2rxcnt);
            }
            else
                printf("stash busy, drop this putVersion reply\r\n");
        }
        return GPRS_RX_RET_NO_DATA;
    }
```

下载会话上下文（`gprs_rx.c:22-61`），跨包保留、会话结束统一清零：

```c
static u32 s_dl_addr = ST32_A2_SADDR;        // 当前写地址
static u32 s_dl_lastaddr = ST32_A2_SADDR;    // 上一包写地址
static u16 s_dl_expected_offset = 0;         // 会话内期望收到的包序号
static u32 s_dl_written_bytes = 0;           // 会话内已写入 A2 的字节数
static u8  s_dl_seq_retry_cnt = 0;           // 序号不符的连续重发次数
static u8  s_dl_crc_retry_cnt = 0;           // 同一包 CRC 失败次数

u8 gprs_download_pending(void)
{
    return (s_dl_expected_offset != 0) ? 1 : 0;  // ≠0 即「下载在途」
}
```

### 4.4 版本回执决策：cjson_upgrade()（`gprs_rx.c:522-584`）

升级 = **平台 upgrade 标志 ∧ 版本闸**，两个条件分开打印便于现场排查：

```c
    // 判断是否需要升级 = 平台标志 ∧ 版本闸。
    // 设备升级标志 UPDATE_flag 已在 main.c 进入本状态机前判过（为 0 时根本不会发 putVersion）
    upgrade_flag = (cJSON_GetNumberValue(upgrade) != 0) ? 1 : 0;
    version_same = (strcmp((char *)OTA_Info.OTA_ver, ver->valuestring) == 0) ? 1 : 0;

    if (upgrade_flag && !version_same)
    {
        OtaParas.size = cJSON_GetNumberValue(size);
        strncpy(OtaParas.ver, ver->valuestring, ...);   // 目标版本号

        // 固件大小合法性：非 0，且不超过 A2 区容量（100 页 × 2KB = 200KB）
        if (OtaParas.size == 0 || OtaParas.size > (u32)(ST32_A2_PAGE_NUM * ST32_PAGE_SIZE))
        {
            printf("invalid firmware size: %u, abort upgrade\r\n", OtaParas.size);
            return 0;   // 视为无需升级 → 主流程跳回 A1（旧程序）
        }

        // offset = 包序号，count = 总包数：按 512 字节/包换算
        if ((OtaParas.size % BYTESIZE) != 0)
        {
            OtaParas.count = (u16)(OtaParas.size / BYTESIZE + 1);
            OtaParas.remaining = (u16)(OtaParas.size % BYTESIZE); // 最后一包字节数
        }
        else
        {
            OtaParas.count = (u16)(OtaParas.size / BYTESIZE);
            OtaParas.remaining = BYTESIZE;
        }

        // 新一轮下载会话：复位写指针与进度
        // （掉线重连不会走到这里：ONLINE 状态会先重发在途请求，见 main.c）
        gprs_download_context_reset();

        process = OtaParas.count;     // 进度条分母＝总包数
        ProgressBar_Update(0);
        cjson_pub_getbin(0);          // 请求第 0 包
        return 1;                     // → 主循环 GPRS_RX_RET_START_UPDATE
    }
    return 0;                         // 无需升级 → GPRS_RX_RET_NO_UPDATE → 跳 A1
```

### 4.5 逐包下载：cjson_download_reply()（`gprs_rx.c:587-767`）

每包回执 = `7,{JSON} + 数据 + 2B CRC16`。处理顺序：**序号闸门 → 长度闸门 → CRC → 写 A2 → 请求下一包 / 末包落盘**。

```c
u8 cjson_download_reply(cJSON * root, char *message, u16 message_len)
{
    Offset = cJSON_GetNumberValue(bOffset);   // 包序号
    Size   = cJSON_GetNumberValue(bsize);     // 本包字节数

    // ---- 序号闸门：只接受本会话期望的那一包；重复包/乱序包一律丢弃，
    //      然后"重新申请同一个请求"（不是从 0 重来）
    if (Offset != s_dl_expected_offset)
    {
        printf("packet offset mismatch: got %u, expect %u\r\n", Offset, s_dl_expected_offset);
        if (s_dl_seq_retry_cnt < 5)
        {
            s_dl_seq_retry_cnt++;
            cjson_pub_getbin(s_dl_expected_offset);      // 重发期望序号
            return GPRS_RX_RET_DOWNLOAD_CONTINUE;        // 按"仍在下载"处理，不报错
        }
        return GPRS_RX_ERR_SEQ_RETRY_EXCEEDED;           // 超限 → 主流程计数放弃
    }
    s_dl_seq_retry_cnt = 0;

    // ---- 包长闸门：单包不得为 0，且不得超过 512 字节
    if (Size == 0 || Size > BYTESIZE)
        return GPRS_RX_ERR_PACKET_SIZE;

    // ---- 累计长度闸门：已写字节 + 本包不得越过协议声明的固件大小
    if ((s_dl_written_bytes + Size) > OtaParas.size)
        return GPRS_RX_ERR_LENGTH_OVERFLOW;

    if ((u32)Size + 2 > message_len)                     // 帧体必须装得下数据+CRC
        return GPRS_RX_ERR_PACKET_SHORT;

    // ---- CRC 校验（CRC16-CCITT-FALSE，低字节在前）
    crc = CRC16_CCITT_FALSE((u8 *)&message[0], Size);
    if (((crc >> 8) & 0xff) != message[Size + 1] || (crc & 0xff) != message[Size])
    {
        // 换了包序号就清零重试计数；同一包连续 5 次 CRC 失败 → 上抛
        if (s_dl_last_offset != Offset)
        {   s_dl_crc_retry_cnt = 0;  s_dl_last_offset = Offset;  }
        else
            s_dl_crc_retry_cnt++;
        if (s_dl_crc_retry_cnt < 5)
            cjson_pub_getbin(Offset);                    // 重新请求同一包
        return GPRS_RX_ERR_CRC_RETRY_EXCEEDED;
    }

    // ---- 写 A2 区（Size 折成半字数；写指针按字节推进）
    orig_size = Size;
    if ((Size % 2) != 0) Size = Size / 2 + 1;
    else                 Size = Size / 2;

    s_dl_addr += Size * 2;
    STMFLASH_Write(s_dl_lastaddr, (u16 *)&message[0], Size);
    s_dl_lastaddr = s_dl_addr;
    s_dl_written_bytes += orig_size;
    s_dl_expected_offset++;                              // 推进会话进度

    if (Offset < (OtaParas.count - 1))                   // 中间包：请求下一包
    {
        cjson_pub_getbin(Offset + 1);
        ProgressBar_Update(Offset * 100 / process);
    }
    else if (Offset == (OtaParas.count - 1))             // 最后一包：落盘并收尾
    {
        // 收尾一致性校验：累计写入必须等于协议声明的固件大小，
        // 否则不落盘 OTA_flag，交由主流程计数并放弃（引导旧程序）
        if (s_dl_written_bytes != OtaParas.size)
            return GPRS_RX_ERR_LENGTH_OVERFLOW;

        gprs_download_context_reset();
        ... /* 填 OTA_Info.OTA_ver / OTA_size */
        // 顺序：先写 OTA_size、再写 OTA_ver，最后才写 OTA_flag。
        // 24C02 是逐字节 I2C 写，中途掉电时只会停在「flag 仍是 A1」的安全状态；
        // 若先写 flag，就可能留下 flag=A2 而 size=0 的坏状态
        if (AT24CXX_Write(OTA_ADDR + OTA_OFF_SIZE, (uint8_t *)&OTA_Info.OTA_size, 4) != AT24CXX_OK)
            return GPRS_RX_ERR_EEPROM_WRITE;
        if (AT24CXX_Write(OTA_ADDR + OTA_OFF_VER, (uint8_t *)&OTA_Info.OTA_ver[0], 32) != AT24CXX_OK)
            return GPRS_RX_ERR_EEPROM_WRITE;
        OTA_Info.OTA_flag = OTA_A2_FLAG;
        if (AT24CXX_Write(OTA_ADDR + OTA_OFF_FLAG, (uint8_t *)&OTA_Info.OTA_flag, 4) != AT24CXX_OK)
            return GPRS_RX_ERR_EEPROM_WRITE;

        cjson_pub_putError();   // （函数名历史遗留）实际是上报升级成功
        ProgressBar_Update(100);
        return 1;               // → 主循环 GPRS_RX_RET_DOWNLOAD_DONE → 复位
    }
    return 0;
}
```

---

## 5. 底层 Flash 读写与一个已知 bug（`HARDWARE/STMFLASH/stmflash.c`）

`STMFLASH_Write()` 按 2KB 扇区处理：写入前把整个扇区读进 `STMFLASH_BUF`，有非 `0xFFFF` 就擦除改写，然后跨扇区循环（`stmflash.c:37-80`）：

```c
void STMFLASH_Write(u32 WriteAddr,u16 *pBuffer,u16 NumToWrite)  // NumToWrite: 半字数
{
    if(WriteAddr<STM32_FLASH_BASE||(WriteAddr>=(STM32_FLASH_BASE+1024*STM32_FLASH_SIZE)))return;
    FLASH_Unlock();
    offaddr=WriteAddr-STM32_FLASH_BASE;
    secpos=offaddr/STM_SECTOR_SIZE;            // 扇区号（2KB/扇区）
    secoff=(offaddr%STM_SECTOR_SIZE)/2;        // 扇区内偏移（半字单位）
    secremain=STM_SECTOR_SIZE/2-secoff;        // 扇区剩余空间（半字单位）
    if(NumToWrite<=secremain)secremain=NumToWrite;
    while(1)
    {
        STMFLASH_Read(secpos*STM_SECTOR_SIZE+STM32_FLASH_BASE,STMFLASH_BUF,STM_SECTOR_SIZE/2);
        for(i=0;i<secremain;i++)
            if(STMFLASH_BUF[secoff+i]!=0XFFFF)break;   // 有数据 → 需要擦除
        if(i<secremain)   // 需要擦除：整扇区擦除后合并写入
        {
            FLASH_ErasePage(secpos*STM_SECTOR_SIZE+STM32_FLASH_BASE);
            for(i=0;i<secremain;i++) STMFLASH_BUF[i+secoff]=pBuffer[i];
            STMFLASH_Write_NoCheck(secpos*STM_SECTOR_SIZE+STM32_FLASH_BASE,STMFLASH_BUF,STM_SECTOR_SIZE/2);
        }else STMFLASH_Write_NoCheck(WriteAddr,pBuffer,secremain);  // 已擦除：直接写
        if(NumToWrite==secremain)break;
        else   // 跨扇区续写
        {
            secpos++;
            secoff=0;
            pBuffer+=secremain;    // 指针偏移（单位：半字，正确）
            WriteAddr+=secremain;  // 写地址偏移 ← ★ 已知 bug，见下
            NumToWrite-=secremain;
            if(NumToWrite>(STM_SECTOR_SIZE/2))secremain=STM_SECTOR_SIZE/2;
            else secremain=NumToWrite;
        }
    };
    FLASH_Lock();
}
```

**★ bug：`stmflash.c:73` 的 `WriteAddr+=secremain;` 单位错了。** `secremain` 是**半字数**（`:49`），而 `WriteAddr` 是**字节地址**（`:23` 的 `WriteAddr+=2` 可证）。跨扇区时应加 `secremain*2` 字节。触发条件：一次写入跨 2KB 扇区边界、且下一扇区为空白——**A2 首次下载正是全 `0xFF`，每 4 个 512B 包就有一个跨扇区，必现**；数据会被写到偏小 `secremain*2` 字节的位置，且分包 CRC 查不出来，要到运行新固件时才暴露。方向：`WriteAddr += secremain * 2;` 并补跨扇区回读校验。（本文未改代码。）

---

## 6. 剩余风险（未修，按影响排序）

### 6.1 上面那个跨扇区 bug

真实下载固件（A2 首次写入）时必现，是 OTA 路径上最高优先级的隐患。

### 6.2 跳转前未重定位向量表、外设状态残留

`LOAD_A()` 里 `vtor = SCB->VTOR;` **只打印不写回**（`boot.c:253-254`），本工程 `system_stm32f10x.c:128` 的 `VECT_TAB_OFFSET = 0x0`，向量表仍指向 B 区。A1 应用必须在 `SystemInit` 里自行设 VTOR=`0x0800E000`（此前核对确认 A1 工程已如此设置），否则运行期任何中断都会跳回 B 区。

`BootLoader_Clear()` 只复位 USART1/USART2 和清屏，DMA1_CH5/CH6、EXTI9_5、SysTick、IWDG、LCD GPIO 全部保持 B 区状态（`main.c:38-41` 注释明确承认）。两条跳转路径要求不同：

- **「无需升级 → JumpToApp()」发生在 `IWDG_Init()` 之前**（`main.c:128` vs `:131`），看门狗没开，外设要求宽松；
- **「放弃升级 → OtaGiveUpAndRunApp()」时 IWDG 已启动且无法关闭**，A1 必须在 3.18s 内开始喂狗。

### 6.3 其它

- **在线判断**：`gprsonlineflag`（`4G.c:9`）无初值，`GM4G_Init()` 不读 PC5 电平，只在 EXTI 双沿跳变时更新。上电时模块已在线且无跳变 → 标志恒 0 → 约 40s 后放弃升级。
- **解析失败返回 0 被当正常**（`gprs.h:48` + `main.c:249-278`）：收到垃圾帧（`PARSE_FAILED=0`）不会推进超时计数，只有「完全没数据」(-100) 才计时。
- **无整包校验**：只有逐包 CRC16 + A1/A2 `memcmp`（后者只能证明两段一致，不能证明内容正确）；`OtaPara.signMethod/dProtocol/streamId`（`gprs.h:73-75`）至今未使用，平台侧 `FileMd5` 字段可作为整包校验落点。
- **跳转直跳路径只查栈顶 SP，未校验复位向量**；镜像头校验（`OtaImageValidAt`）只在搬运/自愈路径调用。
- **`boot.h:102-104` 注释与代码不一致**（见 §2.4）。

---

## 7. 阅读路线建议

按依赖顺序读，配合本文对应小节：

1. `HARDWARE/BOOT/boot.h` —— 分区宏、OTA_InfoCB、编译期断言（§1）
2. `USER/main.c` —— 上电初始化、放弃升级、OTA 状态机（§2.2、§3）
3. `HARDWARE/BOOT/boot.c` —— 三分支、搬运、跳转（§2.3~§2.6）
4. `HARDWARE/GPRS/gprs.h` —— 协议结构体与返回码（§3.1、§4.1）
5. `HARDWARE/GPRS/gprs_rx.c` —— 帧重组、下载闸门、EEPROM 落盘（§4.3~§4.5）
6. `HARDWARE/GPRS/gprs_tx.c` —— 组包与重发（§4.2）
7. `HARDWARE/STMFLASH/stmflash.c` —— 底层读写 + 已知 bug（§5）

---

*本文基于当前工作副本逐文件只读核对；未修改任何源码。*
