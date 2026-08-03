#ifndef OV7725M12_H
#define OV7725M12_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"

#define OV7725M12_I2C_7BIT_ADDRESS  0x21U
#define OV7725M12_I2C_HAL_ADDRESS   (OV7725M12_I2C_7BIT_ADDRESS << 1U)

#define OV7725M12_EXPECTED_PID      0x77U
#define OV7725M12_EXPECTED_VERSION  0x21U

typedef enum
{
  OV7725M12_STATUS_OK = 0,
  OV7725M12_STATUS_BAD_ARGUMENT = -1,
  OV7725M12_STATUS_BUS_ERROR = -2,
  OV7725M12_STATUS_ID_MISMATCH = -3
} OV7725M12_Status_t;

typedef struct
{
  uint8_t pid;
  uint8_t version;
} OV7725M12_ID_t;

void OV7725M12_PowerDown(void);
void OV7725M12_ResetAndWake(void);

OV7725M12_Status_t OV7725M12_ReadRegister(I2C_HandleTypeDef *hi2c,
                                           uint8_t reg,
                                           uint8_t *value);
OV7725M12_Status_t OV7725M12_WriteRegister(I2C_HandleTypeDef *hi2c,
                                            uint8_t reg,
                                            uint8_t value);
OV7725M12_Status_t OV7725M12_Probe(I2C_HandleTypeDef *hi2c,
                                    OV7725M12_ID_t *id);

#ifdef __cplusplus
}
#endif

#endif /* OV7725M12_H */
