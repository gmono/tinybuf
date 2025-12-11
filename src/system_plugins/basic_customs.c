#include "tinybuf_common.h"
#include "tinybuf.h"

static int tuple_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_error *r)
{
    (void)name;
    buf_ref br = (buf_ref){(const char *)data, (int64_t)len, (const char *)data, (int64_t)len};
    int n = tinybuf_try_read_box(&br, out, contain_handler, r);
    return n;
}
static int tuple_write(const char *name, const tinybuf_value *in, buffer *out, tinybuf_error *r)
{
    (void)name;
    if (tinybuf_value_get_type(in) != tinybuf_array)
        return -1;
    int n = tinybuf_try_write_box(out, in, r);
    return n;
}
static int tuple_dump(const char *name, buf_ref *buf, buffer *out, tinybuf_error *r)
{
    (void)name;
    int rlen = tinybuf_dump_buffer_as_text(buf->ptr, (int)buf->size, out);
    return rlen > 0 ? rlen : rlen;
}

static int hlist_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_error *r)
{
    (void)name;
    const char *ptr = (const char *)data;
    int size = len;
    tinybuf_value_clear(out);
    for (;;)
    {
        if (size <= 0)
            break;
        buf_ref br = (buf_ref){(const char *)ptr, (int64_t)size, (const char *)ptr, (int64_t)size};
        tinybuf_value *item = tinybuf_value_alloc();
        int n = tinybuf_try_read_box(&br, item, contain_handler, r);
        if (n <= 0)
        {
            tinybuf_value_free(item);
            return n;
        }
        if (tinybuf_value_get_type(out) != tinybuf_array)
        {
            tinybuf_value_clear(out);
        }
        tinybuf_value_array_append(out, item);
        ptr += n;
        size -= n;
    }
    return len;
}
static int hlist_write(const char *name, const tinybuf_value *in, buffer *out, tinybuf_error *r)
{
    (void)name;
    if (tinybuf_value_get_type(in) != tinybuf_array)
        return -1;
    int before = buffer_get_length(out);
    tinybuf_error cr = tinybuf_result_ok(0);
    int n = tinybuf_value_get_child_size(in, &cr);
    if (tinybuf_result_msg_count(&cr) > 0)
        return -1;
    for (int i = 0; i < n; ++i)
    {
        tinybuf_error rr = tinybuf_result_ok(0);
        const tinybuf_value *ch = tinybuf_value_get_array_child(in, i, &rr);
        if (!ch)
            return -1;
        int wn = tinybuf_try_write_box(out, ch, r);
        if (wn <= 0)
            return wn;
    }
    int after = buffer_get_length(out);
    return after - before;
}
static int hlist_dump(const char *name, buf_ref *buf, buffer *out, tinybuf_error *r)
{
    (void)name;
    int rlen = tinybuf_dump_buffer_as_text(buf->ptr, (int)buf->size, out);
    return rlen > 0 ? rlen : rlen;
}

static int dataframe_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_error *r)
{
    (void)name;
    buf_ref br = (buf_ref){(const char *)data, (int64_t)len, (const char *)data, (int64_t)len};
    int n = tinybuf_try_read_box(&br, out, contain_handler, r);
    return n;
}
static int dataframe_write(const char *name, const tinybuf_value *in, buffer *out, tinybuf_error *r)
{
    (void)name;
    if (tinybuf_value_get_type(in) != tinybuf_indexed_tensor)
        return -1;
    int n = tinybuf_try_write_box(out, in, r);
    return n;
}
static int dataframe_dump(const char *name, buf_ref *buf, buffer *out, tinybuf_error *r)
{
    (void)name;
    int rlen = tinybuf_dump_buffer_as_text(buf->ptr, (int)buf->size, out);
    return rlen > 0 ? rlen : rlen;
}

void tinybuf_register_basic_customs(void)
{
    tinybuf_custom_register("tuple", tuple_read, tuple_write, tuple_dump);
    tinybuf_custom_register("hlist", hlist_read, hlist_write, hlist_dump);
    tinybuf_custom_register("dataframe", dataframe_read, dataframe_write, dataframe_dump);
}
