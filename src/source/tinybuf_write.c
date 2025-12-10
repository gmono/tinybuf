#include "tinybuf_private.h"
#include "tinybuf_buffer.h"
#include "tinybuf_plugin.h"
#include "tinybuf_memory.h"
#include <string.h>

/* local varint encoder used for length probing */
static inline int int_serialize_local(uint64_t in, uint8_t *out_bytes){
    int index = 0;
    while(1){
        out_bytes[index] = (uint8_t)(in & 0x7F);
        in >>= 7;
        if(!in){ break; }
        out_bytes[index] |= 0x80;
        ++index;
    }
    return index + 1;
}


int try_write_version_box(buffer *out, uint64_t version, const tinybuf_value *box)
{
    int len = 0;
    len += try_write_type(out, serialize_version);
    len += try_write_int_data(0, out, version);
    len += try_write_box(out, box);
    return len;
}

int try_write_version_list(buffer *out, const uint64_t *versions, const tinybuf_value **boxes, int count)
{
    int len = 0;
    len += try_write_type(out, serialize_version_list);
    len += try_write_int_data(0, out, (uint64_t)count);
    for (int i = 0; i < count; ++i)
    {
        len += try_write_int_data(0, out, versions[i]);
        len += try_write_box(out, boxes[i]);
    }
    return len;
}

int try_write_box(buffer *out, const tinybuf_value *value)
{
    if(!s_use_strpool){
        int before = buffer_get_length_inline(out);
        tinybuf_value_serialize(value, out);
        int after = buffer_get_length_inline(out);
        return after - before;
    }
    buffer *body = buffer_alloc();
    buffer *pool = buffer_alloc();
    strpool_reset_write(body);
    tinybuf_value_serialize(value, body);
    strpool_write_tail(pool);
    int body_len = buffer_get_length_inline(body);
    int pool_len = buffer_get_length_inline(pool);
    uint64_t offset_guess_len = 1; uint8_t tmp[16];
    while(1){ uint64_t off = 1 + offset_guess_len + (uint64_t)body_len; int l = int_serialize_local(off, tmp); if((uint64_t)l == offset_guess_len) break; offset_guess_len = (uint64_t)l; }
    uint64_t final_off = 1 + offset_guess_len + (uint64_t)body_len;
    int before = buffer_get_length_inline(out);
    try_write_type(out, serialize_str_pool_table);
    try_write_int_data(0, out, final_off);
    buffer_append(out, buffer_get_data_inline(body), body_len);
    if(pool_len){ buffer_append(out, buffer_get_data_inline(pool), pool_len); }
    int after = buffer_get_length_inline(out);
    buffer_free(body); buffer_free(pool);
    return after - before;
}

int try_write_plugin_map_table(buffer *out)
{
    int before = buffer_get_length_inline(out);
    int pc = tinybuf_plugin_get_count();
    try_write_type(out, 26);
    try_write_int_data(0, out, (uint64_t)pc);
    for (int i = 0; i < pc; ++i)
    {
        const char *g = tinybuf_plugin_get_guid(i);
        if (!g) g = "";
        int gl = (int)strlen(g);
        try_write_type(out, serialize_string);
        try_write_int_data(0, out, (uint64_t)gl);
        if (gl)
        {
            buffer_append(out, g, gl);
        }
    }
    int after = buffer_get_length_inline(out);
    return after - before;
}

int try_write_part(buffer *out, const tinybuf_value *value)
{
    buffer *body = buffer_alloc();
    int body_len = try_write_box(body, value);
    int before = buffer_get_length_inline(out);
    try_write_type(out, serialize_part);
    try_write_int_data(0, out, (uint64_t)body_len);
    buffer_append(out, buffer_get_data_inline(body), body_len);
    int after = buffer_get_length_inline(out);
    buffer_free(body);
    return after - before;
}

int try_write_partitions(buffer *out, const tinybuf_value *mainbox, const tinybuf_value **subs, int count)
{
    int total = 1 + count;
    buffer **parts = (buffer **)tinybuf_malloc(sizeof(buffer *) * total);
    int *lens = (int *)tinybuf_malloc(sizeof(int) * total);
    for (int i = 0; i < total; ++i)
    {
        parts[i] = buffer_alloc();
    }
    lens[0] = try_write_part(parts[0], mainbox);
    for (int i = 0; i < count; ++i)
    {
        lens[1 + i] = try_write_part(parts[1 + i], subs[i]);
    }
    uint64_t *offs = (uint64_t *)tinybuf_malloc(sizeof(uint64_t) * total);
    uint64_t *vlen = (uint64_t *)tinybuf_malloc(sizeof(uint64_t) * total);
    for (int i = 0; i < total; ++i) vlen[i] = 1;
    uint8_t tmp[32];
    while (1)
    {
        uint64_t table_len = 1 + (uint64_t)int_serialize_local((uint64_t)total, tmp);
        for (int i = 0; i < total; ++i) table_len += vlen[i];
        offs[0] = table_len;
        for (int i = 1; i < total; ++i) offs[i] = offs[i - 1] + (uint64_t)lens[i - 1];
        int stable = 1;
        for (int i = 0; i < total; ++i)
        {
            int l = int_serialize_local(offs[i], tmp);
            if ((uint64_t)l != vlen[i]) { vlen[i] = (uint64_t)l; stable = 0; }
        }
        if (stable) break;
    }
    int before = buffer_get_length_inline(out);
    try_write_type(out, serialize_part_table);
    try_write_int_data(0, out, (uint64_t)total);
    for (int i = 0; i < total; ++i)
    {
        try_write_int_data(0, out, (uint64_t)offs[i]);
    }
    for (int i = 0; i < total; ++i)
    {
        buffer_append(out, buffer_get_data_inline(parts[i]), buffer_get_length_inline(parts[i]));
    }
    int after = buffer_get_length_inline(out);
    for (int i = 0; i < total; ++i) buffer_free(parts[i]);
    tinybuf_free(parts);
    tinybuf_free(lens);
    tinybuf_free(offs);
    tinybuf_free(vlen);
    return after - before;
}

/* removed duplicate tinybuf_value_serialize; keep single definition in tinybuf.c */
static inline serialize_type make_pointer_type(enum offset_type t, int neg)
{
    switch (t)
    {
    case current:
        return neg ? serialize_pointer_from_current_n : serialize_pointer_from_current_p;
    case start:
        return neg ? serialize_pointer_from_start_n : serialize_pointer_from_start_p;
    case end:
        return neg ? serialize_pointer_from_end_n : serialize_pointer_from_end_p;
    default:
        return serialize_null;
    }
}

int try_write_pointer_value(buffer *out, enum offset_type t, int64_t offset)
{
    int neg = offset < 0;
    serialize_type pt = make_pointer_type(t, neg);
    int len = 0;
    len += try_write_type(out, pt);
    uint64_t mag = neg ? (uint64_t)(-offset) : (uint64_t)offset;
    len += try_write_int_data(0, out, mag);
    return len;
}
static inline tinybuf_result _err_with(const char *msg, int rc)
{
    return tinybuf_result_err(rc, msg, NULL);
}

tinybuf_result tinybuf_try_write_box(buffer *out, const tinybuf_value *value)
{
    tinybuf_result rr = tinybuf_result_err(-1, "tinybuf_try_write_box failed", NULL);
    tinybuf_result_add_msg_const(&rr, "tinybuf_try_write_box_r");
    tinybuf_result_set_current(&rr);
    int r = try_write_box(out, value);
    if (r > 0){ tinybuf_result_set_current(NULL); (void)tinybuf_result_unref(&rr); return tinybuf_result_ok(r); }
    rr.res = r; tinybuf_result_set_current(NULL); return rr;
}

tinybuf_result tinybuf_try_write_version_box(buffer *out, uint64_t version, const tinybuf_value *box)
{
    int r = try_write_version_box(out, version, box);
    if (r > 0) return tinybuf_result_ok(r);
    return _err_with("write version box failed", r);
}

tinybuf_result tinybuf_try_write_version_list(buffer *out, const uint64_t *versions, const tinybuf_value **boxes, int count)
{
    int r = try_write_version_list(out, versions, boxes, count);
    if (r > 0) return tinybuf_result_ok(r);
    return _err_with("write version list failed", r);
}

tinybuf_result tinybuf_try_write_plugin_map_table(buffer *out)
{
    int r = try_write_plugin_map_table(out);
    if (r > 0) return tinybuf_result_ok(r);
    return _err_with("write plugin map failed", r);
}

tinybuf_result tinybuf_try_write_part(buffer *out, const tinybuf_value *value)
{
    int r = try_write_part(out, value);
    if (r > 0) return tinybuf_result_ok(r);
    return _err_with("write part failed", r);
}

tinybuf_result tinybuf_try_write_partitions(buffer *out, const tinybuf_value *mainbox, const tinybuf_value **subs, int count)
{
    int r = try_write_partitions(out, mainbox, subs, count);
    if (r > 0) return tinybuf_result_ok(r);
    return _err_with("write partitions failed", r);
}

tinybuf_result tinybuf_try_write_pointer(buffer *out, int t, int64_t offset)
{
    enum offset_type et = start; if (t == 1) et = end; else if (t == 2) et = current;
    int r = try_write_pointer_value(out, et, offset);
    if (r > 0) return tinybuf_result_ok(r);
    return _err_with("write pointer failed", r);
}

tinybuf_result tinybuf_try_write_sub_ref(buffer *out, int t, int64_t offset)
{
    int len = 0; len += try_write_type(out, serialize_sub_ref);
    enum offset_type et = (t == 1 ? end : (t == 2 ? current : start));
    int r = try_write_pointer_value(out, et, offset); if (r <= 0) return _err_with("write sub_ref failed", r);
    len += r; return tinybuf_result_ok(len);
}

int tinybuf_try_write_array_header(buffer *out, int count)
{
    int len = 0; len += try_write_type(out, serialize_array); len += try_write_int_data(0, out, (uint64_t)count); return len;
}
int tinybuf_try_write_map_header(buffer *out, int count)
{
    int len = 0; len += try_write_type(out, serialize_map); len += try_write_int_data(0, out, (uint64_t)count); return len;
}
int tinybuf_try_write_string_raw(buffer *out, const char *str, int len)
{
    return dump_string(len, str, out);
}

tinybuf_result tinybuf_try_write_custom_id_box(buffer *out, const char *name, const tinybuf_value *in)
{
    buffer *body = buffer_alloc(); buffer *pool = buffer_alloc(); strpool_reset_write(body);
    int idx = strpool_add(name, (int)strlen(name)); uint8_t ty = serialize_type_idx; buffer_append(body, (const char *)&ty, 1); dump_int((uint64_t)idx, body);
    buffer *payload = buffer_alloc(); tinybuf_result wr = tinybuf_custom_try_write(name, in, payload); dump_int((uint64_t)(wr.res > 0 ? wr.res : 0), body);
    if (wr.res > 0){ buffer_append(body, buffer_get_data_inline(payload), buffer_get_length_inline(payload)); }
    strpool_write_tail(pool);
    uint64_t body_len = (uint64_t)buffer_get_length_inline(body); uint64_t offset_guess_len = 1; uint8_t tmpv[16];
    while (1){ uint64_t off = 1 + offset_guess_len + body_len; int l = int_serialize(off, tmpv); if ((uint64_t)l == offset_guess_len) break; offset_guess_len = (uint64_t)l; }
    uint64_t final_off = 1 + offset_guess_len + body_len;
    try_write_type(out, serialize_str_pool_table); try_write_int_data(0, out, final_off); buffer_append(out, buffer_get_data_inline(body), (int)body_len);
    int pool_len = buffer_get_length_inline(pool); if (pool_len){ buffer_append(out, buffer_get_data_inline(pool), pool_len); }
    buffer_free(payload); buffer_free(body); buffer_free(pool);
    if (wr.res <= 0){ tinybuf_result rr = tinybuf_result_err(wr.res, "write custom payload failed", NULL); char *m = (char *)tinybuf_malloc(64); snprintf(m, 64, "name=%s", name ? name : "(null)"); tinybuf_result_add_msg(&rr, m, (tinybuf_deleter_fn)tinybuf_free); return rr; }
    int total = 1 + (int)offset_guess_len + (int)body_len + pool_len; return tinybuf_result_ok(total);
}
