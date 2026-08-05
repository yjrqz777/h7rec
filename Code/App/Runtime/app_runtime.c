/**
 * @file app_runtime.c
 * @brief 创建应用工作任务并协调默认任务与各功能任务的职责。
 */

#include "app_runtime.h"

#include "app_camera.h"
#include "app_gui.h"
#include "cherryusb_app.h"
#include "file_rx.h"
#include "sd_manager.h"
#include "SEGGER_RTT.h"

#include "cmsis_os2.h"

#define CHERRYUSB_AUTO_START 1 /* 非零值表示启动独立的 CherryUSB 初始化任务 */

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

/**
 * @brief 初始化各应用模块并创建对应的 CMSIS-RTOS 工作任务。
 * @note 应在 RTOS 内核初始化完成后调用；单个任务创建失败只输出日志，不阻止其他任务创建。
 */
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

/**
 * @brief 运行 CubeMX 保留的默认任务空闲循环。
 * @param[in] argument CMSIS-RTOS 任务参数，当前实现不使用该参数。
 * @note LCD 和摄像头分别由 uiTask 与 cameraTask 管理，本任务不访问这些外设。
 */
void AppRuntime_DefaultTask(void *argument)
{
  (void)argument;

  /* LCD 和摄像头的所有权分别属于 uiTask 和 cameraTask。 */
  for (;;)
  {
    osDelay(1000U);
  }
}

/**
 * @brief 延时等待系统稳定后初始化 CherryUSB 设备并结束一次性任务。
 * @param[in] argument CMSIS-RTOS 任务参数，当前实现不使用该参数。
 */
static void StartUsbTask(void *argument)
{
  (void)argument;

  osDelay(100U);
  SEGGER_RTT_WriteString(0, "[USB] CherryUSB init start\r\n");
  CherryUSB_DeviceInit();
  SEGGER_RTT_WriteString(0, "[USB] CherryUSB init done\r\n");
  osThreadExit();
}
