#include "remote_ui.h"
#include "remote_link.h"
#include "rc_audio.h"
#include "rc_network.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdio.h>

static lv_obj_t *connection, *audio_label, *video_label, *speed_label, *car_label, *image;
static lv_image_dsc_t frame;
static uint32_t last_video, frames, last_frames, last_fps_ms;
static unsigned fps;
static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, const lv_font_t *font) {
    lv_obj_t *l = lv_label_create(parent); lv_label_set_text(l, text); lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_font(l, font, 0); lv_obj_set_style_text_color(l, lv_color_hex(0xe3eaf4), 0); return l;
}
static void direction_event(lv_event_t *e) {
    unsigned direction = (unsigned)(uintptr_t)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) { if (!direction) remote_release(); else remote_press(direction, true); }
    else if (code == LV_EVENT_PRESSING && direction) remote_press(direction, false);
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) remote_release();
}
static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *b = lv_button_create(parent); lv_obj_set_pos(b, x, y); lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0); lv_obj_set_style_radius(b, 18, 0);
    lv_obj_set_style_shadow_width(b, 0, 0); lv_obj_remove_flag(b, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_t *l = label(b, text, 0, 0, &lv_font_montserrat_24); lv_obj_center(l); return b;
}
static void audio_event(lv_event_t *e) { (void)e; remote_audio_toggle(); }
static void speed_event(lv_event_t *e) { remote_speed(lv_slider_get_value(lv_event_get_target(e))); }
static void update(lv_timer_t *timer) {
    (void)timer; remote_state_t s = remote_state(); rc_audio_stats_t a = rc_audio_stats();
    uint32_t now = rc_now_ms();
    lv_label_set_text_fmt(connection, "%s   %lu ms", s.connected ? "CONNECTED" : "CONNECTING", (unsigned long)s.rtt_ms);
    lv_obj_set_style_text_color(connection, lv_color_hex(s.connected ? 0x56ddb0 : 0xffb760), 0);
    lv_label_set_text(audio_label, !a.ready || !s.audio_ready ? "Audio unavailable" : s.audio_enabled ? "Full duplex  ON" : "Audio  OFF");
    lv_label_set_text_fmt(speed_label, "Speed  %u%%", s.speed);
    lv_label_set_text_fmt(car_label, s.motor_ready ? "Wheels   L %d   R %d" : "Motor adapter offline", s.left, s.right);
    if (now - last_fps_ms >= 1000) { fps = (frames - last_frames) * 1000 / (now - last_fps_ms); last_frames = frames; last_fps_ms = now; }
    bool fresh = frames && now - last_video < 1500;
    lv_label_set_text_fmt(video_label, fresh ? "LIVE   %u FPS" : "Waiting for camera", fps);
    if (fresh) lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
}
esp_err_t remote_ui_start(void) {
    // The BSP default keeps a 100 KiB drawing buffer in internal RAM.
    // P4 can draw from DMA-capable PSRAM; preserve internal RAM for AEC and tasks.
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = false,
        .hw_cfg = {.hdmi_resolution = BSP_HDMI_RES_NONE,
            .dsi_bus = {.phy_clk_src = 0, .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS}},
        .flags = {.buff_dma = true, .buff_spiram = true, .sw_rotate = false}
    };
    lv_display_t *display = bsp_display_start_with_config(&cfg);
    if (!display) return ESP_FAIL;
    frame.header.magic = LV_IMAGE_HEADER_MAGIC; frame.header.cf = LV_COLOR_FORMAT_RGB565;
    frame.header.w = 320; frame.header.h = 240; frame.header.stride = 640;
    frame.data_size = 320 * 240 * 2;
    frame.data = heap_caps_malloc(frame.data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!frame.data) return ESP_ERR_NO_MEM;
    memset((void *)frame.data, 0, frame.data_size);
    if (!bsp_display_lock(1000)) return ESP_ERR_TIMEOUT;
    lv_obj_t *root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_hex(0x0b1220), 0); lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    label(root, "ROVER / LIVE CONTROL", 20, 18, &lv_font_montserrat_24);
    connection = label(root, "CONNECTING", 688, 23, &lv_font_montserrat_24);
    lv_obj_t *panel = lv_obj_create(root); lv_obj_set_pos(panel, 16, 72); lv_obj_set_size(panel, 656, 480);
    lv_obj_set_style_pad_all(panel, 0, 0); lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x121e30), 0); lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    image = lv_image_create(panel); lv_image_set_src(image, &frame); lv_image_set_scale(image, 512); lv_obj_center(image);
    video_label = label(panel, "Waiting for camera", 16, 14, &lv_font_montserrat_24);
    lv_obj_t *audio = button(root, "Full duplex  ON", 696, 88, 300, 72, 0x176958);
    audio_label = lv_obj_get_child(audio, 0); lv_obj_add_event_cb(audio, audio_event, LV_EVENT_CLICKED, NULL);
    struct { const char *text; unsigned dir; int x, y; } buttons[] = {
        {LV_SYMBOL_UP, 1, 798, 192}, {LV_SYMBOL_DOWN, 2, 798, 396},
        {LV_SYMBOL_LEFT, 3, 696, 294}, {LV_SYMBOL_RIGHT, 4, 900, 294}, {"STOP", 0, 798, 294}
    };
    for (unsigned i = 0; i < 5; ++i) {
        lv_obj_t *b = button(root, buttons[i].text, buttons[i].x, buttons[i].y, 96, 96, buttons[i].dir ? 0x253c5a : 0xb83249);
        lv_obj_add_event_cb(b, direction_event, LV_EVENT_ALL, (void *)(uintptr_t)buttons[i].dir);
    }
    speed_label = label(root, "Speed  35%", 696, 508, &lv_font_montserrat_24);
    lv_obj_t *slider = lv_slider_create(root); lv_obj_set_pos(slider, 710, 564); lv_obj_set_size(slider, 270, 12);
    lv_slider_set_range(slider, 0, 100); lv_slider_set_value(slider, 35, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, speed_event, LV_EVENT_VALUE_CHANGED, NULL);
    car_label = label(root, "Motor adapter offline", 20, 568, LV_FONT_DEFAULT);
    label(root, "Hold arrows to drive. Release to stop.", 302, 568, LV_FONT_DEFAULT);
    lv_timer_create(update, 100, NULL); bsp_display_unlock();
    return bsp_display_backlight_on();
}
void remote_ui_video(const uint8_t *rgb565, uint32_t bytes) {
    if (bytes != frame.data_size || !frame.data || !bsp_display_lock(100)) return;
    // Copy only under the LVGL lock. The decoder owns a separate buffer, avoiding render races.
    memcpy((void *)frame.data, rgb565, bytes); lv_image_cache_drop(&frame); lv_obj_invalidate(image);
    last_video = rc_now_ms(); ++frames; bsp_display_unlock();
}
