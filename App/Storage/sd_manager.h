/**
 * @file    sd_manager.h
 * @brief   SD 卡挂载管理模块
 *
 * 本模块负责集中管理 SDMMC/FatFs 的初始化、挂载、卸载和容量查询。
 * 上层任务只通过这里查询 SD 状态，避免多个任务同时直接操作底层驱动。
 */
#ifndef SD_MANAGER_H
#define SD_MANAGER_H

#include <stdint.h>

/* ======================== SD 管理配置宏 ======================== */

#define SD_MANAGER_STARTUP_DELAY_MS 1000U  /* 系统启动后延迟多久再开始第一次 SD 挂载 */
#define SD_MANAGER_INIT_SETTLE_MS    50U   /* 复位 SDMMC 驱动后等待多久再调用 BSP_SD_Init */
#define SD_MANAGER_RETRY_MS         2000U  /* 挂载失败后的后台重试周期 */

/**
 * @brief SD 挂载操作返回值
 */
typedef enum {
    SD_MANAGER_OK = 0,
    SD_MANAGER_ERR_ABSENT,  /* 当前工程未使用有效插卡检测时，保留该错误码给上层兼容 */
    SD_MANAGER_ERR_INIT,    /* BSP_SD_Init 或底层 SDMMC 初始化失败 */
    SD_MANAGER_ERR_MOUNT    /* FatFs f_mount 失败 */
} SdManagerStatus;

/**
 * @brief SD 管理器状态，用于 LCD/RTT 显示
 */
typedef enum {
    SD_MANAGER_STATE_ABSENT = 0,
    SD_MANAGER_STATE_INITING,
    SD_MANAGER_STATE_MOUNTED,
    SD_MANAGER_STATE_ERR_INIT,
    SD_MANAGER_STATE_ERR_MOUNT
} SdManagerState;

/**
 * @brief 初始化 SD 管理器内部状态和互斥锁
 */
void SdManager_Init(void);

/**
 * @brief SD 管理后台任务
 *
 * 未挂载时按固定周期尝试挂载；挂载失败不会阻塞其他任务。
 */
void SdManager_Task(void *argument);

/**
 * @brief 尝试初始化并挂载 SD 卡
 */
SdManagerStatus SdManager_Mount(void);

/**
 * @brief 卸载 SD 卡并复位 SDMMC 驱动状态
 */
void SdManager_Unmount(void);

/**
 * @brief 返回 SD 是否可用
 *
 * 由于当前硬件检测脚无效，这里等价于“是否已经成功挂载”。
 */
uint8_t SdManager_IsPresent(void);

/**
 * @brief 返回 SD 是否已经挂载
 */
uint8_t SdManager_IsMounted(void);

/**
 * @brief 获取当前 SD 管理器状态
 */
SdManagerState SdManager_GetState(void);

/**
 * @brief 获取当前状态对应的短文本，用于 LCD 显示
 */
const char *SdManager_GetStateText(void);

/**
 * @brief 获取 SD 容量信息
 *
 * @param total_kb  输出总容量 KB，可为 NULL
 * @param free_kb   输出剩余容量 KB，可为 NULL
 * @return 1 容量有效；0 容量无效或尚未挂载
 */
uint8_t SdManager_GetCapacity(uint32_t *total_kb, uint32_t *free_kb);

#endif /* SD_MANAGER_H */
