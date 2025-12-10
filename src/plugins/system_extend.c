#include "tinybuf.h"
#include "tinybuf_plugin.h"
#include "tinybuf_buffer.h"
#include <stdio.h>
#include <string.h>

static tinybuf_value* clone_box(const tinybuf_value *in)
{
    buffer *b = buffer_alloc();
    tinybuf_result wr = tinybuf_try_write_box(b, in);
    if(wr.res<=0){ buffer_free(b); return NULL; }
    buf_ref br = (buf_ref){ buffer_get_data(b), (int64_t)buffer_get_length(b), buffer_get_data(b), (int64_t)buffer_get_length(b) };
    tinybuf_value *out = tinybuf_value_alloc();
    tinybuf_result rr = tinybuf_try_read_box(&br, out, NULL);
    buffer_free(b);
    if(rr.res<=0){ tinybuf_value_free(out); return NULL; }
    return out;
}

static int op_hlist_insert(tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out){
    if(tinybuf_value_get_type(value) != tinybuf_array){ return -1; }
    int n = tinybuf_value_get_child_size(value);
    int idx = n;
    const tinybuf_value *ins = NULL;
    if(args && tinybuf_value_get_type(args) == tinybuf_array){
        const tinybuf_value *a0 = tinybuf_value_get_array_child(args, 0);
        const tinybuf_value *a1 = tinybuf_value_get_array_child(args, 1);
        if(a0 && tinybuf_value_get_type(a0) == tinybuf_int){ idx = (int)tinybuf_value_get_int(a0); }
        if(a1){ ins = a1; }
    }
    if(!ins){ return -1; }
    tinybuf_value_clear(out);
    int before = tinybuf_value_get_child_size(value);
    for(int i=0;i<=before;i++){
        if(i == idx){ tinybuf_value *cpins = clone_box(ins); if(!cpins) return -1; tinybuf_value_array_append(out, cpins); }
        if(i < before){ const tinybuf_value *ch = tinybuf_value_get_array_child(value, i); tinybuf_value *cp = clone_box(ch); if(!cp) return -1; tinybuf_value_array_append(out, cp); }
    }
    return 0;
}

static int op_hlist_delete(tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out){
    if(tinybuf_value_get_type(value) != tinybuf_array){ return -1; }
    int n = tinybuf_value_get_child_size(value);
    int idx = -1;
    if(args && tinybuf_value_get_type(args) == tinybuf_array){ const tinybuf_value *a0 = tinybuf_value_get_array_child(args, 0); if(a0 && tinybuf_value_get_type(a0) == tinybuf_int){ idx = (int)tinybuf_value_get_int(a0); } }
    if(idx < 0 || idx >= n){ return -1; }
    tinybuf_value_clear(out);
    for(int i=0;i<n;i++){ if(i==idx) continue; const tinybuf_value *ch = tinybuf_value_get_array_child(value, i); tinybuf_value *cp = clone_box(ch); if(!cp) return -1; tinybuf_value_array_append(out, cp); }
    return 0;
}

static int op_hlist_concat(tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out){
    if(tinybuf_value_get_type(value) != tinybuf_array){ return -1; }
    const tinybuf_value *other = NULL;
    if(args && tinybuf_value_get_type(args) == tinybuf_array){ other = tinybuf_value_get_array_child(args, 0); }
    if(!other || tinybuf_value_get_type(other) != tinybuf_array){ return -1; }
    tinybuf_value_clear(out);
    int n = tinybuf_value_get_child_size(value);
    for(int i=0;i<n;i++){ const tinybuf_value *ch = tinybuf_value_get_array_child(value, i); tinybuf_value *cp = clone_box(ch); if(!cp) return -1; tinybuf_value_array_append(out, cp); }
    int m = tinybuf_value_get_child_size(other);
    for(int j=0;j<m;j++){ const tinybuf_value *ch2 = tinybuf_value_get_array_child(other, j); tinybuf_value *cp2 = clone_box(ch2); if(!cp2) return -1; tinybuf_value_array_append(out, cp2); }
    return 0;
}

static int op_str(tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out){ (void)args; char buf[64]; int n = tinybuf_value_get_child_size(value); int len = snprintf(buf, sizeof(buf), "hetero_list[%d]", n); if(len<0) len=0; tinybuf_value_init_string(out, buf, len); return 0; }
static int op_desc(tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out){ (void)args; const char *s = "system.extend hetero_list"; tinybuf_value_init_string(out, s, (int)strlen(s)); return 0; }

static tinybuf_result tuple_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler){ (void)name; buf_ref br = (buf_ref){ (const char*)data, (int64_t)len, (const char*)data, (int64_t)len }; return tinybuf_try_read_box(&br, out, contain_handler); }
static tinybuf_result tuple_write(const char *name, const tinybuf_value *in, buffer *out){ (void)name; if(tinybuf_value_get_type(in) != tinybuf_array){ return tinybuf_result_err(-1, "tuple_write type mismatch", NULL); } return tinybuf_try_write_box(out, in); }
static tinybuf_result tuple_dump(const char *name, buf_ref *buf, buffer *out){ (void)name; int r = tinybuf_dump_buffer_as_text(buf->ptr, (int)buf->size, out); return r>0 ? tinybuf_result_ok(r) : tinybuf_result_err(r, "tuple_dump failed", NULL); }

static tinybuf_result hlist_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler){ (void)name; const char *ptr = (const char*)data; int size = len; tinybuf_value_clear(out); for(;;){ if(size<=0) break; buf_ref br = (buf_ref){ (const char*)ptr, (int64_t)size, (const char*)ptr, (int64_t)size }; tinybuf_value *item = tinybuf_value_alloc(); tinybuf_result rr = tinybuf_try_read_box(&br, item, contain_handler); if(rr.res<=0){ tinybuf_value_free(item); tinybuf_result_add_msg_const(&rr, "hlist_read item failed"); return rr; } if(tinybuf_value_get_type(out) != tinybuf_array){ tinybuf_value_clear(out); } tinybuf_value_array_append(out, item); ptr += rr.res; size -= rr.res; } return tinybuf_result_ok(len); }
static tinybuf_result hlist_write(const char *name, const tinybuf_value *in, buffer *out){ (void)name; if(tinybuf_value_get_type(in) != tinybuf_array) return tinybuf_result_err(-1, "hlist_write type mismatch", NULL); int before = buffer_get_length(out); int n = tinybuf_value_get_child_size(in); for(int i=0;i<n;++i){ const tinybuf_value *ch = tinybuf_value_get_array_child(in, i); if(!ch) return tinybuf_result_err(-1, "hlist_write null child", NULL); tinybuf_result wr = tinybuf_try_write_box(out, ch); if(wr.res<=0){ tinybuf_result_add_msg_const(&wr, "hlist_write child failed"); return wr; } } int after = buffer_get_length(out); return tinybuf_result_ok(after - before); }
static tinybuf_result hlist_dump(const char *name, buf_ref *buf, buffer *out){ (void)name; int r = tinybuf_dump_buffer_as_text(buf->ptr, (int)buf->size, out); return r>0 ? tinybuf_result_ok(r) : tinybuf_result_err(r, "hlist_dump failed", NULL); }

static tinybuf_result dataframe_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler){ (void)name; buf_ref br = (buf_ref){ (const char*)data, (int64_t)len, (const char*)data, (int64_t)len }; return tinybuf_try_read_box(&br, out, contain_handler); }
static tinybuf_result dataframe_write(const char *name, const tinybuf_value *in, buffer *out){ (void)name; if(tinybuf_value_get_type(in) != tinybuf_indexed_tensor) return tinybuf_result_err(-1, "dataframe_write type mismatch", NULL); return tinybuf_try_write_box(out, in); }
static tinybuf_result dataframe_dump(const char *name, buf_ref *buf, buffer *out){ (void)name; int r = tinybuf_dump_buffer_as_text(buf->ptr, (int)buf->size, out); return r>0 ? tinybuf_result_ok(r) : tinybuf_result_err(r, "dataframe_dump failed", NULL); }

__declspec(dllexport) int tinybuf_plugin_init(void){
    tinybuf_custom_register("hetero_tuple", tuple_read, tuple_write, tuple_dump);
    tinybuf_custom_register("hetero_list", hlist_read, hlist_write, hlist_dump);
    tinybuf_custom_register("dataframe", dataframe_read, dataframe_write, dataframe_dump);
    return 0;
}

__declspec(dllexport) int tinybuf_plugin_init_with_host(int (*host_custom_register)(const char*, tinybuf_custom_read_fn, tinybuf_custom_write_fn, tinybuf_custom_dump_fn)){
    host_custom_register("hetero_tuple", tuple_read, tuple_write, tuple_dump);
    host_custom_register("hetero_list", hlist_read, hlist_write, hlist_dump);
    host_custom_register("dataframe", dataframe_read, dataframe_write, dataframe_dump);
    return 0;
}

__declspec(dllexport) tinybuf_plugin_descriptor* tinybuf_get_plugin_descriptor(void){
    static uint8_t types[1] = { 0 };
    static const char *op_names[] = { "hlist_insert", "hlist_delete", "hlist_concat", "__str__", "__desc__" };
    static const char *op_sigs[] = { "(list,int,value)->list", "(list,int)->list", "(list,list)->list", "(list)->string", "(list)->string" };
    static const char *op_descs[] = { "insert element", "delete element", "concat lists", "stringify", "describe" };
    static tinybuf_plugin_value_op_fn op_fns[] = { op_hlist_insert, op_hlist_delete, op_hlist_concat, op_str, op_desc };
    static tinybuf_plugin_descriptor d;
    d.types = types; d.type_count = 0; d.guid = "plugin:system.extend";
    d.read = NULL; d.write = NULL; d.dump = NULL; d.show_value = NULL;
    d.op_names = op_names; d.op_sigs = op_sigs; d.op_descs = op_descs; d.op_fns = op_fns; d.op_count = 5;
    return &d;
}
