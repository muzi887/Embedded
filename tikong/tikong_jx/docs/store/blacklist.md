# 黑名单

更新时间：2026-07-20

> **相关**：[eeprom.md](./eeprom.md)、[storage-logic.md](../superpowers/storage-logic.md)、[thing-model-v2.md](../cloud/thing-model-v2.md)  
> **源码**：`App/Store/blackList.*` · `App/Pass/pass_permission.c` · `App/Pass/cmd.c` · `App/Pass/data_handler.c` · `App/Cloud/Live_data_r.c` / `Live_data.c`

黑名单是一份 **最多 200 条、每条最多 10 字符的用户 ID** 拦截表：掉电落在 EEPROM `0..2199`，运行时只查 RAM `dataArray[]`。  
**查**：权限通行链 `Cmd_Permission_CheckBlacklist`。**改**：主路径为云端 `parseSerialData` case **104 / 105 / 106**。读头 cmd `'3'` 现码不可达。

它不管口令、公钥、限层、MQTT 配置——那些是别的区。

---

## 1. 存储布局与 Store API

每条 11 字节槽：`地址 = i * 11`（`i = 0..199`）。

| 偏移 | 含义 |
|------|------|
| `[0..9]` | ID（最多 10 字节） |
| `[10]` | `0x00` 有效；`0x01` 空/已删 |

| 符号 | 值 |
|------|-----|
| `MAX_ARRAY_SIZE` | 200 |
| `DATA_SIZE` | 11 |
| RAM | `DataEntry dataArray[200]`（`data[11]` + `isEmpty`） |

删除只改标记字节，不整槽擦 `0xFF`。`loadDataFromEEPROM` 仅当 `data[10]==0x00` 才装入 RAM。权威地址见 `eeprom.h` / [eeprom.md](./eeprom.md)。

### `App/Store/blackList.c`（唯一写名单真源）

| 函数 | 行为 |
|------|------|
| `loadDataFromEEPROM` | EEPROM → `dataArray`；`app_init` 调用；打印 `blacklist:` |
| `addDataToBlacklist` | 查重 → 找空槽 → 写 RAM + 整槽 EEPROM；返回 `-1` 已存在，`-2` 满 |
| `deleteDataFromBlacklist` | 命中：`isEmpty=1`，EEPROM `[10]` 写 `0x01` |
| `clearPublicKeySpace` | **名易误解**：只清黑名单槽标记，**不动**公钥区 |
| `initBlacklist` | 仅清 RAM；**现网未调用**（靠 `load`） |

通行检查**不读 EEPROM**，只扫 `dataArray`。

---

## 2. 总览：谁读谁写

```text
              EEPROM 0..2199
                    ▲
         load / add / delete / clear
                    │
              dataArray[]  ◄── 运行时唯一查询面
           ┌────────┼────────┐
           │        │        │
      【查】拦人  【改】云端  【改】读头
  CheckBlacklist  104/105/106  CommControl '3'
       ✅           ✅           ❌ return -1
```

---

## 3. 路径 A — 通行拦截（有效）

权限二维码（非口令）经读头进 MCU 后：

```text
QRProcess → CommControl(cmd_ch=='2')
  → Cmd_Permission(...)                    // pass_permission.c
       → …验签…
       → Cmd_Permission_CheckBlacklist()
            对 i=0..199：
              strncmp(dataArray[i].data, recv_arg_id, DATA_SIZE-1)==0
              && isEmpty==0
            → 命中：g_result.code=400，"user is in blacklist"
            → 未命中：code=200，继续时间窗 / 开门
```

要点：

- 比对对象是解析后的 **`recv_arg_id`** 与 **`dataArray[i].data` 前 10 字节**。
- 院门 / 楼门 / 电梯分支都会走到 `CheckBlacklist`（同文件内多次调用）。
- **四位口令**走 `pass_password.c`，**全程不调**黑名单 API——名单上的人仍可能用正确密码通行（产品未定是否算漏洞）。

---

## 4. 路径 B — 云端增删查（有效主路径）

USART3 → `parseSerialData` → 按业务号分支。物模型 key：`blacklist_get` / `blacklist_add` / `blacklist_delete`（见 [thing-model-v2.md](../cloud/thing-model-v2.md)）。

| case | 作用 | 调用链 |
|------|------|--------|
| **104** 提取 | 上报当前名单 | `parse_tiqublack_json` → `Four_Reply`（`Live_data.c` 扫 `dataArray` 组帧） |
| **105** 新增 | 写入名单 | `parse_addblack_json` → 对每个 ID `addDataToBlacklist` → `Fifth_Reply` → **再** `loadDataFromEEPROM` |
| **106** 删除 | 移出名单 | `parse_delblack_json` → `deleteDataFromBlacklist` → `Sixth_Reply`（无强制 reload；delete 已改 RAM+EEPROM） |

固件解析常见字段是 **`arguments.qrCodeIds[]`**（字符串 ID）。物模型摘录里可能出现 `cardNumber`——**联调以实际串口 JSON 为准**，两边命名不一致是已知债。

---

## 5. 路径 C — 读头 CSV 改名单（现码不可达）

历史上读头可用 cmd `'3'` 批量改名单，实现仍在，但入口已断：

```text
CommControl:
  cmd_ch == '3'  →  return -1;     // 未调用下方任何函数
```

残留符号（无人从 `CommControl` 进入）：

| 符号 | 文件 | 内部再调 |
|------|------|----------|
| `BlackUserListAdd` / `Del` / `Clear` | `cmd.c` | `AddBlacklistIds_FromPacket` / `Del*` / `clearPublicKeySpace` |
| `AddBlacklistIds_FromPacket` / `DelBlacklistIds_FromPacket` | `data_handler.c` | 解析下划线分隔多 ID → `addDataToBlacklist` / `deleteDataFromBlacklist` |

结论：**改名单以云端为准**；不要按「读头能管黑名单」联调，除非先接通 `'3'`。

---

## 6. 上电与运行时数据流

```text
board_init → … → app_init
                    loadDataFromEEPROM()    // EEPROM → dataArray
主循环
  权限码：Cmd_Permission_CheckBlacklist 只读 dataArray
  云 105：addDataToBlacklist（双写）→ loadDataFromEEPROM（全量再扫，偏冗余）
  云 106：deleteDataFromBlacklist（双写已完成，一般不必 reload）
```

---

## 7. 待完善

主链（权限拦人 + 云端 104/105/106）可用。下列是半成品 / 协议债 / 产品缺口。

| 优先级 | 问题 | 现状 | 完成标准 |
|--------|------|------|----------|
| P0 | 读头 `'3'` 假入口 | `BlackUserList*` 在，`CommControl` 直接 `-1` | 接通 **或** 删/标废弃并改文档 |
| P0 | 字段名 | 固件 `qrCodeIds` vs 文档/物模型 `cardNumber` | 协议统一 + 一份样例 JSON |
| P0 | 口令是否拦 | `pass_password` 不查 `dataArray` | 产品书面规定；要拦则开门前复用同一比对 |
| P1 | `clearPublicKeySpace` 命名 | 像清公钥，实清黑名单 | 改名如 `Blacklist_ClearAll` |
| P1 | 代码散落 | Store / Cloud / 死路径 cmd+data_handler | 死路径迁 Store 或删；`cmd` 只留路由 |
| P2 | 满员/重复/超长 ID | `-1`/`-2` 与云端回执往往笼统 | 明确错误码与日志 |
| P2 | case 105 全量 reload | `add` 已双写仍 `loadDataFromEEPROM` | 可改为信任双写（可选） |
| P2 | `initBlacklist` | 未调用 | 删或在 `load` 前显式用 |

**非目标（近期）**：为黑名单上 RTOS / 换芯片；与限层、MQTT 区混地址。

---

## 8. 联调清单

1. 上电 USART1 有 `blacklist:`（可空列表）。  
2. 云 105 加 ID → 刷含该 ID 的权限码 → `user is in blacklist` / `code 400`。  
3. 云 106 删同 ID → 应能再通行。  
4. 云 104 → 回执与 `dataArray` 一致。  
5. 勿依赖读头 `'3'`，除非已按 §7 P0 接通。

---

## 9. 源码索引

| 想搞清楚… | 打开 |
|-----------|------|
| 槽、增删、装载 | `App/Store/blackList.c` |
| 通行比对 | `pass_permission.c` → `Cmd_Permission_CheckBlacklist` |
| 云增删查 | `Live_data_r.c` case 104/105/106；`parse_*black_json` |
| 列表组帧 | `Live_data.c`（`Four_Reply` 等扫 `dataArray`） |
| 读头死路径 | `cmd.c`：`CommControl` `'3'`、`BlackUserList*` |
| CSV 多 ID 解析 | `data_handler.c`：`Add/DelBlacklistIds_FromPacket` |
| 地址常量 | `Hardware/Storage/eeprom.h`、本文 §1 |
