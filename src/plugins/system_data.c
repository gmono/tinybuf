#include "tinybuf.h"
#include "tinybuf_plugin.h"
#include "tinybuf_buffer.h"
#include "tinybuf_memory.h"
#include <string.h>

static int sd_indexed_tensor_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_error *r)
{
    (void)name;
    buf_ref br = (buf_ref){(const char *)data, (int64_t)len, (const char *)data, (int64_t)len};
    tinybuf_value *tmp = tinybuf_value_alloc();
    int n = tinybuf_try_read_box(&br, tmp, contain_handler, r);
    if (n <= 0) { tinybuf_value_free(tmp); return n; }
    if (tinybuf_value_get_type(tmp) == tinybuf_tensor)
    {
        tinybuf_error er1 = tinybuf_result_ok(0);
        tinybuf_error er2 = tinybuf_result_ok(0);
        tinybuf_error er3 = tinybuf_result_ok(0);
        tinybuf_error er4 = tinybuf_result_ok(0);
        int dtype = tinybuf_tensor_get_dtype(tmp, &er1);
        int ndim = tinybuf_tensor_get_ndim(tmp, &er2);
        const int64_t *shape = tinybuf_tensor_get_shape(tmp, &er3);
        int64_t count = tinybuf_tensor_get_count(tmp, &er4);
        const void *data = tinybuf_tensor_get_data_const(tmp, &er1);
        tinybuf_value_init_tensor(out, dtype, shape, ndim, data, count);
        tinybuf_value_free(tmp);
        return n;
    }
    if (tinybuf_value_get_type(tmp) == tinybuf_array)
    {
        tinybuf_value_clear(out);
        tinybuf_error rr = tinybuf_result_ok(0);
        int sz = tinybuf_value_get_child_size(tmp, &rr);
        for (int i = 0; i < sz; ++i)
        {
            const tinybuf_value *child = tinybuf_value_get_array_child(tmp, i, &rr);
            tinybuf_value *cloned = tinybuf_value_clone(child);
            tinybuf_value_array_append(out, cloned);
        }
        tinybuf_value_free(tmp);
        return n;
    }
    tinybuf_value_free(tmp);
    return -1;
}

static int sd_indexed_tensor_write(const char *name, const tinybuf_value *in, buffer *out, tinybuf_error *r)
{
    (void)name;
    if (tinybuf_value_get_type(in) == tinybuf_array || tinybuf_value_get_type(in) == tinybuf_tensor)
    {
        return tinybuf_try_write_box(out, in, r);
    }
    return -1;
}

static int sd_indexed_tensor_dump(const char *name, buf_ref *buf, buffer *out, tinybuf_error *r)
{
    (void)name;
    int rlen = tinybuf_dump_buffer_as_text(buf->ptr, (int)buf->size, out);
    return rlen > 0 ? rlen : rlen;
}

/* dataframe is an alias of indexed_tensor box format */
static int sd_dataframe_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_error *r)
{
    return sd_indexed_tensor_read(name, data, len, out, contain_handler, r);
}
static int sd_dataframe_write(const char *name, const tinybuf_value *in, buffer *out, tinybuf_error *r)
{
    return sd_indexed_tensor_write(name, in, out, r);
}
static int sd_dataframe_dump(const char *name, buf_ref *buf, buffer *out, tinybuf_error *r)
{
    return sd_indexed_tensor_dump(name, buf, out, r);
}

TB_EXPORT int tinybuf_plugin_init(void)
{
    tinybuf_custom_register("indexed_tensor", sd_indexed_tensor_read, sd_indexed_tensor_write, sd_indexed_tensor_dump);
    tinybuf_custom_register("dataframe", sd_dataframe_read, sd_dataframe_write, sd_dataframe_dump);
    return 0;
}

TB_EXPORT int tinybuf_plugin_init_with_host(int (*host_custom_register)(const char *, tinybuf_custom_read_fn, tinybuf_custom_write_fn, tinybuf_custom_dump_fn))
{
    host_custom_register("indexed_tensor", sd_indexed_tensor_read, sd_indexed_tensor_write, sd_indexed_tensor_dump);
    host_custom_register("dataframe", sd_dataframe_read, sd_dataframe_write, sd_dataframe_dump);
    return 0;
}

TB_EXPORT tinybuf_plugin_descriptor *tinybuf_get_plugin_descriptor(void)
{
    static const uint8_t tags[] = { 0 };
    static tinybuf_plugin_descriptor d;
    d.tags = tags;
    d.tag_count = 0;
    d.guid = "plugin:system.data";
    d.read = NULL;
    d.write = NULL;
    d.dump = NULL;
    d.show_value = NULL;
    d.op_names = NULL;
    d.op_sigs = NULL;
    d.op_descs = NULL;
    d.op_fns = NULL;
    d.op_count = 0;
    return &d;
}
