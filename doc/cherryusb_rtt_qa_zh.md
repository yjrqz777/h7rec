---
title: STM32H743 移植 CherryUSB 与 SEGGER RTT 的问题排查记录
date: 2026-06-06 00:00:00
categories:
  - STM32
  - USB
tags:
  - STM32H743
  - CherryUSB
  - FreeRTOS
  - SEGGER RTT
  - Keil
  - ST7735
---

开源项目地址：[yjrqz777/h7rec](https://github.com/yjrqz777/h7rec)

这篇记录整理了在 STM32H743 工程中移植 CherryUSB 和 SEGGER RTT 时遇到的问题。工程里同时使用了 FreeRTOS、ST7735 LCD、CherryUSB CDC 设备和 Keil MDK。

最开始的现象比较迷惑：USB 移植前 LCD 正常，加入 CherryUSB 后屏幕不亮；调试模式下要点几次 Run 才可能点亮；屏幕亮一会后还会复位。后面继续排查，又遇到了 RTT 输出延迟、Keil 链接 `_sys_*` 重定义等问题。

下面按问题和解决方式整理。

<!-- more -->

## 1. 移植 CherryUSB 后 LCD 不亮

一开始看起来像 LCD 初始化坏了，但后面确认 LCD 本身没问题。问题是在 USB 相关代码启动后才出现的。

关键原因有三个：

1. CherryUSB DWC2 FIFO 参数对 `USB_OTG_FS` 配得过大。
2. CherryUSB 默认日志走 `printf`，Keil 没有 retarget 时可能触发 semihosting。
3. FreeRTOS 的 `defaultTask` 栈偏小，而任务里用了 `sprintf` 和 LCD 绘制。

处理方式：

- 在 `BSP/CherryUSB_port/usb_dwc2_port.c` 中将 FS FIFO 改成 320 words 的保守配置。
- 将 CherryUSB 日志从 `printf` 改为 SEGGER RTT。
- 增大 `Core/Src/freertos.c` 中 `defaultTask` 的栈。
- 开启 FreeRTOS 栈溢出检测。

## 2. DWC2 FIFO 配置过大

原来的本地 DWC2 参数使用了类似 952 words 的 FIFO 配置，这对当前项目里的 H743 `USB_OTG_FS` 不合适，容易在 CherryUSB DWC2 初始化时触发 assert。

后来改成了保守的 FS 配置：

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

这样可以避免 FIFO overflow，或者请求的 FIFO 大于上电默认值。

## 3. USB 硬件配置检查

排查过程中检查了 USB 硬件配置，结果是硬件配置基本正确：

- `USB_OTG_FS` 使用 Device 模式。
- PA11 是 USB DM。
- PA12 是 USB DP。
- USB 时钟来自 PLL3。
- HSE 是 12 MHz。
- PLL3 输出 48 MHz 给 USB。
- VBUS sensing 关闭。
- USB 引脚和 ST7735 LCD 引脚没有冲突。

因此这次问题主要在软件移植和运行时配置，不是 PA11/PA12 或 USB 时钟的硬件配置问题。

## 4. Keil 下 `printf` 和 semihosting 的坑

CherryUSB 模板里的 `usb_config.h` 默认是：

```c
#define CONFIG_USB_PRINTF(...) printf(__VA_ARGS__)
```

在 Keil 工程中，如果没有 retarget 标准 `printf`，它可能走 semihosting。实际表现可能是：

- 调试时卡住。
- 触发 BKPT。
- 类似 HardFault。
- 输出延迟。
- USB 初始化后屏幕不亮或复位。

后来将 CherryUSB 的日志输出改为 SEGGER RTT：

```c
#include "SEGGER_RTT.h"

#define CONFIG_USB_PRINTF(...) SEGGER_RTT_printf(0, __VA_ARGS__)
```

同时关掉了当前 CDC 设备用不到的模板选项，例如：

- `CONFIG_USBDEV_MTP_THREAD`
- `CONFIG_USBDEV_RNDIS_USING_LWIP`
- `CONFIG_USBDEV_CDC_ECM_USING_LWIP`
- `CONFIG_USBHOST_BLUETOOTH_HCI_H4`

处理完后，LCD 可以正常显示，CherryUSB 也可以正常初始化。

## 5. FreeRTOS 栈问题

`defaultTask` 里有 RTC 读取、`sprintf` 和 LCD 字符串绘制。原来的栈只有 512 字节，比较危险。

处理方式是增大 `defaultTask` 栈，例如：

```c
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
```

同时开启 FreeRTOS 栈溢出检测：

```c
#define configCHECK_FOR_STACK_OVERFLOW 2
```

并添加栈溢出 hook：

```c
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;
  (void)pcTaskName;

  taskDISABLE_INTERRUPTS();
  for (;;) {
  }
}
```

这样如果后面再次出现任务栈溢出，可以在 hook 里下断点定位。

## 6. CherryUSB 放在 main 里初始化是否可以

一开始 `CherryUSB_DeviceInit()` 放在 `main()` 里会导致屏幕异常，所以暂时改成 FreeRTOS 任务里延时初始化。

后来 FIFO、`printf` 和任务栈问题修复后，重新测试发现 `CherryUSB_DeviceInit()` 放回 `main()` 也正常。

如果只是 CDC 设备，并且没有启用 `CONFIG_USBDEV_EP0_THREAD` 这类依赖 RTOS 线程的配置，那么放在 `main()` 中初始化是可以的。

推荐顺序：

```c
MX_GPIO_Init();
MX_RTC_Init();
MX_SPI2_Init();
MX_TIM2_Init();

CherryUSB_DeviceInit();

osKernelInitialize();
MX_FREERTOS_Init();
osKernelStart();
```

如果后续启用了 CherryUSB 的线程模式、MSC 线程或其他依赖 FreeRTOS 对象的功能，再考虑放回 RTOS task 中初始化。

## 7. SEGGER RTT 移植

工程中加入了 SEGGER RTT：

- `External/SEGGER_RTT_V798a/RTT/SEGGER_RTT.c`
- `External/SEGGER_RTT_V798a/RTT/SEGGER_RTT_printf.c`

Keil include path 增加：

- `External/SEGGER_RTT_V798a/RTT`
- `External/SEGGER_RTT_V798a/Config`

CherryUSB 日志最终走：

```c
SEGGER_RTT_printf(0, ...);
```

这样不会再经过 Keil semihosting。

## 8. `SEGGER_RTT_Syscalls_KEIL.c` 的链接冲突

一开始尝试加入 `SEGGER_RTT_Syscalls_KEIL.c`，结果链接报错：

```text
h7rec\h7rec.axf: Error: L6200E: Symbol _sys_close multiply defined
h7rec\h7rec.axf: Error: L6200E: Symbol _sys_write multiply defined
```

原因是 Keil 的 C 库对象 `sys_io.o` 已经定义了 `_sys_open`、`_sys_write`、`_sys_close` 等符号，而 SEGGER 的 syscalls 文件也定义了同名符号。

解决方式：

- 从 Keil 工程中移除 `SEGGER_RTT_Syscalls_KEIL.c`。
- 不 retarget 标准 `printf`。
- 直接调用 `SEGGER_RTT_printf`。

这样既能用 RTT，又不会和 Keil C 库冲突。

## 9. 是否必须调用 `SEGGER_RTT_ConfigUpBuffer`

不需要。

SEGGER RTT 默认会创建 0 号通道，下面这样即可使用：

```c
SEGGER_RTT_Init();
SEGGER_RTT_WriteString(0, "RTT start\r\n");
SEGGER_RTT_printf(0, "tick=%lu\r\n", HAL_GetTick());
```

`SEGGER_RTT_ConfigUpBuffer()` 只在需要修改 buffer 名字、大小、内存地址或满 buffer 策略时才需要。

## 10. RTT 下载后第一次输出慢，按复位后马上输出

这个现象也比较典型。

下载后第一次运行时：

1. Keil/J-Link 先擦写 Flash。
2. 然后复位并启动目标程序。
3. MCU 很快执行到 `SEGGER_RTT_WriteString`。
4. 但 RTT Viewer 可能还没有扫描到 RAM 中的 RTT 控制块。

因此 MCU 实际已经写入 RTT buffer，只是 PC 端还没有开始读。

按复位键后：

- RTT Viewer 已经连接。
- RTT 控制块地址可能已经找到。
- MCU 重新运行后，输出就会马上出现。

减少延迟的方法：

- 先打开 RTT Viewer，再复位运行。
- 在 RTT Viewer 中手动填写 `_SEGGER_RTT` 控制块地址，不使用 Auto Detection。
- 地址可以在 Keil map 文件中搜索 `_SEGGER_RTT`，或者在调试 Watch 中查看 `&_SEGGER_RTT`。

## 11. RTT 可以在哪里使用

RTT 大多数位置都可以使用，但建议注意场景：

- `main()` 中可以使用。
- FreeRTOS 任务中可以使用。
- 中断中可以使用，但不要大量使用 `SEGGER_RTT_printf`。
- HardFault 中建议只用 `SEGGER_RTT_WriteString`。
- 不要在 C 运行库和 RAM 初始化完成前使用。

当前配置中 RTT 默认模式是：

```c
SEGGER_RTT_MODE_NO_BLOCK_SKIP
```

也就是说 buffer 满了会丢日志，不会阻塞程序。这对 USB 和 RTOS 调试比较友好。

## 12. RTOS 中多个任务并行输出 RTT

可以。SEGGER RTT 内部有锁，不会破坏内部 ring buffer。

但多个任务同时输出时，文本可能交错。

建议：

- 一次输出一整行。
- 高频日志少用 `SEGGER_RTT_printf`。
- 中断里尽量用 `SEGGER_RTT_WriteString`。
- 如果要求日志绝对不交错，可以自己用 FreeRTOS mutex 包住 RTT 输出。

## 13. 调试开关：`CHERRYUSB_AUTO_START`

为了隔离 USB 初始化问题，在 `Core/Src/freertos.c` 中加入了：

```c
#define CHERRYUSB_AUTO_START 1
```

如果设置为 `0`：

```c
#define CHERRYUSB_AUTO_START 0
```

则 CherryUSB 代码仍然参与编译，但不会启动 USB 初始化。

这个开关可以用来判断问题是否来自 USB 初始化或 USB 中断链路。

## 总结

这次问题的核心不是 LCD，也不是 USB 硬件引脚，而是几个移植细节叠加在一起：

- DWC2 FIFO 配置不匹配。
- Keil 下 `printf` 触发 semihosting 风险。
- RTOS 任务栈偏小。
- RTT Viewer 首次自动扫描有延迟。
- SEGGER RTT syscalls 和 Keil C 库符号冲突。

处理完这些后，LCD、CherryUSB 和 RTT 都可以正常工作。后续如果继续扩展 USB 类，例如 MSC、HID 或复合设备，优先注意 FIFO、任务栈、日志输出和 RTOS 初始化时机。
