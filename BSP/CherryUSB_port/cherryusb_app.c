/**
 * @file    cherryusb_app.c
 * @brief   CherryUSB CDC ACM 设备应用 — STM32H743 实现
 *
 * 本文件实现了基于 CherryUSB 栈的 USB CDC ACM（虚拟串口）设备：
 *   - 定义 USB 1.1 全速设备描述符、配置描述符（含 CDC 接口）、
 *     字符串描述符和设备限定符描述符
 *   - 注册描述符回调与 USB 事件回调（复位/断开/配置完成）
 *   - 实现 CDC 数据收发：OUT 端点接收并转发到应用层，
 *     IN 端点发送应用层下发的数据
 *
 * @note    使用 OTG_FS 外设，全速（FS）模式，单端点 CDC 配置。
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "stm32h7xx_hal.h"
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "cherryusb_app.h"
#include "file_rx.h"
#include "SEGGER_RTT.h"
/* ======================== 总线与端点宏定义 ======================== */

#define CHERRYUSB_BUSID 0     /* CherryUSB 总线 ID（单总线） */

/* CDC 端点地址 */
#define CDC_IN_EP  0x81       /* IN 端点（设备→主机），地址 1，方向 IN  */
#define CDC_OUT_EP 0x02       /* OUT 端点（主机→设备），地址 2，方向 OUT */
#define CDC_INT_EP 0x83       /* 中断端点（CDC 串行状态通知），地址 3，方向 IN */

/* ======================== USB 标准描述符常量 ======================== */

#define USBD_VID           0x0483   /* STMicroelectronics 厂商 ID */
#define USBD_PID           0x5740   /* 产品 ID */
#define USBD_MAX_POWER     100      /* 最大功耗（单位：2mA），此处为 200mA */
#define USBD_LANGID_STRING 1033     /* 英语-美国语言 ID */

/* CDC 端点参数 */
#define CDC_MAX_MPS        64       /* CDC 端点最大包大小（全速 64 字节） */
#define CDC_RX_BUFFER_SIZE 512      /* CDC 接收缓冲区大小 */
#define CDC_TX_BUFFER_SIZE 512      /* CDC 发送缓冲区大小 */
#define USB_CONFIG_SIZE    (9 + CDC_ACM_DESCRIPTOR_LEN)  /* 配置描述符总长 */

/**
 * @brief  设备描述符
 *
 * - USB 版本: 2.0
 * - 设备类: 0xEF（混合设备，通过接口描述符定义功能）
 * - 子类: 0x02（CDC 控制类）
 * - 协议: 0x01（IAD — 接口关联描述符）
 * - VID/PID: 0x0483 / 0x5740
 * - 版本: 0x0100
 * - 制造商字符串索引: 1
 */
static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01)
};

/**
 * @brief  配置描述符（含 CDC ACM 接口描述符）
 *
 * 包含 1 个配置描述符头 + CDC 通信接口 + CDC 数据接口：
 *   - CDC 通信接口: 控制端点 + 中断端点（串行状态通知）
 *   - CDC 数据接口: 批量 IN / 批量 OUT 端点
 *   - 总线供电，最大电流 200mA
 */
static const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, CDC_MAX_MPS, 0x02)
};

/**
 * @brief  设备限定符描述符
 *
 * 当设备同时支持全速和高速时，主机通过此描述符判断另一速率下的配置能力。
 * 此处仅作占位（实际仅全速模式），返回全速参数。
 *
 * 字节含义: 长度(10) / 类型(DEVICE_QUALIFIER) / USB版本 / 类/子类/协议 /
 *          最大包大小(64) / 配置数(0) / 保留(0)
 */
static const uint8_t device_quality_descriptor[] = {
    0x0a,
    USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00,
    0x02,
    0x00,
    0x00,
    0x00,
    0x40,
    0x00,
    0x00,
};

/**
 * @brief  字符串描述符表
 *
 * 索引 0: 语言 ID（0x0409 = 英语-美国）
 * 索引 1: 制造商字符串
 * 索引 2: 产品字符串
 * 索引 3: 序列号字符串
 */
static const char *string_descriptors[] = {
    (const char[]){ 0x09, 0x04 },           /* 语言 ID: 英语-美国 */
    "STMicroelectronics",                    /* iManufacturer */
    "h7rec CherryUSB CDC",                  /* iProduct */
    "H743-CHERRYUSB",                        /* iSerialNumber */
};

/* ====================== CDC 数据缓冲区 ====================== */

/* 接收缓冲区 — 放置在非 Cache 内存段（避免 DMA 与 Cache 一致性问题） */
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t cdc_rx_buffer[CDC_RX_BUFFER_SIZE];
/* 发送缓冲区 — 同上 */
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t cdc_tx_buffer[CDC_TX_BUFFER_SIZE];

/* ====================== 状态标志 ====================== */

static volatile bool cdc_configured;  /* USB 主机已完成配置（Set Configuration） */
static volatile bool cdc_tx_busy;     /* CDC IN 发送正在进行中 */
static volatile bool usb_device_ready; /* USB 设备栈已完成初始化 */

/* ====================== CDC 接口描述符 ====================== */

static struct usbd_interface cdc_cmd_intf;   /* CDC 通信接口（控制/管理） */
static struct usbd_interface cdc_data_intf;  /* CDC 数据接口（批量传输） */

/* ====================== 描述符回调函数 ====================== */

/**
 * @brief  设备描述符回调
 * @param  speed  当前 USB 速率（全速/高速，此处忽略）
 * @return 指向 device_descriptor 的指针
 */
static const uint8_t *device_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return device_descriptor;
}

/**
 * @brief  配置描述符回调
 * @param  speed  当前 USB 速率（忽略）
 * @return 指向 config_descriptor 的指针
 */
static const uint8_t *config_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return config_descriptor;
}

/**
 * @brief  设备限定符描述符回调
 * @param  speed  当前 USB 速率（忽略）
 * @return 指向 device_quality_descriptor 的指针
 */
static const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return device_quality_descriptor;
}

/**
 * @brief  字符串描述符回调
 * @param  speed  当前 USB 速率（忽略）
 * @param  index  字符串索引号
 * @return 字符串指针；索引超出范围时返回 NULL
 */
static const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
    (void)speed;

    if (index >= (sizeof(string_descriptors) / sizeof(string_descriptors[0]))) {
        return NULL;
    }

    return string_descriptors[index];
}

/**
 * @brief  USB 描述符集合结构体
 *
 * 将四个描述符回调注册到 CherryUSB 栈，
 * 启用 CONFIG_USBDEV_ADVANCE_DESC 宏后使用此结构注册。
 */
static const struct usb_descriptor cdc_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback,
};

/* ====================== USB 事件回调 ====================== */

/**
 * @brief  USB 设备事件回调
 *
 * 处理 CherryUSB 栈上报的设备级事件：
 *   - USBD_EVENT_RESET / USBD_EVENT_DISCONNECTED: 复位或断开连接
 *     清除配置状态，停止发送
 *   - USBD_EVENT_CONFIGURED: 主机已完成配置枚举
 *     标记配置就绪，启动 CDC OUT 端点接收主机数据
 *
 * @param  busid  总线 ID
 * @param  event  事件类型
 */
static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    switch (event) {
        case USBD_EVENT_RESET:
        case USBD_EVENT_DISCONNECTED:
            cdc_configured = false;   /* 清除配置状态 */
            cdc_tx_busy = false;      /* 重置发送忙标志 */
            break;

        case USBD_EVENT_CONFIGURED:
            cdc_configured = true;    /* 标记配置完成 */
            cdc_tx_busy = false;      /* 重置发送忙标志 */
            /* 启动 CDC OUT 端点接收，等待主机下发数据 */
            usbd_ep_start_read(busid, CDC_OUT_EP, cdc_rx_buffer, sizeof(cdc_rx_buffer));
            break;

        default:
            break;
    }
}

/* ====================== CDC 端点回调 ====================== */

/**
 * @brief  CDC 批量 OUT 端点完成回调（主机→设备）
 *
 * 当主机通过 CDC OUT 端点下发数据时触发：
 * 1. 若接收到有效数据，调用 FileRx_OnUsbData() 转发到文件传输模块
 * 2. 重新启动 OUT 端点接收，准备接收下一包数据
 *
 * @param  busid   总线 ID
 * @param  ep      端点地址
 * @param  nbytes  本次接收的字节数
 */
static void cdc_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)ep;

    if (nbytes != 0U) {
        /* 将接收到的数据转发给文件传输模块 */
        FileRx_OnUsbData(cdc_rx_buffer, nbytes);
    }
    // SEGGER_RTT_printf(0, "[CDC OUT] Received %u bytes\r\n", nbytes);
    /* rtt输出 接收到的数据 */
    // SEGGER_RTT_WriteString(0, "[CDC OUT] Data: ");
    // for (uint32_t i = 0; i < nbytes; i++) {
    //     SEGGER_RTT_printf(0, "%02X ", cdc_rx_buffer[i]);
    // }
    // SEGGER_RTT_WriteString(0, "\r\n");
    /* 重新启动 OUT 接收，准备下一包数据 */
    usbd_ep_start_read(busid, CDC_OUT_EP, cdc_rx_buffer, sizeof(cdc_rx_buffer));
}

/**
 * @brief  CDC 批量 IN 端点完成回调（设备→主机）
 *
 * 当 CDC IN 端点数据已成功发送到主机时触发：
 * - 如果发送的字节数恰好填满一个 MPS（最大包长），
 *   主机可能等待更多数据（短包终止），因此发送一个 ZLP（零长度包）以刷新
 * - 否则标记发送已完成，允许应用层发起下一次发送
 *
 * @param  busid   总线 ID
 * @param  ep      端点地址
 * @param  nbytes  本次发送的字节数
 */
static void cdc_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;

    if ((nbytes != 0U) && ((nbytes % usbd_get_ep_mps(CHERRYUSB_BUSID, ep)) == 0U)) {
        /* 数据长度是 MPS 的整数倍，发送 ZLP 表示传输结束 */
        usbd_ep_start_write(CHERRYUSB_BUSID, CDC_IN_EP, NULL, 0);
        return;
    }

    /* 发送完成，清除忙标志 */
    cdc_tx_busy = false;
}

/* ====================== CDC 端点注册结构体 ====================== */

/* CDC OUT 端点：主机到设备数据接收 */
static struct usbd_endpoint cdc_out_ep = {
    .ep_addr = CDC_OUT_EP,
    .ep_cb = cdc_bulk_out,
};

/* CDC IN 端点：设备到主机数据发送 */
static struct usbd_endpoint cdc_in_ep = {
    .ep_addr = CDC_IN_EP,
    .ep_cb = cdc_bulk_in,
};

/* ====================== 公共 API 实现 ====================== */

/**
 * @brief  初始化 USB CDC ACM 设备
 *
 * 执行步骤：
 * 1. 清除所有状态标志
 * 2. 注册 USB 描述符到 CherryUSB 栈
 * 3. 添加 CDC 命令接口和数据接口
 * 4. 注册 CDC IN/OUT 端点及回调
 * 5. 调用 usbd_initialize 启动 USB 设备栈
 * 6. 使能 OTG_FS 中断（IRQn）
 *
 * @note  必须在 USB 外设时钟和 GPIO 初始化（HAL_PCD_MspInit）之后调用。
 *        CherryUSB 栈接管中断处理，需要在中断服务程序中调用 usbd_irq_handler()。
 */
void CherryUSB_DeviceInit(void)
{
    /* 清除所有状态标志 */
    cdc_configured = false;
    cdc_tx_busy = false;
    usb_device_ready = false;

    /* 注册描述符（启用 CONFIG_USBDEV_ADVANCE_DESC 后使用此方式） */
    usbd_desc_register(CHERRYUSB_BUSID, &cdc_descriptor);

    /* 添加 CDC 接口 */
    usbd_add_interface(CHERRYUSB_BUSID, usbd_cdc_acm_init_intf(CHERRYUSB_BUSID, &cdc_cmd_intf));
    usbd_add_interface(CHERRYUSB_BUSID, usbd_cdc_acm_init_intf(CHERRYUSB_BUSID, &cdc_data_intf));

    /* 注册端点 */
    usbd_add_endpoint(CHERRYUSB_BUSID, &cdc_out_ep);
    usbd_add_endpoint(CHERRYUSB_BUSID, &cdc_in_ep);

    /* 启动 CherryUSB 设备栈 */
    usbd_initialize(CHERRYUSB_BUSID, (uintptr_t)USB_OTG_FS, usbd_event_handler);

    /* 标记设备就绪，使能 OTG_FS 中断 */
    usb_device_ready = true;
    HAL_NVIC_ClearPendingIRQ(OTG_FS_IRQn);
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
}

/**
 * @brief  查询 USB 设备是否已完成初始化
 * @return true — 设备已就绪；false — 尚未初始化
 */
bool CherryUSB_DeviceIsReady(void)
{
    return usb_device_ready;
}

bool CherryUSB_CdcCanSend(void)
{
    return cdc_configured && !cdc_tx_busy;
}

/**
 * @brief  通过 CDC IN 端点发送数据到主机
 *
 * 将数据从应用缓冲区拷贝到 CDC 发送缓冲区中，
 * 然后启动 USB 批量 IN 传输。
 *
 * @note   若设备未配置、上一次发送尚未完成、或参数无效则静默返回。
 *         发送完成后通过 cdc_bulk_in 回调清除 busy 标志。
 *
 * @param  data  待发送数据的缓冲区指针
 * @param  len   待发送数据的长度（超过 CDC_TX_BUFFER_SIZE 会被截断）
 */
void CherryUSB_CdcSend(const uint8_t *data, uint32_t len)
{
    if (!cdc_configured || cdc_tx_busy || data == NULL || len == 0U) {
        return;
    }

    if (len > sizeof(cdc_tx_buffer)) {
        len = sizeof(cdc_tx_buffer);
    }

    memcpy(cdc_tx_buffer, data, len);
    cdc_tx_busy = true;
    usbd_ep_start_write(CHERRYUSB_BUSID, CDC_IN_EP, cdc_tx_buffer, len);
}
