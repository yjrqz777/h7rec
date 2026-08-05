/**
 * @file app_camera.h
 * @brief Declares OV7725 capture and LVGL preview interfaces.
 */

#ifndef __APP_CAMERA_H__
#define __APP_CAMERA_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifndef APP_CAMERA_DIAGNOSTIC_ENABLE
#define APP_CAMERA_DIAGNOSTIC_ENABLE (0U) /* Enable register dumps and color-bar diagnostics */
#endif

#ifndef APP_CAMERA_COLOR_COMPARE_ENABLE
#define APP_CAMERA_COLOR_COMPARE_ENABLE (0U) /* Show all four RGB565 interpretations */
#endif

#ifndef APP_CAMERA_SWAP_RED_BLUE
#define APP_CAMERA_SWAP_RED_BLUE (0U) /* Swap the RGB565 red and blue channels */
#endif

#ifndef APP_CAMERA_SWAP_BYTES
#define APP_CAMERA_SWAP_BYTES (1U) /* Swap captured RGB565 high and low bytes */
#endif

#define APP_CAMERA_CAPTURE_TIMEOUT_MS       (3000U) /* Maximum snapshot wait time */
#define APP_CAMERA_SENSOR_SETTLE_MS         (1500U) /* AEC and AGC settling time */
#define APP_CAMERA_PREVIEW_WIDTH            (160U)  /* Preview width in pixels */
#define APP_CAMERA_PREVIEW_HEIGHT           (80U)   /* Preview height in pixels */
#define APP_CAMERA_PREVIEW_BPP              (2U)    /* RGB565 bytes per pixel */
#define APP_CAMERA_PREVIEW_PIXEL_COUNT      (APP_CAMERA_PREVIEW_WIDTH * APP_CAMERA_PREVIEW_HEIGHT) /* Pixels in one preview buffer */
#define APP_CAMERA_PREVIEW_SIZE_BYTES       (APP_CAMERA_PREVIEW_PIXEL_COUNT * APP_CAMERA_PREVIEW_BPP) /* Bytes in one preview buffer */
#define APP_CAMERA_PREVIEW_BUFFER_COUNT     (3U)    /* Triple-buffer count */
#define APP_CAMERA_INDEX_NONE               (0xFFU) /* Invalid preview-buffer index */
#define APP_CAMERA_TRANSFORM_SWAP_BYTES     (0x01U) /* RGB565 byte-swap transform */
#define APP_CAMERA_TRANSFORM_SWAP_RED_BLUE  (0x02U) /* RGB565 red-blue transform */
#define APP_CAMERA_COMPARE_WIDTH            (APP_CAMERA_PREVIEW_WIDTH / 2U) /* Width of one comparison pane */
#define APP_CAMERA_COMPARE_HEIGHT           (APP_CAMERA_PREVIEW_HEIGHT / 2U) /* Height of one comparison pane */
#define APP_CAMERA_COMPARE_SAMPLE_STEP      (4U)    /* Source stride for comparison panes */

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
