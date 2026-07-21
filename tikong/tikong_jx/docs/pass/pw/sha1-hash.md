# `sha1_hash` 算法说明（结合本工程代码）

更新时间：2026-07-19

> **源码**：[Middlewares/sha1.c](../../Middlewares/sha1.c)、[Middlewares/sha1.h](../../Middlewares/sha1.h)  
> **业务用法**：四位口令见 [password-4digit-auth.md](./password-4digit-auth.md)；通行验签见 `App/Pass/cmd.c`

---

## 1. 什么是 SHA-1

**SHA-1**（Secure Hash Algorithm 1）是一种**单向哈希（摘要）算法**：

- 任意长度的输入（一段文字、一串字节）→ 固定长度的输出  
- SHA-1 的输出是 **160 bit**，常写成 **40 个十六进制字符**（每位 hex = 4 bit，40×4=160）  
- **相同输入 → 相同输出**；输入改一个比特，输出几乎面目全非（雪崩效应）  
- **单向**：从输出很难反推出输入；所以它是「指纹 / 校验用」，**不是加密**（没有密钥、也不能解密还原原文）

直观理解：给文件或字符串盖一个固定长度的「指纹」。核对时只要再算一次指纹，相同则认为内容一致（在工程常用场景下）。

本工程不直接对外暴露完整 SHA-1 状态机，而是通过封装函数 `sha1_hash`：传入 C 字符串，得到那 40 个 hex 字符。下文从该函数讲起，再对照底层实现。

---

## 2. 本工程入口：`sha1_hash` 干什么

`sha1_hash` 是对 **SHA-1** 的薄封装：传入一个 C 字符串，得到 **40 个十六进制字符**（表示 160 bit 摘要）。

| 项 | 说明 |
| --- | --- |
| 输入 | `const char *source`（按 `strlen` 计长度，**不含**结尾 `'\0'`） |
| 输出 | `char *output_hex`：40 个 **大写** hex 字符，并带 `'\0'`（调用方缓冲区建议 ≥ 41） |
| 返回 | 成功 `40`；失败 `-1`（`SHA1Result` 失败，如上下文已损坏） |

声明：

```21:21:Middlewares/sha1.h
int sha1_hash(const char *source, char *output_hex); // 直接生成 40字节 HEX 串
```

实现：

```153:169:Middlewares/sha1.c
int sha1_hash(const char *source, char *output_hex)
{
    SHA1Context sha;
    int i;

    SHA1Reset(&sha);
    SHA1Input(&sha, (const uint8_t *)source, strlen(source));

    if (!SHA1Result(&sha))
        return -1;

    for (i = 0; i < 5; i++)
    {
        sprintf(output_hex + i * 8, "%08X", sha.Message_Digest[i]);
    }
    return 40; 
}
```

三步对应关系：

```text
SHA1Reset   → 清空上下文，写入标准初始常量
SHA1Input   → 把 source 的每个字节喂进去（满 64 字节就压缩一块）
SHA1Result  → 补齐填充并收尾，得到 5 个 uint32
sprintf×5  → 每个字打成 8 位大写 hex，拼成 40 字符
```

---

## 3. 调用方怎么用（本工程）

口令路径（`qr_comm.c`）：

```text
auth_src = "设备码,公钥,YYYYMMDDhhmm"
sha1_hash(auth_src, auth_dic_hash);   // 得到大写 40 hex
再转小写 → 取前 10 字符 → ASCII 累加 → sum
```

通行命令验签（`cmd.c`）多处同样调用 `sha1_hash`，再与报文里的摘要字段比对（常取前若干位）。

注意：

- `sha1_hash` 输出是 **`%08X` 大写**；口令路径会再转成小写再加工。  
- 这是**哈希**，不是加密：不能从 40 字符反推出源串；相同源串必得相同结果。

---

## 4. 上下文结构（读代码入口）

```7:15:Middlewares/sha1.h
typedef struct {
    uint32_t Message_Digest[5];   // 5 * 32 = 160 位输出
    uint32_t Length_Low;          // 消息长度（低 32 位，bit）
    uint32_t Length_High;         // 消息长度（高 32 位，bit）
    uint8_t  Message_Block[64];   // 512 位缓冲区
    int      Message_Block_Index; 
    int      Computed;            
    int      Corrupted;           
} SHA1Context;
```

| 字段 | 含义 |
| --- | --- |
| `Message_Digest[5]` | 五个 32 位寄存器，最终就是 160 bit 摘要 |
| `Length_Low/High` | 已处理消息的**比特**长度（每喂一字节 `+8`） |
| `Message_Block[64]` | 当前未满的一块（512 bit） |
| `Message_Block_Index` | 块内已写入字节数；到 64 就压缩 |
| `Computed` / `Corrupted` | 是否已出结果 / 是否出错（出错后禁止再喂） |

---

## 5. 底层四步（结合函数）

### 5.1 `SHA1Reset` — 初始化

把五个摘要寄存器设为 SHA-1 标准初值，长度与块索引清零：

```17:21:Middlewares/sha1.c
    context->Message_Digest[0] = 0x67452301;
    context->Message_Digest[1] = 0xEFCDAB89;
    context->Message_Digest[2] = 0x98BADCFE;
    context->Message_Digest[3] = 0x10325476;
    context->Message_Digest[4] = 0xC3D2E1F0;
```

### 5.2 `SHA1Input` — 喂数据

逐字节写入 `Message_Block`；每凑满 **64 字节** 调用一次 `SHA1ProcessMessageBlock`。  
同时用 `Length_Low/High` 累计**比特**长度（供填充阶段写入）。

### 5.3 `SHA1ProcessMessageBlock` — 压缩一块

标准 SHA-1 块变换（接手只需知道「一块 64 字节进，更新五个寄存器」）：

1. 前 16 个字：由 64 字节按**大端**拼成 `W[0..15]`  
2. 扩展到 `W[16..79]`：异或 + 循环左移 1  
3. 80 轮：用 `A,B,C,D,E` 与常量 `K[]`、不同位运算更新  
4. 把 `A..E` 加回 `Message_Digest[0..4]`，块索引归零  

循环左移宏：

```5:6:Middlewares/sha1.c
#define SHA1CircularShift(bits, word) \
    ((((word) << (bits)) & 0xFFFFFFFF) | ((word) >> (32 - (bits))))
```

### 5.4 `SHA1PadMessage` + `SHA1Result` — 收尾

SHA-1 要求：原始消息后追加 `0x80`，再填 0，最后 8 字节写上消息比特长度，使总长为 512 bit 的整数倍，再处理最后一块。

`SHA1Result`：若尚未 `Computed`，先 `SHA1PadMessage`，再允许读 `Message_Digest`。

---

## 6. 从源串到 40 字符（端到端）

```text
source = "abc"          （例）
    │
    ▼
SHA1Reset / SHA1Input / SHA1Result
    │
    ▼
Message_Digest[0..4]    五个 uint32（大端语义）
    │
    ▼
sprintf("%08X") × 5
    │
    ▼
output_hex = 40 个大写 hex + '\0'
例（标准测试向量 "abc"）：
A9993E364706816ABA3E25717850C26C9CD0D89D
```

口令业务在此之后还有「转小写 → 左 10 → ASCII 和」，那一步**不属于** `sha1_hash`，见 [password-4digit-auth.md](./password-4digit-auth.md) 第 5 节。

---

## 7. 接手注意

- **缓冲区**：`output_hex` 至少 41 字节；本函数不检查指针与长度。  
- **字符集**：按字节哈希；中文等非 ASCII 取决于源串实际编码字节。  
- **大小写**：库输出大写；业务若与平台比对，须统一大小写（口令路径显式转小写）。  
- **安全定位**：SHA-1 已不推荐用于新密码学协议；本工程用于通行摘要/验签短材料，与 [password-4digit-collision.md](./password-4digit-collision.md) 中的碰撞问题主要来自后续 `%10`/`%25`，而非 SHA-1 本身撞车。  
- **改库**：`Middlewares/` 约定尽量少改；业务加工放在 `App/Pass`。

---

## 8. API 一览

| 函数 | 角色 |
| --- | --- |
| `sha1_hash` | 业务入口：字符串 → 40 hex |
| `SHA1Reset` | 初始化上下文 |
| `SHA1Input` | 追加消息字节 |
| `SHA1Result` | 填充并完成；成功返回非 0 |
| `SHA1ProcessMessageBlock` | （内部）压缩 512-bit 块 |
| `SHA1PadMessage` | （内部）按规范填充 |
