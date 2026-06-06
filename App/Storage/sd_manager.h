#ifndef SD_MANAGER_H
#define SD_MANAGER_H

#include <stdint.h>

typedef enum {
    SD_MANAGER_EVENT_NONE = 0,
    SD_MANAGER_EVENT_INSERTED,
    SD_MANAGER_EVENT_REMOVED
} SdManagerEvent;

typedef enum {
    SD_MANAGER_OK = 0,
    SD_MANAGER_ERR_ABSENT,
    SD_MANAGER_ERR_INIT,
    SD_MANAGER_ERR_MOUNT
} SdManagerStatus;

void SdManager_Init(void);
SdManagerEvent SdManager_Poll(void);
void SdManager_Task(void *argument);
SdManagerStatus SdManager_Mount(void);
void SdManager_Unmount(void);
uint8_t SdManager_IsPresent(void);
uint8_t SdManager_IsMounted(void);
uint8_t SdManager_TakeInsertedEvent(void);
uint8_t SdManager_TakeRemovedEvent(void);

#endif /* SD_MANAGER_H */
