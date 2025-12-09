// plugin system implementation compatible with core tryread/trywrite
#include "tinybuf_plugin.h"
#include "tinybuf_buffer.h"
#include "tinybuf_memory.h"
#include <string.h>

typedef struct {
    uint8_t *types;
    int type_count;
    tinybuf_plugin_read_fn read;
    tinybuf_plugin_write_fn write;
    tinybuf_plugin_dump_fn dump;
    tinybuf_plugin_show_value_fn show_value;
} plugin_entry;

static plugin_entry *s_plugins = NULL;
static int s_plugins_count = 0;
static int s_plugins_capacity = 0;

static inline int buf_offset_local(buf_ref *buf, int64_t offset){
    if(offset < 0 || offset > buf->size) return -1;
    buf->ptr += offset;
    buf->size -= offset;
    return 0;
}

int tinybuf_plugin_register(const uint8_t *types, int type_count, tinybuf_plugin_read_fn read, tinybuf_plugin_write_fn write, tinybuf_plugin_dump_fn dump, tinybuf_plugin_show_value_fn show_value){
    if(!types || type_count <= 0 || !read) return -1;
    if(s_plugins_count == s_plugins_capacity){
        int newcap = s_plugins_capacity ? s_plugins_capacity * 2 : 8;
        s_plugins = (plugin_entry*)tinybuf_realloc(s_plugins, sizeof(plugin_entry) * newcap);
        s_plugins_capacity = newcap;
    }
    plugin_entry e;
    e.types = (uint8_t*)tinybuf_malloc(type_count);
    memcpy(e.types, types, (size_t)type_count);
    e.type_count = type_count;
    e.read = read;
    e.write = write;
    e.dump = dump;
    e.show_value = show_value;
    s_plugins[s_plugins_count++] = e;
    return 0;
}

int tinybuf_plugin_unregister_all(void){
    for(int i=0;i<s_plugins_count;++i){ tinybuf_free(s_plugins[i].types); }
    tinybuf_free(s_plugins); s_plugins = NULL; s_plugins_count = 0; s_plugins_capacity = 0; return 0;
}

int tinybuf_plugins_try_read_by_type(uint8_t type, buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler){
    (void)contain_handler;
    for(int i=0;i<s_plugins_count;++i){
        for(int k=0;k<s_plugins[i].type_count;++k){ if(s_plugins[i].types[k] == type){ return s_plugins[i].read(type, buf, out, contain_handler); } }
    }
    return -1;
}

int tinybuf_plugins_try_write(uint8_t type, const tinybuf_value *in, buffer *out){
    for(int i=0;i<s_plugins_count;++i){
        for(int k=0;k<s_plugins[i].type_count;++k){ if(s_plugins[i].types[k] == type && s_plugins[i].write){ return s_plugins[i].write(type, in, out); } }
    }
    return -1;
}

int tinybuf_plugins_try_dump_by_type(uint8_t type, buf_ref *buf, buffer *out){
    for(int i=0;i<s_plugins_count;++i){
        for(int k=0;k<s_plugins[i].type_count;++k){ if(s_plugins[i].types[k] == type && s_plugins[i].dump){ return s_plugins[i].dump(type, buf, out); } }
    }
    return 0;
}

int tinybuf_plugins_try_show_value(uint8_t type, const tinybuf_value *in, buffer *out){
    for(int i=0;i<s_plugins_count;++i){
        for(int k=0;k<s_plugins[i].type_count;++k){ if(s_plugins[i].types[k] == type && s_plugins[i].show_value){ return s_plugins[i].show_value(type, in, out); } }
    }
    return 0;
}

int tinybuf_try_read_box_with_plugins(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler){
    if(buf->size <= 0) return 0;
    uint8_t t = (uint8_t)buf->ptr[0];
    int pr = tinybuf_plugins_try_read_by_type(t, buf, out, contain_handler);
    if(pr != -1) return pr;
    return tinybuf_try_read_box(buf, out, contain_handler);
}

// builtin sample plugin: type 200 -> upper-string
#define TINYBUF_PLUGIN_UPPER_STRING 200

static int plugin_upper_read(uint8_t type, buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler){
    (void)contain_handler;
    if(type != TINYBUF_PLUGIN_UPPER_STRING) return -1;
    if(buf->size < 2) return 0;
    uint8_t t = (uint8_t)buf->ptr[0];
    if(t != TINYBUF_PLUGIN_UPPER_STRING) return -1;
    uint8_t len = (uint8_t)buf->ptr[1];
    if(buf->size < (int64_t)(2 + len)) return 0;
    const char *p = buf->ptr + 2;
    char *tmp = (char*)tinybuf_malloc(len);
    for(int i=0;i<len;++i){ char c = p[i]; if(c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A'); tmp[i] = c; }
    tinybuf_value_init_string(out, tmp, len);
    tinybuf_free(tmp);
    buf_offset_local(buf, 2 + len);
    return 2 + len;
}

static int plugin_upper_write(uint8_t type, const tinybuf_value *in, buffer *out){
    if(type != TINYBUF_PLUGIN_UPPER_STRING) return -1;
    buffer *s = tinybuf_value_get_string(in);
    int len = buffer_get_length(s);
    if(len > 255) len = 255;
    uint8_t t = TINYBUF_PLUGIN_UPPER_STRING;
    buffer_append(out, (const char*)&t, 1);
    uint8_t l = (uint8_t)len;
    buffer_append(out, (const char*)&l, 1);
    buffer_append(out, buffer_get_data(s), len);
    return 2 + len;
}

static int plugin_upper_dump(uint8_t type, buf_ref *buf, buffer *out){
    if(type != TINYBUF_PLUGIN_UPPER_STRING) return -1;
    if(buf->size < 2) return 0;
    uint8_t t = (uint8_t)buf->ptr[0];
    if(t != TINYBUF_PLUGIN_UPPER_STRING) return -1;
    uint8_t len = (uint8_t)buf->ptr[1];
    if(buf->size < (int64_t)(2 + len)) return 0;
    const char *p = buf->ptr + 2;
    buffer_append(out, "\"", 1);
    for(int i=0;i<len;++i){ char c = p[i]; if(c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A'); buffer_append(out, &c, 1); }
    buffer_append(out, "\"", 1);
    return 2 + len;
}

static int plugin_upper_show_value(uint8_t type, const tinybuf_value *in, buffer *out){
    if(type != TINYBUF_PLUGIN_UPPER_STRING) return -1;
    buffer *s = tinybuf_value_get_string(in);
    if(!s) return -1;
    buffer_append(out, "upper(", 6);
    buffer_append(out, buffer_get_data(s), buffer_get_length(s));
    buffer_append(out, ")", 1);
    return buffer_get_length(s) + 7;
}

int tinybuf_register_builtin_plugins(void){
    uint8_t types[1] = { TINYBUF_PLUGIN_UPPER_STRING };
    return tinybuf_plugin_register(types, 1, plugin_upper_read, plugin_upper_write, plugin_upper_dump, plugin_upper_show_value);
}

#ifdef _WIN32
#include <windows.h>
typedef struct {
    const uint8_t *types; int type_count; tinybuf_plugin_read_fn read; tinybuf_plugin_write_fn write; tinybuf_plugin_dump_fn dump; tinybuf_plugin_show_value_fn show_value;
} tinybuf_plugin_descriptor;
typedef tinybuf_plugin_descriptor* (*get_desc_fn)(void);
int tinybuf_plugin_register_from_dll(const char *dll_path){
    HMODULE h = LoadLibraryA(dll_path);
    if(!h) return -1;
    get_desc_fn getter = (get_desc_fn)GetProcAddress(h, "tinybuf_get_plugin_descriptor");
    if(!getter){ FreeLibrary(h); return -1; }
    tinybuf_plugin_descriptor *d = getter();
    if(!d){ FreeLibrary(h); return -1; }
    return tinybuf_plugin_register(d->types, d->type_count, d->read, d->write, d->dump, d->show_value);
}
#else
int tinybuf_plugin_register_from_dll(const char *dll_path){ (void)dll_path; return -1; }
#endif
