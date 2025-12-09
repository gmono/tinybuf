#ifndef TINYBUF_PLUGIN_H
#define TINYBUF_PLUGIN_H
#include <stdint.h>
#include "tinybuf.h"
#include "tinybuf_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    const char *base;
    int64_t all_size;
    const char *ptr;
    int64_t size;
} buf_ref;

typedef int (*CONTAIN_HANDLER)(uint64_t);

typedef int (*tinybuf_plugin_read_fn)(uint8_t type, buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler);
typedef int (*tinybuf_plugin_write_fn)(uint8_t type, const tinybuf_value *in, buffer *out);

int tinybuf_plugin_register(const uint8_t *types, int type_count, tinybuf_plugin_read_fn read, tinybuf_plugin_write_fn write);
int tinybuf_plugin_unregister_all(void);

int tinybuf_plugins_try_read_by_type(uint8_t type, buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler);
int tinybuf_plugins_try_write(uint8_t type, const tinybuf_value *in, buffer *out);

int tinybuf_try_read_box_with_plugins(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler);

// pointer read mode API
int tinybuf_try_read_box_with_mode(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_read_pointer_mode mode);

// core try-read/try-write APIs
int tinybuf_try_read_box(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler);
int tinybuf_try_write_box(buffer *out, const tinybuf_value *value);
int tinybuf_try_write_version_box(buffer *out, uint64_t version, const tinybuf_value *box);
int tinybuf_try_write_version_list(buffer *out, const uint64_t *versions, const tinybuf_value **boxes, int count);

int tinybuf_plugin_register_from_dll(const char *dll_path);

typedef enum {
    tinybuf_offset_start = 0,
    tinybuf_offset_end = 1,
    tinybuf_offset_current = 2
} tinybuf_offset_type;

int tinybuf_try_write_pointer(buffer *out, tinybuf_offset_type t, int64_t offset);
int tinybuf_try_write_sub_ref(buffer *out, tinybuf_offset_type t, int64_t offset);

#ifdef __cplusplus
}
#endif

#endif
