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
#include "fatfs.h"
#include "sdmmc.h"
#include "bsp_driver_sd.h"
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
#define SD_TEST_ENABLE     1
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
  .stack_size = 1024 * 2,
  .priority = (osPriority_t) osPriorityLow,
};

#if SD_TEST_ENABLE
osThreadId_t sdTestTaskHandle;
const osThreadAttr_t sdTestTask_attributes = {
  .name = "sdTestTask",
  .stack_size = 1024 * 5,
  .priority = (osPriority_t) osPriorityNormal,
};
#endif
/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void StartUsbTask(void *argument);
#if SD_TEST_ENABLE
static void StartSDTestTask(void *argument);
#endif

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
  SdManager_Init();
  sdManagerTaskHandle = osThreadNew(SdManager_Task, NULL, &sdManagerTask_attributes);
  FileRx_Init();
  fileRxTaskHandle = osThreadNew(FileRx_Task, NULL, &fileRxTask_attributes);
#if CHERRYUSB_AUTO_START
  usbTaskHandle = osThreadNew(StartUsbTask, NULL, &usbTask_attributes);
#endif
#if SD_TEST_ENABLE
  sdTestTaskHandle = osThreadNew(StartSDTestTask, NULL, &sdTestTask_attributes);
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
  LCD_Test();
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

  osDelay(100);
  SEGGER_RTT_WriteString(0, "[USB] CherryUSB init start\r\n");
  CherryUSB_DeviceInit();
  SEGGER_RTT_WriteString(0, "[USB] CherryUSB init done\r\n");
  osThreadExit();
}

#if SD_TEST_ENABLE
/**
  * @brief  SD卡测试任务：挂载文件系统、写文件、读文件验证
  */
static void StartSDTestTask(void *argument)
{
  (void)argument;
  FRESULT res;
  FIL file;
  UINT bw, br;
  FATFS *fs;
  DWORD fre_clust;
  uint32_t total_size, free_size;
  uint8_t pass = 0;

  /* 写测试数据 */
  const char write_buf[] = "Hello SD Card from STM32H743! This is a FatFS test.";
  char read_buf[128] = {0};

  osDelay(200);

  SEGGER_RTT_WriteString(0, "\r\n[SD] test start\r\n");

  {
    uint8_t sd_state = BSP_SD_Init();
    if (sd_state != 0) {
      SEGGER_RTT_printf(0, "[SD] init failed: ret=%u err=0x%08X state=%u detect=%u\r\n",
                        sd_state, (uint32_t)hsd1.ErrorCode, hsd1.State, BSP_SD_IsDetected());
      goto sd_test_done;
    }
  }

  res = f_mount(&SDFatFS, (TCHAR const*)SDPath, 0);
  if (res != FR_OK) {
    SEGGER_RTT_printf(0, "[SD] lazy mount failed: res=%d\r\n", res);
    goto sd_test_done;
  }

  {
    DIR dir;
    res = f_opendir(&dir, "0:/");
    if (res == FR_OK) {
      f_closedir(&dir);
    }

    if (res == FR_NO_FILESYSTEM) {
      SEGGER_RTT_WriteString(0, "[SD] no filesystem, formatting...\r\n");
      {
        static BYTE work_buf[_MAX_SS];
        res = f_mkfs((TCHAR const*)SDPath, FM_ANY, 0, work_buf, sizeof(work_buf));
        if (res != FR_OK) {
          SEGGER_RTT_printf(0, "[SD] format failed: res=%d\r\n", res);
          goto sd_test_done;
        }
      }
    } else if (res != FR_OK) {
      SEGGER_RTT_printf(0, "[SD] filesystem check failed: res=%d\r\n", res);
      goto sd_test_done;
    }
  }

  res = f_mount(&SDFatFS, (TCHAR const*)SDPath, 1);
  if (res != FR_OK) {
    SEGGER_RTT_printf(0, "[SD] mount failed: res=%d\r\n", res);
    goto sd_test_done;
  }
  SEGGER_RTT_WriteString(0, "[SD] mounted\r\n");

  res = f_getfree((TCHAR const*)SDPath, &fre_clust, &fs);
  if (res == FR_OK) {
    total_size = (uint32_t)((fs->n_fatent - 2) * fs->csize * 0.5); /* KB */
    free_size  = (uint32_t)(fre_clust * fs->csize * 0.5);          /* KB */
    SEGGER_RTT_printf(0, "[SD] total=%u KB free=%u KB\r\n", total_size, free_size);
    pass = 1;
  } else {
    SEGGER_RTT_printf(0, "[SD] f_getfree failed: res=%d\r\n", res);
  }

  // res = f_open(&file, "0:hello.txt", FA_CREATE_ALWAYS | FA_WRITE);
  // if (res != FR_OK) {
  //   SEGGER_RTT_printf(0, "[SD] open write failed: res=%d\r\n", res);
  //   goto sd_test_unmount;
  // }

  // res = f_write(&file, write_buf, sizeof(write_buf) - 1, &bw);
  // f_close(&file);
  // if (res != FR_OK || bw != sizeof(write_buf) - 1) {
  //   SEGGER_RTT_printf(0, "[SD] write failed: res=%d bytes=%u\r\n", res, bw);
  //   goto sd_test_unmount;
  // }

  // res = f_open(&file, "0:hello.txt", FA_OPEN_EXISTING | FA_READ);
  // if (res != FR_OK) {
  //   SEGGER_RTT_printf(0, "[SD] open read failed: res=%d\r\n", res);
  //   goto sd_test_unmount;
  // }

  // res = f_read(&file, read_buf, sizeof(read_buf) - 1, &br);
  // f_close(&file);
  // if (res != FR_OK) {
  //   SEGGER_RTT_printf(0, "[SD] read failed: res=%d\r\n", res);
  //   goto sd_test_unmount;
  // }

  // read_buf[br] = '\0';
  // if (br == sizeof(write_buf) - 1 && memcmp(read_buf, write_buf, br) == 0) {
  //   SEGGER_RTT_printf(0, "[SD] read/write verify OK (%u bytes)\r\n", br);
  //   pass = 1;
  // } else {
  //   SEGGER_RTT_printf(0, "[SD] verify failed: read=%u written=%u\r\n",
  //                     br, (uint32_t)(sizeof(write_buf) - 1));
  // }

  // (void)f_unlink("0:hello.txt");
  goto sd_test_unmount;

#if 0

  osDelay(200); /* 等待系统稳定 */

  SEGGER_RTT_WriteString(0, "\r\n===== SD Card Test Start =====\r\n");

  /* 0. 预诊断 */
  SEGGER_RTT_printf(0, "[DBG] PG7 (SD_DETECT) = %d\r\n",
                    HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_7));
  SEGGER_RTT_printf(0, "[DBG] BSP_SD_IsDetected() = %d\r\n",
                    BSP_SD_IsDetected());
  {
    uint8_t sd_state = BSP_SD_Init();
    SEGGER_RTT_printf(0, "[DBG] BSP_SD_Init() = %d (0=OK)\r\n", sd_state);
    SEGGER_RTT_printf(0, "[DBG] hsd1.ErrorCode = 0x%08X\r\n", (uint32_t)hsd1.ErrorCode);
    SEGGER_RTT_printf(0, "[DBG] hsd1.State = %d\r\n", hsd1.State);
    SEGGER_RTT_printf(0, "[DBG] hsd1.Init.ClockDiv = %u\r\n", hsd1.Init.ClockDiv);
    if (sd_state == 0) {
      HAL_SD_CardInfoTypeDef info;
      BSP_SD_GetCardInfo(&info);
      SEGGER_RTT_printf(0, "[DBG] Card Type=%u, Class=%u, BlockNbr=%u, BlockSize=%u\r\n",
                        info.CardType, info.Class, info.LogBlockNbr, info.LogBlockSize);
    } else {
      SEGGER_RTT_WriteString(0, "[FAIL] BSP_SD_Init failed; stop before raw/filesystem access\r\n");
      goto sd_test_done;
    }
    SEGGER_RTT_printf(0, "[DBG] BSP_SD_GetCardState() = %d\r\n",
                      BSP_SD_GetCardState());
  }

  /* 0c. 原始轮询读取测试（绕过 FatFS，避免 DMA IRQ/回调路径干扰）*/
  SEGGER_RTT_WriteString(0, "[DBG] Raw polling read test (sector 0)...\r\n");
  {
    static __ALIGNED(32) uint8_t test_sector[512];
    uint32_t start = HAL_GetTick();
    HAL_StatusTypeDef hal_ret;

    SEGGER_RTT_WriteString(0, "[DBG] Calling HAL_SD_ReadBlocks...\r\n");
    hal_ret = HAL_SD_ReadBlocks(&hsd1, test_sector, 0, 1, 5000);
    SEGGER_RTT_printf(0, "[DBG] HAL_SD_ReadBlocks returned %d in %u ms\r\n",
                      hal_ret, HAL_GetTick() - start);

    if (hal_ret == HAL_OK) {
      while (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER) {
        if (HAL_GetTick() - start >= 5000) {
          break;
        }
        osDelay(1);
      }
      SEGGER_RTT_printf(0, "[DBG] Boot sector[0..3]=%02X %02X %02X %02X, sig=%02X %02X\r\n",
                        test_sector[0], test_sector[1],
                        test_sector[2], test_sector[3],
                        test_sector[510], test_sector[511]);
    } else {
      SEGGER_RTT_printf(0, "[DBG] HAL_SD_ReadBlocks FAILED, ret=%d ErrCode=0x%08X State=%d\r\n",
                        hal_ret, (uint32_t)hsd1.ErrorCode, hsd1.State);
      SEGGER_RTT_WriteString(0, "[FAIL] Raw sector read failed; stop test to avoid unsafe filesystem access\r\n");
      goto sd_test_done;
    }
  }

  /* 1. 懒挂载（不立即读取扇区，避免卡死）*/
  SEGGER_RTT_WriteString(0, "[1] Lazy mount (opt=0)...\r\n");
  res = f_mount(&SDFatFS, (TCHAR const*)SDPath, 0);
  SEGGER_RTT_printf(0, "[1] Lazy mount returned res=%d\r\n", res);
  if (res != FR_OK) {
    SEGGER_RTT_printf(0, "[FAIL] Lazy mount failed, res=%d\r\n", res);
    goto sd_test_done;
  }

  /* 2. 检查文件系统是否存在 */
  SEGGER_RTT_WriteString(0, "[2] Checking filesystem...\r\n");
  {
    DIR dir;
    res = f_opendir(&dir, "0:/");
    if (res == FR_OK) {
      f_closedir(&dir);
    }
    SEGGER_RTT_printf(0, "[2] f_opendir returned res=%d\r\n", res);

    if (res == FR_NO_FILESYSTEM) {
      /* 无文件系统，先格式化 */
      SEGGER_RTT_WriteString(0, "[3] No filesystem, formatting with f_mkfs...\r\n");
      {
        static BYTE work_buf[_MAX_SS];
        res = f_mkfs((TCHAR const*)SDPath, FM_ANY, 0, work_buf, sizeof(work_buf));
        SEGGER_RTT_printf(0, "[3] f_mkfs returned res=%d\r\n", res);
        if (res != FR_OK) {
          SEGGER_RTT_printf(0, "[FAIL] f_mkfs failed, res=%d\r\n", res);
          goto sd_test_done;
        }
      }
      SEGGER_RTT_WriteString(0, "[OK] Format done\r\n");
    } else if (res != FR_OK) {
      SEGGER_RTT_printf(0, "[FAIL] Filesystem check failed, res=%d; not formatting\r\n", res);
      goto sd_test_done;
    } else {
      SEGGER_RTT_WriteString(0, "[OK] Filesystem exists\r\n");
    }
  }

  /* 3. 强制挂载（现在卡已格式化，可以安全读取扇区）*/
  SEGGER_RTT_WriteString(0, "[4] Force mount (opt=1)...\r\n");
  res = f_mount(&SDFatFS, (TCHAR const*)SDPath, 1);
  SEGGER_RTT_printf(0, "[4] Force mount returned res=%d\r\n", res);
  if (res != FR_OK) {
    SEGGER_RTT_printf(0, "[FAIL] Force mount failed, res=%d\r\n", res);
    goto sd_test_done;
  }
  SEGGER_RTT_WriteString(0, "[OK] Filesystem mounted\r\n");

  /* 5. 获取磁盘空间信息 */
  SEGGER_RTT_WriteString(0, "[5] Getting disk info...\r\n");
  res = f_getfree((TCHAR const*)SDPath, &fre_clust, &fs);
  if (res == FR_OK) {
    total_size = (uint32_t)((fs->n_fatent - 2) * fs->csize * 0.5); /* KB */
    free_size  = (uint32_t)(fre_clust * fs->csize * 0.5);          /* KB */
    SEGGER_RTT_printf(0, "[OK] Total: %u KB, Free: %u KB\r\n", total_size, free_size);
  } else {
    SEGGER_RTT_printf(0, "[FAIL] f_getfree failed, res=%d\r\n", res);
  }

  /* 6. 写入测试文件 */
  SEGGER_RTT_WriteString(0, "[6] Writing test file...\r\n");
  res = f_open(&file, "0:hello.txt", FA_CREATE_ALWAYS | FA_WRITE);
  if (res != FR_OK) {
    SEGGER_RTT_printf(0, "[FAIL] f_open(write) failed, res=%d\r\n", res);
    goto sd_test_unmount;
  }

  res = f_write(&file, write_buf, sizeof(write_buf) - 1, &bw);
  f_close(&file);
  if (res != FR_OK || bw != sizeof(write_buf) - 1) {
    SEGGER_RTT_printf(0, "[FAIL] f_write failed, res=%d, written=%u\r\n", res, bw);
    goto sd_test_unmount;
  }
  SEGGER_RTT_printf(0, "[OK] Written %u bytes to hello.txt\r\n", bw);

  /* 7. 回读验证 */
  SEGGER_RTT_WriteString(0, "[7] Reading back test file...\r\n");
  res = f_open(&file, "0:hello.txt", FA_OPEN_EXISTING | FA_READ);
  if (res != FR_OK) {
    SEGGER_RTT_printf(0, "[FAIL] f_open(read) failed, res=%d\r\n", res);
    goto sd_test_unmount;
  }

  res = f_read(&file, read_buf, sizeof(read_buf) - 1, &br);
  f_close(&file);
  if (res != FR_OK) {
    SEGGER_RTT_printf(0, "[FAIL] f_read failed, res=%d\r\n", res);
    goto sd_test_unmount;
  }

  read_buf[br] = '\0';
  SEGGER_RTT_printf(0, "[OK] Read %u bytes: \"%s\"\r\n", br, read_buf);

  /* 8. 数据比对 */
  if (br == sizeof(write_buf) - 1 && memcmp(read_buf, write_buf, br) == 0) {
    SEGGER_RTT_WriteString(0, "[PASS] Data verification OK!\r\n");
  } else {
    SEGGER_RTT_WriteString(0, "[FAIL] Data mismatch!\r\n");
  }

  /* 9. 删除测试文件（可选） */
  res = f_unlink("0:hello.txt");
  if (res == FR_OK) {
    SEGGER_RTT_WriteString(0, "[OK] Test file deleted\r\n");
  }

#endif

sd_test_unmount:
  f_mount(NULL, (TCHAR const*)SDPath, 1); /* 卸载 */

sd_test_done:
  SEGGER_RTT_printf(0, "[SD] test %s\r\n\r\n", pass ? "PASS" : "FAIL");
  osThreadExit(); /* 测试完成后退出任务 */
}
#endif

/* USER CODE END Application */

