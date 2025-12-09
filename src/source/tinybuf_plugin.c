#include "tinybuf_plugin.h"
#include "tinybuf_memory.h"
#include <string.h>
#ifdef _WIN32
// avoid heavy windows headers to prevent macro conflicts
#else
#include <dlfcn.h>
#endif

typedef struct {
    uint8_t *type_list;
    int type_count;
    tinybuf_plugin_read_fn read;
    tinybuf_plugin_write_fn write;
} plugin_entry;

static plugin_entry *s_plugins = NULL;
static int s_plugin_count = 0;

static plugin_entry *find_plugin(uint8_t type){
    int i;
    for(i=0;i<s_plugin_count;++i){
        plugin_entry *p = &s_plugins[i];
        int j;
        for(j=0;j<p->type_count;++j){
            if(p->type_list[j] == type){
                return p;
            }
        }
    }
    return NULL;
}

static inline int buf_offset(buf_ref *buf, int64_t offset){
    const char *temp = buf->ptr;
    buf->ptr += offset;
    if(!(buf->ptr >= buf->base && buf->ptr < buf->base + buf->all_size)){
        buf->ptr = temp;
        return -1;
    }
    buf->size -= offset;
    return 0;
}

int tinybuf_plugin_register(const uint8_t *types, int type_count, tinybuf_plugin_read_fn read, tinybuf_plugin_write_fn write){
    if(!types || type_count <= 0){
        return -1;
    }
    plugin_entry *new_list = (plugin_entry *)tinybuf_realloc(s_plugins, sizeof(plugin_entry) * (s_plugin_count + 1));
    if(!new_list){
        return -1;
    }
    s_plugins = new_list;
    plugin_entry *p = &s_plugins[s_plugin_count];
    p->type_list = (uint8_t *)tinybuf_malloc(type_count);
    if(!p->type_list){
        return -1;
    }
    memcpy(p->type_list, types, type_count);
    p->type_count = type_count;
    p->read = read;
    p->write = write;
    s_plugin_count += 1;
    return 0;
}

int tinybuf_plugin_unregister_all(void){
    int i;
    for(i=0;i<s_plugin_count;++i){
        if(s_plugins[i].type_list){
            tinybuf_free(s_plugins[i].type_list);
        }
    }
    tinybuf_free(s_plugins);
    s_plugins = NULL;
    s_plugin_count = 0;
    return 0;
}

int tinybuf_plugins_try_read_by_type(uint8_t type, buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler){
    plugin_entry *p = find_plugin(type);
    if(!p || !p->read){
        return -1;
    }
    {
        int consumed = p->read(type, buf, out, contain_handler);
        if(consumed <= 0){
            return -1;
        }
        return consumed;
    }
}

int tinybuf_plugins_try_write(uint8_t type, const tinybuf_value *in, buffer *out){
    plugin_entry *p = find_plugin(type);
    if(!p || !p->write){
        return -1;
    }
    if(buffer_push(out, (char)type) != 0){
        return -1;
    }
    int w = p->write(type, in, out);
    if(w <= 0){
        return -1;
    }
    return w + 1;
}

int tinybuf_try_read_box_with_plugins(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler){
    int len = tinybuf_value_deserialize(buf->ptr, (int)buf->size, out);
    if(len == 0){
        return 0;
    }
    if(len > 0){
        buf_offset(buf, len);
        return len;
    }
    if(buf->size < 1){
        return 0;
    }
    uint8_t type = (uint8_t)buf->ptr[0];
    if(buf_offset(buf, 1) != 0){
        return -1;
    }
    int r = tinybuf_plugins_try_read_by_type(type, buf, out, contain_handler);
    if(r > 0){
        return r + 1;
    }
    buf_offset(buf, -1);
    return -1;
}

int tinybuf_plugin_register_from_dll(const char *dll_path){
    if(!dll_path){
        return -1;
    }
#ifdef _WIN32
    return -1; // stub on Windows
#else
    void *h = dlopen(dll_path, RTLD_NOW);
    if(!h){
        return -1;
    }
    void (*init_fn)(void) = (void (*)(void))dlsym(h, "tinybuf_plugin_init");
    if(!init_fn){
        dlclose(h);
        return -1;
    }
    init_fn();
    return 0;
#endif
}
