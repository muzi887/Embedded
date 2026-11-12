# 项目长期约定：qibiji（STM32 闸门控制器源码解读）

## 文档产出规则（2026-11-12 用户明确）

- **不要新增 `docs/*.md`**。源码解读、订正、问答结论一律**并入既有文档**（新增小节或「补充（本轮对话）」段落）。
- 确需新建独立主题文档时**先征求用户同意**；用户会自行决定保留哪些。
- 用户已删除新建的 `USART2_RX_chain_explained.md`、`STM32_USART_basics_explained.md`、`switch_explained.md`，**仅保留 `Protocols_explained.md`**（协议现状属独立主题）。被删文档的内容用户没要求找回，不要擅自搬回。
- 删除/合并文档后，必须全量检查并清理其它文档里指向它的**悬空引用**，改成就地自包含表述或指向源码文件。
- 全程**不改任何 `.c` / `.h` 源码**，只编辑 `docs/*.md`。

## 文档集现状（2026-11-12）

`docs/` 共 9 份 md：
- 7 份 `*_explained.md`：`ADC1_Read_filter`、`Dcmotor_Process`、`Eeprom_Wpara`、`Gprs_Process`、`Lcd_Process`、`ViewPages`、`Protocols`。
- 2 份非解读稿：`嵌入式节点通讯协议应用规范(基于SL651 ASCII模式).md`（**另一产品的方案设计稿，本工程零实现**）、`长连接在线保证说明.md`。

## 已确认的技术结论（避免反复查证）

- 对外唯一在跑的协议链路是 **4G 模块内 MQTT + 私有 JSON**；RS485 `485.c` 是「只问不用」的 Modbus RTU 主站（应答解析整段被注释）；`HARDWARE/MODBUS/` 未编进工程；**SL651 未使用**。
- Modbus 请求帧 CRC 字节序**符合标准 RTU**（实算 `01 04 30 33 00 20` → 线上 `0E DD`），此前「非标」的判断是错的。
- `GenerateProgressBar1()` 写屏在 **Y=14**（不是第 0 像素行）；「8 段」是 GDRAM 物理格、「5 段」是业务页份，两者不同层。
