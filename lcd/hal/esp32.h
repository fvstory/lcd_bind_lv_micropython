#ifndef _ESP32_H_
#define _ESP32_H_
#include "i80_panel.h"
#include "rgb_panel.h"

// i8080
void hal_lcd_i80_construct(mp_obj_base_t *self);

void hal_lcd_i80_tx_param(
    mp_obj_base_t *self,
    int lcd_cmd,
    const void *param,
    size_t param_size
);

void hal_lcd_i80_tx_color(
    mp_obj_base_t *self,
    int lcd_cmd,
    const void *color,
    size_t color_size
);

void hal_lcd_i80_deinit(mp_obj_base_t *self);

// rgb
#if RGB_LCD_SUPPORTED
void hal_lcd_rgb_construct(mp_lcd_rgb_obj_t *self);

void hal_lcd_rgb_reset(mp_lcd_rgb_obj_t *self);

void hal_lcd_rgb_init(mp_lcd_rgb_obj_t *self);

void hal_lcd_rgb_del(mp_lcd_rgb_obj_t *self);

void hal_lcd_rgb_bitmap(mp_lcd_rgb_obj_t *self, int x_start, int y_start, int x_end, int y_end, const void *color_data);

void hal_lcd_rgb_mirror(mp_lcd_rgb_obj_t *self, bool mirror_x, bool mirror_y);

void hal_lcd_rgb_swap_xy(mp_lcd_rgb_obj_t *self, bool swap_axes);

void hal_lcd_rgb_set_gap(mp_lcd_rgb_obj_t *self, int x_gap, int y_gap);

void hal_lcd_rgb_invert_color(mp_lcd_rgb_obj_t *self, bool invert_color_data);

void hal_lcd_rgb_disp_off(mp_lcd_rgb_obj_t *self, bool off);
#endif

#endif
