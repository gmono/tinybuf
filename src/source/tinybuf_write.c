#include "tinybuf_private.h"
#include "tinybuf_buffer.h"
#include "tinybuf_plugin.h"
#include "tinybuf_memory.h"
#include <string.h>

/* local varint encoder used for length probing */
static inline int int_serialize_local(uint64_t in, uint8_t *out_bytes)
{
    int index = 0;
    while (1)
    {
        out_bytes[index] = (uint8_t)(in & 0x7F);
        in >>= 7;
        if (!in)
        {
            break;
        }
        out_bytes[index] |= 0x80;
        ++index;
    }
    return index + 1;
}

tinybuf_error try_write_type(buffer *out, serialize_type type)
{
    buffer_append(out, (char *)&type, 1);
    return tinybuf_result_ok(1);
}

tinybuf_error try_write_int_data(int isneg, buffer *out, uint64_t val)
{
    (void)isneg;
    int n = dump_int(val, out);
    return tinybuf_result_ok(n);
}

int try_write_version_box(buffer *out, uint64_t version, const tinybuf_value *box, tinybuf_error *r)
{
    int before = buffer_get_length_inline(out);
    tinybuf_error r1 = try_write_type(out, serialize_version);
    if (r1.res <= 0)
    {
        tinybuf_result_append_merge(r, &r1, tinybuf_merger_left);
        return r1.res;
    }
    tinybuf_error r2 = try_write_int_data(0, out, version);
    if (r2.res <= 0)
    {
        tinybuf_result_append_merge(r, &r2, tinybuf_merger_left);
        return r2.res;
    }
    int wb = try_write_box(out, box, r);
    if (wb <= 0)
        return wb;
    int after = buffer_get_length_inline(out);
    return after - before;
}

int try_write_version_list(buffer *out, const uint64_t *versions, const tinybuf_value **boxes, int count, tinybuf_error *r)
{
    int before = buffer_get_length_inline(out);
    tinybuf_error r0 = try_write_type(out, serialize_version_list);
    if (r0.res <= 0)
    {
        tinybuf_result_append_merge(r, &r0, tinybuf_merger_left);
        return r0.res;
    }
    tinybuf_error rcnt = try_write_int_data(0, out, (uint64_t)count);
    if (rcnt.res <= 0)
    {
        tinybuf_result_append_merge(r, &rcnt, tinybuf_merger_left);
        return rcnt.res;
    }
    for (int i = 0; i < count; ++i)
    {
        tinybuf_error ra = try_write_int_data(0, out, versions[i]);
        if (ra.res <= 0)
        {
            tinybuf_result_append_merge(r, &ra, tinybuf_merger_left);
            return ra.res;
        }
        int wb = try_write_box(out, boxes[i], r);
        if (wb <= 0)
            return wb;
    }
    int after = buffer_get_length_inline(out);
    return after - before;
}

int try_write_box(buffer *out, const tinybuf_value *value, tinybuf_error *r)
{
    if (!s_use_strpool)
    {
        int before = buffer_get_length_inline(out);
        int n = tinybuf_value_serialize(value, out, r);
        if (n <= 0)
            return n;
        int after = buffer_get_length_inline(out);
        return after - before;
    }
    buffer *body = buffer_alloc();
    buffer *pool = buffer_alloc();
    strpool_reset_write(body);
    {
        int n2 = tinybuf_value_serialize(value, body, r);
        if (n2 <= 0)
        {
            buffer_free(body);
            buffer_free(pool);
            return n2;
        }
    }
    {
        tinybuf_error rt = strpool_write_tail(pool);
        if (rt.res <= 0)
        {
            buffer_free(body);
            buffer_free(pool);
            tinybuf_result_append_merge(r, &rt, tinybuf_merger_left);
            return rt.res;
        }
    }
    int body_len = buffer_get_length_inline(body);
    int pool_len = buffer_get_length_inline(pool);
    uint64_t offset_guess_len = 1;
    uint8_t tmp[16];
    while (1)
    {
        uint64_t off = 1 + offset_guess_len + (uint64_t)body_len;
        int l = int_serialize_local(off, tmp);
        if ((uint64_t)l == offset_guess_len)
            break;
        offset_guess_len = (uint64_t)l;
    }
    uint64_t final_off = 1 + offset_guess_len + (uint64_t)body_len;
    int before = buffer_get_length_inline(out);
    {
        tinybuf_error rtype = try_write_type(out, serialize_str_pool_table);
        if (rtype.res <= 0)
        {
            buffer_free(body);
            buffer_free(pool);
            tinybuf_result_append_merge(r, &rtype, tinybuf_merger_left);
            return rtype.res;
        }
    }
    {
        tinybuf_error rlen = try_write_int_data(0, out, final_off);
        if (rlen.res <= 0)
        {
            buffer_free(body);
            buffer_free(pool);
            tinybuf_result_append_merge(r, &rlen, tinybuf_merger_left);
            return rlen.res;
        }
    }
    buffer_append(out, buffer_get_data_inline(body), body_len);
    if (pool_len)
    {
        buffer_append(out, buffer_get_data_inline(pool), pool_len);
    }
    int after = buffer_get_length_inline(out);
    buffer_free(body);
    buffer_free(pool);
    return after - before;
}

int try_write_plugin_map_table(buffer *out, tinybuf_error *r)
{
    int before = buffer_get_length_inline(out);
    int pc = tinybuf_plugin_get_count();
    {
        tinybuf_error r1 = try_write_type(out, 26);
        if (r1.res <= 0)
        {
            tinybuf_result_append_merge(r, &r1, tinybuf_merger_left);
            return r1.res;
        }
    }
    {
        tinybuf_error r2 = try_write_int_data(0, out, (uint64_t)pc);
        if (r2.res <= 0)
        {
            tinybuf_result_append_merge(r, &r2, tinybuf_merger_left);
            return r2.res;
        }
    }
    for (int i = 0; i < pc; ++i)
    {
        const char *g = tinybuf_plugin_get_guid(i);
        if (!g)
            g = "";
        int gl = (int)strlen(g);
        {
            tinybuf_error r3 = try_write_type(out, serialize_string);
            if (r3.res <= 0)
            {
                tinybuf_result_append_merge(r, &r3, tinybuf_merger_left);
                return r3.res;
            }
        }
        {
            tinybuf_error r4 = try_write_int_data(0, out, (uint64_t)gl);
            if (r4.res <= 0)
            {
                tinybuf_result_append_merge(r, &r4, tinybuf_merger_left);
                return r4.res;
            }
        }
        if (gl)
        {
            buffer_append(out, g, gl);
        }
    }
    int after = buffer_get_length_inline(out);
    return after - before;
}

int try_write_part(buffer *out, const tinybuf_value *value, tinybuf_error *r)
{
    buffer *body = buffer_alloc();
    tinybuf_error rbody_acc = tinybuf_result_ok(0);
    int rbody = try_write_box(body, value, &rbody_acc);
    if (rbody <= 0)
    {
        buffer_free(body);
        return rbody;
    }
    int body_len = rbody;
    int before = buffer_get_length_inline(out);
    {
        tinybuf_error rt = try_write_type(out, serialize_part);
        if (rt.res <= 0)
        {
            buffer_free(body);
            tinybuf_result_append_merge(r, &rt, tinybuf_merger_left);
            return rt.res;
        }
    }
    {
        tinybuf_error ri = try_write_int_data(0, out, (uint64_t)body_len);
        if (ri.res <= 0)
        {
            buffer_free(body);
            tinybuf_result_append_merge(r, &ri, tinybuf_merger_left);
            return ri.res;
        }
    }
    buffer_append(out, buffer_get_data_inline(body), body_len);
    int after = buffer_get_length_inline(out);
    buffer_free(body);
    return after - before;
}

int try_write_partitions(buffer *out, const tinybuf_value *mainbox, const tinybuf_value **subs, int count, tinybuf_error *r)
{
    int total = 1 + count;
    buffer **parts = (buffer **)tinybuf_malloc(sizeof(buffer *) * total);
    int *lens = (int *)tinybuf_malloc(sizeof(int) * total);
    for (int i = 0; i < total; ++i)
    {
        parts[i] = buffer_alloc();
    }
    {
        int rmain = try_write_part(parts[0], mainbox, r);
        if (rmain <= 0)
        {
            for (int i = 0; i < total; ++i)
                buffer_free(parts[i]);
            tinybuf_free(parts);
            tinybuf_free(lens);
            return rmain;
        }
        lens[0] = rmain;
    }
    for (int i = 0; i < count; ++i)
    {
        int rsub = try_write_part(parts[1 + i], subs[i], r);
        if (rsub <= 0)
        {
            for (int k = 0; k < total; ++k)
                buffer_free(parts[k]);
            tinybuf_free(parts);
            tinybuf_free(lens);
            return rsub;
        }
        lens[1 + i] = rsub;
    }
    uint64_t *offs = (uint64_t *)tinybuf_malloc(sizeof(uint64_t) * total);
    uint64_t *vlen = (uint64_t *)tinybuf_malloc(sizeof(uint64_t) * total);
    for (int i = 0; i < total; ++i)
        vlen[i] = 1;
    uint8_t tmp[32];
    while (1)
    {
        uint64_t table_len = 1 + (uint64_t)int_serialize_local((uint64_t)total, tmp);
        for (int i = 0; i < total; ++i)
            table_len += vlen[i];
        offs[0] = table_len;
        for (int i = 1; i < total; ++i)
            offs[i] = offs[i - 1] + (uint64_t)lens[i - 1];
        int stable = 1;
        for (int i = 0; i < total; ++i)
        {
            int l = int_serialize_local(offs[i], tmp);
            if ((uint64_t)l != vlen[i])
            {
                vlen[i] = (uint64_t)l;
                stable = 0;
            }
        }
        if (stable)
            break;
    }
    int before = buffer_get_length_inline(out);
    {
        tinybuf_error rt = try_write_type(out, serialize_part_table);
        if (rt.res <= 0)
        {
            for (int k = 0; k < total; ++k)
                buffer_free(parts[k]);
            tinybuf_free(parts);
            tinybuf_free(lens);
            tinybuf_free(offs);
            tinybuf_free(vlen);
            tinybuf_result_append_merge(r, &rt, tinybuf_merger_left);
            return rt.res;
        }
    }
    {
        tinybuf_error ri = try_write_int_data(0, out, (uint64_t)total);
        if (ri.res <= 0)
        {
            for (int k = 0; k < total; ++k)
                buffer_free(parts[k]);
            tinybuf_free(parts);
            tinybuf_free(lens);
            tinybuf_free(offs);
            tinybuf_free(vlen);
            tinybuf_result_append_merge(r, &ri, tinybuf_merger_left);
            return ri.res;
        }
    }
    for (int i = 0; i < total; ++i)
    {
        tinybuf_error rj = try_write_int_data(0, out, (uint64_t)offs[i]);
        if (rj.res <= 0)
        {
            for (int k = 0; k < total; ++k)
                buffer_free(parts[k]);
            tinybuf_free(parts);
            tinybuf_free(lens);
            tinybuf_free(offs);
            tinybuf_free(vlen);
            tinybuf_result_append_merge(r, &rj, tinybuf_merger_left);
            return rj.res;
        }
    }
    for (int i = 0; i < total; ++i)
    {
        buffer_append(out, buffer_get_data_inline(parts[i]), buffer_get_length_inline(parts[i]));
    }
    int after = buffer_get_length_inline(out);
    for (int i = 0; i < total; ++i)
        buffer_free(parts[i]);
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

int try_write_pointer_value(buffer *out, enum offset_type t, int64_t offset, tinybuf_error *r)
{
    int neg = offset < 0;
    serialize_type pt = make_pointer_type(t, neg);
    tinybuf_error r1 = try_write_type(out, pt);
    if (r1.res <= 0)
    {
        tinybuf_result_append_merge(r, &r1, tinybuf_merger_left);
        return r1.res;
    }
    uint64_t mag = neg ? (uint64_t)(-offset) : (uint64_t)offset;
    tinybuf_error r2 = try_write_int_data(0, out, mag);
    if (r2.res <= 0)
    {
        tinybuf_result_append_merge(r, &r2, tinybuf_merger_left);
        return r2.res;
    }
    return r1.res + r2.res;
}
static inline tinybuf_error _err_with(const char *msg, int rc)
{
    return tinybuf_result_err(rc, msg, NULL);
}

int tinybuf_try_write_box(buffer *out, const tinybuf_value *value, tinybuf_error *r)
{
    int n = try_write_box(out, value, r);
    if (n > 0)
        return n;
    tinybuf_result_add_msg_const(r, "tinybuf_try_write_box_r");
    return n;
}

int tinybuf_try_write_version_box(buffer *out, uint64_t version, const tinybuf_value *box, tinybuf_error *r)
{
    int n = try_write_version_box(out, version, box, r);
    if (n > 0)
        return n;
    tinybuf_result_add_msg_const(r, "tinybuf_try_write_version_box_r");
    return n;
}

int tinybuf_try_write_version_list(buffer *out, const uint64_t *versions, const tinybuf_value **boxes, int count, tinybuf_error *r)
{
    int n = try_write_version_list(out, versions, boxes, count, r);
    if (n > 0)
        return n;
    tinybuf_result_add_msg_const(r, "tinybuf_try_write_version_list_r");
    return n;
}

int tinybuf_try_write_plugin_map_table(buffer *out, tinybuf_error *r)
{
    int n = try_write_plugin_map_table(out, r);
    if (n > 0)
        return n;
    tinybuf_result_add_msg_const(r, "tinybuf_try_write_plugin_map_table_r");
    return n;
}

int tinybuf_try_write_part(buffer *out, const tinybuf_value *value, tinybuf_error *r)
{
    int n = try_write_part(out, value, r);
    if (n > 0)
        return n;
    tinybuf_result_add_msg_const(r, "tinybuf_try_write_part_r");
    return n;
}

int tinybuf_try_write_partitions(buffer *out, const tinybuf_value *mainbox, const tinybuf_value **subs, int count, tinybuf_error *r)
{
    int n = try_write_partitions(out, mainbox, subs, count, r);
    if (n > 0)
        return n;
    tinybuf_result_add_msg_const(r, "tinybuf_try_write_partitions_r");
    return n;
}

int tinybuf_try_write_pointer(buffer *out, int t, int64_t offset, tinybuf_error *r)
{
    enum offset_type et = start;
    if (t == 1)
        et = end;
    else if (t == 2)
        et = current;
    int n = try_write_pointer_value(out, et, offset, r);
    if (n > 0)
        return n;
    tinybuf_result_add_msg_const(r, "tinybuf_try_write_pointer_r");
    return n;
}

int tinybuf_try_write_sub_ref(buffer *out, int t, int64_t offset, tinybuf_error *r)
{
    tinybuf_error r0 = try_write_type(out, serialize_sub_ref);
    if (r0.res <= 0)
    {
        tinybuf_result_add_msg_const(&r0, "tinybuf_try_write_sub_ref_r");
        tinybuf_result_append_merge(r, &r0, tinybuf_merger_left);
        return r0.res;
    }
    enum offset_type et = (t == 1 ? end : (t == 2 ? current : start));
    int n = try_write_pointer_value(out, et, offset, r);
    if (n <= 0)
    {
        tinybuf_result_add_msg_const(r, "tinybuf_try_write_sub_ref_r");
        return n;
    }
    return r0.res + n;
}

int tinybuf_try_write_array_header(buffer *out, int count, tinybuf_error *r)
{
    tinybuf_error r1 = try_write_type(out, serialize_array);
    if (r1.res <= 0)
    {
        tinybuf_result_add_msg_const(&r1, "tinybuf_try_write_array_header");
        tinybuf_result_append_merge(r, &r1, tinybuf_merger_left);
        return r1.res;
    }
    tinybuf_error r2 = try_write_int_data(0, out, (uint64_t)count);
    if (r2.res <= 0)
    {
        tinybuf_result_add_msg_const(&r2, "tinybuf_try_write_array_header");
        tinybuf_result_append_merge(r, &r2, tinybuf_merger_left);
        return r2.res;
    }
    int n = r1.res + r2.res;
    tinybuf_error ok = tinybuf_result_ok(n);
    tinybuf_result_append_merge(r, &ok, tinybuf_merger_sum);
    return n;
}
int tinybuf_try_write_map_header(buffer *out, int count, tinybuf_error *r)
{
    tinybuf_error r1 = try_write_type(out, serialize_map);
    if (r1.res <= 0)
    {
        tinybuf_result_add_msg_const(&r1, "tinybuf_try_write_map_header");
        tinybuf_result_append_merge(r, &r1, tinybuf_merger_left);
        return r1.res;
    }
    tinybuf_error r2 = try_write_int_data(0, out, (uint64_t)count);
    if (r2.res <= 0)
    {
        tinybuf_result_add_msg_const(&r2, "tinybuf_try_write_map_header");
        tinybuf_result_append_merge(r, &r2, tinybuf_merger_left);
        return r2.res;
    }
    int n = r1.res + r2.res;
    tinybuf_error ok = tinybuf_result_ok(n);
    tinybuf_result_append_merge(r, &ok, tinybuf_merger_sum);
    return n;
}
int tinybuf_try_write_string_raw(buffer *out, const char *str, int len, tinybuf_error *r)
{
    int ret = dump_string(len, str, out);
    if (ret > 0)
    {
        tinybuf_error ok = tinybuf_result_ok(ret);
        tinybuf_result_append_merge(r, &ok, tinybuf_merger_sum);
        return ret;
    }
    tinybuf_error er = tinybuf_result_err(ret, "dump_string failed", NULL);
    tinybuf_result_add_msg_const(&er, "tinybuf_try_write_string_raw_r");
    tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
    return ret;
}

int tinybuf_try_write_custom_id_box(buffer *out, const char *name, const tinybuf_value *in, tinybuf_error *r)
{
    buffer *body = buffer_alloc();
    buffer *pool = buffer_alloc();
    strpool_reset_write(body);
    int idx = strpool_add(name, (int)strlen(name));
    uint8_t ty = serialize_type_idx;
    buffer_append(body, (const char *)&ty, 1);
    dump_int((uint64_t)idx, body);
    buffer *payload = buffer_alloc();
    int wlen = tinybuf_custom_try_write(name, in, payload, r);
    dump_int((uint64_t)(wlen > 0 ? wlen : 0), body);
    if (wlen > 0)
    {
        buffer_append(body, buffer_get_data_inline(payload), buffer_get_length_inline(payload));
    }
    tinybuf_error rpool = strpool_write_tail(pool);
    if (rpool.res <= 0)
    {
        buffer_free(payload);
        buffer_free(body);
        buffer_free(pool);
        tinybuf_result_append_merge(r, &rpool, tinybuf_merger_left);
        return rpool.res;
    }
    uint64_t body_len = (uint64_t)buffer_get_length_inline(body);
    uint64_t offset_guess_len = 1;
    uint8_t tmpv[16];
    while (1)
    {
        uint64_t off = 1 + offset_guess_len + body_len;
        int l = int_serialize_local(off, tmpv);
        if ((uint64_t)l == offset_guess_len)
            break;
        offset_guess_len = (uint64_t)l;
    }
    uint64_t final_off = 1 + offset_guess_len + body_len;
    tinybuf_error acc = tinybuf_result_ok(0);
    tinybuf_error rtype = try_write_type(out, serialize_str_pool_table);
    if (rtype.res <= 0)
    {
        buffer_free(payload);
        buffer_free(body);
        buffer_free(pool);
        tinybuf_result_append_merge(r, &rtype, tinybuf_merger_left);
        return rtype.res;
    }
    tinybuf_result_append_merge(&acc, &rtype, tinybuf_merger_sum);
    tinybuf_error rlen = try_write_int_data(0, out, final_off);
    if (rlen.res <= 0)
    {
        buffer_free(payload);
        buffer_free(body);
        buffer_free(pool);
        tinybuf_result_append_merge(r, &rlen, tinybuf_merger_left);
        return rlen.res;
    }
    tinybuf_result_append_merge(&acc, &rlen, tinybuf_merger_sum);
    buffer_append(out, buffer_get_data_inline(body), (int)body_len);
    int pool_len = buffer_get_length_inline(pool);
    if (pool_len)
    {
        buffer_append(out, buffer_get_data_inline(pool), pool_len);
    }
    buffer_free(payload);
    buffer_free(body);
    buffer_free(pool);
    if (wlen < 0)
    {
        tinybuf_result_add_msg_const(r, "write custom payload failed");
        char *m = (char *)tinybuf_malloc(64);
        snprintf(m, 64, "name=%s", name ? name : "(null)");
        tinybuf_result_add_msg(r, m, (tinybuf_deleter_fn)tinybuf_free);
        tinybuf_result_add_msg_const(r, "tinybuf_try_write_custom_id_box_r");
        return wlen;
    }
    int total = 1 + (int)offset_guess_len + (int)body_len + pool_len;
    tinybuf_error rt = tinybuf_result_ok(total);
    tinybuf_result_append_merge(r, &rt, tinybuf_merger_sum);
    return total;
}
