#ifndef LCD_I80_H_
#define LCD_I80_H_

#include "mphalport.h"
#include "py/obj.h"
#if USE_ESP_LCD
#include "esp_lcd_panel_io.h"
#include "hal/lcd_types.h"
#endif
#include "lvgl/lvgl.h"

typedef struct mp_lcd_i80_obj_t {
    mp_obj_base_t base;
    
    uint16_t width;
    uint16_t height;

    mp_obj_t databus[8];
    mp_obj_t dc;
    mp_obj_t wr;
    mp_obj_t rd;
    mp_obj_t reset;
    mp_obj_t cs;
    bool flushdown;
    lv_disp_drv_t *disp_drv_obj;
    // mp_obj_t backlight;
    /*
    mp_hal_pin_obj_t databus_pins[8];
    mp_hal_pin_obj_t dc_pin;
    mp_hal_pin_obj_t wr_pin;
    mp_hal_pin_obj_t rd_pin;
    mp_hal_pin_obj_t reset_pin;
    mp_hal_pin_obj_t cs_pin;
    mp_hal_pin_obj_t backlight_pin;
    */
    uint32_t pclk;
    size_t bus_width;
    bool swap_color_bytes;
    int cmd_bits;
    int param_bits;
    size_t queue_size;
    
#if USE_ESP_LCD
    esp_lcd_i80_bus_handle_t i80_bus;
    esp_lcd_panel_io_handle_t io_handle;
#else
    void (*write_color)(mp_hal_pin_obj_t *databus, mp_hal_pin_obj_t wr, const uint8_t *buf, int len);
#endif
    enum {
        MP_HW_LCD_STATE_NONE,
        MP_HW_LCD_STATE_INIT,
        MP_HW_LCD_STATE_DEINIT
    } state;
} mp_lcd_i80_obj_t;

// I8080 protocol
typedef struct _mp_lcd_i80_p_t {
    void (*tx_param)(mp_obj_base_t *self, int lcd_cmd, const void *param, size_t param_size);
    void (*tx_color)(mp_obj_base_t *self, int lcd_cmd, const void *color, size_t color_size);
} mp_lcd_i80_p_t;

extern const mp_obj_type_t mp_lcd_i80_type;

#endif

/////////////////////////////////////////////////////////////////////////////////////////////
// lvgl flush and ISR in C
//
// disp_drv->user_data should be a dict that contains dc and spi, setup by micropython.
// like this: "self.disp_drv.user_data = {'dc': self.dc, 'spi': self.spi, 'dt': display_type}"

void lvgl_flush(void *disp_drv, const void *area, void *color_p);
