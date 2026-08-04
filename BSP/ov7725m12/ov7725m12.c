#include "ov7725m12.h"

#include "fmc.h"
#include "main.h"

#define OV7725M12_REG_PID                0x0AU
#define OV7725M12_REG_VERSION            0x0BU
#define OV7725M12_REG_COM3               0x0CU
#define OV7725M12_REG_COM6               0x0FU
#define OV7725M12_REG_COM7               0x12U
#define OV7725M12_REG_COM8               0x13U
#define OV7725M12_REG_COM10              0x15U
#define OV7725M12_REG_HOUTSIZE           0x29U
#define OV7725M12_REG_VOUTSIZE           0x2CU
#define OV7725M12_REG_DSP_CTRL3          0x66U
#define OV7725M12_COM7_SOFTWARE_RESET    0x80U
#define OV7725M12_COM7_QVGA_RGB565       0x46U
#define OV7725M12_COM8_AUTOMATIC_OFF      0xF0U
#define OV7725M12_COM8_AUTOMATIC_ON       0xFFU
#define OV7725M12_COM3_COLOR_BAR_MASK    0x01U
#define OV7725M12_DSP_COLOR_BAR_MASK     0x20U
#define OV7725M12_SCCB_TIMEOUT_MS        100U
#define OV7725M12_POWER_DELAY_MS         10U
#define OV7725M12_RESET_DELAY_MS         20U
#define OV7725M12_REGISTER_DELAY_MS      1U
#define OV7725M12_FAILED_REG_NONE        0xFFU
#define OV7725M12_CACHE_LINE_SIZE         32U
#define OV7725M12_FNV1A_OFFSET_BASIS      2166136261UL
#define OV7725M12_FNV1A_PRIME             16777619UL

typedef struct
{
  uint8_t reg;
  uint8_t value;
} OV7725M12_RegisterValue_t;

/*
 * QVGA RGB565 baseline adapted from HuffieWang/STM32F4-DCMI-OV7725.
 * COM10 remains 0x00 for non-inverted HREF and pixel clock outputs.
 * COM3 remains 0x00 for canonical RGB565 without mirror or red/blue swap.
 */
static const OV7725M12_RegisterValue_t qvga_rgb565_registers[] =
{
  {0x32U, 0x00U},
  {0x2AU, 0x00U},
  {0x11U, 0x03U},
  {OV7725M12_REG_COM6, 0x01U},
  {OV7725M12_REG_COM7, OV7725M12_COM7_QVGA_RGB565},
  {OV7725M12_REG_COM8, OV7725M12_COM8_AUTOMATIC_OFF},
  {0x42U, 0x7FU},
  {0x4DU, 0x00U},
  {0x63U, 0xF0U},
  {0x64U, 0x1FU},
  {0x65U, 0x20U},
  {OV7725M12_REG_DSP_CTRL3, 0x00U},
  {0x67U, 0x00U},
  {0x69U, 0x50U},
  {0x0DU, 0x41U},
  {0x14U, 0x06U},
  {0x24U, 0x75U},
  {0x25U, 0x63U},
  {0x26U, 0xD1U},
  {0x2BU, 0xFFU},
  {0x6BU, 0xAAU},
  {0x8EU, 0x10U},
  {0x8FU, 0x00U},
  {0x90U, 0x00U},
  {0x91U, 0x00U},
  {0x92U, 0x00U},
  {0x93U, 0x00U},
  {0x94U, 0x2CU},
  {0x95U, 0x24U},
  {0x96U, 0x08U},
  {0x97U, 0x14U},
  {0x98U, 0x24U},
  {0x99U, 0x38U},
  {0x9AU, 0x9EU},
  {OV7725M12_REG_COM10, 0x00U},
  {0x9BU, 0x00U},
  {0x9CU, 0x20U},
  {0xA7U, 0x40U},
  {0xA8U, 0x40U},
  {0xA9U, 0x80U},
  {0xAAU, 0x80U},
  {0x9EU, 0x81U},
  {0xA6U, 0x06U},
  {0x7EU, 0x0CU},
  {0x7FU, 0x16U},
  {0x80U, 0x2AU},
  {0x81U, 0x4EU},
  {0x82U, 0x61U},
  {0x83U, 0x6FU},
  {0x84U, 0x7BU},
  {0x85U, 0x86U},
  {0x86U, 0x8EU},
  {0x87U, 0x97U},
  {0x88U, 0xA4U},
  {0x89U, 0xAFU},
  {0x8AU, 0xC5U},
  {0x8BU, 0xD7U},
  {0x8CU, 0xE8U},
  {0x8DU, 0x20U},
  {0x33U, 0x40U},
  {0x34U, 0x00U},
  {0x22U, 0xAFU},
  {0x23U, 0x01U},
  {0x49U, 0x10U},
  {0x4AU, 0x10U},
  {0x4BU, 0x14U},
  {0x4CU, 0x17U},
  {0x46U, 0x05U},
  {0x47U, 0x08U},
  {0x0EU, 0x01U},
  {OV7725M12_REG_COM3, 0x00U},
  {0x09U, 0x03U},
  {OV7725M12_REG_HOUTSIZE, 0x50U},
  {OV7725M12_REG_VOUTSIZE, 0x78U},
  {OV7725M12_REG_COM8, OV7725M12_COM8_AUTOMATIC_ON}
};

static uint8_t *const frame_buffer = (uint8_t *)(uintptr_t)SDRAM_APP_BUF;
static DCMI_HandleTypeDef *volatile active_dcmi;
static volatile OV7725M12_CaptureState_t capture_state = OV7725M12_CAPTURE_IDLE;
static volatile uint32_t capture_error = HAL_DCMI_ERROR_NONE;

static OV7725M12_Status_t OV7725M12_VerifyRegister(I2C_HandleTypeDef *hi2c,
                                                    uint8_t reg,
                                                    uint8_t expected,
                                                    uint8_t *failed_reg);
static void OV7725M12_PrepareFrameForDma(void);

void OV7725M12_PowerDown(void)
{
  HAL_GPIO_WritePin(CAM_RST_GPIO_Port, CAM_RST_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(CAM_PWDN_GPIO_Port, CAM_PWDN_Pin, GPIO_PIN_SET);
}

void OV7725M12_ResetAndWake(void)
{
  OV7725M12_PowerDown();
  HAL_Delay(OV7725M12_POWER_DELAY_MS);

  HAL_GPIO_WritePin(CAM_PWDN_GPIO_Port, CAM_PWDN_Pin, GPIO_PIN_RESET);
  HAL_Delay(OV7725M12_POWER_DELAY_MS);

  HAL_GPIO_WritePin(CAM_RST_GPIO_Port, CAM_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(OV7725M12_RESET_DELAY_MS);
}

OV7725M12_Status_t OV7725M12_ReadRegister(I2C_HandleTypeDef *hi2c,
                                           uint8_t reg,
                                           uint8_t *value)
{
  HAL_StatusTypeDef hal_status;

  if ((hi2c == NULL) || (value == NULL))
  {
    return OV7725M12_STATUS_BAD_ARGUMENT;
  }

  /* SCCB reads use separate write-address and read-data phases with a STOP. */
  hal_status = HAL_I2C_Master_Transmit(hi2c,
                                       OV7725M12_I2C_HAL_ADDRESS,
                                       &reg,
                                       1U,
                                       OV7725M12_SCCB_TIMEOUT_MS);
  if (hal_status != HAL_OK)
  {
    return OV7725M12_STATUS_BUS_ERROR;
  }

  hal_status = HAL_I2C_Master_Receive(hi2c,
                                      OV7725M12_I2C_HAL_ADDRESS,
                                      value,
                                      1U,
                                      OV7725M12_SCCB_TIMEOUT_MS);
  if (hal_status != HAL_OK)
  {
    return OV7725M12_STATUS_BUS_ERROR;
  }

  return OV7725M12_STATUS_OK;
}

OV7725M12_Status_t OV7725M12_WriteRegister(I2C_HandleTypeDef *hi2c,
                                            uint8_t reg,
                                            uint8_t value)
{
  uint8_t data[2];

  if (hi2c == NULL)
  {
    return OV7725M12_STATUS_BAD_ARGUMENT;
  }

  data[0] = reg;
  data[1] = value;

  if (HAL_I2C_Master_Transmit(hi2c,
                              OV7725M12_I2C_HAL_ADDRESS,
                              data,
                              sizeof(data),
                              OV7725M12_SCCB_TIMEOUT_MS) != HAL_OK)
  {
    return OV7725M12_STATUS_BUS_ERROR;
  }

  return OV7725M12_STATUS_OK;
}

OV7725M12_Status_t OV7725M12_Probe(I2C_HandleTypeDef *hi2c,
                                    OV7725M12_ID_t *id)
{
  OV7725M12_Status_t status;

  if ((hi2c == NULL) || (id == NULL))
  {
    return OV7725M12_STATUS_BAD_ARGUMENT;
  }

  id->pid = 0U;
  id->version = 0U;

  OV7725M12_ResetAndWake();

  status = OV7725M12_ReadRegister(hi2c, OV7725M12_REG_PID, &id->pid);
  if (status != OV7725M12_STATUS_OK)
  {
    return status;
  }

  status = OV7725M12_ReadRegister(hi2c,
                                  OV7725M12_REG_VERSION,
                                  &id->version);
  if (status != OV7725M12_STATUS_OK)
  {
    return status;
  }

  if ((id->pid != OV7725M12_EXPECTED_PID) ||
      (id->version != OV7725M12_EXPECTED_VERSION))
  {
    return OV7725M12_STATUS_ID_MISMATCH;
  }

  return OV7725M12_STATUS_OK;
}

OV7725M12_Status_t OV7725M12_ConfigureQVGA_RGB565(I2C_HandleTypeDef *hi2c,
                                                   uint8_t *failed_reg)
{
  OV7725M12_Status_t status;
  uint32_t index;

  if (hi2c == NULL)
  {
    return OV7725M12_STATUS_BAD_ARGUMENT;
  }

  if (failed_reg != NULL)
  {
    *failed_reg = OV7725M12_FAILED_REG_NONE;
  }

  status = OV7725M12_WriteRegister(hi2c,
                                    OV7725M12_REG_COM7,
                                    OV7725M12_COM7_SOFTWARE_RESET);
  if (status != OV7725M12_STATUS_OK)
  {
    if (failed_reg != NULL)
    {
      *failed_reg = OV7725M12_REG_COM7;
    }
    return status;
  }
  HAL_Delay(OV7725M12_RESET_DELAY_MS);

  for (index = 0U;
       index < (sizeof(qvga_rgb565_registers) / sizeof(qvga_rgb565_registers[0]));
       ++index)
  {
    status = OV7725M12_WriteRegister(hi2c,
                                      qvga_rgb565_registers[index].reg,
                                      qvga_rgb565_registers[index].value);
    if (status != OV7725M12_STATUS_OK)
    {
      if (failed_reg != NULL)
      {
        *failed_reg = qvga_rgb565_registers[index].reg;
      }
      return status;
    }
  }

  HAL_Delay(OV7725M12_REGISTER_DELAY_MS);

  status = OV7725M12_VerifyRegister(hi2c,
                                     OV7725M12_REG_COM7,
                                     OV7725M12_COM7_QVGA_RGB565,
                                     failed_reg);
  if (status != OV7725M12_STATUS_OK)
  {
    return status;
  }

  status = OV7725M12_VerifyRegister(hi2c,
                                     OV7725M12_REG_COM10,
                                     0x00U,
                                     failed_reg);
  if (status != OV7725M12_STATUS_OK)
  {
    return status;
  }

  status = OV7725M12_VerifyRegister(hi2c,
                                     OV7725M12_REG_COM3,
                                     0x00U,
                                     failed_reg);
  if (status != OV7725M12_STATUS_OK)
  {
    return status;
  }

  status = OV7725M12_VerifyRegister(hi2c,
                                     OV7725M12_REG_COM6,
                                     0x01U,
                                     failed_reg);
  if (status != OV7725M12_STATUS_OK)
  {
    return status;
  }

  status = OV7725M12_VerifyRegister(hi2c,
                                     OV7725M12_REG_HOUTSIZE,
                                     0x50U,
                                     failed_reg);
  if (status != OV7725M12_STATUS_OK)
  {
    return status;
  }

  return OV7725M12_VerifyRegister(hi2c,
                                   OV7725M12_REG_VOUTSIZE,
                                   0x78U,
                                   failed_reg);
}

OV7725M12_Status_t OV7725M12_SetColorBar(I2C_HandleTypeDef *hi2c,
                                          uint8_t enable)
{
  return OV7725M12_SetColorBarStages(hi2c, enable, enable);
}

OV7725M12_Status_t OV7725M12_SetColorBarStages(I2C_HandleTypeDef *hi2c,
                                                uint8_t sensor_enable,
                                                uint8_t dsp_enable)
{
  OV7725M12_Status_t status;
  uint8_t com3;
  uint8_t dsp_ctrl3;
  uint8_t new_com3;
  uint8_t new_dsp_ctrl3;
  uint8_t verify;

  if (hi2c == NULL)
  {
    return OV7725M12_STATUS_BAD_ARGUMENT;
  }

  status = OV7725M12_ReadRegister(hi2c, OV7725M12_REG_COM3, &com3);
  if (status != OV7725M12_STATUS_OK)
  {
    return status;
  }

  status = OV7725M12_ReadRegister(hi2c,
                                  OV7725M12_REG_DSP_CTRL3,
                                  &dsp_ctrl3);
  if (status != OV7725M12_STATUS_OK)
  {
    return status;
  }

  new_com3 = (uint8_t)(com3 & (uint8_t)~OV7725M12_COM3_COLOR_BAR_MASK);
  new_dsp_ctrl3 =
      (uint8_t)(dsp_ctrl3 & (uint8_t)~OV7725M12_DSP_COLOR_BAR_MASK);
  if (sensor_enable != 0U)
  {
    new_com3 |= OV7725M12_COM3_COLOR_BAR_MASK;
  }
  if (dsp_enable != 0U)
  {
    new_dsp_ctrl3 |= OV7725M12_DSP_COLOR_BAR_MASK;
  }

  status = OV7725M12_WriteRegister(hi2c,
                                    OV7725M12_REG_COM3,
                                    new_com3);
  if (status != OV7725M12_STATUS_OK)
  {
    return status;
  }

  status = OV7725M12_WriteRegister(hi2c,
                                    OV7725M12_REG_DSP_CTRL3,
                                    new_dsp_ctrl3);
  if (status != OV7725M12_STATUS_OK)
  {
    (void)OV7725M12_WriteRegister(hi2c, OV7725M12_REG_COM3, com3);
    return status;
  }

  HAL_Delay(OV7725M12_REGISTER_DELAY_MS);

  status = OV7725M12_ReadRegister(hi2c, OV7725M12_REG_COM3, &verify);
  if ((status != OV7725M12_STATUS_OK) ||
      ((verify & OV7725M12_COM3_COLOR_BAR_MASK) !=
       (new_com3 & OV7725M12_COM3_COLOR_BAR_MASK)))
  {
    (void)OV7725M12_WriteRegister(hi2c, OV7725M12_REG_COM3, com3);
    (void)OV7725M12_WriteRegister(hi2c,
                                  OV7725M12_REG_DSP_CTRL3,
                                  dsp_ctrl3);
    return (status == OV7725M12_STATUS_OK) ?
           OV7725M12_STATUS_VERIFY_ERROR : status;
  }

  status = OV7725M12_ReadRegister(hi2c,
                                  OV7725M12_REG_DSP_CTRL3,
                                  &verify);
  if ((status != OV7725M12_STATUS_OK) ||
      ((verify & OV7725M12_DSP_COLOR_BAR_MASK) !=
       (new_dsp_ctrl3 & OV7725M12_DSP_COLOR_BAR_MASK)))
  {
    (void)OV7725M12_WriteRegister(hi2c, OV7725M12_REG_COM3, com3);
    (void)OV7725M12_WriteRegister(hi2c,
                                  OV7725M12_REG_DSP_CTRL3,
                                  dsp_ctrl3);
    return (status == OV7725M12_STATUS_OK) ?
           OV7725M12_STATUS_VERIFY_ERROR : status;
  }

  return OV7725M12_STATUS_OK;
}

static OV7725M12_Status_t OV7725M12_VerifyRegister(I2C_HandleTypeDef *hi2c,
                                                    uint8_t reg,
                                                    uint8_t expected,
                                                    uint8_t *failed_reg)
{
  OV7725M12_Status_t status;
  uint8_t value;

  status = OV7725M12_ReadRegister(hi2c, reg, &value);
  if (status != OV7725M12_STATUS_OK)
  {
    if (failed_reg != NULL)
    {
      *failed_reg = reg;
    }
    return status;
  }

  if (value != expected)
  {
    if (failed_reg != NULL)
    {
      *failed_reg = reg;
    }
    return OV7725M12_STATUS_VERIFY_ERROR;
  }

  return OV7725M12_STATUS_OK;
}

HAL_StatusTypeDef OV7725M12_StartSnapshot(DCMI_HandleTypeDef *hdcmi)
{
  HAL_StatusTypeDef hal_status;

  if ((hdcmi == NULL) || (hdcmi->DMA_Handle == NULL) ||
      (((uintptr_t)frame_buffer & (OV7725M12_CACHE_LINE_SIZE - 1U)) != 0U))
  {
    return HAL_ERROR;
  }

  if (capture_state == OV7725M12_CAPTURE_BUSY)
  {
    return HAL_BUSY;
  }

  __HAL_DCMI_DISABLE_IT(hdcmi,
                        DCMI_IT_LINE | DCMI_IT_VSYNC | DCMI_IT_FRAME |
                        DCMI_IT_ERR | DCMI_IT_OVR);
  __HAL_DCMI_CLEAR_FLAG(hdcmi,
                         DCMI_FLAG_FRAMERI | DCMI_FLAG_OVRRI |
                         DCMI_FLAG_ERRRI | DCMI_FLAG_VSYNCRI |
                         DCMI_FLAG_LINERI);

  hdcmi->ErrorCode = HAL_DCMI_ERROR_NONE;
  hdcmi->DMA_Handle->ErrorCode = HAL_DMA_ERROR_NONE;
  OV7725M12_PrepareFrameForDma();

  active_dcmi = hdcmi;
  capture_error = HAL_DCMI_ERROR_NONE;
  capture_state = OV7725M12_CAPTURE_BUSY;
  __HAL_DCMI_ENABLE_IT(hdcmi, DCMI_IT_ERR | DCMI_IT_OVR);

  hal_status = HAL_DCMI_Start_DMA(hdcmi,
                                  DCMI_MODE_SNAPSHOT,
                                  (uint32_t)(uintptr_t)frame_buffer,
                                  OV7725M12_FRAME_WORD_COUNT);
  if (hal_status != HAL_OK)
  {
    capture_error = hdcmi->ErrorCode;
    capture_state = OV7725M12_CAPTURE_ERROR;
    (void)HAL_DCMI_Stop(hdcmi);
    active_dcmi = NULL;
  }

  return hal_status;
}

HAL_StatusTypeDef OV7725M12_StopSnapshot(DCMI_HandleTypeDef *hdcmi)
{
  HAL_StatusTypeDef hal_status;

  if ((hdcmi == NULL) || (hdcmi->DMA_Handle == NULL))
  {
    return HAL_ERROR;
  }

  hal_status = HAL_DCMI_Stop(hdcmi);
  active_dcmi = NULL;
  capture_state = OV7725M12_CAPTURE_IDLE;

  return hal_status;
}

OV7725M12_CaptureState_t OV7725M12_GetCaptureState(uint32_t *dcmi_error)
{
  OV7725M12_CaptureState_t state = capture_state;

  __DMB();
  if (dcmi_error != NULL)
  {
    *dcmi_error = capture_error;
  }

  return state;
}

uint8_t *OV7725M12_GetFrameBuffer(void)
{
  return frame_buffer;
}

void OV7725M12_PrepareFrameForCpu(void)
{
  __DMB();
#if (__DCACHE_PRESENT == 1U)
  if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
  {
    SCB_InvalidateDCache_by_Addr((uint32_t *)frame_buffer,
                                 (int32_t)OV7725M12_FRAME_SIZE_BYTES);
    __DSB();
    __ISB();
  }
#endif
}

void OV7725M12_AnalyzeFrame(OV7725M12_FrameStats_t *stats)
{
  const uint16_t *pixels = (const uint16_t *)frame_buffer;
  uint32_t index;
  uint32_t hash = OV7725M12_FNV1A_OFFSET_BASIS;
  uint32_t adjacent_changes = 0U;
  uint16_t min_word = 0xFFFFU;
  uint16_t max_word = 0U;
  uint16_t previous;

  if (stats == NULL)
  {
    return;
  }

  previous = pixels[0];

  for (index = 0U; index < OV7725M12_FRAME_PIXEL_COUNT; ++index)
  {
    uint16_t value = pixels[index];

    hash ^= (uint8_t)value;
    hash *= OV7725M12_FNV1A_PRIME;
    hash ^= (uint8_t)(value >> 8U);
    hash *= OV7725M12_FNV1A_PRIME;

    if (value < min_word)
    {
      min_word = value;
    }
    if (value > max_word)
    {
      max_word = value;
    }
    if ((index != 0U) && (value != previous))
    {
      ++adjacent_changes;
    }
    previous = value;
  }

  stats->fnv1a = hash;
  stats->adjacent_changes = adjacent_changes;
  stats->min_word = min_word;
  stats->max_word = max_word;
}

void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi)
{
  if ((hdcmi == active_dcmi) &&
      (capture_state == OV7725M12_CAPTURE_BUSY))
  {
    if ((hdcmi->ErrorCode != HAL_DCMI_ERROR_NONE) ||
        (hdcmi->DMA_Handle->ErrorCode != HAL_DMA_ERROR_NONE))
    {
      capture_error = hdcmi->ErrorCode;
      if (hdcmi->DMA_Handle->ErrorCode != HAL_DMA_ERROR_NONE)
      {
        capture_error |= HAL_DCMI_ERROR_DMA;
      }
      __DMB();
      capture_state = OV7725M12_CAPTURE_ERROR;
      return;
    }

    __DMB();
    capture_state = OV7725M12_CAPTURE_COMPLETE;
  }
}

void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *hdcmi)
{
  if (hdcmi == active_dcmi)
  {
    capture_error = hdcmi->ErrorCode;
    if (hdcmi->DMA_Handle->ErrorCode != HAL_DMA_ERROR_NONE)
    {
      capture_error |= HAL_DCMI_ERROR_DMA;
    }
    __DMB();
    capture_state = OV7725M12_CAPTURE_ERROR;
  }
}

static void OV7725M12_PrepareFrameForDma(void)
{
#if (__DCACHE_PRESENT == 1U)
  if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
  {
    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)frame_buffer,
                                      (int32_t)OV7725M12_FRAME_SIZE_BYTES);
    __DSB();
    __ISB();
  }
#endif
}
