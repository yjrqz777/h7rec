/**
 * @file app_camera.c
 * @brief Implements OV7725 capture and LVGL preview buffer management.
 */

#include "app_camera.h"

#include "cmsis_os2.h"
#include "dcmi.h"
#include "fmc.h"
#include "i2c.h"
#include "lvgl.h"
#include "ov7725m12.h"
#include "SEGGER_RTT.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdint.h>

#if APP_CAMERA_DIAGNOSTIC_ENABLE
typedef enum
{
  E_APP_CAMERA_CAPTURE_FAILED = 0, /**< The snapshot operation failed. */
  E_APP_CAMERA_CAPTURE_UNIFORM,   /**< The captured frame contains one value. */
  E_APP_CAMERA_CAPTURE_ACTIVE     /**< The captured frame contains changing data. */
} eAppCameraCaptureResultDef;
#endif

static lv_obj_t * ptPreviewImage;
static lv_img_dsc_t tPreviewDescriptors[APP_CAMERA_PREVIEW_BUFFER_COUNT];

#if APP_CAMERA_COLOR_COMPARE_ENABLE
static lv_obj_t * ptPreviewModeLabels[4];
#endif

/* Three SDRAM buffers let the producer replace a pending frame safely. */
static volatile uint8_t u8PreviewActiveIndex = APP_CAMERA_INDEX_NONE;
static volatile uint8_t u8PreviewPendingIndex = APP_CAMERA_INDEX_NONE;
static uint8_t u8PreviewNextWriteIndex;

/**
 * @brief Resolves one preview buffer within the reserved SDRAM application area.
 * @param[in] u8Index Index of the requested preview buffer.
 * @return Pointer to the selected preview buffer.
 * @note Preview buffers begin immediately after the full-resolution capture buffer.
 */
static uint8_t * AppCamera_GetPreviewBuffer(uint8_t u8Index)
{
  const uintptr_t PreviewBaseAddress =
    (uintptr_t)SDRAM_APP_BUF + (uintptr_t)OV7725M12_FRAME_SIZE_BYTES;

  return (uint8_t *)(PreviewBaseAddress +
                     ((uintptr_t)u8Index * (uintptr_t)APP_CAMERA_PREVIEW_SIZE_BYTES));
}

/**
 * @brief Selects a preview buffer not owned by the producer or LVGL.
 * @return An available buffer index, or APP_CAMERA_INDEX_NONE when all buffers are owned.
 * @note Shared ownership indices are read and updated inside an RTOS critical section.
 */
static uint8_t AppCamera_AcquireWriteBuffer(void)
{
  uint8_t ActiveIndex;
  uint8_t PendingIndex;
  uint8_t Candidate;
  uint8_t Offset;

  taskENTER_CRITICAL();
  ActiveIndex = u8PreviewActiveIndex;
  PendingIndex = u8PreviewPendingIndex;

  Candidate = APP_CAMERA_INDEX_NONE;
  for (Offset = 0U; Offset < APP_CAMERA_PREVIEW_BUFFER_COUNT; ++Offset)
  {
    uint8_t Index = (uint8_t)((u8PreviewNextWriteIndex + Offset) %
                              APP_CAMERA_PREVIEW_BUFFER_COUNT);
    if ((Index != ActiveIndex) && (Index != PendingIndex))
    {
      Candidate = Index;
      u8PreviewNextWriteIndex = (uint8_t)((Index + 1U) %
                                          APP_CAMERA_PREVIEW_BUFFER_COUNT);
      break;
    }
  }
  taskEXIT_CRITICAL();

  return Candidate;
}

/**
 * @brief Transfers ownership of a filled preview buffer to the LVGL task.
 * @param[in] u8Index Index of the completed preview buffer.
 * @note The data memory barrier completes buffer writes before publishing the index.
 */
static void AppCamera_PublishPreviewBuffer(uint8_t u8Index)
{
  __DMB();
  taskENTER_CRITICAL();
  u8PreviewPendingIndex = u8Index;
  taskEXIT_CRITICAL();
}

/**
 * @brief Converts one captured RGB565 pixel into the configured preview layout.
 * @param[in] pu8Source Pointer to the two captured pixel bytes.
 * @param[out] pu8Destination Pointer receiving the two converted pixel bytes.
 * @param[in] u8Transform Bit mask selecting byte and red-blue transformations.
 */
static void AppCamera_WritePreviewPixel(const uint8_t * pu8Source,
                                        uint8_t * pu8Destination,
                                        uint8_t u8Transform)
{
  uint8_t HighByte = pu8Source[0];
  uint8_t LowByte = pu8Source[1];

  if ((u8Transform & APP_CAMERA_TRANSFORM_SWAP_BYTES) != 0U)
  {
    const uint8_t Temporary = HighByte;
    HighByte = LowByte;
    LowByte = Temporary;
  }

  if ((u8Transform & APP_CAMERA_TRANSFORM_SWAP_RED_BLUE) != 0U)
  {
    uint16_t Pixel = ((uint16_t)HighByte << 8U) | (uint16_t)LowByte;

    Pixel = (uint16_t)(((Pixel & 0x001FU) << 11U) |
                       (Pixel & 0x07E0U) |
                       ((Pixel & 0xF800U) >> 11U));
    HighByte = (uint8_t)(Pixel >> 8U);
    LowByte = (uint8_t)Pixel;
  }

  pu8Destination[0] = HighByte;
  pu8Destination[1] = LowByte;
}

/**
 * @brief Crops and downsamples one QVGA frame into the LCD preview buffers.
 * @param[in] pu8Source Pointer to the captured QVGA RGB565 frame.
 * @param[out] pu8Destination Pointer to the destination preview buffer.
 */
static void AppCamera_CropAndDownsample(const uint8_t * pu8Source,
                                        uint8_t * pu8Destination)
{
  uint32_t OutputY;

#if APP_CAMERA_COLOR_COMPARE_ENABLE
  /* Each quadrant shows the same central 320x160 crop with one transform. */
  for (OutputY = 0U; OutputY < APP_CAMERA_PREVIEW_HEIGHT; ++OutputY)
  {
    const uint32_t LocalY = OutputY % APP_CAMERA_COMPARE_HEIGHT;
    const uint32_t SourceY = 40U +
                             (LocalY * APP_CAMERA_COMPARE_SAMPLE_STEP);
    const uint8_t * pu8SourceRow = pu8Source +
                                   ((SourceY * OV7725M12_FRAME_WIDTH) *
                                    OV7725M12_BYTES_PER_PIXEL);
    uint8_t * pu8DestinationRow = pu8Destination +
                                  (OutputY * APP_CAMERA_PREVIEW_WIDTH *
                                   APP_CAMERA_PREVIEW_BPP);
    uint32_t OutputX;

    for (OutputX = 0U; OutputX < APP_CAMERA_PREVIEW_WIDTH; ++OutputX)
    {
      const uint32_t LocalX = OutputX % APP_CAMERA_COMPARE_WIDTH;
      const uint8_t * pu8SourcePixel = pu8SourceRow +
                                       ((LocalX * APP_CAMERA_COMPARE_SAMPLE_STEP) *
                                        OV7725M12_BYTES_PER_PIXEL);
      uint8_t Transform = 0U;

      if (OutputX >= APP_CAMERA_COMPARE_WIDTH)
      {
        Transform |= APP_CAMERA_TRANSFORM_SWAP_RED_BLUE;
      }
      if (OutputY >= APP_CAMERA_COMPARE_HEIGHT)
      {
        Transform |= APP_CAMERA_TRANSFORM_SWAP_BYTES;
      }

      AppCamera_WritePreviewPixel(pu8SourcePixel,
                                  &pu8DestinationRow[OutputX * 2U],
                                  Transform);
    }
  }
#else
  uint8_t Transform = 0U;

#if APP_CAMERA_SWAP_BYTES
  Transform |= APP_CAMERA_TRANSFORM_SWAP_BYTES;
#endif
#if APP_CAMERA_SWAP_RED_BLUE
  Transform |= APP_CAMERA_TRANSFORM_SWAP_RED_BLUE;
#endif

  /* Crop the 320x240 source to its central 320x160 region, then sample 2x2. */
  for (OutputY = 0U; OutputY < APP_CAMERA_PREVIEW_HEIGHT; ++OutputY)
  {
    const uint32_t SourceY = 40U + (OutputY * 2U);
    const uint8_t * pu8SourceRow = pu8Source +
                                   ((SourceY * OV7725M12_FRAME_WIDTH) *
                                    OV7725M12_BYTES_PER_PIXEL);
    uint8_t * pu8DestinationRow = pu8Destination +
                                  (OutputY * APP_CAMERA_PREVIEW_WIDTH *
                                   APP_CAMERA_PREVIEW_BPP);
    uint32_t OutputX;

    for (OutputX = 0U; OutputX < APP_CAMERA_PREVIEW_WIDTH; ++OutputX)
    {
      const uint8_t * pu8SourcePixel = pu8SourceRow +
                                       ((OutputX * 2U) *
                                        OV7725M12_BYTES_PER_PIXEL);

      AppCamera_WritePreviewPixel(pu8SourcePixel,
                                  &pu8DestinationRow[OutputX * 2U],
                                  Transform);
    }
  }
#endif
}

/**
 * @brief Captures one frame and prepares its SDRAM buffer for CPU access.
 * @param[out] pu32ElapsedMs Optional pointer receiving the capture duration.
 * @retval 0 The capture failed or timed out.
 * @retval 1 The frame was captured successfully.
 */
static uint8_t AppCamera_CaptureFrame(uint32_t * pu32ElapsedMs)
{
  OV7725M12_CaptureState_t eCaptureState;
  HAL_StatusTypeDef eHalStatus;
  HAL_StatusTypeDef eStopStatus;
  uint32_t CaptureStart;
  uint32_t CaptureElapsed;
  uint32_t DcmiError = HAL_DCMI_ERROR_NONE;
  uint32_t DmaError;
  uint32_t DmaRemaining;
  uint32_t DcmiRisr;

  eHalStatus = OV7725M12_StartSnapshot(&hdcmi);
  if (eHalStatus != HAL_OK)
  {
    SEGGER_RTT_printf(0,
                      "[CAM] live start failed: HAL=%d DCMI=0x%08lX DMA=0x%08lX\r\n",
                      (int)eHalStatus,
                      (unsigned long)hdcmi.ErrorCode,
                      (unsigned long)hdcmi.DMA_Handle->ErrorCode);
    return 0U;
  }

  CaptureStart = osKernelGetTickCount();
  do
  {
    eCaptureState = OV7725M12_GetCaptureState(&DcmiError);
    if ((eCaptureState != OV7725M12_CAPTURE_BUSY) ||
        (hdcmi.ErrorCode != HAL_DCMI_ERROR_NONE) ||
        (hdcmi.DMA_Handle->ErrorCode != HAL_DMA_ERROR_NONE))
    {
      break;
    }
    osDelay(1U);
  } while ((osKernelGetTickCount() - CaptureStart) <
           APP_CAMERA_CAPTURE_TIMEOUT_MS);

  CaptureElapsed = osKernelGetTickCount() - CaptureStart;
  eCaptureState = OV7725M12_GetCaptureState(&DcmiError);
  DmaRemaining = __HAL_DMA_GET_COUNTER(hdcmi.DMA_Handle);
  DmaError = hdcmi.DMA_Handle->ErrorCode;
  if (hdcmi.ErrorCode != HAL_DCMI_ERROR_NONE)
  {
    DcmiError = hdcmi.ErrorCode;
  }
  DcmiRisr = hdcmi.Instance->RISR;

  eStopStatus = OV7725M12_StopSnapshot(&hdcmi);
  if (eStopStatus != HAL_OK)
  {
    DcmiError |= hdcmi.ErrorCode;
  }

  if ((eCaptureState == OV7725M12_CAPTURE_BUSY) &&
      (DcmiError == HAL_DCMI_ERROR_NONE) &&
      (DmaError == HAL_DMA_ERROR_NONE))
  {
    SEGGER_RTT_printf(0,
                      "[CAM] live timeout: %lu ms left=%lu RISR=0x%08lX DCMI=0x%08lX DMA=0x%08lX stop=%d\r\n",
                      (unsigned long)CaptureElapsed,
                      (unsigned long)DmaRemaining,
                      (unsigned long)DcmiRisr,
                      (unsigned long)DcmiError,
                      (unsigned long)DmaError,
                      (int)eStopStatus);
    return 0U;
  }

  if ((eCaptureState != OV7725M12_CAPTURE_COMPLETE) ||
      (DcmiError != HAL_DCMI_ERROR_NONE) ||
      (DmaError != HAL_DMA_ERROR_NONE) ||
      (DmaRemaining != 0U) ||
      (eStopStatus != HAL_OK))
  {
    SEGGER_RTT_printf(0,
                      "[CAM] live capture error: %lu ms DCMI=0x%08lX DMA=0x%08lX left=%lu stop=%d\r\n",
                      (unsigned long)CaptureElapsed,
                      (unsigned long)DcmiError,
                      (unsigned long)DmaError,
                      (unsigned long)DmaRemaining,
                      (int)eStopStatus);
    return 0U;
  }

  OV7725M12_PrepareFrameForCpu();
  if (pu32ElapsedMs != NULL)
  {
    *pu32ElapsedMs = CaptureElapsed;
  }
  return 1U;
}

#if APP_CAMERA_DIAGNOSTIC_ENABLE
/**
 * @brief Reads and logs the sensor registers used to diagnose exposure and DSP state.
 * @param[in] pcLabel Optional label included in the diagnostic output.
 */
static void AppCamera_DumpSensorState(const char * pcLabel)
{
  enum
  {
    E_APP_CAMERA_STATE_GAIN = 0,
    E_APP_CAMERA_STATE_BAVG,
    E_APP_CAMERA_STATE_GAVG,
    E_APP_CAMERA_STATE_RAVG,
    E_APP_CAMERA_STATE_AECH,
    E_APP_CAMERA_STATE_AEC,
    E_APP_CAMERA_STATE_YAVG,
    E_APP_CAMERA_STATE_COM2,
    E_APP_CAMERA_STATE_COM3,
    E_APP_CAMERA_STATE_COM4,
    E_APP_CAMERA_STATE_COM5,
    E_APP_CAMERA_STATE_COM6,
    E_APP_CAMERA_STATE_CLKRC,
    E_APP_CAMERA_STATE_COM7,
    E_APP_CAMERA_STATE_COM8,
    E_APP_CAMERA_STATE_COM9,
    E_APP_CAMERA_STATE_COM10,
    E_APP_CAMERA_STATE_COM12,
    E_APP_CAMERA_STATE_COM13,
    E_APP_CAMERA_STATE_FIXGAIN,
    E_APP_CAMERA_STATE_AWB0,
    E_APP_CAMERA_STATE_DSP1,
    E_APP_CAMERA_STATE_DSP2,
    E_APP_CAMERA_STATE_DSP3,
    E_APP_CAMERA_STATE_DSP4,
    E_APP_CAMERA_STATE_DSPAUTO,
    E_APP_CAMERA_STATE_COUNT
  };
  static const uint8_t Registers[E_APP_CAMERA_STATE_COUNT] =
  {
    0x00U, 0x05U, 0x06U, 0x07U, 0x08U, 0x10U, 0x2FU,
    0x09U, 0x0CU, 0x0DU, 0x0EU, 0x0FU, 0x11U, 0x12U,
    0x13U, 0x14U, 0x15U, 0x3DU, 0x3EU, 0x4DU, 0x63U,
    0x64U, 0x65U, 0x66U, 0x67U, 0xACU
  };
  OV7725M12_Status_t eStatus;
  uint8_t Values[E_APP_CAMERA_STATE_COUNT];
  uint32_t Index;
  const char * pcStateLabel = (pcLabel != NULL) ? pcLabel : "state";

  for (Index = 0U; Index < E_APP_CAMERA_STATE_COUNT; ++Index)
  {
    eStatus = OV7725M12_ReadRegister(&hi2c1, Registers[Index], &Values[Index]);
    if (eStatus != OV7725M12_STATUS_OK)
    {
      SEGGER_RTT_printf(0,
                        "[CAM] regs/%s read failed: reg=0x%02X status=%d I2C=0x%08lX\r\n",
                        pcStateLabel,
                        Registers[Index],
                        (int)eStatus,
                        (unsigned long)HAL_I2C_GetError(&hi2c1));
      return;
    }
  }

  SEGGER_RTT_printf(0,
                    "[CAM] regs/%s auto: GAIN=%02X EXP=%02X%02X AVG(B/G/R)=%02X/%02X/%02X YAVG=%02X COM8=%02X\r\n",
                    pcStateLabel,
                    Values[E_APP_CAMERA_STATE_GAIN],
                    Values[E_APP_CAMERA_STATE_AECH], Values[E_APP_CAMERA_STATE_AEC],
                    Values[E_APP_CAMERA_STATE_BAVG], Values[E_APP_CAMERA_STATE_GAVG],
                    Values[E_APP_CAMERA_STATE_RAVG], Values[E_APP_CAMERA_STATE_YAVG],
                    Values[E_APP_CAMERA_STATE_COM8]);
  SEGGER_RTT_printf(0,
                    "[CAM] regs/%s core: COM2=%02X COM3=%02X COM4=%02X COM5=%02X COM6=%02X CLKRC=%02X COM7=%02X COM9=%02X COM10=%02X\r\n",
                    pcStateLabel,
                    Values[E_APP_CAMERA_STATE_COM2], Values[E_APP_CAMERA_STATE_COM3],
                    Values[E_APP_CAMERA_STATE_COM4], Values[E_APP_CAMERA_STATE_COM5],
                    Values[E_APP_CAMERA_STATE_COM6], Values[E_APP_CAMERA_STATE_CLKRC],
                    Values[E_APP_CAMERA_STATE_COM7], Values[E_APP_CAMERA_STATE_COM9],
                    Values[E_APP_CAMERA_STATE_COM10]);
  SEGGER_RTT_printf(0,
                    "[CAM] regs/%s dsp: COM12=%02X COM13=%02X FIXGAIN=%02X AWB0=%02X DSP1=%02X DSP2=%02X DSP3=%02X DSP4=%02X AUTO=%02X\r\n",
                    pcStateLabel,
                    Values[E_APP_CAMERA_STATE_COM12], Values[E_APP_CAMERA_STATE_COM13],
                    Values[E_APP_CAMERA_STATE_FIXGAIN], Values[E_APP_CAMERA_STATE_AWB0],
                    Values[E_APP_CAMERA_STATE_DSP1], Values[E_APP_CAMERA_STATE_DSP2],
                    Values[E_APP_CAMERA_STATE_DSP3], Values[E_APP_CAMERA_STATE_DSP4],
                    Values[E_APP_CAMERA_STATE_DSPAUTO]);
}

/**
 * @brief Configures the sensor and DSP color-bar generators for diagnostics.
 * @param[in] u8SensorEnable Nonzero to enable the sensor color-bar generator.
 * @param[in] u8DspEnable Nonzero to enable the DSP color-bar generator.
 * @param[in] pcLabel Optional label included in the diagnostic output.
 * @retval 0 The requested color-bar configuration failed.
 * @retval 1 The requested color-bar configuration was applied.
 */
static uint8_t AppCamera_SetColorBarStages(uint8_t u8SensorEnable,
                                           uint8_t u8DspEnable,
                                           const char * pcLabel)
{
  OV7725M12_Status_t eStatus;
  const char * pcPatternLabel = (pcLabel != NULL) ? pcLabel : "pattern";

  eStatus = OV7725M12_SetColorBarStages(&hi2c1, u8SensorEnable, u8DspEnable);
  if (eStatus != OV7725M12_STATUS_OK)
  {
    SEGGER_RTT_printf(0,
                      "[CAM] colorbar/%s setup failed: status=%d I2C=0x%08lX\r\n",
                      pcPatternLabel,
                      (int)eStatus,
                      (unsigned long)HAL_I2C_GetError(&hi2c1));
    return 0U;
  }

  SEGGER_RTT_printf(0,
                    "[CAM] colorbar/%s configured: sensor=%u DSP=%u\r\n",
                    pcPatternLabel,
                    (unsigned int)(u8SensorEnable != 0U),
                    (unsigned int)(u8DspEnable != 0U));
  return 1U;
}

/**
 * @brief Captures one diagnostic frame and classifies its pixel activity.
 * @param[in] pcLabel Optional label included in the diagnostic output.
 * @return Classification of the captured diagnostic frame.
 */
static eAppCameraCaptureResultDef AppCamera_CaptureDiagnostic(const char * pcLabel)
{
  OV7725M12_FrameStats_t tFrameStats;
  uint32_t CaptureElapsed;
  uint8_t * pu8FrameBuffer;
  const char * pcCaptureLabel = (pcLabel != NULL) ? pcLabel : "snapshot";

  osDelay(100U);
  if (AppCamera_CaptureFrame(&CaptureElapsed) == 0U)
  {
    return E_APP_CAMERA_CAPTURE_FAILED;
  }

  OV7725M12_AnalyzeFrame(&tFrameStats);
  pu8FrameBuffer = OV7725M12_GetFrameBuffer();
  SEGGER_RTT_printf(0,
                    "[CAM] %s OK: %lu ms addr=0x%08lX bytes=%lu hash=0x%08lX\r\n",
                    pcCaptureLabel,
                    (unsigned long)CaptureElapsed,
                    (unsigned long)(uintptr_t)pu8FrameBuffer,
                    (unsigned long)OV7725M12_FRAME_SIZE_BYTES,
                    (unsigned long)tFrameStats.fnv1a);
  SEGGER_RTT_printf(0,
                    "[CAM] %s raw16 min=0x%04X max=0x%04X changes=%lu first=%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                    pcCaptureLabel,
                    tFrameStats.min_word,
                    tFrameStats.max_word,
                    (unsigned long)tFrameStats.adjacent_changes,
                    pu8FrameBuffer[0], pu8FrameBuffer[1],
                    pu8FrameBuffer[2], pu8FrameBuffer[3],
                    pu8FrameBuffer[4], pu8FrameBuffer[5],
                    pu8FrameBuffer[6], pu8FrameBuffer[7]);

  if (tFrameStats.min_word == tFrameStats.max_word)
  {
    SEGGER_RTT_printf(0,
                      "[CAM] %s warning: captured frame is uniform\r\n",
                      pcCaptureLabel);
    return E_APP_CAMERA_CAPTURE_UNIFORM;
  }

  return E_APP_CAMERA_CAPTURE_ACTIVE;
}

/**
 * @brief Runs the staged exposure and color-bar capture diagnostics.
 */
static void AppCamera_RunDiagnostic(void)
{
  OV7725M12_Status_t eCameraStatus;
  eAppCameraCaptureResultDef eBaselineResult;

  AppCamera_DumpSensorState("post-config");
  SEGGER_RTT_printf(0,
                    "[CAM] waiting %lu ms for AEC/AGC convergence\r\n",
                    (unsigned long)APP_CAMERA_SENSOR_SETTLE_MS);
  osDelay(APP_CAMERA_SENSOR_SETTLE_MS);
  AppCamera_DumpSensorState("settled");

  eBaselineResult = AppCamera_CaptureDiagnostic("live/settled-baseline");
  if (eBaselineResult == E_APP_CAMERA_CAPTURE_ACTIVE)
  {
    SEGGER_RTT_WriteString(0,
                           "[CAM] live image became active after AEC/AGC settling\r\n");
    return;
  }

  if (AppCamera_SetColorBarStages(0U, 1U, "DSP-only") != 0U)
  {
    (void)AppCamera_CaptureDiagnostic("colorbar/DSP-only");
  }
  if (AppCamera_SetColorBarStages(1U, 0U, "sensor-only") != 0U)
  {
    (void)AppCamera_CaptureDiagnostic("colorbar/sensor-only");
  }
  if (AppCamera_SetColorBarStages(1U, 1U, "both") != 0U)
  {
    (void)AppCamera_CaptureDiagnostic("colorbar/both");
  }

  eCameraStatus = OV7725M12_SetColorBarStages(&hi2c1, 0U, 0U);
  if (eCameraStatus != OV7725M12_STATUS_OK)
  {
    SEGGER_RTT_printf(0,
                      "[CAM] colorbar disable failed: status=%d I2C=0x%08lX\r\n",
                      (int)eCameraStatus,
                      (unsigned long)HAL_I2C_GetError(&hi2c1));
    return;
  }
  SEGGER_RTT_WriteString(0, "[CAM] colorbar disabled: sensor=0 DSP=0\r\n");
  SEGGER_RTT_printf(0,
                    "[CAM] waiting %lu ms after colorbar disable\r\n",
                    (unsigned long)APP_CAMERA_SENSOR_SETTLE_MS);
  osDelay(APP_CAMERA_SENSOR_SETTLE_MS);
  AppCamera_DumpSensorState("recovered");
  (void)AppCamera_CaptureDiagnostic("live/recovered");
}
#endif /* APP_CAMERA_DIAGNOSTIC_ENABLE */

/**
 * @brief Initializes camera preview state and SDRAM image descriptors.
 * @note Call this function before creating the camera and GUI tasks.
 */
void AppCamera_Init(void)
{
  uint8_t Index;

  ptPreviewImage = NULL;
  u8PreviewActiveIndex = APP_CAMERA_INDEX_NONE;
  u8PreviewPendingIndex = APP_CAMERA_INDEX_NONE;
  u8PreviewNextWriteIndex = 0U;

  for (Index = 0U; Index < APP_CAMERA_PREVIEW_BUFFER_COUNT; ++Index)
  {
    tPreviewDescriptors[Index].header.cf = LV_IMG_CF_TRUE_COLOR;
    tPreviewDescriptors[Index].header.always_zero = 0U;
    tPreviewDescriptors[Index].header.reserved = 0U;
    tPreviewDescriptors[Index].header.w = APP_CAMERA_PREVIEW_WIDTH;
    tPreviewDescriptors[Index].header.h = APP_CAMERA_PREVIEW_HEIGHT;
    tPreviewDescriptors[Index].data_size = APP_CAMERA_PREVIEW_SIZE_BYTES;
    tPreviewDescriptors[Index].data = AppCamera_GetPreviewBuffer(Index);
  }
}

/**
 * @brief Creates the LVGL objects used to display the camera preview.
 * @note Call this function only from the task that owns LVGL.
 */
void AppCamera_GuiInit(void)
{
  lv_obj_t * ptScreen = lv_scr_act();
#if APP_CAMERA_COLOR_COMPARE_ENABLE
  static const char * const ppcModeNames[4] = {"N", "R", "B", "X"};
  static const lv_coord_t ModeX[4] = {0, 80, 0, 80};
  static const lv_coord_t ModeY[4] = {0, 0, 40, 40};
  uint8_t ModeIndex;
#endif

  lv_obj_clean(ptScreen);
  lv_obj_set_style_bg_color(ptScreen, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(ptScreen, LV_OPA_COVER, LV_PART_MAIN);

  ptPreviewImage = lv_img_create(ptScreen);
  lv_obj_set_size(ptPreviewImage,
                  APP_CAMERA_PREVIEW_WIDTH,
                  APP_CAMERA_PREVIEW_HEIGHT);
  lv_obj_set_pos(ptPreviewImage, 0, 0);
  lv_obj_set_style_bg_color(ptPreviewImage, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(ptPreviewImage, LV_OPA_COVER, LV_PART_MAIN);

#if APP_CAMERA_COLOR_COMPARE_ENABLE
  for (ModeIndex = 0U; ModeIndex < 4U; ++ModeIndex)
  {
    ptPreviewModeLabels[ModeIndex] = lv_label_create(ptScreen);
    lv_label_set_text(ptPreviewModeLabels[ModeIndex], ppcModeNames[ModeIndex]);
    lv_obj_set_pos(ptPreviewModeLabels[ModeIndex],
                   ModeX[ModeIndex],
                   ModeY[ModeIndex]);
    lv_obj_set_style_text_color(ptPreviewModeLabels[ModeIndex],
                                lv_color_white(),
                                LV_PART_MAIN);
    lv_obj_set_style_bg_color(ptPreviewModeLabels[ModeIndex],
                              lv_color_black(),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ptPreviewModeLabels[ModeIndex],
                            LV_OPA_COVER,
                            LV_PART_MAIN);
    lv_obj_set_style_pad_all(ptPreviewModeLabels[ModeIndex], 0, LV_PART_MAIN);
  }
#endif

  AppCamera_GuiProcess();
}

/**
 * @brief Publishes a pending camera frame to the LVGL image object.
 * @note Call this function only from the task that owns LVGL.
 */
void AppCamera_GuiProcess(void)
{
  uint8_t PendingIndex;

  if (ptPreviewImage == NULL)
  {
    return;
  }

  taskENTER_CRITICAL();
  PendingIndex = u8PreviewPendingIndex;
  if (PendingIndex != APP_CAMERA_INDEX_NONE)
  {
    u8PreviewPendingIndex = APP_CAMERA_INDEX_NONE;
    u8PreviewActiveIndex = PendingIndex;
  }
  taskEXIT_CRITICAL();
  __DMB();

  if (PendingIndex != APP_CAMERA_INDEX_NONE)
  {
    lv_img_set_src(ptPreviewImage, &tPreviewDescriptors[PendingIndex]);
    lv_obj_invalidate(ptPreviewImage);
  }
}

/**
 * @brief Runs camera detection, configuration, capture, and preview production.
 * @param[in] pvArgument Optional RTOS task argument. The current implementation ignores it.
 */
void AppCamera_Task(void * pvArgument)
{
  OV7725M12_ID_t tCameraId;
  OV7725M12_Status_t eCameraStatus;
  uint8_t CameraFailedReg = 0xFFU;
  uint8_t FirstFrame = 1U;
  uint32_t CaptureElapsed;

  (void)pvArgument;

  eCameraStatus = OV7725M12_Probe(&hi2c1, &tCameraId);
  if (eCameraStatus != OV7725M12_STATUS_OK)
  {
    SEGGER_RTT_printf(0,
                      "[CAM] OV7725 probe failed: status=%d PID=0x%02X VER=0x%02X I2C=0x%08lX\r\n",
                      (int)eCameraStatus,
                      tCameraId.pid,
                      tCameraId.version,
                      (unsigned long)HAL_I2C_GetError(&hi2c1));
    osThreadExit();
    return;
  }

  SEGGER_RTT_printf(0, "[CAM] OV7725 detected: PID=0x%02X VER=0x%02X\r\n",
                    tCameraId.pid,
                    tCameraId.version);
  eCameraStatus = OV7725M12_ConfigureQVGA_RGB565(&hi2c1, &CameraFailedReg);
  if (eCameraStatus != OV7725M12_STATUS_OK)
  {
    SEGGER_RTT_printf(0,
                      "[CAM] configure failed: status=%d reg=0x%02X I2C=0x%08lX\r\n",
                      (int)eCameraStatus,
                      CameraFailedReg,
                      (unsigned long)HAL_I2C_GetError(&hi2c1));
    osThreadExit();
    return;
  }
  SEGGER_RTT_WriteString(0, "[CAM] configured: QVGA RGB565\r\n");

#if APP_CAMERA_COLOR_COMPARE_ENABLE
  SEGGER_RTT_WriteString(0,
                         "[CAM] color compare: N=none R=swap-RB B=swap-bytes X=both\r\n");
#endif

#if APP_CAMERA_DIAGNOSTIC_ENABLE
  AppCamera_RunDiagnostic();
#else
  SEGGER_RTT_printf(0,
                    "[CAM] waiting %lu ms for AEC/AGC convergence\r\n",
                    (unsigned long)APP_CAMERA_SENSOR_SETTLE_MS);
  osDelay(APP_CAMERA_SENSOR_SETTLE_MS);
#endif

  for (;;)
  {
    uint8_t WriteIndex;
    uint8_t * pu8FrameBuffer;

    if (AppCamera_CaptureFrame(&CaptureElapsed) == 0U)
    {
      osDelay(100U);
      continue;
    }

    WriteIndex = AppCamera_AcquireWriteBuffer();
    if (WriteIndex == APP_CAMERA_INDEX_NONE)
    {
      /* UI is still flushing the previous frames; keep the newest capture only. */
      osDelay(1U);
      continue;
    }

    pu8FrameBuffer = OV7725M12_GetFrameBuffer();
    AppCamera_CropAndDownsample(pu8FrameBuffer, AppCamera_GetPreviewBuffer(WriteIndex));
    AppCamera_PublishPreviewBuffer(WriteIndex);

    if (FirstFrame != 0U)
    {
      SEGGER_RTT_printf(0,
                        "[CAM] live preview started: %ux%u RGB565 capture=%lu ms\r\n",
                        APP_CAMERA_PREVIEW_WIDTH,
                        APP_CAMERA_PREVIEW_HEIGHT,
                        (unsigned long)CaptureElapsed);
      FirstFrame = 0U;
    }
  }
}
