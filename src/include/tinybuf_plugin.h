#ifndef TINYBUF_PLUGIN_H
#define TINYBUF_PLUGIN_H
#include <stdint.h>
#include "tinybuf_common.h"

#ifdef __cplusplus
extern "C" {
#endif

 

typedef tinybuf_result (*tinybuf_plugin_read_fn)(uint8_t type, buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler);
typedef tinybuf_result (*tinybuf_plugin_write_fn)(uint8_t type, const tinybuf_value *in, buffer *out);
typedef tinybuf_result (*tinybuf_plugin_dump_fn)(uint8_t type, buf_ref *buf, buffer *out);
typedef tinybuf_result (*tinybuf_plugin_show_value_fn)(uint8_t type, const tinybuf_value *in, buffer *out);
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

tinybuf_result tinybuf_plugins_try_read_by_type(uint8_t type, buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler);
tinybuf_result tinybuf_plugins_try_write(uint8_t type, const tinybuf_value *in, buffer *out);
tinybuf_result tinybuf_plugins_try_dump_by_type(uint8_t type, buf_ref *buf, buffer *out);
tinybuf_result tinybuf_plugins_try_show_value(uint8_t type, const tinybuf_value *in, buffer *out);
int tinybuf_plugin_set_runtime_map(const char **guids, int count);
int tinybuf_plugin_get_runtime_index_by_type(uint8_t type);
int tinybuf_plugin_do_value_op(int plugin_runtime_index, const char *name, tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out);
int tinybuf_plugin_do_value_op_by_type(uint8_t type, const char *name, tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out);

int tinybuf_register_builtin_plugins(void);
int tinybuf_plugin_register_from_dll(const char *dll_path);
int tinybuf_plugin_scan_dir(const char *dir);

 

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

#ifdef __cplusplus
}
#endif

/* removed int-return overloads */

#endif
#ifdef __cplusplus
/* compatibility macros for _r suffixed plugin APIs used in tests */
#define tinybuf_try_read_box_with_plugins_r(buf,out,contain) tinybuf_try_read_box_with_plugins((buf),(out),(contain))
#define tinybuf_plugins_try_write_r(out,val) tinybuf_try_write_box((out),(val))
#endif
