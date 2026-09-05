# 代码审查发现总表（持续维护）

- 基线：upstream master `1fd876d`（2026-09-02，v1.6.1+）。首次审查日期：2026-09-06。
- 审查方式：人工复核 core 层 + 3 路并行深度审查（class 驱动 / osal+common / adb·video·audio·vendor）。
- **修复前必读**：先 `git fetch upstream` 确认上游尚未修复该问题；修复流程见 `AGENTS.md`。
- 状态：`open` 未修 / `fixed` 已修（附 commit）/ `upstream` 已在上游修复 / `wontfix`。
- 复核：`✅` 表示已逐行人工核实代码；`⏳` 表示来自审查报告、摘录可信但尚未逐行复核。

## 严重度定义

| 级别 | 含义 |
|---|---|
| P0 | 恶意/畸形对端可稳定触发内存破坏、信息泄露或死锁 |
| P1 | 功能失效、常见场景丢数据、可致崩溃或构建失败 |
| P2 | 边缘场景错误、特定配置下失效 |
| P3 | 健壮性隐患、理论问题 |

---

## P0（恶意对端可打穿）

| ID | 位置 | 复核 | 说明 |
|---|---|---|---|
| CLS-01 | class/cdc/usbh_cdc_ncm.c:302-329 | ✅ | NCM RX：`wNdpIndex`/`wLength`/datagram `index+length` 全部未对 `g_cdc_ncm_rx_length` 校验；`wLength<8` 时 `(uint16)(wLength-8)/4` 下溢 → 越界读 + 任意数据注入网络栈。参照设备端 usbd_cdc_ncm.c 的完整检查改写 |
| CLS-02 | class/wireless/usbh_rndis.c:547-573 | ✅ | RNDIS RX 聚合循环：`MessageLength==0` 死循环卡死 rx 线程；`MessageLength>剩余` 时 uint32 下溢越界扫描；`DataOffset/DataLength` 无界 → 任意地址数据送网络栈。注意上游 740f349 只修了外层分支 |
| CLS-03 | class/audio/usbh_audio.c:647,655,685,691,701,714 | ⏳ | `as_msg_table[cur_iface-ctrl_intf-1]` 上界仅由设备 IAD `bInterfaceCount`（0~255）决定，数组仅 `CONFIG_USBHOST_AUDIO_MAX_STREAMS=2` 项 → 恶意 UAC 设备受控 OOB 写全局区 |
| CLS-04 | class/video/usbh_video.c:442-480 | ⏳ | `format_index/frame_index` 取自设备描述符（≤255），仅 `USB_ASSERT` 挡（死循环或空操作）→ 对 `format[3]/frame[12]` OOB 写 |
| CLS-05 | class/audio/usbh_audio.c:744-746 | ⏳ | feature unit `bLength-7`（≤248）memcpy 进 6 字节 `bmaControls`，仅 assert 挡 → OOB 写 |
| CLS-06 | class/vendor/net/usbh_rtl8152.c:2188-2210 | ⏳ | RX 描述符 `len`（设备可控 ≤32767）无剩余长度校验 → 32KB 级越界读数据当以太网帧注入 TCP/IP 栈；`g_rtl8152_rx_length` 无符号下溢 + `data_offset` uint16 回绕 → rx 线程死循环 |
| CLS-07 | class/vendor/net/usbh_asix.c:723-741 | ⏳ | RX 头 `len`（≤2047）只校验反码自洽不校验剩余空间 → 越界读 + 帧注入 + 下溢死循环 |
| CLS-08 | class/adb/usbd_adb.c:117-118,129,164 | ⏳ | 消息头 `nbytes==24`、`data_length<4096` 仅靠 `USB_ASSERT`（`CONFIG_USB_ASSERT_DISABLE` 时为空操作；否则中断里 while(1) 死机）→ 关断言工程被恶意 host OOB DMA 写 / A_OPEN `payload[data_length]` OOB 写。上游 adb 两次修复皆因依赖断言而不彻底 |

## P1（功能失效 / 崩溃 / 构建失败）

| ID | 位置 | 复核 | 说明 |
|---|---|---|---|
| CORE-01 | core/usbd_core.c:1351-1362 | ✅ | `CONFIG_USBDEV_EP0_THREAD` 打开时 `usbd_initialize()` 花括号不平衡 → 编译失败。上游 3d14b52 把 `while(1){}` 改为 `return` 时遗留。该功能自 3d14b52 起在上游即不可用 |
| CORE-02 | core/usbd_core.c:650,655 | ✅ | GET/SET_INTERFACE 的 `intf_num=wIndex(0~255)` 无校验直接索引 `intf_altsetting[16]` → 恶意 host 越界读写 g_usbd_core 内部（含 `event_handler` 函数指针前的字段），可破坏设备状态 |
| CORE-03 | core/usbd_core.c:837-843 | ✅ | MS OS v1 vendor request `comp_id_property[setup->wValue]`（wValue 为 host 可控 uint16，数组以 NULL 结尾）→ 越界读出野指针作为 `*data`，把任意地址内容回送给 host（信息泄露/崩溃）。应遍历数组计数并判 wValue 越界 |
| CLS-09 | class/msc/usbd_msc.c:716-733（CBWDecode） | ✅ | CBW `bLUN` 从不校验，直接作 `scsi_blk_nbr[]/scsi_blk_size[]`（`CONFIG_USBDEV_MSC_MAX_LUN`，默认 1）下标 → host 发 bLUN≥1 的 CBW 读出相邻成员；块大小恰为 0 时除零 HardFault |
| CLS-10 | class/msc/usbd_msc.c:551-561,584-594,619-629,648-658 | ⏳ | READ/WRITE(10/12) 的 `start_sector+nsectors`（uint32）可回绕绕过 LBA 上界检查 → 越界读返回给 host（泄露）/越界写破坏存储。改为无回绕形式 `nsectors<=blk_nbr && start_sector<=blk_nbr-nsectors` |
| CLS-11 | class/cdc/usbh_cdc_ecm.c:278-286 | ✅ | RX 判 `< 1514`，恰好 1514 字节的标准 MTU 帧被当溢出丢弃 → 静默丢包。改 `<=` |
| CLS-12 | class/cdc/usbd_cdc_ecm.c:232-242 | ✅ | TX 仅钳制 `p->tot_len`，memcpy 循环仍拷整条 pbuf 链 → 超长帧写穿 `g_cdc_ecm_tx_buffer`。循环内按缓冲上限截断 |
| CLS-13 | class/hub/usbh_hub.c:526-537 | ✅ | 清 change 位失败（拔线竞态常态）时 `continue` 不推进 mask/feat → `while(portchange)` 死循环，hub 线程挂死，整条总线插拔失效。失败应 break |
| OSAL-01 | osal/idf/usb_osal_idf.c:46-49 | ✅ | `xSemaphoreCreateCounting(0, max_count)` 参数写反（应为 `(max_count, 0)`）→ ESP-IDF 下计数信号量容量 0，give 永远失败（mempool/display 受影响），开断言则 panic |
| OSAL-02 | osal/usb_osal_nuttx.c:258-270 | ⏳ | NuttX `usb_osal_mq_send` 注释称 ISR 可用，实际 `file_mq_send` 非中断安全 → HC ISR → hub 线程唤醒链路 PANIC |
| OSAL-03 | osal/usb_osal_threadx.c:34-48,290-303 | ⏳ | 自删线程 `mq_send` 唤醒清理线程后才 `tx_thread_terminate`，低优先级线程被抢先后 `tx_byte_release` 释放仍在运行的线程 → 栈被复用灾难性破坏；`tx_thread_delete` 返回值未查 |
| OSAL-04 | osal/usb_osal_freertos.c:196-201（zephyr/rtthread/liteos/nuttx 同型） | ⏳ | `usb_osal_timer_delete`：`xTimerStop/Delete(block=0)` 返回值不查 + 立即 free 包装结构，与 timer daemon 中"已到期/回调执行中"竞态 → UAF。需延迟释放 |
| OSAL-05 | osal/usb_osal_freertos.c:63,83,135,152,208,223,237,249 | ⏳ | 1fd876d 依赖 `xPortIsInsideInterrupt()`，仅部分 Cortex-M port 提供 → CM0/RISC-V 等目标编译失败。需 `USB_OSAL_IN_ISR()` 归一宏兜底 |
| CLS-14 | class/adb/usbd_adb.c:287-304 | ⏳ | `usbd_abd_write` 无 `len<=MAX_PAYLOAD` 校验 → 应用传大 buffer 写穿 tx_packet 全局缓冲 |
| CLS-15 | class/adb/usbd_adb.c:82-110,287-309 | ⏳ | 应用线程与 bulk_out ISR 共用 tx_packet 无互斥 → A_WRTE 高频时撕包/双 start_write |
| CLS-16 | class/vendor/display/usbd_display.c:88-131 | ⏳ | 帧偏移边界仅 assert；恶意 host 持续满包 → 关断言 OOB DMA 写，开断言中断死循环 |

## P2

| ID | 位置 | 复核 | 说明 |
|---|---|---|---|
| OSAL-06 | osal/usb_osal_zephyr.c:134-141 | ⏳ | `usb_osal_sem_take` 缺 `k_is_in_isr()` 分支（mq 有、sem 没修全），timer 回调链路里 sem_take 触发内核断言/破坏 |
| OSAL-07 | osal/usb_osal_idf.c:56-63 | ⏳ | `usb_osal_sem_take` 缺 ISR 分支（同文件 give/mq 有） |
| OSAL-08 | osal/usb_osal_nuttx.c:122-138 | ⏳ | `usb_osal_sem_take` ISR 中 `nxsem_wait` → PANIC（潜在） |
| OSAL-09 | osal/usb_osal_rtthread.c:67-86 | ⏳ | `rt_sem_take` 无 ISR 守卫（RT_DEBUG_NOT_IN_INTERRUPT） |
| OSAL-10 | osal/usb_osal_liteos_m.c:86-97 | ⏳ | `LOS_SemPend` 无 `OS_INT_ACTIVE` 守卫（同文件 mq 有、sem 漏） |
| OSAL-11 | osal/usb_osal_rtthread.c:164 | ⏳ | `rt_timer_create` 把 ms 当 tick 传（缺 `rt_tick_from_millisecond`，同文件 sem/mq 都换了）→ `RT_TICK_PER_SECOND!=1000` 时全部超时错倍；`RT_TIMER_FLAG_SOFT_TIMER` 依赖 `RT_USING_TIMER_SOFT` 未强制 |
| OSAL-12 | osal/usb_osal_liteos_m.c:86-97,198,214-216 | ⏳ | LiteOS sem/queue/swtmr ms 未转 tick（缺 `LOS_MS2Tick`），常见 100Hz tick 下错 10 倍 |
| OSAL-13 | osal/usb_osal_nuttx.c:313-326 | ⏳ | 周期 wd timer 先回调后重挂 → 回调内 `usb_osal_timer_stop` 失效（stop 后又被 wd_start），且周期漂移 |
| OSAL-14 | osal/usb_osal_threadx.c:231-244 | ⏳ | `tx_timer_change` 不允许 ISR 调用（返回 TX_CALLER_ERROR 被忽略）；在树触发点：hub NAK 重试在 ISR 里 timer_start（usbh_hub.c:315） |
| OSAL-15 | osal/usb_osal_zephyr.c:60-82 | ⏳ | 按句柄删线程 `k_thread_abort+k_free`，SMP 下被 abort 线程可能仍在用栈 → UAF（自删路径上游已延迟释放，外部句柄路径未覆盖） |
| OSAL-16 | common/usb_list.h:60-65 | ✅ | `usb_slist_insert`：`usb_slist_add_tail(next, l)` 参数写反（next=NULL 时对 NULL 解引用；应为 `add_tail(l, n)`）。当前无在树调用者，属地雷 |
| OSAL-17 | common/usb_list.h:449-453 | ✅ | `usb_dlist_for_each_entry_safe_reverse` 用未定义的 `field`（应为 `member`），恰有同名符号时按错误偏移取 container_of |
| OSAL-18 | common/usb_list.h:222-226 | ✅ | `usb_slist_for_each_entry_safe` 空表时 `pos=container_of(NULL)`，初始化 n 时读 0 附近地址（dlist 版有自指头节点故安全） |
| OSAL-19 | common/usb_memcpy.h:14-20,63-81 | ⏳ | 非 4 对齐分支 `dword2array` 按固定小端搬 32 位读数 → 大端 CPU 上 memcpy 内容字节反转；且 `#define memcpy usb_memcpy` 全局替换放大影响 |
| OSAL-20 | common/usb_mempool.h:113-129 | ⏳ | `usb_mempool_recv` 放行多消费者但底层 `usb_ringbuffer_read` 非多读者安全（撕裂读）；`usb_mempool_create` 容量非 2 的幂时静默失败。需注明 SPSC 约束或加锁 |
| CLS-17 | usbh_cdc_ncm.c:348-382、usbh_rndis.c:591-617 | ⏳ | TX 无 `buflen<=MAX_TX-头长` 校验 → 超长帧把 NDP16/RNDIS 头写到缓冲区外；NCM `wBlockLength` uint16 截断 |
| CLS-18 | class/hid/usbh_hid.c:217-229 | ⏳ | connect 假定接口描述符后紧跟 HID 描述符（`p+9`/`subdesc[0]` 无越界判断）→ 恶意短描述符堆越界读（≤3 字节） |
| CLS-19 | class/hid/usbh_hid.c:448-493 | ⏳ | `usbh_hid_report_convert` 无缓冲长度参数，`report_bit_offset/report_count` 来自设备 → 转换输出可指向 g_hid_buf(32B) 外数百字节 |
| CLS-20 | class/cdc/usbd_cdc_acm.c:9-10,42-48 | ⏳ | `parity_name[bParityType]`(5 项)/`stop_name[bCharFormat]`(3 项) 直接以 host 可控值索引，DBG 日志开启时 `%s` 解引用越界指针 |
| CLS-21 | class/wireless/usbh_rndis.c:543-576 | ✅ | RX 以 `len%MPS!=0` 作为多包聚合判定条件 → 恰为 MPS 整数倍的合法聚合（规范允许，靠短包/ZLP 结尾）整包丢弃报 overflow |
| CLS-22 | class/vendor/net/usbh_asix.c:771 | ⏳ | `if (!(buflen + 4) % mps)` 运算符优先级错误（`!` 先于 `%`），填充分支恒不执行 → 帧长恰为 MPS 整数倍时缺尾 padding 被设备丢帧（确定性丢帧） |
| CLS-23 | class/serial/usbh_ch34x.c:54,243 | ⏳ | `c/baudrate` 波特率 0 除零 HardFault；`get_baudrate_div` 返回值忽略 |
| CLS-24 | class/video/usbh_video.c:370 | ⏳ | `1000/(dwDefaultFrameInterval/10000)` 设备声明 <10000 时除零，插入即崩 |
| CLS-25 | class/video/usbh_video.c:436,445,168-181,360-371,397 | ⏳ | `num_of_formats/num_of_frames` 无上界（数组 3/12）→ open/list_info 循环越界读 + `format_type[garbage]` 字符串表索引；`intf[intf+1]` 可越界读 intf 数组 |
| CLS-26 | class/serial/usbh_serial.c:271-273,215-233 | ⏳ | 拔出未 open 的串口：close 因 ref_count==0 不杀 URB → remove 后 HCD 回调解引用已 memset 的结构（NULL driver / NULL sem） |
| CLS-27 | class/audio/usbd_audio.c:233-238 | ⏳ | UAC2 `usbd_audio_get_sampling_freq_table` weak 默认不赋值 → 枚举必发 RANGE 请求时 NULL deref；拷贝长度未对 ep0 缓冲钳制 |
| CLS-28 | class/video/usbd_video.c:768-770,823-825 | ⏳ | `dwMaxPayloadTransferSize<12` 时 `-=stream_headerlen` 下溢，MIN 失效 → memcpy/写长度失控 |
| CLS-29 | usbd_adb.c:89,302、usbd_gamepad.c:96,174、usbd_display.c:67,100,127 | ⏳ | busid 硬编码 0（同文件其余路径用 busid 参数）→ `CONFIG_USBDEV_MAX_BUS>1` 时功能错乱 |

## P3（健壮性，择机处理）

| ID | 位置 | 说明 |
|---|---|---|
| CORE-24 | core/usbd_core.c:1229-1234 | `usbd_add_interface` 不查 `intf_offset<16`，注册 >16 接口静默越界 |
| CORE-25 | core/usbd_core.c:343,412,584 | `config_descriptor_callback` 无 NULL 检查（usbd_get_descriptor 有），配置缺失时 NULL deref |
| CORE-26 | core/usbd_core.c:999 | RESET 事件直接解引用 `device_descriptor_callback()` 返回值，断言关闭时 NULL deref |
| CORE-27 | core/usbh_core.c:769-777 | `usbh_get_string_desc` 奇数 bLength 时循环写入数比检查值多 1（极小 output_len 时 1 字节溢出） |
| CORE-28 | common/usb_util.h:123-128 | `LO_BYTE/HI_BYTE` 宏参数未括号，`LO_BYTE(a|b)` 优先级错 |
| CORE-35 | core/usbd_core.c:938-954（notify_handler） | `usbd_class_event_notify_handler` 把一切非 NULL `arg` 一律当作 `struct usb_interface_descriptor*` 解引用做接口过滤。当前上游只有 SET_INTERFACE 传 arg 故未触发，但 API 契约脆弱：任何事件带非描述符 arg（如端点号）即越界读。USBTMC 扩展（CH32_USBTMC 项目）已验证更稳妥写法：仅 `event == USBD_EVENT_SET_INTERFACE` 时按描述符过滤，其余事件直接广播
| OSAL-21 | osal/usb_osal_freertos.c:167-172 | `__usb_timeout(TimerHandle_t*)` 回调签名与 `TimerCallbackFunction_t` 不符（按值传），靠指针宽度巧合工作 |
| OSAL-22 | osal/usb_osal_zephyr.c:126-132 | `usb_osal_sem_delete` 先 take 后 free，仍有等待者时 UAF（zephyr 无 delete 语义，需文档化） |
| OSAL-23 | osal/usb_osal_threadx.c:21-25,67-71,311-315 | 创建失败 `while(1)` 死等；`usb_osal_malloc` 用 `TX_WAIT_FOREVER` |
| OSAL-24 | osal/usb_osal_nuttx.c:355-361 | `wd_cancel+kmm_free` 与回调执行无同步（SMP 罕见） |
| OSAL-25 | osal/usb_osal_zephyr.c:26-45 | 动态线程栈尺寸/对齐未按 `K_THREAD_STACK_LEN` 调整，MPU 平台可能 fault |
| OSAL-26 | osal/usb_osal_freertos.c:141 / idf:128 | 任务态阻塞用 `0xffffffff` 而非 `portMAX_DELAY`（16 位 tick 型 port 语义偏差） |
| CLS-30 | usbh_cdc_ecm.c:61-69、usbh_cdc_ncm.c:92-100 | 中断 notify 解析前不查 `actual_length`，短包时用陈旧数据 |
| CLS-31 | usbd_cdc_ecm.c:68,275 | `usbd_cdc_ecm_send_notify(speed)` 形参无 NULL 防护（公共 API） |
| CLS-32 | usbh_msc.c:178-186,259-297 | CSW 不查 dTag/实际长度；modeswitch 可用 NULL 缓冲发起 bulk IN（DMA 写 NULL）；get_maxlun 失败路径泄漏 devno/priv |
| CLS-33 | usbh_cdc_ncm.c:302-329 | NTH16/NDP16 结构体直读，大端平台失效（设备端用 GET_LE16/32，风格不一致） |
| CLS-34 | usbh_hub.c:334-444,374,384 | connect 失败分支泄漏 hub 槽位（未 free）；`parse_hub_descriptor` 返回值忽略 |
| CLS-35 | usbh_cdc_ncm.c:197-222、usbh_msc.c:301-308、usbh_rndis.c:355-363 | 畸形设备缺端点时 bulk 指针 NULL，后续解引用崩溃；connect 末尾应校验 |
| CLS-36 | usbd_cdc_acm.c（SET_LINE_CODING） | 上游溢出修复后仍未校验 `wLength>=7`，短包接受垃圾线编码 |
| CLS-37 | usbh_audio.c:336,269,742,513,570 | close 用硬编码 altsetting=1 取 ep；`as_msg_table` 负下标边缘路径；`bControlSize` 无上界（≤255，读越界 6 字节数组） |
| CLS-38 | usbh_serial.c:624-626,487-521 | CLI `snprintf` 截断返回值致越界读；open 后未 set_attr 直接 read 的阻塞路径 |
| CLS-39 | usbd_mtp.h:28-39 | `MTP_S_IRWXU` 等引用未定义宏，消费者编译失败（class/mtp 仅有头文件） |
| CLS-40 | usbd_display.c:42,56,104-106 | dummy 缓冲 512 固定，SS mps=1024 时溢出；`frame_size` 采用设备声明值而非实收字节数 |
| CLS-41 | usbd_adb.c:163-177,214-219 | A_OPEN 未知目标不回包（host 挂等）；sync: 数据被静默丢弃（file_read 通知未实现） |
| CLS-42 | usbd_video.c:578-612,637-639,834-838 | GET_LEN 只回 1 字节（规范 2）；未识别 control 返回 0 且回送 ep0 旧内容（泄露）；零拷贝路径首块仍 memcpy |
| CLS-43 | usbh_video.c:78-88 | get 重试后短读返回正值，调用方误判成功 |
| CLS-44 | usbd_dfu.c:9-11,38,99-106,325 | `g_usbd_dfu` 不按 busid 索引；APP_IDLE 收 DETACH 无状态迁移；UPLOAD 回调 weak 空实现时 `actual_length` 未初始化 |

## PORT（port/ch32/ch32hs，来自 EmoeDAQ 项目本地修正，2026-09-06 核实）

> 来源：用户项目 D:\MyProjects\EmoeR_D\EmoeDAQ\0_FW\V2_CH32\CH32V30x_USB_AD7175（基线 v1.5.0）
> 已在该项目验证：1h 长测 25kSPS+1kHz 触发，USB 掉线 0 次。以下问题在我们 fork 基线 1fd876d 上**全部仍存在**。
> **状态：PORT-01~07 已全部移植入 fork（已修复）**。修法批判性复核结论：均为该 IP 下的正确/最优做法，另附两点保留意见：
> ① PORT-02 的 TRANSFER_FLAG 改为块尾清除，相对上游块首清除在极窄窗口下有理论事件丢失面，因有硬件长测背书予以保留；
> ② PORT-07 不无脑照搬，改为 `USB_CH32_USBHS_IRQ_SW_STACK` 宏切换，默认仍为上游 HPE fast，不影响既有用户。

| ID | 位置 | 复核 | 说明 |
|---|---|---|---|
| PORT-01 | port/ch32/ch32hs/usb_ch32_usbhs_reg.h:383 | ✅ | `USBHS_EP9_T_TYP (1 << 8)` 位定义错误（与 EP8_T_TYP 冲突），应为 `1 << 9`（R_TYP=1<<25 同系已对）。当前无在树调用者（ISO 类型走裸移位），属潜伏地雷 |
| PORT-02 | port/ch32/ch32hs/usb_dc_usbhs.c:288-299,393-399 | ✅ | ISR 处理顺序 TRANSFER→SETUP→DETECT 违反 USB 事件因果：复位应最先处理（丢弃全部在途状态），SETUP 应先于其数据/状态 TRANSFER 分发（否则新 SETUP 的数据阶段被旧上下文消费）；且 SETUP 接收后应强制 `UEP0_TX/RX_CTRL = NAK\|TOG_1`（规范要求 SETUP 后首包为 DATA1），与 EP0 软件 toggle 变量(true) 自洽 |
| PORT-03 | port/ch32/ch32hs/usb_dc_usbhs.c:68-90 | ✅ | `usb_dc_init` 缺 WCH 官方初始化序列：`CONTROL=ALL_CLR\|FORCE_RST` → 延时 → 释放 → `HOST_CTRL=PHY_SUSPENDM` → 延时 → `CONTROL=0` → 再设 HS/FS 模式。缺复位+延时导致 PHY 未就绪时间歇枚举失败 |
| PORT-04 | port/ch32/ch32hs/usb_dc_usbhs.c:100-103 | ✅ | `usbd_get_port_speed` 硬编码 `USB_SPEED_HIGH`。FS 插入时速度/描述符全错。应读 `SPEED_TYPE & 0x03`：WCH 官方编码 FULL=0x00 / HIGH=0x01（Peripheral/inc/ch32v30x_usb.h:263-266 已核实），`==0x01→HS else FS` 写法正确 |
| PORT-05 | port/ch32/ch32hs/usb_dc_usbhs.c（DETECT 块） | ✅ | 总线复位处理在 `usbd_event_reset_handler()` 之后才恢复 `UEP0_DMA`/`RX_CTRL`，存在窗口。应封装 `ch32_usbhs_ep0_prepare()`（ENDP_CONFIG=EP0、MAX_LEN、DMA、TX_LEN=0、TX NAK、RX ACK、toggle=true），在 reset handler 前后各调一次（幂等，后者恢复被 `usbd_ep_open` 覆盖的 NAK/ACK）；`ENDP_CONFIG` 用全赋值清所有 EP 使能符合复位语义 |
| PORT-06 | port/ch32/ch32hs/usb_dc_usbhs.c（INT_EN） | ✅ | 未使能 `USBHS_SUSPEND_EN`，挂起/恢复事件不上报 → core 的 `is_suspend` 永远 false，remote wakeup 判定失效。修法：SUSPEND_FLAG 块内按 `MIS_ST & USBHS_SUSPEND` 区分挂起（bit2,=UMS_SUSPEND）/恢复后分别调 `usbd_event_suspend/resume_handler`（ch32fs 端口同款模式） |
| PORT-07 | port/ch32/ch32hs/usb_dc_usbhs.c:423 | ✅ | `USBHS_IRQHandler` 用 `interrupt("WCH-Interrupt-fast")`（HPE 硬件压栈）。WCH QingKe V4 约束 HPE 仅适用于硬件压栈区中断；USBHS pri=0 落在 8 级软件压栈区时应改普通 `interrupt()`。**项目相关配置**：移植时建议保留上游默认、以宏开关切换，不要无脑改 |

## 上游已修复完整性核查（首次审查附带结论）

| 上游修复 | 结论 |
|---|---|
| adb len check (af3a981) / A_OPEN off-by-one (#427) | 不彻底：防护依赖 `USB_ASSERT`（见 CLS-08） |
| host net rx 长度检查 (740f349) | 只覆盖外层分支，RNDIS 聚合循环内问题仍在（CLS-02） |
| FreeRTOS ISR 修复 (1fd876d, #440) | 方向正确但引入 `xPortIsInsideInterrupt` 可移植性问题（OSAL-05），且未同步到其他 osal 后端（OSAL-02/06~10） |
| audio samfreq 8 (7743b4c) / serial rx overflow (a6adc3b) | 彻底，无残留 |

## 建议修复顺序

1. **Wave 1（主机侧攻击面，P0）**：CLS-01、CLS-02、CLS-06、CLS-07（网络帧注入）→ CLS-03/04/05（插入即崩类）→ CLS-08（adb 断言防护改运行时校验）。
2. **Wave 2（P1）**：CORE-01（一行修编译）→ CLS-09/10（MSC）→ CLS-11/12/13 → CORE-02/03（设备侧）→ OSAL-01（IDF 一行修）→ OSAL-02~05。
3. **Wave 3（P2 批量）**：osal ISR 守卫补全（OSAL-06~10）、单位换算（OSAL-11/12）、usb_list.h 三处（OSAL-16~18，顺手修）。
4. P3 择机 / 随相关文件修改顺带修。

## 变更记录

- 2026-09-06：首次全库审查（基线 1fd876d），登记 CORE-01~03/24~28（core+common）、OSAL-01~26（osal）、CLS-01~44（类驱动）；三条上游修复完整性核查结论。
- 2026-09-06：并入用户 EmoeDAQ 项目（CH32V30x_USB_AD7175）的本地修正核查结论，新增 PORT-01~07（port/ch32/ch32hs，已逐条对照 WCH 官方头文件/例程核实；该项目的修正在其工程内验证有效，且对应问题在 1fd876d 基线上全部仍存在，待移植）。
- 2026-09-06：PORT-01~07 移植入 fork（port/ch32/ch32hs；含 EP9 寄存器位修正、FORCE_RST 初始化序列、
  SPEED_TYPE 动态测速与 FS 回退、EP0 prepare、ISR 因果序重排 + SETUP 强制 DATA1、挂起/恢复事件、IRQ 压栈方式宏开关）。
- 2026-09-06：核对用户 USBTMC 扩展项目（CH32_USBTMC，usbd_tmc 类驱动 + core SET/CLR_HALT 扩展，基线 v1.5.0）：扩展实现整体合理，其 core notify_handler 修正反哺本表 CORE-35。TMC 驱动自身的问题（临界区宏跨编译单元失效、other_speed 描述符类型 0x02 等）记录在该项目，不属于本 fork 代码。
