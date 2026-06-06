# USB CDC 文件传输到 SD 卡方案

## 目标

电脑通过 USB CDC 虚拟串口把文件发送给 STM32，STM32 接收后保存到 SD 卡的 FatFS 文件系统中。

当前工程基础：

- USB 已使用 CherryUSB CDC ACM 枚举为虚拟串口。
- SD 卡已通过 SDMMC + FatFS 挂载成功。
- STM32 端负责管理 SD 文件系统，电脑端只通过 CDC 发送数据。

## 总体架构

```text
PC Python 工具
    |
    | USB CDC 虚拟串口
    v
CherryUSB CDC OUT endpoint
    |
    | FileRx_OnUsbData()
    v
STM32 文件接收模块
    |
    | FileRxTask
    v
FatFS: f_open / f_write / f_close / f_rename
    |
    v
SD card: 0:/rx/filename
```

关键原则：

- CDC OUT 回调里不要直接写 SD 卡。
- CDC OUT 回调只负责把数据投递给文件接收模块。
- FatFS 操作放在 FreeRTOS task 中执行。
- PC 端按块发送，每块等待 STM32 ACK，避免 STM32 写 SD 时 CDC 数据堆积过快。

## 为什么不用裸发文件

CDC 只是字节流，没有文件语义。裸发文件时 STM32 不知道：

- 文件名是什么。
- 文件大小是多少。
- 什么时候结束。
- 中途是否丢数据。
- 文件是否完整。

所以需要一个简单协议。

## 推荐协议

### 1. 开始传输

PC 发送：

```text
PUT filename filesize file_crc32\n
```

字段说明：

- `filename`: 文件名，不包含路径。
- `filesize`: 文件总字节数，十进制。
- `file_crc32`: 整个文件的 CRC32，8 位十六进制。

示例：

```text
PUT test.bin 123456 89ABCDEF\n
```

STM32 检查文件名和 SD 状态，通过后回复：

```text
READY chunk_size\n
```

示例：

```text
READY 512\n
```

如果失败，回复：

```text
ERR reason\n
```

### 2. 分块发送

PC 每次发送一个数据块：

```text
BLK index len chunk_crc32\n
<binary payload>
```

字段说明：

- `index`: 块序号，从 0 开始。
- `len`: 本块 payload 字节数。
- `chunk_crc32`: 本块 payload 的 CRC32，8 位十六进制。

示例：

```text
BLK 0 512 1234ABCD\n
<512 bytes binary>
```

STM32 收到完整块后：

1. 校验本块 CRC32。
2. 写入临时文件。
3. 回复 ACK。

成功：

```text
ACK index\n
```

失败：

```text
NAK index reason\n
```

PC 收到 ACK 后才发送下一块。

### 3. 结束传输

PC 所有块发送完后：

```text
END\n
```

STM32 执行：

1. 检查收到的总字节数是否等于 `filesize`。
2. 检查整文件 CRC32 是否等于 `file_crc32`。
3. `f_close()` 临时文件。
4. 校验成功后把 `.tmp` 文件重命名为最终文件。

成功：

```text
DONE 0:/rx/filename\n
```

失败：

```text
ERR crc\n
ERR size\n
ERR fs\n
```

## SD 卡文件策略

保存目录：

```text
0:/rx/
```

当前 `FATFS/Target/ffconf.h` 中 `_USE_LFN = 0`，未开启长文件名。第一版建议只发送 FAT 8.3 文件名，例如：

```text
H7REC.HEX
TEST.BIN
README
```

传输中先写固定短文件名临时文件：

```text
0:/rx/RX.TMP
```

校验成功后改名：

```text
0:/rx/filename
```

这样即使传输中断，也不会留下一个看起来正常但内容不完整的文件。

## 文件名安全规则

STM32 端必须过滤文件名，建议规则：

- 长度 1 到 63 字节。
- 只允许 `A-Z`、`a-z`、`0-9`、`_`、`-`、`.`。
- 不允许 `/`、`\`、`:`。
- 不允许 `..`。
- 不允许空文件名。

原因：避免 PC 端构造路径写到非预期位置。

## STM32 端模块划分

建议新增：

```text
App/FileTransfer/file_rx.h
App/FileTransfer/file_rx.c
App/Storage/sd_manager.h
App/Storage/sd_manager.c
```

说明：

- `App/FileTransfer` 只处理 CDC 文件传输协议。
- `App/Storage` 处理 SD 卡检测、消抖、挂载和卸载。
- 文件传输模块通过 `SdManager_Mount()`、`SdManager_IsPresent()` 和 `SdManager_TakeRemovedEvent()` 使用 SD 状态，不直接实现热插拔逻辑。

### file_rx.h

```c
#ifndef FILE_RX_H
#define FILE_RX_H

#include <stdint.h>

void FileRx_Init(void);
void FileRx_OnUsbData(const uint8_t *data, uint32_t len);
void FileRx_Task(void *argument);

#endif
```

### CherryUSB 接收入口

当前 `BSP/CherryUSB_port/cherryusb_app.c` 的 `cdc_bulk_out()` 中是 echo：

```c
CherryUSB_CdcSend(cdc_rx_buffer, nbytes);
```

改成：

```c
FileRx_OnUsbData(cdc_rx_buffer, nbytes);
```

注意：

- `FileRx_OnUsbData()` 只把数据放进缓冲区或队列。
- 不要在 `cdc_bulk_out()` 里调用 `f_open()`、`f_write()`。

### FreeRTOS task

在 `freertos.c` 中创建 `FileRxTask`：

```c
osThreadId_t sdManagerTaskHandle;
const osThreadAttr_t sdManagerTask_attributes = {
  .name = "sdManagerTask",
  .stack_size = 1024 * 2,
  .priority = (osPriority_t) osPriorityLow,
};

osThreadId_t fileRxTaskHandle;
const osThreadAttr_t fileRxTask_attributes = {
  .name = "fileRxTask",
  .stack_size = 1024 * 6,
  .priority = (osPriority_t) osPriorityNormal,
};
```

初始化时：

```c
SdManager_Init();
sdManagerTaskHandle = osThreadNew(SdManager_Task, NULL, &sdManagerTask_attributes);
FileRx_Init();
fileRxTaskHandle = osThreadNew(FileRx_Task, NULL, &fileRxTask_attributes);
```

## FileRx 状态机

推荐状态：

```text
IDLE
    等待 PUT

RECV_BLOCK_HEADER
    等待 BLK 行

RECV_BLOCK_DATA
    等待指定长度的 binary payload

WAIT_END
    等待 END

DONE / ERROR
    清理资源，回到 IDLE
```

核心变量：

```c
typedef struct {
    FIL file;
    uint8_t file_open;
    uint32_t expected_size;
    uint32_t received_size;
    uint32_t expected_crc;
    uint32_t running_crc;
    uint32_t block_index;
    char final_path[96];
    char temp_path[100];
} FileRxContext;
```

## CRC32

PC 和 STM32 必须使用同一种 CRC32：

- 多项式：`0xEDB88320`
- 初始值：`0xFFFFFFFF`
- 输入每字节反射
- 输出异或：`0xFFFFFFFF`

这也是 Python `binascii.crc32()` 的常用形式。

## PC Python 工具流程

依赖：

```bash
pip install pyserial
```

使用方式：

```bash
python tools/pc/send_file.py COM7 test.bin
```

流程：

```text
打开串口
读取文件大小
计算整文件 CRC32
发送 PUT
等待 READY
按 chunk_size 读取文件
发送 BLK header
发送 binary payload
等待 ACK
发送 END
等待 DONE
```

## PC 端伪代码

```python
import binascii
import os
import serial

def crc32(data, crc=0):
    return binascii.crc32(data, crc) & 0xffffffff

def send_line(ser, line):
    ser.write((line + "\n").encode("ascii"))

def read_line(ser):
    return ser.readline().decode("ascii", errors="replace").strip()

def send_file(port, path):
    filename = os.path.basename(path)
    size = os.path.getsize(path)

    file_crc = 0
    with open(path, "rb") as f:
        while True:
            data = f.read(65536)
            if not data:
                break
            file_crc = crc32(data, file_crc)

    with serial.Serial(port, 115200, timeout=5) as ser:
        send_line(ser, f"PUT {filename} {size} {file_crc:08X}")
        resp = read_line(ser)
        if not resp.startswith("READY "):
            raise RuntimeError(resp)

        chunk_size = int(resp.split()[1])

        index = 0
        with open(path, "rb") as f:
            while True:
                payload = f.read(chunk_size)
                if not payload:
                    break

                block_crc = crc32(payload)
                send_line(ser, f"BLK {index} {len(payload)} {block_crc:08X}")
                ser.write(payload)

                resp = read_line(ser)
                if resp != f"ACK {index}":
                    raise RuntimeError(resp)

                index += 1

        send_line(ser, "END")
        resp = read_line(ser)
        if not resp.startswith("DONE "):
            raise RuntimeError(resp)

        print(resp)
```

说明：

- CDC 波特率对 USB CDC 实际速度通常不是严格限制，但 Windows 侧仍需要打开一个串口参数。
- 第一版可以用 `115200`，后续可改大，例如 `921600`，主要看驱动和工具兼容性。

## RTT 日志建议

STM32 端 RTT 保持简洁：

```text
[RX] PUT test.bin size=123456 crc=89ABCDEF
[RX] mounted
[RX] block 0 ok
[RX] block 100 ok
[RX] DONE 0:/rx/test.bin
```

失败时打印：

```text
[RX] ERR open res=...
[RX] ERR write res=... bw=...
[RX] ERR crc expected=... actual=...
```

不要每个 USB 包都打印 RTT，RTT 打太多会拖慢接收。

## 错误处理

建议处理：

- 文件名非法：`ERR filename`
- SD 未挂载：`ERR mount`
- 打开文件失败：`ERR open`
- 写入失败：`ERR write`
- 块 CRC 错：`NAK index crc`
- 块序号错误：`NAK index order`
- 总大小错误：`ERR size`
- 整文件 CRC 错：`ERR crc`
- PC 超时：PC 端退出，STM32 端关闭并删除 `.tmp`

## 推荐第一版参数

```text
chunk_size = 512
目录       = 0:/rx/
临时后缀   = .tmp
ACK 粒度   = 每块一次
CRC        = 块 CRC32 + 整文件 CRC32
```

第一版优先追求稳定，不追求最高速度。稳定后再把 chunk size 调到 2048 或 4096。

## 后续优化

- 添加传输进度百分比。
- 支持覆盖确认。
- 支持取消命令 `ABORT`。
- 支持列目录 `LS`。
- 支持读取 STM32 SD 文件回 PC：`GET filename`。
- 使用双缓冲提高 CDC 接收和 SD 写入并行度。
