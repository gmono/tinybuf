#include "tinybuf_private.h"
#include "tinybuf_buffer.h"
#include "tinybuf_plugin.h"
#include <string.h>

const char *s_last_error_msg = NULL;
const char *tinybuf_last_error_message(void){ return s_last_error_msg; }

uint32_t load_be32(const void *p)
{
    uint32_t val;
    memcpy(&val, p, sizeof val);
    return ntohl(val);
}

double read_double(uint8_t *ptr)
{
    uint64_t val = ((uint64_t)load_be32(ptr) << 32) | load_be32(ptr + 4);
    double db = 0;
    memcpy(&db, &val, 8);
    return db;
}

int try_read_int_tovar(BOOL isneg, const char *ptr, int size, QWORD *out_val)
{
    int len = int_deserialize((uint8_t *)ptr, size, out_val);
    if (len < 0)
    {
        return len;
    }
    if (isneg)
    {
        *out_val = -(*out_val);
    }
    return len;
}

int try_read_int(serialize_type type, const char *ptr, int size, tinybuf_value *out)
{
    uint64_t int_val = 0;
    BOOL isneg = type == serialize_negtive_int;
    int len = try_read_int_tovar(isneg, ptr, size, &int_val);
    if (len < 0)
    {
        return len;
    }
    tinybuf_value_init_int(out, int_val);
    return len;
}

static int tinybuf_deserialize_vector_tensor(const char *ptr, int size, tinybuf_value *out);
static int tinybuf_deserialize_dense_tensor(const char *ptr, int size, tinybuf_value *out);
static int tinybuf_deserialize_sparse_tensor(const char *ptr, int size, tinybuf_value *out);

static int tinybuf_deserialize_vector_tensor(const char *ptr, int size, tinybuf_value *out)
{
    uint64_t cnt = 0;
    uint64_t dt = 0;
    int a = 0;
    int b = 0;
    int64_t count = 0;
    tinybuf_tensor_t *tensor = NULL;
    uint8_t *buf_u8 = NULL;
    int64_t *buf_i64 = NULL;
    a = int_deserialize((uint8_t *)ptr, size, &cnt);
    if (a <= 0)
        return a;
    ptr += a;
    size -= a;
    b = int_deserialize((uint8_t *)ptr, size, &dt);
    if (b <= 0)
        return b;
    ptr += b;
    size -= b;
    count = (int64_t)cnt;
    tensor = (tinybuf_tensor_t *)tinybuf_malloc(sizeof(tinybuf_tensor_t));
    tensor->dtype = (int)dt;
    tensor->dims = 1;
    tensor->count = count;
    tensor->shape = NULL;
    if ((int)dt == 8)
    {
        if (size < 8 * count)
            return 0;
        double *buf = (double *)tinybuf_malloc((int)(sizeof(double) * count));
        for (int64_t i = 0; i < count; ++i)
        {
            buf[i] = read_double((uint8_t *)ptr + i * 8);
        }
        tensor->data = buf;
        out->_type = tinybuf_tensor;
        out->_data._custom = tensor;
        out->_custom_free = NULL;
        return 1 + a + b + (int)(8 * count);
    }
    else if ((int)dt == 10)
    {
        if (size < 4 * count)
            return 0;
        float *buf = (float *)tinybuf_malloc((int)(sizeof(float) * count));
        for (int64_t i = 0; i < count; ++i)
        {
            uint32_t raw = load_be32((uint8_t *)ptr + i * 4);
            memcpy(&buf[i], &raw, 4);
        }
        tensor->data = buf;
        out->_type = tinybuf_tensor;
        out->_data._custom = tensor;
        out->_custom_free = NULL;
        return 1 + a + b + (int)(4 * count);
    }
    else if ((int)dt == 11)
    {
        int64_t bytes = (count + 7) / 8;
        if (size < bytes)
            return 0;
        buf_u8 = (uint8_t *)tinybuf_malloc((int)bytes);
        memcpy(buf_u8, ptr, (size_t)bytes);
        tensor->data = buf_u8;
        out->_type = tinybuf_tensor;
        out->_data._custom = tensor;
        out->_custom_free = NULL;
        return 1 + a + b + (int)bytes;
    }
    else
    {
        buf_i64 = (int64_t *)tinybuf_malloc((int)(sizeof(int64_t) * count));
        int r = 0;
        for (int64_t i = 0; i < count; ++i)
        {
            if (size < 1)
                return 0;
            serialize_type tt = (serialize_type)ptr[0];
            ++ptr;
            --size;
            uint64_t v = 0;
            int c = int_deserialize((uint8_t *)ptr, size, &v);
            if (c <= 0)
                return c;
            ptr += c;
            size -= c;
            int64_t sv = (tt == serialize_negtive_int) ? -(int64_t)v : (int64_t)v;
            buf_i64[i] = sv;
        }
        tensor->data = buf_i64;
        out->_type = tinybuf_tensor;
        out->_data._custom = tensor;
        out->_custom_free = NULL;
        return 1;
    }
}

static int tinybuf_deserialize_dense_tensor(const char *ptr, int size, tinybuf_value *out)
{
    uint64_t dims = 0;
    int a = int_deserialize((uint8_t *)ptr, size, &dims);
    if (a <= 0)
        return a;
    ptr += a;
    size -= a;
    int64_t *shape = (int64_t *)tinybuf_malloc((int)(sizeof(int64_t) * (size_t)dims));
    for (uint64_t i = 0; i < dims; ++i)
    {
        uint64_t k = 0;
        int b = int_deserialize((uint8_t *)ptr, size, &k);
        if (b <= 0)
            return b;
        ptr += b;
        size -= b;
        shape[i] = (int64_t)k;
    }
    uint64_t dt = 0;
    int e = int_deserialize((uint8_t *)ptr, size, &dt);
    if (e <= 0)
        return e;
    ptr += e;
    size -= e;
    int64_t count = 1;
    for (uint64_t i = 0; i < dims; ++i)
        count *= shape[i];
    tinybuf_tensor_t *tensor = (tinybuf_tensor_t *)tinybuf_malloc(sizeof(tinybuf_tensor_t));
    tensor->dtype = (int)dt;
    tensor->dims = (int)dims;
    tensor->count = count;
    tensor->shape = shape;
    if ((int)dt == 8)
    {
        if (size < 8 * count)
            return 0;
        double *buf = (double *)tinybuf_malloc((int)(sizeof(double) * count));
        for (int64_t i = 0; i < count; ++i)
            buf[i] = read_double((uint8_t *)ptr + i * 8);
        tensor->data = buf;
    }
    else if ((int)dt == 10)
    {
        if (size < 4 * count)
            return 0;
        float *buf = (float *)tinybuf_malloc((int)(sizeof(float) * count));
        for (int64_t i = 0; i < count; ++i)
        {
            uint32_t raw = load_be32((uint8_t *)ptr + i * 4);
            memcpy(&buf[i], &raw, 4);
        }
        tensor->data = buf;
    }
    else if ((int)dt == 11)
    {
        int64_t bytes = (count + 7) / 8;
        if (size < bytes)
            return 0;
        uint8_t *buf = (uint8_t *)tinybuf_malloc((int)bytes);
        memcpy(buf, ptr, (size_t)bytes);
        tensor->data = buf;
    }
    else
    {
        if (size < count)
            return 0;
        int64_t *buf = (int64_t *)tinybuf_malloc((int)(sizeof(int64_t) * count));
        for (int64_t flat = 0; flat < count; ++flat)
        {
            if (size < 1)
                return 0;
            serialize_type tt = (serialize_type)ptr[0];
            ++ptr;
            --size;
            uint64_t v = 0;
            int c = int_deserialize((uint8_t *)ptr, size, &v);
            if (c <= 0)
                return c;
            ptr += c;
            size -= c;
            int64_t sv = (tt == serialize_negtive_int) ? -(int64_t)v : (int64_t)v;
            buf[flat] = sv;
        }
        tensor->data = buf;
    }
    out->_type = tinybuf_tensor;
    out->_data._custom = tensor;
    out->_custom_free = NULL;
    return 1;
}

static int tinybuf_deserialize_sparse_tensor(const char *ptr, int size, tinybuf_value *out)
{
    return -1;
}

int tinybuf_value_deserialize_basic(const char *ptr, int size, tinybuf_value *out)
{
    assert(out);
    assert(ptr);
    assert(out->_type == tinybuf_null);
    if (size < 1)
    {
        return 0;
    }

    serialize_type type = ptr[0];
    --size;
    ++ptr;
    switch (type)
    {
    case serialize_null:
    {
        tinybuf_value_clear(out);
        return 1;
    }
    case serialize_negtive_int:
    case serialize_positive_int:
        return optional_add(try_read_int(type, ptr, size, out), 1);
    case serialize_bool_false:
    case serialize_bool_true:
    {
        tinybuf_value_init_bool(out, type == serialize_bool_true);
        return 1;
    }
    case serialize_double:
    {
        if (size < 8)
        {
            return 0;
        }
        tinybuf_value_init_double(out, read_double((uint8_t *)ptr));
        return 9;
    }
    case serialize_string:
    {
        uint64_t bytes_len;
        int len = int_deserialize((uint8_t *)ptr, size, &bytes_len);
        if (len <= 0)
        {
            return len;
        }
        ptr += len;
        size -= len;
        if (size < bytes_len)
        {
            return 0;
        }
        tinybuf_value_init_string(out, ptr, (int)bytes_len);
        return 1 + len + (int)bytes_len;
    }
    /* str_pool index and type_idx branches remain in tinybuf.c to use shared state */
    default:
        s_last_error_msg = "deserialize type unknown";
        return -1;
    }
}
#include <assert.h>

int int_deserialize(const uint8_t *in, int in_size, uint64_t *out)
{
    if (in_size < 1) return 0;
    *out = 0;
    int index = 0;
    while (1)
    {
        uint8_t byte = in[index];
        (*out) |= (uint64_t)(byte & 0x7F) << ((index++) * 7);
        if ((byte & 0x80) == 0) break;
        if (index >= in_size) return 0;
        if (index * 7 > 56) return -1;
    }
    return index;
}

int optional_add(int x, int addx){ if(x<0) return x; return x+addx; }

int tinybuf_value_deserialize(const char *ptr, int size, tinybuf_value *out)
{
    assert(out);
    assert(ptr);
    assert(out->_type == tinybuf_null);
    if (size < 1)
    {
        return 0;
    }

    uint64_t cnt = 0, dt = 0, dims = 0, k = 0;
    int a = 0, b = 0, e = 0;
    int64_t count = 0, prod = 1;
    int64_t *shape = NULL;
    tinybuf_tensor_t *tnsr = NULL;

    serialize_type type = (serialize_type)ptr[0];
    --size;
    ++ptr;
    switch (type)
    {
    case serialize_null:
    {
        tinybuf_value_clear(out);
        return 1;
    }
    case serialize_negtive_int:
    case serialize_positive_int:
        return optional_add(try_read_int(type, ptr, size, out), 1);
    case serialize_bool_false:
    case serialize_bool_true:
    {
        tinybuf_value_init_bool(out, type == serialize_bool_true);
        return 1;
    }
    case serialize_double:
    {
        if (size < 8)
        {
            return 0;
        }
        tinybuf_value_init_double(out, read_double((uint8_t *)ptr));
        return 9;
    }
    case serialize_string:
    {
        uint64_t bytes_len;
        int len = int_deserialize((uint8_t *)ptr, size, &bytes_len);
        if (len <= 0)
        {
            return len;
        }
        ptr += len;
        size -= len;
        if (size < bytes_len)
        {
            return 0;
        }
        tinybuf_value_init_string(out, ptr, (int)bytes_len);
        return 1 + len + (int)bytes_len;
    }
    case serialize_str_index:
    {
        uint64_t idx;
        int len = int_deserialize((uint8_t *)ptr, size, &idx);
        if (len <= 0)
        {
            return len;
        }
        if (s_strpool_offset_read < 0 || s_strpool_base_read == NULL)
        {
            s_last_error_msg = "strpool not initialized";
            return -1;
        }
        const char *pool_start = s_strpool_base_read + s_strpool_offset_read;
        const char *q = pool_start;
        int64_t r = ((const char *)ptr + size) - pool_start;
        if (r < 1)
        {
            return 0;
        }
        if ((uint8_t)q[0] == serialize_str_pool)
        {
            ++q;
            --r;
            uint64_t cnt2 = 0;
            int l2 = int_deserialize((uint8_t *)q, (int)r, &cnt2);
            if (l2 <= 0)
            {
                return -1;
            }
            q += l2;
            r -= l2;
            if (idx >= cnt2)
            {
                return -1;
            }
            for (uint64_t i = 0; i < cnt2; ++i)
            {
                if (r < 1)
                {
                    return 0;
                }
                if ((uint8_t)q[0] != serialize_string)
                {
                    return -1;
                }
                ++q;
                --r;
                uint64_t sl;
                int l3 = int_deserialize((uint8_t *)q, (int)r, &sl);
                if (l3 <= 0)
                {
                    return l3;
                }
                q += l3;
                r -= l3;
                if (r < (int64_t)sl)
                {
                    return 0;
                }
                if (i == idx)
                {
                    tinybuf_value_init_string(out, q, (int)sl);
                    return 1 + len;
                }
                q += sl;
                r -= sl;
            }
            return -1;
        }
        else if ((uint8_t)q[0] == 27)
        {
            ++q;
            --r;
            uint64_t ncount = 0;
            int l2 = int_deserialize((uint8_t *)q, (int)r, &ncount);
            if (l2 <= 0)
            {
                return -1;
            }
            const char *nodes_base = q + l2;
            int64_t nodes_size = r - l2;
            const char *p = nodes_base;
            int64_t rr = nodes_size;
            int found = -1;
            for (uint64_t i = 0; i < ncount; ++i)
            {
                uint64_t parent = 0;
                int lp = int_deserialize((uint8_t *)p, (int)rr, &parent);
                if (lp <= 0)
                    return lp;
                p += lp;
                rr -= lp;
                if (rr < 2)
                    return 0;
                unsigned char ch = (unsigned char)p[0];
                unsigned char flag = (unsigned char)p[1];
                p += 2;
                rr -= 2;
                if (flag)
                {
                    uint64_t leaf = 0;
                    int ll = int_deserialize((uint8_t *)p, (int)rr, &leaf);
                    if (ll <= 0)
                        return ll;
                    p += ll;
                    rr -= ll;
                    if (leaf == idx)
                    {
                        found = (int)i;
                        break;
                    }
                }
            }
            if (found < 0)
            {
                return -1;
            }
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
                uint64_t leaf = 0;
                while (node_index <= current)
                {
                    uint64_t parent = 0;
                    int lp = int_deserialize((uint8_t *)p2, (int)rr2, &parent);
                    p2 += lp;
                    rr2 -= lp;
                    if (rr2 < 2)
                        return 0;
                    ch = (unsigned char)p2[0];
                    flag = (unsigned char)p2[1];
                    p2 += 2;
                    rr2 -= 2;
                    if (flag)
                    {
                        int ll = int_deserialize((uint8_t *)p2, (int)rr2, &leaf);
                        p2 += ll;
                        rr2 -= ll;
                    }
                    parent_index = (int)parent;
                    ++node_index;
                }
                if (ch)
                {
                    buffer_append(tmp, (const char *)&ch, 1);
                }
                if (parent_index <= 0)
                {
                    break;
                }
                current = parent_index;
            }
            int tlen = buffer_get_length_inline(tmp);
            const char *td = buffer_get_data_inline(tmp);
            char *rev = (char *)tinybuf_malloc(tlen);
            for (int i = 0; i < tlen; ++i)
            {
                rev[i] = td[tlen - 1 - i];
            }
            tinybuf_value_init_string(out, rev, tlen);
            tinybuf_free(rev);
            buffer_free(tmp);
            return 1 + len;
        }
        else
        {
            return -1;
        }
    }
    case serialize_map:
    {
        int consumed = 0;
        uint64_t map_size;
        int len = int_deserialize((uint8_t *)ptr, size, &map_size);
        if (len <= 0)
        {
            return len;
        }
        ptr += len;
        size -= len;
        consumed = 1 + len;
        out->_type = tinybuf_map;
        for (int i = 0; i < (int)map_size; ++i)
        {
            uint64_t key_len;
            len = int_deserialize((uint8_t *)ptr, size, &key_len);
            if (len <= 0)
            {
                tinybuf_value_clear(out);
                return len;
            }
            ptr += len;
            size -= len;
            consumed += len;
            if (size < key_len)
            {
                tinybuf_value_clear(out);
                return 0;
            }
            char *key_ptr = (char *)ptr;
            ptr += key_len;
            size -= key_len;
            consumed += key_len;
            tinybuf_value *value = tinybuf_value_alloc();
            int value_len = tinybuf_value_deserialize(ptr, size, value);
            if (value_len <= 0)
            {
                tinybuf_value_free(value);
                tinybuf_value_clear(out);
                return value_len;
            }
            buffer *key = buffer_alloc();
            buffer_assign(key, key_ptr, (int)key_len);
            tinybuf_value_map_set2(out, key, value);
            ptr += value_len;
            size -= value_len;
            consumed += value_len;
        }
        return consumed;
    }
    case serialize_array:
    {
        int consumed = 0;
        uint64_t array_size;
        int len = int_deserialize((uint8_t *)ptr, size, &array_size);
        if (len <= 0)
        {
            return len;
        }
        ptr += len;
        size -= len;
        consumed = 1 + len;
        out->_type = tinybuf_array;
        for (int i = 0; i < (int)array_size; ++i)
        {
            tinybuf_value *value = tinybuf_value_alloc();
            int value_len = tinybuf_value_deserialize(ptr, size, value);
            if (value_len <= 0)
            {
                tinybuf_value_free(value);
                tinybuf_value_clear(out);
                return value_len;
            }
            tinybuf_value_array_append(out, value);
            ptr += value_len;
            size -= value_len;
            consumed += value_len;
        }
        return consumed;
    }
    case serialize_vector_tensor:
        return tinybuf_deserialize_vector_tensor(ptr, size, out);
    case serialize_dense_tensor:
        return tinybuf_deserialize_dense_tensor(ptr, size, out);
    case serialize_sparse_tensor:
        return tinybuf_deserialize_sparse_tensor(ptr, size, out);
    case serialize_bool_map:
    {
        uint64_t cntb = 0;
        int ab = int_deserialize((uint8_t *)ptr, size, &cntb);
        if (ab <= 0) return ab;
        ptr += ab; size -= ab;
        int64_t bytes = ((int64_t)cntb + 7) / 8;
        if (size < bytes) return 0;
        tinybuf_bool_map_t *bm = (tinybuf_bool_map_t *)tinybuf_malloc(sizeof(tinybuf_bool_map_t));
        bm->count = (int64_t)cntb;
        bm->bits = (uint8_t *)tinybuf_malloc((int)bytes);
        memcpy(bm->bits, ptr, (size_t)bytes);
        out->_type = tinybuf_bool_map;
        out->_data._custom = bm;
        out->_custom_free = NULL;
        return 1 + ab + (int)bytes;
    }
    case serialize_indexed_tensor:
    {
        const char *p0 = ptr;
        tinybuf_value *ten = tinybuf_value_alloc();
        int r1 = tinybuf_value_deserialize(ptr, size, ten);
        if (r1 <= 0)
        {
            buf_ref br_fallback = (buf_ref){(char *)ptr, (int64_t)size, (char *)ptr, (int64_t)size};
            tinybuf_result r1b = tinybuf_try_read_box(&br_fallback, ten, contain_any);
            if (r1b.res <= 0)
            {
                tinybuf_value_free(ten);
                return r1b.res;
            }
            r1 = r1b.res;
        }
        ptr += r1;
        size -= r1;
        uint64_t dims1 = 0;
        int a1 = int_deserialize((uint8_t *)ptr, size, &dims1);
        if (a1 <= 0) { tinybuf_value_free(ten); return a1; }
        ptr += a1; size -= a1;
        tinybuf_indexed_tensor_t *it = (tinybuf_indexed_tensor_t *)tinybuf_malloc(sizeof(tinybuf_indexed_tensor_t));
        it->tensor = ten; it->dims = (int)dims1; it->indices = NULL;
        if (it->dims > 0)
        {
            it->indices = (tinybuf_value **)tinybuf_malloc(sizeof(tinybuf_value *) * (size_t)it->dims);
            for (int i = 0; i < it->dims; ++i) it->indices[i] = NULL;
        }
        for (int i = 0; i < it->dims; ++i)
        {
            uint64_t has = 0; int ai = int_deserialize((uint8_t *)ptr, size, &has);
            if (ai <= 0)
            {
                if (it->indices)
                {
                    for (int k = 0; k < i; ++k) if (it->indices[k]) tinybuf_value_free(it->indices[k]);
                    tinybuf_free(it->indices);
                }
                tinybuf_value_free(ten); tinybuf_free(it); return ai;
            }
            ptr += ai; size -= ai;
            if (has)
            {
                tinybuf_value *idx = tinybuf_value_alloc();
                int r2 = tinybuf_value_deserialize(ptr, size, idx);
                if (r2 <= 0)
                {
                    buf_ref br2 = (buf_ref){(char *)ptr, (int64_t)size, (char *)ptr, (int64_t)size};
                    tinybuf_result rr3 = tinybuf_try_read_box(&br2, idx, contain_any);
                    if (rr3.res <= 0)
                    {
                        if (it->indices)
                        {
                            for (int k = 0; k < i; ++k) if (it->indices[k]) tinybuf_value_free(it->indices[k]);
                            tinybuf_free(it->indices);
                        }
                        tinybuf_value_free(ten); tinybuf_free(it); return rr3.res;
                    }
                    r2 = rr3.res;
                }
                it->indices[i] = idx;
                ptr += r2; size -= r2;
            }
        }
        out->_type = tinybuf_indexed_tensor;
        out->_data._custom = it;
        out->_custom_free = NULL;
        return (int)(ptr - p0) + 1;
    }
    default:
        s_last_error_msg = "deserialize type unknown";
        return -1;
    }
}
