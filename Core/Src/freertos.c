/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "cherryusb_app.h"
#include "lcd.h"
#include "rtc.h"
#include "file_rx.h"
#include "sd_manager.h"
#include "SEGGER_RTT.h"
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define CHERRYUSB_AUTO_START 1
#define LCD_STATUS_UPDATE_MS 500U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
osThreadId_t usbTaskHandle;
const osThreadAttr_t usbTask_attributes = {
  .name = "usbTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

osThreadId_t fileRxTaskHandle;
const osThreadAttr_t fileRxTask_attributes = {
  .name = "fileRxTask",
  .stack_size = 1024 * 6,
  .priority = (osPriority_t) osPriorityNormal,
};

osThreadId_t sdManagerTaskHandle;
const osThreadAttr_t sdManagerTask_attributes = {
  .name = "sdManagerTask",
  .stack_size = 1024 * 5,
  .priority = (osPriority_t) osPriorityNormal,
};

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 1024 * 2,
  .priority = (osPriority_t) osPriorityLow,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void StartUsbTask(void *argument);

static void LED_Blink(uint32_t Hdelay,uint32_t Ldelay,uint8_t Mode)
{
	// if(Mode == 0){
	// 	HAL_GPIO_WritePin(RGB_R_GPIO_Port,RGB_R_Pin,GPIO_PIN_RESET);
	// 	HAL_Delay(Hdelay - 1);
	// 	HAL_GPIO_WritePin(RGB_R_GPIO_Port,RGB_R_Pin,GPIO_PIN_SET);
	// 	HAL_Delay(Ldelay - 1);
	// }
	// 	else if(Mode == 1){
	// 	HAL_GPIO_WritePin(RGB_G_GPIO_Port,RGB_G_Pin,GPIO_PIN_RESET);
	// 	HAL_Delay(Hdelay - 1);
	// 	HAL_GPIO_WritePin(RGB_G_GPIO_Port,RGB_G_Pin,GPIO_PIN_SET);
	// 	HAL_Delay(Ldelay - 1);
	// }
	// 	else	if(Mode == 2){
	// 	HAL_GPIO_WritePin(RGB_B_GPIO_Port,RGB_B_Pin,GPIO_PIN_RESET);
	// 	HAL_Delay(Hdelay - 1);
	// 	HAL_GPIO_WritePin(RGB_B_GPIO_Port,RGB_B_Pin,GPIO_PIN_SET);
	// 	HAL_Delay(Ldelay - 1);
	// }
	// 	else {
	// 	HAL_GPIO_WritePin(RGB_R_GPIO_Port,RGB_R_Pin|RGB_G_Pin|RGB_B_Pin,GPIO_PIN_RESET);
	// 	HAL_Delay(Hdelay - 1);
	// 	HAL_GPIO_WritePin(RGB_R_GPIO_Port,RGB_R_Pin|RGB_G_Pin|RGB_B_Pin,GPIO_PIN_SET);
	// 	HAL_Delay(Ldelay - 1);
	// }
}

static void RTC_CalendarShow(RTC_DateTypeDef *sdatestructureget,RTC_TimeTypeDef *stimestructureget)
{
  /* Read both time and date, otherwise RTC shadow registers may not unlock. */
  /* Both time and date must be obtained or RTC cannot be read next time */
  /* Get the RTC current Time */
  HAL_RTC_GetTime(&hrtc, stimestructureget, RTC_FORMAT_BIN);
  /* Get the RTC current Date */
  HAL_RTC_GetDate(&hrtc, sdatestructureget, RTC_FORMAT_BIN);
}
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  SdManager_Init();
  sdManagerTaskHandle = osThreadNew(SdManager_Task, NULL, &sdManagerTask_attributes);
  FileRx_Init();
  fileRxTaskHandle = osThreadNew(FileRx_Task, NULL, &fileRxTask_attributes);
#if CHERRYUSB_AUTO_START
  usbTaskHandle = osThreadNew(StartUsbTask, NULL, &usbTask_attributes);
#endif
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  uint8_t text[32];
  char last_rx_text[32] = {0};
  uint32_t lcd_last_update = 0;
	RTC_DateTypeDef sdatestructureget;
	RTC_TimeTypeDef stimestructureget;
  LCD_Test();
  /* Infinite loop */
  for(;;)
  {
    uint32_t now = osKernelGetTickCount();

    if ((now - lcd_last_update) >= LCD_STATUS_UPDATE_MS) {
      lcd_last_update = now;

      RTC_CalendarShow(&sdatestructureget,&stimestructureget);

      FileRx_GetStatusText((char *)text, sizeof(text));
      if (strcmp((char *)text, last_rx_text) != 0) {
        strncpy(last_rx_text, (char *)text, sizeof(last_rx_text) - 1U);
        last_rx_text[sizeof(last_rx_text) - 1U] = '\0';
        LCD_ShowString(4, 4, 156, 12, 12, text);
      }

      if (stimestructureget.Seconds % 2 == 1)
      {
        snprintf((char *)text, sizeof(text), "Time: %02d:%02d:%02d", stimestructureget.Hours, stimestructureget.Minutes, stimestructureget.Seconds);
        LED_Blink(500,500,0);
      }
      else
      {
        snprintf((char *)text, sizeof(text), "Time: %02d %02d %02d", stimestructureget.Hours, stimestructureget.Minutes, stimestructureget.Seconds);
        LED_Blink(500,500,1);
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
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
static void StartUsbTask(void *argument)
{
  (void)argument;

  osDelay(100);
  SEGGER_RTT_WriteString(0, "[USB] CherryUSB init start\r\n");
  CherryUSB_DeviceInit();
  SEGGER_RTT_WriteString(0, "[USB] CherryUSB init done\r\n");
  osThreadExit();
}

/* USER CODE END Application */

