/**
 * @file    file_rx.h
 * @brief   USB CDC 文件接收模块 — 头文件
 *
 * 与 PC 端 send_file.py 配合，实现通过 USB CDC ACM 虚拟串口
 * 向 STM32 发送文件并保存到 SD 卡的功能。
 *
 * 通信协议:
 *   PUT -> READY -> [BLK+payload+ACK]xN -> END -> DONE
 */
#ifndef FILE_RX_H
#define FILE_RX_H

#include <stdint.h>

/* ======================== 文件接收配置宏 ======================== */

#define FILE_RX_RING_SIZE         32768U              /* USB 接收环形缓冲区大小 */
#define FILE_RX_CHUNK_SIZE        8192U               /* 单块 payload 大小，STM32 通过 READY 返回给 PC */
#define FILE_RX_SD_WRITE_SIZE     FILE_RX_CHUNK_SIZE  /* 单次 f_write 最大写入字节数 */
#define FILE_RX_PROGRESS_STEP     65536U              /* RTT 进度输出步进，减少传输中打印次数 */
#define FILE_RX_UNIT_KB           1024U               /* KiB 单位换算 */
#define FILE_RX_UNIT_MB           (1024U * 1024U)     /* MiB 单位换算 */
#define FILE_RX_LINE_SIZE         160U                /* 文本命令行缓冲区大小，如 PUT/BLK/END */
#define FILE_RX_NAME_SIZE         64U                 /* 文件名缓冲区大小，当前协议限制 ASCII 文件名 */
#define FILE_RX_PATH_SIZE         128U                /* FATFS 路径缓冲区大小 */
#define FILE_RX_LCD_TEXT_CHARS    26U                 /* LCD 第一行 12 号字体可显示字符数 */
#define FILE_RX_DIR               "0:/rx"             /* 接收文件保存目录 */
#define FILE_RX_TEMP_PATH         "0:/rx/RX.TMP"      /* 接收过程使用的临时文件路径 */
#define FILE_RX_DEBUG_DUMP        0                   /* USB 接收字节 RTT dump 开关：0 关闭，1 开启 */
#define FILE_RX_DEBUG_DUMP_MAX    64U                 /* 单次 RTT dump 最多显示的字节数 */

/**
 * @brief  初始化文件接收模块
 *
 * 清空环形缓冲区并重置状态机，在 FreeRTOS 任务启动前调用。
 */
void FileRx_Init(void);

/**
 * @brief  接收 USB 原始数据（由 CDC 回调调用）
 *
 * 将 CDC OUT 端点收到的数据放入环形缓冲区，
 * 由 FileRx_Task 异步解析处理。
 *
 * @param  data  数据缓冲区
 * @param  len   数据长度
 */
void FileRx_OnUsbData(const uint8_t *data, uint32_t len);

/**
 * @brief  获取文件接收状态显示文本
 *
 * 空闲时返回 RX:Idle；接收中返回当前接收进度；
 * 最近一次传输成功后返回 RX:<filename> OK，供 LCD 第一行显示。
 *
 * @param  buffer      输出缓冲区
 * @param  buffer_len  输出缓冲区长度
 */
void FileRx_GetStatusText(char *buffer, uint32_t buffer_len);

/**
 * @brief  文件接收处理任务（FreeRTOS）
 *
 * 轮询环形缓冲区，逐字节解析协议（PUT/BLK/END），
 * 校验 CRC，累积写入 SD 卡。
 *
 * @param  argument  任务参数（未使用）
 */
void FileRx_Task(void *argument);

#endif /* FILE_RX_H */
