#include "app_runtime.h"

#include "cmsis_os.h"
#include "cherryusb_app.h"
#include "file_rx.h"
#include "lcd.h"
#include "rtc.h"
#include "sd_manager.h"
#include "SEGGER_RTT.h"

#include <stdio.h>
#include <string.h>

#define CHERRYUSB_AUTO_START 1
#define LCD_STATUS_UPDATE_MS 500U

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

static void StartUsbTask(void *argument);
static void LED_Blink(uint32_t Hdelay, uint32_t Ldelay, uint8_t Mode);
static void RTC_CalendarShow(RTC_DateTypeDef *sdatestructureget, RTC_TimeTypeDef *stimestructureget);

void AppRuntime_CreateTasks(void)
{
  SdManager_Init();
  (void)osThreadNew(SdManager_Task, NULL, &sdManagerTask_attributes);

  FileRx_Init();
  (void)osThreadNew(FileRx_Task, NULL, &fileRxTask_attributes);

#if CHERRYUSB_AUTO_START
  (void)osThreadNew(StartUsbTask, NULL, &usbTask_attributes);
#endif
}

void AppRuntime_DefaultTask(void *argument)
{
  uint8_t text[32];
  char last_rx_text[32] = {0};
  uint32_t lcd_last_update = 0;
  RTC_DateTypeDef sdatestructureget;
  RTC_TimeTypeDef stimestructureget;

  (void)argument;

  LCD_Test();

  for (;;) {
    uint32_t now = osKernelGetTickCount();

    if ((now - lcd_last_update) >= LCD_STATUS_UPDATE_MS) {
      lcd_last_update = now;

      RTC_CalendarShow(&sdatestructureget, &stimestructureget);

      FileRx_GetStatusText((char *)text, sizeof(text));
      if (strcmp((char *)text, last_rx_text) != 0) {
        strncpy(last_rx_text, (char *)text, sizeof(last_rx_text) - 1U);
        last_rx_text[sizeof(last_rx_text) - 1U] = '\0';
        LCD_ShowString(4, 4, 156, 12, 12, text);
      }

      if (stimestructureget.Seconds % 2 == 1) {
        snprintf((char *)text, sizeof(text), "Time: %02d:%02d:%02d",
                 stimestructureget.Hours,
                 stimestructureget.Minutes,
                 stimestructureget.Seconds);
        LED_Blink(500, 500, 0);
      } else {
        snprintf((char *)text, sizeof(text), "Time: %02d %02d %02d",
                 stimestructureget.Hours,
                 stimestructureget.Minutes,
                 stimestructureget.Seconds);
        LED_Blink(500, 500, 1);
      }
      LCD_ShowString(4, 50, 156, 12, 12, text);

      {
        uint32_t total_kb;
        uint32_t free_kb;

        if (SdManager_GetCapacity(&total_kb, &free_kb) != 0U) {
          snprintf((char *)text, sizeof(text), "SD:%lu/%luMB      ",
                   (unsigned long)(free_kb / 1024U),
                   (unsigned long)(total_kb / 1024U));
        } else {
          snprintf((char *)text, sizeof(text), "SD:%-16s", SdManager_GetStateText());
        }
        LCD_ShowString(4, 64, 156, 12, 12, text);
      }
    }

    osDelay(10);
  }
}

static void StartUsbTask(void *argument)
{
  (void)argument;

  osDelay(100);
  SEGGER_RTT_WriteString(0, "[USB] CherryUSB init start\r\n");
  CherryUSB_DeviceInit();
  SEGGER_RTT_WriteString(0, "[USB] CherryUSB init done\r\n");
  osThreadExit();
}

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

static void RTC_CalendarShow(RTC_DateTypeDef *sdatestructureget, RTC_TimeTypeDef *stimestructureget)
{
  /* Read both time and date, otherwise RTC shadow registers may not unlock. */
  HAL_RTC_GetTime(&hrtc, stimestructureget, RTC_FORMAT_BIN);
  HAL_RTC_GetDate(&hrtc, sdatestructureget, RTC_FORMAT_BIN);
}
