#include "file_rx.h"

#include "FreeRTOS.h"
#include "cmsis_os2.h"

#include "SEGGER_RTT.h"
#include "cherryusb_app.h"
#include "fatfs.h"
#include "sd_manager.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/*
 * USB CDC 文件接收模块
 *
 * 数据路径：
 *   USB CDC OUT 回调 -> file_rx_ring 环形缓冲 -> FileRx_Task 解析协议
 *   -> 校验块 CRC -> 写入 0:/rx/RX.TMP -> END 后校验整文件 CRC -> rename 为目标文件
 *
 * ACK 策略：
 *   每个 block 的 payload 完整接收、CRC 通过并写入 SD 后，才返回 ACK。
 *   这样 PC 端不会跑得比 SD 写入更快，逻辑简单且不容易丢数据。
 */
typedef enum {
    FILE_RX_STATE_IDLE = 0,      /* 等待 PUT 命令 */
    FILE_RX_STATE_WAIT_BLOCK,    /* 文件已打开，等待 block 或 END 文本行 */
    FILE_RX_STATE_RECV_PAYLOAD   /* 正在接收当前 block 的二进制 payload */
} FileRxState;

typedef struct {
    FileRxState state;
    FIL file;
    uint8_t file_open;
    uint32_t expected_size;       /* PUT 中声明的文件总大小 */
    uint32_t received_size;       /* 已经写入并 ACK 的字节数 */
    uint32_t expected_crc;        /* PUT 中声明的整文件 CRC32 */
    uint32_t running_crc;         /* 接收过程中累积计算的整文件 CRC32 */
    uint32_t block_index;         /* 下一个期望收到的 block 序号 */
    uint32_t block_len;           /* 当前 block payload 长度 */
    uint32_t block_received;      /* 当前 block 已收到的 payload 字节数 */
    uint32_t block_expected_crc;  /* 当前 block 头部声明的 CRC32 */
    char name[FILE_RX_NAME_SIZE];
    char final_path[FILE_RX_PATH_SIZE];
    char temp_path[FILE_RX_PATH_SIZE];
} FileRxContext;

/* CDC 回调可能在任务处理 SD 写入时继续进数据，因此先进入环形缓冲。 */
static uint8_t file_rx_ring[FILE_RX_RING_SIZE];              /* USB CDC OUT 回调写入、接收任务读取的环形缓冲区 */
static volatile uint32_t file_rx_ring_head;                  /* 环形缓冲写入位置，由 USB CDC OUT 回调推进 */
static volatile uint32_t file_rx_ring_tail;                  /* 环形缓冲读取位置，由 FileRx_Task 推进 */
static volatile uint8_t file_rx_ring_overflow;               /* 环形缓冲写满标志，提示上层发生过输入丢失 */

static FileRxContext rx;                                     /* 当前文件接收会话的协议状态、路径、CRC 和 FatFs 文件句柄 */
static char last_done_name[FILE_RX_NAME_SIZE];               /* 最近一次成功接收完成的文件名，用于状态显示/查询 */
static uint8_t last_done_valid;                              /* last_done_name 是否有效 */
static char line_buf[FILE_RX_LINE_SIZE];                     /* 文本命令行缓冲区，用于解析 PUT/block/END 等协议行 */
static uint32_t line_len;                                    /* line_buf 当前已缓存的字符数 */
static uint32_t progress_last_report;                        /* 上一次进度 RTT 输出的已接收字节数，用于限频 */
ALIGN_32BYTES(static uint8_t block_buf[FILE_RX_CHUNK_SIZE]); /* 当前 block payload 缓冲区，32 字节对齐便于 SD/DMA/cache 处理 */

/**
 * @brief  整理 LCD 状态字符串长度
 *
 * 将字符串截断/补空格到 FILE_RX_LCD_TEXT_CHARS，确保 LCD 重绘时能覆盖旧字符。
 */
static void finish_status_text(char *buffer, uint32_t buffer_len)
{
    uint32_t max_chars;
    uint32_t len;

    if (buffer == NULL || buffer_len == 0U) {
        return;
    }

    max_chars = FILE_RX_LCD_TEXT_CHARS;
    if (max_chars >= buffer_len) {
        max_chars = buffer_len - 1U;
    }

    len = (uint32_t)strlen(buffer);
    if (len > max_chars) {
        buffer[max_chars] = '\0';
        len = max_chars;
    }

    /* LCD_ShowString 会把空格也画成背景色，用补空格覆盖上一帧残留字符。 */
    while (len < max_chars) {
        buffer[len] = ' ';
        len++;
    }
    buffer[len] = '\0';
}

/**
 * @brief  软件 CRC32 增量计算
 *
 * @param  crc   上一次 CRC 中间值
 * @param  data  待计算数据
 * @param  len   数据长度
 * @return 更新后的 CRC 中间值
 */
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint32_t bit;

    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (bit = 0; bit < 8U; bit++) {
            if ((crc & 1U) != 0U) {
                crc = (crc >> 1) ^ 0xEDB88320UL;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

/**
 * @brief  计算环形缓冲区下一个下标
 *
 * @param  index 当前下标
 * @return 下一个下标，必要时回绕到 0
 */
static uint32_t ring_next(uint32_t index)
{
    index++;
    if (index >= FILE_RX_RING_SIZE) {
        index = 0;
    }
    return index;
}

/**
 * @brief  RTT 打印 USB 原始接收字节
 *
 * 仅在 FILE_RX_DEBUG_DUMP 打开时输出，用于排查协议头或 payload 内容。
 */
static void debug_dump_rx_bytes(const uint8_t *data, uint32_t len)
{
#if FILE_RX_DEBUG_DUMP
    uint32_t i;
    uint32_t dump_len;

    dump_len = len;
    if (dump_len > FILE_RX_DEBUG_DUMP_MAX) {
        dump_len = FILE_RX_DEBUG_DUMP_MAX;
    }

    SEGGER_RTT_printf(0, "[RXD] raw len=%u dump=%u\r\n", len, dump_len);
    for (i = 0; i < dump_len; i++) {
        if ((i % 16U) == 0U) {
            SEGGER_RTT_printf(0, "[RXD] %04u:", i);
        }

        SEGGER_RTT_printf(0, " %02X", data[i]);

        if (((i % 16U) == 15U) || (i + 1U == dump_len)) {
            SEGGER_RTT_WriteString(0, "\r\n");
        }
    }

    if (dump_len < len) {
        SEGGER_RTT_WriteString(0, "[RXD] ...\r\n");
    }
#else
    (void)data;
    (void)len;
#endif
}

/**
 * @brief  RTT 输出接收进度
 *
 * 按 FILE_RX_PROGRESS_STEP 限频输出，避免每个块都打印导致传输变慢。
 */
static void print_progress(void)
{
    uint32_t pct10;
    uint32_t rx_unit100;
    uint32_t total_unit100;

    if (progress_last_report != 0U &&
        rx.received_size != rx.expected_size &&
        (rx.received_size - progress_last_report) < FILE_RX_PROGRESS_STEP) {
        return;
    }
    progress_last_report = rx.received_size;

    if (rx.expected_size == 0U) {
        pct10 = 1000U;
    } else {
        pct10 = (uint32_t)(((uint64_t)rx.received_size * 1000ULL) / rx.expected_size);
        if (pct10 > 1000U) {
            pct10 = 1000U;
        }
    }

    if (rx.expected_size >= FILE_RX_UNIT_MB) {
        rx_unit100 = (uint32_t)(((uint64_t)rx.received_size * 100ULL) / FILE_RX_UNIT_MB);
        total_unit100 = (uint32_t)(((uint64_t)rx.expected_size * 100ULL) / FILE_RX_UNIT_MB);
        SEGGER_RTT_printf(0, "[RX] %u.%02u/%u.%02u MB (%u.%u%%)\r\n",
                          rx_unit100 / 100U,
                          rx_unit100 % 100U,
                          total_unit100 / 100U,
                          total_unit100 % 100U,
                          pct10 / 10U,
                          pct10 % 10U);
    } else {
        rx_unit100 = (uint32_t)(((uint64_t)rx.received_size * 10ULL) / FILE_RX_UNIT_KB);
        total_unit100 = (uint32_t)(((uint64_t)rx.expected_size * 10ULL) / FILE_RX_UNIT_KB);
        SEGGER_RTT_printf(0, "[RX] %u.%u/%u.%u KB (%u.%u%%)\r\n",
                          rx_unit100 / 10U,
                          rx_unit100 % 10U,
                          total_unit100 / 10U,
                          total_unit100 % 10U,
                          pct10 / 10U,
                          pct10 % 10U);
    }
}

/**
 * @brief  检查 PC 端传来的文件名是否安全
 *
 * 当前限制为 FAT 8.3 风格 ASCII 文件名，禁止路径、连续点号和非法字符。
 *
 * @return 1 文件名可接受；0 文件名非法
 */
static uint8_t filename_is_safe(const char *name)
{
    uint32_t i;
    uint32_t len;
    uint32_t dot_count = 0;
    uint32_t base_len = 0;
    uint32_t ext_len = 0;
    uint8_t in_ext = 0;

    if (name == NULL) {
        return 0;
    }

    len = (uint32_t)strlen(name);
    if (len == 0U || len >= FILE_RX_NAME_SIZE) {
        return 0;
    }

    if (strstr(name, "..") != NULL) {
        return 0;
    }

    for (i = 0; i < len; i++) {
        char c = name[i];
        if (c == '.') {
            dot_count++;
            if (dot_count > 1U || base_len == 0U) {
                return 0;
            }
            in_ext = 1;
            continue;
        }

        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '-') {
            if (in_ext != 0U) {
                ext_len++;
                if (ext_len > 3U) {
                    return 0;
                }
            } else {
                base_len++;
                if (base_len > 8U) {
                    return 0;
                }
            }
        }
        else {
            return 0;
        }
    }

    return (base_len != 0U) ? 1U : 0U;
}

/**
 * @brief  通过 USB CDC 给 PC 端发送一行文本响应
 *
 * 响应包括 READY/ACK/DONE/ERR/NAK 等。函数内部会短暂等待 CDC 可发送。
 */
static void send_line(const char *fmt, ...)
{
    char tx[160];
    va_list ap;
    int len;
    uint32_t wait_ms;

    va_start(ap, fmt);
    len = vsnprintf(tx, sizeof(tx), fmt, ap);
    va_end(ap);

    if (len <= 0) {
        return;
    }

    if ((uint32_t)len >= sizeof(tx)) {
        len = (int)sizeof(tx) - 1;
    }

    wait_ms = 0;
    while (!CherryUSB_CdcCanSend() && wait_ms < 1000U) {
        osDelay(1);
        wait_ms++;
    }

    if (CherryUSB_CdcCanSend()) {
        CherryUSB_CdcSend((const uint8_t *)tx, (uint32_t)len);
    } else {
        SEGGER_RTT_printf(0, "[RX] CDC send timeout: %s", tx);
    }
}

/**
 * @brief  关闭当前临时文件并删除临时文件
 *
 * 仅在传输失败路径调用，保证下一次传输不会继承半文件。
 */
static void close_and_delete_temp(void)
{
    /* 传输失败时删除临时文件，避免下次接收误用半文件。 */
    if (rx.file_open != 0U) {
        f_close(&rx.file);
        rx.file_open = 0;
    }

    if (rx.temp_path[0] != '\0') {
        (void)f_unlink(rx.temp_path);
    }
}

/**
 * @brief  重置接收状态机
 *
 * 不清除 last_done_name，这样传输成功后 LCD 仍可显示最后文件名和 OK。
 */
static void reset_context(void)
{
    memset(&rx, 0, sizeof(rx));
    rx.state = FILE_RX_STATE_IDLE;
    line_len = 0;
    progress_last_report = 0;
}

/**
 * @brief  处理传输过程中 SD 不可用的情况
 *
 * 当前工程没有可靠插卡检测，因此实际判断来自 SdManager_IsPresent。
 */
static void handle_sd_removed(void)
{
    SEGGER_RTT_WriteString(0, "[SD] removed\r\n");

    if (rx.state != FILE_RX_STATE_IDLE || rx.file_open != 0U) {
        rx.file_open = 0;
        send_line("ERR sd_removed\n");
        reset_context();
    }
}

/**
 * @brief  统一传输失败处理
 *
 * 打印 RTT、删除临时文件、通知 PC 端错误并回到空闲状态。
 */
static void fail_transfer(const char *reason)
{
    SEGGER_RTT_printf(0, "[RX] ERR %s\r\n", reason);
    close_and_delete_temp();
    send_line("ERR %s\n", reason);
    reset_context();
}

/**
 * @brief  将一块通过校验的数据写入 SD 文件
 *
 * @return 1 写入成功；0 写入失败且已经触发 fail_transfer
 */
static uint8_t write_file_data(const uint8_t *data, uint32_t len)
{
    UINT bw;
    FRESULT res;
    uint32_t offset = 0;

    while (offset < len) {
        uint32_t chunk = len - offset;
        if (chunk > FILE_RX_SD_WRITE_SIZE) {
            chunk = FILE_RX_SD_WRITE_SIZE;
        }

        /* f_write 成功但 bw 小于请求长度也按失败处理，避免生成短文件。 */
        res = f_write(&rx.file, &data[offset], chunk, &bw);
        if (res != FR_OK || bw != chunk) {
            SEGGER_RTT_printf(0, "[RX] write failed res=%d bw=%u len=%u\r\n",
                              res, bw, chunk);
            fail_transfer("write");
            return 0;
        }

        offset += chunk;
    }

    return 1;
}

/**
 * @brief  确保 SD 已挂载并创建接收目录
 *
 * @return 1 SD 可用且目录存在；0 SD 不可用或目录创建失败
 */
static uint8_t sd_mount_if_ready(void)
{
    FRESULT res;
    SdManagerStatus status;

    status = SdManager_Mount();
    if (status == SD_MANAGER_ERR_ABSENT) {
        send_line("ERR sd_absent\n");
        return 0;
    }
    if (status == SD_MANAGER_ERR_INIT) {
        send_line("ERR sd_init\n");
        return 0;
    }
    if (status == SD_MANAGER_ERR_MOUNT) {
        send_line("ERR mount\n");
        return 0;
    }

    res = f_mkdir(FILE_RX_DIR);
    if (res != FR_OK && res != FR_EXIST) {
        SEGGER_RTT_printf(0, "[RX] mkdir failed res=%d\r\n", res);
        send_line("ERR mkdir\n");
        SdManager_Unmount();
        return 0;
    }

    return 1;
}

/**
 * @brief  处理 PUT 命令并准备接收目标文件
 *
 * 校验文件名、挂载 SD、创建临时文件，并向 PC 返回 READY <chunk_size>。
 *
 * @return 1 准备成功；0 准备失败
 */
static uint8_t prepare_file(const char *name, uint32_t size, uint32_t crc)
{
    FRESULT res;
    FILINFO file_info;

    if (!filename_is_safe(name)) {
        send_line("ERR filename\n");
        return 0;
    }

    if (!sd_mount_if_ready()) {
        return 0;
    }

    last_done_valid = 0;
    last_done_name[0] = '\0';

    memset(&rx, 0, sizeof(rx));
    rx.state = FILE_RX_STATE_WAIT_BLOCK;
    rx.expected_size = size;
    rx.expected_crc = crc;
    rx.running_crc = 0xFFFFFFFFUL;
    strncpy(rx.name, name, sizeof(rx.name) - 1U);
    snprintf(rx.final_path, sizeof(rx.final_path), "%s/%s", FILE_RX_DIR, rx.name);
    strncpy(rx.temp_path, FILE_RX_TEMP_PATH, sizeof(rx.temp_path) - 1U);

    res = f_stat(rx.final_path, &file_info);
    if (res == FR_OK) {
        SEGGER_RTT_printf(0, "[RX] overwrite %s old_size=%u\r\n",
                          rx.final_path, (uint32_t)file_info.fsize);
    } else if (res != FR_NO_FILE) {
        SEGGER_RTT_printf(0, "[RX] stat failed res=%d path=%s\r\n", res, rx.final_path);
    }

    /*
     * 先写临时文件，最终校验通过后再 rename。
     * 如果传输中断，旧目标文件仍然保留，不会被半文件覆盖。
     */
    (void)f_unlink(rx.temp_path);
    res = f_open(&rx.file, rx.temp_path, FA_CREATE_ALWAYS | FA_WRITE);
    if (res != FR_OK) {
        SEGGER_RTT_printf(0, "[RX] open failed res=%d\r\n", res);
        send_line("ERR open %d\n", res);
        reset_context();
        return 0;
    }

    rx.file_open = 1;
    SEGGER_RTT_printf(0, "[RX] PUT %s size=%u crc=%08X\r\n", rx.name, rx.expected_size, rx.expected_crc);
    send_line("READY %u\n", FILE_RX_CHUNK_SIZE);
    SEGGER_RTT_printf(0, "[RX] READY %u\r\n", FILE_RX_CHUNK_SIZE);
    return 1;
}

/**
 * @brief  处理 END 命令并完成文件落盘
 *
 * 关闭临时文件，校验总大小和整文件 CRC，通过后覆盖目标文件并返回 DONE。
 */
static void finish_file(void)
{
    FRESULT res;
    uint32_t actual_crc;

    if (!SdManager_IsPresent()) {
        handle_sd_removed();
        return;
    }

    if (rx.file_open != 0U) {
        res = f_close(&rx.file);
        rx.file_open = 0;
        if (res != FR_OK) {
            SEGGER_RTT_printf(0, "[RX] close failed res=%d\r\n", res);
            fail_transfer("close");
            return;
        }
    }

    actual_crc = rx.running_crc ^ 0xFFFFFFFFUL;

    if (rx.received_size != rx.expected_size) {
        SEGGER_RTT_printf(0, "[RX] size mismatch got=%u expected=%u\r\n",
                          rx.received_size, rx.expected_size);
        fail_transfer("size");
        return;
    }

    if (actual_crc != rx.expected_crc) {
        SEGGER_RTT_printf(0, "[RX] crc mismatch got=%08X expected=%08X\r\n",
                          actual_crc, rx.expected_crc);
        fail_transfer("crc");
        return;
    }

    /*
     * 到这里说明大小和整文件 CRC 都已经通过。
     * FatFs rename 不能可靠覆盖同名文件，所以先删除旧文件再改名。
     */
    res = f_unlink(rx.final_path);
    if (res != FR_OK && res != FR_NO_FILE) {
        SEGGER_RTT_printf(0, "[RX] unlink failed res=%d\r\n", res);
    }

    res = f_rename(rx.temp_path, rx.final_path);
    if (res != FR_OK) {
        SEGGER_RTT_printf(0, "[RX] rename failed res=%d\r\n", res);
        fail_transfer("rename");
        return;
    }

    SEGGER_RTT_printf(0, "[RX] DONE %s (%u bytes)\r\n", rx.final_path, rx.received_size);
    send_line("DONE %s\n", rx.final_path);
    strncpy(last_done_name, rx.name, sizeof(last_done_name) - 1U);
    last_done_name[sizeof(last_done_name) - 1U] = '\0';
    last_done_valid = 1U;
    reset_context();
}

/**
 * @brief  解析空闲状态下收到的 PUT 文本行
 */
static void handle_put_line(char *line)
{
    char cmd[8];
    char name[FILE_RX_NAME_SIZE];
    unsigned long size;
    unsigned long crc;
    int n;

    n = sscanf(line, "%7s %63s %lu %lx", cmd, name, &size, &crc);
    if (n != 4 || strcmp(cmd, "PUT") != 0) {
        send_line("ERR command\n");
        return;
    }

    (void)prepare_file(name, (uint32_t)size, (uint32_t)crc);
}

/**
 * @brief  解析接收状态下收到的 block 或 END 文本行
 *
 * block 通过后切换到 FILE_RX_STATE_RECV_PAYLOAD，后续字节按二进制 payload 处理。
 */
static void handle_block_line(char *line)
{
    char cmd[8];
    unsigned long index;
    unsigned long len;
    unsigned long crc;
    int n;

    if (strcmp(line, "END") == 0) {
        finish_file();
        return;
    }

    n = sscanf(line, "%7s %lu %lu %lx", cmd, &index, &len, &crc);
    if (n != 4 || strcmp(cmd, "BLK") != 0) {
        fail_transfer("command");
        return;
    }

    if ((uint32_t)index != rx.block_index) {
        /* PC 端必须等待 ACK 后再发下一块；序号不连续说明协议流已经乱了。 */
        send_line("NAK %lu order\n", index);
        fail_transfer("order");
        return;
    }

    if (len == 0UL || len > FILE_RX_CHUNK_SIZE) {
        send_line("NAK %lu len\n", index);
        fail_transfer("len");
        return;
    }

    if ((rx.received_size + (uint32_t)len) > rx.expected_size) {
        send_line("NAK %lu size\n", index);
        fail_transfer("size");
        return;
    }

    if ((uint32_t)index == 0U) {
        SEGGER_RTT_printf(0, "[RX] BLK0 len=%lu crc=%08lX\r\n", len, crc);
    }

    rx.block_len = (uint32_t)len;
    rx.block_received = 0;
    rx.block_expected_crc = (uint32_t)crc;
    rx.state = FILE_RX_STATE_RECV_PAYLOAD;
}

/**
 * @brief  根据当前状态分发一整行文本命令
 */
static void handle_line(char *line)
{
    if (rx.state == FILE_RX_STATE_IDLE) {
        handle_put_line(line);
    } else {
        handle_block_line(line);
    }
}

/**
 * @brief  处理当前 block 的一个 payload 字节
 *
 * payload 收满后执行块 CRC、SD 写入、整文件 CRC 累积和 ACK。
 */
static void handle_payload_byte(uint8_t byte)
{
    uint32_t block_crc;

    block_buf[rx.block_received] = byte;
    rx.block_received++;

    if (rx.block_received < rx.block_len) {
        return;
    }

    /* payload 收满后先校验块 CRC，只有校验通过才写 SD。 */
    block_crc = crc32_update(0xFFFFFFFFUL, block_buf, rx.block_len) ^ 0xFFFFFFFFUL;
    if (block_crc != rx.block_expected_crc) {
        send_line("NAK %u crc\n", rx.block_index);
        fail_transfer("block_crc");
        return;
    }

    if (rx.block_index == 0U) {
        SEGGER_RTT_WriteString(0, "[RX] BLK0 payload ok\r\n");
    }

    if (!SdManager_IsPresent()) {
        handle_sd_removed();
        return;
    }

    if (write_file_data(block_buf, rx.block_len) == 0U) {
        return;
    }

    if (rx.block_index == 0U) {
        SEGGER_RTT_WriteString(0, "[RX] BLK0 write ok\r\n");
    }

    rx.running_crc = crc32_update(rx.running_crc, block_buf, rx.block_len);
    rx.received_size += rx.block_len;

    /* ACK 代表这一块已经校验并写入 SD，PC 收到后才会发送下一块。 */
    send_line("ACK %u\n", rx.block_index);
    print_progress();

    rx.block_index++;
    rx.block_len = 0;
    rx.block_received = 0;
    rx.state = FILE_RX_STATE_WAIT_BLOCK;
}

/**
 * @brief  协议字节流解析入口
 *
 * 文本状态下按行解析 PUT/block/END；payload 状态下直接收二进制数据。
 */
static void handle_rx_byte(uint8_t byte)
{
    if (rx.state == FILE_RX_STATE_RECV_PAYLOAD) {
        handle_payload_byte(byte);
        return;
    }

    if (byte == '\r') {
        return;
    }

    if (byte == '\n') {
        line_buf[line_len] = '\0';
        if (line_len != 0U) {
            handle_line(line_buf);
        }
        line_len = 0;
        return;
    }

    if (line_len >= (FILE_RX_LINE_SIZE - 1U)) {
        fail_transfer("line");
        line_len = 0;
        return;
    }

    line_buf[line_len] = (char)byte;
    line_len++;
}

/**
 * @brief  初始化文件接收模块
 */
void FileRx_Init(void)
{
    file_rx_ring_head = 0;
    file_rx_ring_tail = 0;
    file_rx_ring_overflow = 0;
    reset_context();
}

/**
 * @brief  CDC OUT 回调入口
 *
 * 这里只做快速入环形缓冲，不在 USB 回调上下文里执行 FatFs 写入。
 */
void FileRx_OnUsbData(const uint8_t *data, uint32_t len)
{
    uint32_t i;

    if (data == NULL || len == 0U) {
        return;
    }

    for (i = 0; i < len; i++) {
        uint32_t next = ring_next(file_rx_ring_head);
        if (next == file_rx_ring_tail) {
            /* 环形缓冲满了，任务侧会统一 fail_transfer("overflow")。 */
            file_rx_ring_overflow = 1;
            break;
        }

        file_rx_ring[file_rx_ring_head] = data[i];
        file_rx_ring_head = next;
    }
}

/**
 * @brief  生成 LCD 第一行接收状态文本
 *
 * 接收中显示文件名和进度；完成后显示最后文件名 OK；空闲且无完成记录时显示 Idle。
 */
void FileRx_GetStatusText(char *buffer, uint32_t buffer_len)
{
    uint32_t received;
    uint32_t expected;
    uint32_t rx_unit;
    uint32_t total_unit;

    if (buffer == NULL || buffer_len == 0U) {
        return;
    }

    received = rx.received_size;
    expected = rx.expected_size;

    if ((rx.state == FILE_RX_STATE_IDLE && rx.file_open == 0U) || expected == 0U) {
        if (last_done_valid != 0U) {
            snprintf(buffer, buffer_len, "RX:%.19s OK", last_done_name);
        } else {
            snprintf(buffer, buffer_len, "RX:Idle");
        }
        finish_status_text(buffer, buffer_len);
        return;
    }

    if (expected >= FILE_RX_UNIT_MB) {
        rx_unit = (uint32_t)(((uint64_t)received * 100ULL) / FILE_RX_UNIT_MB);
        total_unit = (uint32_t)(((uint64_t)expected * 100ULL) / FILE_RX_UNIT_MB);
        snprintf(buffer, buffer_len, "RX:%.10s %u.%02u/%u.%02uMB",
                 rx.name,
                 rx_unit / 100U,
                 rx_unit % 100U,
                 total_unit / 100U,
                 total_unit % 100U);
    } else {
        rx_unit = (uint32_t)(((uint64_t)received * 10ULL) / FILE_RX_UNIT_KB);
        total_unit = (uint32_t)(((uint64_t)expected * 10ULL) / FILE_RX_UNIT_KB);
        snprintf(buffer, buffer_len, "RX:%.10s %u.%u/%u.%uKB",
                 rx.name,
                 rx_unit / 10U,
                 rx_unit % 10U,
                 total_unit / 10U,
                 total_unit % 10U);
    }

    finish_status_text(buffer, buffer_len);
}

/**
 * @brief  文件接收 FreeRTOS 任务
 *
 * 持续从环形缓冲区取数据，分批送入协议解析器；检测到溢出时终止当前传输。
 */
void FileRx_Task(void *argument)
{
    uint8_t rx_chunk[512];

    (void)argument;

    for (;;) {
        while (file_rx_ring_tail != file_rx_ring_head) {
            uint32_t n = 0;
            uint32_t i;

            while ((file_rx_ring_tail != file_rx_ring_head) && (n < sizeof(rx_chunk))) {
                rx_chunk[n] = file_rx_ring[file_rx_ring_tail];
                file_rx_ring_tail = ring_next(file_rx_ring_tail);
                n++;
            }

            debug_dump_rx_bytes(rx_chunk, n);

            for (i = 0; i < n; i++) {
                handle_rx_byte(rx_chunk[i]);
            }
        }

        if (file_rx_ring_overflow != 0U) {
            file_rx_ring_overflow = 0;
            file_rx_ring_tail = file_rx_ring_head;
            fail_transfer("overflow");
        }

        osDelay(1);
    }
}
