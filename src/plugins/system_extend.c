#include "tinybuf.h"
#include "tinybuf_plugin.h"
#include "tinybuf_buffer.h"

static int tuple_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler){ (void)name; buf_ref br = (buf_ref){ (const char*)data, (int64_t)len, (const char*)data, (int64_t)len }; return tinybuf_try_read_box(&br, out, contain_handler); }
static int tuple_write(const char *name, const tinybuf_value *in, buffer *out){ (void)name; if(tinybuf_value_get_type(in) != tinybuf_array) return -1; return tinybuf_try_write_box(out, in); }
static int tuple_dump(const char *name, buf_ref *buf, buffer *out){ (void)name; return tinybuf_dump_buffer_as_text(buf->ptr, (int)buf->size, out); }

static int hlist_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler){ (void)name; const char *ptr = (const char*)data; int size = len; tinybuf_value_clear(out); for(;;){ if(size<=0) break; buf_ref br = (buf_ref){ (const char*)ptr, (int64_t)size, (const char*)ptr, (int64_t)size }; tinybuf_value *item = tinybuf_value_alloc(); int r = tinybuf_try_read_box(&br, item, contain_handler); if(r<=0){ tinybuf_value_free(item); return r; } if(tinybuf_value_get_type(out) != tinybuf_array){ tinybuf_value_clear(out); } tinybuf_value_array_append(out, item); ptr += r; size -= r; } return len; }
static int hlist_write(const char *name, const tinybuf_value *in, buffer *out){ (void)name; if(tinybuf_value_get_type(in) != tinybuf_array) return -1; int before = buffer_get_length(out); int n = tinybuf_value_get_child_size(in); for(int i=0;i<n;++i){ const tinybuf_value *ch = tinybuf_value_get_array_child(in, i); if(!ch) return -1; int w = tinybuf_try_write_box(out, ch); if(w<=0) return w; } int after = buffer_get_length(out); return after - before; }
static int hlist_dump(const char *name, buf_ref *buf, buffer *out){ (void)name; return tinybuf_dump_buffer_as_text(buf->ptr, (int)buf->size, out); }

static int dataframe_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler){ (void)name; buf_ref br = (buf_ref){ (const char*)data, (int64_t)len, (const char*)data, (int64_t)len }; return tinybuf_try_read_box(&br, out, contain_handler); }
static int dataframe_write(const char *name, const tinybuf_value *in, buffer *out){ (void)name; if(tinybuf_value_get_type(in) != tinybuf_indexed_tensor) return -1; return tinybuf_try_write_box(out, in); }
static int dataframe_dump(const char *name, buf_ref *buf, buffer *out){ (void)name; return tinybuf_dump_buffer_as_text(buf->ptr, (int)buf->size, out); }

__declspec(dllexport) int tinybuf_plugin_init(void){
    tinybuf_custom_register("hetero_tuple", tuple_read, tuple_write, tuple_dump);
    tinybuf_custom_register("hetero_list", hlist_read, hlist_write, hlist_dump);
    tinybuf_custom_register("dataframe", dataframe_read, dataframe_write, dataframe_dump);
    return 0;
}

__declspec(dllexport) tinybuf_plugin_descriptor* tinybuf_get_plugin_descriptor(void){
    static uint8_t types[1] = { 0 };
    static tinybuf_plugin_descriptor d;
    d.types = types; d.type_count = 0; d.guid = "plugin:system.extend";
    d.read = NULL; d.write = NULL; d.dump = NULL; d.show_value = NULL;
    d.op_names = NULL; d.op_sigs = NULL; d.op_descs = NULL; d.op_fns = NULL; d.op_count = 0;
    return &d;
}

