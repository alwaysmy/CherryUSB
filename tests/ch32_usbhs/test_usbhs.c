/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include "usbd_core.h"
#include "usb_ch32_usbhs_reg.h"

/* Run the production state machine against RAM registers. Non-PIE keeps their
 * addresses in the CH32 driver's 32-bit address range. Only target-specific
 * function attributes are suppressed; the RISC-V build checks the real ISR. */
static USBHSD_TypeDef registers;
#undef USBHS_DEVICE
#define USBHS_DEVICE (&registers)
#define __attribute__(attributes)
#include "../../port/ch32/ch32hs/usb_dc_usbhs.c"
#undef __attribute__

static unsigned failures, assertions;
static unsigned reset_calls, setup_calls, in_calls, out_calls;
static bool rearm_out, rearm_setup;
static uint8_t callback_rx_ctrl;
static uint8_t buffer[1024] __attribute__((aligned(4)));

#define CHECK(expr) do { assertions++; if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); failures++; \
} } while (0)

static void open_ep(uint8_t ep, uint16_t mps)
{
    struct usb_endpoint_descriptor desc = { 0 };
    desc.bEndpointAddress = ep;
    desc.wMaxPacketSize = mps;
    desc.bmAttributes = ep & 0x7f ? USB_ENDPOINT_TYPE_BULK : USB_ENDPOINT_TYPE_CONTROL;
    CHECK(usbd_ep_open(0, &desc) == 0);
}

void usbd_event_reset_handler(uint8_t busid)
{
    (void)busid;
    reset_calls++;
    usbd_set_address(0, 0);
    open_ep(0, 64);
    open_ep(0x80, 64);
}
void usbd_event_ep0_setup_complete_handler(uint8_t busid, uint8_t *setup)
{
    (void)busid;
    (void)setup;
    setup_calls++;
    if (rearm_setup) {
        CHECK(usbd_ep_start_read(0, 0, buffer, 128) == 0);
        CHECK(usbd_ep_start_write(0, 0x80, buffer, 128) == 0);
    }
}
void usbd_event_ep_in_complete_handler(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    (void)nbytes;
    in_calls++;
}
void usbd_event_ep_out_complete_handler(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)nbytes;
    out_calls++;
    if (rearm_out && ep == 0) {
        CHECK(usbd_ep_start_read(0, 0, buffer + 64, 64) == 0);
        callback_rx_ctrl = registers.UEP0_RX_CTRL;
    }
}
void usbd_event_suspend_handler(uint8_t busid) { (void)busid; }
void usbd_event_resume_handler(uint8_t busid) { (void)busid; }

static void reset_fixture(void)
{
    memset(&registers, 0, sizeof(registers));
    memset(&g_ch32_usbhs_udc, 0, sizeof(g_ch32_usbhs_udc));
    for (unsigned i = 0; i < USB_NUM_BIDIR_ENDPOINTS - 1; i++) {
        epx_tx_data_toggle[i] = false;
    }
    ep0_rx_data_toggle = ep0_tx_data_toggle = true;
    reset_calls = setup_calls = in_calls = out_calls = 0;
    rearm_out = rearm_setup = false;
    callback_rx_ctrl = 0;
}

static void transfer(uint8_t ep, uint8_t token, bool toggle_ok, uint16_t count)
{
    registers.INT_FG = USBHS_TRANSFER_FLAG;
    registers.INT_ST = ep | (token << 4) | (toggle_ok ? USBHS_DEV_UIS_TOG_OK : 0);
    registers.RX_LEN = count;
    USBD_IRQHandler(0);
}

static void test_halt_status(void)
{
    reset_fixture();
    uint8_t stalled;
    for (uint8_t response = 0; response < 4; response++) {
        registers.UEP1_RX_CTRL = USBHS_EP_R_AUTOTOG | USBHS_EP_R_TOG_1 | response;
        registers.UEP1_TX_CTRL = USBHS_EP_T_AUTOTOG | USBHS_EP_T_TOG_1 | response;
        CHECK(usbd_ep_is_stalled(0, 1, &stalled) == 0);
        CHECK(stalled == (response == USBHS_EP_R_RES_STALL));
        CHECK(usbd_ep_is_stalled(0, 0x81, &stalled) == 0);
        CHECK(stalled == (response == USBHS_EP_T_RES_STALL));
    }
}

static void test_duplicate_out(void)
{
    reset_fixture();
    open_ep(1, 64);
    CHECK(usbd_ep_start_read(0, 1, buffer, 128) == 0);
    uint32_t dma = registers.UEP1_RX_DMA;
    transfer(1, PID_OUT, false, 64);
    CHECK(out_calls == 0);
    CHECK(g_ch32_usbhs_udc.out_ep[1].xfer_len == 128);
    CHECK(registers.UEP1_RX_DMA == dma);
    CHECK((registers.UEP1_RX_CTRL & USBHS_EP_R_RES_MASK) == USBHS_EP_R_RES_ACK);
    transfer(1, PID_OUT, true, 64);
    CHECK(out_calls == 0);
    CHECK(g_ch32_usbhs_udc.out_ep[1].xfer_len == 64);
    transfer(1, PID_OUT, true, 64);
    CHECK(out_calls == 1);
    CHECK(g_ch32_usbhs_udc.out_ep[1].actual_xfer_len == 128);
    for (uint8_t response = USBHS_EP_R_RES_NAK; response <= USBHS_EP_R_RES_STALL; response++) {
        registers.UEP1_RX_CTRL = USBHS_EP_R_AUTOTOG | USBHS_EP_R_TOG_1 | response;
        uint8_t ctrl = registers.UEP1_RX_CTRL;
        transfer(1, PID_OUT, false, 64);
        CHECK(registers.UEP1_RX_CTRL == ctrl);
        CHECK(out_calls == 1);
        CHECK(g_ch32_usbhs_udc.out_ep[1].actual_xfer_len == 128);
    }
}

static void test_ep0_toggle_before_rearm(void)
{
    reset_fixture();
    open_ep(0, 64);
    CHECK(usbd_ep_start_read(0, 0, buffer, 128) == 0);
    rearm_out = true;
    transfer(0, PID_OUT, true, 64);
    CHECK(out_calls == 1);
    CHECK((callback_rx_ctrl & USBHS_EP_R_TOG_MASK) == USBHS_EP_R_TOG_0);
    CHECK((registers.UEP0_RX_CTRL & USBHS_EP_R_TOG_MASK) == USBHS_EP_R_TOG_0);
}

static void test_setup_restarts_data1(void)
{
    reset_fixture();
    open_ep(0, 64);
    open_ep(0x80, 64);
    ep0_rx_data_toggle = ep0_tx_data_toggle = false;
    rearm_setup = true;
    registers.INT_FG = USBHS_SETUP_FLAG;
    USBD_IRQHandler(0);
    CHECK(setup_calls == 1);
    CHECK((registers.UEP0_RX_CTRL & USBHS_EP_R_TOG_MASK) == USBHS_EP_R_TOG_1);
    CHECK((registers.UEP0_TX_CTRL & USBHS_EP_T_TOG_MASK) == USBHS_EP_T_TOG_1);
}

static void test_reset_discards_stale_events(void)
{
    reset_fixture();
    open_ep(0x81, 64);
    CHECK(usbd_ep_start_write(0, 0x81, buffer, 64) == 0);
    registers.INT_ST = 1 | (PID_IN << 4);
    registers.INT_FG = USBHS_DETECT_FLAG | USBHS_SETUP_FLAG | USBHS_TRANSFER_FLAG;
    USBD_IRQHandler(0);
    CHECK(reset_calls == 1);
    CHECK(setup_calls == 0);
    CHECK(in_calls == 0);
    CHECK(registers.ENDP_CONFIG == (USBHS_EP0_T_EN | USBHS_EP0_R_EN));
}

static void test_reopen_and_close(void)
{
    reset_fixture();
    open_ep(0x81, 64);
    CHECK(usbd_ep_start_write(0, 0x81, buffer, 4) == 0);
    transfer(1, PID_IN, true, 0);
    CHECK(epx_tx_data_toggle[0]);
    CHECK(usbd_ep_close(0, 0x81) == 0);
    CHECK(usbd_ep_start_write(0, 0x81, buffer, 4) == -2);
    open_ep(0x81, 64);
    CHECK(usbd_ep_start_write(0, 0x81, buffer, 4) == 0);
    CHECK((registers.UEP1_TX_CTRL & USBHS_EP_T_TOG_MASK) == USBHS_EP_T_TOG_0);
    open_ep(1, 64);
    CHECK(usbd_ep_close(0, 1) == 0);
    CHECK(usbd_ep_start_read(0, 1, buffer, 64) == -2);
}

static void test_clear_halt(void)
{
    reset_fixture();
    open_ep(1, 64);
    open_ep(0x81, 64);
    epx_tx_data_toggle[0] = true;
    CHECK(usbd_ep_set_stall(0, 1) == 0);
    CHECK(usbd_ep_set_stall(0, 0x81) == 0);
    CHECK(usbd_ep_clear_stall(0, 1) == 0);
    CHECK(usbd_ep_clear_stall(0, 0x81) == 0);
    CHECK(registers.UEP1_RX_CTRL == (USBHS_EP_R_RES_NAK | USBHS_EP_R_AUTOTOG));
    CHECK(registers.UEP1_TX_CTRL == (USBHS_EP_T_RES_NAK | USBHS_EP_T_AUTOTOG));
    CHECK(!epx_tx_data_toggle[0]);
}

static void test_ep0_duplicate_and_status(void)
{
    reset_fixture();
    open_ep(0, 64);
    CHECK(usbd_ep_start_read(0, 0, buffer, 128) == 0);
    uint8_t ctrl = registers.UEP0_RX_CTRL;
    transfer(0, PID_OUT, false, 64);
    CHECK(out_calls == 0);
    CHECK(g_ch32_usbhs_udc.out_ep[0].xfer_len == 128);
    CHECK(registers.UEP0_RX_CTRL == ctrl);
    CHECK(ep0_rx_data_toggle);
    CHECK(usbd_ep_start_read(0, 0, NULL, 0) == 0);
    transfer(0, PID_OUT, true, 0);
    CHECK(out_calls == 1);
    CHECK(ep0_rx_data_toggle && ep0_tx_data_toggle);
    CHECK(registers.UEP0_DMA == (uint32_t)(uintptr_t)&g_ch32_usbhs_udc.setup);
}

static void test_bulk_in_packets(void)
{
    reset_fixture();
    open_ep(0x81, 512);
    CHECK(usbd_ep_start_write(0, 0x81, buffer, sizeof(buffer)) == 0);
    CHECK(registers.UEP1_TX_LEN == 512);
    CHECK((registers.UEP1_TX_CTRL & USBHS_EP_T_TOG_MASK) == USBHS_EP_T_TOG_0);
    transfer(1, PID_IN, true, 0);
    CHECK(in_calls == 0);
    CHECK(registers.UEP1_TX_DMA == (uint32_t)(uintptr_t)(buffer + 512));
    CHECK((registers.UEP1_TX_CTRL & USBHS_EP_T_TOG_MASK) == USBHS_EP_T_TOG_1);
    transfer(1, PID_IN, true, 0);
    CHECK(in_calls == 1);
    CHECK(g_ch32_usbhs_udc.in_ep[1].actual_xfer_len == sizeof(buffer));
    CHECK(usbd_ep_start_write(0, 0x81, NULL, 0) == 0);
    CHECK(registers.UEP1_TX_LEN == 0);
    transfer(1, PID_IN, true, 0);
    CHECK(in_calls == 2);
}

int main(void)
{
    if ((uintptr_t)&registers > UINT32_MAX) {
        fputs("Build this test with -fno-pie -no-pie\n", stderr);
        return 2;
    }
    test_halt_status();
    test_duplicate_out();
    test_ep0_toggle_before_rearm();
    test_setup_restarts_data1();
    test_reset_discards_stale_events();
    test_reopen_and_close();
    test_clear_halt();
    test_ep0_duplicate_and_status();
    test_bulk_in_packets();
    printf("CH32 USBHS: %u assertions, %u failures\n", assertions, failures);
    return failures ? 1 : 0;
}
