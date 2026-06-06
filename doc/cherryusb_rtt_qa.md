# CherryUSB and RTT Porting QA

## Q1: Porting CherryUSB caused the LCD to stop displaying. What was the key cause?

The LCD itself was not the main problem. The issue appeared after USB was enabled, especially when the USB task started after a delay.

Key causes found during debugging:

- CherryUSB DWC2 FIFO parameters were too large for `USB_OTG_FS`.
- CherryUSB logging used `printf`, which can trigger Keil semihosting if no retarget layer is present.
- The default FreeRTOS task stack was too small for `sprintf` plus LCD drawing.

Fixes:

- Changed the DWC2 FS FIFO config to a conservative 320-word layout in `BSP/CherryUSB_port/usb_dwc2_port.c`.
- Disabled semihosting-prone `printf` first, then routed CherryUSB logs to SEGGER RTT.
- Increased `defaultTask` stack size in `Core/Src/freertos.c`.
- Enabled FreeRTOS stack overflow checking.

## Q2: Why did the screen light only after pressing Run several times in debug mode?

This was consistent with a runtime fault or assert during initialization, not an LCD hardware failure.

The most suspicious points were:

- USB initialization started after `osDelay(3000)` in the USB task.
- CherryUSB DWC2 initialization could assert if FIFO configuration exceeded the FS core capacity.
- `printf` inside CherryUSB logs could enter semihosting/BKPT behavior under Keil.

The screen becoming normal after removing `printf` from CherryUSB logging strongly indicated the semihosting path was one of the major problems.

## Q3: Is the USB hardware configuration correct for STM32H743?

The checked hardware configuration looked correct:

- `USB_OTG_FS` device mode is used.
- PA11 is USB DM.
- PA12 is USB DP.
- USB clock source is PLL3.
- HSE is 12 MHz.
- PLL3 configuration gives 48 MHz for USB.
- VBUS sensing is disabled.
- USB pins do not conflict with the ST7735 LCD pins.

The problem was mainly in software porting/configuration, not in the PA11/PA12 hardware setup.

## Q4: Why was the DWC2 FIFO config changed?

The original local DWC2 params used a 952-word FIFO layout. That is too large for the H743 `USB_OTG_FS` core in this project.

The safer FS configuration is:

```c
.device_rx_fifo_size = (320 - 16 - 64 - 16 - 16),
.device_tx_fifo_size = {
    [0] = 16,
    [1] = 64,
    [2] = 16,
    [3] = 16,
},
.total_fifo_size = 320,
```

This avoids CherryUSB DWC2 assertions such as FIFO overflow or requested FIFO larger than the power-on value.

## Q5: Why did `usb_config.h` cause problems?

The template config used:

```c
#define CONFIG_USB_PRINTF(...) printf(__VA_ARGS__)
```

In Keil, if `printf` is not retargeted, it may use semihosting. That can cause debug BKPT, HardFault-like behavior, or delayed/hanging output.

Fix:

```c
#include "SEGGER_RTT.h"
#define CONFIG_USB_PRINTF(...) SEGGER_RTT_printf(0, __VA_ARGS__)
```

Also disabled unused template options such as MTP thread, RNDIS/ECM LWIP flags, and Bluetooth HCI host flag.

## Q6: How was SEGGER RTT added?

The Keil project was updated to include:

- `BSP/SEGGER_RTT_V798a/RTT/SEGGER_RTT.c`
- `BSP/SEGGER_RTT_V798a/RTT/SEGGER_RTT_printf.c`

Include paths added:

- `BSP/SEGGER_RTT_V798a/RTT`
- `BSP/SEGGER_RTT_V798a/Config`

`SEGGER_RTT_Syscalls_KEIL.c` was not kept in the project because it caused duplicate `_sys_*` symbols with Keil's `sys_io.o`.

## Q7: Why did adding `SEGGER_RTT_Syscalls_KEIL.c` cause linker errors?

Keil's C library object `sys_io.o` already defines symbols such as:

- `_sys_open`
- `_sys_write`
- `_sys_close`
- `_sys_read`

Adding `SEGGER_RTT_Syscalls_KEIL.c` defined the same symbols again, causing `L6200E: Symbol multiply defined`.

Fix:

- Remove `SEGGER_RTT_Syscalls_KEIL.c` from the Keil project.
- Use `SEGGER_RTT_printf` directly instead of retargeting standard `printf`.

## Q8: Is `SEGGER_RTT_ConfigUpBuffer()` required?

No.

SEGGER RTT creates default channel 0 automatically. This works:

```c
SEGGER_RTT_Init();
SEGGER_RTT_WriteString(0, "RTT start\r\n");
SEGGER_RTT_printf(0, "tick=%lu\r\n", HAL_GetTick());
```

`SEGGER_RTT_ConfigUpBuffer()` is only needed if changing buffer name, size, memory, or full-buffer behavior.

## Q9: Why does RTT output appear late after downloading, but immediately after pressing reset?

After flashing, Keil/J-Link may start the target before RTT Viewer has found the RTT control block in RAM. The MCU already wrote the RTT data, but the PC side has not started reading yet.

After pressing reset:

- RTT Viewer is already connected.
- The RTT control block address may already be known.
- The target reruns and output appears immediately.

Ways to reduce delay:

- Open RTT Viewer before reset/run.
- Manually enter the `_SEGGER_RTT` control block address instead of auto detection.
- Find the address from the Keil map file by searching `_SEGGER_RTT`, or inspect `&_SEGGER_RTT` in the debugger.

## Q10: Can RTT be used anywhere?

Mostly yes, with limits:

- Safe in `main`.
- Safe in FreeRTOS tasks.
- Usable in interrupts, but avoid heavy `SEGGER_RTT_printf`.
- In fault handlers, prefer `SEGGER_RTT_WriteString`.
- Do not use it before C runtime/RAM initialization.

The current default RTT mode is `SEGGER_RTT_MODE_NO_BLOCK_SKIP`, so if the buffer is full, logs are dropped instead of blocking the firmware.

## Q11: Can RTT be used concurrently in RTOS tasks?

Yes. SEGGER RTT has internal locking.

However:

- Output from multiple tasks can interleave.
- `SEGGER_RTT_printf` is heavier than `SEGGER_RTT_WriteString`.
- For clean logs, print one full line at a time.
- If strict non-interleaving is needed, wrap RTT output with a FreeRTOS mutex.

## Q12: What debug switches were added?

`CHERRYUSB_AUTO_START` was added in `Core/Src/freertos.c`.

Set it to `0` to compile CherryUSB but skip USB initialization:

```c
#define CHERRYUSB_AUTO_START 0
```

This is useful to isolate whether a crash/reset is caused by USB initialization or by unrelated LCD/RTOS code.
