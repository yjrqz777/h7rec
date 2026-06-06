#include "file_rx.h"

#include "FreeRTOS.h"
#include "cmsis_os.h"

#include "SEGGER_RTT.h"
#include "cherryusb_app.h"
#include "fatfs.h"
#include "sd_manager.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define FILE_RX_RING_SIZE         4096U
#define FILE_RX_CHUNK_SIZE        512U
#define FILE_RX_LINE_SIZE         160U
#define FILE_RX_NAME_SIZE         64U
#define FILE_RX_PATH_SIZE         128U
#define FILE_RX_DIR               "0:/rx"
#define FILE_RX_TEMP_PATH         "0:/rx/RX.TMP"
#define FILE_RX_DEBUG_DUMP        0
#define FILE_RX_DEBUG_DUMP_MAX    64U

typedef enum {
    FILE_RX_STATE_IDLE = 0,
    FILE_RX_STATE_WAIT_BLOCK,
    FILE_RX_STATE_RECV_PAYLOAD
} FileRxState;

typedef struct {
    FileRxState state;
    FIL file;
    uint8_t file_open;
    uint32_t expected_size;
    uint32_t received_size;
    uint32_t expected_crc;
    uint32_t running_crc;
    uint32_t block_index;
    uint32_t block_len;
    uint32_t block_received;
    uint32_t block_expected_crc;
    char name[FILE_RX_NAME_SIZE];
    char final_path[FILE_RX_PATH_SIZE];
    char temp_path[FILE_RX_PATH_SIZE];
} FileRxContext;

static uint8_t file_rx_ring[FILE_RX_RING_SIZE];
static volatile uint32_t file_rx_ring_head;
static volatile uint32_t file_rx_ring_tail;
static volatile uint8_t file_rx_ring_overflow;

static FileRxContext rx;
static char line_buf[FILE_RX_LINE_SIZE];
static uint32_t line_len;
ALIGN_32BYTES(static uint8_t block_buf[FILE_RX_CHUNK_SIZE]);

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

static uint32_t ring_next(uint32_t index)
{
    index++;
    if (index >= FILE_RX_RING_SIZE) {
        index = 0;
    }
    return index;
}

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

static void print_progress(void)
{
    uint32_t pct10;

    if (rx.expected_size == 0U) {
        pct10 = 1000U;
    } else {
        pct10 = (uint32_t)(((uint64_t)rx.received_size * 1000ULL) / rx.expected_size);
        if (pct10 > 1000U) {
            pct10 = 1000U;
        }
    }

    SEGGER_RTT_printf(0, "[RX] %u/%u bytes (%u.%u%%)\r\n",
                      rx.received_size,
                      rx.expected_size,
                      pct10 / 10U,
                      pct10 % 10U);
}

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
    }
}

static void close_and_delete_temp(void)
{
    if (rx.file_open != 0U) {
        f_close(&rx.file);
        rx.file_open = 0;
    }

    if (rx.temp_path[0] != '\0') {
        (void)f_unlink(rx.temp_path);
    }
}

static void reset_context(void)
{
    memset(&rx, 0, sizeof(rx));
    rx.state = FILE_RX_STATE_IDLE;
    line_len = 0;
}

static void handle_sd_removed(void)
{
    SEGGER_RTT_WriteString(0, "[SD] removed\r\n");

    if (rx.state != FILE_RX_STATE_IDLE || rx.file_open != 0U) {
        rx.file_open = 0;
        send_line("ERR sd_removed\n");
        reset_context();
    }
}

static void fail_transfer(const char *reason)
{
    SEGGER_RTT_printf(0, "[RX] ERR %s\r\n", reason);
    close_and_delete_temp();
    send_line("ERR %s\n", reason);
    reset_context();
}

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

static uint8_t prepare_file(const char *name, uint32_t size, uint32_t crc)
{
    FRESULT res;

    if (!filename_is_safe(name)) {
        send_line("ERR filename\n");
        return 0;
    }

    if (!sd_mount_if_ready()) {
        return 0;
    }

    memset(&rx, 0, sizeof(rx));
    rx.state = FILE_RX_STATE_WAIT_BLOCK;
    rx.expected_size = size;
    rx.expected_crc = crc;
    rx.running_crc = 0xFFFFFFFFUL;
    strncpy(rx.name, name, sizeof(rx.name) - 1U);
    snprintf(rx.final_path, sizeof(rx.final_path), "%s/%s", FILE_RX_DIR, rx.name);
    strncpy(rx.temp_path, FILE_RX_TEMP_PATH, sizeof(rx.temp_path) - 1U);

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
    return 1;
}

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
    reset_context();
}

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

    rx.block_len = (uint32_t)len;
    rx.block_received = 0;
    rx.block_expected_crc = (uint32_t)crc;
    rx.state = FILE_RX_STATE_RECV_PAYLOAD;
}

static void handle_line(char *line)
{
    if (rx.state == FILE_RX_STATE_IDLE) {
        handle_put_line(line);
    } else {
        handle_block_line(line);
    }
}

static void handle_payload_byte(uint8_t byte)
{
    UINT bw;
    FRESULT res;
    uint32_t block_crc;

    block_buf[rx.block_received] = byte;
    rx.block_received++;

    if (rx.block_received < rx.block_len) {
        return;
    }

    block_crc = crc32_update(0xFFFFFFFFUL, block_buf, rx.block_len) ^ 0xFFFFFFFFUL;
    if (block_crc != rx.block_expected_crc) {
        send_line("NAK %u crc\n", rx.block_index);
        fail_transfer("block_crc");
        return;
    }

    if (!SdManager_IsPresent()) {
        handle_sd_removed();
        return;
    }

    res = f_write(&rx.file, block_buf, rx.block_len, &bw);
    if (res != FR_OK || bw != rx.block_len) {
        SEGGER_RTT_printf(0, "[RX] write failed res=%d bw=%u len=%u\r\n",
                          res, bw, rx.block_len);
        fail_transfer("write");
        return;
    }

    rx.running_crc = crc32_update(rx.running_crc, block_buf, rx.block_len);
    rx.received_size += rx.block_len;

    if (rx.received_size == rx.expected_size) {
        if (!SdManager_IsPresent()) {
            handle_sd_removed();
            return;
        }

        res = f_sync(&rx.file);
        if (res != FR_OK) {
            SEGGER_RTT_printf(0, "[RX] sync failed res=%d\r\n", res);
            fail_transfer("sync");
            return;
        }
    }

    send_line("ACK %u\n", rx.block_index);
    print_progress();

    rx.block_index++;
    rx.block_len = 0;
    rx.block_received = 0;
    rx.state = FILE_RX_STATE_WAIT_BLOCK;
}

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

void FileRx_Init(void)
{
    file_rx_ring_head = 0;
    file_rx_ring_tail = 0;
    file_rx_ring_overflow = 0;
    reset_context();
}

void FileRx_OnUsbData(const uint8_t *data, uint32_t len)
{
    uint32_t i;

    if (data == NULL || len == 0U) {
        return;
    }

    for (i = 0; i < len; i++) {
        uint32_t next = ring_next(file_rx_ring_head);
        if (next == file_rx_ring_tail) {
            file_rx_ring_overflow = 1;
            break;
        }

        file_rx_ring[file_rx_ring_head] = data[i];
        file_rx_ring_head = next;
    }
}

void FileRx_Task(void *argument)
{
    uint8_t rx_chunk[128];

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

        if (SdManager_TakeRemovedEvent()) {
            handle_sd_removed();
        }
        osDelay(1);
    }
}
