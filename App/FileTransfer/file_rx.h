#ifndef FILE_RX_H
#define FILE_RX_H

#include <stdint.h>

void FileRx_Init(void);
void FileRx_OnUsbData(const uint8_t *data, uint32_t len);
void FileRx_Task(void *argument);

#endif /* FILE_RX_H */
