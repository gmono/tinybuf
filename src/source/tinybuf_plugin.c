// plugin system implementation compatible with core tryread/trywrite
#include "tinybuf_plugin.h"
#include "dyn_sys.h"
#include "tinybuf.h"
#include "tinybuf_buffer.h"
#include "tinybuf_memory.h"
struct hole_string;
#include "tinybuf_support.h"
#include <string.h>
#include <stdio.h>
#ifndef _WIN32
#include <dirent.h>
#endif
/* non-core result helpers moved to tinybuf_result.c */

typedef struct
{
    uint8_t *tags;
    int tag_count;
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
} plugin_entry;

static plugin_entry *s_plugins = NULL;
static int s_plugins_count = 0;
static int s_plugins_capacity = 0;
static const char **s_plugin_runtime_map = NULL;
static int s_plugin_runtime_map_count = 0;

static inline int buf_offset_local(buf_ref *buf, int64_t offset)
{
    if (offset < 0 || offset > buf->size)
        return -1;
    buf->ptr += offset;
    buf->size -= offset;
    return 0;
}

int tinybuf_plugin_register(const uint8_t *tags, int tag_count, tinybuf_plugin_read_fn read, tinybuf_plugin_write_fn write, tinybuf_plugin_dump_fn dump, tinybuf_plugin_show_value_fn show_value)
{
    if (!tags || tag_count <= 0 || !read)
        return -1;
    if (s_plugins_count == s_plugins_capacity)
    {
        int newcap = s_plugins_capacity ? s_plugins_capacity * 2 : 8;
        s_plugins = (plugin_entry *)tinybuf_realloc(s_plugins, sizeof(plugin_entry) * newcap);
        s_plugins_capacity = newcap;
    }
    plugin_entry e;
    e.tags = (uint8_t *)tinybuf_malloc(tag_count);
    memcpy(e.tags, tags, (size_t)tag_count);
    e.tag_count = tag_count;
    e.guid = NULL;
    e.read = read;
    e.write = write;
    e.dump = dump;
    e.show_value = show_value;
    e.op_names = NULL;
    e.op_sigs = NULL;
    e.op_descs = NULL;
    e.op_fns = NULL;
    e.op_count = 0;
    s_plugins[s_plugins_count++] = e;
    return 0;
}

/* result container helpers moved to tinybuf_result.c */

/* result helpers moved to tinybuf_result.c */

int tinybuf_plugin_unregister_all(void)
{
    for (int i = 0; i < s_plugins_count; ++i)
    {
        tinybuf_free(s_plugins[i].tags);
    }
    tinybuf_free(s_plugins);
    s_plugins = NULL;
    s_plugins_count = 0;
    s_plugins_capacity = 0;
    return 0;
}

static inline void _push_plugin_msg(tinybuf_error *rr, int li)
{
    if (li >= 0 && s_plugins[li].guid)
    {
        tinybuf_result_add_msg_const(rr, s_plugins[li].guid);
    }
}

int tinybuf_plugins_try_read_by_tag(uint8_t tag, buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_error *r)
{
    for (int i = 0; i < s_plugins_count; ++i)
    {
        for (int k = 0; k < s_plugins[i].tag_count; ++k)
        {
            if (s_plugins[i].tags[k] == tag)
            {
                int n = s_plugins[i].read(tag, buf, out, contain_handler, r);
                if (n > 0)
                    return n;
                tinybuf_error er = tinybuf_result_err(n, "plugin read failed", NULL);
                _push_plugin_msg(&er, i);
                tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
                return n;
            }
        }
    }
    tinybuf_error er = tinybuf_result_err(-1, "plugin tag not found", NULL);
    tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
    return -1;
}

int tinybuf_plugins_try_write(uint8_t tag, const tinybuf_value *in, buffer *out, tinybuf_error *r)
{
    for (int i = 0; i < s_plugins_count; ++i)
    {
        for (int k = 0; k < s_plugins[i].tag_count; ++k)
        {
            if (s_plugins[i].tags[k] == tag)
            {
                int n = s_plugins[i].write(tag, in, out, r);
                if (n >= 0)
                    return n;
                tinybuf_error er = tinybuf_result_err(n, "plugin write failed", NULL);
                _push_plugin_msg(&er, i);
                tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
                return n;
            }
        }
    }
    tinybuf_error er = tinybuf_result_err(-1, "plugin tag not found", NULL);
    tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
    return -1;
}

int tinybuf_plugins_try_dump_by_tag(uint8_t tag, buf_ref *buf, buffer *out, tinybuf_error *r)
{
    for (int i = 0; i < s_plugins_count; ++i)
    {
        for (int k = 0; k < s_plugins[i].tag_count; ++k)
        {
            if (s_plugins[i].tags[k] == tag)
            {
                int n = s_plugins[i].dump(tag, buf, out, r);
                if (n > 0)
                    return n;
                tinybuf_error er = tinybuf_result_err(n, "plugin dump failed", NULL);
                _push_plugin_msg(&er, i);
                tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
                return n;
            }
        }
    }
    tinybuf_error er = tinybuf_result_err(-1, "plugin tag not found", NULL);
    tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
    return -1;
}

int tinybuf_plugins_try_show_value(uint8_t tag, const tinybuf_value *in, buffer *out, tinybuf_error *r)
{
    for (int i = 0; i < s_plugins_count; ++i)
    {
        for (int k = 0; k < s_plugins[i].tag_count; ++k)
        {
            if (s_plugins[i].tags[k] == tag)
            {
                int n = s_plugins[i].show_value(tag, in, out, r);
                if (n > 0)
                    return n;
                tinybuf_error er = tinybuf_result_err(n, "plugin show failed", NULL);
                _push_plugin_msg(&er, i);
                tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
                return n;
            }
        }
    }
    tinybuf_error er = tinybuf_result_err(-1, "plugin tag not found", NULL);
    tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
    return -1;
}

int tinybuf_plugin_set_runtime_map(const char **guids, int count)
{
    if (s_plugin_runtime_map)
    {
        for (int i = 0; i < s_plugin_runtime_map_count; ++i)
        { /* strings owned by DLL or builtin, no free */
        }
        tinybuf_free(s_plugin_runtime_map);
    }
    s_plugin_runtime_map = NULL;
    s_plugin_runtime_map_count = 0;
    if (!guids || count <= 0)
    {
        return 0;
    }
    s_plugin_runtime_map = (const char **)tinybuf_malloc(sizeof(const char *) * count);
    for (int i = 0; i < count; ++i)
    {
        s_plugin_runtime_map[i] = guids[i];
    }
    s_plugin_runtime_map_count = count;
    return 0;
}

static int plugin_list_index_by_tag(uint8_t tag)
{
    for (int i = 0; i < s_plugins_count; ++i)
    {
        for (int k = 0; k < s_plugins[i].tag_count; ++k)
        {
            if (s_plugins[i].tags[k] == tag)
                return i;
        }
    }
    return -1;
}
static int runtime_index_by_guid(const char *guid)
{
    if (!guid || s_plugin_runtime_map_count <= 0)
        return -1;
    for (int i = 0; i < s_plugin_runtime_map_count; ++i)
    {
        if (s_plugin_runtime_map[i] && strcmp(s_plugin_runtime_map[i], guid) == 0)
            return i;
    }
    return -1;
}
static int plugin_list_index_by_guid(const char *guid)
{
    if (!guid)
        return -1;
    for (int i = 0; i < s_plugins_count; ++i)
    {
        if (s_plugins[i].guid && strcmp(s_plugins[i].guid, guid) == 0)
            return i;
    }
    return -1;
}
int tinybuf_plugin_get_runtime_index_by_tag(uint8_t tag)
{
    int li = plugin_list_index_by_tag(tag);
    if (li < 0)
        return -1;
    const char *g = s_plugins[li].guid;
    return runtime_index_by_guid(g);
}
const char *tinybuf_plugin_get_guid_by_tag(uint8_t tag)
{
    int li = plugin_list_index_by_tag(tag);
    if (li < 0)
        return NULL;
    return s_plugins[li].guid;
}

static int plugin_list_index_by_runtime_index(int runtime_index)
{
    if (runtime_index < 0 || runtime_index >= s_plugin_runtime_map_count)
        return -1;
    const char *g = s_plugin_runtime_map[runtime_index];
    if (!g)
        return -1;
    for (int i = 0; i < s_plugins_count; ++i)
    {
        if (s_plugins[i].guid && strcmp(s_plugins[i].guid, g) == 0)
            return i;
    }
    return -1;
}
int tinybuf_plugin_do_value_op(int plugin_runtime_index, const char *name, tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out)
{
    int li = plugin_list_index_by_runtime_index(plugin_runtime_index);
    if (li < 0)
        return -1;
    plugin_entry *pe = &s_plugins[li];
    if (!name || pe->op_count <= 0)
        return -1;
    for (int i = 0; i < pe->op_count; ++i)
    {
        if (pe->op_names[i] && strcmp(pe->op_names[i], name) == 0)
        {
            return pe->op_fns[i](value, args, out);
        }
    }
    return -1;
}
int tinybuf_plugin_do_value_op_by_tag(uint8_t tag, const char *name, tinybuf_value *value, const tinybuf_value *args, tinybuf_value *out)
{
    int li = plugin_list_index_by_tag(tag);
    if (li < 0)
        return -1;
    plugin_entry *pe = &s_plugins[li];
    if (!name || pe->op_count <= 0)
        return -1;
    for (int i = 0; i < pe->op_count; ++i)
    {
        if (pe->op_names[i] && strcmp(pe->op_names[i], name) == 0)
        {
            return pe->op_fns[i](value, args, out);
        }
    }
    return -1;
}

int tinybuf_plugins_try_read_by_name(const char *name, buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_error *r)
{
    int li = plugin_list_index_by_guid(name);
    if (li < 0)
        return -1;
    uint8_t tag = s_plugins[li].tags && s_plugins[li].tag_count > 0 ? s_plugins[li].tags[0] : 0;
    return s_plugins[li].read(tag, buf, out, contain_handler, r);
}
int tinybuf_plugins_try_write_by_name(const char *name, const tinybuf_value *in, buffer *out, tinybuf_error *r)
{
    int li = plugin_list_index_by_guid(name);
    if (li < 0)
        return -1;
    uint8_t tag = s_plugins[li].tags && s_plugins[li].tag_count > 0 ? s_plugins[li].tags[0] : 0;
    return s_plugins[li].write(tag, in, out, r);
}
int tinybuf_plugins_try_dump_by_name(const char *name, buf_ref *buf, buffer *out, tinybuf_error *r)
{
    int li = plugin_list_index_by_guid(name);
    if (li < 0)
        return -1;
    uint8_t tag = s_plugins[li].tags && s_plugins[li].tag_count > 0 ? s_plugins[li].tags[0] : 0;
    return s_plugins[li].dump(tag, buf, out, r);
}
int tinybuf_try_read_box_with_plugins(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_error *r)
{
    if (buf->size <= 0)
    {
        tinybuf_error er = tinybuf_result_err(0, "buffer too small", NULL);
        tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
        return 0;
    }
    uint8_t t = (uint8_t)buf->ptr[0];
    int pr = tinybuf_plugins_try_read_by_tag(t, buf, out, contain_handler, r);
    if (pr != -1)
        return pr;
    int len = tinybuf_try_read_box(buf, out, contain_handler, r);
    if (len <= 0)
        tinybuf_result_add_msg_const(r, "tinybuf_try_read_box_with_plugins_r");
    return len;
}


/* tinybuf_register_builtin_plugins defined later to also register builtin customs */

#ifdef _WIN32
#include <windows.h>
#include <crtdbg.h>
typedef tinybuf_plugin_descriptor *(*get_desc_fn)(void);
int tinybuf_plugin_register_from_dll(const char *dll_path)
{
    HMODULE h = LoadLibraryA(dll_path);
    if (!h)
        return -1;
    get_desc_fn getter = (get_desc_fn)GetProcAddress(h, "tinybuf_get_plugin_descriptor");
    int r = 0;
    if (getter)
    {
        tinybuf_plugin_descriptor *d = getter();
        if (d)
        {
            if (d->tags && d->tag_count > 0 && d->read)
            {
                r = tinybuf_plugin_register(d->tags, d->tag_count, d->read, d->write, d->dump, d->show_value);
                if (r == 0 && s_plugins_count > 0)
                {
                    s_plugins[s_plugins_count - 1].guid = d->guid;
                }
                if (r == 0 && s_plugins_count > 0)
                {
                    plugin_entry *pe = &s_plugins[s_plugins_count - 1];
                    pe->op_names = d->op_names;
                    pe->op_sigs = d->op_sigs;
                    pe->op_descs = d->op_descs;
                    pe->op_fns = d->op_fns;
                    pe->op_count = d->op_count;
                }
            }
        }
    }
    typedef int (*plugin_init_host_fn)(int (*host_custom_register)(const char *, tinybuf_custom_read_fn, tinybuf_custom_write_fn, tinybuf_custom_dump_fn));
    plugin_init_host_fn phost = (plugin_init_host_fn)GetProcAddress(h, "tinybuf_plugin_init_with_host");
    if (phost)
    {
        (void)phost(&tinybuf_custom_register);
        r = 0;
    }
    typedef int (*plugin_init_fn)(void);
    plugin_init_fn pinit = (plugin_init_fn)GetProcAddress(h, "tinybuf_plugin_init");
    if (!phost && pinit)
    {
        (void)pinit();
        r = 0;
    }
    return r;
}
#else
#include <dlfcn.h>
typedef tinybuf_plugin_descriptor *(*get_desc_fn)(void);
typedef int (*plugin_init_fn)(void);
int tinybuf_plugin_register_from_dll(const char *dll_path)
{
    if (!dll_path)
        return -1;
    void *h = dlopen(dll_path, RTLD_LAZY);
    if (!h)
        return -1;
    get_desc_fn getter = (get_desc_fn)dlsym(h, "tinybuf_get_plugin_descriptor");
    int r = 0;
    if (getter)
    {
        tinybuf_plugin_descriptor *d = getter();
        if (d)
        {
            if (d->tags && d->tag_count > 0 && d->read)
            {
                r = tinybuf_plugin_register(d->tags, d->tag_count, d->read, d->write, d->dump, d->show_value);
                if (r == 0 && s_plugins_count > 0)
                {
                    s_plugins[s_plugins_count - 1].guid = d->guid;
                }
                if (r == 0 && s_plugins_count > 0)
                {
                    plugin_entry *pe = &s_plugins[s_plugins_count - 1];
                    pe->op_names = d->op_names;
                    pe->op_sigs = d->op_sigs;
                    pe->op_descs = d->op_descs;
                    pe->op_fns = d->op_fns;
                    pe->op_count = d->op_count;
                }
            }
        }
    }
    typedef int (*plugin_init_host_fn)(int (*host_custom_register)(const char *, tinybuf_custom_read_fn, tinybuf_custom_write_fn, tinybuf_custom_dump_fn));
    plugin_init_host_fn phost = (plugin_init_host_fn)dlsym(h, "tinybuf_plugin_init_with_host");
    if (phost)
    {
        (void)phost(&tinybuf_custom_register);
        r = 0;
    }
    plugin_init_fn pinit = (plugin_init_fn)dlsym(h, "tinybuf_plugin_init");
    if (!phost && pinit)
    {
        (void)pinit();
        r = 0;
    }
    return r;
}
#endif
int tinybuf_plugin_get_count(void)
{
    return s_plugins_count;
}

const char *tinybuf_plugin_get_guid(int index)
{
    if (index < 0 || index >= s_plugins_count)
        return NULL;
    return s_plugins[index].guid;
}
int tinybuf_plugin_register_descriptor(const tinybuf_plugin_descriptor *d)
{
    if (!d || !d->tags || d->tag_count <= 0 || !d->read)
        return -1;
    int r = tinybuf_plugin_register(d->tags, d->tag_count, d->read, d->write, d->dump, d->show_value);
    if (r == 0 && s_plugins_count > 0)
    {
        s_plugins[s_plugins_count - 1].guid = d->guid;
        plugin_entry *pe = &s_plugins[s_plugins_count - 1];
        pe->op_names = d->op_names;
        pe->op_sigs = d->op_sigs;
        pe->op_descs = d->op_descs;
        pe->op_fns = d->op_fns;
        pe->op_count = d->op_count;
    }
    return r;
}

typedef struct
{
    char *name;
    tinybuf_custom_read_fn read;
    tinybuf_custom_write_fn write;
    tinybuf_custom_dump_fn dump;
} custom_entry;

static custom_entry *s_customs = NULL;
static int s_customs_count = 0;
static int s_customs_capacity = 0;

/* runtime OOP moved to dyn_sys */

static int custom_index_by_name(const char *name)
{
    if (!name)
        return -1;
    for (int i = 0; i < s_customs_count; ++i)
    {
        if (s_customs[i].name && strcmp(s_customs[i].name, name) == 0)
            return i;
    }
    return -1;
}

int tinybuf_custom_register(const char *name, tinybuf_custom_read_fn read, tinybuf_custom_write_fn write, tinybuf_custom_dump_fn dump)
{
    if (!name || !read)
        return -1;
    int idx = custom_index_by_name(name);
    if (idx >= 0)
    {
        s_customs[idx].read = read;
        s_customs[idx].write = write;
        s_customs[idx].dump = dump;
        return 0;
    }
    if (s_customs_count == s_customs_capacity)
    {
        int newcap = s_customs_capacity ? s_customs_capacity * 2 : 8;
        s_customs = (custom_entry *)tinybuf_realloc(s_customs, sizeof(custom_entry) * newcap);
        s_customs_capacity = newcap;
    }
    custom_entry e;
    int nlen = (int)strlen(name);
    e.name = (char *)tinybuf_malloc(nlen + 1);
    memcpy(e.name, name, nlen);
    e.name[nlen] = '\0';
    e.read = read;
    e.write = write;
    e.dump = dump;
    s_customs[s_customs_count++] = e;
    return 0;
}

/* removed int variant: tinybuf_custom_try_read */

static inline tinybuf_error _make_ok(int res) { return tinybuf_result_ok(res); }
static inline tinybuf_error _make_err(const char *msg, int res) { return tinybuf_result_err(res, msg, NULL); }

int tinybuf_custom_try_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_error *r)
{
    int idx = custom_index_by_name(name);
    if (idx >= 0 && s_customs[idx].read)
    {
        {
        }
        int rr = s_customs[idx].read(name, data, len, out, contain_handler, r);
        if (rr > 0)
        {
            tinybuf_error ok = tinybuf_result_ok(rr);
            tinybuf_result_append_merge(r, &ok, tinybuf_merger_sum);
            return rr;
        }
        const char *msg = tinybuf_last_error_message();
        if (!msg)
            msg = "custom read failed";
        tinybuf_error er = _make_err(msg, rr);
        if (name)
            tinybuf_result_add_msg_const(&er, name);
        tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
        return rr;
    }
    tinybuf_custom_read_fn oread = NULL; tinybuf_custom_write_fn owrite = NULL; tinybuf_custom_dump_fn odump = NULL; int serializable = 0;
    if (name && tinybuf_oop_get_serializers(name, &oread, &owrite, &odump, &serializable) == 0 && serializable && oread)
    {
        {
        }
        int rr = oread(name, data, len, out, contain_handler, r);
        if (rr > 0)
        {
            tinybuf_error ok = tinybuf_result_ok(rr);
            tinybuf_result_append_merge(r, &ok, tinybuf_merger_sum);
            return rr;
        }
        const char *msg = tinybuf_last_error_message();
        if (!msg)
            msg = "custom read failed";
        tinybuf_error er = _make_err(msg, rr);
        if (name)
            tinybuf_result_add_msg_const(&er, name);
        tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
        return rr;
    }
    tinybuf_error er = _make_err("custom type not found", -1);
    tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
    return -1;
}

int tinybuf_custom_try_write(const char *name, const tinybuf_value *in, buffer *out, tinybuf_error *r)
{
    int idx = custom_index_by_name(name);
    if (idx >= 0 && s_customs[idx].write)
    {
        {
        }
        int rr = s_customs[idx].write(name, in, out, r);
        if (rr > 0)
        {
            tinybuf_error ok = tinybuf_result_ok(rr);
            tinybuf_result_append_merge(r, &ok, tinybuf_merger_sum);
            return rr;
        }
        tinybuf_error er = _make_err("custom write failed", rr);
        tinybuf_result_add_msg_const(&er, name);
        tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
        return rr;
    }
    tinybuf_custom_read_fn oread = NULL; tinybuf_custom_write_fn owrite = NULL; tinybuf_custom_dump_fn odump = NULL; int serializable = 0;
    if (name && tinybuf_oop_get_serializers(name, &oread, &owrite, &odump, &serializable) == 0 && serializable && owrite)
    {
        {
        }
        int rr = owrite(name, in, out, r);
        if (rr > 0)
        {
            tinybuf_error ok = tinybuf_result_ok(rr);
            tinybuf_result_append_merge(r, &ok, tinybuf_merger_sum);
            return rr;
        }
        /* OOP provided but returned <=0, fall back to legacy */
    }
    int fb = tinybuf_try_write_box(out, in, r);
    if (fb > 0)
    {
        tinybuf_error ok = tinybuf_result_ok(fb);
        tinybuf_result_append_merge(r, &ok, tinybuf_merger_sum);
        return fb;
    }
    tinybuf_error er = _make_err("custom type not found", fb);
    tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
    return fb;
}

int tinybuf_custom_try_dump(const char *name, buf_ref *buf, buffer *out, tinybuf_error *r)
{
    tinybuf_custom_read_fn oread = NULL; tinybuf_custom_write_fn owrite = NULL; tinybuf_custom_dump_fn odump = NULL; int serializable = 0;
    if (name && tinybuf_oop_get_serializers(name, &oread, &owrite, &odump, &serializable) == 0 && serializable && odump)
    {
        int rr = odump(name, buf, out, r);
        if (rr > 0)
        {
            tinybuf_error ok = tinybuf_result_ok(rr);
            tinybuf_result_append_merge(r, &ok, tinybuf_merger_sum);
            return rr;
        }
        tinybuf_error er = _make_err("custom dump failed", rr);
        tinybuf_result_add_msg_const(&er, name);
        tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
        return rr;
    }
    int idx = custom_index_by_name(name);
    if (idx >= 0 && s_customs[idx].dump)
    {
        int rr = s_customs[idx].dump(name, buf, out, r);
        if (rr > 0)
        {
            tinybuf_error ok = tinybuf_result_ok(rr);
            tinybuf_result_append_merge(r, &ok, tinybuf_merger_sum);
            return rr;
        }
        tinybuf_error er = _make_err("custom dump failed", rr);
        tinybuf_result_add_msg_const(&er, name);
        tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
        return rr;
    }
    tinybuf_error er = _make_err("custom type not found", -1);
    tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
    return -1;
}

/* removed example custom implementations */



int tinybuf_oop_register_types_to_custom(void)
{
    int count = 0;
    int n = tinybuf_oop_get_type_count();
    for (int i = 0; i < n; ++i)
    {
        const char *name = tinybuf_oop_get_type_name(i);
        if (!name)
            continue;
        tinybuf_custom_read_fn read = NULL;
        tinybuf_custom_write_fn write = NULL;
        tinybuf_custom_dump_fn dump = NULL;
        int serializable = 0;
        if (tinybuf_oop_get_serializers(name, &read, &write, &dump, &serializable) == 0 && serializable && read)
        {
            tinybuf_custom_register(name, read, write, dump);
            ++count;
        }
    }
    return count;
}
int tinybuf_plugin_scan_dir(const char *dir)
{
#ifdef _WIN32
    if (!dir)
        return -1;
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*.dll", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    int count = 0;
    do
    {
        char full[MAX_PATH];
        snprintf(full, sizeof(full), "%s\\%s", dir, fd.cFileName);
        if (tinybuf_plugin_register_from_dll(full) == 0)
            ++count;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return count;
#else
    if (!dir)
        return -1;
    DIR *dh = opendir(dir);
    if (!dh)
        return 0;
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dh)))
    {
        const char *name = ent->d_name;
        int len = (int)strlen(name);
        if (len > 3 && strcmp(name + len - 3, ".so") == 0)
        {
            char full[1024];
            snprintf(full, sizeof(full), "%s/%s", dir, name);
            if (tinybuf_plugin_register_from_dll(full) == 0)
                ++count;
        }
    }
    closedir(dh);
    return count;
#endif
}

int tinybuf_init(void)
{
    tinybuf_plugin_scan_dir("tinybuf_plugins");
    tinybuf_plugin_scan_dir("../tinybuf_plugins");
    return 0;
}
