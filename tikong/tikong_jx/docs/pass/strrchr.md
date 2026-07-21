# `strrchr` 与 `strncmp`（验签相关）

更新时间：2026-07-20

> **标准库**：`<string.h>`  
> **本工程典型用法**：校时/权限/限层验签——`strrchr` 定位末逗号替换密钥，`strncmp` 比对 SHA1 前 10 位  
> **相关**：[cmd-set-time.md](./cmd-set-time.md)、[sha1-hash.md](./sha1-hash.md)

---

## 1. 一句话

| 函数 | 作用 |
|------|------|
| **`strrchr`** | 在字符串里找某字符 **最后一次** 出现的位置 |
| **`strncmp`** | 比较两串的前 **n** 个字符是否相同（可提前遇到 `'\0'` 结束） |

校时验签里二者前后衔接：`strrchr` 负责「改出正确明文」，`strncmp` 负责「算出的指纹和报文签名是否一致」。

---

## 2. `strrchr`

### 2.1 原型

```c
char *strrchr(const char *s, int c);
```

| 参数 / 返回 | 含义 |
|-------------|------|
| `s` | 以 `'\0'` 结尾的字符串 |
| `c` | 要找的字符（如 `','`、`':'`） |
| 返回值 | 指向该字符；没有则 `NULL` |

名字里的 **`r`** ≈ right / reverse：相对 `strchr`（找**第一次**），`strrchr` 找**最后一次**。  
返回的是原串内部指针；改 `*返回值` 等于改原串。

### 2.2 和 `strchr` 对比

| | `strchr` | `strrchr` |
|--|----------|-----------|
| 找 | **第一次**出现 | **最后一次**出现 |
| 串 `a,b,c` 找 `,` | 第一个逗号 | 最后一个逗号 |

校时 CSV `命令,时间,签名` 的签名在 **最后一个逗号之后**，必须用 `strrchr`；若用 `strchr`，会从第一个逗号后整段覆盖，明文就错了。

### 2.3 本工程用法（换密钥）

```text
收到:  6,20260506105319,51b72d14bd
              ↑ strrchr 找到这里
替换:  6,20260506105319,fbc40b0a1d
```

```210:216:App/Pass/pass_setting.c
	/* 验签：将最后一个逗号后的字段替换为固定值后计算 SHA1 前10位 */
	last_comma = strrchr(received_data, ',');
	if (!last_comma)
		return -2;
	strcpy(last_comma + 1, g_device_public_Key);
	printf("qr csv tail replaced: %s\r\n", received_data);
```

| 表达式 | 含义 |
|--------|------|
| `strrchr(received_data, ',')` | 指向最后一个 `,` |
| `last_comma == NULL` | 无逗号 → return -2 |
| `last_comma + 1` | 原签名起点 |
| `strcpy(last_comma + 1, 密钥)` | 覆盖成 `g_device_public_Key`，供 `sha1_hash` |

同类：`pass_permission.c`、`Cmd_Set_OpenLimit`。  
另：`uart3_at_sequence.c` 用 `strrchr(in, ':')` 取最后一个冒号后的端口等。

---

## 3. `strncmp`

### 3.1 原型

```c
int strncmp(const char *s1, const char *s2, size_t n);
```

| 参数 / 返回 | 含义 |
|-------------|------|
| `s1` / `s2` | 要比较的两个字符串 |
| `n` | **最多**比较前 `n` 个字符 |
| 返回值 | `<0`：s1 小于 s2；`0`：前 n 个（或到 `'\0'`）相等；`>0`：s1 大于 s2 |

验签里通常只关心 **是否等于 0**：

```c
if (strncmp(calc10, recv_arg_signature, 10) != 0)
    /* 不相等 → 失败 */
```

### 3.2 和 `strcmp` 对比

| | `strcmp` | `strncmp` |
|--|----------|-----------|
| 比较范围 | 一直比到某串结束 | **最多 n 个字符** |
| 适用 | 整串必须完全一样 | 只认前缀 / 固定长度字段（如签名 10 位） |

SHA1 十六进制有 40 位，本工程约定只用前 **10** 位当签名，因此用 `strncmp(..., 10)`，而不是 `strcmp` 比满 40 位。

### 3.3 本工程用法（比签名）

`strrchr` + `strcpy` 换密钥之后：

```text
sha1_hash → hash_str（40 位 hex）
取前 10 位写入 calc10（toLowerHex）
strncmp(calc10, recv_arg_signature, 10)
  == 0 → 验签通过
  != 0 → signature verification failed
```

```218:229:App/Pass/pass_setting.c
	sha1_hash(received_data, hash_str);
	printf("hash_str: %s\r\n", hash_str);
	for (i = 0; i < 10; i++)
	{
		char c = hash_str[i];
		calc10[i] = toLowerHex(c);
	}
	if (strncmp(calc10, recv_arg_signature, 10) != 0)
	{
		printf("qr csv: signature verification failed\r\n");
		return -2;
	}
```

成功例子（与 [cmd-set-time.md](./cmd-set-time.md) 一致）：

| 项 | 值 |
|----|-----|
| `recv_arg_signature` | `51b72d14bd`（来自 CSV 缓冲，拆列时保存） |
| `hash_str` 前 10 位 | `51B72D14BD` → `toLowerHex` → `51b72d14bd` |
| `strncmp(..., 10)` | `0` → 通过 |

注意：`recv_arg_signature` 指向 **拆列后的副本**；`strrchr` 改的是 `received_data` 原缓冲。比对的是副本里的原签名，不是改写后的尾字段。

其它用法简记：

- `pass_crypto.c`：同样 `strncmp(calc10, sig_field, 10)`  
- `pass_permission.c` 黑名单：`strncmp(dataArray[i].data, recv_arg_id, DATA_SIZE-1)` 比最多 10 字节 ID  

---

## 4. 二者在验签中的分工（一张图）

```text
received_data = "6,20260506105319,51b72d14bd"
        │
        ├─ qr_split → recv_arg_signature = "51b72d14bd"   （副本，留给 strncmp）
        │
        ├─ strrchr → 最后一个 ','
        │     strcpy → "6,20260506105319,fbc40b0a1d"
        │     sha1_hash → hash_str
        │     前 10 位 → calc10
        │
        └─ strncmp(calc10, recv_arg_signature, 10)
              0 → 继续 parseYMDHMS / Set_CurrentTime
              非0 → return -2
```

---

## 5. 使用注意

**`strrchr`**

1. `s` 须有 `'\0'`。  
2. `strcpy(last_comma+1, …)` 目标空间要够。  
3. 要「第 n 个逗号」时不要用它，应拆 CSV。

**`strncmp`**

1. `n` 不要超过你真正关心的长度；过大可能比到无关内容。  
2. 若某串长度 `< n`，会在 `'\0'` 处停下（与 `strcmp` 类似的提前结束）。  
3. 大小写敏感：本工程靠 `toLowerHex` 先把算得的 hex 统一成小写再比。  
4. 返回值是整型大小关系，验签场景用 `!= 0` / `== 0` 即可。

---

## 6. 一句话记忆

- **`strrchr`** = 找**最后一个**某字符 → 本工程用来定位末 `,` 并换成密钥。  
- **`strncmp`** = 比前 **n** 个字符 → 本工程用来确认 SHA1 前 10 位与报文签名一致。  
- 验签：**先 `strrchr` 造明文，再 `strncmp` 对指纹。**
