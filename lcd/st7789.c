#include "i80_panel.h"
#include "st7789.h"

#include "py/obj.h"
#include "py/runtime.h"
#include "mphalport.h"
#include "py/gc.h"

#include <string.h>


#define COLOR_SPACE_RGB        (0)
#define COLOR_SPACE_BGR        (1)
#define COLOR_SPACE_MONOCHROME (2)

#define DEBUG_printf(...)  //mp_printf(&mp_plat_print, __VA_ARGS__);


typedef struct _st7789_rotation_t {
    uint8_t madctl;
    uint16_t width;
    uint16_t height;
    uint16_t colstart;
    uint16_t rowstart;
} st7789_rotation_t;

// this is the actual C-structure for our new object
typedef struct mp_lcd_st7789_obj_t {
    mp_obj_base_t base;
    // mp_obj_t bus_obj;
    mp_obj_base_t *bus_obj;
    mp_obj_base_t *disp_drv;
    mp_obj_t reset;
    mp_obj_t backlight;
    bool reset_level;
    uint8_t color_space;

    mp_obj_t tx_param_method[2];
    mp_obj_t tx_color_method[2];
    
    uint16_t width;
    uint16_t height;
    uint8_t rotation;
    const st7789_rotation_t *rotations;   // list of rotation tuples [(madctl, colstart, rowstart)]
    int x_gap;
    int y_gap;
    uint32_t bits_per_pixel;
    uint8_t madctl_val; // save current value of LCD_CMD_MADCTL register
    uint8_t colmod_cal; // save surrent value of LCD_CMD_COLMOD register
} mp_lcd_st7789_obj_t;


//
// Default st7789 and st7735 display orientation tables
// can be overridden during init(), madctl values
// will be combined with color_mode
//

//{ madctl, width, height, colstart, rowstart }

STATIC const st7789_rotation_t ORIENTATIONS_240x320[4] = {
    { 0x00, 240, 320, 0, 0},
    { 0x60, 320, 240, 0, 0},
    { 0xc0, 240, 320, 0, 0},
    { 0xa0, 320, 240, 0, 0}
};

STATIC const st7789_rotation_t ORIENTATIONS_170x320[4] = {
    {0x00, 170, 320, 35, 0},
    {0x60, 320, 170, 0, 35},
    {0xc0, 170, 320, 35, 0},
    {0xa0, 320, 170, 0, 35}
};

STATIC const st7789_rotation_t ORIENTATIONS_240x240[4] = {
    {0x00, 240, 240,  0,  0},
    {0x60, 240, 240,  0,  0},
    {0xc0, 240, 240,  0, 80},
    {0xa0, 240, 240, 80,  0}
};

STATIC const st7789_rotation_t ORIENTATIONS_135x240[4] = {
    {0x00, 135, 240, 52, 40},
    {0x60, 240, 135, 40, 53},
    {0xc0, 135, 240, 53, 40},
    {0xa0, 240, 135, 40, 52}
};

STATIC const st7789_rotation_t ORIENTATIONS_128x160[4] = {
    {0x00, 128, 160, 0, 0},
    {0x60, 160, 128, 0, 0},
    {0xc0, 128, 160, 0, 0},
    {0xa0, 160, 128, 0, 0}
};

STATIC const st7789_rotation_t ORIENTATIONS_128x128[4] = {
    {0x00, 128, 128, 2, 1},
    {0x60, 128, 128, 1, 2},
    {0xc0, 128, 128, 2, 3},
    {0xa0, 128, 128, 3, 2}
};


STATIC const char* color_space_description[] = {
    "RGB",
    "BGR",
    "MONOCHROME"
};


STATIC void set_rotation(mp_lcd_st7789_obj_t *self, uint8_t rotation)
{

    if (self->rotations != NULL) {
        self->madctl_val |= self->rotations[rotation].madctl;

        // tx param
	if (mp_obj_is_type(self->bus_obj, &mp_lcd_i80_type)) {
	    #ifdef MP_OBJ_TYPE_GET_SLOT
            mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)MP_OBJ_TYPE_GET_SLOT(self->bus_obj->type, protocol);
            #else
            mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)self->bus_obj->type->protocol;
            #endif
            i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0x36, (uint8_t[]) {
                self->madctl_val,
            }, 1);
        }
        self->width = self->rotations[rotation].width;
        self->height = self->rotations[rotation].height;
        self->x_gap = self->rotations[rotation].colstart;
        self->y_gap = self->rotations[rotation].rowstart;
    } else {
        mp_warning(NULL, "rotation method is not supported");
        mp_warning(NULL, "Please use mirror method and swap_xy method to rotate the screen");
    }
}


STATIC void mp_lcd_st7789_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    (void) kind;
    mp_lcd_st7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<ST7789 bus=%p, reset=%p, color_space=%s, bits_per_pixel=%u>",
              self->bus_obj,
              self->reset,
              color_space_description[self->color_space],
              self->bits_per_pixel);
}


mp_obj_t mp_lcd_st7789_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t * all_args)
{
    enum {
        ARG_bus,
        ARG_disp_drv,
        ARG_reset,
        ARG_backlight,
        ARG_reset_level,
        ARG_color_space,
        ARG_bits_per_pixel
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_bus,            MP_ARG_OBJ | MP_ARG_REQUIRED, {.u_obj = MP_OBJ_NULL}     },
        { MP_QSTR_disp_drv,       MP_ARG_OBJ | MP_ARG_KW_ONLY,  {.u_obj = MP_OBJ_NULL}     },
        { MP_QSTR_reset,          MP_ARG_OBJ | MP_ARG_KW_ONLY,  {.u_obj = MP_OBJ_NULL}     },
        { MP_QSTR_backlight,      MP_ARG_OBJ | MP_ARG_KW_ONLY,  {.u_obj = MP_OBJ_NULL}     },
        { MP_QSTR_reset_level,    MP_ARG_BOOL | MP_ARG_KW_ONLY, {.u_bool = false}          },
        { MP_QSTR_color_space,    MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = COLOR_SPACE_RGB} },
        { MP_QSTR_bits_per_pixel, MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = 16}              },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // create new object
    mp_lcd_st7789_obj_t *self = m_new_obj(mp_lcd_st7789_obj_t);
    self->base.type           = &mp_lcd_st7789_type;

    self->bus_obj = (mp_obj_base_t *)MP_OBJ_TO_PTR(args[ARG_bus].u_obj);
    if (mp_obj_is_type(self->bus_obj, &mp_lcd_i80_type)) {
        self->width = ((mp_lcd_i80_obj_t *)self->bus_obj)->width;
        self->height = ((mp_lcd_i80_obj_t *)self->bus_obj)->height;
    }
    // self->bus_obj        = MP_OBJ_FROM_PTR(args[ARG_bus].u_obj);
    self->reset          = args[ARG_reset].u_obj;
    self->backlight      = args[ARG_backlight].u_obj;
    self->reset_level    = args[ARG_reset_level].u_bool;
    self->color_space    = args[ARG_color_space].u_int;
    self->bits_per_pixel = args[ARG_bits_per_pixel].u_int;

    // reset
    if (self->reset != MP_OBJ_NULL) {
        mp_hal_pin_obj_t reset_pin = mp_hal_get_pin_obj(self->reset);
        mp_hal_pin_output(reset_pin);
    }

    // init backlight
    if (self->backlight != MP_OBJ_NULL) {
        mp_hal_pin_obj_t backlight_pin = mp_hal_get_pin_obj(self->backlight);
        mp_hal_pin_output(backlight_pin);
    }
    
    
    // mp_load_method_maybe(self->bus_obj, MP_QSTR_tx_param, self->tx_param_method);
    // mp_load_method_maybe(self->bus_obj, MP_QSTR_tx_color, self->tx_color_method);

    if ((self->width == 240 && self->height == 320) || \
        (self->width == 320 && self->height == 240)) {
        self->rotations = ORIENTATIONS_240x320;
    } else if ((self->width == 170 && self->height == 320) || \
               (self->width == 320 && self->height == 170)) {
        self->rotations = ORIENTATIONS_170x320;
    } else if (self->width == 240 && self->height == 240) {
        self->rotations = ORIENTATIONS_240x240;
    } else if ((self->width == 135 && self->height == 240) ||
               (self->width == 240 && self->height == 135)) {
        self->rotations = ORIENTATIONS_135x240;
    } else if ((self->width == 128 && self->height == 160) || \
               (self->width == 160 && self->height == 128)) {
        self->rotations = ORIENTATIONS_128x160;
    } else if (self->width == 128 && self->height == 128) {
        self->rotations = ORIENTATIONS_128x128;
    } else {
        mp_warning(NULL, "rotation parameter not detected");
    }

    switch (self->color_space) {
        case COLOR_SPACE_RGB:
            self->madctl_val = 0;
        break;

        case COLOR_SPACE_BGR:
            self->madctl_val |= (1 << 3);
        break;

        default:
            mp_raise_ValueError(MP_ERROR_TEXT("unsupported color space"));
        break;
    }

    switch (self->bits_per_pixel)
    {
        case 16:
            self->colmod_cal = 0x55;
        break;

        case 18:
            self->colmod_cal = 0x66;
        break;

        default:
            mp_raise_ValueError(MP_ERROR_TEXT("unsupported pixel width"));
        break;
    }
    set_rotation(self, 0);
    return MP_OBJ_FROM_PTR(self);
}


STATIC mp_obj_t mp_lcd_st7789_deinit(mp_obj_t self_in)
{
    mp_lcd_st7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
    m_del_obj(mp_lcd_st7789_obj_t, self);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mp_lcd_st7789_deinit_obj, mp_lcd_st7789_deinit);


STATIC mp_obj_t mp_lcd_st7789_reset(mp_obj_t self_in)
{
    mp_lcd_st7789_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->reset != MP_OBJ_NULL) {
        mp_hal_pin_obj_t reset_pin = mp_hal_get_pin_obj(self->reset);
        mp_hal_pin_write(reset_pin, self->reset_level);
        mp_hal_delay_us(200 * 1000);
        mp_hal_pin_write(reset_pin, !self->reset_level);
    } else {
        if (mp_obj_is_type(self->bus_obj, &mp_lcd_i80_type)) {
            #ifdef MP_OBJ_TYPE_GET_SLOT
            mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)MP_OBJ_TYPE_GET_SLOT(self->bus_obj->type, protocol);
            #else
            mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)self->bus_obj->type->protocol;
            #endif
            i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0x01, NULL, 0);
        } else {
            return mp_const_none;
        }
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mp_lcd_st7789_reset_obj, mp_lcd_st7789_reset);


STATIC mp_obj_t mp_lcd_st7789_init(mp_obj_t self_in)
{
    mp_lcd_st7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (mp_obj_is_type(self->bus_obj, &mp_lcd_i80_type)) {
        #ifdef MP_OBJ_TYPE_GET_SLOT
        mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)MP_OBJ_TYPE_GET_SLOT(self->bus_obj->type, protocol);
        #else
        mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)self->bus_obj->type->protocol;
        #endif
        /*
        i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0x11, NULL, 0);
        mp_hal_delay_us(120 * 1000);
        
        i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0x13, NULL, 0);
        
        i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0x36, (uint8_t[]) {
            self->madctl_val,
        }, 1);
        
        i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0xB6, (uint8_t[]) {
            0x0a, 0x82, 
        }, 2);
        
        i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0x3A, (uint8_t[]) {
            self->colmod_cal,
        }, 1);
        mp_hal_delay_us(10 * 1000);
	
	i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0xB2, (uint8_t[]) {
            0x0C, 0x0C, 0x00, 0x33, 0x33,
        }, 5);
	
	i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0xB7, (uint8_t[]) {
            0x35,
        }, 1);
	
	i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0xBB, (uint8_t[]) {
            0x20,
        }, 1);
        
        i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0xC0, (uint8_t[]) {
            0x2C,
        }, 1);
        
        i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0xC2, (uint8_t[]) {
            0x01,
        }, 1);
	
	i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0xC3, (uint8_t[]) {
            0x0B,
        }, 1);//0x15 as 4.5v
        
        i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0xC4, (uint8_t[]) {
            0x20,
        }, 1);
        
        i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0xC6, (uint8_t[]) {
            0x0F,
        }, 1);
        
        i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0xD0, (uint8_t[]) {
            0xA4, 0xA1,
        }, 2);
        
        i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0xE0, (uint8_t[]) {
            0xD0, 0x03, 0x09, 0x0E, 0x11, 0x3D, 0x47, 0x55, 0x53, 0x1A, 0x16, 0x14, 0x1F, 0x22,
        }, 14);
	
	i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0xE1, (uint8_t[]) {
            0xD0, 0x02, 0x08, 0x0D, 0x12, 0x2C, 0x43, 0x55, 0x53, 0x1E, 0x1B, 0x19, 0x20, 0x22,
        }, 14);
        
        i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0x21, NULL, 0);
        
        i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0x2A, (uint8_t[]) {
            0x00, 0x00, 0x00, 0xE5,
        }, 4);
        
        i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0x2B, (uint8_t[]) {
            0x00, 0x00, 0x01, 0x3F,
        }, 4);
        mp_hal_delay_us(120 * 1000);
	*/
	i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0x11, NULL, 0);
        mp_hal_delay_us(100 * 1000);

        i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0x36, (uint8_t[]) {
            self->madctl_val,
        }, 1);

        i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0x3A, (uint8_t[]) {
            self->colmod_cal,
        }, 1);

        // turn on display
        i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0x29, NULL, 0);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mp_lcd_st7789_init_obj, mp_lcd_st7789_init);


STATIC mp_obj_t mp_lcd_st7789_bitmap(size_t n_args, const mp_obj_t *args_in)
{
    mp_lcd_st7789_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    // mp_obj_t args_out[2];

    int x_start = mp_obj_get_int(args_in[1]);
    int y_start = mp_obj_get_int(args_in[2]);
    int x_end   = mp_obj_get_int(args_in[3]);
    int y_end   = mp_obj_get_int(args_in[4]);

    x_start += self->x_gap;
    x_end += self->x_gap;
    y_start += self->y_gap;
    y_end += self->y_gap;

    if (mp_obj_is_type(self->bus_obj, &mp_lcd_i80_type)) {
        #ifdef MP_OBJ_TYPE_GET_SLOT
        mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)MP_OBJ_TYPE_GET_SLOT(self->bus_obj->type, protocol);
        #else
        mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)self->bus_obj->type->protocol;
        #endif
        i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0x2A, (uint8_t[]) {
            ((x_start >> 8) & 0xFF),
            (x_start & 0xFF),
            ((x_end >> 8) & 0xFF),
            (x_end & 0xFF),
        }, 4);
        i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0x2B, (uint8_t[]) {
            ((y_start >> 8) & 0xFF),
            (y_start & 0xFF),
            ((y_end >> 8) & 0xFF),
            (y_end & 0xFF),
        }, 4);
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(args_in[5], &bufinfo, MP_BUFFER_READ);
        // transfer frame buffer
        size_t len = (x_end - x_start + 1) * (y_end - y_start + 1) * self->bits_per_pixel / 8;
        i8080_p->tx_color((mp_lcd_i80_obj_t *)self->bus_obj, 0x2C, &bufinfo.buf[len], len);
    }

    gc_collect();
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_lcd_st7789_bitmap_obj, 6, 6, mp_lcd_st7789_bitmap);

STATIC mp_obj_t mp_lcd_st7789_tx_param(size_t n_args, const mp_obj_t *args_in)
{
    mp_lcd_st7789_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    
    DEBUG_printf("tx_param\n");
    int cmd = mp_obj_get_int(args_in[1]);
    if (mp_obj_is_type(self->bus_obj, &mp_lcd_i80_type)) {
        #ifdef MP_OBJ_TYPE_GET_SLOT
        mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)MP_OBJ_TYPE_GET_SLOT(self->bus_obj->type, protocol);
        #else
        mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)self->bus_obj->type->protocol;
        #endif
        
        if (n_args == 3) {
            mp_buffer_info_t bufinfo;
            mp_get_buffer_raise(args_in[2], &bufinfo, MP_BUFFER_READ);
            i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, cmd, bufinfo.buf, bufinfo.len);
        } else {
            i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, cmd, NULL, 0);
        }
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_lcd_st7789_tx_param_obj, 2, 3, mp_lcd_st7789_tx_param);
/*
STATIC mp_obj_t mp_lcd_st7789_tx_color(size_t n_args, const mp_obj_t *args_in)
{
    mp_lcd_st7789_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    // mp_obj_t args_out[2];
    
    int x_start = mp_obj_get_int(args_in[1]);
    int y_start = mp_obj_get_int(args_in[2]);
    int x_end   = mp_obj_get_int(args_in[3]);
    int y_end   = mp_obj_get_int(args_in[4]);

    if (mp_obj_is_type(self->bus_obj, &mp_lcd_i80_type)) {
        #ifdef MP_OBJ_TYPE_GET_SLOT
        mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)MP_OBJ_TYPE_GET_SLOT(self->bus_obj->type, protocol);
        #else
        mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)self->bus_obj->type->protocol;
        #endif
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(args_in[5], &bufinfo, MP_BUFFER_READ);
        // transfer frame buffer
        size_t len = (x_end - x_start + 1) * self->bits_per_pixel / 8;
        for (size_t i = 0; i < (y_end - y_start + 1); i++)
        {
            i8080_p->tx_color((mp_lcd_i80_obj_t *)self->bus_obj, 0x2C,
                              &bufinfo.buf[i * len],
                              i * len);
        }
    }

    gc_collect();
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_lcd_st7789_tx_color_obj, 6, 6, mp_lcd_st7789_tx_color);
*/

STATIC mp_obj_t mp_lcd_st7789_mirror(mp_obj_t self_in,
                                     mp_obj_t mirror_x_in,
                                     mp_obj_t mirror_y_in)
{
    mp_lcd_st7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
    //while (! self->ready){}
    if (mp_obj_is_true(mirror_x_in)) {
        self->madctl_val |= (1 << 6);
    } else {
        self->madctl_val &= ~(1 << 6);
    }
    if (mp_obj_is_true(mirror_y_in)) {
        self->madctl_val |= (1 << 7);
    } else {
        self->madctl_val &= ~(1 << 7);
    }

    if (mp_obj_is_type(self->bus_obj, &mp_lcd_i80_type)) {
        #ifdef MP_OBJ_TYPE_GET_SLOT
        mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)MP_OBJ_TYPE_GET_SLOT(self->bus_obj->type, protocol);
        #else
        mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)self->bus_obj->type->protocol;
        #endif
        //self->ready = false;
        i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0x36, (uint8_t[]) {
            self->madctl_val
        }, 1);
    }
    
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(mp_lcd_st7789_mirror_obj, mp_lcd_st7789_mirror);


STATIC mp_obj_t mp_lcd_st7789_swap_xy(mp_obj_t self_in, mp_obj_t swap_axes_in)
{
    mp_lcd_st7789_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (mp_obj_is_true(swap_axes_in)) {
        self->madctl_val |= 1 << 5;
    } else {
        self->madctl_val &= ~(1 << 5);
    }

    if (mp_obj_is_type(self->bus_obj, &mp_lcd_i80_type)) {
        #ifdef MP_OBJ_TYPE_GET_SLOT
        mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)MP_OBJ_TYPE_GET_SLOT(self->bus_obj->type, protocol);
        #else
        mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)self->bus_obj->type->protocol;
        #endif
        i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0x36, (uint8_t[]) {
            self->madctl_val
        }, 1);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(mp_lcd_st7789_swap_xy_obj, mp_lcd_st7789_swap_xy);


STATIC mp_obj_t mp_lcd_st7789_set_gap(mp_obj_t self_in, mp_obj_t x_gap_in, mp_obj_t y_gap_in)
{
    mp_lcd_st7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
    
    self->x_gap = mp_obj_get_int(x_gap_in);
    self->y_gap = mp_obj_get_int(y_gap_in);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(mp_lcd_st7789_set_gap_obj, mp_lcd_st7789_set_gap);


STATIC mp_obj_t mp_lcd_st7789_invert_color(mp_obj_t self_in, mp_obj_t invert_in)
{
    mp_lcd_st7789_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (mp_obj_is_true(invert_in)) {
        if (mp_obj_is_type(self->bus_obj, &mp_lcd_i80_type)) {
            #ifdef MP_OBJ_TYPE_GET_SLOT
            mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)MP_OBJ_TYPE_GET_SLOT(self->bus_obj->type, protocol);
            #else
            mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)self->bus_obj->type->protocol;
            #endif
            i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0x21, NULL, 0);
        }
    } else {
        if (mp_obj_is_type(self->bus_obj, &mp_lcd_i80_type)) {
            #ifdef MP_OBJ_TYPE_GET_SLOT
            mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)MP_OBJ_TYPE_GET_SLOT(self->bus_obj->type, protocol);
            #else
            mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)self->bus_obj->type->protocol;
            #endif
            i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0x20, NULL, 0);
        }
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(mp_lcd_st7789_invert_color_obj, mp_lcd_st7789_invert_color);


STATIC mp_obj_t mp_lcd_st7789_disp_off(mp_obj_t self_in, mp_obj_t off_in)
{
    mp_lcd_st7789_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (mp_obj_is_true(off_in)) {
        if (mp_obj_is_type(self->bus_obj, &mp_lcd_i80_type)) {
            #ifdef MP_OBJ_TYPE_GET_SLOT
            mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)MP_OBJ_TYPE_GET_SLOT(self->bus_obj->type, protocol);
            #else
            mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)self->bus_obj->type->protocol;
            #endif
            i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0x28, NULL, 0);
        }
    } else {
        if (mp_obj_is_type(self->bus_obj, &mp_lcd_i80_type)) {
            #ifdef MP_OBJ_TYPE_GET_SLOT
            mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)MP_OBJ_TYPE_GET_SLOT(self->bus_obj->type, protocol);
            #else
            mp_lcd_i80_p_t *i8080_p = (mp_lcd_i80_p_t *)self->bus_obj->type->protocol;
            #endif
            i8080_p->tx_param((mp_lcd_i80_obj_t *)self->bus_obj, 0x29, NULL, 0);
        }
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(mp_lcd_st7789_disp_off_obj, mp_lcd_st7789_disp_off);


STATIC mp_obj_t mp_lcd_st7789_backlight_on(mp_obj_t self_in)
{
    mp_lcd_st7789_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->backlight != MP_OBJ_NULL) {
        mp_hal_pin_obj_t backlight_pin = mp_hal_get_pin_obj(self->backlight);
        mp_hal_pin_write(backlight_pin, 1);
        mp_hal_delay_us(10 * 1000);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mp_lcd_st7789_backlight_on_obj, mp_lcd_st7789_backlight_on);


STATIC mp_obj_t mp_lcd_st7789_backlight_off(mp_obj_t self_in)
{
    mp_lcd_st7789_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->backlight != MP_OBJ_NULL) {
        mp_hal_pin_obj_t backlight_pin = mp_hal_get_pin_obj(self->backlight);
        mp_hal_pin_write(backlight_pin, 0);
        mp_hal_delay_us(10 * 1000);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mp_lcd_st7789_backlight_off_obj, mp_lcd_st7789_backlight_off);


STATIC mp_obj_t mp_lcd_st7789_color565(size_t n_args, const mp_obj_t *args_in)
{
    mp_lcd_st7789_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    mp_lcd_i80_obj_t *i8080_obj = MP_OBJ_TO_PTR(self->bus_obj);
    uint16_t color = 0;
    uint16_t a = 0x1234;

    color = ((mp_obj_get_int(args_in[1]) & 0xF8) << 8) | \
            ((mp_obj_get_int(args_in[2]) & 0xFC) << 3) | \
            ((mp_obj_get_int(args_in[3]) & 0xF8) >> 3);

    if ((*(char *)&a) == 0x34) {
        if (!i8080_obj->swap_color_bytes) {
            color = color >> 8 | color << 8;
        }
    } else {
        if (i8080_obj->swap_color_bytes) {
            color = color >> 8 | color << 8;
        }
    }

    return MP_OBJ_NEW_SMALL_INT(color);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_lcd_st7789_color565_obj, 4, 4, mp_lcd_st7789_color565);


STATIC mp_obj_t mp_lcd_st7789_width(mp_obj_t self_in)
{
    mp_lcd_st7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->width);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mp_lcd_st7789_width_obj, mp_lcd_st7789_width);


STATIC mp_obj_t mp_lcd_st7789_height(mp_obj_t self_in)
{
    mp_lcd_st7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->height);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mp_lcd_st7789_height_obj, mp_lcd_st7789_height);


STATIC mp_obj_t mp_lcd_st7789_rotation(mp_obj_t self_in, mp_obj_t rotation_in)
{
    mp_lcd_st7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
    self->rotation = mp_obj_get_int(rotation_in) % 4;
    set_rotation(self, self->rotation);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(mp_lcd_st7789_rotation_obj, mp_lcd_st7789_rotation);


STATIC const mp_rom_map_elem_t mp_lcd_st7789_locals_dict_table[] = {

    { MP_ROM_QSTR(MP_QSTR_deinit),        MP_ROM_PTR(&mp_lcd_st7789_deinit_obj)        },
    { MP_ROM_QSTR(MP_QSTR_reset),         MP_ROM_PTR(&mp_lcd_st7789_reset_obj)         },
    { MP_ROM_QSTR(MP_QSTR_init),          MP_ROM_PTR(&mp_lcd_st7789_init_obj)          },
    { MP_ROM_QSTR(MP_QSTR_bitmap),        MP_ROM_PTR(&mp_lcd_st7789_bitmap_obj)        },
    { MP_ROM_QSTR(MP_QSTR_tx_param),      MP_ROM_PTR(&mp_lcd_st7789_tx_param_obj)      },
    //{ MP_ROM_QSTR(MP_QSTR_tx_color),      MP_ROM_PTR(&mp_lcd_st7789_tx_color_obj)      },
    { MP_ROM_QSTR(MP_QSTR_mirror),        MP_ROM_PTR(&mp_lcd_st7789_mirror_obj)        },
    { MP_ROM_QSTR(MP_QSTR_swap_xy),       MP_ROM_PTR(&mp_lcd_st7789_swap_xy_obj)       },
    { MP_ROM_QSTR(MP_QSTR_set_gap),       MP_ROM_PTR(&mp_lcd_st7789_set_gap_obj)       },
    { MP_ROM_QSTR(MP_QSTR_invert_color),  MP_ROM_PTR(&mp_lcd_st7789_invert_color_obj)  },
    { MP_ROM_QSTR(MP_QSTR_disp_off),      MP_ROM_PTR(&mp_lcd_st7789_disp_off_obj)      },
    { MP_ROM_QSTR(MP_QSTR_backlight_on),  MP_ROM_PTR(&mp_lcd_st7789_backlight_on_obj)  },
    { MP_ROM_QSTR(MP_QSTR_backlight_off), MP_ROM_PTR(&mp_lcd_st7789_backlight_off_obj) },
    { MP_ROM_QSTR(MP_QSTR_color565),      MP_ROM_PTR(&mp_lcd_st7789_color565_obj)      },
    { MP_ROM_QSTR(MP_QSTR_height),        MP_ROM_PTR(&mp_lcd_st7789_height_obj)        },
    { MP_ROM_QSTR(MP_QSTR_width),         MP_ROM_PTR(&mp_lcd_st7789_width_obj)         },
    { MP_ROM_QSTR(MP_QSTR_rotation),      MP_ROM_PTR(&mp_lcd_st7789_rotation_obj)      },
    { MP_ROM_QSTR(MP_QSTR___del__),       MP_ROM_PTR(&mp_lcd_st7789_deinit_obj)        },

    { MP_ROM_QSTR(MP_QSTR_RGB),           MP_ROM_INT(COLOR_SPACE_RGB)               },
    { MP_ROM_QSTR(MP_QSTR_BGR),           MP_ROM_INT(COLOR_SPACE_BGR)               },
    { MP_ROM_QSTR(MP_QSTR_MONOCHROME),    MP_ROM_INT(COLOR_SPACE_MONOCHROME)        }
};
STATIC MP_DEFINE_CONST_DICT(mp_lcd_st7789_locals_dict, mp_lcd_st7789_locals_dict_table);


const mp_lcd_st7789_p_t mp_lcd_st7789_p = {
    //make other void
    //.init = ...
};


#ifdef MP_OBJ_TYPE_GET_SLOT
MP_DEFINE_CONST_OBJ_TYPE(
    mp_lcd_st7789_type,
    MP_QSTR_ST7789,
    MP_TYPE_FLAG_NONE,
    make_new, mp_lcd_st7789_make_new,
    print, mp_lcd_st7789_print,
    protocol, &mp_lcd_st7789_p,
    locals_dict, &mp_lcd_st7789_locals_dict
    );
#else
const mp_obj_type_t mp_lcd_st7789_type = {
    { &mp_type_type },
    .name = MP_QSTR_ST7789,
    .print = mp_lcd_st7789_print,
    .make_new = mp_lcd_st7789_make_new,
    .protocol = &mp_lcd_st7789_p,
    .locals_dict = (mp_obj_dict_t *)&mp_lcd_st7789_locals_dict,
};
#endif
    
    
    
    
    
    
    
    
    
    


