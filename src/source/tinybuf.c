#include "tinybuf_private.h"
#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif
#include <stdbool.h>
#include <stdio.h>

// 1 支持变长数字格式并支持配置开启 2 支持KVPair通用格式
// 3 支持自描述结构和向前兼容支持
// int类型包括1到8字节整数 包括有符号无符号 且以无符号方式存储

// dll扩展支持 支持dll提交 sign支持列表  并提供read与write接口执行基于插件的序列化

typedef int64_t ssize;
typedef uint64_t usize;

static tinybuf_read_pointer_mode s_read_pointer_mode = tinybuf_read_pointer_ref;

typedef uint64_t QWORD;
typedef int64_t SQWORD;

int s_use_strpool = 0;
static int64_t s_strpool_offset_read = -1;
static const char *s_strpool_base_read = NULL;
typedef struct { buffer *buf; } strpool_entry;
static strpool_entry *s_strpool = NULL;
static int s_strpool_count = 0;
static int s_strpool_capacity = 0;
static const char *s_strpool_base = NULL;

 

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

static inline int clear_stack_contains(tinybuf_value *v){
    for(int i=0;i<s_clear_stack_count;++i){
        if(s_clear_stack[i] == v){
            return 1;
        }
    }
    return 0;
}

static inline void clear_stack_push(tinybuf_value *v){
    if(s_clear_stack_count == s_clear_stack_capacity){
        int newcap = s_clear_stack_capacity ? (s_clear_stack_capacity * 2) : 16;
        s_clear_stack = (tinybuf_value **)tinybuf_realloc(s_clear_stack, sizeof(tinybuf_value*) * newcap);
        s_clear_stack_capacity = newcap;
    }
    s_clear_stack[s_clear_stack_count++] = v;
}

static inline void clear_stack_pop(tinybuf_value *v){
    if(s_clear_stack_count == 0){
        return;
    }
    if(s_clear_stack[s_clear_stack_count - 1] == v){
        --s_clear_stack_count;
        return;
    }
    for(int i=0;i<s_clear_stack_count;++i){
        if(s_clear_stack[i] == v){
            for(int j=i+1;j<s_clear_stack_count;++j){
                s_clear_stack[j-1] = s_clear_stack[j];
            }
            --s_clear_stack_count;
            return;
        }
    }
}

// 是否需要执行custom 指针释放
inline bool need_custom_free(const tinybuf_value *value)
{
    return value->_data._custom != NULL && value->_type == tinybuf_custom;
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

//--目前除了指针系列 version与versionlist其他都未实现
typedef enum
{
    serialize_null = 0,                 // 已实现
    serialize_positive_int = 1,         // 已实现
    serialize_negtive_int = 2,          // 已实现
    serialize_bool_true = 3,            // 已实现
    serialize_bool_false = 4,           // 已实现
    serialize_double = 5,               // 已实现
    serialize_string = 6,               // 已实现
    serialize_map = 7,                  // 已实现
    serialize_array = 8,                // 已实现
    //--额外序列化类型 支持自偏移与头尾偏移 最小化指针int体积
    // p表示正偏移 n表示负偏 后跟变长整数 非完整databox
    serialize_pointer_from_current_n = 9, // 已实现
    serialize_pointer_from_start_n = 10,  // 已实现
    serialize_pointer_from_end_n = 11,    // 已实现
    serialize_pointer_from_current_p = 12,// 已实现
    serialize_pointer_from_start_p = 13,  // 已实现
    serialize_pointer_from_end_p = 14,    // 已实现
    // 支持精准锁定环结构 递归结构后面跟databox
    serialize_pre_cache = 15,            // 未实现
    // 支持版本号标记 向前向后兼容 此标记表示后面是一个verion unit
    // 使用一个变长整数表示 递归结构 非完整databox 变长整数后面是完整的databox
    serialize_version = 16,              // 已实现
    // 集束box标识 表示后面是一个异类型序列集合 递归结构  读取时批量读取
    serialize_boxlist = 17, // 未实现 boxlist_sign lstlen type1 type2 type3 type4 ... data1 data2 data3
    // 压缩树表示中的keysign使用变长int存储
    serialize_zip_kvpairs = 18,        // 未实现 压缩树表示 不存储key 转而存储key 的keysign //需要msgpack的key(0)等标记
    serialize_zip_kvpairs_boxkey = 19, // 未实现 压缩树表示 但key使用完整databox存储 这意味着key支持version或pointer等结构
    // 任何时候value都是完整box存储
    // 兼容机制 version列表 支持新版本兼容旧版本

    serialize_version_list = 20,        // 已实现
    // 用于支持多part 每个part就是一个分区 用于支持并发读取每个分区拥有自己的长度
    // 分区指针指向分区头部位置 目标必然是一个part类型 type partlen data 其中 其中partlen为intdata
    serialize_part = 21,                // 未实现
    // 用于支持并发读取
    serialize_part_table = 22,          // 未实现 分区表 后跟一个整数长度 加标准整数列表 非完整box 每个整数表示分区头指针

    // 字符串池索引
    serialize_str_index = 23,           // 已实现
    // 表示一个字符串池 可缩小体积
    serialize_str_pool = 24,            // 已实现（尾部池对象）
    // 用于支持把字符串池放在末尾
    serialize_str_pool_table = 25,      // 已实现（头部池地址表）
    serialize_uri = 26,            // 未实现 可以是文件uri或其他 结构为 sign uritype otherdata 后面跟一个文件path str 再跟一个整数表示数据在文件中的位置
    serialize_router_link = 27,    // 未实现 跟一个uribox
    // 遇到文件链接 应跳转到另一个文件的指定位置继续读取 高层box 对数据透明
    // 与uri的区别是 uri只读取一个value并返回
    serialize_text_ref = 28, // 未实现 uri指向的文本文件引用 fileid 有编码说明
    serialize_bin_ref = 29,  // 未实现 二进制文件引用 支持部分引用 fileid
    serialize_embed_file=30,    // 未实现 内嵌文件 有fileid 文件名 文件mime等 支持strptr和str
    serialize_file_range=31,    // 未实现 文件范围引用 基于已经有的fileid
    serialize_with_metadata=32, // 未实现 元类型 表示一个带有metadata的box 透明
    serialize_noseq_part=33,    // 未实现 表示一个无序区 遇到直接跳过 无序区用于保存孤立对象 这种对象只被某个指针引用 不存在于其他位置
    // 空白区 遇到直接跳过 用于用fat在其中高性能删除已有数据 并留下一个空白区
    serialize_empty_part=34, // 未实现 sign len
    // fs和fstable都支持多分块 元数据里会有指向下一个block和上一个block的指针 用于将space联成一片
    serialize_fs=35,         // 未实现 表示一个文件系统块里面可以有目录结构
    serialize_file_table=36, // 未实现 表示一个文件表 结构简单 没有目录结构
    serialize_fs_file=37,    // 未实现 表示一个文件系统或文件表中的 文件 文件就是具名+具metadata的数据对象
    serialize_fs_inode=38,   // 未实现 表示一个inode 一个fs节点 节点可以是目录 链接 或文件
    serialize_flat_part=39,  // 未实现 通用连续空间块 支持相对寻址 平坦空间支持指向下一个block或上一个block 使用指针类型
    // 可以是普通指针或高级指针
    serialize_pointer_advance=40, // 未实现 先进指针 支持更丰富的寻址方式 例如flat空间寻址 block内部寻址 跨文件寻址等
    serialize_empty_table=41,     // 未实现 空白空间表 用于配合平坦buf实现文件内连续子空间
    // 子引用：后跟一个完整的指针box，读取时强制生成子引用节点（不透明）
    serialize_sub_ref=42,         // 已实现
    // 使用字符串池+索引index来表示额外类型 后跟整数 要求字符串池存在
    // 后面跟的是idx 直接就是idx后面不跟完整databox而是跟index整数
    serialize_extern_str_idx = 253, // 未实现
    // str类型的extern类型表示 占空间较大 后面有一个完整的databox 可以是str或stridx
    serialize_extern_str = 254,     // 未实现
    // 扩展序列化类型 使用此类型 后面会跟一个变长正数（非完整box） 来表示更丰富的数据类型 用于支持插件系统
    serialize_extern_int = 255      // 未实现
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
    buffer* key_buf=buffer_alloc();
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

// 预写（precache）支持：在写侧允许提前写出对象，后续遇到该对象时改为输出指针
typedef struct { const tinybuf_value *value; buffer *stream; int64_t start; } precache_entry;
static precache_entry *s_precache = NULL;
static int s_precache_count = 0;
static int s_precache_capacity = 0;
static buffer *s_precache_stream = NULL;
static int s_precache_redirect = 0; // 当为1时，序列化遇到已注册对象则输出指针而非内容

static inline void precache_reset(buffer *out){ s_precache_stream = out; s_precache_count = 0; }
static inline int64_t precache_find_start(buffer *out, const tinybuf_value *value){
    if(out != s_precache_stream){ return -1; }
    for(int i=0;i<s_precache_count;++i){ if(s_precache[i].value == value){ return s_precache[i].start; } }
    return -1;
}
static inline void precache_add(buffer *out, const tinybuf_value *value, int64_t start){
    if(out != s_precache_stream){ s_precache_stream = out; s_precache_count = 0; }
    for(int i=0;i<s_precache_count;++i){ if(s_precache[i].value == value){ s_precache[i].start = start; return; } }
    if(s_precache_count == s_precache_capacity){ int newcap = s_precache_capacity ? s_precache_capacity*2 : 16; s_precache = (precache_entry*)tinybuf_realloc(s_precache, sizeof(precache_entry)*newcap); s_precache_capacity = newcap; }
    s_precache[s_precache_count].value = value; s_precache[s_precache_count].stream = out; s_precache[s_precache_count].start = start; ++s_precache_count;
}

void tinybuf_precache_reset(buffer *out){ precache_reset(out); }
int64_t tinybuf_precache_register(buffer *out, const tinybuf_value *value){
    int64_t start = (int64_t)buffer_get_length_inline(out);
    precache_add(out, value, start);
    int old = s_precache_redirect; s_precache_redirect = 0; // 禁用重定向，写入真实内容
    int before = buffer_get_length_inline(out);
    tinybuf_value_serialize(value, out);
    int after = buffer_get_length_inline(out);
    s_precache_redirect = old;
    return (after - before) > 0 ? start : -1;
}

void tinybuf_precache_set_redirect(int enable){ s_precache_redirect = (enable != 0); }
int tinybuf_precache_is_redirect(void){ return s_precache_redirect; }

int tinybuf_value_serialize(const tinybuf_value *value, buffer *out)
{
    assert(value);
    assert(out);
    // 如果开启了重定向，并且该对象已注册为预写，则输出指针到start位置
    if (s_precache_redirect && value)
    {
        int64_t start_pos = precache_find_start(out, value);
        if (start_pos >= 0)
        {
            return try_write_pointer_value(out, (enum offset_type)0, start_pos);
        }
    }
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
        int len = buffer_get_length_inline(value->_data._string);
        if(s_use_strpool){
            int idx = strpool_add(buffer_get_data_inline(value->_data._string), len);
            char type = serialize_str_index;
            buffer_append(out, &type, 1);
            dump_int((uint64_t)idx, out);
        } else {
            char type = serialize_string;
            buffer_append(out, &type, 1);
            if (len){ dump_string(len, buffer_get_data_inline(value->_data._string), out); }
            else { dump_int(0, out); }
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
        // 其他类型 version 和versionlist

    default:
        // 不可达
        assert(0);
        break;
    }

    return 0;
}

/* strpool declarations are at the top of file */

static inline void strpool_reset_write(const buffer *out){ s_strpool_count = 0; s_strpool_base = (const char*)out; }
static inline int strpool_find(const char *data, int len){ for(int i=0;i<s_strpool_count;++i){ int l = buffer_get_length_inline(s_strpool[i].buf); if(l==len && memcmp(buffer_get_data_inline(s_strpool[i].buf), data, len)==0){ return i; } } return -1; }
static inline int strpool_add(const char *data, int len){ int idx = strpool_find(data, len); if(idx>=0) return idx; if(s_strpool_count==s_strpool_capacity){ int newcap = s_strpool_capacity? s_strpool_capacity*2:16; s_strpool = (strpool_entry*)tinybuf_realloc(s_strpool, sizeof(strpool_entry)*newcap); s_strpool_capacity = newcap; } buffer *b = buffer_alloc(); buffer_assign(b, data, len); s_strpool[s_strpool_count].buf = b; return s_strpool_count++; }
static inline int strpool_write_tail(buffer *out){ if(!s_use_strpool || s_strpool_count==0) return 0; int len = 0; len += try_write_type(out, serialize_str_pool); len += try_write_int_data(FALSE, out, (QWORD)s_strpool_count); for(int i=0;i<s_strpool_count;++i){ int sl = buffer_get_length_inline(s_strpool[i].buf); len += try_write_type(out, serialize_string); len += try_write_int_data(FALSE, out, (QWORD)sl); if(sl){ buffer_append(out, buffer_get_data_inline(s_strpool[i].buf), sl); } } return len; }

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
    case serialize_str_index:
    {
        uint64_t idx;
        int len = int_deserialize((uint8_t *)ptr, size, &idx);
        if (len <= 0){ return len; }
        if (s_strpool_offset_read < 0 || s_strpool_base_read == NULL){ return -1; }
        const char *pool_start = s_strpool_base_read + s_strpool_offset_read;
        const char *q = pool_start;
        int64_t r = ((const char*)ptr + size) - pool_start; // remaining after pool start
        if(r < 1){ return 0; }
        if((uint8_t)q[0] != serialize_str_pool){ return -1; }
        ++q; --r;
        uint64_t cnt = 0; int l2 = int_deserialize((uint8_t *)q, (int)r, &cnt); if(l2 <= 0){ return -1; }
        q += l2; r -= l2;
        if(idx >= cnt){ return -1; }
        for(uint64_t i=0;i<cnt;++i){ if(r < 1){ return 0; } if((uint8_t)q[0] != serialize_string){ return -1; } ++q; --r; uint64_t sl; int l3 = int_deserialize((uint8_t *)q, (int)r, &sl); if(l3 <= 0){ return l3; } q += l3; r -= l3; if(r < (int64_t)sl){ return 0; } if(i == idx){ tinybuf_value_init_string_l(out, q, (int)sl, 0); return 1 + len; } q += sl; r -= sl; }
        return -1;
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
    current,
    // 高级指针类型
    parent_flat_start, // 基于父flat 空间寻址
    parent_flat_end    // 基于父flat 末尾寻址
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
#define READ_RETURN return (failed ? -1 : len);

//------------- pointer->object pool for cyclic structures -------------
typedef struct {
    int64_t offset;
    tinybuf_value *value;
    int complete;
} offset_pool_entry;

static const char *s_pool_base = NULL;
static offset_pool_entry *s_pool = NULL;
static int s_pool_count = 0;
static int s_pool_capacity = 0;

static inline void pool_reset(const buf_ref *buf){
    if(s_pool_base != buf->base){
        if(s_pool){
            tinybuf_free(s_pool);
            s_pool = NULL;
            s_pool_capacity = 0;
        }
        s_pool_base = buf->base;
    }
    s_pool_count = 0;
}

static inline offset_pool_entry *pool_find(int64_t offset){
    for(int i=0;i<s_pool_count;++i){
        if(s_pool[i].offset == offset){
            return &s_pool[i];
        }
    }
    return NULL;
}

static offset_pool_entry *pool_register(int64_t offset, tinybuf_value *value){
    offset_pool_entry *e = pool_find(offset);
    if(e){
        if(value){ e->value = value; }
        return e;
    }
    if(s_pool_count == s_pool_capacity){
        int newcap = s_pool_capacity ? (s_pool_capacity * 2) : 16;
        s_pool = (offset_pool_entry *)tinybuf_realloc(s_pool, sizeof(offset_pool_entry) * newcap);
        s_pool_capacity = newcap;
    }
    s_pool[s_pool_count].offset = offset;
    s_pool[s_pool_count].value = value;
    s_pool[s_pool_count].complete = 0;
    return &s_pool[s_pool_count++];
}

static inline void pool_mark_complete(int64_t offset){
    offset_pool_entry *e = pool_find(offset);
    if(e){ e->complete = 1; }
}

static inline void set_out_ref(tinybuf_value *out, tinybuf_value *target){ out->_type = tinybuf_value_ref; out->_data._ref = target; }
static inline void set_out_deref(tinybuf_value *out, const tinybuf_value *target){ tinybuf_value *clone = tinybuf_value_clone(target); tinybuf_value_clear(out); memcpy(out, clone, sizeof(tinybuf_value)); tinybuf_free(clone); }
static inline void set_out_by_mode(tinybuf_value *out, tinybuf_value *target, int deref){ if(deref) set_out_deref(out, target); else set_out_ref(out, target); }
// 从给定位置偏移一段距离 读取值
int _read_box_by_offset(buf_ref *buf, int64_t offset, tinybuf_value *out, CONTAIN_HANDLER contain_handler)
{

    INIT_STATE
    // tinybuf_value *out = NULL;
    const char *cur = buf->ptr;
    int64_t cursize = buf->size;
    // 保存现场并修改指针 从base开始 计算
    buf->ptr = buf->base + offset;
    buf->size = buf->all_size - offset;

    pool_register(offset, out);

    if (OK_AND_ADDTO(try_read_box(buf, out, contain_handler), &len))
    {
        pool_mark_complete(offset);
        SET_SUCCESS();
    }
    else
        SET_FAILED("read box error");
    CHECK_FAILED
    // 恢复现场
    buf->ptr = cur;
    buf->size = cursize;

    READ_RETURN
}

// 从指针位置开始读取box 提供开始位置+偏移 返回消耗的字节数 错误返回-1
int read_box_by_pointer(buf_ref *buf, pointer_value pointer, tinybuf_value *out, CONTAIN_HANDLER contain_handler)
{
    static int s_read_pointer_depth = 0;
    s_read_pointer_depth++;
    int len = -1;
    if (s_read_pointer_depth <= 64)
    {
        pointer_to_start(buf, &pointer);
        assert(pointer.type == start);
        len = _read_box_by_offset(buf, pointer.offset, out, contain_handler);
    }
    s_read_pointer_depth--;
    return len;
}

// 读取一个整数值
inline int try_read_int_data(BOOL isneg, buf_ref *buf, QWORD *out)
{
    INIT_STATE
    int temp = 0;
    if (OK_AND_ADDTO(try_read_int_tovar(isneg, buf->ptr, buf->size, out), &temp))
    {
        // 适配非标准read函数
        len += temp;
        buf_offset(buf, temp);
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
    if (OK_AND_ADDTO(try_read_type(buf, &type), &len))
    {
        if (type == serialize_positive_int || type == serialize_negtive_int)
        {
            BOOL isneg = type == serialize_negtive_int;
            if (OK_AND_ADDTO(try_read_int_data(isneg, buf, saveptr), &len))
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
    READ_RETURN
}

// 是否简单指针
inline BOOL is_simple_pointer_type(serialize_type type)
{
    return type == serialize_pointer_from_current_n ||
           type == serialize_pointer_from_start_n ||
           type == serialize_pointer_from_end_n ||
           type == serialize_pointer_from_current_n ||
           type == serialize_pointer_from_start_p ||
           type == serialize_pointer_from_end_p ||
           type == serialize_pointer_from_current_p;
}
inline bool is_pointer_neg(serialize_type type)
{
    return type == serialize_pointer_from_current_n ||
           type == serialize_pointer_from_start_n ||
           type == serialize_pointer_from_end_n;
}
// 简单指针偏移类型判定
inline enum offset_type get_offset_type(serialize_type type)
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
        break;
    }
}
// 尝试读取一个指针值 目前只支持简单指针 这里必须是指针值
inline int try_read_pointer_value(buf_ref *buf, QWORD *saveptr)
{
    INIT_STATE
    serialize_type type;
    if (OK_AND_ADDTO(try_read_type(buf, &type), &len))
    {
        if (is_simple_pointer_type(type))
        {
            if (OK_AND_ADDTO(try_read_int_data(FALSE, buf, saveptr), &len))
            {
                SET_SUCCESS();
            }
            else
                SET_FAILED("read pointer error");
        }
        else
            SET_FAILED("type error : not simple pointer");
    }
    else
        SET_FAILED("read type error");
    CHECK_FAILED
    READ_RETURN
}

// assert地读取一个简单指针
inline int try_read_pointer(buf_ref *buf, pointer_value *pointer)
{
    INIT_STATE
    serialize_type type;
    if (OK_AND_ADDTO(try_read_type(buf, &type), &len))
    {
        if (is_simple_pointer_type(type))
        {
            QWORD offset;
            bool isneg = is_pointer_neg(type);
            if (OK_AND_ADDTO(try_read_int_data(isneg, buf, &offset), &len))
            {
                pointer->offset = offset;
                pointer->type = get_offset_type(type);
                SET_SUCCESS();
            }
            else
                SET_FAILED("read pointer error");
        }
        else
            SET_FAILED("type error : not simple pointer");
    }
    else
        SET_FAILED("read type error");
    CHECK_FAILED
    READ_RETURN
}
//---核心box读取函数 当前整数表示 signbyte intdata
// typedef struct flat_space_block
// {
//     //对应的flat part的指针
//     QWORD offset;//start开始的指针

//     QWORD size;
// } flat_space_block;
// typedef struct
// {
//     flat_space_block *head;
//     flat_space_block *curr;
// } flat_space_info;
// typedef struct state
// {
//     // 反序列化状态

// } state;
int try_read_box(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER target_version)
{
    // 尝试初阶反序列化
    INIT_STATE
    int64_t box_offset = buf_current_offset(buf);
    pool_register(box_offset, out);
    len = tinybuf_value_deserialize(buf->ptr, buf->size, out);
    if (len == 0)
        return len;
    if (len > 0) //=0的情况为缓冲区太小
    {
        // 兼容buf逻辑
        buf_offset(buf, len);
        pool_mark_complete(box_offset);
        return len;
    }
    // 执行高阶反序列化 支持环数据引用等
    // len表示已经成功消费的字节数 不保存错误码 成功则保存正数 失败保存0
    len = 0;
    serialize_type type = serialize_null;
    if (OK_AND_ADDTO(try_read_type(buf, &type), &len))
    {
        switch (type)
        {
            // 单一version box 支持对version进行校验 版本错误则直接失败
        case serialize_version:
        {
            // version后跟一个裸整数 后跟一个 box
            QWORD version;
            if (OK_AND_ADDTO(try_read_int_data(FALSE, buf, &version), &len))
            {
                // 检查version是否正确
                if (target_version(version))
                {
                    if (OK_AND_ADDTO(try_read_box(buf, out, target_version), &len))
                    {
                        pool_mark_complete(box_offset);
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
        // 版本列表box 后跟一个整数表示列表长度 后跟一个versionbox序列 每个box都必须是versionbox
        // 整个versionlist中 必须是同种类型数据且一旦找到正确版本 则直接返回该版本数据
        case serialize_version_list:
        {
            // 读取版本列表长度
            QWORD list_len;
            if (OK_AND_ADDTO(try_read_int_data(FALSE, buf, &list_len), &len))
            {
                // 读取版本列表
                for (QWORD i = 0; i < list_len; i++)
                {
                    QWORD version;
                    if (OK_AND_ADDTO(try_read_int_data(FALSE, buf, &version), &len))
                    {
                        // 检查version是否正确
                        if (target_version(version))
                        {
                            if (OK_AND_ADDTO(try_read_box(buf, out, target_version), &len))
                            {
                                pool_mark_complete(box_offset);
                                SET_SUCCESS();
                                goto loopend;
                            }
                            SET_FAILED("read box failed");
                            goto loopend;
                        }
                        SET_FAILED("version error");
                        goto loopend;
                    }
                    SET_FAILED("read version failed");
                    goto loopend;
                }
                // 没找到直接失败
                SET_FAILED("read version list failed");
            loopend:
                break;
            }
        }

        case serialize_part:
        {
            QWORD partlen;
            if (OK_AND_ADDTO(try_read_int_data(FALSE, buf, &partlen), &len))
            {
                if (OK_AND_ADDTO(try_read_box(buf, out, target_version), &len))
                {
                    pool_mark_complete(box_offset);
                    SET_SUCCESS();
                    break;
                }
                SET_FAILED("read part failed");
                break;
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
                /* pre-read partitions optional: skip for minimal implementation */
                if (_read_box_by_offset(buf, (int64_t)offs[0], out, target_version) > 0)
                {
                    pool_mark_complete(box_offset);
                    SET_SUCCESS();
                }
                tinybuf_free(offs);
            part_table_end:
                break;
            }
            SET_FAILED("read part table header failed");
            break;
        }

        // 指针box 后接偏移整数
        case serialize_pointer_from_start_p:
        {
            QWORD offset;
            if (OK_AND_ADDTO(try_read_int_data(FALSE, buf, &offset), &len))
            {
                pointer_value pointer = (pointer_value){start, offset};
                offset_pool_entry *e = pool_find(pointer.offset);
                if(e){ set_out_deref(out, e->value); SET_SUCCESS(); break; }
                tinybuf_value *target = tinybuf_value_alloc();
                pool_register(pointer.offset, target);
                if (OK_AND_ADDTO(read_box_by_pointer(buf, pointer, target, target_version), &len))
                {
                    pool_mark_complete(pointer.offset);
                    set_out_deref(out, target);
                    SET_SUCCESS();
                    break;
                }
                SET_FAILED("read pointer->box failed");
                break;
            }
            SET_FAILED("read version failed");
            break;
        }
        case serialize_pointer_from_start_n:
        {
            SQWORD offset;
            // 按负数方式读取
            if (OK_AND_ADDTO(try_read_int_data(TRUE, buf, (QWORD *)&offset), &len))
            {
                pointer_value pointer = (pointer_value){start, offset};
                offset_pool_entry *e = pool_find(pointer.offset);
                if(e){ set_out_deref(out, e->value); SET_SUCCESS(); break; }
                tinybuf_value *target = tinybuf_value_alloc();
                pool_register(pointer.offset, target);
                if (OK_AND_ADDTO(read_box_by_pointer(buf, pointer, target, target_version), &len))
                {
                    pool_mark_complete(pointer.offset);
                    set_out_deref(out, target);
                    SET_SUCCESS();
                    break;
                }
                SET_FAILED("read pointer->box failed");
                break;
            }
            SET_FAILED("read version failed");
            break;
        }
        // 子引用：后跟一个完整的指针box，读取时强制生成子引用节点
        case serialize_sub_ref:
        {
            serialize_type subtype;
            if (OK_AND_ADDTO(try_read_type(buf, &subtype), &len))
            {
                switch (subtype)
                {
                case serialize_pointer_from_start_p:
                {
                    QWORD offset;
                    if (OK_AND_ADDTO(try_read_int_data(FALSE, buf, &offset), &len))
                    {
                        pointer_value pointer = (pointer_value){start, offset};
                        offset_pool_entry *e = pool_find(pointer.offset);
                        if(e){ set_out_ref(out, e->value); SET_SUCCESS(); break; }
                        tinybuf_value *target = tinybuf_value_alloc();
                        pool_register(pointer.offset, target);
                        if (OK_AND_ADDTO(read_box_by_pointer(buf, pointer, target, target_version), &len))
                        { pool_mark_complete(pointer.offset); set_out_ref(out, target); SET_SUCCESS(); break; }
                        SET_FAILED("read subref pointer->box failed"); break;
                    }
                    SET_FAILED("read subref version failed");
                    break;
                }
                case serialize_pointer_from_start_n:
                {
                    SQWORD offset;
                    if (OK_AND_ADDTO(try_read_int_data(TRUE, buf, (QWORD *)&offset), &len))
                    {
                        pointer_value pointer = (pointer_value){start, offset};
                        offset_pool_entry *e = pool_find(pointer.offset);
                        if(e){ set_out_ref(out, e->value); SET_SUCCESS(); break; }
                        tinybuf_value *target = tinybuf_value_alloc();
                        pool_register(pointer.offset, target);
                        if (OK_AND_ADDTO(read_box_by_pointer(buf, pointer, target, target_version), &len))
                        { pool_mark_complete(pointer.offset); set_out_ref(out, target); SET_SUCCESS(); break; }
                        SET_FAILED("read subref pointer->box failed"); break;
                    }
                    SET_FAILED("read subref version failed");
                    break;
                }
                case serialize_pointer_from_end_p:
                {
                    QWORD offset;
                    if (OK_AND_ADDTO(try_read_int_data(FALSE, buf, &offset), &len))
                    {
                        pointer_value pointer = (pointer_value){end, offset};
                        pointer_to_start(buf, &pointer);
                        offset_pool_entry *e = pool_find(pointer.offset);
                        if(e){ set_out_ref(out, e->value); SET_SUCCESS(); break; }
                        tinybuf_value *target = tinybuf_value_alloc();
                        pool_register(pointer.offset, target);
                        if (OK_AND_ADDTO(_read_box_by_offset(buf, pointer.offset, target, target_version), &len))
                        { pool_mark_complete(pointer.offset); set_out_ref(out, target); SET_SUCCESS(); break; }
                        SET_FAILED("read subref pointer->box failed"); break;
                    }
                    SET_FAILED("read subref version failed");
                    break;
                }
                case serialize_pointer_from_end_n:
                {
                    SQWORD offset;
                    if (OK_AND_ADDTO(try_read_int_data(TRUE, buf, (QWORD *)&offset), &len))
                    {
                        pointer_value pointer = (pointer_value){end, offset};
                        pointer_to_start(buf, &pointer);
                        offset_pool_entry *e = pool_find(pointer.offset);
                        if(e){ set_out_ref(out, e->value); SET_SUCCESS(); break; }
                        tinybuf_value *target = tinybuf_value_alloc();
                        pool_register(pointer.offset, target);
                        if (OK_AND_ADDTO(_read_box_by_offset(buf, pointer.offset, target, target_version), &len))
                        { pool_mark_complete(pointer.offset); set_out_ref(out, target); SET_SUCCESS(); break; }
                        SET_FAILED("read subref pointer->box failed"); break;
                    }
                    SET_FAILED("read subref version failed");
                    break;
                }
                case serialize_pointer_from_current_p:
                {
                    QWORD offset;
                    if (OK_AND_ADDTO(try_read_int_data(FALSE, buf, &offset), &len))
                    {
                        pointer_value pointer = (pointer_value){current, offset};
                        pointer_to_start(buf, &pointer);
                        offset_pool_entry *e = pool_find(pointer.offset);
                        if(e){ set_out_ref(out, e->value); SET_SUCCESS(); break; }
                        tinybuf_value *target = tinybuf_value_alloc();
                        pool_register(pointer.offset, target);
                        if (OK_AND_ADDTO(_read_box_by_offset(buf, pointer.offset, target, target_version), &len))
                        { pool_mark_complete(pointer.offset); set_out_ref(out, target); SET_SUCCESS(); break; }
                        SET_FAILED("read subref pointer->box failed"); break;
                    }
                    SET_FAILED("read subref version failed");
                    break;
                }
                case serialize_pointer_from_current_n:
                {
                    SQWORD offset;
                    if (OK_AND_ADDTO(try_read_int_data(TRUE, buf, (QWORD *)&offset), &len))
                    {
                        pointer_value pointer = (pointer_value){current, offset};
                        pointer_to_start(buf, &pointer);
                        offset_pool_entry *e = pool_find(pointer.offset);
                        if(e){ set_out_ref(out, e->value); SET_SUCCESS(); break; }
                        tinybuf_value *target = tinybuf_value_alloc();
                        pool_register(pointer.offset, target);
                        if (OK_AND_ADDTO(_read_box_by_offset(buf, pointer.offset, target, target_version), &len))
                        { pool_mark_complete(pointer.offset); set_out_ref(out, target); SET_SUCCESS(); break; }
                        SET_FAILED("read subref pointer->box failed"); break;
                    }
                    SET_FAILED("read subref version failed");
                    break;
                }
                default:
                    SET_FAILED("read subref subtype UNKNOWN");
                    break;
                }
            }
            else { SET_FAILED("read subref type failed"); }
            break;
        }
        case serialize_pointer_from_end_p:
        {
            QWORD offset;
            if (OK_AND_ADDTO(try_read_int_data(FALSE, buf, &offset), &len))
            {
                pointer_value pointer = (pointer_value){end, offset};
                pointer_to_start(buf, &pointer);
                offset_pool_entry *e = pool_find(pointer.offset);
                if(e){ set_out_deref(out, e->value); SET_SUCCESS(); break; }
                tinybuf_value *target = tinybuf_value_alloc();
                pool_register(pointer.offset, target);
                if (OK_AND_ADDTO(_read_box_by_offset(buf, pointer.offset, target, target_version), &len))
                {
                    pool_mark_complete(pointer.offset);
                    set_out_deref(out, target);
                    SET_SUCCESS();
                    break;
                }
                SET_FAILED("read pointer->box failed");
                break;
            }
            SET_FAILED("read version failed");
            break;
        }
        case serialize_pointer_from_end_n:
        {
            SQWORD offset;
            // 按负数方式读取
            if (OK_AND_ADDTO(try_read_int_data(TRUE, buf, (QWORD *)&offset), &len))
            {
                pointer_value pointer = (pointer_value){end, offset};
                pointer_to_start(buf, &pointer);
                offset_pool_entry *e = pool_find(pointer.offset);
                if(e){ set_out_deref(out, e->value); SET_SUCCESS(); break; }
                tinybuf_value *target = tinybuf_value_alloc();
                pool_register(pointer.offset, target);
                if (OK_AND_ADDTO(_read_box_by_offset(buf, pointer.offset, target, target_version), &len))
                {
                    pool_mark_complete(pointer.offset);
                    set_out_deref(out, target);
                    SET_SUCCESS();
                    break;
                }
                SET_FAILED("read pointer->box failed");
                break;
            }
            SET_FAILED("read version failed");
            break;
        }
        //--current
        case serialize_pointer_from_current_p:
        {
            QWORD offset;
            if (OK_AND_ADDTO(try_read_int_data(FALSE, buf, &offset), &len))
            {
                pointer_value pointer = (pointer_value){current, offset};
                pointer_to_start(buf, &pointer);
                offset_pool_entry *e = pool_find(pointer.offset);
                if(e){ set_out_deref(out, e->value); SET_SUCCESS(); break; }
                tinybuf_value *target = tinybuf_value_alloc();
                pool_register(pointer.offset, target);
                if (OK_AND_ADDTO(_read_box_by_offset(buf, pointer.offset, target, target_version), &len))
                {
                    pool_mark_complete(pointer.offset);
                    set_out_deref(out, target);
                    SET_SUCCESS();
                    break;
                }
                SET_FAILED("read pointer->box failed");
                break;
            }
            SET_FAILED("read version failed");
            break;
        }
        case serialize_pointer_from_current_n:
        {
            SQWORD offset;
            // 按负数方式读取
            if (OK_AND_ADDTO(try_read_int_data(TRUE, buf, (QWORD *)&offset), &len))
            {
                pointer_value pointer = (pointer_value){current, offset};
                pointer_to_start(buf, &pointer);
                offset_pool_entry *e = pool_find(pointer.offset);
                if(e){ set_out_deref(out, e->value); SET_SUCCESS(); break; }
                tinybuf_value *target = tinybuf_value_alloc();
                pool_register(pointer.offset, target);
                if (OK_AND_ADDTO(_read_box_by_offset(buf, pointer.offset, target, target_version), &len))
                {
                    pool_mark_complete(pointer.offset);
                    set_out_deref(out, target);
                    SET_SUCCESS();
                    break;
                }
                SET_FAILED("read pointer->box failed");
                break;
            }
            SET_FAILED("read version failed");
            break;
        }
        default:
            SET_FAILED("read type UNKNOWN");
            break;
        }
    }
    else
    {
        SET_FAILED("read type failed");
    }
    // 如果
    CHECK_FAILED
    if(!failed){ pool_mark_complete(box_offset); }
    READ_RETURN
}

static inline int try_write_type(buffer *out, serialize_type type)
{
    buffer_append(out, (char *)&type, 1);
    return 1;
}

static inline int try_write_int_data(BOOL isneg, buffer *out, QWORD val)
{
    return dump_int(val, out);
}

static inline int try_write_intbox(buffer *out, SQWORD sval)
{
    int len = 0;
    serialize_type type = sval < 0 ? serialize_negtive_int : serialize_positive_int;
    len += try_write_type(out, type);
    QWORD mag = sval < 0 ? (QWORD)(-sval) : (QWORD)sval;
    len += try_write_int_data(type == serialize_negtive_int, out, mag);
    return len;
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

// 写入总入口
static inline int try_write_box(buffer *out, const tinybuf_value *value)
{
    if(!s_use_strpool){
        int before = buffer_get_length_inline(out);
        tinybuf_value_serialize(value, out);
        int after = buffer_get_length_inline(out);
        return after - before;
    }
    // strpool-enabled: write head table, body, then pool without linear scan
    buffer *body = buffer_alloc();
    buffer *pool = buffer_alloc();
    strpool_reset_write(body);
    tinybuf_value_serialize(value, body);
    // write pool into separate buf
    strpool_write_tail(pool);
    int body_len = buffer_get_length_inline(body);
    int pool_len = buffer_get_length_inline(pool);
    // compute varint length of offset iteratively
    uint64_t offset_guess_len = 1; // initial guess for varint
    uint8_t tmp[16];
    while(1){
        uint64_t off = 1 + offset_guess_len + (uint64_t)body_len; // type(1) + varint + body
        int l = int_serialize(off, tmp);
        if((uint64_t)l == offset_guess_len){ break; }
        offset_guess_len = (uint64_t)l;
    }
    uint64_t final_off = 1 + offset_guess_len + (uint64_t)body_len;
    int before = buffer_get_length_inline(out);
    // write table
    try_write_type(out, serialize_str_pool_table);
    try_write_int_data(FALSE, out, final_off);
    // write body
    buffer_append(out, buffer_get_data_inline(body), body_len);
    // write pool
    if(pool_len){ buffer_append(out, buffer_get_data_inline(pool), pool_len); }
    int after = buffer_get_length_inline(out);
    buffer_free(body); buffer_free(pool);
    return after - before;
}

static inline int try_write_version_box(buffer *out, QWORD version, const tinybuf_value *box)
{
    int len = 0;
    len += try_write_type(out, serialize_version);
    len += try_write_int_data(FALSE, out, version);
    len += try_write_box(out, box);
    return len;
}

static inline int try_write_version_list(buffer *out, const QWORD *versions, const tinybuf_value **boxes, int count)
{
    int len = 0;
    len += try_write_type(out, serialize_version_list);
    len += try_write_int_data(FALSE, out, (QWORD)count);
    for (int i = 0; i < count; ++i)
    {
        len += try_write_int_data(FALSE, out, versions[i]);
        len += try_write_box(out, boxes[i]);
    }
    return len;
}

static inline int try_write_part(buffer *out, const tinybuf_value *value)
{
    buffer *body = buffer_alloc();
    int body_len = try_write_box(body, value);
    int before = buffer_get_length_inline(out);
    try_write_type(out, serialize_part);
    try_write_int_data(FALSE, out, (QWORD)body_len);
    buffer_append(out, buffer_get_data_inline(body), body_len);
    int after = buffer_get_length_inline(out);
    buffer_free(body);
    return after - before;
}

static inline int try_write_partitions(buffer *out, const tinybuf_value *mainbox, const tinybuf_value **subs, int count)
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
        uint64_t table_len = 1 + (uint64_t)int_serialize((uint64_t)total, tmp);
        for (int i = 0; i < total; ++i) table_len += vlen[i];
        offs[0] = table_len;
        for (int i = 1; i < total; ++i) offs[i] = offs[i - 1] + (uint64_t)lens[i - 1];
        int stable = 1;
        for (int i = 0; i < total; ++i)
        {
            int l = int_serialize(offs[i], tmp);
            if ((uint64_t)l != vlen[i]) { vlen[i] = (uint64_t)l; stable = 0; }
        }
        if (stable) break;
    }
    int before = buffer_get_length_inline(out);
    try_write_type(out, serialize_part_table);
    try_write_int_data(FALSE, out, (QWORD)total);
    for (int i = 0; i < total; ++i)
    {
        try_write_int_data(FALSE, out, (QWORD)offs[i]);
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

// public wrappers
int tinybuf_try_read_box(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler)
{
    pool_reset(buf);
    s_strpool_base_read = buf->base; s_strpool_offset_read = -1;
    if (buf->ptr == buf->base && buf->size >= 1 && (uint8_t)buf->base[0] == serialize_str_pool_table)
    {
        buf_ref hb = *buf;
        serialize_type t; int c = try_read_type(&hb, &t);
        if (c > 0 && t == serialize_str_pool_table)
        {
            QWORD off; int c2 = try_read_int_data(FALSE, &hb, &off);
            if (c2 > 0){ s_strpool_offset_read = (int64_t)off; buf_offset(buf, c + c2); }
        }
    }
    return try_read_box(buf, out, contain_handler);
}

void tinybuf_set_read_pointer_mode(tinybuf_read_pointer_mode mode){ s_read_pointer_mode = mode; }
int tinybuf_try_read_box_with_mode(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_read_pointer_mode mode){ s_read_pointer_mode = mode; return tinybuf_try_read_box(buf, out, contain_handler); }

void tinybuf_set_use_strpool(int enable){ s_use_strpool = enable ? 1 : 0; }

int tinybuf_try_write_box(buffer *out, const tinybuf_value *value)
{
    return try_write_box(out, value);
}

int tinybuf_try_write_version_box(buffer *out, QWORD version, const tinybuf_value *box)
{
    return try_write_version_box(out, version, box);
}

int tinybuf_try_write_version_list(buffer *out, const QWORD *versions, const tinybuf_value **boxes, int count)
{
    return try_write_version_list(out, versions, boxes, count);
}

int tinybuf_try_write_part(buffer *out, const tinybuf_value *value)
{
    return try_write_part(out, value);
}

int tinybuf_try_write_partitions(buffer *out, const tinybuf_value *mainbox, const tinybuf_value **subs, int count)
{
    return try_write_partitions(out, mainbox, subs, count);
}

int tinybuf_try_write_pointer(buffer *out, int t, SQWORD offset)
{
    enum offset_type et = start;
    if (t == 1) et = end;
    else if (t == 2) et = current;
    return try_write_pointer_value(out, et, offset);
}

int tinybuf_try_write_sub_ref(buffer *out, int t, SQWORD offset)
{
    int len = 0;
    len += try_write_type(out, serialize_sub_ref);
    len += tinybuf_try_write_pointer(out, t, offset);
    return len;
}

int tinybuf_try_write_array_header(buffer *out, int count){
    int len = 0; len += try_write_type(out, serialize_array); len += try_write_int_data(FALSE, out, (QWORD)count); return len;
}
int tinybuf_try_write_map_header(buffer *out, int count){
    int len = 0; len += try_write_type(out, serialize_map); len += try_write_int_data(FALSE, out, (QWORD)count); return len;
}
int tinybuf_try_write_string_raw(buffer *out, const char *str, int len){
    return dump_string(len, str, out);
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

static inline void append_cstr(buffer *out, const char *s){
    buffer_append(out, s, (int)strlen(s));
}
static inline void append_int_dec(buffer *out, int64_t v){
    char tmp[64];
    int n = snprintf(tmp, sizeof(tmp), "%lld", (long long)v);
    buffer_append(out, tmp, n);
}
static inline void append_newline(buffer *out){
    buffer_append(out, "\r\n", 2);
}
static int s_dump_indent = 0;
static inline void append_indent(buffer *out){
    for(int i=0;i<s_dump_indent;++i){
        buffer_append(out, "    ", 4);
    }
}

static int dump_box_text(buf_ref *buf, buffer *out);
static int collect_box_labels(buf_ref *buf);

typedef struct { int64_t pos; int label; } dump_label;
static dump_label *s_dump_labels = NULL;
static int s_dump_labels_count = 0;
static int s_dump_labels_capacity = 0;
static const char *s_dump_base = NULL;
static int64_t s_dump_total = 0;
static int64_t *s_dump_box_starts = NULL;
static int s_dump_box_starts_count = 0;
static int s_dump_box_starts_capacity = 0;

static inline void dump_labels_reset(const buf_ref *buf){
    s_dump_base = buf->base;
    s_dump_total = buf->all_size;
    s_dump_labels_count = 0;
    s_dump_box_starts_count = 0;
}
static inline void dump_labels_register(int64_t target_pos, int label){
    if(s_dump_labels_count == s_dump_labels_capacity){
        int newcap = s_dump_labels_capacity ? (s_dump_labels_capacity * 2) : 16;
        s_dump_labels = (dump_label*)tinybuf_realloc(s_dump_labels, sizeof(dump_label) * newcap);
        s_dump_labels_capacity = newcap;
    }
    s_dump_labels[s_dump_labels_count].pos = target_pos;
    s_dump_labels[s_dump_labels_count].label = (int)target_pos; // store start-based target to match (p:x)
    ++s_dump_labels_count;
}
static inline int64_t nearest_box_start(int64_t pos){
    int64_t best = -1;
    for(int i=0;i<s_dump_box_starts_count;++i){
        int64_t v = s_dump_box_starts[i];
        if(v <= pos && v >= 0){
            if(best < 0 || v > best){ best = v; }
        }
    }
    return best < 0 ? pos : best;
}
static inline void dump_labels_emit_prefix(int64_t cur_pos, buffer *out){
    int first = 1;
    for(int i=0;i<s_dump_labels_count;++i){
        if(s_dump_labels[i].pos == cur_pos){
            if(first){ append_cstr(out, "(p:"); first = 0; }
            else { append_cstr(out, ","); }
            append_int_dec(out, (int64_t)s_dump_labels[i].label);
        }
    }
    if(!first){ append_cstr(out, ") "); }
}

int tinybuf_dump_buffer_as_text(const char *data, int len, buffer *out){
    buf_ref br;
    br.base = data;
    br.all_size = (int64_t)len;
    br.ptr = data;
    br.size = (int64_t)len;
    dump_labels_reset(&br);
    // first pass: collect labels across entire buffer
    collect_box_labels(&br);
    // second pass: real dump with prefixes
    br.ptr = data; br.size = (int64_t)len;
    return dump_box_text(&br, out);
}

static int dump_string_text(buf_ref *buf, buffer *out){
    QWORD slen=0; int consumed=0; int add=0;
    add = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &slen);
    if(add <= 0) return add;
    consumed += add;
    buf_offset(buf, add);
    append_cstr(out, "\"");
    if((int64_t)slen <= buf->size){
        buffer_append(out, buf->ptr, (int)slen);
        buf_offset(buf, (int)slen);
        consumed += (int)slen;
    }
    append_cstr(out, "\"");
    return consumed;
}

static int dump_array_text(buf_ref *buf, buffer *out){
    QWORD cnt=0; int consumed=0; int add=0;
    add = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &cnt);
    if(add <= 0) return add;
    consumed += add; buf_offset(buf, add);
    append_cstr(out, "["); append_newline(out); ++s_dump_indent;
    for(QWORD i=0;i<cnt;++i){
        if(i){ append_cstr(out, ","); append_newline(out); }
        append_indent(out);
        consumed += dump_box_text(buf, out);
    }
    append_newline(out); --s_dump_indent; append_indent(out); append_cstr(out, "]");
    return consumed;
}

static int dump_map_text(buf_ref *buf, buffer *out){
    QWORD cnt=0; int consumed=0; int add=0;
    add = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &cnt);
    if(add <= 0) return add;
    consumed += add; buf_offset(buf, add);
    append_cstr(out, "{"); append_newline(out); ++s_dump_indent;
    for(QWORD i=0;i<cnt;++i){
        if(i){ append_cstr(out, ","); append_newline(out); }
        append_indent(out);
        consumed += dump_string_text(buf, out);
        append_cstr(out, " : ");
        consumed += dump_box_text(buf, out);
    }
    append_newline(out); --s_dump_indent; append_indent(out); append_cstr(out, "}");
    return consumed;
}

static int dump_box_text(buf_ref *buf, buffer *out){
    int consumed=0; int64_t cur_pos = (int64_t)(buf->ptr - buf->base);
    dump_labels_emit_prefix(cur_pos, out);
    serialize_type t=serialize_null; int add=try_read_type(buf,&t); if(add<=0) return add; consumed+=add;
    switch(t){
        case serialize_null:
            append_cstr(out, "null");
            break;
        case serialize_positive_int:
        case serialize_negtive_int:
        {
            QWORD v=0; int a=try_read_int_tovar(t==serialize_negtive_int, buf->ptr, (int)buf->size, &v);
            if(a<=0) return a; consumed+=a; buf_offset(buf,a);
            append_int_dec(out, (int64_t)v);
            break;
        }
        case serialize_bool_true: append_cstr(out, "true"); break;
        case serialize_bool_false: append_cstr(out, "false"); break;
        case serialize_double:
        {
            if(buf->size < 8) return 0; double dv=0; memcpy(&dv, buf->ptr, 8); buf_offset(buf,8); consumed+=8; char tmp[64]; int n=snprintf(tmp,sizeof(tmp),"%g",dv); buffer_append(out,tmp,n);
            break;
        }
        case serialize_string:
            consumed += dump_string_text(buf, out);
            break;
        case serialize_map:
            consumed += dump_map_text(buf, out);
            break;
        case serialize_array:
            consumed += dump_array_text(buf, out);
            break;
        case serialize_pointer_from_current_n:
        case serialize_pointer_from_start_n:
        case serialize_pointer_from_end_n:
        case serialize_pointer_from_current_p:
        case serialize_pointer_from_start_p:
        case serialize_pointer_from_end_p:
        {
            BOOL neg = (t==serialize_pointer_from_current_n||t==serialize_pointer_from_start_n||t==serialize_pointer_from_end_n);
            QWORD mag=0; int a=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &mag);
            if(a<=0) return a; consumed+=a; buf_offset(buf,a);
            int64_t off = neg ? -(int64_t)mag : (int64_t)mag;
            int64_t target_pos = 0;
            if(t==serialize_pointer_from_start_n || t==serialize_pointer_from_start_p){
                target_pos = off;
            }else if(t==serialize_pointer_from_end_n || t==serialize_pointer_from_end_p){
                target_pos = s_dump_total - off;
            }else{
                int64_t anchor = (int64_t)(buf->ptr - buf->base);
                target_pos = anchor + off;
            }
            if(target_pos >= 0 && target_pos <= s_dump_total){
                int64_t adj = nearest_box_start(target_pos);
                dump_labels_register(adj, (int)adj);
                target_pos = adj;
            }
            const char *type_str = (t==serialize_pointer_from_start_n || t==serialize_pointer_from_start_p) ? "start" : (t==serialize_pointer_from_end_n || t==serialize_pointer_from_end_p) ? "end" : "current";
            append_cstr(out, "[pointer "); append_cstr(out, type_str); append_cstr(out, " "); append_int_dec(out, off);
            if(!(t==serialize_pointer_from_start_n || t==serialize_pointer_from_start_p)){
                append_cstr(out, " -> start "); append_int_dec(out, target_pos);
            }
            append_cstr(out, "]");
            break;
        }
        case serialize_version:
        {
            QWORD ver=0; int a=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &ver); if(a<=0) return a; consumed+=a; buf_offset(buf,a);
            consumed += dump_box_text(buf, out); // transparent: dump inner value only
            break;
        }
        case serialize_version_list:
        {
            QWORD cnt=0; int a=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &cnt); if(a<=0) return a; consumed+=a; buf_offset(buf,a);
            append_cstr(out, "{""versions"":{");
            for(QWORD i=0;i<cnt;++i){
                if(i) append_cstr(out, ",");
                QWORD ver=0; int b=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &ver); if(b<=0) return b; consumed+=b; buf_offset(buf,b);
                append_cstr(out, "\""); append_int_dec(out,(int64_t)ver); append_cstr(out, "\":");
                consumed += dump_box_text(buf, out);
            }
            append_cstr(out, "}}");
            break;
        }
        default:
            append_cstr(out, "<unknown>");
            break;
    }
    return consumed;
}

static int collect_string(buf_ref *buf){
    QWORD slen=0; int consumed=0; int add=0;
    add = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &slen);
    if(add <= 0) return add;
    consumed += add; buf_offset(buf, add);
    if((int64_t)slen <= buf->size){ buf_offset(buf, (int)slen); consumed += (int)slen; }
    return consumed;
}
static int collect_array(buf_ref *buf){
    QWORD cnt=0; int consumed=0; int add=0;
    add = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &cnt);
    if(add <= 0) return add;
    consumed += add; buf_offset(buf, add);
    for(QWORD i=0;i<cnt;++i){ consumed += collect_box_labels(buf); }
    return consumed;
}
static int collect_map(buf_ref *buf){
    QWORD cnt=0; int consumed=0; int add=0;
    add = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &cnt);
    if(add <= 0) return add;
    consumed += add; buf_offset(buf, add);
    for(QWORD i=0;i<cnt;++i){ consumed += collect_string(buf); consumed += collect_box_labels(buf); }
    return consumed;
}

static int collect_box_labels(buf_ref *buf){
    int consumed=0; int64_t start_pos = (int64_t)(buf->ptr - buf->base);
    if(s_dump_box_starts_count == s_dump_box_starts_capacity){
        int newcap = s_dump_box_starts_capacity ? (s_dump_box_starts_capacity * 2) : 32;
        s_dump_box_starts = (int64_t*)tinybuf_realloc(s_dump_box_starts, sizeof(int64_t) * newcap);
        s_dump_box_starts_capacity = newcap;
    }
    s_dump_box_starts[s_dump_box_starts_count++] = start_pos;
    serialize_type t=serialize_null; int add=try_read_type(buf,&t); if(add<=0) return add; consumed+=add;
    switch(t){
        case serialize_null: break;
        case serialize_positive_int:
        case serialize_negtive_int:
        {
            QWORD v=0; int a=try_read_int_tovar(t==serialize_negtive_int, buf->ptr, (int)buf->size, &v);
            if(a<=0) return a; consumed+=a; buf_offset(buf,a);
            break;
        }
        case serialize_bool_true: break;
        case serialize_bool_false: break;
        case serialize_double:
        {
            if(buf->size < 8) return 0; buf_offset(buf,8); consumed+=8; break;
        }
        case serialize_string:
            consumed += collect_string(buf);
            break;
        case serialize_map:
            consumed += collect_map(buf);
            break;
        case serialize_array:
            consumed += collect_array(buf);
            break;
        case serialize_pointer_from_current_n:
        case serialize_pointer_from_start_n:
        case serialize_pointer_from_end_n:
        case serialize_pointer_from_current_p:
        case serialize_pointer_from_start_p:
        case serialize_pointer_from_end_p:
        {
            BOOL neg = (t==serialize_pointer_from_current_n||t==serialize_pointer_from_start_n||t==serialize_pointer_from_end_n);
            QWORD mag=0; int a=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &mag);
            if(a<=0) return a; consumed+=a; buf_offset(buf,a);
            int64_t off = neg ? -(int64_t)mag : (int64_t)mag;
            int64_t target_pos = 0;
            if(t==serialize_pointer_from_start_n || t==serialize_pointer_from_start_p){
                target_pos = off;
            }else if(t==serialize_pointer_from_end_n || t==serialize_pointer_from_end_p){
                target_pos = s_dump_total - off;
            }else{
                int64_t anchor = (int64_t)(buf->ptr - buf->base);
                target_pos = anchor + off;
            }
            if(target_pos >= 0 && target_pos <= s_dump_total){
                int64_t adj = nearest_box_start(target_pos);
                dump_labels_register(adj, (int)adj);
            }
            break;
        }
        case serialize_version:
        {
            QWORD ver=0; int a=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &ver); if(a<=0) return a; consumed+=a; buf_offset(buf,a);
            consumed += collect_box_labels(buf);
            break;
        }
        case serialize_version_list:
        {
            QWORD cnt=0; int a=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &cnt); if(a<=0) return a; consumed+=a; buf_offset(buf,a);
            for(QWORD i=0;i<cnt;++i){ QWORD ver=0; int b=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &ver); if(b<=0) return b; consumed+=b; buf_offset(buf,b); consumed += collect_box_labels(buf); }
            break;
        }
        default: break;
    }
    return consumed;
}

//--version
void tinybuf_versionlist_add(tinybuf_value *versionlist, int64_t version, tinybuf_value *value)
{
    buffer* key_buf=buffer_alloc();
    assert(key_buf);
    dump_int(version, key_buf);
    tinybuf_value_map_set2(versionlist, key_buf, value);
}
void tinybuf_version_set(tinybuf_value *target, int64_t version, tinybuf_value *value)
{
    target->_type = tinybuf_version;
    target->_data._ref = value;
}
// forward decl
int try_read_box(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER target_version);
static inline int dump_int(uint64_t len, buffer *out);
static inline int try_write_type(buffer *out, serialize_type type);
static inline int try_write_int_data(BOOL isneg, buffer *out, QWORD val);
static inline int try_write_pointer_value(buffer *out, enum offset_type t, SQWORD offset);
