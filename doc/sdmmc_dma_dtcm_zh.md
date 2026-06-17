# STM32H7 SDMMC DMA 与 DTCM 问题排查记录

## 背景

在开启 FMC、rlottie 之后，SD 卡挂载出现过如下错误：

```text
[SD] mount failed res=1 err=0x00000020 state=1 detect=1
```

其中：

- `res=1` 是 FatFs 的 `FR_DISK_ERR`。
- `err=0x00000020` 是 HAL SD 的 `HAL_SD_ERROR_RX_OVERRUN`。

这个错误表面看是 SDMMC 接收溢出，但本次根因不是 SD 卡、文件系统格式、FMC 电气冲突，也不是 SDMMC 时钟太快，而是 SDMMC DMA 访问到了不支持 DMA 的内存区域。

## 什么是 DTCM

DTCM 是 Data Tightly Coupled Memory，也就是 Cortex-M7 内核旁边的一块紧耦合数据 RAM。

在 STM32H743 上常见 RAM 地址大致如下：

```text
0x20000000 - 0x2001FFFF  DTCM RAM
0x24000000 - 0x2407FFFF  AXI SRAM
0x30000000 - ...         D2 SRAM
0xC0000000 - ...         外部 SDRAM/FMC
```

DTCM 的优点是 CPU 访问非常快、延迟低，适合放栈、小型状态变量、频繁访问的数据。

但 DTCM 有一个很重要的限制：它主要挂在 CPU 的 TCM 接口上，不在普通外设 DMA 可以访问的总线路径上。很多外设 DMA 不能直接读写 DTCM，SDMMC 的 IDMA 也不能直接访问 DTCM。

所以：

```text
CPU 访问 DTCM：可以，而且很快
SDMMC DMA 访问 DTCM：不可以
SDMMC DMA 访问 AXI SRAM：可以
```

## 为什么会导致挂载失败

FatFs 挂载时会读取 SD 卡上的 MBR、BPB、FAT 等元数据。这个过程会调用底层 `disk_read()`。

FatFs 的文件系统对象 `FATFS SDFatFS` 内部有一个 512 字节左右的工作窗口：

```c
BYTE win[_MAX_SS];
```

`f_mount()` 期间读扇区时，目标 buffer 可能就是这个 `SDFatFS.win`。

本次 map 文件显示：

```text
SDFatFS   0x20000758  fatfs.o(.bss.SDFatFS)
```

`0x2000....` 属于 DTCM。也就是说，挂载阶段 SDMMC DMA 试图把 SD 卡扇区数据写到 DTCM 里的 `SDFatFS.win`。

流程变成了：

```text
SD 卡数据
  -> SDMMC 接收
  -> SDMMC IDMA 准备写入 SDFatFS.win
  -> 目标地址在 DTCM，DMA 访问不到
  -> 数据搬不走
  -> RX_OVERRUN
  -> FatFs 返回 FR_DISK_ERR
```

这就是 `err=0x00000020` 的原因。

## 为什么以前可以，后来突然不行

开启 FMC、rlottie 后，代码量和全局变量布局都变了，Keil 链接器重新安排了 `.bss`、`.data` 等段的位置。

以前 `SDFatFS` 可能刚好落在 AXI SRAM，DMA 能访问，所以挂载正常。后来链接布局变化后，`SDFatFS` 落到了 DTCM，DMA 就失败了。

所以“突然不行”不是 SD 驱动随机坏了，而是内存布局变化触发了 SDMMC DMA 的访问限制。

## 解决思路

不要靠 polling 长期绕过，也不要靠降低 SDMMC 时钟掩盖问题。

真正需要处理的是：SDMMC DMA 不能直接访问 DTCM buffer。

当前在 `FATFS/Target/sd_diskio.c` 里的解决策略是：

```text
如果 FatFs 传入的 buffer 可被 DMA 访问：
    直接使用 SDMMC DMA 读写

如果 buffer 在 DTCM，或地址未 4 字节对齐：
    使用 AXI SRAM 中的 512B scratch buffer 做中转
```

也就是 bounce buffer。

读操作的逻辑：

```text
SD 卡
  -> SDMMC DMA
  -> AXI SRAM scratch[512]
  -> CPU memcpy
  -> DTCM 中的真实目标 buffer
```

写操作的逻辑：

```text
DTCM 中的真实源 buffer
  -> CPU memcpy
  -> AXI SRAM scratch[512]
  -> SDMMC DMA
  -> SD 卡
```

对于本来就在 AXI SRAM 的大 buffer，比如文件接收模块里的 `block_buf`，仍然走 DMA 快路径：

```text
SD 卡
  -> SDMMC DMA
  -> AXI SRAM block_buf
```

这样既保留了 DMA，又兼容 FatFs 内部对象、局部变量、栈变量等可能落入 DTCM 的情况。

## 当前代码中的关键点

`FATFS/Target/sd_diskio.c` 中增加了 DMA buffer 判断：

```c
static uint8_t SD_BufferDmaAccessible(const void *buff)
{
  uint32_t addr = (uint32_t)buff;

  if ((addr & 0x3U) != 0U) {
    return 0U;
  }

  if ((addr >= 0x20000000U) && (addr < 0x20020000U)) {
    return 0U;
  }

  return 1U;
}
```

判断结果为不可 DMA 访问时，使用 `scratch`：

```c
static uint8_t scratch[SD_DEFAULT_BLOCK_SIZE]
    __attribute__((section(".noncacheable"), aligned(32)));
```

当前 map 中确认该 `scratch` 位于 AXI SRAM：

```text
scratch  0x24000720  sd_diskio.o(.noncacheable)
```

`App/Storage/sd_manager.c` 中增加了挂载前日志，用来确认关键对象所在 RAM 区域：

```text
[SD] dma ctx SDFatFS=0x...(DTCM) win=0x...(DTCM) SDPath=0x...(DTCM)
```

如果看到 `SDFatFS` 或 `win` 是 DTCM，同时挂载仍然成功，就说明 bounce buffer 路径生效。

## 为什么不直接把 SDFatFS 放到 AXI SRAM

把 `SDFatFS` 强制放到 AXI SRAM 也能解决 `f_mount()` 这一处问题，但它不是最完整的方案。

原因是 FatFs 后续读写时，buffer 来源很多：

- `FATFS.win`
- `FIL.buf`
- 用户传入的读写 buffer
- 全局变量
- 局部变量
- 任务栈上的临时 buffer

这些 buffer 都有可能因为链接布局或栈位置变化而落到 DTCM。

如果只移动 `SDFatFS`，只能修复当前挂载失败；后续其它 FatFs 读写路径仍可能踩到 DTCM。`sd_diskio.c` 里统一判断 buffer 是否 DMA 可达，才是更稳的处理方式。

## 关于 ClockDiv

之前临时把 `Core/Src/sdmmc.c` 里的 `ClockDiv` 从 `1` 改成 `8`，属于止血排查手段。

本次确认根因是 DMA 目标地址不可达，不是 SDMMC 时钟过快，所以已经恢复：

```c
hsd1.Init.ClockDiv = 1;
```

如果后续仍出现 CRC、timeout、通信不稳定，再单独从 SDMMC 时钟、走线、上拉、电源、卡兼容性方向排查。

## 后续排查 checklist

遇到 SDMMC DMA 相关问题时，优先检查：

1. HAL 错误码是否是 `HAL_SD_ERROR_RX_OVERRUN`。
2. FatFs 返回值是否是 `FR_DISK_ERR` 或 `FR_NOT_READY`。
3. map 文件里相关 buffer 是否位于 `0x20000000` 开头的 DTCM。
4. DMA 中转 buffer 是否真的在 `0x24000000` 开头的 AXI SRAM。
5. 如果开启 DCache，是否对 DMA buffer 做了 cache clean/invalidate。
6. SDMMC IRQ 是否启用，DMA 完成回调是否能唤醒等待队列。
7. SDMMC 时钟和硬件流控是否符合当前板子的稳定性要求。

## 结论

这次 SD 卡挂载失败的关键原因是：

```text
FatFs 的工作 buffer 落在 DTCM
SDMMC IDMA 无法访问 DTCM
导致 DMA 接收时 RX_OVERRUN
```

当前解决方案是在 FatFs disk I/O 层保留 DMA，同时对 DTCM/未对齐 buffer 使用 AXI SRAM bounce buffer。这比长期 polling 或单纯降频更接近根因，也更能适应以后链接布局变化。
