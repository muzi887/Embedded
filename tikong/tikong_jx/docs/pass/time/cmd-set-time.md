# `Cmd_Set_Time`：设备校时（读头 cmd `'6'`）

更新时间：2026-07-20

> **源码**：`App/Pass/pass_setting.c` → `Cmd_Set_Time`  
> **入口**：`App/Pass/cmd.c` → `CommControl`，`cmd_ch == '6'`  
> **相关**：[sha1-hash.md](./sha1-hash.md)、[strrchr.md](./strrchr.md)、[qr-process-uart45.md](./qr-process-uart45.md)、[commcontrl-permission-chain.md](./commcontrl-permission-chain.md)

---

## 1. 一句话

现码校时 CSV Comma-Separated Values（逗号分隔值）） 为 **3 列**（无漂移）：`6,时间14位,签名10位`。  
平台/自测侧用 **设备密钥** 算 SHA1 前 10 位做成二维码；设备把尾字段换成密钥再算一遍比对，通过后写 RTC。

---

## 2. 如何生成正确的二维码（验签能过）

### 2.1 联调成功的一套真实例子

| 项 | 值 |
|----|-----|
| 设备密钥 `g_device_public_Key` | `fbc40b0a1d`（以本机 USART1 `tail replaced` 为准） |
| 要写入的时间 | `20260506105319`（2026-05-06 10:53:19） |
| 算签明文 | `6,20260506105319,fbc40b0a1d` |
| SHA1 全文 | `51b72d14bde0ea39ba90605c0b773eee19f01f31` |
| **签名（前 10 位）** | **`51b72d14bd`** |
| **二维码内容（推荐）** | **`6,20260506105319,51b72d14bd`** |

读头扫码后发到 MCU 的帧（读头自动包一层）：

```text
{"type":1,"data":"6,20260506105319,51b72d14bd","uid":0}
```

USART1 成功验签时可见（无 `signature verification failed`）：

```text
type-->1
data-->6,20260506105319,51b72d14bd
recv_cmd: 6
recv_sys_time: 20260506105319
qr csv tail replaced: 6,20260506105319,fbc40b0a1d
hash_str: 51B72D14BDE0EA39BA90605C0B773EEE19F01F31
```

### 2.2 自己算签的步骤（换时间/换设备时）

```text
① 拿到本机密钥
     扫任意校时码看日志「qr csv tail replaced: 6,时间,XXXX」
     逗号后 XXXX 即 g_device_public_Key
     （或 ReadKey / 设置命令写入的公钥）

② 选定 14 位时间 T = yyyyMMddHHmmss

③ 拼明文（没有漂移列）
     plain = "6," + T + "," + 密钥

④ 签名
     sig = SHA1(plain) 的十六进制字符串前 10 位（小写）

⑤ 二维码内容只放 CSV（不要放整段 JSON）
     "6," + T + "," + sig
```

Python 示例（对应上面成功例子）：

```python
import hashlib
key = "fbc40b0a1d"
t = "20260506105319"
plain = "6," + t + "," + key
sig = hashlib.sha1(plain.encode()).hexdigest()[:10]
qr_content = "6," + t + "," + sig
print(plain)       # 6,20260506105319,fbc40b0a1d
print(sig)         # 51b72d14bd
print(qr_content)  # 6,20260506105319,51b72d14bd
```

把 `qr_content` 做成二维码即可。

---

## 3. 从扫码到 `Cmd_Set_Time`（入口）

```text
读头 UART4/5
  → QRProcessUart* 切出 type / data
  → type 为 '1'（或 '0'）→ CommControl(data, …)
       data[0] == '6' → Cmd_Set_Time(received_data)
```

```127:131:App/Pass/cmd.c
	if (cmd_ch == '6') // 设置时间及漂移时间
	{
		Cmd_Set_Time(received_data);
		return -1;
	}
```

`received_data` 即 JSON 的 `data` 字段，例如 `6,20260506105319,51b72d14bd`。  
`CommControl` 调用后 **固定 `return -1`**，成败看日志与 RTC，不看该返回值。

---

## 4. 设备端如何校验（流程 + 代码）

### 4.1 总流程

```text
Cmd_Set_Time(received_data)
  │
  ├─① 清缓冲 + 逗号拆列          → qr_split / memset
  ├─② 列数必须 == 3              → 否则 return -1
  ├─③ 挂字段指针                 → cmd / 时间 / 签名（签名在 CSV 缓冲副本）
  ├─④ 验签                       → 尾字段换密钥 → SHA1 前10位 ≟ 签名
  │      失败 → return -2
  ├─⑤ parseYMDHMS                → 14 位 → RTC_Time
  │      失败 → return -1
  ├─⑥ Set_CurrentTime            → 内存 + RTC 芯片
  └─⑦ return 1
       （漂移 WriteKey 已注释）
```

### 4.2 ①②③ 拆列并挂字段

```192:207:App/Pass/pass_setting.c
	memset(s_recv_csv_field, 0, sizeof(s_recv_csv_field));
	qr_split_s_received_by_comma(received_data);

	if (s_recv_csv_count != 3)
	{
		printf("qr csv: setting field count need 3, got %d\r\n", s_recv_csv_count);
		return -1;
	}

	recv_cmd = s_recv_csv_field[0];
	recv_sys_time = s_recv_csv_field[1];
	// recv_drift_time = s_recv_csv_field[2];
	recv_arg_signature = s_recv_csv_field[2];

	printf("recv_cmd: %s\r\n", recv_cmd);
	printf("recv_sys_time: %s\r\n", recv_sys_time);
```

对应例子：

| 下标 | 指针 | 值 |
|------|------|-----|
| `[0]` | `recv_cmd` | `6` |
| `[1]` | `recv_sys_time` | `20260506105319` |
| `[2]` | `recv_arg_signature` | `51b72d14bd` |

漂移相关已注释；`[2]` 就是签名。

### 4.3 ④ 验签（核心）

思路与生成端对称：

```text
收到:  6,20260506105319,51b72d14bd
        └─ 签名已保存在 recv_arg_signature（CSV 缓冲）

改写:  6,20260506105319,fbc40b0a1d     ← strcpy 把末段换成 g_device_public_Key
        └─ 与生成端 plain 相同

SHA1 → 51B72D14BD… → 取前10位小写 → 与 51b72d14bd 比较
```

```210:229:App/Pass/pass_setting.c
	/* 验签：将最后一个逗号后的字段替换为固定值后计算 SHA1 前10位 */
	last_comma = strrchr(received_data, ',');
	if (!last_comma)
		return -2;
	strcpy(last_comma + 1, g_device_public_Key);
	printf("qr csv tail replaced: %s\r\n", received_data);

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

要点：

- 比对用的是 **`s_recv_csv_field[2]` 里保存的原签名**，不是改写后的 `received_data` 尾部。  
- `strcpy` 会破坏入参缓冲区里的签名，换成密钥后再哈希。  
- `toLowerHex` 只处理 `A-F`，故 `51B72D14BD` 与 `51b72d14bd` 能对上。  
- 算法细节见 [sha1-hash.md](./sha1-hash.md)。

### 4.4 ⑤⑥ 解析时间并写入 RTC

```231:245:App/Pass/pass_setting.c
	if (parseYMDHMS(recv_sys_time, &ct) != 0)
	{
		printf("qr csv: recv_sys_time parse failed, need 14 digits YYYYMMDDhhmmss: %s\r\n",
			   recv_sys_time ? recv_sys_time : "(null)");
		return -1;
	}

	Set_CurrentTime(&ct);

	// 漂移时间写 EEPROM
	// WriteKey(recv_drift_time, PUBLIC_DRIFT_TIME_ADDR, PUBLIC_DRIFT_TIME_LEN);

	// printf("[Cmd_Set_Time_And_Drift] time and drift is set OK!\r\n");

	return 1;
```

- `parseYMDHMS`：同文件，校验 14 位与日历。  
- `Set_CurrentTime`：`app_run.c`，更新内存并 `RtcChip_SetTime`。  
- 成功路径几乎无额外打印；无 `signature verification failed` / `parse failed` 且 RTC 已变即成功。

---

## 5. 返回值

| `Cmd_Set_Time` | 含义 |
|----------------|------|
| `1` | 验签通过且已 `Set_CurrentTime` |
| `-1` | 列数 ≠ 3，或时间解析失败 |
| `-2` | 无末逗号，或签名不符 |

上层 `CommControl` 不透传该值（总是 `-1`）。

---

## 6. 与产品协议（四列）的差异

产品说明常为：`6,时间,漂移分钟,签名`，签含漂移。  
现码：**3 列、无漂移**；四列样例会 `got 4`。要对齐需改列数、恢复 `WriteKey`、签法与平台一致。

---

## 7. 源码索引

| 想搞清楚… | 打开 |
|-----------|------|
| 校时 / 验签 | `pass_setting.c` → `Cmd_Set_Time` |
| 路由 | `cmd.c` → `CommControl` `'6'` |
| 拆列 | `qr_split_s_received_by_comma` |
| 写 RTC | `app_run.c` → `Set_CurrentTime` |
| 密钥 | `g_device_public_Key` / `ReadKey` |
| SHA1 | `Middlewares/sha1.c`；[sha1-hash.md](./sha1-hash.md) |
