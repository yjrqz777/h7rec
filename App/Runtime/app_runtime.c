#include "app_runtime.h"

/*
 * Application runtime layer.
 *
 * Keep CubeMX-generated freertos.c focused on RTOS setup, while this module owns
 * application task creation and the default task's LCD/status refresh loop.
 */

#include "cmsis_os.h"
#include "app_gui.h"
#include "cherryusb_app.h"
#include "dcmi.h"
#include "file_rx.h"
#include "i2c.h"
#include "lcd.h"
#include "ov7725m12.h"
#include "rtc.h"
#include "sd_manager.h"
#include "SEGGER_RTT.h"

#include <stdio.h>
#include <string.h>

#define CHERRYUSB_AUTO_START 1
#define LCD_STATUS_UPDATE_MS 500U
#define CAMERA_SNAPSHOT_TIMEOUT_MS 3000U
#define CAMERA_SENSOR_SETTLE_MS 1500U

typedef enum
{
  CAMERA_CAPTURE_FAILED = 0,
  CAMERA_CAPTURE_UNIFORM,
  CAMERA_CAPTURE_NONUNIFORM
} CameraCaptureResult_t;

static const osThreadAttr_t usbTask_attributes = {
  .name = "usbTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

static const osThreadAttr_t fileRxTask_attributes = {
  .name = "fileRxTask",
  .stack_size = 1024 * 6,
  .priority = (osPriority_t) osPriorityNormal,
};

static const osThreadAttr_t sdManagerTask_attributes = {
  .name = "sdManagerTask",
  .stack_size = 1024 * 5,
  .priority = (osPriority_t) osPriorityNormal,
};

static const osThreadAttr_t uiTask_attributes = {
  .name = "uiTask",
  .stack_size = 1024 * 32,
  .priority = (osPriority_t) osPriorityLow,
};

static void StartUsbTask(void *argument);
static void Camera_RunCaptureDiagnostic(void);
static void Camera_DumpSensorState(const char *label);
static uint8_t Camera_SetColorBarStages(uint8_t sensor_enable,
                                        uint8_t dsp_enable,
                                        const char *label);
static CameraCaptureResult_t Camera_CaptureSnapshot(const char *label);
static void LED_Blink(uint32_t Hdelay, uint32_t Ldelay, uint8_t Mode);
static void RTC_CalendarShow(RTC_DateTypeDef *sdatestructureget, RTC_TimeTypeDef *stimestructureget);

/**
 * @brief Create application worker tasks.
 *
 * Initializes task-local application modules before creating their RTOS tasks.
 * The USB startup task is controlled by CHERRYUSB_AUTO_START.
 */
void AppRuntime_CreateTasks(void)
{
  osThreadId_t thread_id;

  /* Create application-owned worker tasks after the CubeMX default task exists. */
  SdManager_Init();
  thread_id = osThreadNew(SdManager_Task, NULL, &sdManagerTask_attributes);
  if (thread_id == NULL) {
    SEGGER_RTT_WriteString(0, "[RTOS] create sdManagerTask failed\r\n");
  }

  FileRx_Init();
  thread_id = osThreadNew(FileRx_Task, NULL, &fileRxTask_attributes);
  if (thread_id == NULL) {
    SEGGER_RTT_WriteString(0, "[RTOS] create fileRxTask failed\r\n");
  }

  thread_id = osThreadNew(AppGui_Task, NULL, &uiTask_attributes);
  if (thread_id == NULL) {
    SEGGER_RTT_WriteString(0, "[RTOS] create uiTask failed\r\n");
  }

#if CHERRYUSB_AUTO_START
  thread_id = osThreadNew(StartUsbTask, NULL, &usbTask_attributes);
  if (thread_id == NULL) {
    SEGGER_RTT_WriteString(0, "[RTOS] create usbTask failed\r\n");
  }
#endif
}

/**
 * @brief Run the default LCD/status task.
 * @param argument CMSIS-RTOS task argument, currently unused.
 *
 * Shows the LCD splash/init sequence, then periodically refreshes receive
 * progress, RTC time, and SD card state/capacity.
 */
void AppRuntime_DefaultTask(void *argument)
{
  uint8_t text[32];
  char last_rx_text[32] = {0};
  uint32_t lcd_last_update = 0;
  uint8_t camera_failed_reg = 0xFFU;
  OV7725M12_ID_t camera_id;
  OV7725M12_Status_t camera_status;
  RTC_DateTypeDef sdatestructureget;
  RTC_TimeTypeDef stimestructureget;

  (void)argument;

  camera_status = OV7725M12_Probe(&hi2c1, &camera_id);
  if (camera_status == OV7725M12_STATUS_OK) {
    SEGGER_RTT_printf(0, "[CAM] OV7725 detected: PID=0x%02X VER=0x%02X\r\n",
                      camera_id.pid,
                      camera_id.version);
    camera_status = OV7725M12_ConfigureQVGA_RGB565(&hi2c1, &camera_failed_reg);
    if (camera_status == OV7725M12_STATUS_OK) {
      SEGGER_RTT_WriteString(0, "[CAM] configured: QVGA RGB565\r\n");
      Camera_RunCaptureDiagnostic();
    } else {
      SEGGER_RTT_printf(0,
                        "[CAM] configure failed: status=%d reg=0x%02X I2C=0x%08lX\r\n",
                        (int)camera_status,
                        camera_failed_reg,
                        (unsigned long)HAL_I2C_GetError(&hi2c1));
    }
  } else {
    SEGGER_RTT_printf(0,
                      "[CAM] OV7725 probe failed: status=%d PID=0x%02X VER=0x%02X I2C=0x%08lX\r\n",
                      (int)camera_status,
                      camera_id.pid,
                      camera_id.version,
                      (unsigned long)HAL_I2C_GetError(&hi2c1));
  }

  LCD_Test();

  for (;;) {
    // uint32_t now = osKernelGetTickCount();

    // /* LCD drawing is comparatively slow, so refresh status at a coarse cadence. */
    // if ((now - lcd_last_update) >= LCD_STATUS_UPDATE_MS) {
    //   lcd_last_update = now;

    //   RTC_CalendarShow(&sdatestructureget, &stimestructureget);

    //   FileRx_GetStatusText((char *)text, sizeof(text));
    //   if (strcmp((char *)text, last_rx_text) != 0) {
    //     strncpy(last_rx_text, (char *)text, sizeof(last_rx_text) - 1U);
    //     last_rx_text[sizeof(last_rx_text) - 1U] = '\0';
    //     LCD_ShowString(4, 4, 156, 12, 12, text);
    //   }

    //   if (stimestructureget.Seconds % 2 == 1) {
    //     snprintf((char *)text, sizeof(text), "Time: %02d:%02d:%02d",
    //              stimestructureget.Hours,
    //              stimestructureget.Minutes,
    //              stimestructureget.Seconds);
    //     LED_Blink(500, 500, 0);
    //   } else {
    //     snprintf((char *)text, sizeof(text), "Time: %02d %02d %02d",
    //              stimestructureget.Hours,
    //              stimestructureget.Minutes,
    //              stimestructureget.Seconds);
    //     LED_Blink(500, 500, 1);
    //   }
    //   LCD_ShowString(4, 50, 156, 12, 12, text);

    //   {
    //     uint32_t total_kb;
    //     uint32_t free_kb;

    //     /* Show capacity when the card is mounted, otherwise show the SD state. */
    //     if (SdManager_GetCapacity(&total_kb, &free_kb) != 0U) {
    //       snprintf((char *)text, sizeof(text), "SD:%lu/%luMB      ",
    //                (unsigned long)(free_kb / 1024U),
    //                (unsigned long)(total_kb / 1024U));
    //     } else {
    //       snprintf((char *)text, sizeof(text), "SD:%-16s", SdManager_GetStateText());
    //     }
    //     LCD_ShowString(4, 64, 156, 12, 12, text);
    //   }
    // }

    osDelay(10);
  }
}

static void Camera_RunCaptureDiagnostic(void)
{
  OV7725M12_Status_t camera_status;
  CameraCaptureResult_t baseline_result;

  Camera_DumpSensorState("post-config");
  SEGGER_RTT_printf(0,
                    "[CAM] waiting %lu ms for AEC/AGC convergence\r\n",
                    (unsigned long)CAMERA_SENSOR_SETTLE_MS);
  osDelay(CAMERA_SENSOR_SETTLE_MS);
  Camera_DumpSensorState("settled");

  baseline_result = Camera_CaptureSnapshot("live/settled-baseline");
  if (baseline_result == CAMERA_CAPTURE_NONUNIFORM)
  {
    SEGGER_RTT_WriteString(0,
                           "[CAM] live image became active after AEC/AGC settling\r\n");
    return;
  }

  if (Camera_SetColorBarStages(0U, 1U, "DSP-only") != 0U)
  {
    (void)Camera_CaptureSnapshot("colorbar/DSP-only");
  }

  if (Camera_SetColorBarStages(1U, 0U, "sensor-only") != 0U)
  {
    (void)Camera_CaptureSnapshot("colorbar/sensor-only");
  }

  if (Camera_SetColorBarStages(1U, 1U, "both") != 0U)
  {
    (void)Camera_CaptureSnapshot("colorbar/both");
  }

  camera_status = OV7725M12_SetColorBarStages(&hi2c1, 0U, 0U);
  if (camera_status != OV7725M12_STATUS_OK)
  {
    SEGGER_RTT_printf(0,
                      "[CAM] colorbar disable failed: status=%d I2C=0x%08lX\r\n",
                      (int)camera_status,
                      (unsigned long)HAL_I2C_GetError(&hi2c1));
    return;
  }
  SEGGER_RTT_WriteString(0,
                         "[CAM] colorbar disabled: sensor=0 DSP=0\r\n");

  SEGGER_RTT_printf(0,
                    "[CAM] waiting %lu ms after colorbar disable\r\n",
                    (unsigned long)CAMERA_SENSOR_SETTLE_MS);
  osDelay(CAMERA_SENSOR_SETTLE_MS);
  Camera_DumpSensorState("recovered");
  (void)Camera_CaptureSnapshot("live/recovered");
}

static void Camera_DumpSensorState(const char *label)
{
  enum
  {
    CAM_STATE_GAIN = 0,
    CAM_STATE_BAVG,
    CAM_STATE_GAVG,
    CAM_STATE_RAVG,
    CAM_STATE_AECH,
    CAM_STATE_AEC,
    CAM_STATE_YAVG,
    CAM_STATE_COM2,
    CAM_STATE_COM3,
    CAM_STATE_COM4,
    CAM_STATE_COM5,
    CAM_STATE_COM6,
    CAM_STATE_CLKRC,
    CAM_STATE_COM7,
    CAM_STATE_COM8,
    CAM_STATE_COM9,
    CAM_STATE_COM10,
    CAM_STATE_COM12,
    CAM_STATE_COM13,
    CAM_STATE_FIXGAIN,
    CAM_STATE_AWB0,
    CAM_STATE_DSP1,
    CAM_STATE_DSP2,
    CAM_STATE_DSP3,
    CAM_STATE_DSP4,
    CAM_STATE_DSPAUTO,
    CAM_STATE_COUNT
  };
  static const uint8_t registers[CAM_STATE_COUNT] =
  {
    0x00U, 0x05U, 0x06U, 0x07U, 0x08U, 0x10U, 0x2FU,
    0x09U, 0x0CU, 0x0DU, 0x0EU, 0x0FU, 0x11U, 0x12U,
    0x13U, 0x14U, 0x15U, 0x3DU, 0x3EU, 0x4DU, 0x63U,
    0x64U, 0x65U, 0x66U, 0x67U, 0xACU
  };
  OV7725M12_Status_t status;
  uint8_t values[CAM_STATE_COUNT];
  uint32_t index;
  const char *state_label = (label != NULL) ? label : "state";

  for (index = 0U; index < CAM_STATE_COUNT; ++index)
  {
    status = OV7725M12_ReadRegister(&hi2c1,
                                     registers[index],
                                     &values[index]);
    if (status != OV7725M12_STATUS_OK)
    {
      SEGGER_RTT_printf(0,
                        "[CAM] regs/%s read failed: reg=0x%02X status=%d I2C=0x%08lX\r\n",
                        state_label,
                        registers[index],
                        (int)status,
                        (unsigned long)HAL_I2C_GetError(&hi2c1));
      return;
    }
  }

  SEGGER_RTT_printf(0,
                    "[CAM] regs/%s auto: GAIN=%02X EXP=%02X%02X AVG(B/G/R)=%02X/%02X/%02X YAVG=%02X COM8=%02X\r\n",
                    state_label,
                    values[CAM_STATE_GAIN],
                    values[CAM_STATE_AECH], values[CAM_STATE_AEC],
                    values[CAM_STATE_BAVG], values[CAM_STATE_GAVG],
                    values[CAM_STATE_RAVG], values[CAM_STATE_YAVG],
                    values[CAM_STATE_COM8]);
  SEGGER_RTT_printf(0,
                    "[CAM] regs/%s core: COM2=%02X COM3=%02X COM4=%02X COM5=%02X COM6=%02X CLKRC=%02X COM7=%02X COM9=%02X COM10=%02X\r\n",
                    state_label,
                    values[CAM_STATE_COM2], values[CAM_STATE_COM3],
                    values[CAM_STATE_COM4], values[CAM_STATE_COM5],
                    values[CAM_STATE_COM6], values[CAM_STATE_CLKRC],
                    values[CAM_STATE_COM7], values[CAM_STATE_COM9],
                    values[CAM_STATE_COM10]);
  SEGGER_RTT_printf(0,
                    "[CAM] regs/%s dsp: COM12=%02X COM13=%02X FIXGAIN=%02X AWB0=%02X DSP1=%02X DSP2=%02X DSP3=%02X DSP4=%02X AUTO=%02X\r\n",
                    state_label,
                    values[CAM_STATE_COM12], values[CAM_STATE_COM13],
                    values[CAM_STATE_FIXGAIN], values[CAM_STATE_AWB0],
                    values[CAM_STATE_DSP1], values[CAM_STATE_DSP2],
                    values[CAM_STATE_DSP3], values[CAM_STATE_DSP4],
                    values[CAM_STATE_DSPAUTO]);
}

static uint8_t Camera_SetColorBarStages(uint8_t sensor_enable,
                                        uint8_t dsp_enable,
                                        const char *label)
{
  OV7725M12_Status_t status;
  const char *pattern_label = (label != NULL) ? label : "pattern";

  status = OV7725M12_SetColorBarStages(&hi2c1,
                                        sensor_enable,
                                        dsp_enable);
  if (status != OV7725M12_STATUS_OK)
  {
    SEGGER_RTT_printf(0,
                      "[CAM] colorbar/%s setup failed: status=%d I2C=0x%08lX\r\n",
                      pattern_label,
                      (int)status,
                      (unsigned long)HAL_I2C_GetError(&hi2c1));
    return 0U;
  }

  SEGGER_RTT_printf(0,
                    "[CAM] colorbar/%s configured: sensor=%u DSP=%u\r\n",
                    pattern_label,
                    (unsigned int)(sensor_enable != 0U),
                    (unsigned int)(dsp_enable != 0U));
  return 1U;
}

static CameraCaptureResult_t Camera_CaptureSnapshot(const char *label)
{
  OV7725M12_CaptureState_t capture_state;
  OV7725M12_FrameStats_t frame_stats;
  HAL_StatusTypeDef hal_status;
  HAL_StatusTypeDef stop_status;
  uint8_t *frame_buffer;
  uint32_t capture_start;
  uint32_t capture_elapsed;
  uint32_t dcmi_error = HAL_DCMI_ERROR_NONE;
  uint32_t dma_error;
  uint32_t dma_remaining;
  uint32_t dcmi_risr;
  const char *capture_label = (label != NULL) ? label : "snapshot";

  /* Allow sensor register changes to settle before arming DCMI. */
  osDelay(100U);

  hal_status = OV7725M12_StartSnapshot(&hdcmi);
  if (hal_status != HAL_OK)
  {
    SEGGER_RTT_printf(0,
                      "[CAM] %s start failed: HAL=%d DCMI=0x%08lX DMA=0x%08lX\r\n",
                      capture_label,
                      (int)hal_status,
                      (unsigned long)hdcmi.ErrorCode,
                      (unsigned long)hdcmi.DMA_Handle->ErrorCode);
    return CAMERA_CAPTURE_FAILED;
  }

  capture_start = osKernelGetTickCount();
  do
  {
    capture_state = OV7725M12_GetCaptureState(&dcmi_error);
    if ((capture_state != OV7725M12_CAPTURE_BUSY) ||
        (hdcmi.ErrorCode != HAL_DCMI_ERROR_NONE) ||
        (hdcmi.DMA_Handle->ErrorCode != HAL_DMA_ERROR_NONE))
    {
      break;
    }
    osDelay(1U);
  } while ((osKernelGetTickCount() - capture_start) < CAMERA_SNAPSHOT_TIMEOUT_MS);

  capture_elapsed = osKernelGetTickCount() - capture_start;
  capture_state = OV7725M12_GetCaptureState(&dcmi_error);
  dma_remaining = __HAL_DMA_GET_COUNTER(hdcmi.DMA_Handle);
  dma_error = hdcmi.DMA_Handle->ErrorCode;
  if (hdcmi.ErrorCode != HAL_DCMI_ERROR_NONE)
  {
    dcmi_error = hdcmi.ErrorCode;
  }
  dcmi_risr = hdcmi.Instance->RISR;
  stop_status = OV7725M12_StopSnapshot(&hdcmi);
  if (stop_status != HAL_OK)
  {
    dcmi_error |= hdcmi.ErrorCode;
  }

  if ((capture_state == OV7725M12_CAPTURE_BUSY) &&
      (dcmi_error == HAL_DCMI_ERROR_NONE) &&
      (dma_error == HAL_DMA_ERROR_NONE))
  {
    SEGGER_RTT_printf(0,
                      "[CAM] %s timeout: %lu ms left=%lu RISR=0x%08lX DCMI=0x%08lX DMA=0x%08lX stop=%d\r\n",
                      capture_label,
                      (unsigned long)capture_elapsed,
                      (unsigned long)dma_remaining,
                      (unsigned long)dcmi_risr,
                      (unsigned long)dcmi_error,
                      (unsigned long)dma_error,
                      (int)stop_status);
    return CAMERA_CAPTURE_FAILED;
  }

  if ((capture_state != OV7725M12_CAPTURE_COMPLETE) ||
      (dcmi_error != HAL_DCMI_ERROR_NONE) ||
      (dma_error != HAL_DMA_ERROR_NONE) ||
      (dma_remaining != 0U) ||
      (stop_status != HAL_OK))
  {
    SEGGER_RTT_printf(0,
                      "[CAM] %s error: %lu ms DCMI=0x%08lX DMA=0x%08lX left=%lu stop=%d\r\n",
                      capture_label,
                      (unsigned long)capture_elapsed,
                      (unsigned long)dcmi_error,
                      (unsigned long)dma_error,
                      (unsigned long)dma_remaining,
                      (int)stop_status);
    return CAMERA_CAPTURE_FAILED;
  }

  OV7725M12_PrepareFrameForCpu();
  OV7725M12_AnalyzeFrame(&frame_stats);
  frame_buffer = OV7725M12_GetFrameBuffer();

  SEGGER_RTT_printf(0,
                    "[CAM] %s OK: %lu ms addr=0x%08lX bytes=%lu hash=0x%08lX\r\n",
                    capture_label,
                    (unsigned long)capture_elapsed,
                    (unsigned long)(uintptr_t)frame_buffer,
                    (unsigned long)OV7725M12_FRAME_SIZE_BYTES,
                    (unsigned long)frame_stats.fnv1a);
  SEGGER_RTT_printf(0,
                    "[CAM] %s raw16 min=0x%04X max=0x%04X changes=%lu first=%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                    capture_label,
                    frame_stats.min_word,
                    frame_stats.max_word,
                    (unsigned long)frame_stats.adjacent_changes,
                    frame_buffer[0], frame_buffer[1], frame_buffer[2], frame_buffer[3],
                    frame_buffer[4], frame_buffer[5], frame_buffer[6], frame_buffer[7]);

  if (frame_stats.min_word == frame_stats.max_word)
  {
    SEGGER_RTT_printf(0,
                      "[CAM] %s warning: captured frame is uniform\r\n",
                      capture_label);
    return CAMERA_CAPTURE_UNIFORM;
  }

  return CAMERA_CAPTURE_NONUNIFORM;
}

/**
 * @brief Delayed CherryUSB initialization task.
 * @param argument CMSIS-RTOS task argument, currently unused.
 *
 * Starts CherryUSB once after a short delay, writes RTT status messages, then
 * exits the task.
 */
static void StartUsbTask(void *argument)
{
  (void)argument;

  /* Give board peripherals and the scheduler a short settle time before USB init. */
  osDelay(100);
  SEGGER_RTT_WriteString(0, "[USB] CherryUSB init start\r\n");
  CherryUSB_DeviceInit();
  SEGGER_RTT_WriteString(0, "[USB] CherryUSB init done\r\n");
  osThreadExit();
}

/**
 * @brief Placeholder for RGB LED blink feedback.
 * @param Hdelay LED on-time in milliseconds.
 * @param Ldelay LED off-time in milliseconds.
 * @param Mode LED color/mode selector.
 *
 * The GPIO implementation is currently commented out because the active board
 * wiring is not enabled here. The parameters are kept for the existing call
 * sites and for easy restoration.
 */
static void LED_Blink(uint32_t Hdelay, uint32_t Ldelay, uint8_t Mode)
{
  (void)Hdelay;
  (void)Ldelay;
  (void)Mode;

  // if (Mode == 0) {
  //   HAL_GPIO_WritePin(RGB_R_GPIO_Port, RGB_R_Pin, GPIO_PIN_RESET);
  //   HAL_Delay(Hdelay - 1);
  //   HAL_GPIO_WritePin(RGB_R_GPIO_Port, RGB_R_Pin, GPIO_PIN_SET);
  //   HAL_Delay(Ldelay - 1);
  // } else if (Mode == 1) {
  //   HAL_GPIO_WritePin(RGB_G_GPIO_Port, RGB_G_Pin, GPIO_PIN_RESET);
  //   HAL_Delay(Hdelay - 1);
  //   HAL_GPIO_WritePin(RGB_G_GPIO_Port, RGB_G_Pin, GPIO_PIN_SET);
  //   HAL_Delay(Ldelay - 1);
  // } else if (Mode == 2) {
  //   HAL_GPIO_WritePin(RGB_B_GPIO_Port, RGB_B_Pin, GPIO_PIN_RESET);
  //   HAL_Delay(Hdelay - 1);
  //   HAL_GPIO_WritePin(RGB_B_GPIO_Port, RGB_B_Pin, GPIO_PIN_SET);
  //   HAL_Delay(Ldelay - 1);
  // } else {
  //   HAL_GPIO_WritePin(RGB_R_GPIO_Port, RGB_R_Pin | RGB_G_Pin | RGB_B_Pin, GPIO_PIN_RESET);
  //   HAL_Delay(Hdelay - 1);
  //   HAL_GPIO_WritePin(RGB_R_GPIO_Port, RGB_R_Pin | RGB_G_Pin | RGB_B_Pin, GPIO_PIN_SET);
  //   HAL_Delay(Ldelay - 1);
  // }
}

/**
 * @brief Read current RTC date and time.
 * @param sdatestructureget Destination date structure.
 * @param stimestructureget Destination time structure.
 *
 * STM32 RTC shadow registers require reading date after time to unlock the next
 * read sequence.
 */
static void RTC_CalendarShow(RTC_DateTypeDef *sdatestructureget, RTC_TimeTypeDef *stimestructureget)
{
  /* Read both time and date, otherwise RTC shadow registers may not unlock. */
  HAL_RTC_GetTime(&hrtc, stimestructureget, RTC_FORMAT_BIN);
  HAL_RTC_GetDate(&hrtc, sdatestructureget, RTC_FORMAT_BIN);
}
