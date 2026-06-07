# USB CDC 文件发送脚本

`send_file.py` 用于在 PC 端通过 USB CDC 虚拟串口把文件发送到 STM32，固件端接收后保存到 SD 卡的 `0:/rx` 目录。

## 环境准备

安装 Python 依赖：

```bash
pip install pyserial
```

Windows 下串口一般是 `COMx`，例如 `COM7`。Linux/macOS 下通常是 `/dev/ttyACM0` 或 `/dev/ttyUSB0`。

## 基本用法

```bash
python tools/pc/send_file.py COM7 H7REC.HEX
```

指定波特率：

```bash
python tools/pc/send_file.py COM7 H7REC.HEX --baudrate 115200
```

指定读写超时时间：

```bash
python tools/pc/send_file.py COM7 H7REC.HEX --timeout 60
```

开启发送数据调试打印：

```bash
python tools/pc/send_file.py COM7 H7REC.HEX --dump-bytes 32
```

## 命令行参数

| 参数 | 必填 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `port` | 是 | 无 | 串口名称。Windows 示例：`COM7`；Linux/macOS 示例：`/dev/ttyACM0`。 |
| `file` | 是 | 无 | 要发送的本地文件路径。脚本只发送文件名，不发送目录结构。 |
| `--baudrate` | 否 | `115200` | pyserial 打开串口时使用的波特率。USB CDC 虚拟串口通常不真正受这个值限制，但仍需要传给串口驱动。 |
| `--timeout` | 否 | `30.0` | 串口读写超时时间，单位秒。大文件、慢 SD 卡或调试时可以适当加大。 |
| `--dump-bytes` | 否 | `-1` | 调试打印发送内容。负数关闭；`0` 打印全部；正数只打印每个命令/数据块的前 N 个字节。 |

### `--dump-bytes` 细节

`--dump-bytes` 只用于调试，不影响协议本身。

关闭打印：

```bash
python tools/pc/send_file.py COM7 H7REC.HEX --dump-bytes -1
```

打印每条文本命令和每个数据块的前 32 字节：

```bash
python tools/pc/send_file.py COM7 H7REC.HEX --dump-bytes 32
```

打印完整数据：

```bash
python tools/pc/send_file.py COM7 H7REC.HEX --dump-bytes 0
```

注意：`--dump-bytes 0` 会把每个二进制块完整打印到终端，大文件会非常慢，也会刷屏。排查 CRC、协议头、首块数据时建议用 `--dump-bytes 16` 或 `--dump-bytes 32`。

## 文件名限制

当前固件按 FAT 8.3 文件名校验，文件名需要满足：

- 主文件名不超过 8 个字符
- 扩展名不超过 3 个字符
- 只使用 ASCII 字母、数字、`_`、`-`、`.`
- 不允许路径分隔符和 `..`

例如：

```text
H7REC.HEX
TEST.BIN
DATA_001.TXT
```

## 传输协议

PC 和 STM32 使用一问一答的块传输协议：

```text
PC  -> STM32: PUT <filename> <size> <crc32>
STM32 -> PC: READY <chunk_size>

PC  -> STM32: BLK <index> <len> <block_crc32>
PC  -> STM32: <binary payload>
STM32 -> PC: ACK <index>

重复 BLK/ACK 直到文件发送完成

PC  -> STM32: END
STM32 -> PC: DONE <path>
```

`chunk_size` 由 STM32 固件返回，PC 脚本会自动使用，不需要在脚本里手动配置。

## 校验方式

脚本发送前会计算完整文件 CRC32，并在 `PUT` 命令里发送给 STM32。

每个数据块还会单独计算 CRC32：

- 块 CRC 错误：STM32 返回 `NAK <index> crc` 或 `ERR block_crc`
- 文件总 CRC 错误：STM32 在 `END` 后返回 `ERR crc`

## 覆盖规则

STM32 端会先写入临时文件：

```text
0:/rx/RX.TMP
```

文件完整接收并校验通过后，再覆盖目标文件。这样传输中断时不会留下半个目标文件。

## 常见输出

正常传输：

```text
open COM7, send H7REC.HEX, size=7534984, crc=ACE821E3
< READY 8192
7.19/7.19 MB (100.0%)   431.2 KiB/s
send END, waiting DONE...
< DONE 0:/rx/H7REC.HEX
done in 17.06s, avg 431.2 KiB/s
```

常见错误：

```text
ERR sd_absent      SD 卡未挂载或不可用
ERR open           打开临时文件失败
ERR write          写 SD 失败
ERR block_crc      单块 CRC 校验失败
ERR crc            文件总 CRC 校验失败
timeout waiting    等待 STM32 响应超时
```

## 速度说明

速度主要受固件端 `FILE_RX_CHUNK_SIZE`、SD 卡写入延迟、USB CDC 调度和 ACK 等待影响。脚本会以 KiB/s 显示平均传输速度；大于 1 MB 的文件进度按 MB 显示，小文件按 KB 显示。
