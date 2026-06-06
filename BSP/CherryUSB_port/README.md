# CherryUSB 移植层 — STM32H743 CDC ACM 虚拟串口

## 概述

基于 [CherryUSB](https://github.com/cherry-embedded/CherryUSB) 栈在 STM32H743 上实现 USB CDC ACM 设备（虚拟串口）。

```
Host (PC)  ←→  OTG_FS  ←→  CherryUSB Stack  ←→  Application
```

---

## 数据传输路径

### ① 主机 → 设备（数据接收）

```
Host 发送数据
    ↓
USB OUT 端点 (0x02) 接收完成
    ↓
cdc_bulk_out() 回调    ← 数据在 cdc_rx_buffer 中
    ↓
转发至应用层处理
```

**关键函数：**

| 函数 | 文件 | 作用 |
|------|------|------|
| `usbd_event_handler()` 中 `USBD_EVENT_CONFIGURED` 分支 | `cherryusb_app.c` | 枚举完成后首次调用 `usbd_ep_start_read()` 启动 OUT 端点接收，之后每次接收完成都会重新启动 |
| `cdc_bulk_out()` | `cherryusb_app.c` | **主机数据到达的入口**。`cdc_rx_buffer` 中即为收到的数据，`nbytes` 为实际长度。应用在此处处理或转发接收到的数据 |
| `usbd_ep_start_read()` | CherryUSB 栈 | 启动 OUT 端点异步接收，每次完成后需再次调用以继续接收下一包 |

### ② 设备 → 主机（数据发送）

```
应用下发数据
    ↓
CherryUSB_CdcSend(data, len)     ← 将数据拷贝到 cdc_tx_buffer 并启动发送
    ↓
USB IN 端点 (0x81) 传输完成
    ↓
cdc_bulk_in() 回调                ← 处理 ZLP，释放发送忙标志
```

**关键函数：**

| 函数 | 文件 | 作用 |
|------|------|------|
| `CherryUSB_CdcSend()` | `cherryusb_app.c` | **应用发送数据的唯一接口**。检查设备配置状态与发送忙标志，拷贝数据到 `cdc_tx_buffer`，调用 `usbd_ep_start_write()` 启动 IN 传输 |
| `cdc_bulk_in()` | `cherryusb_app.c` | IN 端点发送完成回调。当数据长度是 MPS (64) 的整数倍时自动发送 ZLP 以保证传输终止；否则清除 `cdc_tx_busy` 标志，允许发起下一次发送 |

---

## 全部 API 速查

### 应用层接口 (`cherryusb_app.h`)

| 函数 | 说明 |
|------|------|
| `CherryUSB_DeviceInit()` | 初始化 USB 设备，注册描述符/接口/端点，使能 OTG_FS 中断 |
| `CherryUSB_DeviceIsReady()` | 查询设备初始化是否完成 |
| `CherryUSB_CdcSend()` | 通过 CDC IN 端点向主机发送数据 |

### 移植层接口 (`usb_dwc2_port.h`)

| 函数 | 说明 |
|------|------|
| `usb_dc_low_level_init()` | 配置 OTG_FS GPIO 与时钟（调用 `HAL_PCD_MspInit`） |
| `usb_dc_low_level_deinit()` | 释放 OTG_FS GPIO 与时钟（调用 `HAL_PCD_MspDeInit`） |
| `dwc2_get_user_params()` | 获取 DWC2 FIFO 与 DMA 参数 |
| `usbd_dwc2_delay_ms()` | 毫秒级阻塞延时（CPU 循环等待） |
| `usbd_dwc2_get_system_clock()` | 返回系统时钟频率 |

---

## 注意事项

1. **发送缓冲区** `cdc_tx_buffer` 放置在非 Cache 内存段（`.noncacheable`），避免 DMA 与 Cache 一致性问题
2. **ZLP 处理** — `cdc_bulk_in()` 中当发送长度恰好为 MPS（64 字节）的整数倍时，自动发送零长度包以向主机指示传输结束（USB 批量传输短包终止规则）
3. **接收持续** — `cdc_bulk_out()` 每次处理完数据后自动重新调用 `usbd_ep_start_read()`，确保 OUT 端点始终处于接收状态
4. **发送保护** — `CherryUSB_CdcSend()` 在设备未配置或上一次发送未完成时静默丢弃数据，应用层需等待前一次发送完成（`cdc_tx_busy == false` 隐式保证）
