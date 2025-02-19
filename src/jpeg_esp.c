#include "py/runtime.h"
#include "py/obj.h"
#include "esp_jpeg_common.h"
#include "esp_jpeg_dec.h"
#include "py/mphal.h" 

// structure for the JPEG decoder object
typedef struct _jpeg_decoder_obj_t {
    mp_obj_base_t base;
    jpeg_dec_handle_t handle;
    jpeg_dec_config_t config;
    jpeg_dec_io_t io;
    int block_pos;       // position of the current block
    int block_counts;    // total number of blocks
} jpeg_decoder_obj_t;

// Helper functions
static int jpeg_get_format_code(const char *format_str) {
    if (strcmp(format_str, "RGB565_BE") == 0) {
        return JPEG_PIXEL_FORMAT_RGB565_BE;
    } else if (strcmp(format_str, "RGB565_LE") == 0) {
        return JPEG_PIXEL_FORMAT_RGB565_LE;
    } else if (strcmp(format_str, "CbYCrY") == 0) {
        return JPEG_PIXEL_FORMAT_CbYCrY;
    } else if (strcmp(format_str, "RGB888") == 0) {
        return JPEG_PIXEL_FORMAT_RGB888;
    } else {
        mp_printf(&mp_plat_print, "Using default format: RGB888\n");
        return JPEG_PIXEL_FORMAT_RGB888;
    }
}

static int jpeg_get_rotation_code(int rotation) {
    if (rotation == 0) {
        return JPEG_ROTATE_0D;
    } else if (rotation == 90) {
        return JPEG_ROTATE_90D;
    } else if (rotation == 180) {
        return JPEG_ROTATE_180D;
    } else if (rotation == 270) {
        return JPEG_ROTATE_270D;
    } else {
        mp_printf(&mp_plat_print, "Rotation %i invalid. Using default rotation 0\n", rotation);
        return JPEG_ROTATE_0D;
    }
}

static void jpeg_err_to_mp_exception(jpeg_error_t err, const char *msg) {
    switch (err) {
        case JPEG_ERR_OK:
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT(msg));
            break;
        case JPEG_ERR_INVALID_PARAM:
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%s: Invalid argument"), msg);
            break;
        case JPEG_ERR_NO_MEM:
            mp_raise_msg_varg(&mp_type_MemoryError, MP_ERROR_TEXT("%s: Out of memory"), msg);
            break;
        case JPEG_ERR_BAD_DATA:
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%s: Data format error (may be damaged)"), msg);
            break;
        case JPEG_ERR_NO_MORE_DATA:
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%s: Input data is not enough"), msg);
            break;
        case JPEG_ERR_UNSUPPORT_FMT:
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%s: Format not supported"), msg);
            break;
        case JPEG_ERR_UNSUPPORT_STD:
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%s: JPEG standard not supported"), msg);
            break;
        case JPEG_ERR_FAIL:
            mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("%s: Internal JPEG decoder error"), msg);
            break;
        default:
            mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("%s: Unknown error: %d"), msg, err);
            break;
    }
}

// Consturctor function
static mp_obj_t jpeg_decoder_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_rotation, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_format, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_block, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
    };

    mp_arg_val_t parsed_args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed_args);
    
    jpeg_decoder_obj_t *self = mp_obj_malloc_with_finaliser(jpeg_decoder_obj_t, type);
    self->base.type = type;
    self->io.outbuf = NULL;
    self->io.out_size = 0;
    self->block_pos = 0;
    self->block_counts = 0;
    self->handle = NULL;

    self->config = (jpeg_dec_config_t)DEFAULT_JPEG_DEC_CONFIG();
    self->config.block_enable = parsed_args[2].u_bool;
    if (parsed_args[0].u_obj != mp_const_none) {
        self->config.rotate = jpeg_get_rotation_code(parsed_args[0].u_int);
        if (self->config.block_enable && (self->config.rotate != JPEG_ROTATE_0D)) {
            mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("Block decoding is only supported for rotation 0"));
        }
    }
    if (parsed_args[1].u_obj != mp_const_none) {
        self->config.output_type = jpeg_get_format_code(mp_obj_str_get_str(parsed_args[1].u_obj));
    }
    
    jpeg_error_t ret = jpeg_dec_open(&self->config, &self->handle);
    if (ret != JPEG_ERR_OK) {
        jpeg_err_to_mp_exception(ret, "Failed to initialize JPEG decoder object");
    }
    
    memset(&self->io, 0, sizeof(jpeg_dec_io_t));
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t jpeg_decoder_prepare(mp_obj_t self_in, mp_obj_t jpeg_data) {
    jpeg_decoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(jpeg_data, &bufinfo, MP_BUFFER_READ);

    jpeg_error_t ret = JPEG_ERR_OK;
    if (self->io.inbuf != (uint8_t *)bufinfo.buf || self->io.inbuf_len != bufinfo.len || self->block_pos >= self->block_counts) {
        self->io.inbuf = (uint8_t *)bufinfo.buf;
        self->io.inbuf_len = bufinfo.len;
        self->block_pos = 0;

        jpeg_dec_header_info_t out_info;
        ret = jpeg_dec_parse_header(self->handle, &self->io, &out_info);
        if (ret != JPEG_ERR_OK) {
            jpeg_err_to_mp_exception(ret, "JPEG header parsing failed.");
        }

        int output_len = 0;
        ret = jpeg_dec_get_outbuf_len(self->handle, &output_len);
        if (ret != JPEG_ERR_OK || output_len == 0) {
            jpeg_err_to_mp_exception(ret, "Failed to get output buffer size");
        }
        
        // we want to reuse the output buffer if the size is the same
        if (self->io.out_size != output_len) {
            if (self->io.outbuf) {
                jpeg_free_align(self->io.outbuf);
            }
            self->io.outbuf = jpeg_calloc_align(output_len, 16);
            if (!self->io.outbuf) {
                mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("Failed to allocate output buffer"));
            }
            self->io.out_size = output_len;
        }
        
        ret = jpeg_dec_get_process_count(self->handle, &self->block_counts);
        if (ret != JPEG_ERR_OK || self->block_counts == 0) {
            jpeg_err_to_mp_exception(ret, "Failed to get process count");
        }
    }
    return mp_obj_new_int(self->block_counts);
}
static MP_DEFINE_CONST_FUN_OBJ_2(jpeg_decoder_prepare_obj, jpeg_decoder_prepare);

// `decode()` methods
static mp_obj_t jpeg_decoder_decode(mp_obj_t self_in, mp_obj_t jpeg_data) {
    jpeg_decoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(jpeg_data, &bufinfo, MP_BUFFER_READ);

    self->io.inbuf = (uint8_t *)bufinfo.buf;
    self->io.inbuf_len = bufinfo.len;

    jpeg_dec_header_info_t out_info;
    jpeg_error_t ret = jpeg_dec_parse_header(self->handle, &self->io, &out_info);
    if (ret != JPEG_ERR_OK) {
        jpeg_err_to_mp_exception(ret, "JPEG header parsing failed");
    }
    
    int new_output_len = (self->config.output_type == JPEG_PIXEL_FORMAT_RGB888) ? 
                          (out_info.width * out_info.height * 3) : (out_info.width * out_info.height * 2);

    // If the output buffer size has changed, reallocate the buffer
    if (new_output_len != self->io.out_size) {
        if (self->io.outbuf) {
            jpeg_free_align(self->io.outbuf);
        }
        self->io.outbuf = jpeg_calloc_align(new_output_len, 16);
        if (!self->io.outbuf) {
            mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("Failed to allocate output buffer"));
        }
        self->io.out_size = new_output_len;
    }
    
    ret = jpeg_dec_process(self->handle, &self->io);
    if (ret != JPEG_ERR_OK) {
        jpeg_err_to_mp_exception(ret, "JPEG decoding failed");
    }
    
    return mp_obj_new_memoryview(MP_BUFFER_READ, self->io.out_size, self->io.outbuf);
}
static MP_DEFINE_CONST_FUN_OBJ_2(jpeg_decoder_decode_obj, jpeg_decoder_decode);

static mp_obj_t jpeg_decoder_decode_block(mp_obj_t self_in, mp_obj_t jpeg_data) {
    jpeg_decoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(jpeg_data, &bufinfo, MP_BUFFER_READ);
    jpeg_error_t ret = JPEG_ERR_OK;

    (void)jpeg_decoder_prepare(self_in, jpeg_data);
    // decode the next block
    if ((self->block_pos < self->block_counts) && (self->block_counts > 0)) {
        ret = jpeg_dec_process(self->handle, &self->io);
        if (ret != JPEG_ERR_OK) {
            jpeg_err_to_mp_exception(ret, "JPEG decoding failed");
        }
        self->block_pos++;
        return mp_obj_new_memoryview(MP_BUFFER_READ, self->io.out_size, self->io.outbuf);
    } else {
        return mp_const_none;
    }    
}
static MP_DEFINE_CONST_FUN_OBJ_2(jpeg_decoder_decode_block_obj, jpeg_decoder_decode_block);

// `__del__()` method
static mp_obj_t jpeg_decoder_del(mp_obj_t self_in) {
    jpeg_decoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->handle) {
        jpeg_dec_close(self->handle);
        self->handle = NULL;
    }
    if (self->io.outbuf) {
        jpeg_free_align(self->io.outbuf);
        self->io.outbuf = NULL;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(jpeg_decoder_del_obj, jpeg_decoder_del);

static const mp_rom_map_elem_t jpeg_decoder_locals_dict_table[] = {
    {MP_ROM_QSTR(MP_QSTR_decode), MP_ROM_PTR(&jpeg_decoder_decode_obj)},
    {MP_ROM_QSTR(MP_QSTR_decode_block), MP_ROM_PTR(&jpeg_decoder_decode_block_obj)},
    {MP_ROM_QSTR(MP_QSTR_get_block_counts), MP_ROM_PTR(&jpeg_decoder_prepare_obj)},
    {MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&jpeg_decoder_del_obj)},
    {MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&mp_identity_obj)},
    {MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&jpeg_decoder_del_obj)},
};

static MP_DEFINE_CONST_DICT(jpeg_decoder_locals_dict, jpeg_decoder_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    jpeg_decoder_type,
    MP_QSTR_Decoder,
    MP_TYPE_FLAG_NONE,
    make_new, jpeg_decoder_make_new,
    locals_dict, &jpeg_decoder_locals_dict
);

static const mp_rom_map_elem_t jpeg_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR_decoder), MP_ROM_PTR(&jpeg_decoder_type)},
};

static MP_DEFINE_CONST_DICT(mp_module_jpeg_globals, jpeg_module_globals_table);

const mp_obj_module_t mp_module_jpeg = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&mp_module_jpeg_globals,
};

MP_REGISTER_MODULE(MP_QSTR_jpeg, mp_module_jpeg);
