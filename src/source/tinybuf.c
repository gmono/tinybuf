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
static inline void pool_lock(void);
static inline void pool_unlock(void);

typedef uint64_t QWORD;
typedef int64_t SQWORD;
typedef struct { int dtype; int dims; int64_t *shape; void *data; int64_t count; } tinybuf_tensor_t;
typedef struct { int64_t count; uint8_t *bits; } tinybuf_bool_map_t;
typedef struct { tinybuf_value *tensor; tinybuf_value **indices; int dims; } tinybuf_indexed_tensor_t;

int s_use_strpool = 0;
static int s_use_strpool_trie = 1;
static int64_t s_strpool_offset_read = -1;
static const char *s_strpool_base_read = NULL;
typedef struct { buffer *buf; } strpool_entry;
static strpool_entry *s_strpool = NULL;
static int s_strpool_count = 0;
static int s_strpool_capacity = 0;
static const char *s_strpool_base = NULL;
static const char *s_last_error_msg = NULL;

 

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
    serialize_vector_tensor=43,
    serialize_dense_tensor=44,
    serialize_sparse_tensor=45,
    serialize_bool_map=46,
    serialize_indexed_tensor=47,
    serialize_type_idx = 48,
    serialize_extern_str_idx = 253,
    serialize_extern_str = 254,
    serialize_extern_int = 255
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

int tinybuf_value_set_plugin_index(tinybuf_value *value, int index)
{
    assert(value);
    value->_plugin_index = index;
    return 0;
}

int tinybuf_value_get_plugin_index(const tinybuf_value *value)
{
    if(!value) return -1;
    return value->_plugin_index;
}

int tinybuf_value_set_custom_box_type(tinybuf_value *value, int type)
{
    assert(value);
    value->_custom_box_type = type;
    return 0;
}

int tinybuf_value_get_custom_box_type(const tinybuf_value *value)
{
    if(!value) return -1;
    return value->_custom_box_type;
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
    if(value && value->_custom_box_type >= 0){ return tinybuf_plugins_try_write((uint8_t)value->_custom_box_type, value, out); }
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
    case tinybuf_tensor:
    {
        tinybuf_tensor_t *t = (tinybuf_tensor_t*)value->_data._custom;
        if(!t){ break; }
        int64_t elem = t->count;
        if(t->dims == 1){
            char type = serialize_vector_tensor;
            buffer_append(out, &type, 1);
            dump_int((uint64_t)elem, out);
            dump_int((uint64_t)t->dtype, out);
            if(t->dtype==8){
                const double *pd = (const double*)t->data;
                for(int64_t i=0;i<elem;++i){ dump_double(pd[i], out); }
            } else if(t->dtype==10){
                const float *pf = (const float*)t->data;
                for(int64_t i=0;i<elem;++i){ uint32_t raw=0; memcpy(&raw,&pf[i],4); raw=htonl(raw); buffer_append(out,(char*)&raw,4);}            
            } else if(t->dtype==11){
                const uint8_t *pb = (const uint8_t*)t->data;
                int64_t bytes = (elem + 7) / 8;
                for(int64_t b=0;b<bytes;++b){
                    uint8_t one = 0;
                    for(int k=0;k<8;++k){
                        int64_t idx = b*8 + k; if(idx>=elem) break;
                        uint8_t bit = pb[idx] ? 1 : 0;
                        one |= (uint8_t)(bit << (7 - k));
                    }
                    buffer_append(out, (const char*)&one, 1);
                }
            } else {
                const int64_t *pi64 = (const int64_t*)t->data;
                for(int64_t i=0;i<elem;++i){ uint64_t v = (uint64_t)(pi64[i] < 0 ? -pi64[i] : pi64[i]); char ty = (pi64[i] < 0)?serialize_negtive_int:serialize_positive_int; buffer_append(out,&ty,1); dump_int(v, out); }
            }
        } else {
            char type = serialize_dense_tensor;
            buffer_append(out, &type, 1);
            dump_int((uint64_t)t->dims, out);
            for(int i=0;i<t->dims;++i){ dump_int((uint64_t)t->shape[i], out); }
            dump_int((uint64_t)t->dtype, out);
            if(t->dtype==8){
                const double *pd = (const double*)t->data;
                for(int64_t i=0;i<elem;++i){ dump_double(pd[i], out); }
            } else if(t->dtype==10){
                const float *pf = (const float*)t->data;
                for(int64_t i=0;i<elem;++i){ uint32_t raw=0; memcpy(&raw,&pf[i],4); raw=htonl(raw); buffer_append(out,(char*)&raw,4);}            
            } else if(t->dtype==11){
                const uint8_t *pb = (const uint8_t*)t->data;
                int64_t bytes = (elem + 7) / 8;
                for(int64_t b=0;b<bytes;++b){
                    uint8_t one = 0;
                    for(int k=0;k<8;++k){
                        int64_t idx = b*8 + k; if(idx>=elem) break;
                        uint8_t bit = pb[idx] ? 1 : 0;
                        one |= (uint8_t)(bit << (7 - k));
                    }
                    buffer_append(out, (const char*)&one, 1);
                }
            } else {
                const int64_t *pi64 = (const int64_t*)t->data;
                for(int64_t i=0;i<elem;++i){ uint64_t v = (uint64_t)(pi64[i] < 0 ? -pi64[i] : pi64[i]); char ty = (pi64[i] < 0)?serialize_negtive_int:serialize_positive_int; buffer_append(out,&ty,1); dump_int(v, out); }
            }
        }
        }
    break;
    case tinybuf_bool_map:
    {
        tinybuf_bool_map_t *bm = (tinybuf_bool_map_t*)value->_data._custom;
        if(!bm){ break; }
        char type = serialize_bool_map;
        buffer_append(out, &type, 1);
        dump_int((uint64_t)bm->count, out);
        int64_t bytes = (bm->count + 7) / 8;
        if(bytes>0){ buffer_append(out, (const char*)bm->bits, (int)bytes); }
    }
    break;
    case tinybuf_indexed_tensor:
    {
        tinybuf_indexed_tensor_t *it = (tinybuf_indexed_tensor_t*)value->_data._custom;
        if(!it || !it->tensor){ break; }
        char type = serialize_indexed_tensor;
        buffer_append(out, &type, 1);
        try_write_box(out, it->tensor);
        dump_int((uint64_t)it->dims, out);
        for(int i=0;i<it->dims;++i){
            if(it->indices && it->indices[i]){ dump_int(1, out); try_write_box(out, it->indices[i]); }
            else { dump_int(0, out); }
        }
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
typedef struct { int parent; unsigned char ch; unsigned char is_leaf; int leaf_id; } trie_node;
static inline int strpool_write_tail(buffer *out){ if(!s_use_strpool || s_strpool_count==0) return 0; if(!s_use_strpool_trie){ int len = 0; len += try_write_type(out, serialize_str_pool); len += try_write_int_data(FALSE, out, (QWORD)s_strpool_count); for(int i=0;i<s_strpool_count;++i){ int sl = buffer_get_length_inline(s_strpool[i].buf); len += try_write_type(out, serialize_string); len += try_write_int_data(FALSE, out, (QWORD)sl); if(sl){ buffer_append(out, buffer_get_data_inline(s_strpool[i].buf), sl); } } return len; } int before = buffer_get_length_inline(out); try_write_type(out, 27); /* trie pool */ trie_node *nodes = NULL; int ncount = 1; nodes = (trie_node*)tinybuf_malloc(sizeof(trie_node)*1); nodes[0].parent=-1; nodes[0].ch=0; nodes[0].is_leaf=0; nodes[0].leaf_id=-1; for(int i=0;i<s_strpool_count;++i){ const char *p = buffer_get_data_inline(s_strpool[i].buf); int sl = buffer_get_length_inline(s_strpool[i].buf); int cur = 0; for(int k=0;k<sl;++k){ unsigned char c = (unsigned char)p[k]; int found=-1; for(int j=1;j<ncount;++j){ if(nodes[j].parent==cur && nodes[j].ch==c){ found=j; break; } } if(found<0){ nodes = (trie_node*)tinybuf_realloc(nodes, sizeof(trie_node)*(ncount+1)); nodes[ncount].parent=cur; nodes[ncount].ch=c; nodes[ncount].is_leaf=0; nodes[ncount].leaf_id=-1; found = ncount; ++ncount; } cur = found; } nodes[cur].is_leaf=1; nodes[cur].leaf_id=i; } try_write_int_data(FALSE, out, (QWORD)ncount); for(int i=0;i<ncount;++i){ try_write_int_data(FALSE, out, (QWORD)(nodes[i].parent<0?0:(QWORD)nodes[i].parent)); buffer_append(out, (const char*)&nodes[i].ch, 1); uint8_t flag = nodes[i].is_leaf?1:0; buffer_append(out, (const char*)&flag, 1); if(nodes[i].is_leaf){ try_write_int_data(FALSE, out, (QWORD)nodes[i].leaf_id); } } tinybuf_free(nodes); int after = buffer_get_length_inline(out); return after - before; }

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
static int tinybuf_deserialize_vector_tensor(const char *ptr, int size, tinybuf_value *out){
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
    a = int_deserialize((uint8_t*)ptr, size, &cnt); if(a<=0) return a; ptr += a; size -= a;
    b = int_deserialize((uint8_t*)ptr, size, &dt); if(b<=0) return b; ptr += b; size -= b;
    count = (int64_t)cnt;
    tensor = (tinybuf_tensor_t*)tinybuf_malloc((int)sizeof(tinybuf_tensor_t));
    tensor->dtype = (int)dt; tensor->dims = 1;
    shape = (int64_t*)tinybuf_malloc((int)sizeof(int64_t));
    if(!shape){ tinybuf_free(tensor); return -1; }
    shape[0] = count; tensor->shape = shape;
    tensor->count = count;
    if(tensor->dtype == 8){
        if(size < count*8){ tinybuf_free(shape); tinybuf_free(tensor); return 0; }
        tensor->data = tinybuf_malloc((int)(count*8));
        memcpy(tensor->data, ptr, (size_t)(count*8));
        ptr += count*8; size -= count*8;
    }
    else if(tensor->dtype == 10){
        if(size < count*4){ tinybuf_free(shape); tinybuf_free(tensor); return 0; }
        tensor->data = tinybuf_malloc((int)(count*4));
        memcpy(tensor->data, ptr, (size_t)(count*4));
        ptr += count*4; size -= count*4;
    }
    else if(tensor->dtype == 11){
        int64_t bytes = (count + 7) / 8;
        if(size < bytes){ tinybuf_free(shape); tinybuf_free(tensor); return 0; }
        buf_u8 = (uint8_t*)tinybuf_malloc((int)count);
        for(i=0;i<count;++i){
            uint8_t one = ((const uint8_t*)ptr)[i/8]; int bit = 7 - (int)(i % 8);
            buf_u8[i] = (uint8_t)((one >> bit) & 1);
        }
        ptr += bytes; size -= bytes;
        tensor->data = buf_u8;
    }
    else {
        buf_i64 = (int64_t*)tinybuf_malloc((int)(count*(int64_t)sizeof(int64_t)));
        for(i=0;i<count;++i){
            if(size<1){ tinybuf_free(buf_i64); tinybuf_free(shape); tinybuf_free(tensor); return 0; }
            serialize_type tt = (serialize_type)ptr[0]; ptr += 1; size -= 1;
            uint64_t v = 0; int c = int_deserialize((uint8_t*)ptr, size, &v);
            if(c<=0){ tinybuf_free(buf_i64); tinybuf_free(shape); tinybuf_free(tensor); return c; }
            ptr += c; size -= c;
            int64_t sv = (tt==serialize_negtive_int)?-(int64_t)v:(int64_t)v;
            buf_i64[i] = sv;
        }
        tensor->data = buf_i64;
    }
    out->_type = tinybuf_tensor; out->_data._custom = tensor; out->_custom_free = NULL;
    return 1 + a + b;
}

static int tinybuf_deserialize_dense_tensor(const char *ptr, int size, tinybuf_value *out){
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
    a = int_deserialize((uint8_t*)ptr, size, &dims); if(a<=0) return a; ptr += a; size -= a;
    shape = (int64_t*)tinybuf_malloc((int)(sizeof(int64_t)*(int)dims));
    for(i=0;i<dims;++i){
        uint64_t d=0; int c=int_deserialize((uint8_t*)ptr, size, &d);
        if(c<=0){ tinybuf_free(shape); return c; }
        ptr += c; size -= c; shape[i] = (int64_t)d; prod *= shape[i];
    }
    b = int_deserialize((uint8_t*)ptr, size, &dt); if(b<=0){ tinybuf_free(shape); return b; } ptr += b; size -= b;
    tensor = (tinybuf_tensor_t*)tinybuf_malloc((int)sizeof(tinybuf_tensor_t));
    tensor->dtype = (int)dt; tensor->dims = (int)dims; tensor->shape = shape; tensor->count = prod;
    if(tensor->dtype == 8){
        if(size < prod*8){ tinybuf_free(shape); tinybuf_free(tensor); return 0; }
        tensor->data = tinybuf_malloc((int)(prod*8));
        memcpy(tensor->data, ptr, (size_t)(prod*8));
        ptr += prod*8; size -= prod*8;
    }
    else if(tensor->dtype == 10){
        if(size < prod*4){ tinybuf_free(shape); tinybuf_free(tensor); return 0; }
        tensor->data = tinybuf_malloc((int)(prod*4));
        memcpy(tensor->data, ptr, (size_t)(prod*4));
        ptr += prod*4; size -= prod*4;
    }
    else if(tensor->dtype == 11){
        int64_t bytes = (prod + 7) / 8;
        if(size < bytes){ tinybuf_free(shape); tinybuf_free(tensor); return 0; }
        buf_u8 = (uint8_t*)tinybuf_malloc((int)prod);
        for(ii=0; ii<prod; ++ii){
            uint8_t one = ((const uint8_t*)ptr)[ii/8]; int bit = 7 - (int)(ii % 8);
            buf_u8[ii] = (uint8_t)((one >> bit) & 1);
        }
        ptr += bytes; size -= bytes;
        tensor->data = buf_u8;
    }
    else {
        buf_i64 = (int64_t*)tinybuf_malloc((int)(prod*(int64_t)sizeof(int64_t)));
        for(ii=0; ii<prod; ++ii){
            if(size<1){ tinybuf_free(buf_i64); tinybuf_free(shape); tinybuf_free(tensor); return 0; }
            serialize_type tt = (serialize_type)ptr[0]; ptr += 1; size -= 1;
            uint64_t v = 0; int c = int_deserialize((uint8_t*)ptr, size, &v);
            if(c<=0){ tinybuf_free(buf_i64); tinybuf_free(shape); tinybuf_free(tensor); return c; }
            ptr += c; size -= c; int64_t sv = (tt==serialize_negtive_int)?-(int64_t)v:(int64_t)v; buf_i64[ii] = sv;
        }
        tensor->data = buf_i64;
    }
    out->_type = tinybuf_tensor; out->_data._custom = tensor; out->_custom_free = NULL;
    return 1 + a + b;
}

static int tinybuf_deserialize_sparse_tensor(const char *ptr, int size, tinybuf_value *out){
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
    a = int_deserialize((uint8_t*)ptr, size, &dims); if(a<=0) return a; ptr += a; size -= a;
    shape = (int64_t*)tinybuf_malloc((int)(sizeof(int64_t)*(int)dims));
    for(i=0;i<dims;++i){
        uint64_t d=0; int c=int_deserialize((uint8_t*)ptr, size, &d);
        if(c<=0){ tinybuf_free(shape); return c; }
        ptr += c; size -= c; shape[i] = (int64_t)d; prod *= shape[i];
    }
    b = int_deserialize((uint8_t*)ptr, size, &dt); if(b<=0){ tinybuf_free(shape); return b; } ptr += b; size -= b;
    e = int_deserialize((uint8_t*)ptr, size, &k); if(e<=0){ tinybuf_free(shape); return e; } ptr += e; size -= e;
    tensor = (tinybuf_tensor_t*)tinybuf_malloc((int)sizeof(tinybuf_tensor_t));
    tensor->dtype = (int)dt; tensor->dims = (int)dims; tensor->shape = shape; tensor->count = prod;
    if(tensor->dtype == 8){
        double *buf = (double*)tinybuf_malloc((int)(prod*8)); memset(buf,0,(size_t)(prod*8));
        for(ii=0; ii<k; ++ii){
            int64_t stride=1; int64_t flat=0;
            for(j=(int)dims-1; j>=0; --j){
                uint64_t idx=0; int c=int_deserialize((uint8_t*)ptr, size, &idx);
                if(c<=0){ tinybuf_free(buf); tinybuf_free(shape); tinybuf_free(tensor); return c; }
                ptr += c; size -= c; flat += (int64_t)idx * stride; stride *= shape[j];
            }
            if(size<8){ tinybuf_free(buf); tinybuf_free(shape); tinybuf_free(tensor); return 0; }
            double dv = read_double((uint8_t*)ptr); ptr += 8; size -= 8; buf[flat] = dv;
        }
        tensor->data = buf;
    }
    else if(tensor->dtype == 10){
        float *buf = (float*)tinybuf_malloc((int)(prod*4)); memset(buf,0,(size_t)(prod*4));
        for(ii=0; ii<k; ++ii){
            int64_t stride=1; int64_t flat=0;
            for(j=(int)dims-1; j>=0; --j){
                uint64_t idx=0; int c=int_deserialize((uint8_t*)ptr, size, &idx);
                if(c<=0){ tinybuf_free(buf); tinybuf_free(shape); tinybuf_free(tensor); return c; }
                ptr += c; size -= c; flat += (int64_t)idx * stride; stride *= shape[j];
            }
            if(size<4){ tinybuf_free(buf); tinybuf_free(shape); tinybuf_free(tensor); return 0; }
            uint32_t raw = load_be32(ptr); float fv = 0; memcpy(&fv,&raw,4); ptr += 4; size -= 4; buf[flat] = fv;
        }
        tensor->data = buf;
    }
    else if(tensor->dtype == 11){
        uint8_t *buf = (uint8_t*)tinybuf_malloc((int)prod); memset(buf,0,(size_t)prod);
        for(ii=0; ii<k; ++ii){
            int64_t stride=1; int64_t flat=0;
            for(j=(int)dims-1; j>=0; --j){
                uint64_t idx=0; int c=int_deserialize((uint8_t*)ptr, size, &idx);
                if(c<=0){ tinybuf_free(buf); tinybuf_free(shape); tinybuf_free(tensor); return c; }
                ptr += c; size -= c; flat += (int64_t)idx * stride; stride *= shape[j];
            }
            if(size<1) { tinybuf_free(buf); tinybuf_free(shape); tinybuf_free(tensor); return 0; }
            serialize_type tt = (serialize_type)ptr[0]; ptr += 1; size -= 1;
            uint64_t v = 0; int c = int_deserialize((uint8_t*)ptr, size, &v);
            if(c<=0){ tinybuf_free(buf); tinybuf_free(shape); tinybuf_free(tensor); return c; }
            ptr += c; size -= c; buf[flat] = (uint8_t)(v?1:0);
        }
        tensor->data = buf;
    }
    else {
        int64_t *buf = (int64_t*)tinybuf_malloc((int)(prod*(int64_t)sizeof(int64_t)));
        memset(buf,0,(size_t)(prod*(int64_t)sizeof(int64_t)));
        for(ii=0; ii<k; ++ii){
            int64_t stride=1; int64_t flat=0;
            for(j=(int)dims-1; j>=0; --j){
                uint64_t idx=0; int c=int_deserialize((uint8_t*)ptr, size, &idx);
                if(c<=0){ tinybuf_free(buf); tinybuf_free(shape); tinybuf_free(tensor); return c; }
                ptr += c; size -= c; flat += (int64_t)idx * stride; stride *= shape[j];
            }
            if(size<1){ tinybuf_free(buf); tinybuf_free(shape); tinybuf_free(tensor); return 0; }
            serialize_type tt = (serialize_type)ptr[0]; ptr += 1; size -= 1;
            uint64_t v = 0; int c = int_deserialize((uint8_t*)ptr, size, &v);
            if(c<=0){ tinybuf_free(buf); tinybuf_free(shape); tinybuf_free(tensor); return c; }
            ptr += c; size -= c; int64_t sv = (tt==serialize_negtive_int)?-(int64_t)v:(int64_t)v; buf[flat] = sv;
        }
        tensor->data = buf;
    }
    out->_type = tinybuf_tensor; out->_data._custom = tensor; out->_custom_free = NULL;
    return 1;
}
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

    uint64_t cnt=0, dt=0, dims=0, k=0;
    int a=0, b=0, e=0;
    int64_t count=0, prod=1;
    int64_t *shape=NULL;
    tinybuf_tensor_t *tnsr=NULL;

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
        if (s_strpool_offset_read < 0 || s_strpool_base_read == NULL){ s_last_error_msg = "strpool not initialized"; return -1; }
        const char *pool_start = s_strpool_base_read + s_strpool_offset_read;
        const char *q = pool_start;
        int64_t r = ((const char*)ptr + size) - pool_start; // remaining after pool start
        if(r < 1){ return 0; }
        if((uint8_t)q[0] == serialize_str_pool){
            ++q; --r;
            uint64_t cnt = 0; int l2 = int_deserialize((uint8_t *)q, (int)r, &cnt); if(l2 <= 0){ return -1; }
            q += l2; r -= l2;
            if(idx >= cnt){ return -1; }
            for(uint64_t i=0;i<cnt;++i){ if(r < 1){ return 0; } if((uint8_t)q[0] != serialize_string){ return -1; } ++q; --r; uint64_t sl; int l3 = int_deserialize((uint8_t *)q, (int)r, &sl); if(l3 <= 0){ return l3; } q += l3; r -= l3; if(r < (int64_t)sl){ return 0; } if(i == idx){ tinybuf_value_init_string_l(out, q, (int)sl, 0); return 1 + len; } q += sl; r -= sl; }
            return -1;
        } else if((uint8_t)q[0] == 27){
            ++q; --r;
            uint64_t ncount = 0; int l2 = int_deserialize((uint8_t*)q, (int)r, &ncount); if(l2<=0){ return -1; }
            const char *nodes_base = q + l2; int64_t nodes_size = r - l2;
            /* find leaf node with leaf_id == idx */
            const char *p = nodes_base; int64_t rr = nodes_size; int found = -1; for(uint64_t i=0;i<ncount;++i){ uint64_t parent=0; int lp = int_deserialize((uint8_t*)p, (int)rr, &parent); if(lp<=0) return lp; p+=lp; rr-=lp; if(rr<2) return 0; unsigned char ch = (unsigned char)p[0]; unsigned char flag = (unsigned char)p[1]; p+=2; rr-=2; if(flag){ uint64_t leaf=0; int ll = int_deserialize((uint8_t*)p, (int)rr, &leaf); if(ll<=0) return ll; p+=ll; rr-=ll; if(leaf == idx){ found = (int)i; break; } } }
            if(found < 0){ return -1; }
            /* reconstruct path up to root by scanning nodes repeatedly to find parents */
            buffer *tmp = buffer_alloc();
            int current = found;
            while(1){ /* scan to get node 'current' fields */ const char *p2 = nodes_base; int64_t rr2 = nodes_size; int node_index = 0; int parent_index = -1; unsigned char ch = 0; unsigned char flag = 0; uint64_t leaf = 0; while(node_index <= current){ uint64_t parent=0; int lp = int_deserialize((uint8_t*)p2, (int)rr2, &parent); p2+=lp; rr2-=lp; if(rr2<2) return 0; ch = (unsigned char)p2[0]; flag = (unsigned char)p2[1]; p2+=2; rr2-=2; if(flag){ int ll = int_deserialize((uint8_t*)p2, (int)rr2, &leaf); p2+=ll; rr2-=ll; }
                    parent_index = (int)parent; ++node_index;
                }
                if(ch){ buffer_append(tmp, (const char*)&ch, 1); }
                if(parent_index <= 0){ break; }
                current = parent_index;
            }
            /* reverse tmp into output string */
            int tlen = buffer_get_length_inline(tmp); const char *td = buffer_get_data_inline(tmp);
            char *rev = (char*)tinybuf_malloc(tlen);
            for(int i=0;i<tlen;++i){ rev[i] = td[tlen-1-i]; }
            tinybuf_value_init_string_l(out, rev, tlen, 0);
            tinybuf_free(rev); buffer_free(tmp);
            return 1 + len;
        } else { return -1; }
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
        uint64_t cnt=0; int a = int_deserialize((uint8_t*)ptr, size, &cnt); if(a<=0) return a; ptr += a; size -= a;
        int64_t bytes = ((int64_t)cnt + 7) / 8; if(size < bytes) return 0;
        tinybuf_bool_map_t *bm = (tinybuf_bool_map_t*)tinybuf_malloc((int)sizeof(tinybuf_bool_map_t)); bm->count = (int64_t)cnt;
        bm->bits = (uint8_t*)tinybuf_malloc((int)bytes);
        memcpy(bm->bits, ptr, (size_t)bytes);
        out->_type = tinybuf_bool_map; out->_data._custom = bm; out->_custom_free = NULL;
        return 1 + a + (int)bytes;
    }
    case serialize_indexed_tensor:
    {
        const char *p0 = ptr;
        buf_ref br = (buf_ref){ (char*)ptr, (int64_t)size, (char*)ptr, (int64_t)size };
        tinybuf_value *ten = tinybuf_value_alloc(); int r1 = try_read_box(&br, ten, contain_any); if(r1<=0){ tinybuf_value_free(ten); return r1; }
        ptr += r1; size -= r1;
        uint64_t dims=0; int a = int_deserialize((uint8_t*)ptr, size, &dims); if(a<=0){ tinybuf_value_free(ten); return a; }
        ptr += a; size -= a;
        tinybuf_indexed_tensor_t *it = (tinybuf_indexed_tensor_t*)tinybuf_malloc((int)sizeof(tinybuf_indexed_tensor_t)); it->tensor = ten; it->dims = (int)dims; it->indices = NULL;
        if(dims>0){ it->indices = (tinybuf_value**)tinybuf_malloc(sizeof(tinybuf_value*)*(size_t)dims); for(uint64_t i=0;i<dims;++i) it->indices[i] = NULL; }
        for(uint64_t i=0;i<dims;++i){ uint64_t has=0; int c = int_deserialize((uint8_t*)ptr, size, &has); if(c<=0){ /* cleanup */ if(it->indices){ for(uint64_t k=0;k<i;++k){ if(it->indices[k]) tinybuf_value_free(it->indices[k]); } tinybuf_free(it->indices);} tinybuf_value_free(ten); tinybuf_free(it); return c; } ptr+=c; size-=c; if(has){ tinybuf_value *idx = tinybuf_value_alloc(); buf_ref br2 = (buf_ref){ (char*)ptr, (int64_t)size, (char*)ptr, (int64_t)size }; int r2 = try_read_box(&br2, idx, contain_any); if(r2<=0){ tinybuf_value_free(idx); if(it->indices){ for(uint64_t k=0;k<i;++k){ if(it->indices[k]) tinybuf_value_free(it->indices[k]); } tinybuf_free(it->indices);} tinybuf_value_free(ten); tinybuf_free(it); return r2; } ptr += r2; size -= r2; it->indices[i] = idx; } }
        out->_type = tinybuf_indexed_tensor; out->_data._custom = it; out->_custom_free = NULL;
        return 1 + (int)(ptr - p0);
    }
    case serialize_type_idx:
    {
        uint64_t idx=0; int a=int_deserialize((uint8_t*)ptr, size, &idx); if(a<=0) return a; ptr+=a; size-=a;
        uint64_t blen=0; int b=int_deserialize((uint8_t*)ptr, size, &blen); if(b<=0) return b; ptr+=b; size-=b;
        if(size < (int64_t)blen) return 0;
        if (s_strpool_offset_read < 0 || s_strpool_base_read == NULL){ s_last_error_msg = "strpool not initialized"; return -1; }
        const char *pool_start = s_strpool_base_read + s_strpool_offset_read;
        const char *q = pool_start; int64_t r = ((const char*)ptr + size) - pool_start;
        if(r < 1) return 0;
        char *name_out = NULL; int name_len = 0;
        if((uint8_t)q[0] == serialize_str_pool){
            ++q; --r; uint64_t cnt=0; int l2=int_deserialize((uint8_t*)q,(int)r,&cnt); if(l2<=0) return l2; q+=l2; r-=l2; if(idx>=cnt) return -1; for(uint64_t i=0;i<cnt;++i){ if(r<1) return 0; if((uint8_t)q[0]!=serialize_string) return -1; ++q; --r; uint64_t sl=0; int l3=int_deserialize((uint8_t*)q,(int)r,&sl); if(l3<=0) return l3; q+=l3; r-=l3; if(r < (int64_t)sl) return 0; if(i==idx){ name_out=(char*)tinybuf_malloc((int)sl+1); memcpy(name_out,q,(size_t)sl); name_out[sl]='\0'; name_len=(int)sl; break; } q+=sl; r-=sl; }
        } else if((uint8_t)q[0] == 27){
            ++q; --r; uint64_t ncount=0; int l2=int_deserialize((uint8_t*)q,(int)r,&ncount); if(l2<=0) return l2; const char *nodes_base=q+l2; int64_t nodes_size=r-l2; const char *p = nodes_base; int64_t rr = nodes_size; int found=-1; for(uint64_t i=0;i<ncount;++i){ uint64_t parent=0; int lp=int_deserialize((uint8_t*)p,(int)rr,&parent); if(lp<=0) return lp; p+=lp; rr-=lp; if(rr<2) return 0; unsigned char ch=(unsigned char)p[0]; unsigned char flag=(unsigned char)p[1]; p+=2; rr-=2; if(flag){ uint64_t leaf=0; int ll=int_deserialize((uint8_t*)p,(int)rr,&leaf); if(ll<=0) return ll; p+=ll; rr-=ll; if(leaf==(uint64_t)idx){ found=(int)i; break; } } }
            if(found<0) return -1; buffer *tmp=buffer_alloc(); int current=found; while(1){ const char *p2=nodes_base; int64_t rr2=nodes_size; int node_index=0; int parent_index=-1; unsigned char ch=0; unsigned char flag=0; uint64_t leaf=0; while(node_index<=current){ uint64_t parent=0; int lp=int_deserialize((uint8_t*)p2,(int)rr2,&parent); p2+=lp; rr2-=lp; if(rr2<2) return 0; ch=(unsigned char)p2[0]; flag=(unsigned char)p2[1]; p2+=2; rr2-=2; if(flag){ int ll=int_deserialize((uint8_t*)p2,(int)rr2,&leaf); p2+=ll; rr2-=ll; } parent_index=(int)parent; ++node_index; } if(ch){ buffer_append(tmp,(const char*)&ch,1); } if(parent_index<=0) break; current=parent_index; }
            int tlen=buffer_get_length_inline(tmp); const char *td=buffer_get_data_inline(tmp); name_out=(char*)tinybuf_malloc(tlen+1); for(int i=0;i<tlen;++i){ name_out[i]=td[tlen-1-i]; } name_out[tlen]='\0'; name_len=tlen; buffer_free(tmp);
        } else { return -1; }
        if(!name_out) return -1;
        int rr = tinybuf_custom_try_read(name_out, (const uint8_t*)ptr, (int)blen, out, contain_any);
        tinybuf_free(name_out);
        if(rr < 0){
            buf_ref br3 = (buf_ref){ (char*)ptr, (int64_t)blen, (char*)ptr, (int64_t)blen };
            int ir = try_read_box(&br3, out, contain_any);
            if(ir > 0){ return 1 + a + b + ir; }
            return rr;
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

 
// 高级序列化read入口
// 二级版本 可处理二级格式 readbox标准 成功则修改buf指针 返回>0 否则不修改并返回<=0

#define SET_FAILED(s) (reason = s, s_last_error_msg = s, failed = TRUE)
#define SET_SUCCESS() (failed = FALSE, reason = NULL, s_last_error_msg = NULL)
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

const char *tinybuf_last_error_message(void){ return s_last_error_msg; }

static inline offset_pool_entry *pool_find(int64_t offset){
    pool_lock();
    for(int i=0;i<s_pool_count;++i){
        if(s_pool[i].offset == offset){
            offset_pool_entry *ret = &s_pool[i]; pool_unlock(); return ret;
        }
    }
    pool_unlock(); return NULL;
}

static offset_pool_entry *pool_register(int64_t offset, tinybuf_value *value){
    pool_lock();
    offset_pool_entry *e = NULL; for(int i=0;i<s_pool_count;++i){ if(s_pool[i].offset==offset){ e=&s_pool[i]; break; } }
    if(e){
        if(value){ e->value = value; }
        pool_unlock(); return e;
    }
    if(s_pool_count == s_pool_capacity){
        int newcap = s_pool_capacity ? (s_pool_capacity * 2) : 16;
        s_pool = (offset_pool_entry *)tinybuf_realloc(s_pool, sizeof(offset_pool_entry) * newcap);
        s_pool_capacity = newcap;
    }
    s_pool[s_pool_count].offset = offset;
    s_pool[s_pool_count].value = value;
    s_pool[s_pool_count].complete = 0;
    offset_pool_entry *ret = &s_pool[s_pool_count++]; pool_unlock(); return ret;
}

static inline void pool_mark_complete(int64_t offset){
    pool_lock();
    offset_pool_entry *e = NULL; for(int i=0;i<s_pool_count;++i){ if(s_pool[i].offset==offset){ e=&s_pool[i]; break; } }
    if(e){ e->complete = 1; }
    pool_unlock();
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
            QWORD list_len;
            if (OK_AND_ADDTO(try_read_int_data(FALSE, buf, &list_len), &len))
            {
                int found = 0;
                for (QWORD i = 0; i < list_len; i++)
                {
                    QWORD version = 0;
                    if (OK_AND_ADDTO(try_read_int_data(FALSE, buf, &version), &len))
                    {
                        if (target_version(version))
                        {
                            int inner = try_read_box(buf, out, target_version);
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
                            int consumed = try_read_box(buf, skip, target_version);
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

        case 26:
        {
            QWORD cnt = 0;
            if(OK_AND_ADDTO(try_read_int_data(FALSE, buf, &cnt), &len))
            {
                const char **guids = (const char**)tinybuf_malloc(sizeof(const char*) * cnt);
                for(QWORD i=0;i<cnt;++i){ if(buf->size<=0) { SET_FAILED("plugin map truncated"); break; } if((uint8_t)buf->ptr[0] != serialize_string){ SET_FAILED("plugin map entry not string"); break; } buf_offset(buf,1); len += 1; QWORD sl=0; int l3 = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &sl); if(l3<=0){ SET_FAILED("plugin map string len"); break; } buf_offset(buf,l3); len += l3; if(buf->size < (int64_t)sl){ SET_FAILED("plugin map string body"); break; } char *g = (char*)tinybuf_malloc((int)sl+1); memcpy(g, buf->ptr, (size_t)sl); g[sl] = '\0'; guids[i] = g; buf_offset(buf,(int)sl); len += (int)sl; }
                tinybuf_plugin_set_runtime_map(guids, (int)cnt);
                for(QWORD i=0;i<cnt;++i){ tinybuf_free((void*)guids[i]); }
                tinybuf_free(guids);
                int inner = tinybuf_try_read_box_with_plugins(buf, out, target_version);
                if(inner > 0){ len += inner; SET_SUCCESS(); break; }
                SET_FAILED("plugin map followed by invalid box"); break;
            }
            SET_FAILED("plugin map header failed"); break;
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
        case serialize_type_idx:
        {
            QWORD idx=0; if(!OK_AND_ADDTO(try_read_int_data(FALSE, buf, &idx), &len)){ SET_FAILED("read type_idx index failed"); break; }
            QWORD blen=0; if(!OK_AND_ADDTO(try_read_int_data(FALSE, buf, &blen), &len)){ SET_FAILED("read type_idx length failed"); break; }
            if(buf->size < (int64_t)blen){ SET_FAILED("payload too small"); break; }
            if (s_strpool_offset_read < 0 || s_strpool_base_read == NULL){ s_last_error_msg = "strpool not initialized"; SET_FAILED("strpool not initialized"); break; }
            const char *pool_start = s_strpool_base_read + s_strpool_offset_read;
            const char *q = pool_start; int64_t r = ((const char*)buf->ptr + buf->size) - pool_start;
            char *name_out = NULL; int name_len = 0;
            if(r > 0){
                if((uint8_t)q[0] == serialize_str_pool){
                    ++q; --r; QWORD cnt=0; int l2=try_read_int_tovar(FALSE,q,(int)r,&cnt); if(l2>0){ q+=l2; r-=l2; for(QWORD i=0;i<cnt;++i){ if(r<1) break; if((uint8_t)q[0]!=serialize_string) break; ++q; --r; QWORD sl=0; int l3=try_read_int_tovar(FALSE,q,(int)r,&sl); if(l3<=0) break; q+=l3; r-=l3; if(r < (int64_t)sl) break; if(i==idx){ name_out=(char*)tinybuf_malloc((int)sl+1); memcpy(name_out,q,(size_t)sl); name_out[sl]='\0'; name_len=(int)sl; break; } q+=sl; r-=sl; } }
                } else if((uint8_t)q[0] == 27){
                    ++q; --r; QWORD ncount=0; int l2=try_read_int_tovar(FALSE,q,(int)r,&ncount); if(l2>0){ const char *nodes_base=q+l2; int64_t nodes_size=r-l2; const char *p = nodes_base; int64_t rr = nodes_size; int found=-1; for(QWORD i=0;i<ncount;++i){ QWORD parent=0; int lp=try_read_int_tovar(FALSE,p,(int)rr,&parent); if(lp<=0) break; p+=lp; rr-=lp; if(rr<2) break; unsigned char ch=(unsigned char)p[0]; unsigned char flag=(unsigned char)p[1]; p+=2; rr-=2; if(flag){ QWORD leaf=0; int ll=try_read_int_tovar(FALSE,p,(int)rr,&leaf); if(ll<=0) break; p+=ll; rr-=ll; if(leaf==(QWORD)idx){ found=(int)i; break; } } }
                        if(found>=0){ buffer *tmp=buffer_alloc(); int current=found; while(1){ const char *p2=nodes_base; int64_t rr2=nodes_size; int node_index=0; int parent_index=-1; unsigned char ch=0; unsigned char flag=0; QWORD leaf=0; while(node_index<=current){ QWORD parent=0; int lp=try_read_int_tovar(FALSE,p2,(int)rr2,&parent); p2+=lp; rr2-=lp; if(rr2<2) break; ch=(unsigned char)p2[0]; flag=(unsigned char)p2[1]; p2+=2; rr2-=2; if(flag){ int ll=try_read_int_tovar(FALSE,p2,(int)rr2,&leaf); p2+=ll; rr2-=ll; } parent_index=(int)parent; ++node_index; } if(ch){ buffer_append(tmp,(const char*)&ch,1); } if(parent_index<=0) break; current=parent_index; }
                            int tlen=buffer_get_length_inline(tmp); const char *td=buffer_get_data_inline(tmp); name_out=(char*)tinybuf_malloc(tlen+1); for(int i=0;i<tlen;++i){ name_out[i]=td[tlen-1-i]; } name_out[tlen]='\0'; name_len=tlen; buffer_free(tmp);
                        }
                    }
                }
            }
            if(name_out){
                int ir = tinybuf_custom_try_read(name_out, (const uint8_t*)buf->ptr, (int)blen, out, contain_any);
                if(ir > 0){ buf_offset(buf, (int)blen); len += (int)blen; pool_mark_complete(box_offset); SET_SUCCESS(); }
                else{
                    buf_ref br3 = (buf_ref){ (char*)buf->ptr, (int64_t)blen, (char*)buf->ptr, (int64_t)blen };
                    int ir2 = try_read_box(&br3, out, contain_any);
                    if(ir2 > 0){ buf_offset(buf, ir2); len += ir2; pool_mark_complete(box_offset); SET_SUCCESS(); }
                    else { SET_FAILED("custom read failed"); }
                }
                tinybuf_free(name_out);
            } else {
                SET_FAILED("type_idx name not found");
            }
            if(!failed){ READ_RETURN }
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

static inline int try_write_plugin_map_table(buffer *out){
    int before = buffer_get_length_inline(out);
    int pc = tinybuf_plugin_get_count();
    try_write_type(out, 26);
    try_write_int_data(FALSE, out, (QWORD)pc);
    for(int i=0;i<pc;++i){ const char *g = tinybuf_plugin_get_guid(i); if(!g) g=""; int gl = (int)strlen(g); try_write_type(out, serialize_string); try_write_int_data(FALSE, out, (QWORD)gl); if(gl){ buffer_append(out, g, gl); } }
    int after = buffer_get_length_inline(out);
    return after - before;
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
    if (buf->ptr == buf->base && buf->size >= 1 && (uint8_t)buf->base[0] == serialize_str_pool_table)
    {
        s_strpool_base_read = buf->base; s_strpool_offset_read = -1;
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

tinybuf_result tinybuf_try_read_box_r(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler)
{
    int r = tinybuf_try_read_box(buf, out, contain_handler);
    if (r <= 0){ int alt = tinybuf_value_deserialize(buf->ptr, buf->size, out); if(alt > 0){ return tinybuf_result_ok(alt); } }
    if (r > 0) { return tinybuf_result_ok(r); }
    if(buf && buf->size>0 && (uint8_t)buf->ptr[0] == serialize_type_idx){
        const char *p = buf->ptr + 1; int64_t sz = buf->size - 1;
        QWORD idx=0; int a=try_read_int_tovar(FALSE,p,(int)sz,&idx); if(a>0){ p+=a; sz-=a; QWORD blen=0; int b=try_read_int_tovar(FALSE,p,(int)sz,&blen); if(b>0){ p+=b; sz-=b; if(sz >= (int64_t)blen && s_strpool_offset_read>=0 && s_strpool_base_read){ const char *q = s_strpool_base_read + s_strpool_offset_read; int64_t rsz = ((const char*)buf->ptr + buf->size) - q; char *name_out=NULL; if(rsz>0){ if((uint8_t)q[0]==serialize_str_pool){ ++q; --rsz; QWORD cnt=0; int l2=try_read_int_tovar(FALSE,q,(int)rsz,&cnt); if(l2>0){ q+=l2; rsz-=l2; for(QWORD i=0;i<cnt;++i){ if(rsz<1) break; if((uint8_t)q[0]!=serialize_string) break; ++q; --rsz; QWORD sl=0; int l3=try_read_int_tovar(FALSE,q,(int)rsz,&sl); if(l3<=0) break; q+=l3; rsz-=l3; if(rsz < (int64_t)sl) break; if(i==idx){ name_out=(char*)tinybuf_malloc((int)sl+1); memcpy(name_out,q,(size_t)sl); name_out[sl]='\0'; break; } q+=sl; rsz-=sl; } } }
                                else if((uint8_t)q[0]==27){ ++q; --rsz; QWORD ncount=0; int l2=try_read_int_tovar(FALSE,q,(int)rsz,&ncount); if(l2>0){ const char *nodes_base=q+l2; int64_t nodes_size=rsz-l2; const char *pp=nodes_base; int64_t rr=nodes_size; int found=-1; for(QWORD i=0;i<ncount;++i){ QWORD parent=0; int lp=try_read_int_tovar(FALSE,pp,(int)rr,&parent); if(lp<=0) break; pp+=lp; rr-=lp; if(rr<2) break; unsigned char ch=(unsigned char)pp[0]; unsigned char flag=(unsigned char)pp[1]; pp+=2; rr-=2; if(flag){ QWORD leaf=0; int ll=try_read_int_tovar(FALSE,pp,(int)rr,&leaf); if(ll<=0) break; pp+=ll; rr-=ll; if(leaf==(QWORD)idx){ found=(int)i; break; } } }
                                        if(found>=0){ buffer *tmp=buffer_alloc(); int current=found; while(1){ const char *p2=nodes_base; int64_t rr2=nodes_size; int node_index=0; int parent_index=-1; unsigned char ch=0; unsigned char flag=0; QWORD leaf=0; while(node_index<=current){ QWORD parent=0; int lp=try_read_int_tovar(FALSE,p2,(int)rr2,&parent); p2+=lp; rr2-=lp; if(rr2<2) break; ch=(unsigned char)p2[0]; flag=(unsigned char)p2[1]; p2+=2; rr2-=2; if(flag){ int ll=try_read_int_tovar(FALSE,p2,(int)rr2,&leaf); p2+=ll; rr2-=ll; } parent_index=(int)parent; ++node_index; } if(ch){ buffer_append(tmp,(const char*)&ch,1); } if(parent_index<=0) break; current=parent_index; }
                                            int tlen=buffer_get_length_inline(tmp); const char *td=buffer_get_data_inline(tmp); name_out=(char*)tinybuf_malloc(tlen+1); for(int i=0;i<tlen;++i){ name_out[i]=td[tlen-1-i]; } name_out[tlen]='\0'; buffer_free(tmp); }
                                    }
                                }
                if(name_out){ int ir=tinybuf_custom_try_read(name_out,(const uint8_t*)p,(int)blen,out,contain_handler); tinybuf_free(name_out); if(ir>0){ return tinybuf_result_ok(1 + a + b + (int)blen); } }
            } }
        /* fallback failed */
    }
    const char *msg = tinybuf_last_error_message();
    if(!msg) msg = (r == 0) ? "buffer too small" : "read failed";
    tinybuf_result rr = tinybuf_result_err(r, msg, NULL);
    /* add context */ tinybuf_result_add_msg_const(&rr, "tinybuf_try_read_box failed");
    if(buf && buf->size>0){ char *m=(char*)tinybuf_malloc(64); snprintf(m,64,"head_type=%u",(unsigned)(uint8_t)buf->ptr[0]); tinybuf_result_add_msg(&rr,m,(tinybuf_deleter_fn)tinybuf_free); }
    { char *m2=(char*)tinybuf_malloc(64); snprintf(m2,64,"strpool_off=%lld",(long long)s_strpool_offset_read); tinybuf_result_add_msg(&rr,m2,(tinybuf_deleter_fn)tinybuf_free); }
    { char *m3=(char*)tinybuf_malloc(64); snprintf(m3,64,"plugins=%d",tinybuf_plugin_get_count()); tinybuf_result_add_msg(&rr,m3,(tinybuf_deleter_fn)tinybuf_free); }
    return rr;
}

tinybuf_result tinybuf_try_write_box_r(buffer *out, const tinybuf_value *value)
{
    int r = tinybuf_try_write_box(out, value);
    if (r > 0) { return tinybuf_result_ok(r); }
    tinybuf_result rr = tinybuf_result_err(r, "write failed", NULL);
    tinybuf_result_add_msg_const(&rr, "tinybuf_try_write_box failed");
    return rr;
}

int tinybuf_try_write_version_box(buffer *out, QWORD version, const tinybuf_value *box)
{
    return try_write_version_box(out, version, box);
}

int tinybuf_try_write_version_list(buffer *out, const QWORD *versions, const tinybuf_value **boxes, int count)
{
    return try_write_version_list(out, versions, boxes, count);
}

int tinybuf_try_write_plugin_map_table(buffer *out){ return try_write_plugin_map_table(out); }

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
int tinybuf_try_write_custom_id_box(buffer *out, const char *name, const tinybuf_value *in){
    buffer *body = buffer_alloc(); buffer *pool = buffer_alloc(); strpool_reset_write(body);
    int idx = strpool_add(name, (int)strlen(name)); uint8_t ty = serialize_type_idx; buffer_append(body, (const char*)&ty, 1); dump_int((uint64_t)idx, body);
    buffer *payload = buffer_alloc(); int plen = tinybuf_custom_try_write(name, in, payload); dump_int((uint64_t)plen, body); if(plen>0){ buffer_append(body, buffer_get_data_inline(payload), buffer_get_length_inline(payload)); }
    strpool_write_tail(pool);
    uint64_t body_len = (uint64_t)buffer_get_length_inline(body);
    uint64_t offset_guess_len = 1; uint8_t tmpv[16]; while(1){ uint64_t off = 1 + offset_guess_len + body_len; int l = int_serialize(off, tmpv); if((uint64_t)l == offset_guess_len) break; offset_guess_len = (uint64_t)l; }
    uint64_t final_off = 1 + offset_guess_len + body_len;
    try_write_type(out, serialize_str_pool_table); try_write_int_data(FALSE, out, final_off);
    buffer_append(out, buffer_get_data_inline(body), (int)body_len);
    int pool_len = buffer_get_length_inline(pool); if(pool_len){ buffer_append(out, buffer_get_data_inline(pool), pool_len); }
    buffer_free(payload); buffer_free(body); buffer_free(pool);
    return 1 + (int)offset_guess_len + (int)body_len + pool_len;
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
        memcpy(ret, value, sizeof(tinybuf_value));
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

int tinybuf_tensor_get_dtype(const tinybuf_value *value){ if(!value || value->_type != tinybuf_tensor) return 0; tinybuf_tensor_t *t=(tinybuf_tensor_t*)value->_data._custom; return t? t->dtype : 0; }
int tinybuf_tensor_get_ndim(const tinybuf_value *value){ if(!value || value->_type != tinybuf_tensor) return 0; tinybuf_tensor_t *t=(tinybuf_tensor_t*)value->_data._custom; return t? t->dims : 0; }
const int64_t* tinybuf_tensor_get_shape(const tinybuf_value *value){ if(!value || value->_type != tinybuf_tensor) return NULL; tinybuf_tensor_t *t=(tinybuf_tensor_t*)value->_data._custom; return t? t->shape : NULL; }
int64_t tinybuf_tensor_get_count(const tinybuf_value *value){ if(!value || value->_type != tinybuf_tensor) return 0; tinybuf_tensor_t *t=(tinybuf_tensor_t*)value->_data._custom; return t? t->count : 0; }
void* tinybuf_tensor_get_data(tinybuf_value *value){ if(!value || value->_type != tinybuf_tensor) return NULL; tinybuf_tensor_t *t=(tinybuf_tensor_t*)value->_data._custom; return t? t->data : NULL; }
const void* tinybuf_tensor_get_data_const(const tinybuf_value *value){ if(!value || value->_type != tinybuf_tensor) return NULL; tinybuf_tensor_t *t=(tinybuf_tensor_t*)value->_data._custom; return t? t->data : NULL; }

int tinybuf_value_init_tensor(tinybuf_value *value, int dtype, const int64_t *shape, int dims, const void *data, int64_t elem_count){ if(!value || !shape || dims<=0 || elem_count<0) return -1; tinybuf_value_clear(value); tinybuf_tensor_t *t=(tinybuf_tensor_t*)tinybuf_malloc(sizeof(tinybuf_tensor_t)); t->dtype=dtype; t->dims=dims; t->shape=(int64_t*)tinybuf_malloc(sizeof(int64_t)*(size_t)dims); for(int i=0;i<dims;++i){ t->shape[i]=shape[i]; } t->count=elem_count; size_t bytes=0; if(dtype==8){ bytes=(size_t)(elem_count*8); } else if(dtype==10){ bytes=(size_t)(elem_count*4); } else if(dtype==11){ bytes=(size_t)(elem_count); } else { bytes=(size_t)(elem_count*sizeof(int64_t)); } t->data=tinybuf_malloc(bytes); if(data && bytes){ memcpy(t->data, data, bytes); } value->_type=tinybuf_tensor; value->_data._custom=t; value->_custom_free=NULL; return 0; }

int tinybuf_value_init_bool_map(tinybuf_value *value, const uint8_t *bits, int64_t count){ if(!value || count<0) return -1; tinybuf_value_clear(value); tinybuf_bool_map_t *bm=(tinybuf_bool_map_t*)tinybuf_malloc(sizeof(tinybuf_bool_map_t)); bm->count=count; int64_t bytes=(count+7)/8; bm->bits=(uint8_t*)tinybuf_malloc((int)bytes); if(bits && bytes>0){ memcpy(bm->bits, bits, (size_t)bytes); } else if(bytes>0){ memset(bm->bits, 0, (size_t)bytes); } value->_type=tinybuf_bool_map; value->_data._custom=bm; value->_custom_free=NULL; return 0; }
int64_t tinybuf_bool_map_get_count(const tinybuf_value *value){ if(!value || value->_type!=tinybuf_bool_map) return 0; tinybuf_bool_map_t *bm=(tinybuf_bool_map_t*)value->_data._custom; return bm? bm->count : 0; }
const uint8_t* tinybuf_bool_map_get_bits_const(const tinybuf_value *value){ if(!value || value->_type!=tinybuf_bool_map) return NULL; tinybuf_bool_map_t *bm=(tinybuf_bool_map_t*)value->_data._custom; return bm? bm->bits : NULL; }

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
    int tinybuf_plugins_try_dump_by_type(uint8_t type, buf_ref *buf, buffer *out);
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
        case serialize_vector_tensor:
        {
            QWORD cnt=0; int a=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &cnt); if(a<=0) return a; buf_offset(buf,a); consumed+=a; QWORD dt=0; int b=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &dt); if(b<=0) return b; buf_offset(buf,b); consumed+=b; append_cstr(out, "tensor(shape=["); append_int_dec(out,(int64_t)cnt); append_cstr(out, "], dtype="); append_int_dec(out,(int64_t)dt); append_cstr(out, ", count="); append_int_dec(out,(int64_t)cnt); append_cstr(out, ")"); if((int64_t)dt==8){ int64_t need=(int64_t)cnt*8; if(buf->size<need) return 0; buf_offset(buf,need); consumed+=(int)need; } else if((int64_t)dt==10){ int64_t need=(int64_t)cnt*4; if(buf->size<need) return 0; buf_offset(buf,need); consumed+=(int)need; } else if((int64_t)dt==11){ int64_t need=((int64_t)cnt + 7) / 8; if(buf->size<need) return 0; buf_offset(buf,(int)need); consumed+=(int)need; } else { for(QWORD i=0;i<cnt;++i){ serialize_type t2=(serialize_type)buf->ptr[0]; buf_offset(buf,1); consumed+=1; QWORD v=0; int c=try_read_int_tovar(t2==serialize_negtive_int, buf->ptr, (int)buf->size, &v); if(c<=0) return c; buf_offset(buf,c); consumed+=c; } } return consumed;
        }
        case serialize_dense_tensor:
        {
            QWORD dims=0; int a=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &dims); if(a<=0) return a; buf_offset(buf,a); consumed+=a; int64_t prod=1; for(QWORD i=0;i<dims;++i){ QWORD d=0; int c=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &d); if(c<=0) return c; buf_offset(buf,c); consumed+=c; prod *= (int64_t)d; } QWORD dt=0; int b=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &dt); if(b<=0) return b; buf_offset(buf,b); consumed+=b; append_cstr(out, "tensor(shape=["); for(QWORD i=0;i<dims;++i){ if(i) append_cstr(out, ","); } append_cstr(out, "], dtype="); append_int_dec(out,(int64_t)dt); append_cstr(out, ", count="); append_int_dec(out,prod); append_cstr(out, ")"); if((int64_t)dt==8){ int64_t need=prod*8; if(buf->size<need) return 0; buf_offset(buf,need); consumed+=(int)need; } else if((int64_t)dt==10){ int64_t need=prod*4; if(buf->size<need) return 0; buf_offset(buf,need); consumed+=(int)need; } else if((int64_t)dt==11){ int64_t need=(prod + 7) / 8; if(buf->size<need) return 0; buf_offset(buf,(int)need); consumed+=(int)need; } else { for(int64_t i=0;i<prod;++i){ serialize_type t2=(serialize_type)buf->ptr[0]; buf_offset(buf,1); consumed+=1; QWORD v=0; int c=try_read_int_tovar(t2==serialize_negtive_int, buf->ptr, (int)buf->size, &v); if(c<=0) return c; buf_offset(buf,c); consumed+=c; } } return consumed;
        }
        case serialize_indexed_tensor:
        {
            tinybuf_value tmp; memset(&tmp,0,sizeof(tmp));
            buf_ref hb = *buf; int r1 = try_read_box(&hb, &tmp, contain_any); if(r1<=0) return r1; buf_offset(buf,r1); consumed+=r1; tinybuf_value_free(&tmp);
            QWORD dims=0; int a=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &dims); if(a<=0) return a; buf_offset(buf,a); consumed+=a;
            append_cstr(out, "indexed_tensor(dims="); append_int_dec(out,(int64_t)dims); append_cstr(out, ")");
            for(QWORD i=0;i<dims;++i){ QWORD has=0; int c=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &has); if(c<=0) return c; buf_offset(buf,c); consumed+=c; if(has){ tinybuf_value tmp2; memset(&tmp2,0,sizeof(tmp2)); buf_ref hb2 = *buf; int r2 = try_read_box(&hb2, &tmp2, contain_any); if(r2<=0) return r2; buf_offset(buf,r2); consumed+=r2; tinybuf_value_free(&tmp2);} }
            return consumed;
        }
        case serialize_type_idx:
        {
            QWORD idx=0; int a=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &idx); if(a<=0) return a; buf_offset(buf,a); consumed+=a; QWORD blen=0; int b=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &blen); if(b<=0) return b; buf_offset(buf,b); consumed+=b; const char *pool_start = s_strpool_base_read + s_strpool_offset_read; const char *q = pool_start; int64_t r = ((const char*)buf->ptr + buf->size) - pool_start; char *name_out=NULL; int name_len=0; if(r>0){ if((uint8_t)q[0]==serialize_str_pool){ ++q; --r; QWORD cnt=0; int l2=try_read_int_tovar(FALSE,q,(int)r,&cnt); if(l2>0){ q+=l2; r-=l2; for(QWORD i=0;i<cnt;++i){ if(r<1) break; if((uint8_t)q[0]!=serialize_string) break; ++q; --r; QWORD sl=0; int l3=try_read_int_tovar(FALSE,q,(int)r,&sl); if(l3<=0) break; q+=l3; r-=l3; if(r < (int64_t)sl) break; if(i==idx){ name_out=(char*)tinybuf_malloc((int)sl+1); memcpy(name_out,q,(size_t)sl); name_out[sl]='\0'; name_len=(int)sl; break; } q+=sl; r-=sl; } } } }
            if(name_out){ buffer_append(out, "custom:", 7); buffer_append(out, name_out, name_len); tinybuf_free(name_out); }
            if((int64_t)blen > buf->size) return 0; buf_offset(buf, (int)blen); consumed += (int)blen; break;
        }
        case serialize_bool_map:
        {
            QWORD cnt=0; int a=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &cnt); if(a<=0) return a; buf_offset(buf,a); consumed+=a; int64_t need=((int64_t)cnt + 7) / 8; if(buf->size<need) return 0; buf_offset(buf,(int)need); consumed+=(int)need; append_cstr(out, "bool_map(count="); append_int_dec(out,(int64_t)cnt); append_cstr(out, ")"); return consumed;
        }
        case serialize_sparse_tensor:
        {
            QWORD dims=0; int a=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &dims); if(a<=0) return a; buf_offset(buf,a); consumed+=a; for(QWORD i=0;i<dims;++i){ QWORD d=0; int c=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &d); if(c<=0) return c; buf_offset(buf,c); consumed+=c; } QWORD dt=0; int b=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &dt); if(b<=0) return b; buf_offset(buf,b); consumed+=b; QWORD k=0; int e=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &k); if(e<=0) return e; buf_offset(buf,e); consumed+=e; append_cstr(out, "tensor(sparse, entries="); append_int_dec(out,(int64_t)k); append_cstr(out, ")"); for(QWORD i=0;i<k;++i){ for(QWORD j=0;j<dims;++j){ QWORD idx=0; int c=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &idx); if(c<=0) return c; buf_offset(buf,c); consumed+=c; } if((int64_t)dt==8){ if(buf->size<8) return 0; buf_offset(buf,8); consumed+=8; } else if((int64_t)dt==10){ if(buf->size<4) return 0; buf_offset(buf,4); consumed+=4; } else { serialize_type t2=(serialize_type)buf->ptr[0]; buf_offset(buf,1); consumed+=1; QWORD v=0; int c=try_read_int_tovar(t2==serialize_negtive_int, buf->ptr, (int)buf->size, &v); if(c<=0) return c; buf_offset(buf,c); consumed+=c; } } return consumed;
        }
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
        case 26: /* plugin map table */
        {
            QWORD cnt=0; int a=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &cnt); if(a<=0) return a; consumed+=a; buf_offset(buf,a);
            append_cstr(out, "{\"plugins\":[");
            for(QWORD i=0;i<cnt;++i){
                int64_t size = buf->size; if(size < 1) return 0; uint8_t t2 = (uint8_t)buf->ptr[0]; if(t2 != serialize_string) return -1; buf_offset(buf,1); consumed+=1; QWORD sl=0; int l3 = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &sl); if(l3<=0) return l3; buf_offset(buf,l3); consumed+=l3; if(buf->size < (int64_t)sl) return 0; buffer_append(out, "\"", 1); buffer_append(out, buf->ptr, (int)sl); buffer_append(out, "\"", 1); buf_offset(buf,(int)sl); consumed+=(int)sl; if(i+1<cnt) append_cstr(out, ","); }
            append_cstr(out, "]}");
            break;
        }
        default:
        {
            uint8_t raw = (uint8_t)t;
            int a = tinybuf_plugins_try_dump_by_type(raw, buf, out);
            if(a>0){ consumed += a; }
            else{ append_cstr(out, "<unknown>"); }
            break;
        }
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
        case serialize_type_idx:
        {
            QWORD idx=0; int a=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &idx); if(a<=0) return a; consumed+=a; buf_offset(buf,a); QWORD blen=0; int b=try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &blen); if(b<=0) return b; consumed+=b; buf_offset(buf,b); if(buf->size < (int64_t)blen) return 0; buf_offset(buf,(int)blen); consumed+=(int)blen; break;
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
#ifdef _WIN32
static volatile LONG s_pool_lock_var = 0;
static inline void pool_lock(){ int spins=0; while(InterlockedCompareExchange(&s_pool_lock_var,1,0)!=0){ if(spins<64){ ++spins; } else if(spins<1024){ Sleep(0); ++spins; } else { Sleep(1); } } }
static inline void pool_unlock(){ InterlockedExchange(&s_pool_lock_var,0); }
#else
static atomic_flag s_pool_lock_var = ATOMIC_FLAG_INIT;
static inline void pool_lock(){ int spins=0; while(atomic_flag_test_and_set(&s_pool_lock_var)){ if(spins<64){ ++spins; } else if(spins<1024){ sched_yield(); ++spins; } else { struct timespec ts={0,1000000}; nanosleep(&ts,NULL); } } }
static inline void pool_unlock(){ atomic_flag_clear(&s_pool_lock_var); }
#endif
static void tinybuf_indexed_tensor_free(void *p){ if(!p) return; tinybuf_indexed_tensor_t *it=(tinybuf_indexed_tensor_t*)p; if(it->tensor) tinybuf_value_free(it->tensor); if(it->indices){ for(int i=0;i<it->dims;++i){ if(it->indices[i]) tinybuf_value_free(it->indices[i]); } tinybuf_free(it->indices);} tinybuf_free(it); }
int tinybuf_value_init_indexed_tensor(tinybuf_value *value, const tinybuf_value *tensor, const tinybuf_value **indices, int dims){ if(!value || !tensor || dims<0) return -1; tinybuf_value_clear(value); tinybuf_indexed_tensor_t *it=(tinybuf_indexed_tensor_t*)tinybuf_malloc(sizeof(tinybuf_indexed_tensor_t)); it->dims=dims; it->tensor=tinybuf_value_clone(tensor); it->indices=NULL; if(dims>0){ it->indices=(tinybuf_value**)tinybuf_malloc(sizeof(tinybuf_value*)*(size_t)dims); for(int i=0;i<dims;++i){ it->indices[i] = indices ? (indices[i] ? tinybuf_value_clone(indices[i]) : NULL) : NULL; } } value->_type=tinybuf_indexed_tensor; value->_data._custom=it; value->_custom_free=tinybuf_indexed_tensor_free; return 0; }
static int contain_any(uint64_t v){ (void)v; return 1; }
const tinybuf_value* tinybuf_indexed_tensor_get_tensor_const(const tinybuf_value *value){ if(!value || value->_type!=tinybuf_indexed_tensor) return NULL; tinybuf_indexed_tensor_t *it=(tinybuf_indexed_tensor_t*)value->_data._custom; return it? it->tensor : NULL; }
const tinybuf_value* tinybuf_indexed_tensor_get_index_const(const tinybuf_value *value, int dim){ if(!value || value->_type!=tinybuf_indexed_tensor) return NULL; tinybuf_indexed_tensor_t *it=(tinybuf_indexed_tensor_t*)value->_data._custom; if(!it || dim<0 || dim>=it->dims) return NULL; return it->indices ? it->indices[dim] : NULL; }
