#include "i80_panel.h"
#include "esp_lcd_panel_io.h"

#if USE_ESP_LCD
#include "hal/esp32.h"
#else
#include "hal/soft8080.h"
#endif

#include "mphalport.h"

#include "py/obj.h"
#include "py/runtime.h"
#include "py/gc.h"

#include <string.h>

/*
STATIC void mp_lcd_i80_deinit_internal(mp_lcd_i80_obj_t *self_in){
    mp_lcd_i80_obj_t *self = (mp_lcd_i80_obj_t *)self_in;
    hal_lcd_i80_deinit(self);
    m_del_obj(mp_lcd_i80_obj_t, self);
}

STATIC void mp_hw_lcd_init_internal(mp_lcd_i80_obj_t *self) {
    // if we're not initialized, then we're
    // implicitly 'changed', since this is the init routine
    bool changed = self->state != MP_HW_LCD_STATE_INIT;
    mp_lcd_i80_obj_t old_self = *self;
    if (changed) {
        self->state = MP_HW_LCD_STATE_DEINIT;
        mp_lcd_i80_deinit_internal(&old_self);
    } else {
        self->state = MP_HW_LCD_STATE_INIT;
        // no changes
    }
}
*/
/******************************************************************************/
// MicroPython bindings for hw_lcd
STATIC void mp_lcd_i80_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    (void) kind;
    mp_lcd_i80_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<I8080 data=%p, dc=%p, write=%p, read=%p, cs=%p, width=%u, height=%u, cmd_bits=%u, param_bits=%u, queue_size=%u>",
              self->databus,
              self->dc,
              self->wr,
              self->rd,
              self->cs,
              self->width,
              self->height,
              self->cmd_bits,
              self->param_bits,
              self->queue_size);
}
/*
STATIC void mp_hw_lcd_init(mp_obj_base_t *self_in, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    mp_lcd_i80_obj_t *self = (mp_lcd_i80_obj_t *)self_in;

    enum { ARG_id, ARG_baudrate, ARG_polarity, ARG_phase, ARG_bits, ARG_firstbit, ARG_sck, ARG_mosi, ARG_miso };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_id,       MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_baudrate, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_polarity, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_phase,    MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_bits,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_firstbit, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_sck,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_mosi,     MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_miso,     MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args),
        allowed_args, args);
    int8_t sck, mosi, miso;

    if (args[ARG_sck].u_obj == MP_OBJ_NULL) {
        sck = -2;
    } else if (args[ARG_sck].u_obj == mp_const_none) {
        sck = -1;
    } else {
        sck = machine_pin_get_id(args[ARG_sck].u_obj);
    }

    if (args[ARG_miso].u_obj == MP_OBJ_NULL) {
        miso = -2;
    } else if (args[ARG_miso].u_obj == mp_const_none) {
        miso = -1;
    } else {
        miso = machine_pin_get_id(args[ARG_miso].u_obj);
    }

    if (args[ARG_mosi].u_obj == MP_OBJ_NULL) {
        mosi = -2;
    } else if (args[ARG_mosi].u_obj == mp_const_none) {
        mosi = -1;
    } else {
        mosi = machine_pin_get_id(args[ARG_mosi].u_obj);
    }

    machine_hw_spi_init_internal(self, args[ARG_id].u_int, args[ARG_baudrate].u_int,
        args[ARG_polarity].u_int, args[ARG_phase].u_int, args[ARG_bits].u_int,
        args[ARG_firstbit].u_int, sck, mosi, miso);
}
*/
STATIC mp_obj_t mp_lcd_i80_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
    enum {
        ARG_data,
        ARG_command,
        ARG_write,
        ARG_read,
        ARG_cs,
        ARG_pclk,
        ARG_width,
        ARG_height,
        ARG_swap_color_bytes,
        ARG_cmd_bits,
        ARG_param_bits,
        ARG_queue_size};
    const mp_arg_t make_new_args[] = {
        { MP_QSTR_data,             MP_ARG_OBJ | MP_ARG_KW_ONLY | MP_ARG_REQUIRED        },
        { MP_QSTR_command,          MP_ARG_OBJ | MP_ARG_KW_ONLY | MP_ARG_REQUIRED        },
        { MP_QSTR_write,            MP_ARG_OBJ | MP_ARG_KW_ONLY | MP_ARG_REQUIRED        },
        { MP_QSTR_read,             MP_ARG_OBJ | MP_ARG_KW_ONLY,  {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_cs,               MP_ARG_OBJ | MP_ARG_KW_ONLY,  {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_pclk,             MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = 10000000   } },
        { MP_QSTR_width,            MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = 240        } },
        { MP_QSTR_height,           MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = 240        } },
        { MP_QSTR_swap_color_bytes, MP_ARG_BOOL | MP_ARG_KW_ONLY, {.u_bool = false     } },
        { MP_QSTR_cmd_bits,         MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = 8          } },
        { MP_QSTR_param_bits,       MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = 8          } },
        { MP_QSTR_queue_size,       MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = 10         } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(make_new_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(make_new_args), make_new_args, args);

    // create new object
    mp_lcd_i80_obj_t *self = m_new_obj(mp_lcd_i80_obj_t);
    self->base.type = &mp_lcd_i80_type;

    // data bus
    mp_obj_tuple_t *t = MP_OBJ_TO_PTR(args[ARG_data].u_obj);
    self->bus_width = t->len;
    for (size_t i = 0; i < t->len; i++)
    {
        self->databus[i] = t->items[i];
    }

    self->dc               = args[ARG_command].u_obj;
    self->wr               = args[ARG_write].u_obj;
    self->rd               = args[ARG_read].u_obj;
    self->cs               = args[ARG_cs].u_obj;
    self->pclk             = args[ARG_pclk].u_int;
    self->width            = args[ARG_width].u_int;
    self->height           = args[ARG_height].u_int;
    self->swap_color_bytes = args[ARG_swap_color_bytes].u_bool;
    self->cmd_bits         = args[ARG_cmd_bits].u_int;
    self->param_bits       = args[ARG_param_bits].u_int;
    self->queue_size       = args[ARG_queue_size].u_int;
    self->flushdown        = true;
    self->disp_drv_obj     = MP_OBJ_NULL;

    if (self->rd != MP_OBJ_NULL) {
        mp_hal_pin_obj_t rd_pin = mp_hal_get_pin_obj(self->rd);
        mp_hal_pin_output(rd_pin);
        mp_hal_pin_write(rd_pin, 1);
    }
    // get in values to make the hal_lcd
    hal_lcd_i80_construct(self);

    /*
    mp_hw_lcd_init_internal(
        self,
        args[ARG_id].u_int,
        args[ARG_baudrate].u_int,
        args[ARG_polarity].u_int,
        args[ARG_phase].u_int,
        args[ARG_bits].u_int,
        args[ARG_firstbit].u_int,
        sck,
        mosi,
        miso);
    */
    return MP_OBJ_FROM_PTR(self);
}


// use it to init the lcd
STATIC mp_obj_t mp_lcd_i80_tx_param(size_t n_args, const mp_obj_t *args_in)
{
    mp_lcd_i80_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    int cmd = mp_obj_get_int(args_in[1]);
    if (n_args == 3) {
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(args_in[2], &bufinfo, MP_BUFFER_READ);
        hal_lcd_i80_tx_param(self, cmd, bufinfo.buf, bufinfo.len);
    } else {
        hal_lcd_i80_tx_param(self, cmd, NULL, 0);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_lcd_i80_tx_param_obj, 2, 3, mp_lcd_i80_tx_param);

//
STATIC mp_obj_t mp_lcd_i80_tx_color(size_t n_args, const mp_obj_t *args_in)
{
    mp_lcd_i80_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    int cmd = mp_obj_get_int(args_in[1]);

    if (n_args == 3) {
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(args_in[2], &bufinfo, MP_BUFFER_READ);
        hal_lcd_i80_tx_color(self, cmd, bufinfo.buf, bufinfo.len);
    } else {
        int x_start = mp_obj_get_int(args_in[2]);
        int y_start = mp_obj_get_int(args_in[3]);
        int x_end   = mp_obj_get_int(args_in[4]);
        int y_end   = mp_obj_get_int(args_in[5]);
        hal_lcd_i80_tx_param(self, 0x2A, (uint8_t[]) {
            ((x_start >> 8) & 0xFF),
            (x_start & 0xFF),
            ((x_end >> 8) & 0xFF),
            (x_end & 0xFF),
        }, 4);
        hal_lcd_i80_tx_param(self, 0x2B, (uint8_t[]) {
            ((y_start >> 8) & 0xFF),
            (y_start & 0xFF),
            ((y_end >> 8) & 0xFF),
            (y_end & 0xFF),
        }, 4);
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(args_in[6], &bufinfo, MP_BUFFER_READ);
        hal_lcd_i80_tx_color(self, cmd, bufinfo.buf, bufinfo.len);
    }

    gc_collect();
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_lcd_i80_tx_color_obj, 3, 7, mp_lcd_i80_tx_color);


STATIC mp_obj_t mp_lcd_i80_deinit(mp_obj_t self_in)
{
    mp_lcd_i80_obj_t *self = MP_OBJ_TO_PTR(self_in);

    hal_lcd_i80_deinit(self);
    m_del_obj(mp_lcd_i80_obj_t, self);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mp_lcd_i80_deinit_obj, mp_lcd_i80_deinit);


STATIC const mp_rom_map_elem_t mp_lcd_i80_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_tx_param),    MP_ROM_PTR(&mp_lcd_i80_tx_param_obj)   },
    { MP_ROM_QSTR(MP_QSTR_tx_color),    MP_ROM_PTR(&mp_lcd_i80_tx_color_obj)   },
    { MP_ROM_QSTR(MP_QSTR_deinit),      MP_ROM_PTR(&mp_lcd_i80_deinit_obj)     },
    { MP_ROM_QSTR(MP_QSTR___del__),     MP_ROM_PTR(&mp_lcd_i80_deinit_obj)     },
};
STATIC MP_DEFINE_CONST_DICT(mp_lcd_i80_locals_dict, mp_lcd_i80_locals_dict_table);


STATIC const mp_lcd_i80_p_t mp_lcd_i80_p = {
    .tx_param = hal_lcd_i80_tx_param,
    .tx_color = hal_lcd_i80_tx_color,
};


#ifdef MP_OBJ_TYPE_GET_SLOT
MP_DEFINE_CONST_OBJ_TYPE(
    mp_lcd_i80_type,
    MP_QSTR_I80,
    MP_TYPE_FLAG_NONE,
    make_new, mp_lcd_i80_make_new,
    print, mp_lcd_i80_print,
    protocol, &mp_lcd_i80_p,
    locals_dict, &mp_lcd_i80_locals_dict
    );
#else
const mp_obj_type_t mp_lcd_i80_type = {
    { &mp_type_type },
    .name = MP_QSTR_I80,
    .print = mp_lcd_i80_print,
    .make_new = mp_lcd_i80_make_new,
    .protocol = &mp_lcd_i80_p,
    .locals_dict = (mp_obj_dict_t *)&mp_lcd_i80_locals_dict,
};
#endif
