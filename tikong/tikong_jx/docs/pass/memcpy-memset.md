# `memcpy` 与 `memset`（读头路径中的用法）

更新时间：2026-07-18

> **标准库**：`<string.h>`  
> **本工程典型调用处**：`App/Pass/qr_comm.c` 中 `QRProcessUart4` / `QRProcessUart5`（切完 `data` 之后、清帧之前）  
> **相关**：[qr-process-uart45.md](./qr-process-uart45.md)

---

## 一、这两个函数是干什么的？

| 函数 | 一句话 |
| --- | --- |
| **`memcpy`** | 把一块内存里的字节 **原样拷贝** 到另一块内存 |
| **`memset`** | 把一块内存里的每个字节都 **写成同一个值**（常用 `0` 清零） |

它们操作的是 **字节缓冲**，不关心里面是不是“合法字符串”，所以嵌入式里收包、切片、组帧时很常用。

---

## 二、函数原型（概念）

```c
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
```

| 参数 | `memcpy` | `memset` |
| --- | --- | --- |
| 第一参 | 目标地址 `dest` | 要填充的起始地址 `s` |
| 第二参 | 源地址 `src` | 填充值 `c`（按字节，常用 `0`） |
| 第三参 | 拷贝字节数 `n` | 填充字节数 `n` |

注意：

- **`memcpy` 要求源、目标区域不要重叠**；若可能重叠，应使用 `memmove`（本工程对齐 `{` 时用的就是 `memmove`）。
- 拷贝/清零 **不会自动加 `'\0'`**；当 C 字符串用时要自己补结尾。

---

## 三、在 `qr_comm.c` 里为什么用它们？

以 UART4 为例（UART5 对称）：

### 3.1 `memcpy`：把切好的 `data` 交给业务

```c
s_received_len_uart4 = (uart4_data_slice_len < MAX_RECEIVE_LEN - 1)
    ? uart4_data_slice_len : (MAX_RECEIVE_LEN - 1);
memcpy(s_received_uart4, uart4_data_slice, s_received_len_uart4);
s_received_uart4[s_received_len_uart4] = '\0';
```

| 缓冲 | 角色 |
| --- | --- |
| `uart4_data_slice` | JSON 里切出的 `data` 字段（按长度的字节片） |
| `s_received_uart4` | 传给 `CommControl` / 密码处理的业务字符串 |

为何不用 `strcpy`：

- 切片区按 **长度** 管理，不一定已有 `'\0'`
- `memcpy` 只拷 `n` 个字节，再手动写结尾，行为可控，也方便做 `MAX_RECEIVE_LEN` 上限保护

### 3.2 `memset`：本帧处理完，清空临时区

```c
uart4_rx_accum_len = 0;
uart4_data_slice_len = 0;
uart4_type_slice_len = 0;
memset(uart4_rx_accum, 0, sizeof(uart4_rx_accum));
memset(uart4_type_slice, 0, sizeof(uart4_type_slice));
memset(uart4_data_slice, 0, sizeof(uart4_data_slice));
```

| 做法 | 作用 |
| --- | --- |
| `*_len = 0` | 告诉逻辑“当前有效长度为 0” |
| `memset(..., 0, sizeof(...))` | 把缓冲内容也清掉，避免上一帧残留干扰下一帧或调试打印 |

两者一起用更稳：只清长度、不清内容时，一旦长度算错，可能读到旧数据。

---

## 四、和附近其它内存函数的对比

同文件里还有：

| 函数 | 典型用途 |
| --- | --- |
| `memcpy` | 切片 → 业务缓冲（不重叠） |
| `memmove` | 累积缓冲里丢掉 `{` 前噪声并前移（源目标可能重叠） |
| `memset` | 帧结束后整缓冲清零 |
| `strlen` | 算键名长度（如 `"uid"`）做偏移 |

---

## 五、使用注意（嵌入式）

1. **先算长度、再拷贝**：`n` 不要超过目标缓冲剩余空间。  
2. **当字符串用必须补 `'\0'`**：`memcpy` 本身不加。  
3. **重叠用 `memmove`**：`memcpy` 重叠则行为未定义。  
4. **大缓冲 `memset` 有开销**：本处缓冲几百字节，主循环偶尔清一次可接受；热路径上极长缓冲要掂量。

---

## 六、一句话记忆

- **`memcpy`** = “从 A 复制 n 字节到 B”  
- **`memset`** = “把一块内存刷成同一个字节值（常刷 0）”  
- 在读头路径里：前者把 `data` **交给** `CommControl`，后者在帧结束后 **打扫** 临时缓冲。
