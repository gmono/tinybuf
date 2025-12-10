#include "tinybuf_private.h"
#include "tinybuf_plugin.h"
static int contain_any(uint64_t v);
#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif
#include <stdbool.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <stdatomic.h>
#include <sched.h>
#include <time.h>
#endif

// 1 支持变长数字格式并支持配置开启 2 支持KVPair通用格式
// 3 支持自描述结构和向前兼容支持
// int类型包括1到8字节整数 包括有符号无符号 且以无符号方式存储

// dll扩展支持 支持dll提交 sign支持列表  并提供read与write接口执行基于插件的序列化

typedef int64_t ssize;
typedef uint64_t usize;

static tinybuf_read_pointer_mode s_read_pointer_mode = tinybuf_read_pointer_ref;

typedef uint64_t QWORD;
typedef int64_t SQWORD;

const char *s_last_error_msg = NULL;

// 是否含有子引用
inline bool has_sub_ref(const tinybuf_value *value)
{
    return value->_data._ref != NULL && (value->_type == tinybuf_value_ref || value->_type == tinybuf_version);
}

// 递归需要
int tinybuf_value_clear(tinybuf_value *value);
inline bool maybe_free_sub_ref(tinybuf_value *value)
{
    if (has_sub_ref(value))
    {
        tinybuf_value *sub = value->_data._ref;
        // break the cycle first
        value->_data._ref = NULL;
        // recursively clear the referenced object; container ownership will free its pointer
        tinybuf_value_clear(sub);
        return true;
    }
    return false;
}

static tinybuf_value **s_clear_stack = NULL;
static int s_clear_stack_count = 0;
static int s_clear_stack_capacity = 0;

static inline int clear_stack_contains(tinybuf_value *v)
{
    for (int i = 0; i < s_clear_stack_count; ++i)
    {
        if (s_clear_stack[i] == v)
        {
            return 1;
        }
    }
    return 0;
}

static inline void clear_stack_push(tinybuf_value *v)
{
    if (s_clear_stack_count == s_clear_stack_capacity)
    {
        int newcap = s_clear_stack_capacity ? (s_clear_stack_capacity * 2) : 16;
        s_clear_stack = (tinybuf_value **)tinybuf_realloc(s_clear_stack, sizeof(tinybuf_value *) * newcap);
        s_clear_stack_capacity = newcap;
    }
    s_clear_stack[s_clear_stack_count++] = v;
}

static inline void clear_stack_pop(tinybuf_value *v)
{
    if (s_clear_stack_count == 0)
    {
        return;
    }
    if (s_clear_stack[s_clear_stack_count - 1] == v)
    {
        --s_clear_stack_count;
        return;
    }
    for (int i = 0; i < s_clear_stack_count; ++i)
    {
        if (s_clear_stack[i] == v)
        {
            for (int j = i + 1; j < s_clear_stack_count; ++j)
            {
                s_clear_stack[j - 1] = s_clear_stack[j];
            }
            --s_clear_stack_count;
            return;
        }
    }
}

// 是否需要执行custom 指针释放
inline bool need_custom_free(const tinybuf_value *value)
{
    return value->_data._custom != NULL && (value->_type == tinybuf_custom || value->_type == tinybuf_tensor || value->_type == tinybuf_bool_map || value->_type == tinybuf_indexed_tensor);
}
// 释放custom指针
inline bool maybe_free_custom(tinybuf_value *value)
{
    if (need_custom_free(value))
    {
        if (value->_custom_free)
        {
            value->_custom_free(value->_data._custom);
            value->_data._custom = NULL;
            value->_type = tinybuf_null;
        }
        else
        {
            free(value->_data._custom);
            value->_data._custom = NULL;
            value->_type = tinybuf_null;
        }
        return true;
    }
    return false;
}

//---压缩boxlist数据集合的序列化
static inline int boxlist_serialize()
{
}
static inline int boxlist_deserialize()
{
}
//--压缩kvpairs的序列化

static inline int kvpairs_serialize()
{
}
static inline int kvpairs_deserialize()
{
}
static inline int kvpairs_boxkey_serialize()
{
}
static inline int kvpairs_boxkey_deserialize()
{
}


tinybuf_value *tinybuf_value_alloc(void)
{
    tinybuf_value *ret = tinybuf_malloc(sizeof(tinybuf_value));
    assert(ret);
    memset(ret, 0, sizeof(tinybuf_value));
    ret->_type = tinybuf_null;
    ret->_plugin_index = -1;
    ret->_custom_box_type = -1;
    return ret;
}

/**
 * 创建对象
 * @return 返回对象
 */
tinybuf_value *tinybuf_value_alloc_with_type(tinybuf_type type)
{
    tinybuf_value *value = tinybuf_value_alloc();
    value->_type = type;
    return value;
}

int tinybuf_value_free(tinybuf_value *value)
{
    assert(value);
    // 清空对象
    tinybuf_value_clear(value);
    // 释放对象指针
    tinybuf_free(value);
    return 0;
}

// 清空对象 相当于maybe_free
int tinybuf_value_clear(tinybuf_value *value)
{
    assert(value);
    if (clear_stack_contains(value))
    {
        return 0;
    }
    clear_stack_push(value);
    switch (value->_type)
    {
    case tinybuf_string:
    {
        if (value->_data._string)
        {
            // 清空字符串
            buffer_free(value->_data._string);
            value->_data._string = NULL;
        }
    }
    break;

    case tinybuf_map:
    case tinybuf_array:
    {
        if (value->_data._map_array)
        {
            // 清空map或array
            avl_tree_free(value->_data._map_array);
            value->_data._map_array = NULL;
        }
    }
    break;

    default:
        break;
    }

    // 释放custom指针 如果需要释放的话
    maybe_free_sub_ref(value);
    maybe_free_custom(value);

    if (value->_type != tinybuf_null)
    {
        // 清空所有
        memset(value, 0, sizeof(tinybuf_value));
        value->_type = tinybuf_null;
    }
    clear_stack_pop(value);
    return 0;
}

static int mapKeyCompare(AVLTreeKey key1, AVLTreeKey key2)
{
    buffer *buf_key1 = (buffer *)key1;
    buffer *buf_key2 = (buffer *)key2;
    int buf_len1 = buffer_get_length_inline(buf_key1);
    int buf_len2 = buffer_get_length_inline(buf_key2);
    int min_len = buf_len1 < buf_len2 ? buf_len1 : buf_len2;
    int ret = memcmp(buffer_get_data_inline(buf_key1), buffer_get_data_inline(buf_key2), min_len);
    if (ret != 0)
    {
        return ret;
    }
    // 两者前面字节部分相同，那么再比较字符串长度
    return buf_len1 - buf_len2;
}

static void mapFreeValueFunc(AVLTreeValue value)
{
    tinybuf_value_free(value);
}

static void buffer_key_free(AVLTreeKey key)
{
    buffer_free((buffer *)key);
}

int tinybuf_value_map_set2(tinybuf_value *parent, buffer *key, tinybuf_value *value)
{
    assert(parent);
    assert(key);
    assert(value);
    // version list也是map类型
    if (parent->_type != tinybuf_map &&
        parent->_type != tinybuf_versionlist)
    {
        tinybuf_value_clear(parent);
        parent->_type = tinybuf_map;
    }

    if (!parent->_data._map_array)
    {
        parent->_data._map_array = avl_tree_new(mapKeyCompare);
    }
    avl_tree_insert(parent->_data._map_array, key, value, buffer_key_free, mapFreeValueFunc);
    return 0;
}

int tinybuf_value_map_set(tinybuf_value *parent, const char *key, tinybuf_value *value)
{
    assert(key);
    buffer *key_buf = buffer_alloc();
    assert(key_buf);
    buffer_assign(key_buf, key, 0);
    return tinybuf_value_map_set2(parent, key_buf, value);
}

// 以数字为key
int tinybuf_value_map_set_int(tinybuf_value *parent, uint64_t key, tinybuf_value *value)
{
    buffer *key_buf = buffer_alloc();
    assert(key_buf);
    dump_int(key, key_buf);
    return tinybuf_value_map_set2(parent, key_buf, value);
}

static int arrayKeyCompare(AVLTreeKey key1, AVLTreeKey key2)
{
    return (int)(((intptr_t)key1) - ((intptr_t)key2));
}

int tinybuf_value_array_append(tinybuf_value *parent, tinybuf_value *value)
{
    assert(parent);
    assert(value);
    if (parent->_type != tinybuf_array)
    {
        tinybuf_value_clear(parent);
        parent->_type = tinybuf_array;
    }

    if (!parent->_data._map_array)
    {
        parent->_data._map_array = avl_tree_new(arrayKeyCompare);
    }
    int key = avl_tree_num_entries(parent->_data._map_array);
    avl_tree_insert(parent->_data._map_array,
                    (AVLTreeKey)key,
                    value,
                    NULL,
                    mapFreeValueFunc);
    return 0;
}

int dump_int(uint64_t len, buffer *out)
{
    int buf_len = buffer_get_length_inline(out);
    if (buffer_get_capacity_inline(out) - buf_len < 16)
    {
        // 容量不够，追加容量
        buffer_add_capacity(out, 16);
    }
    // 直接序列化进去
    int add = int_serialize(len, (uint8_t *)buffer_get_data_inline(out) + buf_len);
    buffer_set_length(out, buf_len + add);
    return add;
}





// 预写（precache）支持：在写侧允许提前写出对象，后续遇到该对象时改为输出指针
typedef struct
{
    const tinybuf_value *value;
    buffer *stream;
    int64_t start;
} precache_entry;
static precache_entry *s_precache = NULL;
static int s_precache_count = 0;
static int s_precache_capacity = 0;

/* strpool declarations are at the top of file */

// 裸整数操作读写函数 正整数做uint64 负数作为int64
inline int try_read_int_tovar(BOOL isneg, const char *ptr, int size, QWORD *out_val)
{
    int len = int_deserialize((uint8_t *)ptr, size, out_val);
    if (len < 0)
    {
        return len;
    }
    if (isneg)
    {
        // 负整数，翻转之 作为int64_t 处理
        *out_val = -(*out_val);
    }
    return len;
}

// 读取裸整数 到value
inline int try_read_int(serialize_type type, const char *ptr, int size, tinybuf_value *out)
{
    uint64_t int_val = 0;
    BOOL isneg = type == serialize_negtive_int;
    int len = try_read_int_tovar(isneg, ptr, size, &int_val);
    if (len < 0)
    {
        return len; // 不初始化value
    }
    // 创建一个vlaue对象
    tinybuf_value_init_int(out, int_val);
    return len;
}

//---
int tinybuf_value_deserialize(const char *ptr, int size, tinybuf_value *out);

// 支持自定义描述序列支持的反序列化操作
// 描述子是一串 serialize_type 类型的字节序列
// 用于支持集中类型表示 集中类型表示可优化内存与寄存器
//  int read_box_by_descriptor(const  char* ptr,int size,const serialize_type* desc,int desclen){

// }

//--核心反序列化路由函数 初级版本 只能处理纯初级格式 内部不能嵌套二级box
static int tinybuf_deserialize_vector_tensor(const char *ptr, int size, tinybuf_value *out)
{
    uint64_t cnt = 0;
    uint64_t dt = 0;
    int a = 0;
    int b = 0;
    int64_t count = 0;
    tinybuf_tensor_t *tensor = NULL;
    int64_t *shape = NULL;
    uint8_t *buf_u8 = NULL;
    int64_t *buf_i64 = NULL;
    int64_t i = 0;
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
    tensor = (tinybuf_tensor_t *)tinybuf_malloc((int)sizeof(tinybuf_tensor_t));
    tensor->dtype = (int)dt;
    tensor->dims = 1;
    shape = (int64_t *)tinybuf_malloc((int)sizeof(int64_t));
    if (!shape)
    {
        tinybuf_free(tensor);
        return -1;
    }
    shape[0] = count;
    tensor->shape = shape;
    tensor->count = count;
    if (tensor->dtype == 8)
    {
        if (size < count * 8)
        {
            tinybuf_free(shape);
            tinybuf_free(tensor);
            return 0;
        }
        tensor->data = tinybuf_malloc((int)(count * 8));
        memcpy(tensor->data, ptr, (size_t)(count * 8));
        ptr += count * 8;
        size -= count * 8;
    }
    else if (tensor->dtype == 10)
    {
        if (size < count * 4)
        {
            tinybuf_free(shape);
            tinybuf_free(tensor);
            return 0;
        }
        tensor->data = tinybuf_malloc((int)(count * 4));
        memcpy(tensor->data, ptr, (size_t)(count * 4));
        ptr += count * 4;
        size -= count * 4;
    }
    else if (tensor->dtype == 11)
    {
        int64_t bytes = (count + 7) / 8;
        if (size < bytes)
        {
            tinybuf_free(shape);
            tinybuf_free(tensor);
            return 0;
        }
        buf_u8 = (uint8_t *)tinybuf_malloc((int)count);
        for (i = 0; i < count; ++i)
        {
            uint8_t one = ((const uint8_t *)ptr)[i / 8];
            int bit = 7 - (int)(i % 8);
            buf_u8[i] = (uint8_t)((one >> bit) & 1);
        }
        ptr += bytes;
        size -= bytes;
        tensor->data = buf_u8;
    }
    else
    {
        buf_i64 = (int64_t *)tinybuf_malloc((int)(count * (int64_t)sizeof(int64_t)));
        for (i = 0; i < count; ++i)
        {
            if (size < 1)
            {
                tinybuf_free(buf_i64);
                tinybuf_free(shape);
                tinybuf_free(tensor);
                return 0;
            }
            serialize_type tt = (serialize_type)ptr[0];
            ptr += 1;
            size -= 1;
            uint64_t v = 0;
            int c = int_deserialize((uint8_t *)ptr, size, &v);
            if (c <= 0)
            {
                tinybuf_free(buf_i64);
                tinybuf_free(shape);
                tinybuf_free(tensor);
                return c;
            }
            ptr += c;
            size -= c;
            int64_t sv = (tt == serialize_negtive_int) ? -(int64_t)v : (int64_t)v;
            buf_i64[i] = sv;
        }
        tensor->data = buf_i64;
    }
    out->_type = tinybuf_tensor;
    out->_data._custom = tensor;
    out->_custom_free = NULL;
    return 1 + a + b;
}

static int tinybuf_deserialize_dense_tensor(const char *ptr, int size, tinybuf_value *out)
{
    uint64_t dims = 0;
    uint64_t dt = 0;
    uint64_t i = 0;
    int a = 0;
    int b = 0;
    int64_t *shape = NULL;
    int64_t prod = 1;
    tinybuf_tensor_t *tensor = NULL;
    int64_t ii = 0;
    uint8_t *buf_u8 = NULL;
    int64_t *buf_i64 = NULL;
    a = int_deserialize((uint8_t *)ptr, size, &dims);
    if (a <= 0)
        return a;
    ptr += a;
    size -= a;
    shape = (int64_t *)tinybuf_malloc((int)(sizeof(int64_t) * (int)dims));
    for (i = 0; i < dims; ++i)
    {
        uint64_t d = 0;
        int c = int_deserialize((uint8_t *)ptr, size, &d);
        if (c <= 0)
        {
            tinybuf_free(shape);
            return c;
        }
        ptr += c;
        size -= c;
        shape[i] = (int64_t)d;
        prod *= shape[i];
    }
    b = int_deserialize((uint8_t *)ptr, size, &dt);
    if (b <= 0)
    {
        tinybuf_free(shape);
        return b;
    }
    ptr += b;
    size -= b;
    tensor = (tinybuf_tensor_t *)tinybuf_malloc((int)sizeof(tinybuf_tensor_t));
    tensor->dtype = (int)dt;
    tensor->dims = (int)dims;
    tensor->shape = shape;
    tensor->count = prod;
    if (tensor->dtype == 8)
    {
        if (size < prod * 8)
        {
            tinybuf_free(shape);
            tinybuf_free(tensor);
            return 0;
        }
        tensor->data = tinybuf_malloc((int)(prod * 8));
        memcpy(tensor->data, ptr, (size_t)(prod * 8));
        ptr += prod * 8;
        size -= prod * 8;
    }
    else if (tensor->dtype == 10)
    {
        if (size < prod * 4)
        {
            tinybuf_free(shape);
            tinybuf_free(tensor);
            return 0;
        }
        tensor->data = tinybuf_malloc((int)(prod * 4));
        memcpy(tensor->data, ptr, (size_t)(prod * 4));
        ptr += prod * 4;
        size -= prod * 4;
    }
    else if (tensor->dtype == 11)
    {
        int64_t bytes = (prod + 7) / 8;
        if (size < bytes)
        {
            tinybuf_free(shape);
            tinybuf_free(tensor);
            return 0;
        }
        buf_u8 = (uint8_t *)tinybuf_malloc((int)prod);
        for (ii = 0; ii < prod; ++ii)
        {
            uint8_t one = ((const uint8_t *)ptr)[ii / 8];
            int bit = 7 - (int)(ii % 8);
            buf_u8[ii] = (uint8_t)((one >> bit) & 1);
        }
        ptr += bytes;
        size -= bytes;
        tensor->data = buf_u8;
    }
    else
    {
        buf_i64 = (int64_t *)tinybuf_malloc((int)(prod * (int64_t)sizeof(int64_t)));
        for (ii = 0; ii < prod; ++ii)
        {
            if (size < 1)
            {
                tinybuf_free(buf_i64);
                tinybuf_free(shape);
                tinybuf_free(tensor);
                return 0;
            }
            serialize_type tt = (serialize_type)ptr[0];
            ptr += 1;
            size -= 1;
            uint64_t v = 0;
            int c = int_deserialize((uint8_t *)ptr, size, &v);
            if (c <= 0)
            {
                tinybuf_free(buf_i64);
                tinybuf_free(shape);
                tinybuf_free(tensor);
                return c;
            }
            ptr += c;
            size -= c;
            int64_t sv = (tt == serialize_negtive_int) ? -(int64_t)v : (int64_t)v;
            buf_i64[ii] = sv;
        }
        tensor->data = buf_i64;
    }
    out->_type = tinybuf_tensor;
    out->_data._custom = tensor;
    out->_custom_free = NULL;
    return 1 + a + b;
}

static int tinybuf_deserialize_sparse_tensor(const char *ptr, int size, tinybuf_value *out)
{
    uint64_t dims = 0;
    uint64_t dt = 0;
    uint64_t k = 0;
    uint64_t i = 0;
    uint64_t ii = 0;
    int a = 0;
    int b = 0;
    int e = 0;
    int64_t *shape = NULL;
    int64_t prod = 1;
    tinybuf_tensor_t *tensor = NULL;
    int j = 0;
    a = int_deserialize((uint8_t *)ptr, size, &dims);
    if (a <= 0)
        return a;
    ptr += a;
    size -= a;
    shape = (int64_t *)tinybuf_malloc((int)(sizeof(int64_t) * (int)dims));
    for (i = 0; i < dims; ++i)
    {
        uint64_t d = 0;
        int c = int_deserialize((uint8_t *)ptr, size, &d);
        if (c <= 0)
        {
            tinybuf_free(shape);
            return c;
        }
        ptr += c;
        size -= c;
        shape[i] = (int64_t)d;
        prod *= shape[i];
    }
    b = int_deserialize((uint8_t *)ptr, size, &dt);
    if (b <= 0)
    {
        tinybuf_free(shape);
        return b;
    }
    ptr += b;
    size -= b;
    e = int_deserialize((uint8_t *)ptr, size, &k);
    if (e <= 0)
    {
        tinybuf_free(shape);
        return e;
    }
    ptr += e;
    size -= e;
    tensor = (tinybuf_tensor_t *)tinybuf_malloc((int)sizeof(tinybuf_tensor_t));
    tensor->dtype = (int)dt;
    tensor->dims = (int)dims;
    tensor->shape = shape;
    tensor->count = prod;
    if (tensor->dtype == 8)
    {
        double *buf = (double *)tinybuf_malloc((int)(prod * 8));
        memset(buf, 0, (size_t)(prod * 8));
        for (ii = 0; ii < k; ++ii)
        {
            int64_t stride = 1;
            int64_t flat = 0;
            for (j = (int)dims - 1; j >= 0; --j)
            {
                uint64_t idx = 0;
                int c = int_deserialize((uint8_t *)ptr, size, &idx);
                if (c <= 0)
                {
                    tinybuf_free(buf);
                    tinybuf_free(shape);
                    tinybuf_free(tensor);
                    return c;
                }
                ptr += c;
                size -= c;
                flat += (int64_t)idx * stride;
                stride *= shape[j];
            }
            if (size < 8)
            {
                tinybuf_free(buf);
                tinybuf_free(shape);
                tinybuf_free(tensor);
                return 0;
            }
            double dv = read_double((uint8_t *)ptr);
            ptr += 8;
            size -= 8;
            buf[flat] = dv;
        }
        tensor->data = buf;
    }
    else if (tensor->dtype == 10)
    {
        float *buf = (float *)tinybuf_malloc((int)(prod * 4));
        memset(buf, 0, (size_t)(prod * 4));
        for (ii = 0; ii < k; ++ii)
        {
            int64_t stride = 1;
            int64_t flat = 0;
            for (j = (int)dims - 1; j >= 0; --j)
            {
                uint64_t idx = 0;
                int c = int_deserialize((uint8_t *)ptr, size, &idx);
                if (c <= 0)
                {
                    tinybuf_free(buf);
                    tinybuf_free(shape);
                    tinybuf_free(tensor);
                    return c;
                }
                ptr += c;
                size -= c;
                flat += (int64_t)idx * stride;
                stride *= shape[j];
            }
            if (size < 4)
            {
                tinybuf_free(buf);
                tinybuf_free(shape);
                tinybuf_free(tensor);
                return 0;
            }
            uint32_t raw = load_be32(ptr);
            float fv = 0;
            memcpy(&fv, &raw, 4);
            ptr += 4;
            size -= 4;
            buf[flat] = fv;
        }
        tensor->data = buf;
    }
    else if (tensor->dtype == 11)
    {
        uint8_t *buf = (uint8_t *)tinybuf_malloc((int)prod);
        memset(buf, 0, (size_t)prod);
        for (ii = 0; ii < k; ++ii)
        {
            int64_t stride = 1;
            int64_t flat = 0;
            for (j = (int)dims - 1; j >= 0; --j)
            {
                uint64_t idx = 0;
                int c = int_deserialize((uint8_t *)ptr, size, &idx);
                if (c <= 0)
                {
                    tinybuf_free(buf);
                    tinybuf_free(shape);
                    tinybuf_free(tensor);
                    return c;
                }
                ptr += c;
                size -= c;
                flat += (int64_t)idx * stride;
                stride *= shape[j];
            }
            if (size < 1)
            {
                tinybuf_free(buf);
                tinybuf_free(shape);
                tinybuf_free(tensor);
                return 0;
            }
            serialize_type tt = (serialize_type)ptr[0];
            ptr += 1;
            size -= 1;
            uint64_t v = 0;
            int c = int_deserialize((uint8_t *)ptr, size, &v);
            if (c <= 0)
            {
                tinybuf_free(buf);
                tinybuf_free(shape);
                tinybuf_free(tensor);
                return c;
            }
            ptr += c;
            size -= c;
            buf[flat] = (uint8_t)(v ? 1 : 0);
        }
        tensor->data = buf;
    }
    else
    {
        int64_t *buf = (int64_t *)tinybuf_malloc((int)(prod * (int64_t)sizeof(int64_t)));
        memset(buf, 0, (size_t)(prod * (int64_t)sizeof(int64_t)));
        for (ii = 0; ii < k; ++ii)
        {
            int64_t stride = 1;
            int64_t flat = 0;
            for (j = (int)dims - 1; j >= 0; --j)
            {
                uint64_t idx = 0;
                int c = int_deserialize((uint8_t *)ptr, size, &idx);
                if (c <= 0)
                {
                    tinybuf_free(buf);
                    tinybuf_free(shape);
                    tinybuf_free(tensor);
                    return c;
                }
                ptr += c;
                size -= c;
                flat += (int64_t)idx * stride;
                stride *= shape[j];
            }
            if (size < 1)
            {
                tinybuf_free(buf);
                tinybuf_free(shape);
                tinybuf_free(tensor);
                return 0;
            }
            serialize_type tt = (serialize_type)ptr[0];
            ptr += 1;
            size -= 1;
            uint64_t v = 0;
            int c = int_deserialize((uint8_t *)ptr, size, &v);
            if (c <= 0)
            {
                tinybuf_free(buf);
                tinybuf_free(shape);
                tinybuf_free(tensor);
                return c;
            }
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
static int tinybuf_value_deserialize_impl(const char *ptr, int size, tinybuf_value *out)
{
    assert(out);
    assert(ptr);
    assert(out->_type == tinybuf_null);
    if (size < 1)
    {
        // 数据不够
        return 0;
    }

    uint64_t cnt = 0, dt = 0, dims = 0, k = 0;
    int a = 0, b = 0, e = 0;
    int64_t count = 0, prod = 1;
    int64_t *shape = NULL;
    tinybuf_tensor_t *tnsr = NULL;

    serialize_type type = ptr[0];
    // 剩余字节数
    --size;
    // 指针偏移量
    ++ptr;
    // 消耗的字节数
    switch (type)
    {
    // null类型
    case serialize_null:
    {
        tinybuf_value_clear(out);
        // 消费了一个字节
        return 1;
    }

    // int类型
    case serialize_negtive_int:
    case serialize_positive_int:
        return optional_add(try_read_int(type, ptr, size, out), 1);
    // bool类型
    case serialize_bool_false:
    case serialize_bool_true:
    {
        tinybuf_value_init_bool(out, type == serialize_bool_true);
        // 消费了一个字节
        return 1;
    }

    case serialize_double:
    {
        if (size < 8)
        {
            // 数据不够
            return 0;
        }
        tinybuf_value_init_double(out, read_double((uint8_t *)ptr));
        return 9;
    }

    case serialize_string:
    {
        // 字节长度
        uint64_t bytes_len;
        int len = int_deserialize((uint8_t *)ptr, size, &bytes_len);
        if (len <= 0)
        {
            // 字节不够，反序列化失败
            return len;
        }

        // 跳过字节长度
        ptr += len;
        size -= len;

        if (size < bytes_len)
        {
            // 剩余字节不够
            return 0;
        }

        // 赋值为string
        tinybuf_value_init_string(out, ptr, bytes_len);

        // 消耗总字节数
        return 1 + len + bytes_len;
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
        int64_t r = ((const char *)ptr + size) - pool_start; // remaining after pool start
        if (r < 1)
        {
            return 0;
        }
        if ((uint8_t)q[0] == serialize_str_pool)
        {
            ++q;
            --r;
            uint64_t cnt = 0;
            int l2 = int_deserialize((uint8_t *)q, (int)r, &cnt);
            if (l2 <= 0)
            {
                return -1;
            }
            q += l2;
            r -= l2;
            if (idx >= cnt)
            {
                return -1;
            }
            for (uint64_t i = 0; i < cnt; ++i)
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
            /* find leaf node with leaf_id == idx */
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
            /* reconstruct path up to root by scanning nodes repeatedly to find parents */
            buffer *tmp = buffer_alloc();
            int current = found;
            while (1)
            { /* scan to get node 'current' fields */
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
            /* reverse tmp into output string */
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
        // 获取map_size
        int len = int_deserialize((uint8_t *)ptr, size, &map_size);
        if (len <= 0)
        {
            // 字节不够，反序列化失败
            return len;
        }

        // 跳过map size字段
        ptr += len;
        size -= len;
        // 消耗的字节总数
        consumed = 1 + len;

        out->_type = tinybuf_map;
        int i;
        for (i = 0; i < map_size; ++i)
        {
            uint64_t key_len;
            // 获取key string长度
            len = int_deserialize((uint8_t *)ptr, size, &key_len);
            if (len <= 0)
            {
                // 字节不够，反序列化失败
                // 清空掉未解析完毕的map
                tinybuf_value_clear(out);
                return len;
            }

            // 跳过key string长度部分
            ptr += len;
            size -= len;
            consumed += len;

            if (size < key_len)
            {
                // 剩余字节不够
                // 清空掉未解析完毕的map
                tinybuf_value_clear(out);
                return 0;
            }

            char *key_ptr = (char *)ptr;

            // 跳过string内容部分
            ptr += key_len;
            size -= key_len;
            consumed += key_len;

            // 解析value部分
            tinybuf_value *value = tinybuf_value_alloc();
            int value_len = tinybuf_value_deserialize(ptr, size, value);
            if (value_len <= 0)
            {
                // 数据不够解析value部分
                tinybuf_value_free(value);
                // 清空掉未解析完毕的map
                tinybuf_value_clear(out);
                return value_len;
            }

            {
                // 解析出key，value,保存到map
                buffer *key = buffer_alloc();
                buffer_assign(key, key_ptr, key_len);
                tinybuf_value_map_set2(out, key, value);
            }

            // 重新计算偏移量
            ptr += value_len;
            size -= value_len;
            consumed += value_len;
        }
        // 消耗的总字节数
        return consumed;
    }

    case serialize_array:
    {
        int consumed = 0;
        uint64_t array_size;
        // 获取array_size
        int len = int_deserialize((uint8_t *)ptr, size, &array_size);
        if (len <= 0)
        {
            // 字节不够，反序列化失败
            return len;
        }

        // 跳过array_size字段
        ptr += len;
        size -= len;
        // 消耗的字节总数
        consumed = 1 + len;

        out->_type = tinybuf_array;

        int i;
        for (i = 0; i < array_size; ++i)
        {
            // 解析value部分
            tinybuf_value *value = tinybuf_value_alloc();
            int value_len = tinybuf_value_deserialize(ptr, size, value);
            if (value_len <= 0)
            {
                // 数据不够解析value部分
                tinybuf_value_free(value);
                // 清空掉未解析完毕的array
                tinybuf_value_clear(out);
                return value_len;
            }
            // 解析出value,保存到array
            tinybuf_value_array_append(out, value);
            // 重新计算偏移量
            ptr += value_len;
            size -= value_len;
            consumed += value_len;
        }
        // 消耗的总字节数
        return consumed;
    }
    break;
    case serialize_vector_tensor:
    {
        return tinybuf_deserialize_vector_tensor(ptr, size, out);
    }
    case serialize_dense_tensor:
    {
        return tinybuf_deserialize_dense_tensor(ptr, size, out);
    }
    case serialize_sparse_tensor:
    {
        return tinybuf_deserialize_sparse_tensor(ptr, size, out);
    }
    case serialize_bool_map:
    {
        uint64_t cnt = 0;
        int a = int_deserialize((uint8_t *)ptr, size, &cnt);
        if (a <= 0)
            return a;
        ptr += a;
        size -= a;
        int64_t bytes = ((int64_t)cnt + 7) / 8;
        if (size < bytes)
            return 0;
        tinybuf_bool_map_t *bm = (tinybuf_bool_map_t *)tinybuf_malloc((int)sizeof(tinybuf_bool_map_t));
        bm->count = (int64_t)cnt;
        bm->bits = (uint8_t *)tinybuf_malloc((int)bytes);
        memcpy(bm->bits, ptr, (size_t)bytes);
        out->_type = tinybuf_bool_map;
        out->_data._custom = bm;
        out->_custom_free = NULL;
        return 1 + a + (int)bytes;
    }
    case serialize_indexed_tensor:
    {
        const char *p0 = ptr;
        tinybuf_value *ten = tinybuf_value_alloc();
        int r1 = tinybuf_value_deserialize(ptr, size, ten);
        if (r1 <= 0)
        {
            /* fallback to public wrapper to ensure strpool context */
            buf_ref br_fallback = (buf_ref){(char *)ptr, (int64_t)size, (char *)ptr, (int64_t)size};
            tinybuf_result r1b = tinybuf_try_read_box(&br_fallback, ten, contain_any);
            if (r1b.res <= 0)
            {
                tinybuf_value_free(ten);
                return r1;
            }
            r1 = r1b.res;
        }
        ptr += r1;
        size -= r1;
        uint64_t dims = 0;
        int a = int_deserialize((uint8_t *)ptr, size, &dims);
        if (a <= 0)
        {
            tinybuf_value_free(ten);
            return a;
        }
        ptr += a;
        size -= a;
        tinybuf_indexed_tensor_t *it = (tinybuf_indexed_tensor_t *)tinybuf_malloc((int)sizeof(tinybuf_indexed_tensor_t));
        it->tensor = ten;
        it->dims = (int)dims;
        it->indices = NULL;
        if (dims > 0)
        {
            it->indices = (tinybuf_value **)tinybuf_malloc(sizeof(tinybuf_value *) * (size_t)dims);
            for (uint64_t i = 0; i < dims; ++i)
                it->indices[i] = NULL;
        }
        for (uint64_t i = 0; i < dims; ++i)
        {
            uint64_t has = 0;
            int c = int_deserialize((uint8_t *)ptr, size, &has);
            if (c <= 0)
            {
                if (it->indices)
                {
                    for (uint64_t k = 0; k < i; ++k)
                    {
                        if (it->indices[k])
                            tinybuf_value_free(it->indices[k]);
                    }
                    tinybuf_free(it->indices);
                }
                tinybuf_value_free(ten);
                tinybuf_free(it);
                return c;
            }
            ptr += c;
            size -= c;
            if (has)
            {
                tinybuf_value *idx = tinybuf_value_alloc();
                int r2 = tinybuf_value_deserialize(ptr, size, idx);
                if (r2 <= 0)
                {
                    buf_ref br2 = (buf_ref){(char *)ptr, (int64_t)size, (char *)ptr, (int64_t)size};
                    tinybuf_result r3 = tinybuf_try_read_box(&br2, idx, contain_any);
                    if (r3.res <= 0)
                    {
                        tinybuf_value_free(idx);
                        if (it->indices)
                        {
                            for (uint64_t k = 0; k < i; ++k)
                            {
                                if (it->indices[k])
                                    tinybuf_value_free(it->indices[k]);
                            }
                            tinybuf_free(it->indices);
                        }
                        tinybuf_value_free(ten);
                        tinybuf_free(it);
                        return r2;
                    }
                    r2 = r3.res;
                }
                ptr += r2;
                size -= r2;
                it->indices[i] = idx;
            }
        }
        out->_type = tinybuf_indexed_tensor;
        out->_data._custom = it;
        out->_custom_free = NULL;
        return 1 + (int)(ptr - p0);
    }
    case serialize_type_idx:
    {
        uint64_t idx = 0;
        int a = int_deserialize((uint8_t *)ptr, size, &idx);
        if (a <= 0)
            return a;
        ptr += a;
        size -= a;
        uint64_t blen = 0;
        int b = int_deserialize((uint8_t *)ptr, size, &blen);
        if (b <= 0)
            return b;
        ptr += b;
        size -= b;
        if (size < (int64_t)blen)
            return 0;
        if (s_strpool_offset_read < 0 || s_strpool_base_read == NULL)
        {
            s_last_error_msg = "strpool not initialized";
            return -1;
        }
        const char *pool_start = s_strpool_base_read + s_strpool_offset_read;
        const char *q = pool_start;
        int64_t r = ((const char *)ptr + size) - pool_start;
        if (r < 1)
            return 0;
        char *name_out = NULL;
        int name_len = 0;
        if ((uint8_t)q[0] == serialize_str_pool)
        {
            ++q;
            --r;
            uint64_t cnt = 0;
            int l2 = int_deserialize((uint8_t *)q, (int)r, &cnt);
            if (l2 <= 0)
                return l2;
            q += l2;
            r -= l2;
            if (idx >= cnt)
                return -1;
            for (uint64_t i = 0; i < cnt; ++i)
            {
                if (r < 1)
                    return 0;
                if ((uint8_t)q[0] != serialize_string)
                    return -1;
                ++q;
                --r;
                uint64_t sl = 0;
                int l3 = int_deserialize((uint8_t *)q, (int)r, &sl);
                if (l3 <= 0)
                    return l3;
                q += l3;
                r -= l3;
                if (r < (int64_t)sl)
                    return 0;
                if (i == idx)
                {
                    name_out = (char *)tinybuf_malloc((int)sl + 1);
                    memcpy(name_out, q, (size_t)sl);
                    name_out[sl] = '\0';
                    name_len = (int)sl;
                    break;
                }
                q += sl;
                r -= sl;
            }
        }
        else if ((uint8_t)q[0] == 27)
        {
            ++q;
            --r;
            uint64_t ncount = 0;
            int l2 = int_deserialize((uint8_t *)q, (int)r, &ncount);
            if (l2 <= 0)
                return l2;
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
                    if (leaf == (uint64_t)idx)
                    {
                        found = (int)i;
                        break;
                    }
                }
            }
            if (found < 0)
                return -1;
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
                    break;
                current = parent_index;
            }
            int tlen = buffer_get_length_inline(tmp);
            const char *td = buffer_get_data_inline(tmp);
            name_out = (char *)tinybuf_malloc(tlen + 1);
            for (int i = 0; i < tlen; ++i)
            {
                name_out[i] = td[tlen - 1 - i];
            }
            name_out[tlen] = '\0';
            name_len = tlen;
            buffer_free(tmp);
        }
        else
        {
            return -1;
        }
        if (!name_out)
            return -1;
        tinybuf_result rr = tinybuf_custom_try_read(name_out, (const uint8_t *)ptr, (int)blen, out, contain_any);
        tinybuf_free(name_out);
        if (rr.res <= 0)
        {
            buf_ref br3 = (buf_ref){(char *)ptr, (int64_t)blen, (char *)ptr, (int64_t)blen};
            int ir = try_read_box(&br3, out, contain_any);
            if (ir > 0)
            {
                return 1 + a + b + ir;
            }
            return rr.res;
        }
        return 1 + a + b + (int)blen;
    }
    default:
        s_last_error_msg = "deserialize type unknown";
        return -1;
    }
}

//! --------------------------高级反序列化系列--------------

//>0 表示成功 0表示无法读取（看作失败） 负数表示失败
#define OK(x) x > 0
#define OK_AND_SAVE(x, s) ((s = x) > 0)
// 必须保证s>=0 成功则add 0或失败则不add
inline int OK_AND_ADDTO(int x, int *s)
{
    if (x > 0)
    {
        *s += x;
        return 1;
    }
    return 0;
}

// 从指针位置开始读取box 提供开始位置+偏移
// 返回值表示消耗的字节数

typedef tinybuf_value value;

//---util function s
// 求buf当前偏移
BOOL validate_buf(buf_ref *buf)
{
    return buf->base <= buf->ptr &&
           buf->size <= buf->all_size &&
           buf->all_size + buf->base == buf->ptr + buf->size;
}

// ptr是否在范围内
BOOL buf_ptr_ok(buf_ref *buf)
{
    return buf->ptr >= buf->base && buf->ptr < buf->base + buf->all_size;
}

// 开启严格缓冲区安全验证
#define ENABLE_STRICT_VALIDATE
inline void maybe_validate(buf_ref *buf)
{
#ifdef ENABLE_STRICT_VALIDATE
    assert(validate_buf(buf));
#endif
}

// 执行缓冲区当前指针偏移 返回0 表示成功

//------------utils结束------

// 所有read函数 返回-1表示失败 否则返回消耗的字节数返回0表示数据不够 负数表示具体错误

//--读取一个type 标记

// 指针偏移系统：types are declared in tinybuf_private.h

// 转换pointervalue为从start开始的pointervalue
// 直接修改ptr指针对象

// 支持带错误信息的返回
typedef struct
{
    int len;
    const char *reason;
} read_result;
#define RESULT_OK(x) (x.len > 0)
//--等效ok系列
BOOL RESULT_OK_AND_ADDTO(read_result x, int *s)
{
    if (RESULT_OK(x))
    {
        *s += x.len;
        return TRUE;
    }
    return FALSE;
}

// 高级序列化read入口
// 二级版本 可处理二级格式 readbox标准 成功则修改buf指针 返回>0 否则不修改并返回<=0

#include "tinybuf_plugin.h"
void _tb_push_err_msg(const char *msg)
{
    tinybuf_result *cr = tinybuf_result_get_current();
    if (cr && msg)
    {
        tinybuf_result_add_msg_const(cr, msg);
    }
}

//------------- pointer->object pool for cyclic structures -------------
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

const char *tinybuf_last_error_message(void) { return s_last_error_msg; }

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
    // 输出引用目标（不拷贝）
    out->_type = tinybuf_value_ref;
    out->_data._ref = target;
}
static inline void set_out_deref(tinybuf_value *out, const tinybuf_value *target)
{
    // 输出解引用（拷贝值）
    tinybuf_value *clone = tinybuf_value_clone(target);
    tinybuf_value_clear(out);
    memcpy(out, clone, sizeof(tinybuf_value));
    tinybuf_free(clone);
}
static inline void set_out_by_mode(tinybuf_value *out, tinybuf_value *target, int deref)
{
    // 根据模式设置输出
    if (deref)
        set_out_deref(out, target);
    else
        set_out_ref(out, target);
}



int try_write_type(buffer *out, serialize_type type)
{
    buffer_append(out, (char *)&type, 1);
    return 1;
}

int try_write_int_data(BOOL isneg, buffer *out, QWORD val)
{
    return dump_int(val, out);
}

 

static inline serialize_type make_pointer_type(enum offset_type t, BOOL neg)
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

static inline int try_write_pointer_value(buffer *out, enum offset_type t, SQWORD offset)
{
    BOOL neg = offset < 0;
    serialize_type pt = make_pointer_type(t, neg);
    int len = 0;
    len += try_write_type(out, pt);
    QWORD mag = neg ? (QWORD)(-offset) : (QWORD)offset;
    len += try_write_int_data(FALSE, out, mag);
    return len;
}

static inline int try_write_pointer(buffer *out, pointer_value pointer)
{
    return try_write_pointer_value(out, pointer.type, pointer.offset);
}

// 写入总入口由 tinybuf_write.c 提供实现
 

// public wrappers
void tinybuf_set_read_pointer_mode(tinybuf_read_pointer_mode mode)
{
    s_read_pointer_mode = mode;
}

void tinybuf_set_use_strpool(int enable)
{
    s_use_strpool = enable ? 1 : 0;
}

static inline tinybuf_result _err_with(const char *msg, int rc)
{
    return tinybuf_result_err(rc, msg, NULL);
}


static int avl_tree_for_each_node_is_same(void *user_data, AVLTreeNode *node)
{
    AVLTree *tree2 = (AVLTree *)user_data;
    AVLTreeKey key = avl_tree_node_key(node);
    tinybuf_value *child1 = (tinybuf_value *)avl_tree_node_value(node);
    tinybuf_value *child2 = avl_tree_lookup(tree2, key);
    if (!child2)
    {
        // 不存在相应的key
        return 1;
    }
    if (!tinybuf_value_is_same(child1, child2))
    {
        // 相同key下，value不一致
        return 1;
    }

    // 继续遍历
    return 0;
}

/////////////////////////////读接口//////////////////////////////////

static inline int try_write_pointer_value(buffer *out, enum offset_type t, SQWORD offset);

static int contain_any(uint64_t v)
{
    (void)v;
    return 1;
}
