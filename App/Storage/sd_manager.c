#include "sd_manager.h"

#include "SEGGER_RTT.h"
#include "bsp_driver_sd.h"
#include "cmsis_os.h"
#include "fatfs.h"

#define SD_MANAGER_POLL_MS        50U
#define SD_MANAGER_DEBOUNCE_COUNT 4U

static uint8_t sd_present_stable;
static uint8_t sd_present_candidate;
static uint8_t sd_present_count;
static uint8_t sd_mounted;
static uint32_t sd_last_poll_tick;
static volatile uint8_t sd_inserted_event;
static volatile uint8_t sd_removed_event;

static uint8_t sd_present_raw(void)
{
    return (BSP_SD_IsDetected() == SD_PRESENT) ? 1U : 0U;
}

void SdManager_Init(void)
{
    uint8_t present;

    present = sd_present_raw();
    sd_present_stable = present;
    sd_present_candidate = present;
    sd_present_count = SD_MANAGER_DEBOUNCE_COUNT;
    sd_mounted = 0;
    sd_last_poll_tick = 0;
    sd_inserted_event = 0;
    sd_removed_event = 0;
}

SdManagerEvent SdManager_Poll(void)
{
    uint32_t now;
    uint8_t present;

    now = osKernelGetTickCount();
    if ((now - sd_last_poll_tick) < SD_MANAGER_POLL_MS) {
        return SD_MANAGER_EVENT_NONE;
    }
    sd_last_poll_tick = now;

    present = sd_present_raw();
    if (present != sd_present_candidate) {
        sd_present_candidate = present;
        sd_present_count = 1;
        return SD_MANAGER_EVENT_NONE;
    }

    if (sd_present_count < SD_MANAGER_DEBOUNCE_COUNT) {
        sd_present_count++;
        return SD_MANAGER_EVENT_NONE;
    }

    if (present == sd_present_stable) {
        return SD_MANAGER_EVENT_NONE;
    }

    sd_present_stable = present;
    if (sd_present_stable != 0U) {
        SEGGER_RTT_WriteString(0, "[SD] inserted\r\n");
        sd_inserted_event = 1;
        return SD_MANAGER_EVENT_INSERTED;
    }

    SEGGER_RTT_WriteString(0, "[SD] removed\r\n");
    sd_removed_event = 1;
    SdManager_Unmount();
    return SD_MANAGER_EVENT_REMOVED;
}

void SdManager_Task(void *argument)
{
    (void)argument;

    for (;;) {
        (void)SdManager_Poll();
        osDelay(1);
    }
}

SdManagerStatus SdManager_Mount(void)
{
    FRESULT res;
    uint8_t sd_state;

    if (sd_present_stable == 0U || sd_present_raw() == 0U) {
        return SD_MANAGER_ERR_ABSENT;
    }

    if (sd_mounted != 0U) {
        return SD_MANAGER_OK;
    }

    sd_state = BSP_SD_Init();
    if (sd_state != MSD_OK) {
        SEGGER_RTT_printf(0, "[SD] init failed ret=%u\r\n", sd_state);
        return SD_MANAGER_ERR_INIT;
    }

    res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
    if (res != FR_OK) {
        SEGGER_RTT_printf(0, "[SD] mount failed res=%d\r\n", res);
        return SD_MANAGER_ERR_MOUNT;
    }

    sd_mounted = 1;
    SEGGER_RTT_WriteString(0, "[SD] mounted\r\n");
    return SD_MANAGER_OK;
}

void SdManager_Unmount(void)
{
    if (sd_mounted != 0U) {
        (void)f_mount(NULL, (TCHAR const *)SDPath, 1);
        sd_mounted = 0;
        SEGGER_RTT_WriteString(0, "[SD] unmounted\r\n");
    }
}

uint8_t SdManager_IsPresent(void)
{
    return (sd_present_stable != 0U && sd_present_raw() != 0U) ? 1U : 0U;
}

uint8_t SdManager_IsMounted(void)
{
    return sd_mounted;
}

uint8_t SdManager_TakeInsertedEvent(void)
{
    uint8_t event = sd_inserted_event;
    sd_inserted_event = 0;
    return event;
}

uint8_t SdManager_TakeRemovedEvent(void)
{
    uint8_t event = sd_removed_event;
    sd_removed_event = 0;
    return event;
}
