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

typedef int (*tinybuf_plugin_read_fn)(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler);
typedef int (*tinybuf_plugin_write_fn)(uint8_t sign, const tinybuf_value *in, buffer *out);

int tinybuf_plugin_register(const uint8_t *signs, int sign_count, tinybuf_plugin_read_fn read, tinybuf_plugin_write_fn write);
int tinybuf_plugin_unregister_all(void);

int tinybuf_plugins_try_read(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler);
int tinybuf_plugins_try_write(uint8_t sign, const tinybuf_value *in, buffer *out);

int tinybuf_try_read_box_with_plugins(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler);

int tinybuf_plugin_register_from_dll(const char *dll_path);

#ifdef __cplusplus
}
#endif

#endif
