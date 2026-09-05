# AGENTS.md — CherryUSB Fork 维护指南

本仓库是 [cherry-embedded/CherryUSB](https://github.com/cherry-embedded/CherryUSB) 的 fork，
用途：**自主维护 bug 修复与性能优化**。上游 issue 修复周期长、部分问题无人认领，本 fork 承担
"自用维护版" 的角色。AI 代理（agent）在本仓库工作时必须遵守以下约定。

## 铁律（必须遵守）

1. **禁止主动提 PR / issue 到上游**。除非用户明确提出要求，否则所有修改只留在本 fork。
2. **保持与上游的 diff 最小化**。修复应尽量局部、风格贴近上游（Apache-2.0 头、
   `.clang-format` 格式化），便于将来与上游同步或反向贡献（在用户要求时）。
3. **每个 bug 修复都要走三步流程**：
   1. 在 `docs/REVIEW.md` 登记条目（编号、位置、级别、状态 open）；
   2. 修复代码，提交信息用上游风格：`fix(<scope>): <what>` 或 `[fix] <scope>: <what>`；
   3. 在 `CHANGELOG.md` 追加记录，并把 REVIEW.md 对应条目状态改为 `fixed`（附 commit）。
4. **修复前先复核**。审查结论（包括 AI 生成的）必须逐条打开代码核实后再动手，
   注意排除"上游已在后续提交修复"的情况（先同步上游再看）。
5. **不重构、不改无关代码、不全局重命名**。性能优化必须有明确动机和测量，默认保守。

## 仓库结构

| 目录 | 内容 |
|---|---|
| `core/` | 设备栈核心 `usbd_core.c`、主机栈核心 `usbh_core.c`（审查重点） |
| `class/` | 各 USB 类驱动（cdc/msc/hid/audio/video/adb/wireless(rndis)/hub/...） |
| `osal/` | RTOS 适配层（freertos/rtthread/threadx/zephyr/nuttx/liteos/idf） |
| `port/` | 各 IP/芯片 HCD 与 DCD（dwc2/fsdev/ehci/musb/xhci/ch32...） |
| `common/` | 公共头（usb_def.h、usb_list.h、usb_mempool.h、usb_ringbuffer.h...） |
| `demo/`、`platform/`、`third_party/` | 示例与集成，一般不改 |
| `docs/REVIEW.md` | **bug 审查发现总表（持续维护，修改前必读）** |
| `docs/UPSTREAM_STATUS.md` | 上游 issue / fork 调研快照（定期更新） |
| `CHANGELOG.md` | 本 fork 的变更日志（每改必记） |

## 上游同步

- `origin` = 本 fork（alwaysmy/CherryUSB），上游需手动添加：
  `git remote add upstream https://github.com/cherry-embedded/CherryUSB.git`
- 同步原则：**只取 bugfix，不盲目 merge 整个 master**（避免带入未经审查的新功能）。
  优先 `git fetch upstream && git cherry-pick <commit>`，冲突时人工评估。
- 当前基线：upstream master `1fd876d`（2026-09-02，v1.6.1 之后，含 PR#440 修复）。
  同步前先更新 `docs/UPSTREAM_STATUS.md`。

## 验证手段

无主机侧单测。可用：

1. **cppcheck**（上游 CI 同款）：
   ```
   cppcheck --enable=warning,portability,performance --language=c --platform=unix32 \
     --std=c99 --quiet --force . -i third_party/ -i class/template -i port/template/ \
     -i tests/ --include=tests/hpmicro/inc/usb_config.h
   ```
2. **配置矩阵编译冒烟**：改到 `#ifdef` 分支内的代码（如 `CONFIG_USBDEV_EP0_THREAD`）
   必须显式打开该宏编译验证——上游 CI 不覆盖所有配置组合，历史上在此翻过车。
3. 有硬件时按 `embedded-test` skill 流程做板级回归（枚举 + 数据吞吐 + 反复插拔）。

## 审查关注模式（本仓库的历史高发 bug 类型）

- **主机侧解析设备数据**（NCM/RNDIS/MSC/HID/描述符）：一切长度、索引、偏移字段都是
  不可信输入，必须相对缓冲区实际长度做上界检查；警惕无符号减法下溢做循环计数。
- **设备侧解析主机请求**：`wIndex`/`wValue` 作数组下标前必须校验（如 intf/LUN/属性索引）。
- **ISR 安全**：osal 各后端的 sem_take/mq_send/timer_* 是否有 ISR 分支；修复必须覆盖
  全部 7 个 osal 后端，不能只修一个。
- **ms 单位 vs tick**：rtthread/liteos/threadx 的 timer 与超时参数。
- **DMA 与 cache 一致性**（ehci/dwc2 的 cache clean/invalidate）。
- **错误路径资源泄漏**：class connect 失败分支是否释放 devno/priv/互斥量。

## 用户的本地参考资料

用户会陆续提供此前人工发现的 bug 备忘（路径以后续提供的为准）。收到后：
优先核实并修复，在 REVIEW.md 中以 `来源=local-ref` 登记，与本轮自主审查的条目统一编号管理。
