# GPRS 偶发掉线导致设备不上线 — 排查与修复总结

## 现象

`timeSync` 发出后约 3.2 秒，LINKA 拉低 → `Link down confirmed` → 状态机退回 State1 → `serverTime` 回包丢失 → 首心跳永远被 `NetLiveness_IsWaitingTimeSync()` 死扣 → 设备不上线。**偶发**，不是每次都掉。

---

## 排查路径

| 假设 | 验证 | 结论 |
|---|---|---|
| PC5 引脚接错 | 用户确认 PC5 = LINKA，Socket A 连接指示 | ✅ 引脚正确，排除 |
| 模块 MQTT 配置错 | 加 4 条只读 AT 查询（WKMOD/MQTTMOD/HEARTEN/SOCKASL） | ✅ 全对，排除 |
| Socket A TCP 真的断了 | 进 NORMAL 后查 SOCKALK = Disconnected，但 MQTT publish/回包正常 | 揭示 SOCKALK 在 MQTT 模式下查的不是 MQTT 会话用的 Socket，**MQTT 模式下 SOCKALK 永远 Disconnected，LINKA 实际反映模块内部 MQTT 会话状态** |
| AT 指令干扰 MQTT CONNECT | 对比掉线/成功两次时序：掉线那次 probe 刚推完 14 条 AT 紧接着 LINKA 变高 | **锁定根因** |

---

## 根因

`gprs_at.c` State1→2 切换处**漏了 `UartTx_Queue_Init()`**。

- `hard reset` 分支和 `link down` 分支都清了队列
- **唯独 `link up` 这条路径没清**
- 结果：State1 里 probe 推的 14 条 AT 还在队列里，State2 又推 3 条 AT init → **17 条 AT 交织发出**，模块内部正在做 MQTT CONNECT，被 AT 指令打断 → 会话建立后立刻断 → LINKA 掉

---

## 修复（一行）

`gprs_at.c` 的 State1→2 link up 分支，在 `break;` 前面插一行：

```c
        if (s_link_state && (g_atflag_online || s_link_down_occurred))
        {
            s_link_down_occurred = 0u;
            s_state = ST_AT_PUSH;
            GPRS_Printf("[GPRS] State1->2: link up, start AT init\r\n");
            UartTx_Queue_Init();   /* 新增：清掉 probe 残留，避免干扰 MQTT CONNECT */
            break;
        }
```

---

## 验证

烧入后 2 分钟连续在线：

- `timeSync` 回包 ✅
- `heartbeat ack` ✅
- `property post` ✅
- 2 分钟后第二次 `heartbeat ack (miss=0)` ✅
- 全程无 `Link down confirmed`

---

## 经验沉淀

1. **状态机切换路径要对齐清理动作**——3 条进入 ST_AT_PUSH 的路径，2 条清了队列 1 条没清，就是这条漏的出问题。
2. **偶发 ≠ 硬件问题**——根因是软件时序竞态（probe AT 队列残留 × link up 时机），偶发只是因为 link up 恰好落在 probe 推完 AT 之后。
3. **AT 模块的同名命令在不同工作模式下语义可能完全不同**——SOCKALK 在 MQTT 模式下查的不是 MQTT 会话用的 Socket，差点被误导。
