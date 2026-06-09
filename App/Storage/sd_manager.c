#include "sd_manager.h"

#include "SEGGER_RTT.h"
#include "bsp_driver_sd.h"
#include "cmsis_os.h"
#include "fatfs.h"
#include "sdmmc.h"

/*
 * SD 管理策略：
 * 1. 不再依赖插卡检测引脚，挂载成功才认为 SD 可用。
 * 2. 初始化/挂载失败后只记录状态并定时重试，不让任务卡死。
 * 3. 每次失败后复位 SDMMC/FatFs 状态，避免下次重试继承旧错误状态。
 */
static uint8_t sd_mounted;
static SdManagerState sd_state;
static uint32_t sd_next_mount_tick;
static uint32_t sd_next_capacity_tick;
static uint32_t sd_total_kb;
static uint32_t sd_free_kb;
static uint8_t sd_capacity_valid;
static uint8_t sd_capacity_update_pending;
static uint8_t sd_driver_reset_pending;
static osMutexId_t sd_manager_lock;
static const osMutexAttr_t sd_manager_lock_attr = {
    .name = "sdManagerLock",
    .attr_bits = osMutexPrioInherit
};

/**
 * @brief  判断系统 tick 是否已经到达指定时间点
 *
 * 使用有符号差值比较，避免 osKernel tick 回绕时判断错误。
 */
static uint8_t sd_tick_due(uint32_t now, uint32_t due)
{
    /* 使用有符号差值判断 tick 是否到期，可兼容 osKernel tick 回绕。 */
    return ((int32_t)(now - due) >= 0) ? 1U : 0U;
}

/**
 * @brief  获取 SD 管理互斥锁
 *
 * FatFs/SDMMC 状态由本模块集中维护，挂载、卸载和容量更新需要串行执行。
 */
static void sd_lock(void)
{
    if (sd_manager_lock != NULL) {
        (void)osMutexAcquire(sd_manager_lock, osWaitForever);
    }
}

/**
 * @brief  释放 SD 管理互斥锁
 */
static void sd_unlock(void)
{
    if (sd_manager_lock != NULL) {
        (void)osMutexRelease(sd_manager_lock);
    }
}

/**
 * @brief  清空缓存的容量信息
 *
 * SD 未挂载或容量查询失败时调用，避免 LCD 显示旧容量。
 */
static void sd_clear_capacity(void)
{
    sd_total_kb = 0;
    sd_free_kb = 0;
    sd_capacity_valid = 0;
}

#if SD_MANAGER_DUMP_RX_DIR_ON_MOUNT
static void sd_dump_root_dir(void)
{
    FRESULT res;
    DIR dir;
    FILINFO file_info;
    uint32_t entry_count = 0;
    TCHAR const *RxPath = "0:/RX";

    res = f_opendir(&dir, (TCHAR const *)RxPath);
    if (res != FR_OK) {
        SEGGER_RTT_printf(0, "[SD] root opendir failed res=%d path=%s\r\n", res, RxPath);
        return;
    }

    for (;;) {
        res = f_readdir(&dir, &file_info);
        if (res != FR_OK) {
            SEGGER_RTT_printf(0, "[SD] RX count readdir failed res=%d\r\n", res);
            break;
        }

        if (file_info.fname[0] == '\0') {
            break;
        }

        entry_count++;
    }

    SEGGER_RTT_printf(0, "[SD] RX file count=%u\r\n", entry_count);
    if (entry_count == 0U) {
        SEGGER_RTT_WriteString(0, "[SD]   <empty>\r\n");
        (void)f_closedir(&dir);
        return;
    }

    res = f_rewinddir(&dir);
    if (res != FR_OK) {
        SEGGER_RTT_printf(0, "[SD] RX rewinddir failed res=%d\r\n", res);
        (void)f_closedir(&dir);
        return;
    }

    SEGGER_RTT_WriteString(0, "[SD] RX files:\r\n");
    for (;;) {
        res = f_readdir(&dir, &file_info);
        if (res != FR_OK) {
            SEGGER_RTT_printf(0, "[SD] RX readdir failed res=%d\r\n", res);
            break;
        }

        if (file_info.fname[0] == '\0') {
            break;
        }

        if ((file_info.fattrib & AM_DIR) != 0U) {
            SEGGER_RTT_printf(0, "[SD]   <DIR>  %s\r\n", file_info.fname);
        } else {
            SEGGER_RTT_printf(0, "  [SD]%10u bytes %s\r\n", (uint32_t)file_info.fsize, file_info.fname);
        }
    }

    (void)f_closedir(&dir);
}
#endif

/**
 * @brief  复位 FatFs 挂载关系和 SDMMC/HAL 驱动状态
 *
 * 初始化失败后底层句柄可能停在错误状态，复位后下一次挂载更干净。
 */
static void sd_reset_driver(void)
{
    /*
     * SD 初始化失败后，HAL/SDMMC 句柄可能停在 BUSY/ERROR 状态。
     * 这里主动卸载 FatFs、复位 SDMMC 外设并重新执行 CubeMX 生成的初始化，
     * 让下一次 SdManager_Mount 从干净状态开始。
     */
    (void)f_mount(NULL, (TCHAR const *)SDPath, 1);
    (void)HAL_SD_DeInit(&hsd1);
    __HAL_RCC_SDMMC1_FORCE_RESET();
    __HAL_RCC_SDMMC1_RELEASE_RESET();
    HAL_NVIC_ClearPendingIRQ(SDMMC1_IRQn);
    hsd1.State = HAL_SD_STATE_RESET;
    hsd1.ErrorCode = HAL_SD_ERROR_NONE;
    MX_SDMMC1_SD_Init();
}

/**
 * @brief  查询并缓存 SD 总容量和剩余容量
 *
 * 仅在挂载成功后调用。容量以 KB 保存，供 LCD 和上层状态显示使用。
 */
static void sd_update_capacity(void)
{
    FRESULT res;
    FATFS *fs;
    DWORD fre_clust;

    if (sd_mounted == 0U) {
        sd_clear_capacity();
        return;
    }

    res = f_getfree((TCHAR const *)SDPath, &fre_clust, &fs);
    if (res == FR_OK) {
        /* FatFs 容量单位是 cluster，csize 是每个 cluster 的 sector 数；512B sector 换算成 KB 需要 /2。 */
        sd_total_kb = (uint32_t)((fs->n_fatent - 2U) * fs->csize / 2U);
        sd_free_kb = (uint32_t)(fre_clust * fs->csize / 2U);
        sd_capacity_valid = 1;
        SEGGER_RTT_printf(0, "[SD] OK ! total=%u KB free=%u KB\r\n", sd_total_kb, sd_free_kb);
#if SD_MANAGER_DUMP_RX_DIR_ON_MOUNT
        sd_dump_root_dir();
#endif
    } else {
        sd_clear_capacity();
        SEGGER_RTT_printf(0, "[SD] f_getfree failed res=%d\r\n", res);
    }
}

/**
 * @brief  初始化 SD 管理器内部变量和互斥锁
 */
void SdManager_Init(void)
{
    sd_mounted = 0;
    sd_state = SD_MANAGER_STATE_ABSENT;
    sd_next_mount_tick = 0;
    sd_next_capacity_tick = 0;
    sd_capacity_update_pending = 0;
    sd_driver_reset_pending = 0;
    sd_clear_capacity();
    sd_manager_lock = osMutexNew(&sd_manager_lock_attr);
}

/**
 * @brief  SD 后台管理任务
 *
 * 未挂载时按 SD_MANAGER_RETRY_MS 周期重试挂载，失败不会阻塞其他任务。
 */
void SdManager_Task(void *argument)
{
    SdManagerStatus status;
    uint32_t now;

    (void)argument;

#if SD_MANAGER_STARTUP_DELAY_MS > 0U
    osDelay(SD_MANAGER_STARTUP_DELAY_MS);
#endif
    sd_next_mount_tick = osKernelGetTickCount();
    SEGGER_RTT_WriteString(0, "[SD] manager start\r\n");

    for (;;) {
        now = osKernelGetTickCount();
        if (sd_mounted == 0U && sd_tick_due(now, sd_next_mount_tick) != 0U) {
            /* 挂载失败只安排下一次重试，不能在这里长时间阻塞其他任务。 */
            status = SdManager_Mount();
            if (status == SD_MANAGER_OK) {
                sd_capacity_update_pending = 1;
                sd_next_capacity_tick = osKernelGetTickCount() + SD_MANAGER_CAPACITY_DELAY_MS;
            } else {
                sd_next_mount_tick = now + SD_MANAGER_RETRY_MS;
            }
        } else if (sd_mounted != 0U &&
                   sd_capacity_update_pending != 0U &&
                   sd_tick_due(now, sd_next_capacity_tick) != 0U) {
            sd_capacity_update_pending = 0;
            sd_update_capacity();
        }

        osDelay(50);
    }
}

/**
 * @brief  初始化 SDMMC、挂载 FatFs，并更新 SD 状态
 *
 * @return SD_MANAGER_OK 成功；其他值表示初始化或挂载失败
 */
SdManagerStatus SdManager_Mount(void)
{
    FRESULT res;
    uint8_t bsp_state;
    SdManagerStatus status;

    sd_lock();

    if (sd_mounted != 0U) {
        sd_state = SD_MANAGER_STATE_MOUNTED;
        status = SD_MANAGER_OK;
        goto mount_done;
    }

    sd_state = SD_MANAGER_STATE_INITING;
    if (sd_driver_reset_pending != 0U) {
        sd_reset_driver();
        sd_driver_reset_pending = 0;
        osDelay(SD_MANAGER_INIT_SETTLE_MS);
    }
    /*
     * 当前 sd_diskio.c 定义了 DISABLE_SD_INIT，FatFs disk_initialize()
     * 只检查卡状态，不会调用 BSP_SD_Init()，所以这里必须先完成 BSP 初始化。
     */
    bsp_state = BSP_SD_Init();
    if (bsp_state != MSD_OK) {
        SEGGER_RTT_printf(0, "[SD] init failed ret=%u err=0x%08X state=%u detect=%u\r\n",
                          bsp_state, (uint32_t)hsd1.ErrorCode, hsd1.State, BSP_SD_IsDetected());
        sd_clear_capacity();
        sd_driver_reset_pending = 1;
        sd_state = SD_MANAGER_STATE_ERR_INIT;
        status = SD_MANAGER_ERR_INIT;
        goto mount_done;
    }

    res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
    if (res != FR_OK) {
        SEGGER_RTT_printf(0, "[SD] mount failed res=%d err=0x%08X state=%u detect=%u\r\n",
                          res, (uint32_t)hsd1.ErrorCode, hsd1.State, BSP_SD_IsDetected());
        sd_clear_capacity();
        sd_driver_reset_pending = 1;
        if (res == FR_NOT_READY || res == FR_DISK_ERR) {
            sd_state = SD_MANAGER_STATE_ERR_INIT;
            status = SD_MANAGER_ERR_INIT;
        } else {
            sd_state = SD_MANAGER_STATE_ERR_MOUNT;
            status = SD_MANAGER_ERR_MOUNT;
        }
        goto mount_done;
    }

    sd_mounted = 1;
    sd_state = SD_MANAGER_STATE_MOUNTED;
    sd_driver_reset_pending = 0;
    SEGGER_RTT_WriteString(0, "[SD] mounted\r\n");
    status = SD_MANAGER_OK;

mount_done:
    sd_unlock();
    return status;
}

/**
 * @brief  卸载 FatFs 并复位 SDMMC 驱动
 */
void SdManager_Unmount(void)
{
    sd_lock();
    if (sd_mounted != 0U) {
        (void)f_mount(NULL, (TCHAR const *)SDPath, 1);
        sd_mounted = 0;
        sd_reset_driver();
        sd_driver_reset_pending = 0;
        SEGGER_RTT_WriteString(0, "[SD] unmounted\r\n");
    }
    sd_state = SD_MANAGER_STATE_ABSENT;
    sd_capacity_update_pending = 0;
    sd_clear_capacity();
    sd_unlock();
}

/**
 * @brief  查询 SD 是否在线
 *
 * 当前硬件检测脚无效，因此这里返回“是否已成功挂载”。
 */
uint8_t SdManager_IsPresent(void)
{
    /* 当前工程不依赖插卡检测脚：挂载成功即认为 SD 在线。 */
    return sd_mounted;
}

/**
 * @brief  查询 SD 是否已经挂载
 */
uint8_t SdManager_IsMounted(void)
{
    return sd_mounted;
}

/**
 * @brief  获取 SD 管理器当前状态枚举
 */
SdManagerState SdManager_GetState(void)
{
    return sd_state;
}

/**
 * @brief  获取 SD 状态短文本
 *
 * 返回的字符串用于 LCD 显示，尽量保持短小。
 */
const char *SdManager_GetStateText(void)
{
    switch (sd_state) {
    case SD_MANAGER_STATE_ABSENT:
        return "No Card";
    case SD_MANAGER_STATE_INITING:
        return "Mounting";
    case SD_MANAGER_STATE_MOUNTED:
        return "Mounted";
    case SD_MANAGER_STATE_ERR_INIT:
        return "InitFail";
    case SD_MANAGER_STATE_ERR_MOUNT:
        return "MountFail";
    default:
        return "Unknown";
    }
}

/**
 * @brief  获取缓存的 SD 容量
 *
 * @return 1 容量有效；0 容量无效
 */
uint8_t SdManager_GetCapacity(uint32_t *total_kb, uint32_t *free_kb)
{
    if (total_kb != NULL) {
        *total_kb = sd_total_kb;
    }

    if (free_kb != NULL) {
        *free_kb = sd_free_kb;
    }

    return sd_capacity_valid;
}
