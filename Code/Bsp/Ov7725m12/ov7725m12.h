/**
 * @file ov7725m12.h
 * @brief 声明 OV7725M12 摄像头寄存器配置、DCMI 采集和帧缓冲区接口。
 */

#ifndef OV7725M12_H
#define OV7725M12_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"

#define OV7725M12_I2C_7BIT_ADDRESS  0x21U /* OV7725 SCCB 7 位从机地址 */
#define OV7725M12_I2C_HAL_ADDRESS   (OV7725M12_I2C_7BIT_ADDRESS << 1U) /* STM32 HAL 使用的左移地址 */

#define OV7725M12_EXPECTED_PID      0x77U /* 期望的产品标识 */
#define OV7725M12_EXPECTED_VERSION  0x21U /* 期望的版本标识 */

#define OV7725M12_FRAME_WIDTH       320U /* QVGA 图像宽度，单位：像素 */
#define OV7725M12_FRAME_HEIGHT      240U /* QVGA 图像高度，单位：像素 */
#define OV7725M12_BYTES_PER_PIXEL   2U   /* RGB565 每像素字节数 */
#define OV7725M12_FRAME_PIXEL_COUNT (OV7725M12_FRAME_WIDTH * OV7725M12_FRAME_HEIGHT)
#define OV7725M12_FRAME_SIZE_BYTES  (OV7725M12_FRAME_PIXEL_COUNT * OV7725M12_BYTES_PER_PIXEL)
#define OV7725M12_FRAME_WORD_COUNT  (OV7725M12_FRAME_SIZE_BYTES / 4U)

/**
 * @brief 表示 OV7725M12 驱动操作结果。
 */
typedef enum
{
  OV7725M12_STATUS_OK = 0,              /**< 操作成功。 */
  OV7725M12_STATUS_BAD_ARGUMENT = -1,   /**< 输入参数无效。 */
  OV7725M12_STATUS_BUS_ERROR = -2,      /**< SCCB/I2C 总线操作失败。 */
  OV7725M12_STATUS_ID_MISMATCH = -3,    /**< 读取到的芯片标识不匹配。 */
  OV7725M12_STATUS_VERIFY_ERROR = -4    /**< 寄存器回读值与期望值不一致。 */
} OV7725M12_Status_t;

/**
 * @brief 保存摄像头产品和版本标识。
 */
typedef struct
{
  uint8_t pid;     /**< 产品标识寄存器值。 */
  uint8_t version; /**< 版本标识寄存器值。 */
} OV7725M12_ID_t;

/**
 * @brief 表示当前单帧 DCMI 采集状态。
 */
typedef enum
{
  OV7725M12_CAPTURE_IDLE = 0, /**< 当前没有采集操作。 */
  OV7725M12_CAPTURE_BUSY,     /**< DCMI 和 DMA 正在采集。 */
  OV7725M12_CAPTURE_COMPLETE, /**< 一帧图像已采集完成。 */
  OV7725M12_CAPTURE_ERROR     /**< 最近一次采集发生错误。 */
} OV7725M12_CaptureState_t;

/**
 * @brief 保存一帧 RGB565 图像的诊断统计结果。
 */
typedef struct
{
  uint32_t fnv1a;            /**< 整帧数据的 FNV-1a 哈希值。 */
  uint32_t adjacent_changes; /**< 相邻像素值发生变化的次数。 */
  uint16_t min_word;         /**< 帧内最小 RGB565 原始值。 */
  uint16_t max_word;         /**< 帧内最大 RGB565 原始值。 */
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
