#ifndef TINYBUF_COMMON_H
#define TINYBUF_COMMON_H
#include <stdint.h>
#include "tinybuf_buffer.h"
/* forward declarations to avoid circular include with tinybuf.h */
typedef struct T_tinybuf_value tinybuf_value;
typedef int tinybuf_read_pointer_mode;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    const char *base;
    int64_t all_size;
    const char *ptr;
    int64_t size;
} buf_ref;

typedef int (*CONTAIN_HANDLER)(uint64_t);

typedef void (*tinybuf_deleter_fn)(const char*);
typedef struct { const char *ptr; tinybuf_deleter_fn deleter; } tinybuf_str;
typedef struct { tinybuf_str *items; int count; int capacity; } tinybuf_strlist;
typedef struct {
    int res;
    tinybuf_strlist *msgs;
    int *refcnt;
} tinybuf_result;
tinybuf_result tinybuf_result_ok(int res);
tinybuf_result tinybuf_result_err(int res, const char *msg, tinybuf_deleter_fn deleter);
tinybuf_result tinybuf_result_create(int res, const char *msg, tinybuf_deleter_fn deleter);
tinybuf_result tinybuf_result_create_ok(int res);
tinybuf_result tinybuf_result_create_err(int res, const char *msg, tinybuf_deleter_fn deleter);
int tinybuf_result_add_msg(tinybuf_result *r, const char *msg, tinybuf_deleter_fn deleter);
int tinybuf_result_add_msg_const(tinybuf_result *r, const char *msg);
int tinybuf_result_msg_count(const tinybuf_result *r);
const char *tinybuf_result_msg_at(const tinybuf_result *r, int idx);
int tinybuf_result_format_msgs(const tinybuf_result *r, char *dst, int dst_len);
const char *tinybuf_last_error_message(void);

int tinybuf_result_ref(tinybuf_result *r);
int tinybuf_result_unref(tinybuf_result *r);

void tinybuf_result_set_current(tinybuf_result *r);
tinybuf_result *tinybuf_result_get_current(void);
int tinybuf_result_append_merge(tinybuf_result *dst, tinybuf_result *src, int (*mergeres)(int,int));
int tinybuf_merger_sum(int a, int b);
int tinybuf_merger_max(int a, int b);
int tinybuf_merger_left(int a, int b);
int tinybuf_merger_right(int a, int b);

tinybuf_result tinybuf_try_read_box_with_mode(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_read_pointer_mode mode);
tinybuf_result tinybuf_try_read_box(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler);
tinybuf_result tinybuf_try_write_box(buffer *out, const tinybuf_value *value);
tinybuf_result tinybuf_try_read_box_with_plugins(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler);

typedef tinybuf_result (*tinybuf_custom_read_fn)(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler);
typedef tinybuf_result (*tinybuf_custom_write_fn)(const char *name, const tinybuf_value *in, buffer *out);
typedef tinybuf_result (*tinybuf_custom_dump_fn)(const char *name, buf_ref *buf, buffer *out);
int tinybuf_custom_register(const char *name, tinybuf_custom_read_fn read, tinybuf_custom_write_fn write, tinybuf_custom_dump_fn dump);
tinybuf_result tinybuf_custom_try_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler);
tinybuf_result tinybuf_custom_try_write(const char *name, const tinybuf_value *in, buffer *out);
tinybuf_result tinybuf_custom_try_dump(const char *name, buf_ref *buf, buffer *out);

tinybuf_result tinybuf_try_write_version_box(buffer *out, uint64_t version, const tinybuf_value *box);
tinybuf_result tinybuf_try_write_version_list(buffer *out, const uint64_t *versions, const tinybuf_value **boxes, int count);
tinybuf_result tinybuf_try_write_plugin_map_table(buffer *out);

tinybuf_result tinybuf_try_write_part(buffer *out, const tinybuf_value *value);
tinybuf_result tinybuf_try_write_partitions(buffer *out, const tinybuf_value *mainbox, const tinybuf_value **subs, int count);

tinybuf_result tinybuf_try_write_pointer(buffer *out, int t, int64_t offset);
tinybuf_result tinybuf_try_write_sub_ref(buffer *out, int t, int64_t offset);
tinybuf_result tinybuf_try_write_custom_id_box(buffer *out, const char *name, const tinybuf_value *in);

int tinybuf_init(void);

/* compatibility macros for _r suffixed APIs used in C++ tests */
#define tinybuf_try_read_box_r(buf,out,contain) tinybuf_try_read_box((buf),(out),(contain))
#define tinybuf_try_read_box_with_mode_r(buf,out,contain,mode) tinybuf_try_read_box_with_mode((buf),(out),(contain),(mode))
#define tinybuf_try_write_box_r(out,value) tinybuf_try_write_box((out),(value))
#define tinybuf_try_write_version_box_r(out,ver,box) tinybuf_try_write_version_box((out),(ver),(box))
#define tinybuf_try_write_version_list_r(out,vers,boxes,count) tinybuf_try_write_version_list((out),(vers),(boxes),(count))
#define tinybuf_try_write_part_r(out,value) tinybuf_try_write_part((out),(value))
#define tinybuf_try_write_partitions_r(out,mainbox,subs,count) tinybuf_try_write_partitions((out),(mainbox),(subs),(count))
#define tinybuf_try_write_pointer_r(out,t,off) tinybuf_try_write_pointer((out),(t),(off))
#define tinybuf_try_write_custom_id_box_r(out,name,in) tinybuf_try_write_custom_id_box((out),(name),(in))
#define tinybuf_custom_try_read_r(name,data,len,out,contain) tinybuf_custom_try_read((name),(data),(len),(out),(contain))
#define tinybuf_custom_try_write_r(name,in,out) tinybuf_custom_try_write((name),(in),(out))
#define tinybuf_custom_try_dump_r(name,buf,out) tinybuf_custom_try_dump((name),(buf),(out))

/* pointer offset constants used by tests */
#define tinybuf_offset_start 0
#define tinybuf_offset_end 1
#define tinybuf_offset_current 2

#ifdef __cplusplus
}
#endif

#endif
