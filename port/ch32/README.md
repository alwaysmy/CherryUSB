# Note

## Support Chip List

## CH32FS

- CH32V30X/CH32V20X(USBFS, USBD 用 fsdev)/CH32V10X/CH32L103

## CH32HS

- CH32V30X

## CH58X

- CH57X/CH58X(usbfs)
- CH585(usbhs)

## 移植注意（本 fork 维护内容，上游无此节）

### 1. USB_NOCACHE_RAM_SECTION（.noncacheable 段）

CH32V30x（QingKe V4）**没有 D-cache**，`.noncacheable` 段没有任何硬件收益。而 CherryUSB
默认配置（`cherryusb_config_template.h`）会把 DMA 缓冲与核心状态（如 `g_usbd_core`，
约 1KB）放入该段——若工程链接脚本未定义同名输出段，它成为孤儿段：启动拷贝环
（`_data_lma→_edata`）与 BSS 清零环（`_sbss→_ebss`）都不覆盖它，带初值的静态变量初始化
丢失、静态零初始化预期落空（板上实测可致枚举半途死、行为飘忽）。

**推荐做法（二选一）**：

1. 置空宏（CH32 上推荐）——工程 `usb_config.h` 中：
   ```c
   #define USB_NOCACHE_RAM_SECTION
   ```
   相关缓冲回到普通 `.data`/`.bss`，由启动代码正常初始化与清零。
2. 保留段——按 `cherryusb_config_template.h` 注释把 `.noncacheable` 收编进链接脚本
   `.data` 段（EmoeDAQ 工程 Link.ld 已板级验证），或改用 non_init 语义段（纯缓冲场景）。

### 2. 中断栈深（ch32hs）

- 默认 `WCH-Interrupt-fast`（HPE 硬件压栈）。
- `USB_CH32_USBHS_IRQ_SW_STACK`（USBHS 中断优先级为 0、落在 8 级软件压栈区时必须）：
  寄存器保存与中断内调用链（类驱动/协议栈级）全走 C 栈，**实测 2KB 栈不足，≥4KB 安全**。

### 3. 已知问题索引

见仓库 `docs/REVIEW.md`：PORT-01~08（已修复）、TODO-01/TODO-02（已文档化）、
TODO-03（HS 经三级 Hub 链 BABBLE，留观）。

