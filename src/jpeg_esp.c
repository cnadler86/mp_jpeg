#include "py/runtime.h"
#include "py/obj.h"
#include "esp_jpeg_common.h"
#include "esp_jpeg_dec.h"
#include "esp_jpeg_enc.h"
#include "py/mphal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"
#include "esp_log.h"

// Stream decoder structure to hold decoded frame data
typedef struct {
    mp_obj_t jpeg_data;      // JPEG frame as memoryview
    uint8_t *decoded_data;   // Decoded image data
    size_t decoded_len;      // Length of decoded data
} stream_frame_t;

// Queue handle for decoded frames
static QueueHandle_t frame_queue = NULL;
static TaskHandle_t decode_task_handle = NULL;
#define QUEUE_SIZE 2
#define STACK_SIZE 8192

// Task function prototype
static void decode_task(void *arg);

// Helper function to call camera methods
static mp_obj_t call_camera_method(mp_obj_t camera, const char* method_name) {
    // Get type dictionary
    mp_obj_t type_dict = MP_OBJ_TYPE_GET_SLOT(mp_obj_get_type(camera), locals_dict);
    if (type_dict == MP_OBJ_NULL) {
        return MP_OBJ_NULL;
    }
    
    // Get method from dictionary
    mp_obj_t method = mp_obj_dict_get(type_dict, MP_OBJ_NEW_QSTR(qstr_from_str(method_name)));
    if (method == MP_OBJ_NULL) {
        return MP_OBJ_NULL;
    }

    // Call method with no arguments
    return mp_call_function_1(method, camera);
}

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
    } else if (strcmp(format_str, "GRAY") == 0) {
        return JPEG_PIXEL_FORMAT_GRAY;
    } else if (strcmp(format_str, "RGBA") == 0) {
        return JPEG_PIXEL_FORMAT_RGBA;
    } else if (strcmp(format_str, "YCbYCr") == 0) {
        return JPEG_PIXEL_FORMAT_YCbYCr;
    } else if (strcmp(format_str, "YCbY2YCrY2") == 0) {
        return JPEG_PIXEL_FORMAT_YCbY2YCrY2;
    } else {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("Format %s not supported"), format_str);
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
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%s: Data error"), msg);
            break;
        case JPEG_ERR_NO_MORE_DATA:
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%s: Input data not enough"), msg);
            break;
        case JPEG_ERR_UNSUPPORT_FMT:
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%s: Format not supported"), msg);
            break;
        case JPEG_ERR_UNSUPPORT_STD:
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%s: JPEG standard not supported"), msg);
            break;
        case JPEG_ERR_FAIL:
            mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("%s: Internal JPEG library error"), msg);
            break;
        default:
            mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("%s: Unknown error: %d"), msg, err);
            break;
    }
}

// type and structure for the JPEG decoder object
extern const mp_obj_type_t mp_jpeg_decoder_type;

typedef struct _jpeg_decoder_obj_t {
    mp_obj_base_t base;
    jpeg_dec_handle_t handle;
    jpeg_dec_io_t io;
    jpeg_dec_config_t config;
    jpeg_dec_header_info_t out_info;
    int block_pos;       // position of the current block
    int block_counts;    // total number of blocks
    bool return_bytes;   // whether to return bytes or a memoryview
    mp_obj_t camera_obj; // Camera object reference
} jpeg_decoder_obj_t;

// Stream decoding task function
static void decode_task(void *arg) {
    jpeg_decoder_obj_t *decoder = (jpeg_decoder_obj_t *)arg;
    mp_obj_t camera = decoder->camera_obj;  // Camera object reference
    stream_frame_t frame;

    while (1) {
        // Check for new frame using camera frame_available method
        mp_obj_t frame_available = call_camera_method(camera, "frame_available");
        if (!mp_obj_is_true(frame_available)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Get frame using camera capture method
        mp_obj_t captured = call_camera_method(camera, "capture");
        ESP_LOGI("JPEG", "Captured frame: %p", captured);
        if (captured == mp_const_none) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Extract buffer info from memoryview object
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(captured, &bufinfo, MP_BUFFER_READ);
        ESP_LOGI("JPEG", "Buffer info: %p, %d", bufinfo.buf, bufinfo.len);

        // Setup decoder input
        decoder->io.inbuf = bufinfo.buf;
        decoder->io.inbuf_len = bufinfo.len;

        // Parse header and prepare decoder
        jpeg_error_t ret = jpeg_dec_parse_header(decoder->handle, &decoder->io, &decoder->out_info);
        ESP_LOGI("JPEG", "Header parsed: %d", ret);
        if (ret == JPEG_ERR_OK) {
            int output_len = 0;
            ret = jpeg_dec_get_outbuf_len(decoder->handle, &output_len);
            if (ret == JPEG_ERR_OK && output_len > 0) {
                ESP_LOGI("JPEG", "Output buffer length: %d", output_len);
                frame.decoded_data = jpeg_calloc_align(output_len, 16);
                if (frame.decoded_data) {
                    frame.decoded_len = output_len;
                    decoder->io.outbuf = frame.decoded_data;
                    decoder->io.out_size = output_len;
                    frame.jpeg_data = captured;  // Store original JPEG frame
                    ESP_LOGI("JPEG", "Decoding frame: %p", frame.jpeg_data);
                    // Decode frame
                    ret = jpeg_dec_process(decoder->handle, &decoder->io);
                    if (ret == JPEG_ERR_OK) {
                        // Try to send to queue, timeout after 10ms
                        ESP_LOGI("JPEG", "Sending frame to queue: %p", frame.jpeg_data);
                        if (xQueueSend(frame_queue, &frame, pdMS_TO_TICKS(10)) != pdTRUE) {
                            ESP_LOGI("JPEG", "Queue full, freeing memory");
                            // Queue full, free memory
                            jpeg_free_align(frame.decoded_data);
                            call_camera_method(camera, "free_buffer");
                        }
                        continue;
                    }
                }
                if (frame.decoded_data) {
                    jpeg_free_align(frame.decoded_data);
                }
            }
        }
        call_camera_method(camera, "free_buffer");
    }
}

// Consturctor function for the JPEG decoder object
static mp_obj_t jpeg_decoder_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_rotation, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_format, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_block, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_scale_width, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_scale_height, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_clipper_width, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_clipper_height, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_return_bytes, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
    };
    enum { ARG_rotation, ARG_format, ARG_block, ARG_scale_width, ARG_scale_height, ARG_clipper_width, ARG_clipper_height, ARG_return_bytes };
    mp_arg_val_t parsed_args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed_args);
    
    jpeg_decoder_obj_t *self = mp_obj_malloc_with_finaliser(jpeg_decoder_obj_t, &mp_jpeg_decoder_type);
    self->io.outbuf = NULL;
    self->io.out_size = 0;
    self->block_pos = 0;
    self->block_counts = 0;
    self->handle = NULL;

    self->config = (jpeg_dec_config_t)DEFAULT_JPEG_DEC_CONFIG();
    self->config.block_enable = parsed_args[ARG_block].u_bool;
    if (parsed_args[ARG_rotation].u_obj != mp_const_none) {
        self->config.rotate = jpeg_get_rotation_code(parsed_args[ARG_rotation].u_int);
    }
    if (parsed_args[ARG_format].u_obj != mp_const_none) {
        self->config.output_type = jpeg_get_format_code(mp_obj_str_get_str(parsed_args[ARG_format].u_obj));
    }
    if (parsed_args[ARG_scale_width].u_int > 0 && parsed_args[ARG_scale_height].u_int > 0) {
        self->config.scale.width = parsed_args[ARG_scale_width].u_int;
        self->config.scale.height = parsed_args[ARG_scale_height].u_int;
    }
    if (parsed_args[ARG_clipper_width].u_int > 0 && parsed_args[ARG_clipper_height].u_int > 0) {
        self->config.clipper.width = parsed_args[ARG_clipper_width].u_int;
        self->config.clipper.height = parsed_args[ARG_clipper_height].u_int;
    }

    if (self->config.block_enable) {
        if (self->config.rotate != JPEG_ROTATE_0D) {
            mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("Block decoding is only supported for rotation 0"));
        }
        if (self->config.scale.width != 0 || self->config.scale.height != 0) {
            mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("Block decoding does not support scaling"));
        }
        if (self->config.clipper.width != 0 || self->config.clipper.height != 0) {
            mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("Block decoding does not support clipping"));
        }
    }

    if (parsed_args[ARG_return_bytes].u_bool) {
        self->return_bytes = true;
    } else {
        self->return_bytes = false;
    }

    jpeg_error_t ret = jpeg_dec_open(&self->config, &self->handle);
    if (ret != JPEG_ERR_OK) {
        jpeg_err_to_mp_exception(ret, "Failed to initialize JPEG decoder object");
    }
    
    memset(&self->io, 0, sizeof(jpeg_dec_io_t));
    return MP_OBJ_FROM_PTR(self);
}

static jpeg_decoder_obj_t* jpeg_decoder_prepare(mp_obj_t self_in, mp_obj_t jpeg_data) {
    jpeg_decoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(jpeg_data, &bufinfo, MP_BUFFER_READ);

    jpeg_error_t ret = JPEG_ERR_OK;
    if (self->io.inbuf != (uint8_t *)bufinfo.buf || self->io.inbuf_len != bufinfo.len || self->block_pos >= self->block_counts) {
        self->io.inbuf = (uint8_t *)bufinfo.buf;
        self->io.inbuf_len = bufinfo.len;
        self->block_pos = 0;

        ret = jpeg_dec_parse_header(self->handle, &self->io, &self->out_info);
        if (ret != JPEG_ERR_OK) {
            jpeg_err_to_mp_exception(ret, "JPEG header parsing failed");
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
    return self;
}

static mp_obj_t jpeg_decoder_get_img_info(mp_obj_t self_in, mp_obj_t jpeg_data) {
    jpeg_decoder_obj_t *self = jpeg_decoder_prepare(self_in, jpeg_data);
    mp_obj_t list = mp_obj_new_list(0, NULL);
    mp_obj_list_append(list, mp_obj_new_int(self->out_info.width));
    mp_obj_list_append(list, mp_obj_new_int(self->out_info.height));
    if (self->config.block_enable) {
        mp_obj_list_append(list, mp_obj_new_int(self->block_counts));
        mp_obj_list_append(list, mp_obj_new_int(self->out_info.height / self->block_counts));
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_2(jpeg_decoder_get_img_info_obj, jpeg_decoder_get_img_info);

// Initialize stream decoding
static mp_obj_t jpeg_decoder_init_stream(mp_obj_t self_in, mp_obj_t camera_obj) {
    jpeg_decoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    
    // Store camera object reference
    self->camera_obj = camera_obj;

    // Check if queue already exists
    if (frame_queue != NULL) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Stream already initialized"));
    }

    // Create frame queue
    frame_queue = xQueueCreate(QUEUE_SIZE, sizeof(stream_frame_t));
    if (frame_queue == NULL) {
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("Failed to create frame queue"));
    }

    // Create decoding task
    BaseType_t ret = xTaskCreatePinnedToCore(
        decode_task,
        "jpeg_decode",
        STACK_SIZE,
        self,
        5,
        &decode_task_handle,
        0
    );

    if (ret != pdPASS) {
        vQueueDelete(frame_queue);
        frame_queue = NULL;
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Failed to create decode task"));
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(jpeg_decoder_init_stream_obj, jpeg_decoder_init_stream);

// Check if decoded frame is available
static mp_obj_t jpeg_decoder_frame_available(mp_obj_t self_in) {
    if (frame_queue == NULL) {
        return mp_const_false;
    }
    return mp_obj_new_bool(uxQueueMessagesWaiting(frame_queue) > 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(jpeg_decoder_frame_available_obj, jpeg_decoder_frame_available);

// Get decoded frame from queue
static mp_obj_t jpeg_decoder_decode_stream(mp_obj_t self_in) {
    if (frame_queue == NULL) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Stream not initialized"));
    }

    stream_frame_t frame;
    if (xQueueReceive(frame_queue, &frame, pdMS_TO_TICKS(10)) == pdTRUE) {
        // Create tuple with both JPEG and decoded data
        mp_obj_tuple_t *tuple = MP_OBJ_TO_PTR(mp_obj_new_tuple(2, NULL));
        
        // Original JPEG frame
        tuple->items[0] = frame.jpeg_data;
        
        // Decoded data
        tuple->items[1] = mp_obj_new_memoryview(MP_BUFFER_READ, frame.decoded_len, frame.decoded_data);
        
        return MP_OBJ_FROM_PTR(tuple);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(jpeg_decoder_decode_stream_obj, jpeg_decoder_decode_stream);

// Stop streaming and cleanup
static mp_obj_t jpeg_decoder_stop_stream(mp_obj_t self_in) {
    if (decode_task_handle != NULL) {
        vTaskDelete(decode_task_handle);
        decode_task_handle = NULL;
    }
    if (frame_queue != NULL) {
        // Clear and delete queue
        stream_frame_t frame;
        while (xQueueReceive(frame_queue, &frame, 0) == pdTRUE) {
            jpeg_free_align(frame.decoded_data);
            // Free camera buffer by calling free_buffer method
            mp_obj_t camera = ((jpeg_decoder_obj_t *)MP_OBJ_TO_PTR(self_in))->camera_obj;
            call_camera_method(camera, "free_buffer");
        }
        vQueueDelete(frame_queue);
        frame_queue = NULL;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(jpeg_decoder_stop_stream_obj, jpeg_decoder_stop_stream);

// `decode()` methods
static mp_obj_t jpeg_decoder_decode_block(mp_obj_t self_in, mp_obj_t jpeg_data) {
    jpeg_decoder_obj_t *self = jpeg_decoder_prepare(self_in, jpeg_data);
    // decode the next block
    if (self->block_pos < self->block_counts) {
        jpeg_error_t ret = jpeg_dec_process(self->handle, &self->io);
        if (ret != JPEG_ERR_OK) {
            jpeg_err_to_mp_exception(ret, "JPEG decoding failed");
        }
        self->block_pos++;
        
        if (self->return_bytes) {
            return mp_obj_new_bytes(self->io.outbuf, self->io.out_size);
        }
        return mp_obj_new_memoryview(MP_BUFFER_READ, self->io.out_size, self->io.outbuf);
    } else {
        return mp_const_none;
    }    
}
static MP_DEFINE_CONST_FUN_OBJ_2(jpeg_decoder_decode_block_obj, jpeg_decoder_decode_block);

// `__del__()` method
static mp_obj_t jpeg_decoder_del(mp_obj_t self_in) {
    jpeg_decoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    
    // Stop streaming if active
    jpeg_decoder_stop_stream(self_in);
    
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
    {MP_ROM_QSTR(MP_QSTR_decode), MP_ROM_PTR(&jpeg_decoder_decode_block_obj)},
    {MP_ROM_QSTR(MP_QSTR_get_img_info), MP_ROM_PTR(&jpeg_decoder_get_img_info_obj)},
    {MP_ROM_QSTR(MP_QSTR_init_stream), MP_ROM_PTR(&jpeg_decoder_init_stream_obj)},
    {MP_ROM_QSTR(MP_QSTR_frame_available), MP_ROM_PTR(&jpeg_decoder_frame_available_obj)},
    {MP_ROM_QSTR(MP_QSTR_decode_stream), MP_ROM_PTR(&jpeg_decoder_decode_stream_obj)},
    {MP_ROM_QSTR(MP_QSTR_stop_stream), MP_ROM_PTR(&jpeg_decoder_stop_stream_obj)},
    {MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&jpeg_decoder_del_obj)},
    {MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&mp_identity_obj)},
    {MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&jpeg_decoder_del_obj)},
};

static MP_DEFINE_CONST_DICT(jpeg_decoder_locals_dict, jpeg_decoder_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    mp_jpeg_decoder_type,
    MP_QSTR_Decoder,
    MP_TYPE_FLAG_NONE,
    make_new, jpeg_decoder_make_new,
    locals_dict, &jpeg_decoder_locals_dict
);

// Encoder object

// type structure for the JPEG encoder object
extern const mp_obj_type_t mp_jpeg_encoder_type;

typedef struct _jpeg_encoder_obj_t {
    mp_obj_base_t base;
    jpeg_enc_handle_t jpeg_enc;
    jpeg_enc_config_t config;
    uint8_t *workbuf;
    int workbuf_size;
} jpeg_encoder_obj_t;

// Consturctor function for the JPEG encoder object
static mp_obj_t jpeg_encoder_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_height, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_width, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_format, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_quality, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 90} },
        { MP_QSTR_rotation, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
    };

    mp_arg_val_t parsed_args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed_args);

    jpeg_encoder_obj_t *self = mp_obj_malloc_with_finaliser(jpeg_encoder_obj_t, &mp_jpeg_encoder_type);
    self->jpeg_enc = NULL;

    self->config = (jpeg_enc_config_t)DEFAULT_JPEG_ENC_CONFIG();
    self->config.height = parsed_args[0].u_int;
    self->config.width = parsed_args[1].u_int;
    self->config.quality = parsed_args[3].u_int;
    self->config.rotate = jpeg_get_rotation_code(parsed_args[4].u_int);
    if (parsed_args[2].u_obj != mp_const_none) {
        self->config.src_type = jpeg_get_format_code(mp_obj_str_get_str(parsed_args[2].u_obj));
    }

    jpeg_error_t ret = jpeg_enc_open(&self->config, &self->jpeg_enc);
    if (ret != JPEG_ERR_OK) {
        jpeg_err_to_mp_exception(ret, "Failed to initialize JPEG encoder object");
    }

    self->workbuf_size = self->config.width * self->config.height;
    self->workbuf = (uint8_t *)calloc(1, self->workbuf_size);
    if (self->workbuf == NULL) {
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("Failed to allocate work buffer"));
    }

    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t jpeg_encoder_encode(mp_obj_t self_in, mp_obj_t img_data) {
    jpeg_encoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(img_data, &bufinfo, MP_BUFFER_READ);

    jpeg_error_t ret = JPEG_ERR_OK;
    int out_len = 0;
    // encode the image
    ret = jpeg_enc_process(self->jpeg_enc, bufinfo.buf, bufinfo.len, self->workbuf, self->workbuf_size, &out_len);
    if (ret != JPEG_ERR_OK) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("JPEG encoding failed"));
    }

    mp_obj_t out_data = mp_obj_new_bytes(self->workbuf, out_len);
    return out_data;
}
static MP_DEFINE_CONST_FUN_OBJ_2(jpeg_encoder_encode_obj, jpeg_encoder_encode);

// `__del__()` method
static mp_obj_t jpeg_encoder_del(mp_obj_t self_in) {
    jpeg_encoder_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->jpeg_enc) {
        jpeg_enc_close(self->jpeg_enc);
        self->jpeg_enc = NULL;
    }
    if (self->workbuf) {
        free(self->workbuf);
        self->workbuf = NULL;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(jpeg_encoder_del_obj, jpeg_encoder_del);

static const mp_rom_map_elem_t jpeg_encoder_locals_dict_table[] = {
    {MP_ROM_QSTR(MP_QSTR_encode), MP_ROM_PTR(&jpeg_encoder_encode_obj)},
    {MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&jpeg_encoder_del_obj)},
    {MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&mp_identity_obj)},
    {MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&jpeg_encoder_del_obj)},
};
static MP_DEFINE_CONST_DICT(jpeg_encoder_locals_dict, jpeg_encoder_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    mp_jpeg_encoder_type,
    MP_QSTR_Encoder,
    MP_TYPE_FLAG_NONE,
    make_new, jpeg_encoder_make_new,
    locals_dict, &jpeg_encoder_locals_dict
);

static const mp_rom_map_elem_t jpeg_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR_Decoder), MP_ROM_PTR(&mp_jpeg_decoder_type)},
    {MP_ROM_QSTR(MP_QSTR_Encoder), MP_ROM_PTR(&mp_jpeg_encoder_type)},
};

static MP_DEFINE_CONST_DICT(mp_module_jpeg_globals, jpeg_module_globals_table);

const mp_obj_module_t mp_module_jpeg = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&mp_module_jpeg_globals,
};

MP_REGISTER_MODULE(MP_QSTR_jpeg, mp_module_jpeg);
