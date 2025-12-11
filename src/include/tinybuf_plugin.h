#ifndef TINYBUF_PLUGIN_H
#define TINYBUF_PLUGIN_H
#include <stdint.h>
#include "tinybuf_common.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef int (*tinybuf_plugin_read_fn)(uint8_t type, buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_error *r);
    typedef int (*tinybuf_plugin_write_fn)(uint8_t type, const tinybuf_value *in, buffer *out, tinybuf_error *r);
    typedef int (*tinybuf_plugin_dump_fn)(uint8_t type, buf_ref *buf, buffer *out, tinybuf_error *r);
    typedef int (*tinybuf_plugin_show_value_fn)(uint8_t type, const tinybuf_value *in, buffer *out, tinybuf_error *r);
    typedef struct
    {
        const uint8_t *types;
        int type_count;
        const char *guid;
        tinybuf_plugin_read_fn read;
        tinybuf_plugin_write_fn write;
        tinybuf_plugin_dump_fn dump;
        tinybuf_plugin_show_value_fn show_value;
        const char **op_names;
        const char **op_sigs;
        const char **op_descs;
        tinybuf_plugin_value_op_fn *op_fns;
        int op_count;
    } tinybuf_plugin_descriptor;

    int tinybuf_plugin_register(const uint8_t *types, int type_count, tinybuf_plugin_read_fn read, tinybuf_plugin_write_fn write, tinybuf_plugin_dump_fn dump, tinybuf_plugin_show_value_fn show_value);
    int tinybuf_plugin_unregister_all(void);
    int tinybuf_plugin_get_count(void);
    const char *tinybuf_plugin_get_guid(int index);

    int tinybuf_plugins_try_read_by_type(uint8_t type, buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_error *r);
    int tinybuf_plugins_try_write(uint8_t type, const tinybuf_value *in, buffer *out, tinybuf_error *r);
    int tinybuf_plugins_try_dump_by_type(uint8_t type, buf_ref *buf, buffer *out, tinybuf_error *r);
    int tinybuf_plugins_try_show_value(uint8_t type, const tinybuf_value *in, buffer *out, tinybuf_error *r);
    int tinybuf_plugin_set_runtime_map(const char **guids, int count);
    int tinybuf_plugin_get_runtime_index_by_type(uint8_t type);
    int tinybuf_plugin_do_value_op(int plugin_runtime_index, const char *name, tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out);
    int tinybuf_plugin_do_value_op_by_type(uint8_t type, const char *name, tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out);

    int tinybuf_plugin_register_descriptor(const tinybuf_plugin_descriptor *d);

    int tinybuf_register_builtin_plugins(void);
    int tinybuf_plugin_register_from_dll(const char *dll_path);
    int tinybuf_plugin_scan_dir(const char *dir);


#ifdef __cplusplus
}
#endif

/* removed int-return overloads */

#endif
#ifdef __cplusplus
/* compatibility macros for _r suffixed plugin APIs used in tests */
#define tinybuf_try_read_box_with_plugins_r(buf, out, contain) tinybuf_try_read_box_with_plugins((buf), (out), (contain))
#define tinybuf_plugins_try_write_r(out, val, r) tinybuf_try_write_box((out), (val), (r))
#endif
#if !defined(TB_EXPORT)
#  if defined(_WIN32) || defined(__CYGWIN__)
#    define TB_EXPORT __declspec(dllexport)
#  else
#    define TB_EXPORT __attribute__((visibility("default")))
#  endif
#endif
