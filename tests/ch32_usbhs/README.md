# CH32V30x USBHS device regression tests

Run on Linux/x86-64 with GCC (or compatible `CC`):

```sh
./tests/ch32_usbhs/run.sh
```

The test compiles the actual CH32 USBHS driver and public headers with RAM-backed
registers. It checks halt response decoding, duplicate OUT handling (bulk and
EP0), control-transfer PID sequencing and SETUP preemption, reset cancellation,
endpoint close/reopen, clear-halt recovery, multi-packet HS IN and ZLP completion.
AddressSanitizer and UndefinedBehaviorSanitizer are enabled. Leak detection is
disabled because this test has no heap allocations and some sandbox runtimes do
not support LeakSanitizer's process inspection.

The non-PIE executable keeps mock register addresses representable by the
production driver's 32-bit MMIO macros. Target function attributes are suppressed
only in this native test. RAM registers do **not** emulate USB signaling, DMA,
write-one-to-clear behavior, interrupt timing or the PHY. These are state-machine
regressions, not a substitute for enumeration/data/clear-halt/reconnect board tests.

## RISC-V compile check

With a RISC-V bare-metal GCC and C library headers available:

```sh
riscv64-unknown-elf-gcc -std=c99 -march=rv32imafc -mabi=ilp32f -O2 \
  -Itests/ch32_usbhs -Icore -Icommon \
  -c port/ch32/ch32hs/usb_dc_usbhs.c -o /tmp/ch32-usbhs.o
riscv64-unknown-elf-objdump -d /tmp/ch32-usbhs.o
```

Repeat with `-Os` and with `-DCONFIG_USB_HS` (without it the FS configuration is
compiled). Supply the toolchain's libc include path when it does not locate
`string.h`/`stdlib.h` automatically. The test config selects the ordinary
software-stacked RISC-V interrupt attribute. Inspect `USBHS_IRQHandler` for
`mret`, and `usb_dc_init` for both retained countdown loops. The vendor-specific
`WCH-Interrupt-fast` path requires WCH's compiler and separate hardware testing;
stock GCC is not evidence that that ABI is correct. Loop iteration counts are
preserved, but actual settling time still depends on CPU/clock/board conditions.
