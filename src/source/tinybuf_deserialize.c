#include "tinybuf_private.h"
#include "tinybuf_buffer.h"
#include "tinybuf_plugin.h"
#include <string.h>

static inline uint32_t load_be32(const void *p)
{
    uint32_t val;
    memcpy(&val, p, sizeof val);
    return ntohl(val);
}

static inline double read_double(uint8_t *ptr)
{
    uint64_t val = ((uint64_t)load_be32(ptr) << 32) | load_be32(ptr + 4);
    double db = 0;
    memcpy(&db, &val, 8);
    return db;
}

inline int try_read_int_tovar(BOOL isneg, const char *ptr, int size, QWORD *out_val)
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

inline int try_read_int(serialize_type type, const char *ptr, int size, tinybuf_value *out)
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
