#include "tinybuf_plugin.h"
#include "tinybuf.h"
#include "tinybuf_buffer.h"
#include "tinybuf_memory.h"
#include "tinybuf_support.h"
#include <stdint.h>

#define TINYBUF_PLUGIN_UPPER_STRING 200

static inline int buf_offset_local(buf_ref *buf, int64_t offset)
{
    if (offset < 0 || offset > buf->size)
        return -1;
    buf->ptr += offset;
    buf->size -= offset;
    return 0;
}

static int plugin_upper_read(uint8_t type, buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_error *r)
{
    (void)contain_handler;
    if (type != TINYBUF_PLUGIN_UPPER_STRING)
        return -1;
    if (buf->size < 2)
        return 0;
    uint8_t t = (uint8_t)buf->ptr[0];
    if (t != TINYBUF_PLUGIN_UPPER_STRING)
        return -1;
    uint8_t len = (uint8_t)buf->ptr[1];
    if (buf->size < (int64_t)(2 + len))
        return 0;
    const char *p = buf->ptr + 2;
    char *tmp = (char *)tinybuf_malloc(len);
    for (int i = 0; i < len; ++i)
    {
        char c = p[i];
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        tmp[i] = c;
    }
    tinybuf_value_init_string(out, tmp, len);
    int pidx = tinybuf_plugin_get_runtime_index_by_type(TINYBUF_PLUGIN_UPPER_STRING);
    tinybuf_value_set_plugin_index(out, pidx);
    tinybuf_free(tmp);
    buf_offset_local(buf, 2 + len);
    return 2 + len;
}

static int plugin_upper_write(uint8_t type, const tinybuf_value *in, buffer *out, tinybuf_error *r)
{
    if (type != TINYBUF_PLUGIN_UPPER_STRING)
        return -1;
    tinybuf_error gr = tinybuf_result_ok(0);
    buffer *s = tinybuf_value_get_string(in, &gr);
    if (!s)
        return -1;
    int len = buffer_get_length(s);
    if (len > 255)
        len = 255;
    uint8_t t = TINYBUF_PLUGIN_UPPER_STRING;
    buffer_append(out, (const char *)&t, 1);
    uint8_t l = (uint8_t)len;
    buffer_append(out, (const char *)&l, 1);
    buffer_append(out, buffer_get_data(s), len);
    return 2 + len;
}

static int plugin_upper_dump(uint8_t type, buf_ref *buf, buffer *out, tinybuf_error *r)
{
    if (type != TINYBUF_PLUGIN_UPPER_STRING)
        return -1;
    if (buf->size < 2)
        return 0;
    uint8_t t = (uint8_t)buf->ptr[0];
    if (t != TINYBUF_PLUGIN_UPPER_STRING)
        return -1;
    uint8_t len = (uint8_t)buf->ptr[1];
    if (buf->size < (int64_t)(2 + len))
        return 0;
    const char *p = buf->ptr + 2;
    buffer_append(out, "\"", 1);
    for (int i = 0; i < len; ++i)
    {
        char c = p[i];
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        buffer_append(out, &c, 1);
    }
    buffer_append(out, "\"", 1);
    return 2 + len;
}

static int plugin_upper_show_value(uint8_t type, const tinybuf_value *in, buffer *out, tinybuf_error *r)
{
    if (type != TINYBUF_PLUGIN_UPPER_STRING)
        return -1;
    tinybuf_error gr = tinybuf_result_ok(0);
    buffer *s = tinybuf_value_get_string(in, &gr);
    if (!s)
        return -1;
    buffer_append(out, "upper(", 6);
    buffer_append(out, buffer_get_data(s), buffer_get_length(s));
    buffer_append(out, ")", 1);
    return buffer_get_length(s) + 7;
}

static int plugin_upper_to_lower(tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out)
{
    (void)args;
    tinybuf_error gr = tinybuf_result_ok(0);
    buffer *s = tinybuf_value_get_string(value, &gr);
    if (!s)
        return -1;
    int len = buffer_get_length(s);
    char *tmp = (char *)tinybuf_malloc(len);
    const char *p = buffer_get_data(s);
    for (int i = 0; i < len; ++i)
    {
        char c = p[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + ('a' - 'A'));
        tmp[i] = c;
    }
    tinybuf_value_init_string(out, tmp, len);
    tinybuf_free(tmp);
    return 0;
}

static const uint8_t g_types[] = { TINYBUF_PLUGIN_UPPER_STRING };
static const char *g_op_names[] = { "to_lower" };
static const char *g_op_sigs[] = { "string->string" };
static const char *g_op_descs[] = { "lowercase" };
static tinybuf_plugin_value_op_fn g_op_fns[] = { plugin_upper_to_lower };

static tinybuf_plugin_descriptor g_desc = {
    g_types,
    1,
    "builtin:upper-string",
    plugin_upper_read,
    plugin_upper_write,
    plugin_upper_dump,
    plugin_upper_show_value,
    g_op_names,
    g_op_sigs,
    g_op_descs,
    g_op_fns,
    1
};

tinybuf_plugin_descriptor *tinybuf_get_upper_string_descriptor(void)
{
    return &g_desc;
}

static int custom_string_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_error *r)
{
    (void)name;
    (void)contain_handler;
    tinybuf_value_init_string(out, (const char *)data, len);
    return len;
}
static int custom_string_write(const char *name, const tinybuf_value *in, buffer *out, tinybuf_error *r)
{
    (void)name;
    tinybuf_error gr = tinybuf_result_ok(0);
    buffer *s = tinybuf_value_get_string(in, &gr);
    if (!s)
        return -1;
    int len = buffer_get_length(s);
    if (len > 0)
        buffer_append(out, buffer_get_data(s), len);
    return len;
}
static int custom_string_dump(const char *name, buf_ref *buf, buffer *out, tinybuf_error *r)
{
    (void)name;
    int64_t len = buf->size;
    buffer_append(out, "\"", 1);
    if (len > 0)
        buffer_append(out, buf->ptr, (int)len);
    buffer_append(out, "\"", 1);
    return (int)len;
}

void tinybuf_register_builtin_customs(void)
{
    tinybuf_custom_register("string", custom_string_read, custom_string_write, custom_string_dump);
}

int tinybuf_register_builtin_plugins(void)
{
    int r = tinybuf_plugin_register_descriptor(tinybuf_get_upper_string_descriptor());
    if (r == 0)
        tinybuf_register_builtin_customs();
    return r;
}
