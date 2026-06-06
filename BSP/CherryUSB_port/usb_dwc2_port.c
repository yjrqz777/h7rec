/**
 * @file    usb_dwc2_port.c
 * @brief   CherryUSB DWC2 控制器移植层 — STM32H743 OTG_FS 适配实现
 *
 * 本文件实现了 CherryUSB 设备栈所需的 DWC2 控制器底层接口：
 *   - USB 控制器管脚与时钟的初始化/反初始化（通过 HAL 库 PCD MSP 回调）
 *   - DWC2 FIFO 大小与传输模式参数的配置
 *   - 基于 SysTick 的毫秒级延时与系统时钟查询
 *
 * @note    对应 STM32H7xx 系列 USB_OTG_FS 外设，全速（FS）模式。
 */

#include <string.h>

#include "stm32h7xx_hal.h"
#include "usbd_core.h"
#include "usb_dwc2_param.h"

extern uint32_t SystemCoreClock;

/* HAL PCD 句柄，用于 OTG_FS 的 MSP 初始化/反初始化 */
static PCD_HandleTypeDef hpcd_cherryusb_fs;

/**
 * @brief  H743 OTG_FS 的 DWC2 用户参数配置
 *
 * 各 FIFO 大小分配（单位：32-bit 字）：
 *   - device_rx_fifo_size: RX FIFO = 总 FIFO 320 - EP0 TX 16 - EP1 TX 64 - EP2 TX 16 - EP3 TX 16 = 208
 *   - device_tx_fifo_size[0..3]: 各端点的 TX FIFO
 *   - host_*_fifo_size: Host 模式下的 FIFO（当前未使用，仅作占位）
 *   - device_gccfg / host_gccfg: 位[16] = USB2_LPM_L1 检测使能
 *   - b_session_valid_override: 覆盖 B 会话检测，适用于自供电设备
 */
static const struct dwc2_user_params h743_otg_fs_params = {
    .phy_type = DWC2_PHY_TYPE_PARAM_FS,          /* 全速 PHY（无需 ULPI 外部 PHY） */
    .phy_utmi_width = 8,                         /* UTMI+ 数据宽度（仅作占位，FS 模式不涉及） */
    .device_dma_enable = false,                  /* 不使用 DMA 模式 */
    .device_dma_desc_enable = false,             /* 不使用 DMA 描述符链 */
    .device_rx_fifo_size = (320 - 16 - 64 - 16 - 16),  /* RX FIFO = 208 字 */
    .device_tx_fifo_size = {
        [0] = 16,   /* EP0 （控制端点）TX FIFO */
        [1] = 64,   /* EP1 （CDC 数据输入端点）TX FIFO */
        [2] = 16,   /* EP2 （CDC 命令端点/预留）TX FIFO */
        [3] = 16,   /* EP3 （预留）TX FIFO */
    },
    .host_dma_desc_enable = false,               /* Host 模式不使用 DMA 描述符 */
    .host_rx_fifo_size = 176,                    /* Host RX FIFO（当前未使用） */
    .host_nperio_tx_fifo_size = 64,              /* Host 非周期 TX FIFO（当前未使用） */
    .host_perio_tx_fifo_size = 64,               /* Host 周期 TX FIFO（当前未使用） */
    .device_gccfg = (1 << 16),                   /* 设备模式 GCCFG：使能 USB2_LPM_L1 检测 */
    .host_gccfg = (1 << 16),                     /* 主机模式 GCCFG：使能 USB2_LPM_L1 检测 */
    .b_session_valid_override = true,            /* 覆盖 B 会话有效检测 */
    .total_fifo_size = 320,                      /* OTG_FS 总 FIFO 深度（320 个 32-bit 字） */
};

/**
 * @brief  USB 设备控制器低层初始化
 *
 * 调用 HAL 库的 MSP 初始化回调来配置 OTG_FS 外设的 GPIO 时钟和管脚，
 * 然后关闭并清除 OTG_FS 中断，交由 CherryUSB 栈管理中断处理。
 *
 * @param  busid  USB 总线 ID（此处未使用，仅支持单总线）
 */
void usb_dc_low_level_init(uint8_t busid)
{
    (void)busid;

    hpcd_cherryusb_fs.Instance = USB_OTG_FS;
    HAL_PCD_MspInit(&hpcd_cherryusb_fs);
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    HAL_NVIC_ClearPendingIRQ(OTG_FS_IRQn);
}

/**
 * @brief  USB 设备控制器低层反初始化
 *
 * 调用 HAL 库的 MSP 反初始化回调，释放 OTG_FS 外设的 GPIO 和时钟资源。
 *
 * @param  busid  USB 总线 ID（此处未使用）
 */
void usb_dc_low_level_deinit(uint8_t busid)
{
    (void)busid;

    hpcd_cherryusb_fs.Instance = USB_OTG_FS;
    HAL_PCD_MspDeInit(&hpcd_cherryusb_fs);
}

/**
 * @brief  获取 DWC2 控制器用户参数
 *
 * CherryUSB 栈在初始化 DWC2 控制器时调用此函数，获取 FIFO 大小、
 * DMA 使能、PHY 类型等硬件参数。
 *
 * @param  reg_base  控制器寄存器基地址（此处未使用，仅单实例）
 * @param  params    输出参数结构体，通过 memcpy 填充预配置的静态参数
 */
void dwc2_get_user_params(uint32_t reg_base, struct dwc2_user_params *params)
{
    (void)reg_base;

    memcpy(params, &h743_otg_fs_params, sizeof(struct dwc2_user_params));
}

/**
 * @brief  USB 设备栈 DWC2 毫秒级延时
 *
 * 使用 CPU 循环等待实现阻塞延时。
 * 计数 = (系统时钟频率 / 1000) × 毫秒数，约等价于 1ms 精度的 busy-wait。
 *
 * @param  ms  需要延时的毫秒数
 */
void usbd_dwc2_delay_ms(uint8_t ms)
{
    uint32_t count = (SystemCoreClock / 1000U) * ms;

    while (count--) {
        __NOP();
    }
}

/**
 * @brief  获取系统时钟频率
 *
 * CherryUSB 栈通过此函数获取当前系统时钟，用于计算 USB 时序参数。
 *
 * @return  SystemCoreClock 变量值，单位 Hz
 */
uint32_t usbd_dwc2_get_system_clock(void)
{
    return SystemCoreClock;
}
