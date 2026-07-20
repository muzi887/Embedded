# 黑名单逻辑与待做（本工程）

更新时间：2026-07-20

> **相关**：[eeprom.md](./eeprom.md)、[storage-logic.md](../superpowers/storage-logic.md)、[thing-model-v2.md](../cloud/thing-model-v2.md)、[pass-split-migrate-list.md](../overview/pass-split-migrate-list.md)  
> **源码**：`App/Store/blackList.*`、`App/Pass/pass_permission.c`、`App/Pass/data_handler.c`、`App/Pass/cmd.c`、`App/Cloud/Live_data_r.c` / `Live_data.c`

---

## 1. 一句话

黑名单是 **最多 200 条、每条约 10 字符 ID** 的拦截表：掉电存在 EEPROM `0..2199`，运行时查 **RAM `dataArray[]`**。  
**通行拦截**在权限链里做；**增删查**当前以 **云端 `parseSerialData` case 104/105/106** 为主。

---

## 2. 数据布局

| 项 | 值 |
|----|-----|
| 容量 | `MAX_ARRAY_SIZE = 200` |
| 槽大小 | `DATA_SIZE = 11` 字节；槽 `i` 地址 = `i * 11` |
| `[0..9]` | ID 内容（最多 10 字节） |
| `[10]` | 占用：`0x00` 有效；`0x01` 空/已删 |
| RAM | `DataEntry dataArray[200]`（`data[11]` + `isEmpty`） |

删除只写标记字节，不整槽擦 `0xFF`。加载时：`data[10]==0x00` 才拷进 RAM。

权威地址见 `eeprom.h` / [eeprom.md](./eeprom.md)。

---

## 3. 核心 API（`App/Store/blackList.c`）

| 函数 | 行为 |
|------|------|
| `loadDataFromEEPROM` | 上电（`app_init`）装载；打印 `blacklist:` |
| `addDataToBlacklist` | 查重 → 找空槽 → 写 RAM + 11 字节 EEPROM；`-1` 已存在，`-2` 满 |
| `deleteDataFromBlacklist` | 命中则 `isEmpty=1`，EEPROM 标记字节写 `0x01` |
| `clearPublicKeySpace` | **名易误解**：只清黑名单槽标记，**不动**公钥区 |
| `initBlacklist` | 仅清 RAM；**现网未调用**（靠 `loadDataFromEEPROM`） |

通行时**不读 EEPROM**，只扫 `dataArray`。

---

## 4. 谁在用：三条路径

### 4.1 通行拦截（有效）

```text
QRProcess → CommControl('2') → Cmd_Permission
  → Cmd_Permission_CheckBlacklist   （pass_permission.c）
       比较 recv_arg_id 与 dataArray[i].data（前 10 字节）
       命中 → g_result.code = 400
```

四位口令路径（`pass_password.c`）**当前不查黑名单**。

### 4.2 云端增删查（有效主路径）

| case | 作用 | 入口 |
|------|------|------|
| **104** | 获取/上报列表 | `parse_tiqublack_json` → `Four_Reply`（扫 `dataArray`） |
| **105** | 新增 | `parse_addblack_json` → `addDataToBlacklist` → `Fifth_Reply` → **再** `loadDataFromEEPROM` |
| **106** | 删除 | `parse_delblack_json` → `deleteDataFromBlacklist` → `Sixth_Reply`（无强制 reload） |

物模型 key 见 [thing-model-v2.md](../cloud/thing-model-v2.md)：`blacklist_get` / `blacklist_add` / `blacklist_delete`。  
云端 JSON 现实现里常见字段是 **`arguments.qrCodeIds[]`**（字符串 ID），与文档表格里的 `cardNumber` 命名可能不一致——联调以串口 JSON 为准。

### 4.3 读头 CSV / `cmd.c`（基本不可达）

| 符号 | 说明 |
|------|------|
| `BlackUserListAdd` / `Del` / `Clear` | 实现仍在 `cmd.c`，内部走 `AddBlacklistIds_FromPacket` / `Del*` / `clearPublicKeySpace` |
| `CommControl` 中 `cmd_ch == '3'` | **直接 `return -1`**，未调用上述函数 |

故读头侧「黑名单命令」**现码未接通**；增删以云端为准。

`AddBlacklistIds_FromPacket` / `DelBlacklistIds_FromPacket`（`data_handler.c`）：解析旧式 CSV 包（多 ID 下划线分隔）再调 `addDataToBlacklist` / `deleteDataFromBlacklist`。

---

## 5. 上电与数据流

```text
board_init → … → app_init
                    loadDataFromEEPROM()   // EEPROM → dataArray
主循环
  权限：CheckBlacklist 读 dataArray
  云 105：addDataToBlacklist →（可选）loadDataFromEEPROM 再同步
  云 106：deleteDataFromBlacklist（RAM+EEPROM 已改，一般不必 reload）
```

---

## 6. 待做（按优先级）

### P0 — 行为 / 联调债

1. **读头 cmd `'3'`**：要么接通 `BlackUserListAdd/Del/Clear`，要么删死代码并改文档，避免「有函数却进不去」。  
2. **物模型字段对齐**：平台 `blacklist_*` 的 `cardNumber` vs 固件 `qrCodeIds`——确认协议，改解析或改文档。  
3. **口令是否拦黑名单**：产品若要求密码也拦截，在 `pass_password` 成功开门前加同一套 RAM 查询。

### P1 — 结构（与 Pass 拆分正交）

4. 将 `BlackUserList*` 从 `cmd.c` 迁到 `App/Store`（或云侧旁路），`cmd` 只留 `CommControl`。  
5. `clearPublicKeySpace` **改名**（如 `Blacklist_ClearAll`），消除密钥误解（见 [naming-conventions.md](../overview/naming-conventions.md)）。  
6. `data_handler` 中 `Add/DelBlacklistIds_FromPacket`：若仅服务死路径，标记废弃或并入 Store。

### P2 — 健壮性

7. ID 长度：云端 `strncpy` 进缓冲 vs EEPROM 仅 10 字节——超长 ID 截断/拒绝策略写清。  
8. 满员 `-2`、已存在 `-1`：云端回执是否区分失败原因（现多蜂鸣 + 笼统 reply）。  
9. case 105 每次全量 `loadDataFromEEPROM`：可改为信任 `addDataToBlacklist` 已写双端，减少上电级扫描（可选优化）。  
10. `initBlacklist`：不用则删或在 `load` 前显式调用并写注释。

### 非目标（近期）

- 不为黑名单单独上 RTOS / 换存储芯片。  
- 不与限层位图、MQTT 配置区混地址。

---

## 7. 联调检查清单

1. 上电 USART1 有 `blacklist:` 列表（或空）。  
2. 云下发 105 → 再刷含该 ID 的权限二维码 → 应 `user is in blacklist` / code 400。  
3. 云 106 删除 → 同 ID 应能再通行。  
4. 云 104 → 回执列表与 RAM 一致。  
5. 勿依赖读头 cmd `'3'`，除非已按待做 P0 接通。

---

## 8. 源码索引

| 想搞清楚… | 打开 |
|-----------|------|
| 槽与增删 | `App/Store/blackList.c` |
| 通行检查 | `App/Pass/pass_permission.c` → `Cmd_Permission_CheckBlacklist` |
| 云增删查 | `App/Cloud/Live_data_r.c` case 104/105/106 |
| 列表组帧上报 | `App/Cloud/Live_data.c`（扫 `dataArray`） |
| 读头死路径 | `App/Pass/cmd.c` → `BlackUserList*`、`CommControl` `'3'` |
| CSV 包解析 | `App/Pass/data_handler.c` → `Add/DelBlacklistIds_FromPacket` |
| 地址布局 | `Hardware/Storage/eeprom.h`、本文 §2 |
