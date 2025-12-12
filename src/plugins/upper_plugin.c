#include "tinybuf_plugin.h"
#include "tinybuf.h"
#include "tinybuf_buffer.h"
#include "tinybuf_memory.h"
#include <string.h>

#define DLL_UPPER_TYPE 201

static int dll_upper_read(uint8_t type, buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_error *r)
{
    (void)contain_handler;
    if (type != DLL_UPPER_TYPE)
    {
        tinybuf_error er = tinybuf_result_err(-1, "type mismatch", NULL);
        tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
        return -1;
    }
    if (buf->size < 2)
        return 0;
    uint8_t t = (uint8_t)buf->ptr[0];
    if (t != DLL_UPPER_TYPE)
    {
        tinybuf_error er = tinybuf_result_err(-1, "tag mismatch", NULL);
        tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
        return -1;
    }
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
    tinybuf_value_set_plugin_index(out, tinybuf_plugin_get_runtime_index_by_tag(DLL_UPPER_TYPE));
    tinybuf_free(tmp);
    buf->ptr += 2 + len;
    buf->size -= 2 + len;
    return 2 + len;
}

static int dll_upper_write(uint8_t type, const tinybuf_value *in, buffer *out, tinybuf_error *r)
{
    if (type != DLL_UPPER_TYPE)
    {
        tinybuf_error er = tinybuf_result_err(-1, "type mismatch", NULL);
        tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
        return -1;
    }
    tinybuf_error gr = tinybuf_result_ok(0);
    buffer *s = tinybuf_value_get_string(in, &gr);
    if (!s)
    {
        tinybuf_error er = tinybuf_result_err(-1, "upper write: not string", NULL);
        tinybuf_result_append_merge(&er, &gr, tinybuf_merger_left);
        tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
        return -1;
    }
    int len = buffer_get_length(s);
    if (len > 255)
        len = 255;
    uint8_t t = DLL_UPPER_TYPE;
    buffer_append(out, (const char *)&t, 1);
    uint8_t l = (uint8_t)len;
    buffer_append(out, (const char *)&l, 1);
    buffer_append(out, buffer_get_data(s), len);
    return 2 + len;
}

static int dll_upper_dump(uint8_t type, buf_ref *buf, buffer *out, tinybuf_error *r)
{
    if (type != DLL_UPPER_TYPE)
    {
        tinybuf_error er = tinybuf_result_err(-1, "type mismatch", NULL);
        tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
        return -1;
    }
    if (buf->size < 2)
        return 0;
    uint8_t t = (uint8_t)buf->ptr[0];
    if (t != DLL_UPPER_TYPE)
    {
        tinybuf_error er = tinybuf_result_err(-1, "tag mismatch", NULL);
        tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
        return -1;
    }
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

static int dll_upper_show_value(uint8_t type, const tinybuf_value *in, buffer *out, tinybuf_error *r)
{
    if (type != DLL_UPPER_TYPE)
    {
        tinybuf_error er = tinybuf_result_err(-1, "type mismatch", NULL);
        tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
        return -1;
    }
    tinybuf_error gr = tinybuf_result_ok(0);
    buffer *s = tinybuf_value_get_string(in, &gr);
    if (!s)
    {
        tinybuf_error er = tinybuf_result_err(-1, "null string", NULL);
        tinybuf_result_append_merge(&er, &gr, tinybuf_merger_left);
        tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
        return -1;
    }
    buffer_append(out, "dll_upper(", 10);
    buffer_append(out, buffer_get_data(s), buffer_get_length(s));
    buffer_append(out, ")", 1);
    return buffer_get_length(s) + 11;
}

static int dll_to_lower(tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out)
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
TB_EXPORT tinybuf_plugin_descriptor *tinybuf_get_plugin_descriptor(void)
{
    static uint8_t tags[1] = {DLL_UPPER_TYPE};
    static const char *names[1] = {"to_lower"};
    static const char *sigs[1] = {"string->string"};
    static const char *descs[1] = {"lowercase"};
    static tinybuf_plugin_value_op_fn fns[1] = {dll_to_lower};
    static tinybuf_plugin_descriptor d;
    d.tags = tags;
    d.tag_count = 1;
    d.guid = "dll:upper-string";
    d.read = dll_upper_read;
    d.write = dll_upper_write;
    d.dump = dll_upper_dump;
    d.show_value = dll_upper_show_value;
    d.op_names = names;
    d.op_sigs = sigs;
    d.op_descs = descs;
    d.op_fns = fns;
    d.op_count = 1;
    return &d;
}
