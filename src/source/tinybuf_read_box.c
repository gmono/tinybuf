#include "tinybuf_private.h"
#include "tinybuf_buffer.h"
#include "tinybuf_plugin.h"
#ifdef _WIN32
#include <windows.h>
static volatile LONG s_pool_lock_var = 0;
#else
#include <stdatomic.h>
#include <time.h>
#include <sched.h>
static atomic_flag s_pool_lock_var = ATOMIC_FLAG_INIT;
#endif

static inline void pool_lock(void);
static inline void pool_unlock(void);
static inline BOOL is_simple_pointer_type(serialize_type type);
static inline bool is_pointer_neg(serialize_type type);
static inline enum offset_type get_offset_type(serialize_type type);

/* result-only accumulation helper */
#define OK_AND_ADDTO(r, s) (((r).res > 0) ? ((*(s)) += (r).res, 1) : 0)

const char *buf_end_ptr(buf_ref *buf)
{
    return buf->base + buf->all_size;
}
int64_t s_strpool_offset_read = -1;
const char *s_strpool_base_read = NULL;
int64_t buf_current_offset(buf_ref *buf)
{
    return buf->ptr - buf->base;
}
int buf_offset(buf_ref *buf, int64_t offset)
{
    const char *temp = buf->ptr;
    buf->ptr += offset;
    if (!buf_ptr_ok(buf))
    {
        buf->ptr = temp;
        return -1;
    }
    buf->size -= offset;
    maybe_validate(buf);
    return 0;
}

tinybuf_error try_read_type(buf_ref *buf, serialize_type *type)
{
    if (buf->size < 1)
    {
        return tinybuf_result_err(0, "buffer too small", NULL);
    }
    *type = (serialize_type)buf->ptr[0];
    if (buf_offset(buf, 1) != 0)
        return tinybuf_result_err(-1, "buf_offset failed", NULL);
    return tinybuf_result_ok(1);
}

void pointer_to_start(const buf_ref *buf, pointer_value *ptr)
{
    switch (ptr->type)
    {
    case start:
        break;
    case end:
        ptr->offset = buf->all_size - ptr->offset;
        ptr->type = start;
        break;
    case current:
        ptr->offset = buf_current_offset(buf) + ptr->offset;
        ptr->type = start;
        break;
    default:
        break;
    }
    maybe_validate((buf_ref *)buf);
}

typedef struct
{
    int len;
    const char *reason;
} read_result;
#define RESULT_OK(x) (x.len > 0)

typedef struct
{
    int64_t offset;
    tinybuf_value *value;
    int complete;
} offset_pool_entry;
static const char *s_pool_base = NULL;
static offset_pool_entry *s_pool = NULL;
static int s_pool_count = 0;
static int s_pool_capacity = 0;

static inline void pool_reset(const buf_ref *buf)
{
    if (s_pool_base != buf->base)
    {
        if (s_pool)
        {
            tinybuf_free(s_pool);
            s_pool = NULL;
            s_pool_capacity = 0;
        }
        s_pool_base = buf->base;
    }
    s_pool_count = 0;
}

const char *tinybuf_last_error_message(void);

static inline offset_pool_entry *pool_find(int64_t offset)
{
    pool_lock();
    for (int i = 0; i < s_pool_count; ++i)
    {
        if (s_pool[i].offset == offset)
        {
            offset_pool_entry *ret = &s_pool[i];
            pool_unlock();
            return ret;
        }
    }
    pool_unlock();
    return NULL;
}

static offset_pool_entry *pool_register(int64_t offset, tinybuf_value *value)
{
    pool_lock();
    offset_pool_entry *e = NULL;
    for (int i = 0; i < s_pool_count; ++i)
    {
        if (s_pool[i].offset == offset)
        {
            e = &s_pool[i];
            break;
        }
    }
    if (e)
    {
        if (value)
        {
            e->value = value;
        }
        pool_unlock();
        return e;
    }
    if (s_pool_count == s_pool_capacity)
    {
        int newcap = s_pool_capacity ? (s_pool_capacity * 2) : 16;
        s_pool = (offset_pool_entry *)tinybuf_realloc(s_pool, sizeof(offset_pool_entry) * newcap);
        s_pool_capacity = newcap;
    }
    s_pool[s_pool_count].offset = offset;
    s_pool[s_pool_count].value = value;
    s_pool[s_pool_count].complete = 0;
    offset_pool_entry *ret = &s_pool[s_pool_count++];
    pool_unlock();
    return ret;
}

static inline void pool_mark_complete(int64_t offset)
{
    pool_lock();
    offset_pool_entry *e = NULL;
    for (int i = 0; i < s_pool_count; ++i)
    {
        if (s_pool[i].offset == offset)
        {
            e = &s_pool[i];
            break;
        }
    }
    if (e)
    {
        e->complete = 1;
    }
    pool_unlock();
}

static inline void set_out_ref(tinybuf_value *out, tinybuf_value *target)
{
    out->_type = tinybuf_value_ref;
    out->_data._ref = target;
}
static inline void set_out_deref(tinybuf_value *out, const tinybuf_value *target)
{
    tinybuf_value *clone = tinybuf_value_clone(target);
    tinybuf_value_clear(out);
    memcpy(out, clone, sizeof(tinybuf_value));
    tinybuf_free(clone);
}
static inline void set_out_by_mode(tinybuf_value *out, tinybuf_value *target, int deref)
{
    if (deref)
        set_out_deref(out, target);
    else
        set_out_ref(out, target);
}

int _read_box_by_offset(buf_ref *buf, int64_t offset, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_error *r)
{
    INIT_STATE
    const char *cur = buf->ptr;
    int64_t cursize = buf->size;
    buf->ptr = buf->base + offset;
    buf->size = buf->all_size - offset;
    pool_register(offset, out);
    int rr = try_read_box(buf, out, contain_handler, r);
    buf->ptr = cur;
    buf->size = cursize;
    if (rr > 0)
    {
        pool_mark_complete(offset);
        return rr;
    }
    return rr;
}

int read_box_by_pointer(buf_ref *buf, pointer_value pointer, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_error *r)
{
    static int s_read_pointer_depth = 0;
    s_read_pointer_depth++;
    int rr = -1;
    if (s_read_pointer_depth <= 64)
    {
        pointer_to_start(buf, &pointer);
        assert(pointer.type == start);
        rr = _read_box_by_offset(buf, pointer.offset, out, contain_handler, r);
    }
    s_read_pointer_depth--;
    return rr;
}

static inline tinybuf_error try_read_int_data(BOOL isneg, buf_ref *buf, QWORD *out)
{
    INIT_STATE
    int temp = try_read_int_tovar(isneg, buf->ptr, (int)buf->size, out);
    if (temp > 0)
    {
        len += temp;
        if (buf_offset(buf, temp) == 0)
            SET_SUCCESS();
        else
            SET_FAILED("buf_offset failed");
    }
    else
        SET_FAILED("read data error");
    CHECK_FAILED
    return failed ? tinybuf_result_err(-1, reason, NULL) : tinybuf_result_ok(len);
}

inline tinybuf_error try_read_intbox(buf_ref *buf, QWORD *saveptr)
{
    serialize_type type;
    INIT_STATE
    tinybuf_error rtype = try_read_type(buf, &type);
    if (OK_AND_ADDTO(rtype, &len))
    {
        if (type == serialize_positive_int || type == serialize_negtive_int)
        {
            BOOL isneg = type == serialize_negtive_int;
            {
                tinybuf_error rint = try_read_int_data(isneg, buf, saveptr);
                if (OK_AND_ADDTO(rint, &len))
                {
                    SET_SUCCESS();
                }
                else
                    SET_FAILED("read int error");
            }
        }
        else
            SET_FAILED("type error");
    }
    else
        SET_FAILED("read type error");
    CHECK_FAILED
    return failed ? tinybuf_result_err(-1, reason, NULL) : tinybuf_result_ok(len);
}

inline tinybuf_error try_read_pointer_value(buf_ref *buf, QWORD *saveptr)
{
    INIT_STATE
    serialize_type type;
    tinybuf_error rtype = try_read_type(buf, &type);
    if (OK_AND_ADDTO(rtype, &len))
    {
        if (is_simple_pointer_type(type))
        {
            {
                tinybuf_error rint = try_read_int_data(FALSE, buf, saveptr);
                if (OK_AND_ADDTO(rint, &len))
                {
                    SET_SUCCESS();
                }
                else
                    SET_FAILED("read pointer error");
            }
        }
        else
            SET_FAILED("type error : not simple pointer");
    }
    else
        SET_FAILED("read type error");
    CHECK_FAILED
    return failed ? tinybuf_result_err(-1, reason, NULL) : tinybuf_result_ok(len);
}

inline tinybuf_error try_read_pointer(buf_ref *buf, pointer_value *pointer)
{
    INIT_STATE
    serialize_type type;
    tinybuf_error rtype = try_read_type(buf, &type);
    if (OK_AND_ADDTO(rtype, &len))
    {
        if (is_simple_pointer_type(type))
        {
            QWORD offset;
            bool isneg = is_pointer_neg(type);
            {
                tinybuf_error rint = try_read_int_data(isneg, buf, &offset);
                if (OK_AND_ADDTO(rint, &len))
                {
                    pointer->offset = (int64_t)offset;
                    pointer->type = get_offset_type(type);
                    SET_SUCCESS();
                }
                else
                    SET_FAILED("read pointer error");
            }
        }
        else
            SET_FAILED("type error : not simple pointer");
    }
    else
        SET_FAILED("read type error");
    CHECK_FAILED
    return failed ? tinybuf_result_err(-1, reason, NULL) : tinybuf_result_ok(len);
}

int tinybuf_try_read_box_with_mode(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_read_pointer_mode mode, tinybuf_error *r)
{
    (void)mode;
    int n = try_read_box(buf, out, contain_handler, r);
    if (n > 0)
        return n;
    tinybuf_result_add_msg_const(r, "tinybuf_try_read_box_with_mode_r");
    return n;
}

int tinybuf_try_read_box(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_error *r)
{
    s_strpool_base_read = buf->base;
    int n = try_read_box(buf, out, contain_handler, r);
    if (n > 0)
        return n;
    tinybuf_result_add_msg_const(r, "tinybuf_try_read_box_r");
    return n;
}

int try_read_box(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_error *r)
{
    INIT_STATE
    tinybuf_result_add_msg_const(r, "try_read_box");
    if (!s_strpool_base_read)
        s_strpool_base_read = buf->base;
    if (buf->size >= 1 && (uint8_t)buf->ptr[0] == serialize_str_pool_table)
    {
        buf_ref hb = *buf;
        serialize_type t;
        tinybuf_error c = try_read_type(&hb, &t);
        if (c.res > 0 && t == serialize_str_pool_table)
        {
            QWORD off;
            tinybuf_error c2 = try_read_int_data(FALSE, &hb, &off);
            if (c2.res > 0)
            {
                s_strpool_offset_read = (int64_t)off;
                buf_offset(buf, c.res + c2.res);
            }
        }
    }
    int64_t box_offset = buf_current_offset(buf);
    pool_register(box_offset, out);
    len = tinybuf_value_deserialize(buf->ptr, (int)buf->size, out, r);
    if (len == 0)
    {
        return 0;
    }
    if (len > 0)
    {
        buf_offset(buf, len);
        pool_mark_complete(box_offset);
        return len;
    }
    len = 0;
    tinybuf_error acc = tinybuf_result_ok(0);
    serialize_type type = serialize_null;
    {
        tinybuf_error rt_main = try_read_type(buf, &type);
        if (OK_AND_ADDTO(rt_main, &len))
        {
            switch (type)
            {
            case serialize_version:
            {
                QWORD version;
                if (OK_AND_ADDTO(try_read_int_data(FALSE, buf, &version), &len))
                {
                    if (contain_handler(version))
                    {
                        {
                            int rr2 = try_read_box(buf, out, contain_handler, r);
                            if (rr2 > 0)
                            {
                                len += rr2;
                                pool_mark_complete(box_offset);
                                SET_SUCCESS();
                                break;
                            }
                            SET_FAILED("read box failed");
                            break;
                        }
                    }
                    SET_FAILED("version error");
                    break;
                }
                SET_FAILED("read version failed");
                break;
            }
            case serialize_version_list:
            {
                QWORD list_len;
                if (OK_AND_ADDTO(try_read_int_data(FALSE, buf, &list_len), &len))
                {
                    int found = 0;
                    for (QWORD i = 0; i < list_len; i++)
                    {
                        QWORD version = 0;
                        if (OK_AND_ADDTO(try_read_int_data(FALSE, buf, &version), &len))
                        {
                            if (contain_handler(version))
                            {
                                int inner = try_read_box(buf, out, contain_handler, r);
                                if (inner > 0)
                                {
                                    len += inner;
                                    pool_mark_complete(box_offset);
                                    SET_SUCCESS();
                                    found = 1;
                                    break;
                                }
                                SET_FAILED("read box failed");
                                break;
                            }
                            else
                            {
                                tinybuf_value *skip = tinybuf_value_alloc();
                                int consumed = try_read_box(buf, skip, contain_handler, r);
                                if (consumed <= 0)
                                {
                                    tinybuf_value_free(skip);
                                    SET_FAILED("read non-match box failed");
                                    break;
                                }
                                len += consumed;
                                tinybuf_value_free(skip);
                                continue;
                            }
                        }
                        else
                        {
                            SET_FAILED("read version failed");
                            break;
                        }
                    }
                    if (!found)
                    {
                        SET_FAILED("read version list failed");
                    }
                    break;
                }
            }
            case serialize_part:
            {
                QWORD partlen;
                if (OK_AND_ADDTO(try_read_int_data(FALSE, buf, &partlen), &len))
                {
                    {
                        int rr3 = try_read_box(buf, out, contain_handler, r);
                        if (rr3 > 0)
                        {
                            len += rr3;
                            pool_mark_complete(box_offset);
                            SET_SUCCESS();
                            break;
                        }
                        SET_FAILED("read part failed");
                        break;
                    }
                }
                SET_FAILED("read part header failed");
                break;
            }
            case serialize_part_table:
            {
                QWORD cnt;
                if (OK_AND_ADDTO(try_read_int_data(FALSE, buf, &cnt), &len))
                {
                    if (cnt == 0)
                    {
                        SET_FAILED("empty part table");
                        break;
                    }
                    QWORD *offs = (QWORD *)tinybuf_malloc((int)(cnt * sizeof(QWORD)));
                    for (QWORD i = 0; i < cnt; ++i)
                    {
                        QWORD off = 0;
                        if (OK_AND_ADDTO(try_read_int_data(FALSE, buf, &off), &len))
                        {
                            offs[i] = off;
                        }
                        else
                        {
                            tinybuf_free(offs);
                            SET_FAILED("read part table offset failed");
                            goto part_table_end;
                        }
                    }
                    {
                        int rbo = _read_box_by_offset(buf, (int64_t)offs[0], out, contain_handler, r);
                        if (rbo > 0)
                        {
                            pool_mark_complete(box_offset);
                            SET_SUCCESS();
                        }
                    }
                    tinybuf_free(offs);
                part_table_end:
                    break;
                }
                SET_FAILED("read part table header failed");
                break;
            }
            case serialize_indexed_tensor:
            {
                tinybuf_error rr = tinybuf_result_ok(0);
                tinybuf_value *ten = tinybuf_value_alloc();
                int r1 = tinybuf_value_deserialize(buf->ptr, (int)buf->size, ten, &rr);
                if (r1 <= 0)
                {
                    buf_ref br_fallback = *buf;
                    tinybuf_error rr1 = tinybuf_result_ok(0);
                    int rlen1 = tinybuf_try_read_box(&br_fallback, ten, contain_any, &rr1);
                    if (rlen1 <= 0)
                    {
                        tinybuf_value_free(ten);
                        SET_FAILED("read indexed_tensor tensor failed");
                        break;
                    }
                    r1 = rlen1;
                }
                buf_offset(buf, r1);
                {
                    tinybuf_error t = tinybuf_result_ok(r1);
                    tinybuf_result_append_merge(&acc, &t, tinybuf_merger_sum);
                    len = acc.res;
                }
                QWORD dims = 0;
                if (!OK_AND_ADDTO(try_read_int_data(FALSE, buf, &dims), &len))
                {
                    tinybuf_value_free(ten);
                    SET_FAILED("read indexed_tensor dims failed");
                    break;
                }
                tinybuf_indexed_tensor_t *it = (tinybuf_indexed_tensor_t *)tinybuf_malloc((int)sizeof(tinybuf_indexed_tensor_t));
                it->tensor = ten;
                it->dims = (int)dims;
                it->indices = NULL;
                if (dims > 0)
                {
                    it->indices = (tinybuf_value **)tinybuf_malloc(sizeof(tinybuf_value *) * (size_t)dims);
                    for (QWORD i = 0; i < dims; ++i)
                        it->indices[i] = NULL;
                }
                for (QWORD i = 0; i < dims; ++i)
                {
                    QWORD has = 0;
                    if (!OK_AND_ADDTO(try_read_int_data(FALSE, buf, &has), &len))
                    {
                        if (it->indices)
                        {
                            for (QWORD k = 0; k < i; ++k)
                            {
                                if (it->indices[k])
                                    tinybuf_value_free(it->indices[k]);
                            }
                            tinybuf_free(it->indices);
                        }
                        tinybuf_value_free(ten);
                        tinybuf_free(it);
                        SET_FAILED("read indexed_tensor has failed");
                        break;
                    }
                    if (has)
                    {
                        tinybuf_value *idx = tinybuf_value_alloc();
                        int r2 = tinybuf_value_deserialize(buf->ptr, (int)buf->size, idx, &rr);
                        if (r2 <= 0)
                        {
                            buf_ref br2 = *buf;
                            tinybuf_error rr3 = tinybuf_result_ok(0);
                            int rl3 = tinybuf_try_read_box(&br2, idx, contain_any, &rr3);
                            if (rl3 <= 0)
                            {
                                if (it->indices)
                                {
                                    for (QWORD k = 0; k < i; ++k)
                                    {
                                        if (it->indices[k])
                                            tinybuf_value_free(it->indices[k]);
                                    }
                                    tinybuf_free(it->indices);
                                }
                                tinybuf_value_free(ten);
                                tinybuf_free(it);
                                SET_FAILED("read indexed_tensor index failed");
                                break;
                            }
                            buf_offset(buf, rl3);
                            tinybuf_result_append_merge(&acc, &rr3, tinybuf_merger_sum);
                            len = acc.res;
                            r2 = 0; /* already advanced */
                        }
                        else
                        {
                            buf_offset(buf, r2);
                            {
                                tinybuf_error t2 = tinybuf_result_ok(r2);
                                tinybuf_result_append_merge(&acc, &t2, tinybuf_merger_sum);
                                len = acc.res;
                            }
                        }
                        it->indices[i] = idx;
                    }
                }
                if (!failed)
                {
                    out->_type = tinybuf_indexed_tensor;
                    out->_data._custom = it;
                    out->_custom_free = NULL;
                    pool_mark_complete(box_offset);
                    SET_SUCCESS();
                }
                break;
            }
            case serialize_type_idx:
            {
                QWORD idx = 0;
                if (!OK_AND_ADDTO(try_read_int_data(FALSE, buf, &idx), &len))
                {
                    SET_FAILED("read type_idx index failed");
                    break;
                }
                QWORD blen = 0;
                if (!OK_AND_ADDTO(try_read_int_data(FALSE, buf, &blen), &len))
                {
                    SET_FAILED("read type_idx length failed");
                    break;
                }
                if (buf->size < (int64_t)blen)
                {
                    SET_FAILED("payload too small");
                    break;
                }
                if (s_strpool_offset_read < 0 || s_strpool_base_read == NULL)
                {
                    s_last_error_msg = "strpool not initialized";
                    SET_FAILED("strpool not initialized");
                    break;
                }
                const char *pool_start = s_strpool_base_read + s_strpool_offset_read;
                const char *q = pool_start;
                int64_t rem = ((const char *)buf->ptr + buf->size) - pool_start;
                char *name_out = NULL;
                int name_len = 0;
                if (rem > 0)
                {
                    if ((uint8_t)q[0] == serialize_str_pool)
                    {
                        ++q;
                        --rem;
                        QWORD cnt = 0;
                        int l2 = try_read_int_tovar(FALSE, q, (int)rem, &cnt);
                        if (l2 > 0)
                        {
                            q += l2;
                            rem -= l2;
                            for (QWORD i = 0; i < cnt; ++i)
                            {
                                if (rem < 1)
                                    break;
                                if ((uint8_t)q[0] != serialize_string)
                                    break;
                                ++q;
                                --rem;
                                QWORD sl = 0;
                                int l3 = try_read_int_tovar(FALSE, q, (int)rem, &sl);
                                if (l3 <= 0)
                                    break;
                                q += l3;
                                rem -= l3;
                                if (rem < (int64_t)sl)
                                    break;
                                if (i == idx)
                                {
                                    name_out = (char *)tinybuf_malloc((int)sl + 1);
                                    memcpy(name_out, q, (size_t)sl);
                                    name_out[sl] = '\0';
                                    name_len = (int)sl;
                                    break;
                                }
                                q += sl;
                                rem -= sl;
                            }
                        }
                    }
                    else if ((uint8_t)q[0] == 27)
                    {
                        ++q;
                        --rem;
                        QWORD ncount = 0;
                        int l2 = try_read_int_tovar(FALSE, q, (int)rem, &ncount);
                        if (l2 > 0)
                        {
                            const char *nodes_base = q + l2;
                            int64_t nodes_size = rem - l2;
                            const char *p = nodes_base;
                            int64_t rr = nodes_size;
                            int found = -1;
                            for (QWORD i = 0; i < ncount; ++i)
                            {
                                QWORD parent = 0;
                                int lp = try_read_int_tovar(FALSE, p, (int)rr, &parent);
                                if (lp <= 0)
                                    break;
                                p += lp;
                                rr -= lp;
                                if (rr < 2)
                                    break;
                                unsigned char ch = (unsigned char)p[0];
                                unsigned char flag = (unsigned char)p[1];
                                p += 2;
                                rr -= 2;
                                if (flag)
                                {
                                    QWORD leaf = 0;
                                    int ll = try_read_int_tovar(FALSE, p, (int)rr, &leaf);
                                    if (ll <= 0)
                                        break;
                                    p += ll;
                                    rr -= ll;
                                    if (leaf == (QWORD)idx)
                                    {
                                        found = (int)i;
                                        break;
                                    }
                                }
                            }
                            if (found >= 0)
                            {
                                buffer *tmp = buffer_alloc();
                                int current = found;
                                while (1)
                                {
                                    const char *p2 = nodes_base;
                                    int64_t rr2 = nodes_size;
                                    int node_index = 0;
                                    int parent_index = -1;
                                    unsigned char ch = 0;
                                    unsigned char flag = 0;
                                    QWORD leaf = 0;
                                    while (node_index <= current)
                                    {
                                        QWORD parent = 0;
                                        int lp = try_read_int_tovar(FALSE, p2, (int)rr2, &parent);
                                        p2 += lp;
                                        rr2 -= lp;
                                        if (rr2 < 2)
                                            break;
                                        ch = (unsigned char)p2[0];
                                        flag = (unsigned char)p2[1];
                                        p2 += 2;
                                        rr2 -= 2;
                                        if (flag)
                                        {
                                            int ll = try_read_int_tovar(FALSE, p2, (int)rr2, &leaf);
                                            p2 += ll;
                                            rr2 -= ll;
                                        }
                                        parent_index = (int)parent - 1;
                                        ++node_index;
                                    }
                                    if (ch)
                                    {
                                        buffer_append(tmp, (const char *)&ch, 1);
                                    }
                                    if (parent_index < 0)
                                        break;
                                    current = parent_index;
                                }
                                int tlen = buffer_get_length_inline(tmp);
                                const char *td = buffer_get_data_inline(tmp);
                                char *name_out2 = (char *)tinybuf_malloc(tlen + 1);
                                for (int i = 0; i < tlen; ++i)
                                    name_out2[i] = td[tlen - 1 - i];
                                name_out2[tlen] = '\0';
                                name_out = name_out2;
                                name_len = tlen;
                                buffer_free(tmp);
                            }
                        }
                    }
                }
                if (name_out)
                {
                    tinybuf_error cr = tinybuf_result_ok(0);
                    int crlen = tinybuf_custom_try_read(name_out, (const uint8_t *)buf->ptr, (int)blen, out, contain_any, &cr);
                    if (crlen > 0)
                    {
                        buf_offset(buf, (int)blen);
                        len += (int)blen;
                        pool_mark_complete(box_offset);
                        SET_SUCCESS();
                    }
                    else
                    {
                        buf_ref br3 = (buf_ref){(char *)buf->ptr, (int64_t)blen, (char *)buf->ptr, (int64_t)blen};
                        int rl2 = tinybuf_try_read_box(&br3, out, contain_any, r);
                        if (rl2 > 0)
                        {
                            buf_offset(buf, rl2);
                            len += rl2;
                            pool_mark_complete(box_offset);
                            SET_SUCCESS();
                        }
                        else
                        {
                            SET_FAILED("custom read failed");
                        }
                    }
                    tinybuf_free(name_out);
                }
                else
                {
                    SET_FAILED("type_idx name not found");
                }
                if (!failed)
                {
                    return len;
                }
                break;
            }
            default:
            {
                char *m = (char *)tinybuf_malloc(64);
                snprintf(m, 64, "read type UNKNOWN %u", (unsigned)type);
                reason = m;
                s_last_error_msg = m;
                failed = TRUE;
                break;
            }
            }
        }
        else
        {
            SET_FAILED("read type failed");
        }
    }
    CHECK_FAILED
    if (!failed)
    {
        pool_mark_complete(box_offset);
        return len;
    }
    return -1;
}
static inline void pool_lock(void)
{
#ifdef _WIN32
    int spins = 0;
    while (InterlockedCompareExchange(&s_pool_lock_var, 1, 0) != 0)
    {
        if (spins < 64)
        {
            ++spins;
        }
        else if (spins < 1024)
        {
            Sleep(0);
            ++spins;
        }
        else
        {
            Sleep(1);
        }
    }
#else
    int spins = 0;
    while (atomic_flag_test_and_set(&s_pool_lock_var))
    {
        if (spins < 64)
        {
            ++spins;
        }
        else if (spins < 1024)
        {
            sched_yield();
            ++spins;
        }
        else
        {
            struct timespec ts = {0, 1000000};
            nanosleep(&ts, NULL);
        }
    }
#endif
}
static inline void pool_unlock(void)
{
#ifdef _WIN32
    InterlockedExchange(&s_pool_lock_var, 0);
#else
    atomic_flag_clear(&s_pool_lock_var);
#endif
}
static inline BOOL is_simple_pointer_type(serialize_type type)
{
    return type == serialize_pointer_from_current_n ||
           type == serialize_pointer_from_start_n ||
           type == serialize_pointer_from_end_n ||
           type == serialize_pointer_from_current_n ||
           type == serialize_pointer_from_start_p ||
           type == serialize_pointer_from_end_p ||
           type == serialize_pointer_from_current_p;
}
static inline bool is_pointer_neg(serialize_type type)
{
    return type == serialize_pointer_from_current_n ||
           type == serialize_pointer_from_start_n ||
           type == serialize_pointer_from_end_n;
}
static inline enum offset_type get_offset_type(serialize_type type)
{
    switch (type)
    {
    case serialize_pointer_from_current_n:
    case serialize_pointer_from_current_p:
        return current;
    case serialize_pointer_from_start_n:
    case serialize_pointer_from_start_p:
        return start;
    case serialize_pointer_from_end_n:
    case serialize_pointer_from_end_p:
        return end;
    default:
        return start;
    }
}
