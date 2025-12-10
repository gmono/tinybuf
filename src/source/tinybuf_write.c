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


tinybuf_result try_write_version_box(buffer *out, uint64_t version, const tinybuf_value *box)
{
    int len = 0;
    tinybuf_result r1 = try_write_type(out, serialize_version);
    if (r1.res <= 0) return r1; len += r1.res;
    tinybuf_result r2 = try_write_int_data(0, out, version);
    if (r2.res <= 0) return r2; len += r2.res;
    tinybuf_result r3 = try_write_box(out, box);
    if (r3.res <= 0) return r3; len += r3.res;
    return tinybuf_result_ok(len);
}

tinybuf_result try_write_version_list(buffer *out, const uint64_t *versions, const tinybuf_value **boxes, int count)
{
    int len = 0;
    tinybuf_result r0 = try_write_type(out, serialize_version_list);
    if (r0.res <= 0) return r0; len += r0.res;
    tinybuf_result rcnt = try_write_int_data(0, out, (uint64_t)count);
    if (rcnt.res <= 0) return rcnt; len += rcnt.res;
    for (int i = 0; i < count; ++i)
    {
        tinybuf_result ra = try_write_int_data(0, out, versions[i]);
        if (ra.res <= 0) return ra; len += ra.res;
        tinybuf_result rb = try_write_box(out, boxes[i]);
        if (rb.res <= 0) return rb; len += rb.res;
    }
    return tinybuf_result_ok(len);
}

tinybuf_result try_write_box(buffer *out, const tinybuf_value *value)
{
    if(!s_use_strpool){
        int before = buffer_get_length_inline(out);
        tinybuf_result r = tinybuf_value_serialize(value, out);
        if (r.res <= 0) return r;
        int after = buffer_get_length_inline(out);
        return tinybuf_result_ok(after - before);
    }
    buffer *body = buffer_alloc();
    buffer *pool = buffer_alloc();
    strpool_reset_write(body);
    {
        tinybuf_result rs = tinybuf_value_serialize(value, body);
        if (rs.res <= 0){ buffer_free(body); buffer_free(pool); return rs; }
    }
    {
        tinybuf_result rt = strpool_write_tail(pool);
        if (rt.res <= 0){ buffer_free(body); buffer_free(pool); return rt; }
    }
    int body_len = buffer_get_length_inline(body);
    int pool_len = buffer_get_length_inline(pool);
    uint64_t offset_guess_len = 1; uint8_t tmp[16];
    while(1){ uint64_t off = 1 + offset_guess_len + (uint64_t)body_len; int l = int_serialize_local(off, tmp); if((uint64_t)l == offset_guess_len) break; offset_guess_len = (uint64_t)l; }
    uint64_t final_off = 1 + offset_guess_len + (uint64_t)body_len;
    int before = buffer_get_length_inline(out);
    {
        tinybuf_result rtype = try_write_type(out, serialize_str_pool_table);
        if (rtype.res <= 0){ buffer_free(body); buffer_free(pool); return rtype; }
    }
    {
        tinybuf_result rlen = try_write_int_data(0, out, final_off);
        if (rlen.res <= 0){ buffer_free(body); buffer_free(pool); return rlen; }
    }
    buffer_append(out, buffer_get_data_inline(body), body_len);
    if(pool_len){ buffer_append(out, buffer_get_data_inline(pool), pool_len); }
    int after = buffer_get_length_inline(out);
    buffer_free(body); buffer_free(pool);
    return tinybuf_result_ok(after - before);
}

tinybuf_result try_write_plugin_map_table(buffer *out)
{
    int before = buffer_get_length_inline(out);
    int pc = tinybuf_plugin_get_count();
    {
        tinybuf_result r1 = try_write_type(out, 26);
        if (r1.res <= 0) return r1;
    }
    {
        tinybuf_result r2 = try_write_int_data(0, out, (uint64_t)pc);
        if (r2.res <= 0) return r2;
    }
    for (int i = 0; i < pc; ++i)
    {
        const char *g = tinybuf_plugin_get_guid(i);
        if (!g) g = "";
        int gl = (int)strlen(g);
        {
            tinybuf_result r3 = try_write_type(out, serialize_string);
            if (r3.res <= 0) return r3;
        }
        {
            tinybuf_result r4 = try_write_int_data(0, out, (uint64_t)gl);
            if (r4.res <= 0) return r4;
        }
        if (gl)
        {
            buffer_append(out, g, gl);
        }
    }
    int after = buffer_get_length_inline(out);
    return tinybuf_result_ok(after - before);
}

tinybuf_result try_write_part(buffer *out, const tinybuf_value *value)
{
    buffer *body = buffer_alloc();
    tinybuf_result rbody = try_write_box(body, value);
    if (rbody.res <= 0){ buffer_free(body); return rbody; }
    int body_len = rbody.res;
    int before = buffer_get_length_inline(out);
    {
        tinybuf_result rt = try_write_type(out, serialize_part);
        if (rt.res <= 0){ buffer_free(body); return rt; }
    }
    {
        tinybuf_result ri = try_write_int_data(0, out, (uint64_t)body_len);
        if (ri.res <= 0){ buffer_free(body); return ri; }
    }
    buffer_append(out, buffer_get_data_inline(body), body_len);
    int after = buffer_get_length_inline(out);
    buffer_free(body);
    return tinybuf_result_ok(after - before);
}

tinybuf_result try_write_partitions(buffer *out, const tinybuf_value *mainbox, const tinybuf_value **subs, int count)
{
    int total = 1 + count;
    buffer **parts = (buffer **)tinybuf_malloc(sizeof(buffer *) * total);
    int *lens = (int *)tinybuf_malloc(sizeof(int) * total);
    for (int i = 0; i < total; ++i)
    {
        parts[i] = buffer_alloc();
    }
    {
        tinybuf_result rmain = try_write_part(parts[0], mainbox);
        if (rmain.res <= 0){ for(int i=0;i<total;++i) buffer_free(parts[i]); tinybuf_free(parts); tinybuf_free(lens); return rmain; }
        lens[0] = rmain.res;
    }
    for (int i = 0; i < count; ++i)
    {
        tinybuf_result rsub = try_write_part(parts[1 + i], subs[i]);
        if (rsub.res <= 0){ for(int k=0;k<total;++k) buffer_free(parts[k]); tinybuf_free(parts); tinybuf_free(lens); return rsub; }
        lens[1 + i] = rsub.res;
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
    {
        tinybuf_result rt = try_write_type(out, serialize_part_table);
        if (rt.res <= 0){ for(int k=0;k<total;++k) buffer_free(parts[k]); tinybuf_free(parts); tinybuf_free(lens); tinybuf_free(offs); tinybuf_free(vlen); return rt; }
    }
    {
        tinybuf_result ri = try_write_int_data(0, out, (uint64_t)total);
        if (ri.res <= 0){ for(int k=0;k<total;++k) buffer_free(parts[k]); tinybuf_free(parts); tinybuf_free(lens); tinybuf_free(offs); tinybuf_free(vlen); return ri; }
    }
    for (int i = 0; i < total; ++i)
    {
        tinybuf_result rj = try_write_int_data(0, out, (uint64_t)offs[i]);
        if (rj.res <= 0){ for(int k=0;k<total;++k) buffer_free(parts[k]); tinybuf_free(parts); tinybuf_free(lens); tinybuf_free(offs); tinybuf_free(vlen); return rj; }
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
    return tinybuf_result_ok(after - before);
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

tinybuf_result try_write_pointer_value(buffer *out, enum offset_type t, int64_t offset)
{
    int neg = offset < 0;
    serialize_type pt = make_pointer_type(t, neg);
    int len = 0;
    tinybuf_result r1 = try_write_type(out, pt);
    if (r1.res <= 0) return r1; len += r1.res;
    uint64_t mag = neg ? (uint64_t)(-offset) : (uint64_t)offset;
    tinybuf_result r2 = try_write_int_data(0, out, mag);
    if (r2.res <= 0) return r2; len += r2.res;
    return tinybuf_result_ok(len);
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
    tinybuf_result r = try_write_box(out, value);
    if (r.res > 0){ tinybuf_result_set_current(NULL); (void)tinybuf_result_unref(&rr); return r; }
    tinybuf_result_set_current(NULL); return r;
}

tinybuf_result tinybuf_try_write_version_box(buffer *out, uint64_t version, const tinybuf_value *box)
{
    return try_write_version_box(out, version, box);
}

tinybuf_result tinybuf_try_write_version_list(buffer *out, const uint64_t *versions, const tinybuf_value **boxes, int count)
{
    return try_write_version_list(out, versions, boxes, count);
}

tinybuf_result tinybuf_try_write_plugin_map_table(buffer *out)
{
    return try_write_plugin_map_table(out);
}

tinybuf_result tinybuf_try_write_part(buffer *out, const tinybuf_value *value)
{
    return try_write_part(out, value);
}

tinybuf_result tinybuf_try_write_partitions(buffer *out, const tinybuf_value *mainbox, const tinybuf_value **subs, int count)
{
    return try_write_partitions(out, mainbox, subs, count);
}

tinybuf_result tinybuf_try_write_pointer(buffer *out, int t, int64_t offset)
{
    enum offset_type et = start; if (t == 1) et = end; else if (t == 2) et = current;
    return try_write_pointer_value(out, et, offset);
}

tinybuf_result tinybuf_try_write_sub_ref(buffer *out, int t, int64_t offset)
{
    int len = 0; tinybuf_result r0 = try_write_type(out, serialize_sub_ref); if (r0.res <= 0) return r0; len += r0.res;
    enum offset_type et = (t == 1 ? end : (t == 2 ? current : start));
    tinybuf_result r = try_write_pointer_value(out, et, offset); if (r.res <= 0) return r;
    len += r.res; return tinybuf_result_ok(len);
}

tinybuf_result tinybuf_try_write_array_header(buffer *out, int count)
{
    int len = 0; tinybuf_result r1 = try_write_type(out, serialize_array); if (r1.res <= 0) return r1; len += r1.res; tinybuf_result r2 = try_write_int_data(0, out, (uint64_t)count); if (r2.res <= 0) return r2; len += r2.res; return tinybuf_result_ok(len);
}
tinybuf_result tinybuf_try_write_map_header(buffer *out, int count)
{
    int len = 0; tinybuf_result r1 = try_write_type(out, serialize_map); if (r1.res <= 0) return r1; len += r1.res; tinybuf_result r2 = try_write_int_data(0, out, (uint64_t)count); if (r2.res <= 0) return r2; len += r2.res; return tinybuf_result_ok(len);
}
tinybuf_result tinybuf_try_write_string_raw(buffer *out, const char *str, int len)
{
    int ret = dump_string(len, str, out);
    return tinybuf_result_ok(ret);
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
