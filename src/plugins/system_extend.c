#include "tinybuf.h"
#include "tinybuf_plugin.h"
#include "tinybuf_buffer.h"
#include "tinybuf_memory.h"
#include <stdio.h>
#include <string.h>
static tinybuf_value *clone_box(const tinybuf_value *in)
{
    buffer *b = buffer_alloc();
    tinybuf_error wr = tinybuf_result_ok(0);
    int wl = tinybuf_try_write_box(b, in, &wr);
    if (wl <= 0)
    {
        buffer_free(b);
        return NULL;
    }
    buf_ref br = (buf_ref){buffer_get_data(b), (int64_t)buffer_get_length(b), buffer_get_data(b), (int64_t)buffer_get_length(b)};
    tinybuf_value *out = tinybuf_value_alloc();
    tinybuf_error rr = tinybuf_result_ok(0);
    int rl = tinybuf_try_read_box(&br, out, NULL, &rr);
    buffer_free(b);
    if (rl <= 0)
    {
        tinybuf_value_free(out);
        return NULL;
    }
    return out;
}

static int op_hlist_insert(tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out)
{
    if (tinybuf_value_get_type(value) != tinybuf_array)
    {
        return -1;
    }
    tinybuf_error cr0 = tinybuf_result_ok(0);
    int n = tinybuf_value_get_child_size(value, &cr0);
    int idx = n;
    const tinybuf_value *ins = NULL;
    if (args && tinybuf_value_get_type(args) == tinybuf_array)
    {
        tinybuf_error rr0 = tinybuf_result_ok(0);
        tinybuf_error rr1 = tinybuf_result_ok(0);
        const tinybuf_value *a0 = tinybuf_value_get_array_child(args, 0, &rr0);
        const tinybuf_value *a1 = tinybuf_value_get_array_child(args, 1, &rr1);
        if (a0 && tinybuf_value_get_type(a0) == tinybuf_int)
        {
            tinybuf_error gri = tinybuf_result_ok(0);
            idx = (int)tinybuf_value_get_int(a0, &gri);
        }
        if (a1)
        {
            ins = a1;
        }
    }
    if (!ins)
    {
        return -1;
    }
    tinybuf_value_clear(out);
    tinybuf_error cr1 = tinybuf_result_ok(0);
    int before = tinybuf_value_get_child_size(value, &cr1);
    for (int i = 0; i <= before; i++)
    {
        if (i == idx)
        {
            tinybuf_value *cpins = clone_box(ins);
            if (!cpins)
                return -1;
            tinybuf_value_array_append(out, cpins);
        }
        if (i < before)
        {
            tinybuf_error rr2 = tinybuf_result_ok(0);
            const tinybuf_value *ch = tinybuf_value_get_array_child(value, i, &rr2);
            tinybuf_value *cp = clone_box(ch);
            if (!cp)
                return -1;
            tinybuf_value_array_append(out, cp);
        }
    }
    return 0;
}

static int op_hlist_delete(tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out)
{
    if (tinybuf_value_get_type(value) != tinybuf_array)
    {
        return -1;
    }
    tinybuf_error cr2 = tinybuf_result_ok(0);
    int n = tinybuf_value_get_child_size(value, &cr2);
    int idx = -1;
    if (args && tinybuf_value_get_type(args) == tinybuf_array)
    {
        tinybuf_error rrA = tinybuf_result_ok(0);
        const tinybuf_value *a0 = tinybuf_value_get_array_child(args, 0, &rrA);
        if (a0 && tinybuf_value_get_type(a0) == tinybuf_int)
        {
            tinybuf_error gri2 = tinybuf_result_ok(0);
            idx = (int)tinybuf_value_get_int(a0, &gri2);
        }
    }
    if (idx < 0 || idx >= n)
    {
        return -1;
    }
    tinybuf_value_clear(out);
    for (int i = 0; i < n; i++)
    {
        if (i == idx)
            continue;
        tinybuf_error rr3 = tinybuf_result_ok(0);
        const tinybuf_value *ch = tinybuf_value_get_array_child(value, i, &rr3);
        tinybuf_value *cp = clone_box(ch);
        if (!cp)
            return -1;
        tinybuf_value_array_append(out, cp);
    }
    return 0;
}

static int op_hlist_concat(tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out)
{
    if (tinybuf_value_get_type(value) != tinybuf_array)
    {
        return -1;
    }
    const tinybuf_value *other = NULL;
    if (args && tinybuf_value_get_type(args) == tinybuf_array)
    {
        tinybuf_error rr4 = tinybuf_result_ok(0);
        other = tinybuf_value_get_array_child(args, 0, &rr4);
    }
    if (!other || tinybuf_value_get_type(other) != tinybuf_array)
    {
        return -1;
    }
    tinybuf_value_clear(out);
    tinybuf_error cr3 = tinybuf_result_ok(0);
    int n = tinybuf_value_get_child_size(value, &cr3);
    for (int i = 0; i < n; i++)
    {
        tinybuf_error rr5 = tinybuf_result_ok(0);
        const tinybuf_value *ch = tinybuf_value_get_array_child(value, i, &rr5);
        tinybuf_value *cp = clone_box(ch);
        if (!cp)
            return -1;
        tinybuf_value_array_append(out, cp);
    }
    tinybuf_error cr4 = tinybuf_result_ok(0);
    int m = tinybuf_value_get_child_size(other, &cr4);
    for (int j = 0; j < m; j++)
    {
        tinybuf_error rr6 = tinybuf_result_ok(0);
        const tinybuf_value *ch2 = tinybuf_value_get_array_child(other, j, &rr6);
        tinybuf_value *cp2 = clone_box(ch2);
        if (!cp2)
            return -1;
        tinybuf_value_array_append(out, cp2);
    }
    return 0;
}

static int op_str(tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out)
{
    (void)args;
    char buf[64];
    tinybuf_error cr5 = tinybuf_result_ok(0);
    int n = tinybuf_value_get_child_size(value, &cr5);
    int len = snprintf(buf, sizeof(buf), "hetero_list[%d]", n);
    if (len < 0)
        len = 0;
    tinybuf_value_init_string(out, buf, len);
    return 0;
}
static int op_desc(tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out)
{
    (void)args;
    const char *s = "system.extend hetero_list";
    tinybuf_value_init_string(out, s, (int)strlen(s));
    return 0;
}

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
    {
        tinybuf_error er = tinybuf_result_err(-1, "tuple_write type mismatch", NULL);
        tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
        return -1;
    }
    int n = tinybuf_value_serialize(in, out, r);
    return n;
}
static int tuple_dump(const char *name, buf_ref *buf, buffer *out, tinybuf_error *r)
{
    (void)name;
    int rlen = tinybuf_dump_buffer_as_text(buf->ptr, (int)buf->size, out);
    if (rlen > 0)
    {
        tinybuf_error ok = tinybuf_result_ok(rlen);
        tinybuf_result_append_merge(r, &ok, tinybuf_merger_sum);
        return rlen;
    }
    tinybuf_error er = tinybuf_result_err(rlen, "tuple_dump failed", NULL);
    tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
    return rlen;
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
            tinybuf_result_add_msg_const(r, "hlist_read item failed");
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
    {
        tinybuf_error er = tinybuf_result_err(-1, "hlist_write type mismatch", NULL);
        tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
        return -1;
    }
    int before = buffer_get_length(out);
    tinybuf_error cr6 = tinybuf_result_ok(0);
    int n = tinybuf_value_get_child_size(in, &cr6);
    if (tinybuf_result_msg_count(&cr6) > 0)
    {
        tinybuf_result_append_merge(r, &cr6, tinybuf_merger_left);
        tinybuf_error er = tinybuf_result_err(-1, "hlist_write container error", NULL);
        tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
        return -1;
    }
    for (int i = 0; i < n; ++i)
    {
        tinybuf_error rr7 = tinybuf_result_ok(0);
        const tinybuf_value *ch = tinybuf_value_get_array_child(in, i, &rr7);
        if (!ch)
        {
            tinybuf_result_append_merge(r, &rr7, tinybuf_merger_left);
            tinybuf_error er = tinybuf_result_err(-1, "hlist_write null child", NULL);
            tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
            return -1;
        }
        int wn = tinybuf_try_write_box(out, ch, r);
        if (wn <= 0)
        {
            tinybuf_result_add_msg_const(r, "hlist_write child failed");
            return wn;
        }
    }
    int after = buffer_get_length(out);
    return after - before;
}
static int hlist_dump(const char *name, buf_ref *buf, buffer *out, tinybuf_error *r)
{
    (void)name;
    int rlen = tinybuf_dump_buffer_as_text(buf->ptr, (int)buf->size, out);
    if (rlen > 0)
    {
        tinybuf_error ok = tinybuf_result_ok(rlen);
        tinybuf_result_append_merge(r, &ok, tinybuf_merger_sum);
        return rlen;
    }
    tinybuf_error er = tinybuf_result_err(rlen, "hlist_dump failed", NULL);
    tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
    return rlen;
}


TB_EXPORT int tinybuf_plugin_init(void)
{
    tinybuf_custom_register("hetero_tuple", tuple_read, tuple_write, tuple_dump);
    tinybuf_custom_register("hetero_list", hlist_read, hlist_write, hlist_dump);
    return 0;
}

TB_EXPORT int tinybuf_plugin_init_with_host(int (*host_custom_register)(const char *, tinybuf_custom_read_fn, tinybuf_custom_write_fn, tinybuf_custom_dump_fn))
{
    host_custom_register("hetero_tuple", tuple_read, tuple_write, tuple_dump);
    host_custom_register("hetero_list", hlist_read, hlist_write, hlist_dump);
    return 0;
}

TB_EXPORT tinybuf_plugin_descriptor *tinybuf_get_plugin_descriptor(void)
{
    static uint8_t tags[1] = {0};
    static const char *op_names[] = {"hlist_insert", "hlist_delete", "hlist_concat", "__str__", "__desc__"};
    static const char *op_sigs[] = {"(list,int,value)->list", "(list,int)->list", "(list,list)->list", "(list)->string", "(list)->string"};
    static const char *op_descs[] = {"insert element", "delete element", "concat lists", "stringify", "describe"};
    static tinybuf_plugin_value_op_fn op_fns[] = {op_hlist_insert, op_hlist_delete, op_hlist_concat, op_str, op_desc};
    static tinybuf_plugin_descriptor d;
    d.tags = tags;
    d.tag_count = 0;
    d.guid = "plugin:system.extend";
    d.read = NULL;
    d.write = NULL;
    d.dump = NULL;
    d.show_value = NULL;
    d.op_names = op_names;
    d.op_sigs = op_sigs;
    d.op_descs = op_descs;
    d.op_fns = op_fns;
    d.op_count = 5;
    return &d;
}
