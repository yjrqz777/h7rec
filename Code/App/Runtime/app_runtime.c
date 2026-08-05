#include "app_runtime.h"

#include "app_camera.h"
#include "app_gui.h"
#include "cherryusb_app.h"
#include "file_rx.h"
#include "sd_manager.h"
#include "SEGGER_RTT.h"

#include "cmsis_os2.h"

#define CHERRYUSB_AUTO_START 1

static const osThreadAttr_t usbTask_attributes = {
  .name = "usbTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t)osPriorityLow,
};

static const osThreadAttr_t fileRxTask_attributes = {
  .name = "fileRxTask",
  .stack_size = 1024 * 6,
  .priority = (osPriority_t)osPriorityNormal,
};

static const osThreadAttr_t sdManagerTask_attributes = {
  .name = "sdManagerTask",
  .stack_size = 1024 * 5,
  .priority = (osPriority_t)osPriorityNormal,
};

static const osThreadAttr_t uiTask_attributes = {
  .name = "uiTask",
  .stack_size = 1024 * 32,
  .priority = (osPriority_t)osPriorityLow,
};

static const osThreadAttr_t cameraTask_attributes = {
  .name = "cameraTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t)osPriorityNormal,
};

static void StartUsbTask(void *argument);

void AppRuntime_CreateTasks(void)
{
  osThreadId_t thread_id;

  SdManager_Init();
  thread_id = osThreadNew(SdManager_Task, NULL, &sdManagerTask_attributes);
  if (thread_id == NULL)
  {
    SEGGER_RTT_WriteString(0, "[RTOS] create sdManagerTask failed\r\n");
  }

  FileRx_Init();
  thread_id = osThreadNew(FileRx_Task, NULL, &fileRxTask_attributes);
  if (thread_id == NULL)
  {
    SEGGER_RTT_WriteString(0, "[RTOS] create fileRxTask failed\r\n");
  }

  AppCamera_Init();
  thread_id = osThreadNew(AppGui_Task, NULL, &uiTask_attributes);
  if (thread_id == NULL)
  {
    SEGGER_RTT_WriteString(0, "[RTOS] create uiTask failed\r\n");
  }

  thread_id = osThreadNew(AppCamera_Task, NULL, &cameraTask_attributes);
  if (thread_id == NULL)
  {
    SEGGER_RTT_WriteString(0, "[RTOS] create cameraTask failed\r\n");
  }

#if CHERRYUSB_AUTO_START
  thread_id = osThreadNew(StartUsbTask, NULL, &usbTask_attributes);
  if (thread_id == NULL)
  {
    SEGGER_RTT_WriteString(0, "[RTOS] create usbTask failed\r\n");
  }
#endif
}

void AppRuntime_DefaultTask(void *argument)
{
  (void)argument;

  /* LCD and camera ownership belongs to uiTask/cameraTask. */
  for (;;)
  {
    osDelay(1000U);
  }
}

static void StartUsbTask(void *argument)
{
  (void)argument;

  osDelay(100U);
  SEGGER_RTT_WriteString(0, "[USB] CherryUSB init start\r\n");
  CherryUSB_DeviceInit();
  SEGGER_RTT_WriteString(0, "[USB] CherryUSB init done\r\n");
  osThreadExit();
}
