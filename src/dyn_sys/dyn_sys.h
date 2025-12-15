#ifndef TINYBUF_DYN_SYS_H
#define TINYBUF_DYN_SYS_H
#include "type_core.h"

#ifdef __cplusplus
extern "C" {
#endif

int dyn_oop_register_type(const char *type_name);
int dyn_oop_get_type_count(void);
const char *dyn_oop_get_type_name(int index);

typedef struct
{
    const char *name;
    const char *type_name;
    const char *desc;
} dyn_param_desc;

typedef struct
{
    const char *ret_type_name;
    const dyn_param_desc *params;
    int param_count;
    const char *desc;
} dyn_method_sig;

typedef int (*dyn_method_call_fn)(typed_obj *self, const typed_obj *args, int argc, typed_obj *ret_out);

int dyn_oop_register_op_typed(const char *type_name, const char *op_name, const dyn_method_sig *sig, const char *op_desc, dyn_method_call_fn fn);
int dyn_oop_get_op_typed(const char *type_name, const char *op_name, const dyn_method_sig **sig);
int dyn_oop_get_op_count(const char *type_name);
const char *dyn_oop_get_op_name(const char *type_name, int index);
int dyn_oop_get_op_meta(const char *type_name, int index, const char **name, const char **sig, const char **desc);
int dyn_oop_do_op(const char *type_name, const char *op_name, typed_obj *self, const typed_obj *args, int argc, typed_obj *out);

#ifndef TINYBUF_COMMON_H
typedef struct T_tinybuf_value tinybuf_value;
typedef int (*tinybuf_plugin_value_op_fn)(tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out);
typedef int (*tinybuf_custom_read_fn)(const char *name, const uint8_t *data, int len, tinybuf_value *out, int (*contain_handler)(uint64_t), void *r);
typedef int (*tinybuf_custom_write_fn)(const char *name, const tinybuf_value *in, void *out, void *r);
typedef int (*tinybuf_custom_dump_fn)(const char *name, void *buf, void *out, void *r);
#endif

int tinybuf_oop_register_type(const char *type_name);
int tinybuf_oop_get_type_count(void);
const char *tinybuf_oop_get_type_name(int index);
int tinybuf_oop_register_op(const char *type_name, const char *op_name, const char *sig, const char *op_desc, tinybuf_plugin_value_op_fn fn);
int tinybuf_oop_get_op_count(const char *type_name);
const char *tinybuf_oop_get_op_name(const char *type_name, int index);
int tinybuf_oop_get_op_meta(const char *type_name, int index, const char **name, const char **sig, const char **desc);
int tinybuf_oop_do_op(const char *type_name, const char *op_name, tinybuf_value *self, const tinybuf_value *args, tinybuf_value *out);
int tinybuf_oop_register_op_typed(const char *type_name, const char *op_name, const dyn_method_sig *sig, const char *op_desc, tinybuf_plugin_value_op_fn fn);
int tinybuf_oop_get_op_typed(const char *type_name, const char *op_name, const dyn_method_sig **sig);
int tinybuf_oop_attach_serializers(const char *type_name, tinybuf_custom_read_fn read, tinybuf_custom_write_fn write, tinybuf_custom_dump_fn dump);
int tinybuf_oop_set_serializable(const char *type_name, int serializable);
int tinybuf_oop_get_serializers(const char *type_name, tinybuf_custom_read_fn *read, tinybuf_custom_write_fn *write, tinybuf_custom_dump_fn *dump, int *is_serializable);
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
