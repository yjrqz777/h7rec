/**
 * @file    cherryusb_app.h
 * @brief   CherryUSB CDC ACM 设备应用 — 头文件
 *
 * 基于 CherryUSB 栈在 STM32H743 上实现 USB CDC ACM（虚拟串口）设备。
 * 提供设备初始化、就绪状态查询以及 CDC 数据发送接口。
 *
 * @note    对应 USB 描述符使用全速（FS）模式，端点配置见 cherryusb_app.c。
 */
#ifndef CHERRYUSB_APP_H
#define CHERRYUSB_APP_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  初始化 USB 设备（CDC ACM 类）
 *
 * 注册 USB 描述符、CDC 接口、端点回调，使能 OTG_FS 中断。
 * 必须在 USB 外设时钟使能之后调用。
 */
void CherryUSB_DeviceInit(void);

/**
 * @brief  查询 USB 设备是否已完成初始化
 * @return true  — 设备已就绪，可进行 CDC 收发
 *         false — 设备尚未初始化完成
 */
bool CherryUSB_DeviceIsReady(void);

/**
 * @brief  通过 CDC IN 端点发送数据
 *
 * 将数据拷贝到发送缓冲区后启动 USB 传输。
 * 如果设备未配置、上次发送未完成或参数无效，则静默丢弃。
 *
 * @param  data  待发送数据的缓冲区指针
 * @param  len   待发送数据的字节数（最大 CDC_TX_BUFFER_SIZE）
 */
void CherryUSB_CdcSend(const uint8_t *data, uint32_t len);

#endif /* CHERRYUSB_APP_H */
