# 常见访客 OTP / 门禁临时码产品介绍

更新时间：2026-07-21

> **用途**：调研对照——业界如何发「临时通行码 / OTP / PIN / 二维码」，有效期与形态大概怎样。  
> **相关**：[drift-timeout-skew-analysis.md](./drift-timeout-skew-analysis.md)、[drift-time-setting.md](./drift-time-setting.md)  
> **说明**：以下为公开产品文档与官网摘要，参数以各厂商当前页面为准，可能随版本变化；链接均可直接访问。

---

## 1. 产品怎么分类

| 类型 | 典型形态 | 有效期常见量级 | 与本工程关系 |
|------|----------|----------------|--------------|
| **前台访客 OTP** | 短信/邮件验证码，在 kiosk 输入 | 数分钟～约 30 分钟 | 偏「身份核验」，不一定直接控门 |
| **门禁临时 PIN / Guest PIN** | 6 位左右数字，键盘输入 | 由主机设定的访问时段 | 接近本工程「四位口令」输入形态 |
| **访客通行证** | 链接 / QR / Apple Wallet | 预约时段 ± buffer，或一次性 | 接近本工程「扫码 + 限时」 |
| **云端下发凭证** | 云平台把临时用户/码推到控制器 | begin～end 明确写库 | 与本工程「离线哈希校验」不同路线 |
| **TOTP 登录 MFA** | 30 s 步长动态码 | 约 30～90 s（±1 步） | 算法亲缘近，**业务 TTL 远短于访客门禁** |

本工程更接近：**住宅/园区多门（园区门 → 单元门 → 电梯）+ 离线可验的短数字口令**，TTL 往往比办公楼前台 OTP 更长。

---

## 2. 国际：访客管理 + 门禁临时凭证

### 2.1 Vizitor（访客 OTP + 可联动开门）

- **做什么**：到访登记时发 SMS/Email **OTP**；通过后完成签到；Enterprise 可经硬件桥接触发门禁继电器。  
- **有效期**：公开页写可配约 **2～30 分钟**（机房可更短，开放办公可更长）。  
- **链接**  
  - OTP 能力介绍：<https://www.vizitorapp.com/otp-based-authentication/>  
  - 访客管理系统：<https://www.vizitorapp.com/visitor-management-system/>  

### 2.2 Johnson Controls OpenBlue Companion（楼宇访客 OTP）

- **做什么**：主机在 App/门户邀请访客；访客收 **邮件 OTP**，在限定访问时段进出；管理员可配访问前后 **buffer**。  
- **有效期**：由**访问时段 + before/after buffer** 决定，不是固定 30 s 步长。  
- **链接**  
  - 产品公告 · Visitor access：<https://docs.johnsoncontrols.com/bas/r/OpenBlue/en-US/OpenBlue-Companion-Product-Bulletin/2.22/Features/Visitor-access>  
  - 桌面用户指南 · Visitor：<https://docs.johnsoncontrols.com/bas/r/OpenBlue/en-US/OpenBlue-Companion-Desktop-User-Guide/2.22/Visitor>  
  - 移动端 · Visitors：<https://docs.johnsoncontrols.com/bas/r/Johnson-Controls/en-US/OpenBlue-Companion-Mobile-App-User-Guide/2.8/Visitors>  

### 2.3 Brivo Access · Guest Management（临时 PIN / Mobile Pass）

- **做什么**：主机邀请访客，下发 **6 位 PIN**（键盘输入，常需以 `#` 结束）或 **Brivo Mobile Pass**；也可与 Envoy 访客签到联动发临时凭证。  
- **有效期**：邀请里的 **日期/时间窗**；过期后 PIN/凭证回收。  
- **链接**  
  - Guest Management 配置与使用：<https://support.brivo.com/s/article/Configuring-and-Using-Guest-Management>  
  - App 邀请访客：<https://support.brivo.com/s/article/Inviting-a-Guest-with-the-Brivo-Mobile-Pass-App>  
  - 访客 PIN 排障：<https://support.brivo.com/s/article/Questions-about-your-Guest-Invite>  
  - 与 Envoy 联动发 PIN/QR：<https://support.brivo.com/s/article/Brivo-Visitor-Management---Powered-by-Envoy-Configuration-Guide>  

### 2.4 Envoy（办公访客签到 → 临时门禁凭证）

- **做什么**：前台/自助签到（kiosk、静态 QR、预登记邮件）；与门禁系统集成后，可发 **临时 mobile / PIN / QR / web link**，签出后可回收。  
- **有效期**：跟签到会话与门禁策略绑定，偏「在场期间有效」。  
- **链接**  
  - 访客体验方案：<https://envoy.com/solutions/visitor-experience-management>  
  - 帮助中心 · Visitors：<https://envoy.help/en/collections/1930712-visitors>  
  - 静态 QR 签到说明：<https://envoy.com/visitor-management/static-qr-codes-visitor-check-in>  
  - 集成与临时通行能力综述：<https://envoy.com/workplace-compliance-security-safety/envoy-most-loved-integrations>  

### 2.5 ButterflyMX（住宅/公寓 Visitor Pass + Door PIN）

- **做什么**：住户发 **Visitor Pass**（QR / PIN），访客在可视对讲/键盘使用；另有住户自用 Door PIN（文档明确：**访客应走 Visitor Pass，不要共用住户 PIN**）。API 支持一次性、配送短时、周期等 keychain。  
- **有效期**：创建时指定开始/结束；配送类可带约 **5 分钟** 宽限（见 API 文档）。  
- **链接**  
  - 如何创建使用 Visitor Pass：<https://help.butterflymx.com/hc/en-us/articles/35345464139156-How-to-Create-and-Use-Visitor-Pass>  
  - 产品博客 · Visitor Passes：<https://butterflymx.com/blog/visitor-passes/>  
  - Door PIN 说明：<https://butterflymx.com/blog/door-pins/>  
  - API · Visitor Passes / Virtual Keys：<https://apidocs.butterflymx.com/docs/virtual-keys>  

### 2.6 Kisi（Access Link / QR 数字凭证）

- **做什么**：给访客发 **网页开锁链接** 或 **QR**；可不注册、不装 App；适合短期访问。厂商提醒：地理围栏等限制对数字凭证较弱，应设**明确过期**，高安全门更建议正式凭证。  
- **有效期**：管理员设定的 expiry；强调短期使用。  
- **链接**  
  - Digital credentials 文档：<https://docs.kisi.io/dashboard/credentials/digital_credentials/>  

---

## 3. 国内：小区 / 梯控 / 智慧社区（形态更接近本工程）

### 3.1 涂鸦（Tuya）梯控开放能力

- **做什么**：云端人员/楼层权限；支持卡、人脸、**二维码**、密码等；可配动态二维码刷新周期（文档示例默认刷新约 **300000 ms = 5 min**）；App 与设备可共密钥生成动态码。  
- **与本工程**：同属「梯控 + 访客凭证」；涂鸦偏**云端下发/刷新**，本工程偏**离线哈希三槽**。  
- **链接**  
  - 智能梯控接入：<https://developer.tuya.com/cn/docs/iot/Intelligent_ladder_control?id=Kb8av8t6ckykt>  
  - 梯控云 API：<https://developer.tuya.com/cn/docs/cloud/ladder-control-service?id=Kbgn66ojxwetz>  

### 3.2 西墨 Thinmoo / DoorMaster（二维码通道 + 梯控）

- **做什么**：业主发访客二维码；同一码可过小区门、单元门，电梯按权限楼层；支持按次/按天/设备组等权限。  
- **链接**  
  - 二维码出入访客通道梯控方案：<https://www.doormaster.me/二维码出入访客通道梯控解决方案/>  
  -（若链接编码打不开，可从站点首页进入方案页：<https://www.doormaster.me/>）  

### 3.3 住云等物业门禁梯控（临时通行码）

- **做什么**：业主 App 生成**临时通行码**或远程开门；可设有效期与次数；门禁与梯控联动。  
- **链接**  
  - 门禁梯控介绍（含访客授权）：<https://www.cvoon.com/archives/9790>  

> 国内还有云丁、果加、海康/大华智慧社区等同类能力（业主发临时码/二维码、限时限楼层）。公开技术细节与稳定英文文档相对少，选型时以各厂商最新白皮书/对接手册为准。

---

## 4. 算法参照（不是访客产品，但常被拿来比）

| 名称 | 要点 | 链接 |
|------|------|------|
| **RFC 6238 TOTP** | 默认时间步常见 **30 s**；校验允许小窗口（常见 ±1 步） | <https://datatracker.ietf.org/doc/html/rfc6238> |
| **RFC 6238 HTML** | 同上可读 HTML | <https://www.rfc-editor.org/rfc/rfc6238.html> |
| **Authgear · TOTP 开发者说明** | 步长、±1 窗、时钟同步实践 | <https://www.authgear.com/post/what-is-totp/> |
| **Protectimus · Token 时钟漂移** | 硬件 token 漂移与服务端有限回溯 | <https://www.protectimus.com/blog/time-drift-in-totp-hardware-tokens/> |

**对照提醒**：MFA TOTP 的「有效」是秒～分钟级；门禁访客码的「有效」是**访问时段或旅程时间**（分钟～小时）。二者都用时间，但产品目标不同，不能直接把 30 s TOTP 默认搬到多门访客场景。

---

## 5. 横向对照（便于评审）

| 产品 | 凭证形态 | 典型 TTL / 窗口 | 校验方式（公开描述） | 多门/梯控 |
|------|----------|-----------------|----------------------|-----------|
| Vizitor | OTP 数字 | 约 2～30 min 可配 | 在线核验 + 可选开门 | 前台为主 |
| OpenBlue | 邮件 OTP | 访问时段 + buffer | 楼宇策略 | 楼宇访问 |
| Brivo Guest | 6 位 PIN / Mobile | 邀请时间窗 | 控制器/云 | 指定门组 |
| Envoy + ACS | PIN/QR/Link/Mobile | 签到会话 | 集成门禁发临时用户 | 办公多门 |
| ButterflyMX | QR / PIN Pass | 创建时指定起止 | 对讲/键盘本地或云 | 公寓大门为主 |
| Kisi | Link / QR | 设 expiry | 云端开锁 / 扫码 | 指定门 |
| 涂鸦梯控 | 二维码/密码等 | begin/end；动态码可 5 min 刷新 | 云下发 + 设备核验 | **门+梯** |
| 西墨等 | 访客二维码 | 按次/天等 | 读头 + 控制器 | **门+梯** |
| **本工程（对照）** | 4 位口令（哈希） | 槽宽 × 三槽（如 20×3≈1 h） | **设备离线算槽** | 园区/单元/电梯 |

---

## 6. 对本工程的几条启示

1. **办公前台 OTP** 多在 **几分钟～半小时**；你们因「多门步行」把窗口做到约 **1 h**，属于住宅访客里偏长的一档，需用限次、楼层、黑名单等补安全。  
2. **成熟门禁** 更常发「时段内有效的 PIN/QR/链接」，而不是把「时钟差」写进哈希；时差靠校时 + 小校验窗。  
3. **国内梯控** 与「一码多门 + 限楼层」最接近本场景；差异在云端下发 vs 离线三槽哈希。  
4. 对外口径可写成：「类似公寓 Visitor PIN / 临时通行码，有效期按时间槽设计（例如约 1 小时），不是登录用 TOTP。」

---

## 7. 链接速查（复制用）

```text
Vizitor OTP          https://www.vizitorapp.com/otp-based-authentication/
OpenBlue Visitor     https://docs.johnsoncontrols.com/bas/r/OpenBlue/en-US/OpenBlue-Companion-Product-Bulletin/2.22/Features/Visitor-access
Brivo Guest Mgmt     https://support.brivo.com/s/article/Configuring-and-Using-Guest-Management
Envoy Visitors       https://envoy.help/en/collections/1930712-visitors
ButterflyMX Pass     https://help.butterflymx.com/hc/en-us/articles/35345464139156-How-to-Create-and-Use-Visitor-Pass
Kisi Digital Creds   https://docs.kisi.io/dashboard/credentials/digital_credentials/
Tuya 梯控接入        https://developer.tuya.com/cn/docs/iot/Intelligent_ladder_control?id=Kb8av8t6ckykt
Tuya 梯控 API        https://developer.tuya.com/cn/docs/cloud/ladder-control-service?id=Kbgn66ojxwetz
Thinmoo 二维码梯控   https://www.doormaster.me/
RFC 6238 TOTP        https://datatracker.ietf.org/doc/html/rfc6238
```
