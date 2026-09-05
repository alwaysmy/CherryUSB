# 上游状态快照（cherry-embedded/CherryUSB）

> 调研时间：2026-09-06。下次同步上游时更新本文件。

## 仓库概况

| 项 | 值 |
|---|---|
| 上游 master | `1fd876d`（2026-09-02，`[fix] usb_osal_freertos: fix usb_osal_sem_take() and usb_osal_timer_stop() in isr`） |
| 最新 release | v1.6.1（`c9625ff`），master 已领先该 tag 若干提交 |
| Stars / Forks | 2022 / 447 |
| 本 fork 基线 | `1fd876d`，**与上游 master 完全同步（截至调研时）** |

## 开放 issue（9 个，全部为功能请求，无 bug 类）

| # | 标题 | 类型 |
|---|---|---|
| #438 | [Port] CH32X315 USBHS 设备端支持（开放 PR） | PR/新 port |
| #415 | Bulk support for video device | 功能 |
| #381 | Host net framework and multi packet support | 功能 |
| #378 | Linux, FreeBSD compatibility layer | 功能 |
| #330 | WCH support | port |
| #328 | USB over IP support | 功能 |
| #315 | DWC2 Scatter/gather DMA support | 性能 |
| #303 | RA support | port |
| #298 | RK support | port |

**结论**：上游没有已知的未修复 bug 挂在 issue 区。本 fork 的 bug 修复价值主要来自自主审查。

## 近期关闭的 bug（fork 已包含这些修复，避免重复报告）

- #437/#439 CH58x/CH585 USB HS 端点 MPS 截断为 8bit
- #409 CDC ACM SET_LINE_CODING wLength 栈溢出
- #427 adb A_OPEN off-by-one 越界写
- #428 SetInterface alternate setting 端点处理
- #426 parse_config_descriptor ep[] 越界
- #403 CH58x USBFS DMA 地址 16bit 溢出
- #406 设备/主机模式切换后中断风暴（v1.5.0）
- #335~#342 hub 枚举/USB3.0 hub/ESP32-P4 DT 错误一批
- #339 MSC 单 LUN 设备回归
- #346 DWC2 反复插拔异常、#338 STM32F446 FIFO 断言

## 其他 fork 调研

- 抽样了推送时间最近的 25 个 fork + 全量 445 个 fork 列表：**没有任何 fork 的 master
  含有未合入上游的实质性 bugfix**（KevinACoder/sakumisu/vossstef 等只是自动同步工作流
  造成的 pushed_at 领先，commit 均为 1fd876d）。
- `sakumisu/CherryUSB` 为上游作者本人 fork（12 stars），master 与上游一致，无额外 WIP 提交。
- 结论：暂无从其他 fork 摘取修复的必要，后续可定期复查（同一 API，按 pushed_at 排序看 AHEAD 的）。
