#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create application worker tasks.
 *
 * Creates the SD manager, file receiver, GUI, camera, and optional USB task.
 * Call this from MX_FREERTOS_Init() after the CubeMX default task is created.
 */
void AppRuntime_CreateTasks(void);

/**
 * @brief Run the default application task loop.
 * @param argument CMSIS-RTOS task argument, currently unused.
 *
 * The worker tasks own the active application functions; this task remains as
 * the CubeMX-compatible idle entry point and does not return.
 */
void AppRuntime_DefaultTask(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* APP_RUNTIME_H */
