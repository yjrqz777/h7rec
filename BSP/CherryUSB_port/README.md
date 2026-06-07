# CherryUSB 移植层 — STM32H743 CDC ACM 虚拟串口

## 概述

基于 [CherryUSB](https://github.com/cherry-embedded/CherryUSB) 栈在 STM32H743 上实现 USB CDC ACM 设备（虚拟串口）。配合 `App/FileTransfer/` 文件接收模块，通过简单 ASCII 行协议将文件从 PC 传输到 SD 卡（无 ACK 流式传输）。

```
Host (PC)  ←→  OTG_FS  ←→  CherryUSB  ←→  FileRx_OnUsbData  ←→  FileRx_Task  ←→  FATFS / SD
```

---

## 数据传输路径

### ① 主机 → 设备（数据接收）

```
Host 发送数据
    ↓
USB OUT 端点 (0x02) 接收完成
    ↓
cdc_bulk_out() 回调        ← 数据在 cdc_rx_buffer 中
    ↓
FileRx_OnUsbData()         ← 逐字节放入环形缓冲区
    ↓
FileRx_Task (RTOS 任务)    ← 轮询环形缓冲区，送入协议状态机
    ↓
handle_rx_byte()           ← 逐字节状态机
    ├── ASCII 行 → handle_line() → PUT/BLK/END 协议解析
    └── 二进制负载 → handle_payload_byte() → CRC 校验 → write_buf 累积
    ↓
f_write()                  ← 写满 8 KiB 或文件结束时批量写入 SD
```

**关键函数：**

| 函数 | 文件 | 作用 |
|------|------|------|
| `cdc_bulk_out()` | `cherryusb_app.c` | **USB 数据入口**。`cdc_rx_buffer` 中为收到的数据，调用 `FileRx_OnUsbData()` 转发 |
| `FileRx_OnUsbData()` | `file_rx.c` | 将 USB 数据放入环形缓冲区（生产者端） |
| `FileRx_Task()` | `file_rx.c` | 从环形缓冲区取出数据，逐字节送入状态机（消费者端） |
| `handle_rx_byte()` | `file_rx.c` | 字节级状态机，区分 ASCII 命令与二进制负载 |

### ② 设备 → 主机（数据发送）

```
应用下发数据
    ↓
send_line(fmt, ...)        ← 格式化响应文本 (READY/DONE/ERR)
    ↓
CherryUSB_CdcSend(data, len)
    ↓
USB IN 端点 (0x81) 传输完成
    ↓
cdc_bulk_in() 回调          ← 处理 ZLP，释放发送忙标志
```

**关键函数：**

| 函数 | 文件 | 作用 |
|------|------|------|
| `send_line()` | `file_rx.c` | 应用层向 PC 发送 ASCII 响应（等待 CDC 可发送，最多 1s） |
| `CherryUSB_CdcSend()` | `cherryusb_app.c` | **应用发送数据的底层接口**。拷贝数据到 `cdc_tx_buffer`，启动 IN 传输 |
| `cdc_bulk_in()` | `cherryusb_app.c` | IN 端点完成回调。MPS 整倍长时发 ZLP 终止，否则清 `cdc_tx_busy` |

---

## 文件传输协议（无 ACK 流式）

```
PC                                     STM32
──                                     ─────
PUT <name> <size> <crc32>      ──→
                                ←──    READY <chunk_size>
BLK <i> <len> <crc>            ──→
<raw payload N bytes>          ──→    (无需 ACK，连续推送下一块)
... 重复直到文件发完 ...
END                             ──→
                                ←──    DONE <path>          (成功)
                                ←──    ERR <reason>         (失败)
```

- 块大小由 STM32 在 `READY` 响应中指定（当前 1024 字节）
- 无逐块 ACK — PC 连续推送，STM32 通过 USB NAK 机制做流控
- 异常时 STM32 中止传输，发送 `ERR` 响应

---

## 全部 API 速查

### CDC 应用层 (`cherryusb_app.h`)

| 函数 | 说明 |
|------|------|
| `CherryUSB_DeviceInit()` | 初始化 USB 设备，注册描述符/接口/端点，使能 OTG_FS 中断 |
| `CherryUSB_DeviceIsReady()` | 查询设备初始化是否完成 |
| `CherryUSB_CdcSend()` | 通过 CDC IN 端点向主机发送数据 |
| `CherryUSB_CdcCanSend()` | 查询 CDC IN 端点是否可发送（无正在进行的传输） |

### 文件接收模块 (`file_rx.h`)

| 函数 | 说明 |
|------|------|
| `FileRx_Init()` | 初始化文件接收模块（清空环形缓冲区、重置状态机） |
| `FileRx_OnUsbData()` | USB 数据入口，写入环形缓冲区（由 `cdc_bulk_out` 调用） |
| `FileRx_Task()` | FreeRTOS 任务，轮询环形缓冲区并解析协议 |

### DWC2 移植层 (`usb_dwc2_port.h`)

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
4. **无 ACK 流控** — 去掉逐块 ACK 后，依赖 USB 硬件 NAK 机制做自然流控。STM32 处理不过来时 USB 核心自动 NAK OUT 事务，PC 端 USB 主机控制器自动重试
5. **SD 写入优化** — 接收模块使用 8 KiB 写入累积缓冲区，积满或文件结束时才一次性 `f_write`，减少 FATFS 元数据更新次数
