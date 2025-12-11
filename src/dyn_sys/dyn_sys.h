#ifndef TINYBUF_DYN_SYS_H
#define TINYBUF_DYN_SYS_H
#include "tinybuf_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* runtime OOP: types & ops */
int tinybuf_oop_register_type(const char *type_name);
int tinybuf_oop_get_type_count(void);
const char *tinybuf_oop_get_type_name(int index);
int tinybuf_oop_register_op(const char *type_name, const char *op_name, const char *sig, const char *desc, tinybuf_plugin_value_op_fn fn);
int tinybuf_oop_get_op_count(const char *type_name);
int tinybuf_oop_get_op_meta(const char *type_name, int index, const char **name, const char **sig, const char **desc);
int tinybuf_oop_do_op(const char *type_name, const char *op_name, tinybuf_value *self, const tinybuf_value *args, tinybuf_value *out);

/* runtime OOP: serialization adapters */
int tinybuf_oop_attach_serializers(const char *type_name, tinybuf_custom_read_fn read, tinybuf_custom_write_fn write, tinybuf_custom_dump_fn dump);
int tinybuf_oop_register_types_to_custom(void);
int tinybuf_oop_set_serializable(const char *type_name, int serializable);
int tinybuf_oop_get_serializers(const char *type_name, tinybuf_custom_read_fn *read, tinybuf_custom_write_fn *write, tinybuf_custom_dump_fn *dump, int *is_serializable);

/* runtime traits */
int tinybuf_oop_register_trait(const char *trait_name);
int tinybuf_oop_trait_add_type(const char *trait_name, const char *type_name);
int tinybuf_oop_trait_register_op(const char *trait_name, const char *op_name, const char *sig, const char *desc, tinybuf_plugin_value_op_fn fn);
int tinybuf_oop_trait_get_op_count(const char *trait_name);
int tinybuf_oop_trait_get_op_meta(const char *trait_name, int index, const char **name, const char **sig, const char **desc);
int tinybuf_oop_type_implements_trait(const char *type_name, const char *trait_name);
int tinybuf_oop_do_trait_op(const char *trait_name, const char *op_name, const char *type_name, tinybuf_value *self, const tinybuf_value *args, tinybuf_value *out);

#ifdef __cplusplus
}
#endif

#endif
