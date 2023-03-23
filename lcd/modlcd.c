
#include "st7789.h"
#include "i80_panel.h"
#if RGB_LCD_SUPPORTED
#include "rgb_panel.h"
#endif

#include "py/obj.h"


STATIC const mp_map_elem_t mp_module_lcd_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_lcd)   },
    { MP_ROM_QSTR(MP_QSTR_ST7789),   (mp_obj_t)&mp_lcd_st7789_type     },
    { MP_ROM_QSTR(MP_QSTR_I80),    (mp_obj_t)&mp_lcd_i80_type        },
#if RGB_LCD_SUPPORTED
    { MP_ROM_QSTR(MP_QSTR_RGB),      (mp_obj_t)&mp_lcd_rgb_type        },
#endif
};
STATIC MP_DEFINE_CONST_DICT(mp_module_lcd_globals, mp_module_lcd_globals_table);


const mp_obj_module_t mp_module_lcd = {
    .base    = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&mp_module_lcd_globals,
};


#if MICROPY_VERSION >= 0x011300 // MicroPython 1.19 or later
MP_REGISTER_MODULE(MP_QSTR_lcd, mp_module_lcd);
#else
MP_REGISTER_MODULE(MP_QSTR_lcd, mp_module_lcd, MODULE_LCD_ENABLE);
#endif
