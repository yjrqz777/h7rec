/**
 * @file    usb_dwc2_port.h
 * @brief   CherryUSB DWC2 控制器移植层 — 头文件
 *
 * 提供 STM32H743 OTG_FS 外设的低级初始化/反初始化函数声明，
 * 以及 DWC2 控制器参数获取和延时函数的声明。
 *
 * @note    该文件为 CherryUSB 设备栈与 STM32 HAL 库之间的适配层接口。
 */
#ifndef USB_DWC2_PORT_H
#define USB_DWC2_PORT_H

#include <stdint.h>

/**
 * @brief  USB DVC 控制器低层初始化
 * @param  busid  USB 总线 ID
 */
void usb_dc_low_level_init(uint8_t busid);

/**
 * @brief  USB DVC 控制器低层反初始化
 * @param  busid  USB 总线 ID
 */
void usb_dc_low_level_deinit(uint8_t busid);

/**
 * @brief  获取 DWC2 控制器参数
 * @param  reg_base  控制器寄存器基地址
 * @param  params    输出参数结构体指针
 */
void dwc2_get_user_params(uint32_t reg_base, struct dwc2_user_params *params);

/**
 * @brief  USB 设备栈 DWC2 毫秒级延时
 * @param  ms  延时毫秒数
 */
void usbd_dwc2_delay_ms(uint8_t ms);

/**
 * @brief  获取系统时钟频率
 * @return SystemCoreClock 值（单位: Hz）
 */
uint32_t usbd_dwc2_get_system_clock(void);

#endif /* USB_DWC2_PORT_H */