#include "app_gui.h"
#include "lcd.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_fs.h"

#include "FreeRTOS.h"
#include "cmsis_os2.h"
// #include "cmsis_os.h"
static void btn_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn = lv_event_get_target(e);
    if(code == LV_EVENT_CLICKED) {
        static uint8_t cnt = 0;
        cnt++;

        /*Get the first child of the button which is the label and change its text*/
        lv_obj_t * label = lv_obj_get_child(btn, 0);
        lv_label_set_text_fmt(label, "Button: %d", cnt);
    }
}

/**
 * Create a button with a label and react on click event.
 */
void lv_example_get_started_1(void)
{
    lv_obj_t * btn = lv_btn_create(lv_scr_act());     /*Add a button the current screen*/
    lv_obj_set_pos(btn, 10, 10);                            /*Set its position*/
    lv_obj_set_size(btn, 120, 50);                          /*Set its size*/
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_ALL, NULL);           /*Assign a callback to the button*/

    lv_obj_t * label = lv_label_create(btn);          /*Add a label to the button*/
    lv_label_set_text(label, "Button");                     /*Set the labels text*/
    lv_obj_center(label);
}


static void gif_zoom_anim_cb(void * obj, int32_t zoom)
{
    lv_img_set_zoom((lv_obj_t *)obj, zoom);
}

void ShowGifZoomAnim(void)
{
    lv_obj_t * gif = lv_gif_create(lv_scr_act());
    lv_gif_set_src(gif, "P:/rx/miao.gif");

    /* 120x120 图片中心 */
    lv_img_set_pivot(gif, 60, 60);

    /* 让最终 80x80 居中显示在 160x80 屏幕 */
    lv_obj_set_pos(gif, 20, -20);
    /* 显示尺寸 = 原始尺寸 * zoom / 256 */
    lv_img_set_zoom(gif, 8);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, gif);
    lv_anim_set_exec_cb(&a, gif_zoom_anim_cb);
    lv_anim_set_values(&a, 8, 270);
    lv_anim_set_time(&a, 1500);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);

    lv_anim_set_playback_time(&a, 2000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}


void AppGui_Task(void *argument)
{
    (void)argument;
    LCD_Test();
    lv_init();
    lv_port_disp_init();
    lv_port_fs_init();

    // lv_example_get_started_1();
    ShowGifZoomAnim();

    for (;;) {
        lv_task_handler();
        osDelay(5);
    }
}
