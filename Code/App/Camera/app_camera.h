/**
 * @file app_camera.h
 * @brief 声明 OV7725 图像采集与 LVGL 预览接口。
 */

#ifndef __APP_CAMERA_H__
#define __APP_CAMERA_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifndef APP_CAMERA_DIAGNOSTIC_ENABLE
#define APP_CAMERA_DIAGNOSTIC_ENABLE (0U) /* 启用寄存器输出和彩条诊断 */
#endif

#ifndef APP_CAMERA_COLOR_COMPARE_ENABLE
#define APP_CAMERA_COLOR_COMPARE_ENABLE (0U) /* 同时显示四种 RGB565 解析方式 */
#endif

#ifndef APP_CAMERA_SWAP_RED_BLUE
#define APP_CAMERA_SWAP_RED_BLUE (0U) /* 交换 RGB565 的红蓝通道 */
#endif

#ifndef APP_CAMERA_SWAP_BYTES
#define APP_CAMERA_SWAP_BYTES (1U) /* 交换采集到的 RGB565 高低字节 */
#endif

#define APP_CAMERA_CAPTURE_TIMEOUT_MS       (3000U) /* 单帧采集最长等待时间 */
#define APP_CAMERA_SENSOR_SETTLE_MS         (1500U) /* AEC 和 AGC 稳定等待时间 */
#define APP_CAMERA_PREVIEW_WIDTH            (160U)  /* 预览图像宽度，单位：像素 */
#define APP_CAMERA_PREVIEW_HEIGHT           (80U)   /* 预览图像高度，单位：像素 */
#define APP_CAMERA_PREVIEW_BPP              (2U)    /* RGB565 每像素字节数 */
#define APP_CAMERA_PREVIEW_PIXEL_COUNT      (APP_CAMERA_PREVIEW_WIDTH * APP_CAMERA_PREVIEW_HEIGHT) /* 单个预览缓冲区的像素数 */
#define APP_CAMERA_PREVIEW_SIZE_BYTES       (APP_CAMERA_PREVIEW_PIXEL_COUNT * APP_CAMERA_PREVIEW_BPP) /* 单个预览缓冲区的字节数 */
#define APP_CAMERA_PREVIEW_BUFFER_COUNT     (3U)    /* 预览三缓冲数量 */
#define APP_CAMERA_INDEX_NONE               (0xFFU) /* 无效预览缓冲区索引 */
#define APP_CAMERA_TRANSFORM_SWAP_BYTES     (0x01U) /* RGB565 高低字节交换标志 */
#define APP_CAMERA_TRANSFORM_SWAP_RED_BLUE  (0x02U) /* RGB565 红蓝通道交换标志 */
#define APP_CAMERA_COMPARE_WIDTH            (APP_CAMERA_PREVIEW_WIDTH / 2U) /* 单个对比区域宽度 */
#define APP_CAMERA_COMPARE_HEIGHT           (APP_CAMERA_PREVIEW_HEIGHT / 2U) /* 单个对比区域高度 */
#define APP_CAMERA_COMPARE_SAMPLE_STEP      (4U)    /* 对比区域的源图像采样步长 */

#if ((APP_CAMERA_DIAGNOSTIC_ENABLE != 0) && \
     (APP_CAMERA_DIAGNOSTIC_ENABLE != 1))
#error "APP_CAMERA_DIAGNOSTIC_ENABLE must be 0 or 1"
#endif

#if ((APP_CAMERA_SWAP_RED_BLUE != 0) && (APP_CAMERA_SWAP_RED_BLUE != 1))
#error "APP_CAMERA_SWAP_RED_BLUE must be 0 or 1"
#endif

#if ((APP_CAMERA_SWAP_BYTES != 0) && (APP_CAMERA_SWAP_BYTES != 1))
#error "APP_CAMERA_SWAP_BYTES must be 0 or 1"
#endif

#if ((APP_CAMERA_COLOR_COMPARE_ENABLE != 0) && \
     (APP_CAMERA_COLOR_COMPARE_ENABLE != 1))
#error "APP_CAMERA_COLOR_COMPARE_ENABLE must be 0 or 1"
#endif

void AppCamera_Init(void);

void AppCamera_GuiInit(void);

void AppCamera_GuiProcess(void);

void AppCamera_Task(void * pvArgument);

#ifdef __cplusplus
}
#endif

#endif /* __APP_CAMERA_H__ */
