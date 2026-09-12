# Changelog（本 fork 维护记录）

本文件记录 **alwaysmy/CherryUSB fork 相对上游 cherry-embedded/CherryUSB 的全部变更**。
格式参考 [Keep a Changelog](https://keepachangelog.com/)；版本号沿用上游版本并加 fork 后缀。

上游同步基线在每个版本的 `Base` 字段标明。bug 详情见 `docs/REVIEW.md` 对应编号。

## [Unreleased]

### Fixed
- **TODO-01/TODO-02（分支 `fix/todo-docs`）**：`cherryusb_config_template.h` 的
  `USB_NOCACHE_RAM_SECTION` 注释补全孤儿段风险与三选一处理指引，并把"置空宏"提为多数
  无 D-cache 目标（STM32F1/F4、GD32、CH32、AT32 等）的**首选**（附风险不对称说明；
  置空 / 并入 .data 的已验证 LD 片段 / non_init 语义）；AGENTS.md 验证手段补 ISR 软压栈
  栈深 ≥4KB 要求。
- **port/ch32/ch32hs**：修复 `76faec6` 引入的移植回归——`CH32_USBHS_IRQ_ATTR` 宏定义后未贴到
  `USBHS_IRQHandler`（外部 agent 在 CH32V307 机器码级实证：ISR 被编译成普通函数，epilogue 以
  `ret` 而非 `mret` 结尾，首次中断后全局中断永久失效，枚举报 "Device Descriptor Request Failed"）。
  现属性贴在声明上（与上游同构），并完成全库 `*_IRQHandler` 属性审计（仅 WCH RISC-V 端口需要
  interrupt 属性，ch32fs/ch58x 本就正确，ARM 端口无需，无同类问题）。
- **core（CORE-35）**：`usbd_class_event_notify_handler` 不再把一切非 NULL `arg` 当
  `usb_interface_descriptor*` 解引用——仅 `USBD_EVENT_SET_INTERFACE` 按描述符过滤，其余事件直接
  广播（补丁取自 USBTMC 工程已验证实现）。

### Added
- **core**：新增 `USBD_EVENT_SET_HALT` / `USBD_EVENT_CLR_HALT` 事件，并在
  CLEAR_FEATURE / SET_FEATURE(ENDPOINT_HALT) 处通知类驱动（arg = 端点地址），
  供 USBTMC 等需要感知主机 halt 操作的类驱动使用（取自 USBTMC 工程已验证实现）。
- **port/ch32/ch32hs 全量修复**（来源：EmoeDAQ 项目现场验证的修正，批判性复核后移植，详见 `docs/REVIEW.md` PORT-01~07）：
  - 寄存器头 `USBHS_EP9_T_TYP` 位定义错误（1<<8 与 EP8 冲突，应为 1<<9）。
  - `usb_dc_init` 补齐 WCH 官方 FORCE_RST 复位序列与 PHY 稳定延时，消除间歇性枚举失败。
  - `usbd_get_port_speed` 改为读 `SPEED_TYPE` 寄存器（0x00=FS/0x01=HS），**支持完整 FS 回退**
    （应用侧按速度返回 HS/FS 两份配置描述符与 0x07 型 other speed 描述符，配方见函数头注释）。
  - 新增 `ch32_usbhs_ep0_prepare()`：EP0 全量准备，复位处理前后幂等调用，消除 `UEP0_DMA/RX_CTRL` 恢复窗口。
  - ISR 事件因果序重排（DETECT→SETUP→TRANSFER）；SETUP 后强制 EP0 DATA1；TRANSFER 标志改为处理后清除。
  - 使能挂起/恢复中断并按 `MIS_ST` 区分上报（原端口完全不上报 suspend/resume，remote wakeup 判定失效）。
  - `USBHS_IRQHandler` 压栈方式改为可配置：默认保持上游 `WCH-Interrupt-fast`（HPE），定义
    `USB_CH32_USBHS_IRQ_SW_STACK` 时切换为软件压栈（USBHS 优先级落在 8 级软件压栈区时必须）。

### Added
- `docs/REVIEW.md` 新增 **TODO（待修）** 一节：核对 USBTMC 工程上板实测问题清单后，
  立项 TODO-01（`.noncacheable` 孤儿段致 core 状态/DMA 缓冲不初始化不清零，含参考 LD 片段与
  三种修复方向）、TODO-02（ISR 软压栈栈深 ≥4KB 文档化）、TODO-03（HS 经三级 Hub 链 BABBLE
  观察项，速率编码已由 WCH 官方头排除）；其 P0/P1/P2 经核对在 master 已修（b7dd9ee/580ccfd）。
- `AGENTS.md`：fork 维护规则（禁止主动提 PR、最小 diff、修复三步流程、上游同步策略）。
- `docs/REVIEW.md`：首次全库代码审查发现总表（CORE/OSAL/CLS 三个系列共 100 余条，
  含 8 条 P0），并附上游近期修复的完整性核查结论。
- `docs/UPSTREAM_STATUS.md`：上游 issue / fork 调研快照（2026-09-06）。
- 本 CHANGELOG。
- 核查用户 EmoeDAQ 项目对 port/ch32/ch32hs 的 7 项本地修正（基线 v1.5.0）：全部判定正确，
  对应问题在 1fd876d 基线仍存在，已登记为 PORT-01~07 待移植（详见 docs/REVIEW.md）。

### 基线说明
- **Base**: upstream master `1fd876d`（2026-09-02，v1.6.1 之后，含 PR #440）。
  fork master 与上游完全同步，本版本无代码变更，仅新增维护文档。
- 首次审查结论摘要：上游 issue 区无未修复 bug（9 个开放 issue 均为功能请求）；
  其他 445 个 fork 的 master 无可摘取的独立修复。维护价值在本 fork 的自主审查
  （详见 `docs/REVIEW.md`），修复顺序建议见该文档"建议修复顺序"。

[Unreleased]: https://github.com/alwaysmy/CherryUSB/compare/v1.6.1...HEAD
