#ifndef TINYBUF_PRIVATE_H
#define TINYBUF_PRIVATE_H

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "avl-tree.h"
#include "tinybuf.h"
#include "tinybuf_memory.h"
#include "tinybuf_common.h"
#include "tinybuf_log.h"
#include "tinybuf_buffer_private.h"
#include <stdbool.h>
typedef void (*free_handler)(void *);
// 动态值表示
struct T_tinybuf_value
{
    union
    {
        int64_t _int;
        int _bool;
        double _double;
        buffer *_string;     // 变长缓冲区
        AVLTree *_map_array; // kvpairs versionlist也会使用此字段保存不同版本的buf引用
        void *_custom;       // 自定义类型指针 支持任何struct
        tinybuf_value *_ref; // 引用类型指针 value_ref version都会使用此字段
    } _data;
    // 自定义类型的释放函数 不存在时为NULL 表示直接free
    free_handler _custom_free;
    tinybuf_type _type;
    int _plugin_index;
    int _custom_box_type;
};

// internal types for tensor and advanced values
typedef struct
{
    int dtype;
    int dims;
    int64_t *shape;
    void *data;
    int64_t count;
} tinybuf_tensor_t;
typedef struct
{
    int64_t count;
    uint8_t *bits;
} tinybuf_bool_map_t;
typedef struct
{
    tinybuf_value *tensor;
    tinybuf_value **indices;
    int dims;
} tinybuf_indexed_tensor_t;

// serialize type markers
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
    serialize_pointer_from_current_n = 9,
    serialize_pointer_from_start_n = 10,
    serialize_pointer_from_end_n = 11,
    serialize_pointer_from_current_p = 12,
    serialize_pointer_from_start_p = 13,
    serialize_pointer_from_end_p = 14,
    serialize_pre_cache = 15,
    serialize_version = 16,
    serialize_boxlist = 17,
    serialize_zip_kvpairs = 18,
    serialize_zip_kvpairs_boxkey = 19,
    serialize_version_list = 20,
    serialize_part = 21,
    serialize_part_table = 22,
    serialize_str_index = 23,
    serialize_str_pool = 24,
    serialize_str_pool_table = 25,
    serialize_uri = 26,
    serialize_router_link = 27,
    serialize_text_ref = 28,
    serialize_bin_ref = 29,
    serialize_embed_file = 30,
    serialize_file_range = 31,
    serialize_with_metadata = 32,
    serialize_noseq_part = 33,
    serialize_empty_part = 34,
    serialize_fs = 35,
    serialize_file_table = 36,
    serialize_fs_file = 37,
    serialize_fs_inode = 38,
    serialize_flat_part = 39,
    serialize_pointer_advance = 40,
    serialize_empty_table = 41,
    serialize_sub_ref = 42,
    serialize_vector_tensor = 43,
    serialize_dense_tensor = 44,
    serialize_sparse_tensor = 45,
    serialize_bool_map = 46,
    serialize_indexed_tensor = 47,
    serialize_type_idx = 48,
    serialize_extern_str_idx = 253,
    serialize_extern_str = 254,
    serialize_extern_int = 255
} serialize_type;

// pointer offset addressing types (shared)
enum offset_type
{
    start = 0,
    end = 1,
    current = 2,
    parent_flat_start = 3,
    parent_flat_end = 4
};
typedef struct
{
    int64_t offset;
    enum offset_type type;
} pointer_value;

// common basic types used across compilation units
typedef uint64_t QWORD;
typedef int64_t SQWORD;
typedef int BOOL;
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

// internal write helpers used across modules
tinybuf_error try_write_type(buffer *out, serialize_type type);
tinybuf_error try_write_int_data(int isneg, buffer *out, uint64_t val);
int try_write_pointer_value(buffer *out, enum offset_type t, int64_t offset, tinybuf_error *r);
int tinybuf_value_serialize(const tinybuf_value *value, buffer *out, tinybuf_error *r);
int dump_int(uint64_t len, buffer *out);

// internal read helpers used across modules
int buf_offset(buf_ref *buf, int64_t offset);
tinybuf_error try_read_type(buf_ref *buf, serialize_type *type);
int try_read_int_tovar(BOOL isneg, const char *ptr, int size, QWORD *out_val);
int int_deserialize(const uint8_t *in, int in_size, uint64_t *out);
int optional_add(int x, int addx);
int int_serialize(uint64_t in, uint8_t *out);
int dump_string(int len, const char *str, buffer *out);
int try_read_box(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER target_version, tinybuf_error *r);
int tinybuf_value_deserialize(const char *ptr, int size, tinybuf_value *out, tinybuf_error *r);
const char *tinybuf_last_error_message(void);
int contain_any(uint64_t v);
uint32_t load_be32(const void *p);
double read_double(uint8_t *ptr);

// shared strpool read state for dumping
extern int64_t s_strpool_offset_read;
extern const char *s_strpool_base_read;
extern const char *s_last_error_msg;

#define SET_FAILED(s) (reason = s, s_last_error_msg = s, failed = TRUE)
#define SET_SUCCESS() (failed = FALSE, reason = NULL, s_last_error_msg = NULL)
#define CHECK_FAILED (failed && buf_offset(buf, -len));
#define INIT_STATE       \
    int len = 0;         \
    BOOL failed = FALSE; \
    const char *reason = NULL;
#define READ_RETURN return (failed ? -1 : len);

// minimal plugin runtime APIs used by writer
int tinybuf_plugin_get_count(void);
const char *tinybuf_plugin_get_guid(int index);
// precache APIs
void tinybuf_precache_reset(buffer *out);
int64_t tinybuf_precache_register(buffer *out, const tinybuf_value *value, tinybuf_error *r);
void tinybuf_precache_set_redirect(int enable);
int tinybuf_precache_is_redirect(void);
int64_t tinybuf_precache_find_start_for(buffer *out, const tinybuf_value *value);
// internal write APIs
int try_write_box(buffer *out, const tinybuf_value *value, tinybuf_error *r);
int try_write_version_box(buffer *out, uint64_t version, const tinybuf_value *box, tinybuf_error *r);
int try_write_version_list(buffer *out, const uint64_t *versions, const tinybuf_value **boxes, int count, tinybuf_error *r);
int try_write_plugin_map_table(buffer *out, tinybuf_error *r);
int try_write_part(buffer *out, const tinybuf_value *value, tinybuf_error *r);
int try_write_partitions(buffer *out, const tinybuf_value *mainbox, const tinybuf_value **subs, int count, tinybuf_error *r);

// string pool (write side)
extern int s_use_strpool;
void strpool_reset_write(const buffer *out);
int strpool_add(const char *data, int len);
tinybuf_error strpool_write_tail(buffer *out);

#endif // TINYBUF_PRIVATE_H
