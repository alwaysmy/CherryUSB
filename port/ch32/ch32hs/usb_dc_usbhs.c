/*
 * Copyright (c) 2022, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "usbd_core.h"
#include "usb_ch32_usbhs_reg.h"

#ifndef USB_NUM_BIDIR_ENDPOINTS
#define USB_NUM_BIDIR_ENDPOINTS 16
#endif

#define USB_SET_RX_DMA(ep_idx, addr) (*(volatile uint32_t *)((uint32_t)(&USBHS_DEVICE->UEP1_RX_DMA) + 4 * (ep_idx - 1)) = addr)
#define USB_SET_TX_DMA(ep_idx, addr) (*(volatile uint32_t *)((uint32_t)(&USBHS_DEVICE->UEP1_TX_DMA) + 4 * (ep_idx - 1)) = addr)
#define USB_SET_MAX_LEN(ep_idx, len) (*(volatile uint16_t *)((uint32_t)(&USBHS_DEVICE->UEP0_MAX_LEN) + 4 * ep_idx) = len)
#define USB_SET_TX_LEN(ep_idx, len)  (*(volatile uint16_t *)((uint32_t)(&USBHS_DEVICE->UEP0_TX_LEN) + 4 * ep_idx) = len)
#define USB_GET_TX_LEN(ep_idx)       (*(volatile uint16_t *)((uint32_t)(&USBHS_DEVICE->UEP0_TX_LEN) + 4 * ep_idx))
#define USB_SET_TX_CTRL(ep_idx, val) (*(volatile uint8_t *)((uint32_t)(&USBHS_DEVICE->UEP0_TX_CTRL) + 4 * ep_idx) = val)
#define USB_GET_TX_CTRL(ep_idx)      (*(volatile uint8_t *)((uint32_t)(&USBHS_DEVICE->UEP0_TX_CTRL) + 4 * ep_idx))
#define USB_SET_RX_CTRL(ep_idx, val) (*(volatile uint8_t *)((uint32_t)(&USBHS_DEVICE->UEP0_RX_CTRL) + 4 * ep_idx) = val)
#define USB_GET_RX_CTRL(ep_idx)      (*(volatile uint8_t *)((uint32_t)(&USBHS_DEVICE->UEP0_RX_CTRL) + 4 * ep_idx))

/* Endpoint state */
struct ch32_usbhs_ep_state {
    uint16_t ep_mps;    /* Endpoint max packet size */
    uint8_t ep_type;    /* Endpoint type */
    uint8_t ep_stalled; /* Endpoint stall flag */
    uint8_t ep_enable;  /* Endpoint enable */
    uint8_t *xfer_buf;
    uint32_t xfer_len;
    uint32_t actual_xfer_len;
};

/* Driver state */
struct ch32_usbhs_udc {
    __attribute__((aligned(4))) struct usb_setup_packet setup;
    volatile uint8_t dev_addr;
    struct ch32_usbhs_ep_state in_ep[USB_NUM_BIDIR_ENDPOINTS];  /*!< IN endpoint parameters*/
    struct ch32_usbhs_ep_state out_ep[USB_NUM_BIDIR_ENDPOINTS]; /*!< OUT endpoint parameters */
} g_ch32_usbhs_udc;

volatile uint8_t mps_over_flag = 0;
volatile bool ep0_rx_data_toggle;
volatile bool ep0_tx_data_toggle;
volatile bool epx_tx_data_toggle[USB_NUM_BIDIR_ENDPOINTS - 1];

/* EP0 完整准备: 总线复位与 usb_dc_init 共用; 在 usbd_event_reset_handler
 * 前后各调一次(幂等), 后者恢复 usbd_ep_open 覆盖掉的 EP0 响应/翻转状态 */
static void ch32_usbhs_ep0_prepare(void)
{
    USBHS_DEVICE->ENDP_CONFIG = USBHS_EP0_T_EN | USBHS_EP0_R_EN;
    USBHS_DEVICE->UEP0_MAX_LEN = USB_CTRL_EP_MPS;
    USBHS_DEVICE->UEP0_DMA = (uint32_t)&g_ch32_usbhs_udc.setup;
    USBHS_DEVICE->UEP0_TX_LEN = 0;
    USBHS_DEVICE->UEP0_TX_CTRL = USBHS_EP_T_RES_NAK;
    USBHS_DEVICE->UEP0_RX_CTRL = USBHS_EP_R_RES_ACK;

    ep0_tx_data_toggle = true;
    ep0_rx_data_toggle = true;
}

__WEAK void usb_dc_low_level_init(void)
{
}

__WEAK void usb_dc_low_level_deinit(void)
{
}

int usb_dc_init(uint8_t busid)
{
    uint32_t phy_delay;

    usb_dc_low_level_init();

    /* WCH 参考初始化序列: 强位复位 USB 内核并等待 PHY 稳定,
     * 缺少该序列会导致间歇性枚举失败 */
    USBHS_DEVICE->CONTROL = USBHS_ALL_CLR | USBHS_FORCE_RST;
    phy_delay = 1000;
    while (--phy_delay);
    USBHS_DEVICE->CONTROL &= ~USBHS_FORCE_RST;

    USBHS_DEVICE->HOST_CTRL = 0x00;
    USBHS_DEVICE->HOST_CTRL = USBHS_PHY_SUSPENDM;

    phy_delay = 50000;
    while (--phy_delay);

    USBHS_DEVICE->CONTROL = 0;
#ifdef CONFIG_USB_HS
    USBHS_DEVICE->CONTROL = USBHS_DMA_EN | USBHS_INT_BUSY_EN | USBHS_HIGH_SPEED;
#else
    USBHS_DEVICE->CONTROL = USBHS_DMA_EN | USBHS_INT_BUSY_EN | USBHS_FULL_SPEED;
#endif

    USBHS_DEVICE->INT_FG = 0xff;
    USBHS_DEVICE->INT_EN = 0;
    USBHS_DEVICE->INT_EN = USBHS_SETUP_ACT_EN | USBHS_TRANSFER_EN | USBHS_DETECT_EN | USBHS_SUSPEND_EN;

    USBHS_DEVICE->ENDP_TYPE = 0x00;
    USBHS_DEVICE->BUF_MODE = 0x00;
    memset(&g_ch32_usbhs_udc, 0, sizeof(struct ch32_usbhs_udc));
    ch32_usbhs_ep0_prepare();

    USBHS_DEVICE->CONTROL |= USBHS_DEV_PU_EN;

    return 0;
}

int usb_dc_deinit(uint8_t busid)
{
    return 0;
}

int usbd_set_address(uint8_t busid, const uint8_t addr)
{
    if (addr == 0) {
        USBHS_DEVICE->DEV_AD = addr & 0xff;
    }
    g_ch32_usbhs_udc.dev_addr = addr;
    return 0;
}

int usbd_set_remote_wakeup(uint8_t busid)
{
    return -1;
}

/* 实际枚举速度以 SPEED_TYPE 为准 (WCH 编码: 0x00=FS, 0x01=HS)。
 * FS 回退时 usbd core 会以 USB_SPEED_FULL 回调描述符接口, 应用须按速度
 * 返回对应 MPS 的配置描述符 (HS 512 / FS 64 各一份, other speed 描述符
 * 必须用 USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT, bDescriptorType=0x07) */
uint8_t usbd_get_port_speed(uint8_t busid)
{
    uint8_t speed = USBHS_DEVICE->SPEED_TYPE & USBSPEED_MASK;
    if (speed == 0x01) {
        return USB_SPEED_HIGH;
    }
    return USB_SPEED_FULL;
}

int usbd_ep_open(uint8_t busid, const struct usb_endpoint_descriptor *ep)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep->bEndpointAddress);

    if (USB_EP_DIR_IS_OUT(ep->bEndpointAddress)) {
        g_ch32_usbhs_udc.out_ep[ep_idx].ep_mps = USB_GET_MAXPACKETSIZE(ep->wMaxPacketSize);
        g_ch32_usbhs_udc.out_ep[ep_idx].ep_type = USB_GET_ENDPOINT_TYPE(ep->bmAttributes);
        g_ch32_usbhs_udc.out_ep[ep_idx].ep_enable = true;
        if (g_ch32_usbhs_udc.out_ep[ep_idx].ep_type == USB_ENDPOINT_TYPE_ISOCHRONOUS) {
            USBHS_DEVICE->ENDP_TYPE |= (1 << (ep_idx + 16));
        } else {
            USBHS_DEVICE->ENDP_TYPE &= ~(1 << (ep_idx + 16));
        }
        USBHS_DEVICE->ENDP_CONFIG |= (1 << (ep_idx + 16));
        USB_SET_RX_CTRL(ep_idx, USBHS_EP_R_RES_NAK | USBHS_EP_R_TOG_0 | USBHS_EP_R_AUTOTOG);
    } else {
        g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps = USB_GET_MAXPACKETSIZE(ep->wMaxPacketSize);
        g_ch32_usbhs_udc.in_ep[ep_idx].ep_type = USB_GET_ENDPOINT_TYPE(ep->bmAttributes);
        g_ch32_usbhs_udc.in_ep[ep_idx].ep_enable = true;
        if (g_ch32_usbhs_udc.in_ep[ep_idx].ep_type == USB_ENDPOINT_TYPE_ISOCHRONOUS) {
            USBHS_DEVICE->ENDP_TYPE |= (1 << (ep_idx));
            USB_SET_TX_CTRL(ep_idx, USBHS_EP_T_RES_NAK | USBHS_EP_T_TOG_0);
        } else {
            USBHS_DEVICE->ENDP_TYPE &= ~(1 << (ep_idx));
            USB_SET_TX_CTRL(ep_idx, USBHS_EP_T_RES_NAK | USBHS_EP_T_TOG_0 | USBHS_EP_T_AUTOTOG);
        }
        USBHS_DEVICE->ENDP_CONFIG |= (1 << (ep_idx));
    }
    USB_SET_MAX_LEN(ep_idx, USB_GET_MAXPACKETSIZE(ep->wMaxPacketSize));
    return 0;
}

int usbd_ep_close(uint8_t busid, const uint8_t ep)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    if (USB_EP_DIR_IS_OUT(ep)) {
        USBHS_DEVICE->ENDP_CONFIG &= ~(1 << (ep_idx + 16));
    } else {
        USBHS_DEVICE->ENDP_CONFIG &= ~(1 << (ep_idx));
    }
    return 0;
}

int usbd_ep_set_stall(uint8_t busid, const uint8_t ep)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);

    if (USB_EP_DIR_IS_OUT(ep)) {
        if (ep_idx == 0) {
            USBHS_DEVICE->UEP0_RX_CTRL = USBHS_EP_R_RES_STALL;
        } else {
            USB_SET_RX_CTRL(ep_idx, (USB_GET_RX_CTRL(ep_idx) & ~USBHS_EP_R_RES_MASK) | USBHS_EP_R_RES_STALL);
        }
    } else {
        if (ep_idx == 0) {
            USBHS_DEVICE->UEP0_TX_CTRL = USBHS_EP_T_RES_STALL;
        } else {
            USB_SET_TX_CTRL(ep_idx, (USB_GET_TX_CTRL(ep_idx) & ~USBHS_EP_T_RES_MASK) | USBHS_EP_T_RES_STALL);
        }
    }

    return 0;
}

int usbd_ep_clear_stall(uint8_t busid, const uint8_t ep)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);

    if (USB_EP_DIR_IS_OUT(ep)) {
        /* 恢复 open 时位型 (NAK|TOG_0|AUTOTOG): 重写整个寄存器而漏掉 AUTOTOG
         * 会让 bulk-OUT 落入手动 TOG 恒等 DATA0 -> clear 后主机 DATA1 包被
         * 硬件静默丢弃 -> 写挂死 (USBTMC Device Clear 根因)。RES 用 NAK 而非
         * ACK: 类驱动随后 start_read 才置 ACK, 无已挂 DMA 缓冲时 ACK 会把
         * 主机包收进陈旧缓冲 */
        USB_SET_RX_CTRL(ep_idx, USBHS_EP_R_RES_NAK | USBHS_EP_R_TOG_0 | USBHS_EP_R_AUTOTOG);
    } else {
        USB_SET_TX_CTRL(ep_idx, USBHS_EP_T_RES_NAK | USBHS_EP_T_TOG_0 | USBHS_EP_T_AUTOTOG);
        if (ep_idx > 0) {
            /* USB 2.0 9.1.1.6: clear stall 后数据 toggle 复位为 DATA0。
             * 本端口 start_write 用软件 toggle 数组设起始 PID, 必须同步,
             * 否则 STALL 恢复后 IN 传输起始 PID 错 -> 主机丢包/超时 */
            epx_tx_data_toggle[ep_idx - 1] = false;
        }
    }
    return 0;
}

int usbd_ep_is_stalled(uint8_t busid, const uint8_t ep, uint8_t *stalled)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);

    if (USB_EP_DIR_IS_OUT(ep)) {
        *stalled = USB_GET_RX_CTRL(ep_idx) & USBHS_EP_R_RES_STALL ? 1 : 0;
    } else {
        *stalled = USB_GET_TX_CTRL(ep_idx) & USBHS_EP_T_RES_STALL ? 1 : 0;
    }
    return 0;
}

int usbd_ep_start_write(uint8_t busid, const uint8_t ep, const uint8_t *data, uint32_t data_len)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    uint32_t tmp;

    if (!data && data_len) {
        return -1;
    }
    if (!g_ch32_usbhs_udc.in_ep[ep_idx].ep_enable) {
        return -2;
    }
    if ((uint32_t)data & 0x03) {
        return -3;
    }

    g_ch32_usbhs_udc.in_ep[ep_idx].xfer_buf = (uint8_t *)data;
    g_ch32_usbhs_udc.in_ep[ep_idx].xfer_len = data_len;
    g_ch32_usbhs_udc.in_ep[ep_idx].actual_xfer_len = 0;

    if (ep_idx == 0) {
        if (data_len == 0) {
            USB_SET_TX_LEN(ep_idx, 0);
        } else {
            data_len = MIN(data_len, g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps);
            USB_SET_TX_LEN(ep_idx, data_len);
            USBHS_DEVICE->UEP0_DMA = (uint32_t)data;
        }
        tmp = ep0_tx_data_toggle ? USBHS_EP_T_TOG_1 : USBHS_EP_T_TOG_0;
        USBHS_DEVICE->UEP0_TX_CTRL = USBHS_EP_T_RES_ACK | tmp;
    } else {
        if (data_len == 0) {
            USB_SET_TX_LEN(ep_idx, 0);
        } else {
            data_len = MIN(data_len, g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps);
            USB_SET_TX_LEN(ep_idx, data_len);
            USB_SET_TX_DMA(ep_idx, (uint32_t)data);
        }
        tmp = USB_GET_TX_CTRL(ep_idx);
        tmp &= ~(USBHS_EP_T_RES_MASK | USBHS_EP_T_TOG_MASK);
        tmp |= USBHS_EP_T_RES_ACK;
        if (g_ch32_usbhs_udc.in_ep[ep_idx].ep_type == USB_ENDPOINT_TYPE_ISOCHRONOUS) {
            tmp |= USBHS_EP_T_TOG_0;
        } else {
            tmp |= (epx_tx_data_toggle[ep_idx - 1] ? USBHS_EP_T_TOG_1 : USBHS_EP_T_TOG_0);
        }
        USB_SET_TX_CTRL(ep_idx, tmp);
    }
    return 0;
}

int usbd_ep_start_read(uint8_t busid, const uint8_t ep, uint8_t *data, uint32_t data_len)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);

    if (!data && data_len) {
        return -1;
    }
    if (!g_ch32_usbhs_udc.out_ep[ep_idx].ep_enable) {
        return -2;
    }
    if ((uint32_t)data & 0x03) {
        return -3;
    }

    g_ch32_usbhs_udc.out_ep[ep_idx].xfer_buf = (uint8_t *)data;
    g_ch32_usbhs_udc.out_ep[ep_idx].xfer_len = data_len;
    g_ch32_usbhs_udc.out_ep[ep_idx].actual_xfer_len = 0;

    if (ep_idx == 0) {
        if (data_len == 0) {
            USBHS_DEVICE->UEP0_RX_CTRL = USBHS_EP_R_RES_ACK | USBHS_EP_R_TOG_1;
        } else {
            USBHS_DEVICE->UEP0_DMA = (uint32_t)data;
            USBHS_DEVICE->UEP0_RX_CTRL = USBHS_EP_R_RES_ACK | (ep0_rx_data_toggle ? USBHS_EP_R_TOG_1 : USBHS_EP_R_TOG_0);
        }
        return 0;
    } else {
        USB_SET_RX_DMA(ep_idx, (uint32_t)data);
        if (g_ch32_usbhs_udc.out_ep[ep_idx].ep_type == USB_ENDPOINT_TYPE_ISOCHRONOUS) {
            USB_SET_RX_CTRL(ep_idx, (USB_GET_RX_CTRL(ep_idx) & ~(USBHS_EP_R_RES_MASK | USBHS_EP_R_TOG_MASK)) | USBHS_EP_R_RES_ACK | USBHS_EP_R_TOG_0);
        } else {
            USB_SET_RX_CTRL(ep_idx, (USB_GET_RX_CTRL(ep_idx) & ~USBHS_EP_R_RES_MASK) | USBHS_EP_R_RES_ACK);
        }
    }

    return 0;
}

void USBD_IRQHandler(uint8_t busid)
{
    uint32_t ep_idx, token, write_count, read_count;
    uint8_t intflag = 0;

    intflag = USBHS_DEVICE->INT_FG;

    /* 事件按因果序处理: 总线复位最优先(丢弃全部在途状态),
     * SETUP 先于其数据/状态阶段的 TRANSFER 事件分发 */
    if (intflag & USBHS_DETECT_FLAG) {
        USBHS_DEVICE->INT_FG = USBHS_DETECT_FLAG;

        memset(&g_ch32_usbhs_udc, 0, sizeof(struct ch32_usbhs_udc));
        ch32_usbhs_ep0_prepare();

        for (uint8_t ep_idx = 1; ep_idx < USB_NUM_BIDIR_ENDPOINTS; ep_idx++) {
            USB_SET_TX_LEN(ep_idx, 0);
            USB_SET_TX_CTRL(ep_idx, USBHS_EP_T_AUTOTOG | USBHS_EP_T_RES_NAK); // autotog does not work
            USB_SET_RX_CTRL(ep_idx, USBHS_EP_R_AUTOTOG | USBHS_EP_R_RES_NAK);
            epx_tx_data_toggle[ep_idx - 1] = false;
        }

        usbd_event_reset_handler(0);
        ch32_usbhs_ep0_prepare();
    }

    if (intflag & USBHS_SETUP_FLAG) {
        USBHS_DEVICE->INT_FG = USBHS_SETUP_FLAG;
        /* 规范要求 SETUP 包之后的首个数据包为 DATA1 */
        USBHS_DEVICE->UEP0_TX_CTRL = USBHS_EP_T_RES_NAK | USBHS_EP_T_TOG_1;
        USBHS_DEVICE->UEP0_RX_CTRL = USBHS_EP_R_RES_NAK | USBHS_EP_R_TOG_1;
        usbd_event_ep0_setup_complete_handler(0, (uint8_t *)&g_ch32_usbhs_udc.setup);
    }

    if (intflag & USBHS_TRANSFER_FLAG) {
        ep_idx = (USBHS_DEVICE->INT_ST) & MASK_UIS_ENDP;
        token = (((USBHS_DEVICE->INT_ST) & MASK_UIS_TOKEN) >> 4) & 0x03;

        if (token == PID_IN) {
            USB_SET_TX_CTRL(ep_idx, (USB_GET_TX_CTRL(ep_idx) & ~(USBHS_EP_T_RES_MASK | USBHS_EP_T_TOG_MASK)) | USBHS_EP_T_RES_NAK | USBHS_EP_T_TOG_0);
            if (ep_idx == 0x00) {
                if (g_ch32_usbhs_udc.in_ep[ep_idx].xfer_len >= g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps) {
                    g_ch32_usbhs_udc.in_ep[ep_idx].xfer_len -= g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps;
                    g_ch32_usbhs_udc.in_ep[ep_idx].actual_xfer_len += g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps;
                    ep0_tx_data_toggle ^= 1;
                } else {
                    g_ch32_usbhs_udc.in_ep[ep_idx].actual_xfer_len += g_ch32_usbhs_udc.in_ep[ep_idx].xfer_len;
                    g_ch32_usbhs_udc.in_ep[ep_idx].xfer_len = 0;
                    ep0_tx_data_toggle = true;
                }

                usbd_event_ep_in_complete_handler(0, ep_idx | 0x80, g_ch32_usbhs_udc.in_ep[ep_idx].actual_xfer_len);

                if (g_ch32_usbhs_udc.dev_addr > 0) {
                    USBHS_DEVICE->DEV_AD = g_ch32_usbhs_udc.dev_addr & 0xff;
                    g_ch32_usbhs_udc.dev_addr = 0;
                }

                if (g_ch32_usbhs_udc.setup.wLength && ((g_ch32_usbhs_udc.setup.bmRequestType & USB_REQUEST_DIR_MASK) == USB_REQUEST_DIR_OUT)) {
                    /* In status, start reading setup */
                    USBHS_DEVICE->UEP0_DMA = (uint32_t)&g_ch32_usbhs_udc.setup;
                    USBHS_DEVICE->UEP0_RX_CTRL = USBHS_EP_R_RES_ACK;
                    ep0_tx_data_toggle = true;

                } else if (g_ch32_usbhs_udc.setup.wLength == 0) {
                    /* In status, start reading setup */
                    USBHS_DEVICE->UEP0_DMA = (uint32_t)&g_ch32_usbhs_udc.setup;
                    USBHS_DEVICE->UEP0_RX_CTRL = USBHS_EP_R_RES_ACK;
                    ep0_tx_data_toggle = true;
                }
            } else {
                if (g_ch32_usbhs_udc.in_ep[ep_idx].xfer_len > g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps) {
                    g_ch32_usbhs_udc.in_ep[ep_idx].xfer_buf += g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps;
                    g_ch32_usbhs_udc.in_ep[ep_idx].xfer_len -= g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps;
                    g_ch32_usbhs_udc.in_ep[ep_idx].actual_xfer_len += g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps;
                    epx_tx_data_toggle[ep_idx - 1] ^= 1;

                    write_count = MIN(g_ch32_usbhs_udc.in_ep[ep_idx].xfer_len, g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps);
                    USB_SET_TX_LEN(ep_idx, write_count);
                    USB_SET_TX_DMA(ep_idx, (uint32_t)g_ch32_usbhs_udc.in_ep[ep_idx].xfer_buf);

                    uint32_t tmp = USB_GET_TX_CTRL(ep_idx);
                    tmp &= ~(USBHS_EP_T_RES_MASK | USBHS_EP_T_TOG_MASK);
                    tmp |= USBHS_EP_T_RES_ACK;

                    if (g_ch32_usbhs_udc.in_ep[ep_idx].ep_type == USB_ENDPOINT_TYPE_ISOCHRONOUS) {
                        tmp |= USBHS_EP_T_TOG_0;
                    } else {
                        tmp |= (epx_tx_data_toggle[ep_idx - 1] ? USBHS_EP_T_TOG_1 : USBHS_EP_T_TOG_0);
                    }

                    USB_SET_TX_CTRL(ep_idx, tmp);
                } else {
                    g_ch32_usbhs_udc.in_ep[ep_idx].actual_xfer_len += g_ch32_usbhs_udc.in_ep[ep_idx].xfer_len;
                    g_ch32_usbhs_udc.in_ep[ep_idx].xfer_len = 0;
                    epx_tx_data_toggle[ep_idx - 1] ^= 1;
                    usbd_event_ep_in_complete_handler(0, ep_idx | 0x80, g_ch32_usbhs_udc.in_ep[ep_idx].actual_xfer_len);
                }
            }
        } else if (token == PID_OUT) {
            USB_SET_RX_CTRL(ep_idx, (USB_GET_RX_CTRL(ep_idx) & ~USBHS_EP_R_RES_MASK) | USBHS_EP_R_RES_NAK);
            if (ep_idx == 0x00) {
                read_count = USBHS_DEVICE->RX_LEN;

                g_ch32_usbhs_udc.out_ep[ep_idx].actual_xfer_len += read_count;
                g_ch32_usbhs_udc.out_ep[ep_idx].xfer_len -= read_count;

                usbd_event_ep_out_complete_handler(0, 0x00, g_ch32_usbhs_udc.out_ep[ep_idx].actual_xfer_len);

                if (read_count == 0) {
                    /* Out status, start reading setup */
                    USBHS_DEVICE->UEP0_DMA = (uint32_t)&g_ch32_usbhs_udc.setup;
                    USBHS_DEVICE->UEP0_RX_CTRL = USBHS_EP_R_RES_ACK;
                    ep0_rx_data_toggle = true;
                    ep0_tx_data_toggle = true;
                } else {
                    ep0_rx_data_toggle ^= 1;
                }
            } else {
                if (USBHS_DEVICE->INT_ST & USBHS_DEV_UIS_TOG_OK) {
                    read_count = USBHS_DEVICE->RX_LEN;

                    g_ch32_usbhs_udc.out_ep[ep_idx].xfer_buf += read_count;
                    g_ch32_usbhs_udc.out_ep[ep_idx].actual_xfer_len += read_count;
                    g_ch32_usbhs_udc.out_ep[ep_idx].xfer_len -= read_count;

                    if ((read_count < g_ch32_usbhs_udc.out_ep[ep_idx].ep_mps) || (g_ch32_usbhs_udc.out_ep[ep_idx].xfer_len == 0)) {
                        usbd_event_ep_out_complete_handler(0, ep_idx, g_ch32_usbhs_udc.out_ep[ep_idx].actual_xfer_len);
                    } else {
                        USB_SET_RX_DMA(ep_idx, (uint32_t)g_ch32_usbhs_udc.out_ep[ep_idx].xfer_buf);
                        USB_SET_RX_CTRL(ep_idx, (USB_GET_RX_CTRL(ep_idx) & ~USBHS_EP_R_RES_MASK) | USBHS_EP_R_RES_ACK);
                    }
                }
            }
        }

        /* 传输标志在处理完后清除: 处理期间新完成的事件保持置位 */
        USBHS_DEVICE->INT_FG = USBHS_TRANSFER_FLAG;
    }

    if (intflag & USBHS_SUSPEND_FLAG) {
        USBHS_DEVICE->INT_FG = USBHS_SUSPEND_FLAG;
        if (USBHS_DEVICE->MIS_ST & USBHS_SUSPEND) {
            usbd_event_suspend_handler(0);
        } else {
            usbd_event_resume_handler(0);
        }
    }
}

/* interrupt("WCH-Interrupt-fast") 使用 HPE 硬件压栈, 仅适用于中断落在
 * 硬件压栈区的配置; 若 USBHS 中断优先级位于 8 级软件压栈区(如 pri=0),
 * 须在 usb_config.h 定义 USB_CH32_USBHS_IRQ_SW_STACK 改用软件压栈
 * (软压栈下寄存器保存与中断内调用链全走 C 栈, 栈深需 >=4KB, 实测 2KB 不足) */
#ifdef USB_CH32_USBHS_IRQ_SW_STACK
#define CH32_USBHS_IRQ_ATTR __attribute__((interrupt()))
#else
#define CH32_USBHS_IRQ_ATTR __attribute__((interrupt("WCH-Interrupt-fast")))
#endif
/* 属性必须落在声明(或定义)上: 缺失时 GCC 按普通函数编译 ISR, epilogue 以 ret
 * 而非 mret 结尾, 首次中断后 mstatus.MIE 停在 0, 全局中断永久失效 */
void USBHS_IRQHandler(void) CH32_USBHS_IRQ_ATTR;
void USBHS_IRQHandler(void)
{
    extern void USBD_IRQHandler(uint8_t busid);
    USBD_IRQHandler(0);
}
