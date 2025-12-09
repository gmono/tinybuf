// plugin system implementation compatible with core tryread/trywrite
#include "tinybuf_plugin.h"
#include "tinybuf_buffer.h"
#include "tinybuf_memory.h"
#include <string.h>

typedef struct {
    uint8_t *types;
    int type_count;
    const char *guid;
    tinybuf_plugin_read_fn read;
    tinybuf_plugin_write_fn write;
    tinybuf_plugin_dump_fn dump;
    tinybuf_plugin_show_value_fn show_value;
    tinybuf_plugin_read_fn_r read_r;
    tinybuf_plugin_write_fn_r write_r;
    tinybuf_plugin_dump_fn_r dump_r;
    tinybuf_plugin_show_value_fn_r show_value_r;
    const char **op_names; const char **op_sigs; const char **op_descs; tinybuf_plugin_value_op_fn *op_fns; int op_count;
} plugin_entry;

static plugin_entry *s_plugins = NULL;
static int s_plugins_count = 0;
static int s_plugins_capacity = 0;
static const char **s_plugin_runtime_map = NULL;
static int s_plugin_runtime_map_count = 0;

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
    e.guid = NULL;
    e.read = read;
    e.write = write;
    e.dump = dump;
    e.show_value = show_value;
    e.read_r = NULL; e.write_r = NULL; e.dump_r = NULL; e.show_value_r = NULL;
    e.op_names = NULL; e.op_sigs = NULL; e.op_descs = NULL; e.op_fns = NULL; e.op_count = 0;
    s_plugins[s_plugins_count++] = e;
    return 0;
}

static tinybuf_strlist* strlist_new(void){ tinybuf_strlist *l=(tinybuf_strlist*)tinybuf_malloc(sizeof(tinybuf_strlist)); l->items=NULL; l->count=0; l->capacity=0; return l; }
static void strlist_ensure(tinybuf_strlist *l){ if(!l) return; if(l->count==l->capacity){ int nc = l->capacity? l->capacity*2 : 4; l->items=(tinybuf_str*)tinybuf_realloc(l->items, sizeof(tinybuf_str)*nc); l->capacity=nc; } }
static void strlist_add_owned(tinybuf_strlist *l, const char *msg, tinybuf_deleter_fn d){ if(!l||!msg) return; strlist_ensure(l); l->items[l->count].ptr=msg; l->items[l->count].deleter=d; l->count++; }
static void strlist_free(tinybuf_strlist *l){ if(!l) return; for(int i=0;i<l->count;++i){ if(l->items[i].ptr && l->items[i].deleter){ l->items[i].deleter(l->items[i].ptr); } } tinybuf_free(l->items); tinybuf_free(l); }
tinybuf_result tinybuf_result_ok(int res){ tinybuf_result r; r.res=res; r.msgs=NULL; return r; }
tinybuf_result tinybuf_result_err(int res, const char *msg, tinybuf_deleter_fn deleter){ tinybuf_result r; r.res=res; r.msgs=strlist_new(); if(msg) strlist_add_owned(r.msgs,msg,deleter); return r; }
int tinybuf_result_add_msg(tinybuf_result *r, const char *msg, tinybuf_deleter_fn deleter){ if(!r||!msg) return -1; if(!r->msgs) r->msgs=strlist_new(); strlist_add_owned(r->msgs,msg,deleter); return r->msgs->count; }
int tinybuf_result_add_msg_const(tinybuf_result *r, const char *msg){ return tinybuf_result_add_msg(r,msg,NULL); }
int tinybuf_result_msg_count(const tinybuf_result *r){ return (r && r->msgs)? r->msgs->count : 0; }
const char *tinybuf_result_msg_at(const tinybuf_result *r, int idx){ if(!r||!r->msgs||idx<0||idx>=r->msgs->count) return NULL; return r->msgs->items[idx].ptr; }
int tinybuf_result_format_msgs(const tinybuf_result *r, char *dst, int dst_len){ if(!dst||dst_len<=0) return 0; int n=tinybuf_result_msg_count(r); int off=0; for(int i=0;i<n;++i){ const char *s=tinybuf_result_msg_at(r,i); if(!s) continue; int sl=(int)strlen(s); if(off+sl+ (i+1<n?2:0) >= dst_len) break; memcpy(dst+off,s,(size_t)sl); off+=sl; if(i+1<n){ dst[off++]=';'; dst[off++]=' '; } } if(off<dst_len) dst[off]='\0'; return off; }
void tinybuf_result_dispose(tinybuf_result *r){ if(!r) return; if(r->msgs){ strlist_free(r->msgs); r->msgs=NULL; } }

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

tinybuf_result tinybuf_plugins_try_read_by_type_r(uint8_t type, buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler){
    (void)contain_handler;
    for(int i=0;i<s_plugins_count;++i){
        for(int k=0;k<s_plugins[i].type_count;++k){
            if(s_plugins[i].types[k] == type){
                if(s_plugins[i].read_r){ return s_plugins[i].read_r(type, buf, out, contain_handler); }
                int ir = s_plugins[i].read(type, buf, out, contain_handler);
                if(ir>0) return tinybuf_result_ok(ir);
                tinybuf_result rr = tinybuf_result_err(ir, "plugin read failed", NULL);
                if(s_plugins[i].guid) tinybuf_result_add_msg_const(&rr, s_plugins[i].guid);
                return rr;
            }
        }
    }
    return tinybuf_result_err(-1, "plugin type not found", NULL);
}

int tinybuf_plugins_try_write(uint8_t type, const tinybuf_value *in, buffer *out){
    for(int i=0;i<s_plugins_count;++i){
        for(int k=0;k<s_plugins[i].type_count;++k){ if(s_plugins[i].types[k] == type && s_plugins[i].write){ return s_plugins[i].write(type, in, out); } }
    }
    return -1;
}

tinybuf_result tinybuf_plugins_try_write_r(uint8_t type, const tinybuf_value *in, buffer *out){
    for(int i=0;i<s_plugins_count;++i){
        for(int k=0;k<s_plugins[i].type_count;++k){ if(s_plugins[i].types[k] == type){ if(s_plugins[i].write_r) return s_plugins[i].write_r(type, in, out); int ir = s_plugins[i].write? s_plugins[i].write(type, in, out) : -1; if(ir>0) return tinybuf_result_ok(ir); tinybuf_result rr = tinybuf_result_err(ir, "plugin write failed", NULL); if(s_plugins[i].guid) tinybuf_result_add_msg_const(&rr, s_plugins[i].guid); return rr; } }
    }
    return tinybuf_result_err(-1, "plugin type not found", NULL);
}

int tinybuf_plugins_try_dump_by_type(uint8_t type, buf_ref *buf, buffer *out){
    for(int i=0;i<s_plugins_count;++i){
        for(int k=0;k<s_plugins[i].type_count;++k){ if(s_plugins[i].types[k] == type && s_plugins[i].dump){ return s_plugins[i].dump(type, buf, out); } }
    }
    return 0;
}

tinybuf_result tinybuf_plugins_try_dump_by_type_r(uint8_t type, buf_ref *buf, buffer *out){
    for(int i=0;i<s_plugins_count;++i){
        for(int k=0;k<s_plugins[i].type_count;++k){ if(s_plugins[i].types[k] == type){ if(s_plugins[i].dump_r) return s_plugins[i].dump_r(type, buf, out); int ir = s_plugins[i].dump? s_plugins[i].dump(type, buf, out) : -1; if(ir>0) return tinybuf_result_ok(ir); tinybuf_result rr = tinybuf_result_err(ir, "plugin dump failed", NULL); if(s_plugins[i].guid) tinybuf_result_add_msg_const(&rr, s_plugins[i].guid); return rr; } }
    }
    return tinybuf_result_err(-1, "plugin type not found", NULL);
}

int tinybuf_plugins_try_show_value(uint8_t type, const tinybuf_value *in, buffer *out){
    for(int i=0;i<s_plugins_count;++i){
        for(int k=0;k<s_plugins[i].type_count;++k){ if(s_plugins[i].types[k] == type && s_plugins[i].show_value){ return s_plugins[i].show_value(type, in, out); } }
    }
    return 0;
}

tinybuf_result tinybuf_plugins_try_show_value_r(uint8_t type, const tinybuf_value *in, buffer *out){
    for(int i=0;i<s_plugins_count;++i){
        for(int k=0;k<s_plugins[i].type_count;++k){ if(s_plugins[i].types[k] == type){ if(s_plugins[i].show_value_r) return s_plugins[i].show_value_r(type, in, out); int ir = s_plugins[i].show_value? s_plugins[i].show_value(type, in, out) : -1; if(ir>0) return tinybuf_result_ok(ir); tinybuf_result rr = tinybuf_result_err(ir, "plugin show_value failed", NULL); if(s_plugins[i].guid) tinybuf_result_add_msg_const(&rr, s_plugins[i].guid); return rr; } }
    }
    return tinybuf_result_err(-1, "plugin type not found", NULL);
}

int tinybuf_plugin_set_runtime_map(const char **guids, int count){
    if(s_plugin_runtime_map){ for(int i=0;i<s_plugin_runtime_map_count;++i){ /* strings owned by DLL or builtin, no free */ } tinybuf_free(s_plugin_runtime_map); }
    s_plugin_runtime_map = NULL; s_plugin_runtime_map_count = 0;
    if(!guids || count<=0){ return 0; }
    s_plugin_runtime_map = (const char**)tinybuf_malloc(sizeof(const char*)*count);
    for(int i=0;i<count;++i){ s_plugin_runtime_map[i] = guids[i]; }
    s_plugin_runtime_map_count = count; return 0;
}

static int plugin_list_index_by_type(uint8_t type){ for(int i=0;i<s_plugins_count;++i){ for(int k=0;k<s_plugins[i].type_count;++k){ if(s_plugins[i].types[k]==type) return i; } } return -1; }
static int runtime_index_by_guid(const char *guid){ if(!guid || s_plugin_runtime_map_count<=0) return -1; for(int i=0;i<s_plugin_runtime_map_count;++i){ if(s_plugin_runtime_map[i] && strcmp(s_plugin_runtime_map[i], guid)==0) return i; } return -1; }
int tinybuf_plugin_get_runtime_index_by_type(uint8_t type){ int li = plugin_list_index_by_type(type); if(li<0) return -1; const char *g = s_plugins[li].guid; return runtime_index_by_guid(g); }

static int plugin_list_index_by_runtime_index(int runtime_index){ if(runtime_index<0 || runtime_index>=s_plugin_runtime_map_count) return -1; const char *g = s_plugin_runtime_map[runtime_index]; if(!g) return -1; for(int i=0;i<s_plugins_count;++i){ if(s_plugins[i].guid && strcmp(s_plugins[i].guid, g)==0) return i; } return -1; }
int tinybuf_plugin_do_value_op(int plugin_runtime_index, const char *name, tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out){ int li = plugin_list_index_by_runtime_index(plugin_runtime_index); if(li<0) return -1; plugin_entry *pe = &s_plugins[li]; if(!name || pe->op_count<=0) return -1; for(int i=0;i<pe->op_count;++i){ if(pe->op_names[i] && strcmp(pe->op_names[i], name)==0){ return pe->op_fns[i](value, args, out); } } return -1; }
int tinybuf_plugin_do_value_op_by_type(uint8_t type, const char *name, tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out){ int li = plugin_list_index_by_type(type); if(li<0) return -1; plugin_entry *pe = &s_plugins[li]; if(!name || pe->op_count<=0) return -1; for(int i=0;i<pe->op_count;++i){ if(pe->op_names[i] && strcmp(pe->op_names[i], name)==0){ return pe->op_fns[i](value, args, out); } } return -1; }

int tinybuf_try_read_box_with_plugins(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler){
    if(buf->size <= 0) return 0;
    uint8_t t = (uint8_t)buf->ptr[0];
    int pr = tinybuf_plugins_try_read_by_type(t, buf, out, contain_handler);
    if(pr != -1) return pr;
    return tinybuf_try_read_box(buf, out, contain_handler);
}

// builtin sample plugin: type 200 -> upper-string
#define TINYBUF_PLUGIN_UPPER_STRING 200
static int plugin_upper_to_lower(tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out);

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
    int pidx = tinybuf_plugin_get_runtime_index_by_type(TINYBUF_PLUGIN_UPPER_STRING);
    tinybuf_value_set_plugin_index(out, pidx);
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

/* tinybuf_register_builtin_plugins defined later to also register builtin customs */

#ifdef _WIN32
#include <windows.h>
typedef tinybuf_plugin_descriptor* (*get_desc_fn)(void);
int tinybuf_plugin_register_from_dll(const char *dll_path){
    HMODULE h = LoadLibraryA(dll_path);
    if(!h) return -1;
    get_desc_fn getter = (get_desc_fn)GetProcAddress(h, "tinybuf_get_plugin_descriptor");
    int r = 0;
    if(getter){
        tinybuf_plugin_descriptor *d = getter();
        if(d){
            if(d->types && d->type_count > 0 && d->read){
                r = tinybuf_plugin_register(d->types, d->type_count, d->read, d->write, d->dump, d->show_value);
                if(r==0 && s_plugins_count>0){ s_plugins[s_plugins_count-1].guid = d->guid; }
                if(r==0 && s_plugins_count>0){ plugin_entry *pe = &s_plugins[s_plugins_count-1]; pe->op_names = d->op_names; pe->op_sigs = d->op_sigs; pe->op_descs = d->op_descs; pe->op_fns = d->op_fns; pe->op_count = d->op_count; pe->read_r = d->read_r; pe->write_r = d->write_r; pe->dump_r = d->dump_r; pe->show_value_r = d->show_value_r; }
            }
        }
    }
    typedef int (*plugin_init_host_fn)(int (*host_custom_register)(const char*, tinybuf_custom_read_fn, tinybuf_custom_write_fn, tinybuf_custom_dump_fn));
    plugin_init_host_fn phost = (plugin_init_host_fn)GetProcAddress(h, "tinybuf_plugin_init_with_host");
    if(phost){ (void)phost(&tinybuf_custom_register); r = 0; }
    typedef int (*plugin_init_fn)(void);
    plugin_init_fn pinit = (plugin_init_fn)GetProcAddress(h, "tinybuf_plugin_init");
    if(!phost && pinit){ (void)pinit(); r = 0; }
    return r;
}
#else
#include <dlfcn.h>
typedef tinybuf_plugin_descriptor* (*get_desc_fn)(void);
typedef int (*plugin_init_fn)(void);
int tinybuf_plugin_register_from_dll(const char *dll_path){
    if(!dll_path) return -1;
    void *h = dlopen(dll_path, RTLD_LAZY);
    if(!h) return -1;
    get_desc_fn getter = (get_desc_fn)dlsym(h, "tinybuf_get_plugin_descriptor");
    int r = 0;
    if(getter){
        tinybuf_plugin_descriptor *d = getter();
        if(d){
            if(d->types && d->type_count > 0 && d->read){
                r = tinybuf_plugin_register(d->types, d->type_count, d->read, d->write, d->dump, d->show_value);
                if(r==0 && s_plugins_count>0){ s_plugins[s_plugins_count-1].guid = d->guid; }
                if(r==0 && s_plugins_count>0){ plugin_entry *pe = &s_plugins[s_plugins_count-1]; pe->op_names = d->op_names; pe->op_sigs = d->op_sigs; pe->op_descs = d->op_descs; pe->op_fns = d->op_fns; pe->op_count = d->op_count; pe->read_r = d->read_r; pe->write_r = d->write_r; pe->dump_r = d->dump_r; pe->show_value_r = d->show_value_r; }
            }
        }
    }
    typedef int (*plugin_init_host_fn)(int (*host_custom_register)(const char*, tinybuf_custom_read_fn, tinybuf_custom_write_fn, tinybuf_custom_dump_fn));
    plugin_init_host_fn phost = (plugin_init_host_fn)dlsym(h, "tinybuf_plugin_init_with_host");
    if(phost){ (void)phost(&tinybuf_custom_register); r = 0; }
    plugin_init_fn pinit = (plugin_init_fn)dlsym(h, "tinybuf_plugin_init");
    if(!phost && pinit){ (void)pinit(); r = 0; }
    return r;
}
#endif
int tinybuf_plugin_get_count(void){ return s_plugins_count; }
const char* tinybuf_plugin_get_guid(int index){ if(index<0 || index>=s_plugins_count) return NULL; return s_plugins[index].guid; }
static int plugin_upper_to_lower(tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out){ (void)args; buffer *s = tinybuf_value_get_string(value); int len = buffer_get_length(s); char *tmp=(char*)tinybuf_malloc(len); const char *p = buffer_get_data(s); for(int i=0;i<len;++i){ char c=p[i]; if(c>='A'&&c<='Z') c=(char)(c+('a'-'A')); tmp[i]=c; } tinybuf_value_init_string(out, tmp, len); tinybuf_free(tmp); return 0; }

typedef struct {
    char *name;
    tinybuf_custom_read_fn read;
    tinybuf_custom_write_fn write;
    tinybuf_custom_dump_fn dump;
    tinybuf_custom_read_fn_r read_r;
    tinybuf_custom_write_fn_r write_r;
    tinybuf_custom_dump_fn_r dump_r;
} custom_entry;

static custom_entry *s_customs = NULL;
static int s_customs_count = 0;
static int s_customs_capacity = 0;

typedef struct { char *name; char **op_names; char **op_sigs; char **op_descs; tinybuf_plugin_value_op_fn *op_fns; int op_count; int op_cap; tinybuf_custom_read_fn read; tinybuf_custom_write_fn write; tinybuf_custom_dump_fn dump; int is_serializable; } oop_entry;
static oop_entry *s_oop = NULL; static int s_oop_count = 0; static int s_oop_cap = 0;
static int oop_index_by_name(const char *name);

static int custom_index_by_name(const char *name){ if(!name) return -1; for(int i=0;i<s_customs_count;++i){ if(s_customs[i].name && strcmp(s_customs[i].name, name)==0) return i; } return -1; }

int tinybuf_custom_register(const char *name, tinybuf_custom_read_fn read, tinybuf_custom_write_fn write, tinybuf_custom_dump_fn dump){ if(!name || !read) return -1; int idx = custom_index_by_name(name); if(idx >= 0){ s_customs[idx].read = read; s_customs[idx].write = write; s_customs[idx].dump = dump; return 0; } if(s_customs_count == s_customs_capacity){ int newcap = s_customs_capacity ? s_customs_capacity * 2 : 8; s_customs = (custom_entry*)tinybuf_realloc(s_customs, sizeof(custom_entry) * newcap); s_customs_capacity = newcap; } custom_entry e; int nlen = (int)strlen(name); e.name = (char*)tinybuf_malloc(nlen+1); memcpy(e.name, name, nlen); e.name[nlen] = '\0'; e.read = read; e.write = write; e.dump = dump; e.read_r = NULL; e.write_r = NULL; e.dump_r = NULL; s_customs[s_customs_count++] = e; return 0; }

int tinybuf_custom_register_r(const char *name, tinybuf_custom_read_fn_r read, tinybuf_custom_write_fn_r write, tinybuf_custom_dump_fn_r dump){ if(!name || !read) return -1; int idx = custom_index_by_name(name); if(idx >= 0){ s_customs[idx].read_r = read; s_customs[idx].write_r = write; s_customs[idx].dump_r = dump; return 0; } if(s_customs_count == s_customs_capacity){ int newcap = s_customs_capacity ? s_customs_capacity * 2 : 8; s_customs = (custom_entry*)tinybuf_realloc(s_customs, sizeof(custom_entry) * newcap); s_customs_capacity = newcap; } custom_entry e; int nlen = (int)strlen(name); e.name = (char*)tinybuf_malloc(nlen+1); memcpy(e.name, name, nlen); e.name[nlen] = '\0'; e.read = NULL; e.write = NULL; e.dump = NULL; e.read_r = read; e.write_r = write; e.dump_r = dump; s_customs[s_customs_count++] = e; return 0; }

int tinybuf_custom_try_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler){ int idx = custom_index_by_name(name); if(idx >= 0 && s_customs[idx].read) return s_customs[idx].read(name, data, len, out, contain_handler); int oi = oop_index_by_name(name); if(oi>=0 && s_oop[oi].is_serializable && s_oop[oi].read) return s_oop[oi].read(name, data, len, out, contain_handler); return -1; }

static inline tinybuf_result _make_ok(int res){ return tinybuf_result_ok(res); }
static inline tinybuf_result _make_err(const char *msg, int res){ return tinybuf_result_err(res, msg, NULL); }

tinybuf_result tinybuf_custom_try_read_r(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler){ int idx = custom_index_by_name(name); if(idx >= 0 && s_customs[idx].read_r) return s_customs[idx].read_r(name, data, len, out, contain_handler); int oi = oop_index_by_name(name); if(oi>=0 && s_oop[oi].is_serializable && s_oop[oi].read){ int ir = s_oop[oi].read(name, data, len, out, contain_handler); if(ir>0) return _make_ok(ir); const char *msg = tinybuf_last_error_message(); if(!msg) msg = "custom read failed"; return _make_err(msg, ir); } if(idx >= 0 && s_customs[idx].read){ int ir = s_customs[idx].read(name, data, len, out, contain_handler); if(ir>0) return _make_ok(ir); const char *msg = tinybuf_last_error_message(); if(!msg) msg = "custom read failed"; return _make_err(msg, ir); } return _make_err("custom type not found", -1); }

tinybuf_result tinybuf_custom_try_write_r(const char *name, const tinybuf_value *in, buffer *out){ int idx = custom_index_by_name(name); if(idx >= 0 && s_customs[idx].write_r) return s_customs[idx].write_r(name, in, out); int oi = oop_index_by_name(name); if(oi>=0 && s_oop[oi].is_serializable && s_oop[oi].write){ int ir = s_oop[oi].write(name, in, out); if(ir>0) return _make_ok(ir); tinybuf_result rr = _make_err("custom write failed", ir); tinybuf_result_add_msg_const(&rr, name); return rr; } if(idx >= 0 && s_customs[idx].write){ int ir = s_customs[idx].write(name, in, out); if(ir>0) return _make_ok(ir); tinybuf_result rr = _make_err("custom write failed", ir); tinybuf_result_add_msg_const(&rr, name); return rr; } return _make_err("custom type not found", -1); }

tinybuf_result tinybuf_custom_try_dump_r(const char *name, buf_ref *buf, buffer *out){ int idx = custom_index_by_name(name); if(idx >= 0 && s_customs[idx].dump_r) return s_customs[idx].dump_r(name, buf, out); int oi = oop_index_by_name(name); if(oi>=0 && s_oop[oi].is_serializable && s_oop[oi].dump){ int ir = s_oop[oi].dump(name, buf, out); if(ir>0) return _make_ok(ir); tinybuf_result rr = _make_err("custom dump failed", ir); tinybuf_result_add_msg_const(&rr, name); return rr; } if(idx >= 0 && s_customs[idx].dump){ int ir = s_customs[idx].dump(name, buf, out); if(ir>0) return _make_ok(ir); tinybuf_result rr = _make_err("custom dump failed", ir); tinybuf_result_add_msg_const(&rr, name); return rr; } return _make_err("custom type not found", -1); }

int tinybuf_custom_try_write(const char *name, const tinybuf_value *in, buffer *out){ int idx = custom_index_by_name(name); if(idx >= 0 && s_customs[idx].write) return s_customs[idx].write(name, in, out); int oi = oop_index_by_name(name); if(oi>=0 && s_oop[oi].is_serializable && s_oop[oi].write) return s_oop[oi].write(name, in, out); return -1; }

int tinybuf_custom_try_dump(const char *name, buf_ref *buf, buffer *out){ int idx = custom_index_by_name(name); if(idx >= 0 && s_customs[idx].dump) return s_customs[idx].dump(name, buf, out); int oi = oop_index_by_name(name); if(oi>=0 && s_oop[oi].is_serializable && s_oop[oi].dump) return s_oop[oi].dump(name, buf, out); return -1; }

static int custom_string_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler){ (void)name; (void)contain_handler; tinybuf_value_init_string(out, (const char*)data, len); return len; }
static int custom_string_write(const char *name, const tinybuf_value *in, buffer *out){ (void)name; buffer *s = tinybuf_value_get_string(in); if(!s) return -1; int len = buffer_get_length(s); if(len>0){ buffer_append(out, buffer_get_data(s), len); } return len; }
static int custom_string_dump(const char *name, buf_ref *buf, buffer *out){ (void)name; int64_t len = buf->size; buffer_append(out, "\"", 1); if(len>0){ buffer_append(out, buf->ptr, (int)len); } buffer_append(out, "\"", 1); return (int)len; }

static void register_builtin_customs(void){ tinybuf_custom_register("string", custom_string_read, custom_string_write, custom_string_dump); }

static int tuple_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler){ (void)name; buf_ref br = (buf_ref){ (const char*)data, (int64_t)len, (const char*)data, (int64_t)len }; return tinybuf_try_read_box(&br, out, contain_handler); }
static int tuple_write(const char *name, const tinybuf_value *in, buffer *out){ (void)name; if(tinybuf_value_get_type(in) != tinybuf_array) return -1; return tinybuf_try_write_box(out, in); }
static int tuple_dump(const char *name, buf_ref *buf, buffer *out){ (void)name; return tinybuf_dump_buffer_as_text(buf->ptr, (int)buf->size, out); }

static int hlist_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler){ (void)name; const char *ptr = (const char*)data; int size = len; tinybuf_value_clear(out); for(;;){ if(size<=0) break; buf_ref br = (buf_ref){ (const char*)ptr, (int64_t)size, (const char*)ptr, (int64_t)size }; tinybuf_value *item = tinybuf_value_alloc(); int r = tinybuf_try_read_box(&br, item, contain_handler); if(r<=0){ tinybuf_value_free(item); return r; } if(tinybuf_value_get_type(out) != tinybuf_array){ tinybuf_value_clear(out); /* switch to array and append */ } tinybuf_value_array_append(out, item); ptr += r; size -= r; }
    return len;
}
static int hlist_write(const char *name, const tinybuf_value *in, buffer *out){ (void)name; if(tinybuf_value_get_type(in) != tinybuf_array) return -1; int before = buffer_get_length(out); int n = tinybuf_value_get_child_size(in); for(int i=0;i<n;++i){ const tinybuf_value *ch = tinybuf_value_get_array_child(in, i); if(!ch) return -1; int w = tinybuf_try_write_box(out, ch); if(w<=0) return w; } int after = buffer_get_length(out); return after - before; }
static int hlist_dump(const char *name, buf_ref *buf, buffer *out){ (void)name; return tinybuf_dump_buffer_as_text(buf->ptr, (int)buf->size, out); }

static int dataframe_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler){ (void)name; buf_ref br = (buf_ref){ (const char*)data, (int64_t)len, (const char*)data, (int64_t)len }; return tinybuf_try_read_box(&br, out, contain_handler); }
static int dataframe_write(const char *name, const tinybuf_value *in, buffer *out){ (void)name; if(tinybuf_value_get_type(in) != tinybuf_indexed_tensor) return -1; return tinybuf_try_write_box(out, in); }
static int dataframe_dump(const char *name, buf_ref *buf, buffer *out){ (void)name; return tinybuf_dump_buffer_as_text(buf->ptr, (int)buf->size, out); }
static void register_system_extend(void){ /* moved to DLL plugin: system_extend */ }

int tinybuf_register_builtin_plugins(void){
    uint8_t types[1] = { TINYBUF_PLUGIN_UPPER_STRING };
    int r = tinybuf_plugin_register(types, 1, plugin_upper_read, plugin_upper_write, plugin_upper_dump, plugin_upper_show_value);
    if(r==0){ if(s_plugins_count>0){ s_plugins[s_plugins_count-1].guid = "builtin:upper-string"; } }
    if(r==0){ if(s_plugins_count>0){ static const char *names[1] = { "to_lower" }; static const char *sigs[1] = { "string->string" }; static const char *descs[1] = { "lowercase" }; static tinybuf_plugin_value_op_fn fns[1] = { plugin_upper_to_lower }; plugin_entry *pe = &s_plugins[s_plugins_count-1]; pe->op_names = names; pe->op_sigs = sigs; pe->op_descs = descs; pe->op_fns = fns; pe->op_count = 1; } }
    register_builtin_customs();
    return r;
}

static int oop_index_by_name(const char *name){ if(!name) return -1; for(int i=0;i<s_oop_count;++i){ if(s_oop[i].name && strcmp(s_oop[i].name, name)==0) return i; } return -1; }
int tinybuf_oop_register_type(const char *type_name){ if(!type_name) return -1; int idx = oop_index_by_name(type_name); if(idx>=0) return 0; if(s_oop_count==s_oop_cap){ int nc = s_oop_cap? s_oop_cap*2 : 8; s_oop = (oop_entry*)tinybuf_realloc(s_oop, sizeof(oop_entry)*nc); s_oop_cap = nc; } int tl=(int)strlen(type_name); oop_entry e; e.name=(char*)tinybuf_malloc(tl+1); memcpy(e.name,type_name,tl); e.name[tl]='\0'; e.op_names=NULL; e.op_sigs=NULL; e.op_descs=NULL; e.op_fns=NULL; e.op_count=0; e.op_cap=0; e.read=NULL; e.write=NULL; e.dump=NULL; e.is_serializable=0; s_oop[s_oop_count++]=e; return 0; }
int tinybuf_oop_get_type_count(void){ return s_oop_count; }
const char* tinybuf_oop_get_type_name(int index){ if(index<0||index>=s_oop_count) return NULL; return s_oop[index].name; }
static int oop_ensure_cap(int idx){ if(idx<0||idx>=s_oop_count) return -1; if(s_oop[idx].op_count==s_oop[idx].op_cap){ int nc = s_oop[idx].op_cap? s_oop[idx].op_cap*2 : 4; s_oop[idx].op_names = (char**)tinybuf_realloc(s_oop[idx].op_names, sizeof(char*)*nc); s_oop[idx].op_sigs = (char**)tinybuf_realloc(s_oop[idx].op_sigs, sizeof(char*)*nc); s_oop[idx].op_descs = (char**)tinybuf_realloc(s_oop[idx].op_descs, sizeof(char*)*nc); s_oop[idx].op_fns = (tinybuf_plugin_value_op_fn*)tinybuf_realloc(s_oop[idx].op_fns, sizeof(tinybuf_plugin_value_op_fn)*nc); s_oop[idx].op_cap = nc; } return 0; }
int tinybuf_oop_register_op(const char *type_name, const char *op_name, const char *sig, const char *desc, tinybuf_plugin_value_op_fn fn){ if(!type_name||!op_name||!fn) return -1; int idx = oop_index_by_name(type_name); if(idx<0) { if(tinybuf_oop_register_type(type_name)<0) return -1; idx = oop_index_by_name(type_name); } for(int i=0;i<s_oop[idx].op_count;++i){ if(s_oop[idx].op_names[i] && strcmp(s_oop[idx].op_names[i], op_name)==0){ s_oop[idx].op_sigs[i] = NULL; if(sig){ int sl=(int)strlen(sig); s_oop[idx].op_sigs[i]=(char*)tinybuf_malloc(sl+1); memcpy(s_oop[idx].op_sigs[i],sig,sl); s_oop[idx].op_sigs[i][sl]='\0'; } s_oop[idx].op_descs[i] = NULL; if(desc){ int dl=(int)strlen(desc); s_oop[idx].op_descs[i]=(char*)tinybuf_malloc(dl+1); memcpy(s_oop[idx].op_descs[i],desc,dl); s_oop[idx].op_descs[i][dl]='\0'; } s_oop[idx].op_fns[i] = fn; return 0; } }
 if(oop_ensure_cap(idx)<0) return -1; int nl = (int)strlen(op_name); s_oop[idx].op_names[s_oop[idx].op_count] = (char*)tinybuf_malloc(nl+1); memcpy(s_oop[idx].op_names[s_oop[idx].op_count], op_name, nl); s_oop[idx].op_names[s_oop[idx].op_count][nl]='\0'; s_oop[idx].op_sigs[s_oop[idx].op_count] = NULL; if(sig){ int sl=(int)strlen(sig); s_oop[idx].op_sigs[s_oop[idx].op_count]=(char*)tinybuf_malloc(sl+1); memcpy(s_oop[idx].op_sigs[s_oop[idx].op_count],sig,sl); s_oop[idx].op_sigs[s_oop[idx].op_count][sl]='\0'; } s_oop[idx].op_descs[s_oop[idx].op_count] = NULL; if(desc){ int dl=(int)strlen(desc); s_oop[idx].op_descs[s_oop[idx].op_count]=(char*)tinybuf_malloc(dl+1); memcpy(s_oop[idx].op_descs[s_oop[idx].op_count],desc,dl); s_oop[idx].op_descs[s_oop[idx].op_count][dl]='\0'; } s_oop[idx].op_fns[s_oop[idx].op_count] = fn; s_oop[idx].op_count++; return 0; }
int tinybuf_oop_get_op_count(const char *type_name){ int idx = oop_index_by_name(type_name); if(idx<0) return -1; return s_oop[idx].op_count; }
int tinybuf_oop_get_op_meta(const char *type_name, int index, const char **name, const char **sig, const char **desc){ int idx = oop_index_by_name(type_name); if(idx<0) return -1; if(index<0 || index>=s_oop[idx].op_count) return -1; if(name) *name = s_oop[idx].op_names[index]; if(sig) *sig = s_oop[idx].op_sigs[index]; if(desc) *desc = s_oop[idx].op_descs[index]; return 0; }
static int oop_find_op_index(int idx, const char *op_name){ if(idx<0||idx>=s_oop_count||!op_name) return -1; for(int i=0;i<s_oop[idx].op_count;++i){ if(s_oop[idx].op_names[i] && strcmp(s_oop[idx].op_names[i], op_name)==0) return i; } return -1; }
int tinybuf_oop_do_op(const char *type_name, const char *op_name, tinybuf_value *self, const tinybuf_value *args, tinybuf_value *out){ int idx = oop_index_by_name(type_name); if(idx<0) return -1; int oi = oop_find_op_index(idx, op_name); if(oi<0) return -1; if(!s_oop[idx].op_fns[oi]) return -1; return s_oop[idx].op_fns[oi](self, args, out); }
int tinybuf_oop_attach_serializers(const char *type_name, tinybuf_custom_read_fn read, tinybuf_custom_write_fn write, tinybuf_custom_dump_fn dump){ if(!type_name||!read) return -1; int idx = oop_index_by_name(type_name); if(idx<0){ if(tinybuf_oop_register_type(type_name)<0) return -1; idx = oop_index_by_name(type_name); } s_oop[idx].read = read; s_oop[idx].write = write; s_oop[idx].dump = dump; return 0; }
int tinybuf_oop_register_types_to_custom(void){ int count=0; for(int i=0;i<s_oop_count;++i){ if(s_oop[i].name && s_oop[i].read){ tinybuf_custom_register(s_oop[i].name, s_oop[i].read, s_oop[i].write, s_oop[i].dump); ++count; } } return count; }
int tinybuf_oop_set_serializable(const char *type_name, int serializable){ int idx = oop_index_by_name(type_name); if(idx<0){ if(tinybuf_oop_register_type(type_name)<0) return -1; idx = oop_index_by_name(type_name); } s_oop[idx].is_serializable = serializable ? 1 : 0; return 0; }
int tinybuf_plugin_scan_dir(const char *dir){
#ifdef _WIN32
    if(!dir) return -1;
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*.dll", dir);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pattern, &fd);
    if(h == INVALID_HANDLE_VALUE) return 0;
    int count = 0;
    do{
        char full[MAX_PATH];
        snprintf(full, sizeof(full), "%s\\%s", dir, fd.cFileName);
        if(tinybuf_plugin_register_from_dll(full) == 0) ++count;
    }while(FindNextFileA(h, &fd));
    FindClose(h);
    return count;
#else
    if(!dir) return -1;
    DIR *dh = opendir(dir);
    if(!dh) return 0;
    int count = 0;
    struct dirent *ent;
    while((ent = readdir(dh))){
        const char *name = ent->d_name;
        int len = (int)strlen(name);
        if(len > 3 && strcmp(name + len - 3, ".so") == 0){
            char full[1024];
            snprintf(full, sizeof(full), "%s/%s", dir, name);
            if(tinybuf_plugin_register_from_dll(full) == 0) ++count;
        }
    }
    closedir(dh);
    return count;
#endif
}

int tinybuf_init(void){
    tinybuf_register_builtin_plugins();
    tinybuf_plugin_scan_dir("tinybuf_plugins");
    tinybuf_plugin_scan_dir("../tinybuf_plugins");
    return 0;
}
