#ifndef TINYBUF_PLUGIN_H
#define TINYBUF_PLUGIN_H
#include <stdint.h>
#include "tinybuf.h"
#include "tinybuf_buffer.h"

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

typedef int (*tinybuf_plugin_read_fn)(uint8_t type, buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler);
typedef int (*tinybuf_plugin_write_fn)(uint8_t type, const tinybuf_value *in, buffer *out);
typedef int (*tinybuf_plugin_dump_fn)(uint8_t type, buf_ref *buf, buffer *out);
typedef int (*tinybuf_plugin_show_value_fn)(uint8_t type, const tinybuf_value *in, buffer *out);
typedef int (*tinybuf_plugin_value_op_fn)(tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out);
typedef struct {
    const uint8_t *types; int type_count; const char *guid;
    tinybuf_plugin_read_fn read; tinybuf_plugin_write_fn write; tinybuf_plugin_dump_fn dump; tinybuf_plugin_show_value_fn show_value;
    const char **op_names; const char **op_sigs; const char **op_descs; tinybuf_plugin_value_op_fn *op_fns; int op_count;
} tinybuf_plugin_descriptor;

int tinybuf_plugin_register(const uint8_t *types, int type_count, tinybuf_plugin_read_fn read, tinybuf_plugin_write_fn write, tinybuf_plugin_dump_fn dump, tinybuf_plugin_show_value_fn show_value);
int tinybuf_plugin_unregister_all(void);
int tinybuf_plugin_get_count(void);
const char* tinybuf_plugin_get_guid(int index);

int tinybuf_plugins_try_read_by_type(uint8_t type, buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler);
int tinybuf_plugins_try_write(uint8_t type, const tinybuf_value *in, buffer *out);
int tinybuf_plugins_try_dump_by_type(uint8_t type, buf_ref *buf, buffer *out);
int tinybuf_plugins_try_show_value(uint8_t type, const tinybuf_value *in, buffer *out);
int tinybuf_plugin_set_runtime_map(const char **guids, int count);
int tinybuf_plugin_get_runtime_index_by_type(uint8_t type);
int tinybuf_plugin_do_value_op(int plugin_runtime_index, const char *name, tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out);
int tinybuf_plugin_do_value_op_by_type(uint8_t type, const char *name, tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out);

int tinybuf_try_read_box_with_plugins(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler);
int tinybuf_register_builtin_plugins(void);
int tinybuf_plugin_register_from_dll(const char *dll_path);

int tinybuf_try_write_plugin_map_table(buffer *out);

// pointer read mode API
int tinybuf_try_read_box_with_mode(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_read_pointer_mode mode);

// core try-read/try-write APIs
int tinybuf_try_read_box(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler);
int tinybuf_try_write_box(buffer *out, const tinybuf_value *value);

typedef int (*tinybuf_custom_read_fn)(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler);
typedef int (*tinybuf_custom_write_fn)(const char *name, const tinybuf_value *in, buffer *out);
typedef int (*tinybuf_custom_dump_fn)(const char *name, buf_ref *buf, buffer *out);
int tinybuf_custom_register(const char *name, tinybuf_custom_read_fn read, tinybuf_custom_write_fn write, tinybuf_custom_dump_fn dump);
int tinybuf_custom_try_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler);
int tinybuf_custom_try_write(const char *name, const tinybuf_value *in, buffer *out);
int tinybuf_custom_try_dump(const char *name, buf_ref *buf, buffer *out);
int tinybuf_try_write_version_box(buffer *out, uint64_t version, const tinybuf_value *box);
int tinybuf_try_write_version_list(buffer *out, const uint64_t *versions, const tinybuf_value **boxes, int count);

int tinybuf_try_write_part(buffer *out, const tinybuf_value *value);
int tinybuf_try_write_partitions(buffer *out, const tinybuf_value *mainbox, const tinybuf_value **subs, int count);

int tinybuf_plugin_register_from_dll(const char *dll_path);

typedef enum {
    tinybuf_offset_start = 0,
    tinybuf_offset_end = 1,
    tinybuf_offset_current = 2
} tinybuf_offset_type;

int tinybuf_try_write_pointer(buffer *out, tinybuf_offset_type t, int64_t offset);
int tinybuf_try_write_sub_ref(buffer *out, tinybuf_offset_type t, int64_t offset);
int tinybuf_try_write_custom_id_box(buffer *out, const char *name, const tinybuf_value *in);

int tinybuf_oop_register_type(const char *type_name);
int tinybuf_oop_get_type_count(void);
const char* tinybuf_oop_get_type_name(int index);
int tinybuf_oop_register_op(const char *type_name, const char *op_name, const char *sig, const char *desc, tinybuf_plugin_value_op_fn fn);
int tinybuf_oop_get_op_count(const char *type_name);
int tinybuf_oop_get_op_meta(const char *type_name, int index, const char **name, const char **sig, const char **desc);
int tinybuf_oop_do_op(const char *type_name, const char *op_name, tinybuf_value *self, const tinybuf_value *args, tinybuf_value *out);
int tinybuf_oop_attach_serializers(const char *type_name, tinybuf_custom_read_fn read, tinybuf_custom_write_fn write, tinybuf_custom_dump_fn dump);
int tinybuf_oop_register_types_to_custom(void);
int tinybuf_oop_set_serializable(const char *type_name, int serializable);
int tinybuf_trait_register(const char *trait_name, const char **methods, int method_count);
int tinybuf_trait_attach(const char *type_name, const char *trait_name);
int tinybuf_trait_has(const char *type_name, const char *trait_name);
int tinybuf_trait_do(const char *type_name, const char *trait_name, const char *method_name, tinybuf_value *self, const tinybuf_value *args, tinybuf_value *out);

#ifdef __cplusplus
}
#endif

#endif
