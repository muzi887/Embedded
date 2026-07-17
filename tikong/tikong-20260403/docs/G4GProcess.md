**文件位置**
- [G4GProcess.md](./G4GProcess.md)（本目录）

**模块概述**
- `G4GProcess` 为主循环中负责处理 4G 模块（USART3）接收数据的入口函数。其职责是：安全终止接收缓冲、打印原始数据、调用通用解析器 `parseSerialData` 处理业务逻辑，并清理接收状态。

**总体流程（文字描述，适合实验报告）**
1. 条件检查：检测接收完成标志（`USART3_RX_Complete`）。若未完成直接返回，函数为非阻塞轮询调用。
2. 边界保护：根据接收长度（`USART3_RX_CNT` 与 `USART3_REC_LEN`），在接收缓冲末尾放置字符串终止符（`'\0'`），以确保后续以 C 字符串方式处理时不会越界读取。
3. 原始数据打印：逐字节将接收缓冲区内容打印到调试串口，以便本地日志与故障定位。
4. 业务解析：调用 `parseSerialData((const char*)USART3_RX_BUF)` 将原始字符串交由上层通用解析器处理。此解析器负责识别平台下发命令并分派到具体处理函数。
5. 状态清理：将 `USART3_RX_Complete` 与 `USART3_RX_CNT` 清零，准备下一轮接收。

**`parseSerialData` 的核心职责与调用关系**
- 作用：解析来自云平台/4G 模块的文本命令或上行确认，采用 JSON 或自定义前缀格式识别命令类型，并根据命令执行对应操作。
- 解析策略：优先进行最小边界检查与 JSON 解析（使用 cJSON），若解析失败则以容错方式记录错误并返回。

常见下游解析/处理函数（`parseSerialData` 内部分派）：
- `parse_rs485_json`：将云下发的梯控/RS485 指令封装后通过 RS485 串口转发给梯控板（构建帧、添加校验、发送），并可在必要时等待或处理回执。
- `parse_tiqublack_json`：触发设备向云请求或确认黑名单相关操作（例如拉黑名单、同步黑名单状态等）。
- `parse_addblack_json`：解析“添加黑名单”命令的数据结构，调用本地黑名单写入流程（内存更新 + EEPROM 持久化），并构造 ACK 或上报结果。
- `parse_delblack_json`：解析“删除黑名单”命令，执行本地删除与 EEPROM 同步，并返回处理结果给云平台。
- `parse_uptime_json`：处理平台要求上报设备运行时间或同步时间戳的请求，可能会调用 DS3231 时间读写接口来获取系统时间并回报。
- `parse_upgongyao_json`：处理公钥/密钥管理类命令（下发公钥、更新验证信息等），并可能调用 `SetDevicePublicKey` / `VerifyDeviceCredentials` 等函数。

每个解析函数通常包含以下步骤：
- 输入校验：检查字段完整性（必要字段、长度、类型）。
- 权限/签名验证：对敏感命令（如黑名单操作、公钥更新）进行签名或凭证校验，防止未授权操作。
- 执行操作：调用对应底层函数（如 EEPROM 写入、黑名单数组更新、RS485 发送、DS3231_SetTime 等）。
- 回复/上报：通过 `USART3_SendData` 或 `RS485_SendData` 发送处理结果或构造 JSON 上报到云平台；在需要时构造第一/四/五等回应包（工程中以 `First_Reply` / `Four_Reply` / 等命名）。

**错误处理与鲁棒性**
- 边界保护：`G4GProcess` 已做接收尾部的 `\0` 保护，避免字符串操作越界；`parseSerialData` 仍需对最大长度与 JSON 解析错误进行检查。
- 解析失败：若 JSON 解析失败或必要字段缺失，应记录错误（日志）并向云返回错误码或简单 ACK，避免出现半处理状态。
- 内存/分配：解析过程中会分配临时内存（cJSON、字符串构建），调用方需确保所用分配器（`mymalloc`）可用且在操作完成后释放（`cJSON_Delete`）。

**中断/任务划分建议**
- `G4GProcess` 为主循环任务，在主循环中被轮询执行，避免在中断上下文内做复杂解析。
- 若系统接收到频繁或大型上行数据，建议将 `parseSerialData` 的重工作移到专用解析任务队列中，由中断仅设置接收就绪并复制缓冲到工作队列，主任务异步处理，防止阻塞主循环。

**测试建议**
- 用例覆盖：正常 JSON 命令、缺字段的命令、格式错误（非 JSON）、超长 payload、并发多条命令到达时的排队与处理顺序。
- 端到端验证：模拟云下发命令并在设备上验证：解析->执行（例如 EEPROM 写入/RS485 发送）->上报结果回云。
- 资源异常：在内存分配失败（cJSON malloc 返回 NULL）或 EEPROM 写失败时，验证设备是否能正确回退或报告错误而不中断其他功能。

---

相关：[G4GProcess_flow.mmd](./G4GProcess_flow.mmd)、[parseSerialData_ANALYSIS.md](./parseSerialData_ANALYSIS.md)、[parseSerialData_flow.mmd](./parseSerialData_flow.mmd)。