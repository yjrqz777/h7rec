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
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define CHERRYUSB_AUTO_START 1
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

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
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
  /* 必须同时获取时间和日期 不然会导致下次RTC不能读取 */
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
  	uint8_t text[20];
	RTC_DateTypeDef sdatestructureget;
	RTC_TimeTypeDef stimestructureget;
  /* Infinite loop */
  for(;;)
  {
		RTC_CalendarShow(&sdatestructureget,&stimestructureget);
		
		if (stimestructureget.Seconds % 2 == 1)
		{
			sprintf((char *)&text,"Time: %02d:%02d", stimestructureget.Hours, stimestructureget.Minutes);
			LED_Blink(500,500,0);
		}
		else
		{
			sprintf((char *)&text,"Time: %02d %02d", stimestructureget.Hours, stimestructureget.Minutes);
			LED_Blink(500,500,1);
		}
		LCD_ShowString(4, 58, 160, 16, 16, text);
		
		sprintf((char *)&text,"Tick: %d ms",HAL_GetTick());
		LCD_ShowString(4, 74, 160, 16, 16,text);

    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
static void StartUsbTask(void *argument)
{
  (void)argument;

  // osDelay(3000);
  // CherryUSB_DeviceInit();
  osThreadExit();
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;
  (void)pcTaskName;

  taskDISABLE_INTERRUPTS();
  for (;;) {
  }
}

/* USER CODE END Application */

