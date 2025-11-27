#include "tinybuf_private.h"
#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

// 1 支持变长数字格式并支持配置开启 2 支持KVPair通用格式
// 3 支持自描述结构和向前兼容支持
// int类型包括1到8字节整数 包括有符号无符号 且以无符号方式存储
typedef int64_t ssize;
typedef uint64_t usize;

//--
typedef enum
{
    serialize_null = 0,
    serialize_positive_int = 1,
    serialize_negtive_int = 2,
    serialize_bool_true = 3,
    serialize_bool_false = 4,
    serialize_double = 5,
    serialize_string = 6,
    serialize_map = 7,
    serialize_array = 8,
    //--额外序列化类型 支持自偏移与头尾偏移 最小化指针int体积
    // p表示正偏移 n表示负偏 后跟变长整数 非完整databox
    serialize_pointer_from_current_n = 9,
    serialize_pointer_from_start_n = 10,
    serialize_pointer_from_end_n = 11,
    serialize_pointer_from_current_p = 12,
    serialize_pointer_from_start_p = 13,
    serialize_pointer_from_end_p = 14,
    // 支持精准锁定环结构 递归结构后面跟databox
    serialize_pre_cache = 15,
    // 支持版本号标记 向前向后兼容 此标记表示后面是一个verion unit
    // 使用一个变长整数表示 递归结构 非完整databox 变长整数后面是完整的databox
    serialize_version = 16,
    // 集束box标识 表示后面是一个异类型序列集合 递归结构  读取时批量读取
    serialize_boxlist = 17, // boxlist_sign lstlen type1 type2 type3 type4 ... data1 data2 data3
    // 压缩树表示中的keysign使用变长int存储
    serialize_zip_kvpairs = 18,       // 压缩树表示 不存储key 转而存储key 的keysign //需要msgpack的key(0)等标记
    serialize_zip_kvpairs_boxkey = 19 // 压缩树表示 但key使用完整databox存储 这意味着key支持version或pointer等结构
    // 任何时候value都是完整box存储
} serialize_type;
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

//-----------------------
//--变长整数支持
/**
 * 序列化整型
 * @param in 整型
 * @param out 序列化字节存放地址，最少预留10个字节
 * @return 序列化字节长度
 */
static inline int int_serialize(uint64_t in, uint8_t *out)
{
    int index = 0;
    int i;
    for (i = 0; i <= (8 * sizeof(in)) / 7; ++i, ++index)
    {
        // 取最低位7bit
        out[index] = in & 0x7F;
        // 右移7位
        in >>= 7;
        if (!in)
        {
            // 剩余字节为0，最高位bit为0，停止序列化
            break;
        }
        // 后面还有字节,那么最高位bit为1
        out[index] |= 0x80;
    }
    // 返回序列化后总字节数
    return ++index;
}

/**
 * 反序列化整型
 * @param in 输入字节流
 * @param in_size 输入字节数
 * @param out 序列化后的整型
 * @return 代表反序列化消耗的字节数
 */
static inline int int_deserialize(const uint8_t *in, int in_size, uint64_t *out)
{
    if (in_size < 1)
    {
        return 0;
    }
    *out = 0;
    int index = 0;
    while (1)
    {
        uint8_t byte = in[index];
        (*out) |= (byte & 0x7F) << ((index++) * 7);
        if ((byte & 0x80) == 0)
        {
            // 最高位为0，所以没有更多后续字节
            break;
        }
        // 后续还有字节
        if (index >= in_size)
        {
            // 字节不够，反序列失败
            return 0;
        }
        if (index * 7 > 56)
        {
            // 7bit 最多左移动56位，最大支持64位有符号整形
            return -1;
        }
    }
    // 序列号成功
    return index;
}

tinybuf_value *tinybuf_value_alloc(void)
{
    tinybuf_value *ret = tinybuf_malloc(sizeof(tinybuf_value));
    assert(ret);
    // 置0
    memset(ret, 0, sizeof(tinybuf_value));
    ret->_type = tinybuf_null;
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

int tinybuf_value_clear(tinybuf_value *value)
{
    assert(value);
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

    if (value->_type != tinybuf_null)
    {
        // 清空所有
        memset(value, 0, sizeof(tinybuf_value));
        value->_type = tinybuf_null;
    }
    return 0;
}

int tinybuf_value_init_bool(tinybuf_value *value, int flag)
{
    assert(value);
    tinybuf_value_clear(value);
    value->_type = tinybuf_bool;
    value->_data._bool = (flag != 0);
    return 0;
}

int tinybuf_value_init_int(tinybuf_value *value, int64_t int_val)
{
    assert(value);
    tinybuf_value_clear(value);
    value->_type = tinybuf_int;
    value->_data._int = int_val;
    return 0;
}

// 初始化整数like box 使用特定类型
int tinybuf_value_init_intlike_with_type(tinybuf_value *value, tinybuf_type type, int64_t int_val)
{
    int ret = tinybuf_value_init_int(value, int_val);
    if (ret != 0)
    {
        return ret;
    }
    value->_type = type;
    return 0;
}
// 直接设置类型 不修改数据
int tinybuf_value_set_type(tinybuf_value *value, tinybuf_type type)
{
    assert(value);
    value->_type = type;
    return 0;
}

int tinybuf_value_init_double(tinybuf_value *value, double db_val)
{
    assert(value);
    tinybuf_value_clear(value);
    value->_type = tinybuf_double;
    value->_data._double = db_val;
    return 0;
}

int tinybuf_value_init_string2(tinybuf_value *value, buffer *buf)
{
    assert(value);
    assert(buf);
    if (value->_type != tinybuf_string)
    {
        tinybuf_value_clear(value);
    }

    if (value->_data._string)
    {
        buffer_free(value->_data._string);
    }
    value->_type = tinybuf_string;
    value->_data._string = buf;
    return 0;
}

static inline int tinybuf_value_init_string_l(tinybuf_value *value, const char *str, int len, int use_strlen)
{
    assert(value);
    assert(str);
    if (value->_type != tinybuf_string)
    {
        tinybuf_value_clear(value);
    }

    if (!value->_data._string)
    {
        value->_data._string = buffer_alloc();
        assert(value->_data._string);
    }

    value->_type = tinybuf_string;

    if (len <= 0 && use_strlen)
    {
        len = strlen(str);
    }

    if (len > 0)
    {
        buffer_assign(value->_data._string, str, len);
    }
    else
    {
        buffer_set_length(value->_data._string, 0);
    }
    return 0;
}

int tinybuf_value_init_string(tinybuf_value *value, const char *str, int len)
{
    return tinybuf_value_init_string_l(value, str, len, 1);
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
    if (parent->_type != tinybuf_map)
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

static inline int dump_int(uint64_t len, buffer *out)
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

static inline int dump_double(double db, buffer *out)
{
    uint64_t encoded = 0;
    memcpy(&encoded, &db, 8);
    uint32_t val = htonl(encoded >> 32);
    buffer_append(out, (char *)&val, 4);
    val = htonl(encoded & 0xFFFFFFFF);
    buffer_append(out, (char *)&val, 4);
    return 8;
}

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

static inline int dump_string(int len, const char *str, buffer *out)
{
    int ret = dump_int(len, out);
    buffer_append(out, str, len);
    return ret + len;
}

static int avl_tree_for_each_node_dump_map(void *user_data, AVLTreeNode *node)
{
    buffer *out = (buffer *)user_data;
    buffer *key = (buffer *)avl_tree_node_key(node);
    tinybuf_value *val = (tinybuf_value *)avl_tree_node_value(node);
    // 写key
    dump_string(buffer_get_length_inline(key), buffer_get_data_inline(key), out);
    // 写value
    tinybuf_value_serialize(val, out);
    // 继续遍历
    return 0;
}

static int avl_tree_for_each_node_dump_array(void *user_data, AVLTreeNode *node)
{
    buffer *out = (buffer *)user_data;
    // 写value
    tinybuf_value_serialize(avl_tree_node_value(node), out);
    return 0;
}

int tinybuf_value_serialize(const tinybuf_value *value, buffer *out)
{
    assert(value);
    assert(out);
    switch (value->_type)
    {
    case tinybuf_null:
    {
        char type = serialize_null;
        // null类型
        buffer_append(out, &type, 1);
    }
    break;

    case tinybuf_int:
    {
        char type = value->_data._int > 0 ? serialize_positive_int : serialize_negtive_int;
        // 正整数或负整数类型
        buffer_append(out, &type, 1);
        // 后面是int序列化字节
        dump_int(value->_data._int > 0 ? value->_data._int : -value->_data._int, out);
    }
    break;

    case tinybuf_bool:
    {
        char type = value->_data._bool ? serialize_bool_true : serialize_bool_false;
        // bool类型
        buffer_append(out, &type, 1);
    }
    break;

    case tinybuf_double:
    {
        char type = serialize_double;
        // double类型
        buffer_append(out, &type, 1);
        // double值序列化
        dump_double(value->_data._double, out);
    }
    break;

    case tinybuf_string:
    {
        char type = serialize_string;
        // string或bytes类型
        buffer_append(out, &type, 1);
        int len = buffer_get_length_inline(value->_data._string);
        if (len)
        {
            dump_string(len, buffer_get_data_inline(value->_data._string), out);
        }
        else
        {
            dump_int(0, out);
        }
    }
    break;

    case tinybuf_map:
    {
        char type = serialize_map;
        // map类型
        buffer_append(out, &type, 1);
        int map_size = avl_tree_num_entries(value->_data._map_array);
        // map size
        dump_int(map_size, out);
        avl_tree_for_each_node(value->_data._map_array, out, avl_tree_for_each_node_dump_map);
    }
    break;

    case tinybuf_array:
    {
        char type = serialize_array;
        // array类型
        buffer_append(out, &type, 1);
        int array_size = avl_tree_num_entries(value->_data._map_array);
        // array size
        dump_int(array_size, out);
        // 遍历node并序列化
        avl_tree_for_each_node(value->_data._map_array, out, avl_tree_for_each_node_dump_array);
    }
    break;
    default:
        // 不可达
        assert(0);
        break;
    }

    return 0;
}

typedef uint64_t QWORD;
typedef int64_t SQWORD;

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

// 可用于传递错误码 如果无错误 则增加addx后返回
inline int optional_add(int x, int addx)
{
    if (x < 0)
        return x;
    return x + addx;
}

// 支持自定义描述序列支持的反序列化操作
// 描述子是一串 serialize_type 类型的字节序列
// 用于支持集中类型表示 集中类型表示可优化内存与寄存器
//  int read_box_by_descriptor(const  char* ptr,int size,const serialize_type* desc,int desclen){

// }

//--核心反序列化路由函数 初级版本 只能处理纯初级格式 内部不能嵌套二级box
int tinybuf_value_deserialize(const char *ptr, int size, tinybuf_value *out)
{
    assert(out);
    assert(ptr);
    assert(out->_type == tinybuf_null);
    if (size < 1)
    {
        // 数据不够
        return 0;
    }

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
        tinybuf_value_init_string_l(out, ptr, bytes_len, 0);

        // 消耗总字节数
        return 1 + len + bytes_len;
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
    default:
        // 解析失败
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

// 入口
int try_read_box(buf_ref *buf, tinybuf_value *out);
typedef tinybuf_value value;

typedef struct
{
    // 缓冲区基址
    const char *base;

    // 缓冲区总大小
    int64_t all_size;

    // 缓冲区当前位置指针
    const char *ptr;
    // 剩余缓冲区大小
    int64_t size;
} buf_ref;
//---util function s
const char *buf_end_ptr(buf_ref *buf)
{
    return buf->base + buf->all_size;
}
// 求buf当前偏移
int64_t buf_current_offset(buf_ref *buf)
{
    return buf->ptr - buf->base;
}
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

//------------utils结束------

// 所有read函数 返回-1表示失败 否则返回消耗的字节数返回0表示数据不够 负数表示具体错误

//--读取一个type 标记
int try_read_type(buf_ref *buf, serialize_type *type)
{
    if (buf->size < 1)
    {
        return 0;
    }
    *type = buf->ptr[0];
    buf_offset(buf, 1);
    return 1;
}

// 指针偏移系统
enum offset_type
{
    start,
    end,
    current
};
typedef struct
{
    int64_t offset;
    enum offset_type type;
} pointer_value;

// 转换pointervalue为从start开始的pointervalue
// 直接修改ptr指针对象
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
        // 错误分支
        break;
    }
    maybe_validate(buf);
}

// 从给定位置偏移一段距离 读取值
int _read_box_by_offset(const char *ptr, int size, int64_t offset, value *out)
{
    // tinybuf_value *out = NULL;
    int len = try_read_box(ptr + offset, size - offset, out);
    if (len <= 0)
    {
        return len; // 不初始化value
    }
    // 成功反序列化 消耗的字节数为len
    return len;
}

// 从指针位置开始读取box 提供开始位置+偏移 返回消耗的字节数 错误返回-1
int read_box_by_pointer(buf_ref *buf, pointer_value pointer, tinybuf_value *out)
{
    // 转化指针
    pointer_to_start(buf, &pointer);
    // 确保指针类型为start
    assert(pointer.type == start);
    int len = _read_box_by_offset(buf->base, buf->all_size, pointer.offset, out);
    return len;
}

typedef BOOL (*CONTAIN_HANDLER)(QWORD);
// 高级序列化read入口
// 二级版本 可处理二级格式 readbox标准 成功则修改buf指针 返回>0 否则不修改并返回<=0

#define SET_FAILED(s) (reason = s, failed = TRUE)
#define SET_SUCCESS() (failed = FALSE, reason = NULL)
#define CHECK_FAILED (failed && buf_offset(buf, -len));
#define INIT_STATE       \
    int len = 0;         \
    BOOL failed = FALSE; \
    const char *reason = NULL;
#define READ_RETURN
inline int try_read_int_data(BOOL isneg, buf_ref *buf, QWORD *out)
{
    INIT_STATE
    int temp = 0;
    if (OK_AND_ADDTO(try_read_int_tovar(isneg, buf->ptr, buf->size, out), &temp))
    {
        //适配非标准read函数
        len += temp;
        buf_offset(buf,temp);
    }
    else
        SET_FAILED("read data error");

    CHECK_FAILED
    READ_RETURN
}
// 读取一个标准的变长整数box
inline int try_read_intbox(buf_ref *buf, QWORD *saveptr)
{
    serialize_type type;
    INIT_STATE
    if (OK_AND_ADDTO(try_read_type(buf, type), &len))
    {
        if (type == serialize_positive_int || type == serialize_negtive_int)
        {
            BOOL isneg = type == serialize_negtive_int;
            if (OK_AND_ADDTO(try_read_int_tovar(isneg, buf->ptr, buf->size, saveptr), &len))
            {
                SET_SUCCESS();
            }
            else
                SET_FAILED("read int error");
        }
        else
            SET_FAILED("type error");
    }
    else
        SET_FAILED("read type error");
    CHECK_FAILED
}
//---核心box读取函数 当前整数表示 signbyte intdata

int try_read_box(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER target_version)
{
    // 尝试初阶反序列化
    int len = tinybuf_value_deserialize(buf->ptr, buf->size, out);
    BOOL failed = FALSE;
    const char *reason = NULL;

    if (len == 0)
        return len;
    if (len > 0) //=0的情况为缓冲区太小
    {
        // 兼容buf逻辑
        buf->ptr += len;
        buf->size -= len;
        return len;
    }
    // 执行高阶反序列化 支持环数据引用等
    // len表示已经成功消费的字节数
    len = 0;
    // 是否读取失败
    BOOL failed = FALSE;
    const char *reason = NULL;

    serialize_type type = serialize_null;
    if (OK_AND_ADDTO(try_read_type(buf, &type), &len))
    {
        switch (type)
        {
        case serialize_version:
        {
            // version后跟一个裸整数 后跟一个 box
            QWORD version;
            if (OK_AND_ADDTO(try_read_int_tovar(FALSE, buf->ptr, buf->size, &version), &len))
            {
                // 检查version是否正确
                if (target_version(version))
                {
                    if (OK_AND_ADDTO(try_read_box(buf, out, target_version), &len))
                    {
                        SET_SUCCESS();
                        break;
                    }
                    SET_FAILED("read box failed");
                    break;
                }
                SET_FAILED("version error");
                break;
            }
            SET_FAILED("read version failed");
            break;
        }
        case serialize_pointer_from_start_p:
        {
        }
        default:
            break;
        }
    }
    else
    {
        SET_FAILED("read type failed");
    }
    // 如果
    if (failed)
    {
        buf_offset(buf, -len);
    }
}

//------------------------------
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

int tinybuf_value_is_same(const tinybuf_value *value1, const tinybuf_value *value2)
{
    assert(value1);
    assert(value2);
    if (value1 == value2)
    {
        // 是同一个对象
        return 1;
    }
    if (value1->_type != value2->_type)
    {
        // 对象类型不同
        return 0;
    }

    switch (value1->_type)
    {
    case tinybuf_null:
        return 1;
    case tinybuf_int:
        return value1->_data._int == value2->_data._int;
    case tinybuf_bool:
        return value1->_data._bool == value2->_data._bool;
    case tinybuf_double:
    {
        return value1->_data._double == value2->_data._double;
    }
    case tinybuf_string:
    {
        int len1 = buffer_get_length_inline(value1->_data._string);
        int len2 = buffer_get_length_inline(value2->_data._string);
        if (len1 != len2)
        {
            // 长度不一样
            return 0;
        }
        if (!len1)
        {
            // 都是空
            return 1;
        }
        return buffer_is_same(value1->_data._string, value2->_data._string);
    }

    case tinybuf_array:
    case tinybuf_map:
    {
        int map_size1 = avl_tree_num_entries(value1->_data._map_array);
        int map_size2 = avl_tree_num_entries(value2->_data._map_array);
        if (map_size1 != map_size2)
        {
            // 元素个数不一致
            return 0;
        }
        if (!map_size1)
        {
            // 都是空
            return 1;
        }
        return avl_tree_for_each_node(value1->_data._map_array, value2->_data._map_array, avl_tree_for_each_node_is_same) == 0;
    }

    default:
        assert(0);
        return 0;
    }
}

static int avl_tree_for_each_node_clone_map(void *user_data, AVLTreeNode *node)
{
    tinybuf_value *ret = (tinybuf_value *)user_data;
    buffer *key = (buffer *)avl_tree_node_key(node);
    tinybuf_value *val = (tinybuf_value *)avl_tree_node_value(node);

    buffer *key_clone = buffer_alloc();
    buffer_append_buffer(key_clone, key);
    tinybuf_value_map_set2(ret, key_clone, tinybuf_value_clone(val));

    // 继续遍历
    return 0;
}

static int avl_tree_for_each_node_clone_array(void *user_data, AVLTreeNode *node)
{
    tinybuf_value_array_append((tinybuf_value *)user_data, tinybuf_value_clone(avl_tree_node_value(node)));
    // 继续遍历
    return 0;
}

tinybuf_value *tinybuf_value_clone(const tinybuf_value *value)
{
    tinybuf_value *ret = tinybuf_value_alloc_with_type(value->_type);
    switch (value->_type)
    {
    case tinybuf_null:
    case tinybuf_int:
    case tinybuf_bool:
    case tinybuf_double:
    {
        memcpy(ret, value, sizeof(tinybuf_value));
        return ret;
    }
    case tinybuf_string:
        if (buffer_get_length_inline(value->_data._string))
        {
            ret->_data._string = buffer_alloc();
            assert(ret->_data._string);
            buffer_append_buffer(ret->_data._string, value->_data._string);
        }
        return ret;

    case tinybuf_map:
    {
        avl_tree_for_each_node(value->_data._map_array, ret, avl_tree_for_each_node_clone_map);
        return ret;
    }

    case tinybuf_array:
    {
        avl_tree_for_each_node(value->_data._map_array, ret, avl_tree_for_each_node_clone_array);
        return ret;
    }
    default:
        assert(0);
        return ret;
    }
}

/////////////////////////////读接口//////////////////////////////////

tinybuf_type tinybuf_value_get_type(const tinybuf_value *value)
{
    if (!value)
    {
        return tinybuf_null;
    }
    return value->_type;
}

int64_t tinybuf_value_get_int(const tinybuf_value *value)
{
    if (!value || value->_type != tinybuf_int)
    {
        return 0;
    }
    return value->_data._int;
}

double tinybuf_value_get_double(const tinybuf_value *value)
{
    if (!value || value->_type != tinybuf_double)
    {
        return 0;
    }
    return value->_data._double;
}

int tinybuf_value_get_bool(const tinybuf_value *value)
{
    if (!value || value->_type != tinybuf_bool)
    {
        return 0;
    }
    return value->_data._bool;
}

buffer *tinybuf_value_get_string(const tinybuf_value *value)
{
    if (!value || value->_type != tinybuf_string)
    {
        return 0;
    }
    return value->_data._string;
}

int tinybuf_value_get_child_size(const tinybuf_value *value)
{
    if (!value || (value->_type != tinybuf_map && value->_type != tinybuf_array))
    {
        return 0;
    }
    return avl_tree_num_entries(value->_data._map_array);
}

const tinybuf_value *tinybuf_value_get_array_child(const tinybuf_value *value, int index)
{
    if (!value || value->_type != tinybuf_array || !value->_data._map_array)
    {
        return NULL;
    }
    return (tinybuf_value *)avl_tree_lookup(value->_data._map_array, (AVLTreeKey)index);
}

const tinybuf_value *tinybuf_value_get_map_child(const tinybuf_value *value, const char *key)
{
    return tinybuf_value_get_map_child2(value, key, strlen(key));
}

const tinybuf_value *tinybuf_value_get_map_child2(const tinybuf_value *value, const char *key, int key_len)
{
    if (!value || !key || value->_type != tinybuf_map || !value->_data._map_array)
    {
        return NULL;
    }

    buffer buf;
    buf._data = (char *)key;
    buf._len = key_len;
    buf._capacity = buf._len + 1;

    return (tinybuf_value *)avl_tree_lookup(value->_data._map_array, &buf);
}

const tinybuf_value *tinybuf_value_get_map_child_and_key(const tinybuf_value *value, int index, buffer **key)
{
    if (!value || value->_type != tinybuf_map || !value->_data._map_array)
    {
        return NULL;
    }

    AVLTreeNode *node = avl_tree_get_node_by_index(value->_data._map_array, index);
    if (!node)
    {
        return NULL;
    }

    if (key)
    {
        *key = (buffer *)avl_tree_node_key(node);
    }

    return (tinybuf_value *)avl_tree_node_value(node);
}
