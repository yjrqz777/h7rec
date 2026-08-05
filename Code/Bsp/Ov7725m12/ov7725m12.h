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

#define OV7725M12_FRAME_WIDTH       320U
#define OV7725M12_FRAME_HEIGHT      240U
#define OV7725M12_BYTES_PER_PIXEL   2U
#define OV7725M12_FRAME_PIXEL_COUNT (OV7725M12_FRAME_WIDTH * OV7725M12_FRAME_HEIGHT)
#define OV7725M12_FRAME_SIZE_BYTES  (OV7725M12_FRAME_PIXEL_COUNT * OV7725M12_BYTES_PER_PIXEL)
#define OV7725M12_FRAME_WORD_COUNT  (OV7725M12_FRAME_SIZE_BYTES / 4U)

typedef enum
{
  OV7725M12_STATUS_OK = 0,
  OV7725M12_STATUS_BAD_ARGUMENT = -1,
  OV7725M12_STATUS_BUS_ERROR = -2,
  OV7725M12_STATUS_ID_MISMATCH = -3,
  OV7725M12_STATUS_VERIFY_ERROR = -4
} OV7725M12_Status_t;

typedef struct
{
  uint8_t pid;
  uint8_t version;
} OV7725M12_ID_t;

typedef enum
{
  OV7725M12_CAPTURE_IDLE = 0,
  OV7725M12_CAPTURE_BUSY,
  OV7725M12_CAPTURE_COMPLETE,
  OV7725M12_CAPTURE_ERROR
} OV7725M12_CaptureState_t;

typedef struct
{
  uint32_t fnv1a;
  uint32_t adjacent_changes;
  uint16_t min_word;
  uint16_t max_word;
} OV7725M12_FrameStats_t;

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
OV7725M12_Status_t OV7725M12_ConfigureQVGA_RGB565(I2C_HandleTypeDef *hi2c,
                                                   uint8_t *failed_reg);
OV7725M12_Status_t OV7725M12_SetColorBar(I2C_HandleTypeDef *hi2c,
                                          uint8_t enable);
OV7725M12_Status_t OV7725M12_SetColorBarStages(I2C_HandleTypeDef *hi2c,
                                                uint8_t sensor_enable,
                                                uint8_t dsp_enable);

HAL_StatusTypeDef OV7725M12_StartSnapshot(DCMI_HandleTypeDef *hdcmi);
HAL_StatusTypeDef OV7725M12_StopSnapshot(DCMI_HandleTypeDef *hdcmi);
OV7725M12_CaptureState_t OV7725M12_GetCaptureState(uint32_t *dcmi_error);
uint8_t *OV7725M12_GetFrameBuffer(void);
void OV7725M12_PrepareFrameForCpu(void);
void OV7725M12_AnalyzeFrame(OV7725M12_FrameStats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* OV7725M12_H */
