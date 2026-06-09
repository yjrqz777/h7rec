#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

void AppRuntime_CreateTasks(void);
void AppRuntime_DefaultTask(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* APP_RUNTIME_H */
