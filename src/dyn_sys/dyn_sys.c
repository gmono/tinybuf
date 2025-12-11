#include "dyn_sys.h"
#include "tinybuf_buffer.h"
#include "tinybuf_memory.h"
#include <string.h>

typedef struct
{
    char *name;
    char **op_names;
    char **op_sigs;
    char **op_descs;
    tinybuf_plugin_value_op_fn *op_fns;
    int op_count;
    int op_cap;
    tinybuf_custom_read_fn read;
    tinybuf_custom_write_fn write;
    tinybuf_custom_dump_fn dump;
    int is_serializable;
} oop_entry;
static oop_entry *s_oop = NULL;
static int s_oop_count = 0;
static int s_oop_cap = 0;

static int oop_index_by_name(const char *name)
{
    if (!name)
        return -1;
    for (int i = 0; i < s_oop_count; ++i)
    {
        if (s_oop[i].name && strcmp(s_oop[i].name, name) == 0)
            return i;
    }
    return -1;
}
int tinybuf_oop_register_type(const char *type_name)
{
    if (!type_name)
        return -1;
    int idx = oop_index_by_name(type_name);
    if (idx >= 0)
        return 0;
    if (s_oop_count == s_oop_cap)
    {
        int nc = s_oop_cap ? s_oop_cap * 2 : 8;
        s_oop = (oop_entry *)tinybuf_realloc(s_oop, sizeof(oop_entry) * nc);
        s_oop_cap = nc;
    }
    int tl = (int)strlen(type_name);
    oop_entry e;
    e.name = (char *)tinybuf_malloc(tl + 1);
    memcpy(e.name, type_name, tl);
    e.name[tl] = '\0';
    e.op_names = NULL;
    e.op_sigs = NULL;
    e.op_descs = NULL;
    e.op_fns = NULL;
    e.op_count = 0;
    e.op_cap = 0;
    e.read = NULL;
    e.write = NULL;
    e.dump = NULL;
    e.is_serializable = 0;
    s_oop[s_oop_count++] = e;
    return 0;
}
int tinybuf_oop_get_type_count(void) { return s_oop_count; }
const char *tinybuf_oop_get_type_name(int index)
{
    if (index < 0 || index >= s_oop_count)
        return NULL;
    return s_oop[index].name;
}
static int oop_ensure_cap(int idx)
{
    if (idx < 0 || idx >= s_oop_count)
        return -1;
    if (s_oop[idx].op_count == s_oop[idx].op_cap)
    {
        int nc = s_oop[idx].op_cap ? s_oop[idx].op_cap * 2 : 4;
        s_oop[idx].op_names = (char **)tinybuf_realloc(s_oop[idx].op_names, sizeof(char *) * nc);
        s_oop[idx].op_sigs = (char **)tinybuf_realloc(s_oop[idx].op_sigs, sizeof(char *) * nc);
        s_oop[idx].op_descs = (char **)tinybuf_realloc(s_oop[idx].op_descs, sizeof(char *) * nc);
        s_oop[idx].op_fns = (tinybuf_plugin_value_op_fn *)tinybuf_realloc(s_oop[idx].op_fns, sizeof(tinybuf_plugin_value_op_fn) * nc);
        s_oop[idx].op_cap = nc;
    }
    return 0;
}
int tinybuf_oop_register_op(const char *type_name, const char *op_name, const char *sig, const char *desc, tinybuf_plugin_value_op_fn fn)
{
    if (!type_name || !op_name || !fn)
        return -1;
    int idx = oop_index_by_name(type_name);
    if (idx < 0)
    {
        if (tinybuf_oop_register_type(type_name) < 0)
            return -1;
        idx = oop_index_by_name(type_name);
    }
    for (int i = 0; i < s_oop[idx].op_count; ++i)
    {
        if (s_oop[idx].op_names[i] && strcmp(s_oop[idx].op_names[i], op_name) == 0)
        {
            s_oop[idx].op_sigs[i] = NULL;
            if (sig)
            {
                int sl = (int)strlen(sig);
                s_oop[idx].op_sigs[i] = (char *)tinybuf_malloc(sl + 1);
                memcpy(s_oop[idx].op_sigs[i], sig, sl);
                s_oop[idx].op_sigs[i][sl] = '\0';
            }
            s_oop[idx].op_descs[i] = NULL;
            if (desc)
            {
                int dl = (int)strlen(desc);
                s_oop[idx].op_descs[i] = (char *)tinybuf_malloc(dl + 1);
                memcpy(s_oop[idx].op_descs[i], desc, dl);
                s_oop[idx].op_descs[i][dl] = '\0';
            }
            s_oop[idx].op_fns[i] = fn;
            return 0;
        }
    }
    if (oop_ensure_cap(idx) < 0)
        return -1;
    int nl = (int)strlen(op_name);
    s_oop[idx].op_names[s_oop[idx].op_count] = (char *)tinybuf_malloc(nl + 1);
    memcpy(s_oop[idx].op_names[s_oop[idx].op_count], op_name, nl);
    s_oop[idx].op_names[s_oop[idx].op_count][nl] = '\0';
    s_oop[idx].op_sigs[s_oop[idx].op_count] = NULL;
    if (sig)
    {
        int sl = (int)strlen(sig);
        s_oop[idx].op_sigs[s_oop[idx].op_count] = (char *)tinybuf_malloc(sl + 1);
        memcpy(s_oop[idx].op_sigs[s_oop[idx].op_count], sig, sl);
        s_oop[idx].op_sigs[s_oop[idx].op_count][sl] = '\0';
    }
    s_oop[idx].op_descs[s_oop[idx].op_count] = NULL;
    if (desc)
    {
        int dl = (int)strlen(desc);
        s_oop[idx].op_descs[s_oop[idx].op_count] = (char *)tinybuf_malloc(dl + 1);
        memcpy(s_oop[idx].op_descs[s_oop[idx].op_count], desc, dl);
        s_oop[idx].op_descs[s_oop[idx].op_count][dl] = '\0';
    }
    s_oop[idx].op_fns[s_oop[idx].op_count] = fn;
    s_oop[idx].op_count++;
    return 0;
}
int tinybuf_oop_get_op_count(const char *type_name)
{
    int idx = oop_index_by_name(type_name);
    if (idx < 0)
        return -1;
    return s_oop[idx].op_count;
}
int tinybuf_oop_get_op_meta(const char *type_name, int index, const char **name, const char **sig, const char **desc)
{
    int idx = oop_index_by_name(type_name);
    if (idx < 0)
        return -1;
    if (index < 0 || index >= s_oop[idx].op_count)
        return -1;
    if (name)
        *name = s_oop[idx].op_names[index];
    if (sig)
        *sig = s_oop[idx].op_sigs[index];
    if (desc)
        *desc = s_oop[idx].op_descs[index];
    return 0;
}
static int oop_find_op_index(int idx, const char *op_name)
{
    if (idx < 0 || idx >= s_oop_count || !op_name)
        return -1;
    for (int i = 0; i < s_oop[idx].op_count; ++i)
    {
        if (s_oop[idx].op_names[i] && strcmp(s_oop[idx].op_names[i], op_name) == 0)
            return i;
    }
    return -1;
}
int tinybuf_oop_do_op(const char *type_name, const char *op_name, tinybuf_value *self, const tinybuf_value *args, tinybuf_value *out)
{
    int idx = oop_index_by_name(type_name);
    if (idx < 0)
        return -1;
    int oi = oop_find_op_index(idx, op_name);
    if (oi < 0)
        return -1;
    if (!s_oop[idx].op_fns[oi])
        return -1;
    return s_oop[idx].op_fns[oi](self, args, out);
}
int tinybuf_oop_attach_serializers(const char *type_name, tinybuf_custom_read_fn read, tinybuf_custom_write_fn write, tinybuf_custom_dump_fn dump)
{
    if (!type_name || !read)
        return -1;
    int idx = oop_index_by_name(type_name);
    if (idx < 0)
    {
        if (tinybuf_oop_register_type(type_name) < 0)
            return -1;
        idx = oop_index_by_name(type_name);
    }
    s_oop[idx].read = read;
    s_oop[idx].write = write;
    s_oop[idx].dump = dump;
    return 0;
}
int tinybuf_oop_set_serializable(const char *type_name, int serializable)
{
    int idx = oop_index_by_name(type_name);
    if (idx < 0)
    {
        if (tinybuf_oop_register_type(type_name) < 0)
            return -1;
        idx = oop_index_by_name(type_name);
    }
    s_oop[idx].is_serializable = serializable ? 1 : 0;
    return 0;
}
int tinybuf_oop_get_serializers(const char *type_name, tinybuf_custom_read_fn *read, tinybuf_custom_write_fn *write, tinybuf_custom_dump_fn *dump, int *is_serializable)
{
    int idx = oop_index_by_name(type_name);
    if (idx < 0)
        return -1;
    if (read)
        *read = s_oop[idx].read;
    if (write)
        *write = s_oop[idx].write;
    if (dump)
        *dump = s_oop[idx].dump;
    if (is_serializable)
        *is_serializable = s_oop[idx].is_serializable;
    return 0;
}

/* traits */
typedef struct
{
    char *name;
    char **type_names;
    int type_count;
    int type_cap;
    char **op_names;
    char **op_sigs;
    char **op_descs;
    tinybuf_plugin_value_op_fn *op_fns;
    int op_count;
    int op_cap;
} trait_entry;
static trait_entry *s_traits = NULL;
static int s_trait_count = 0;
static int s_trait_cap = 0;
static int trait_index_by_name(const char *name)
{
    if (!name)
        return -1;
    for (int i = 0; i < s_trait_count; ++i)
    {
        if (s_traits[i].name && strcmp(s_traits[i].name, name) == 0)
            return i;
    }
    return -1;
}
static int trait_ensure_cap(int idx)
{
    if (idx < 0 || idx >= s_trait_count)
        return -1;
    if (s_traits[idx].op_count == s_traits[idx].op_cap)
    {
        int nc = s_traits[idx].op_cap ? s_traits[idx].op_cap * 2 : 4;
        s_traits[idx].op_names = (char **)tinybuf_realloc(s_traits[idx].op_names, sizeof(char *) * nc);
        s_traits[idx].op_sigs = (char **)tinybuf_realloc(s_traits[idx].op_sigs, sizeof(char *) * nc);
        s_traits[idx].op_descs = (char **)tinybuf_realloc(s_traits[idx].op_descs, sizeof(char *) * nc);
        s_traits[idx].op_fns = (tinybuf_plugin_value_op_fn *)tinybuf_realloc(s_traits[idx].op_fns, sizeof(tinybuf_plugin_value_op_fn) * nc);
        s_traits[idx].op_cap = nc;
    }
    return 0;
}
static int trait_find_op_index(int idx, const char *op_name)
{
    if (idx < 0 || idx >= s_trait_count || !op_name)
        return -1;
    for (int i = 0; i < s_traits[idx].op_count; ++i)
    {
        if (s_traits[idx].op_names[i] && strcmp(s_traits[idx].op_names[i], op_name) == 0)
            return i;
    }
    return -1;
}
int tinybuf_oop_register_trait(const char *trait_name)
{
    if (!trait_name)
        return -1;
    int idx = trait_index_by_name(trait_name);
    if (idx >= 0)
        return 0;
    if (s_trait_count == s_trait_cap)
    {
        int nc = s_trait_cap ? s_trait_cap * 2 : 8;
        s_traits = (trait_entry *)tinybuf_realloc(s_traits, sizeof(trait_entry) * nc);
        s_trait_cap = nc;
    }
    int tl = (int)strlen(trait_name);
    trait_entry e;
    e.name = (char *)tinybuf_malloc(tl + 1);
    memcpy(e.name, trait_name, tl);
    e.name[tl] = '\0';
    e.type_names = NULL;
    e.type_count = 0;
    e.type_cap = 0;
    e.op_names = NULL;
    e.op_sigs = NULL;
    e.op_descs = NULL;
    e.op_fns = NULL;
    e.op_count = 0;
    e.op_cap = 0;
    s_traits[s_trait_count++] = e;
    return 0;
}
int tinybuf_oop_trait_add_type(const char *trait_name, const char *type_name)
{
    if (!trait_name || !type_name)
        return -1;
    int idx = trait_index_by_name(trait_name);
    if (idx < 0)
    {
        if (tinybuf_oop_register_trait(trait_name) < 0)
            return -1;
        idx = trait_index_by_name(trait_name);
    }
    for (int i = 0; i < s_traits[idx].type_count; ++i)
    {
        if (s_traits[idx].type_names[i] && strcmp(s_traits[idx].type_names[i], type_name) == 0)
            return 0;
    }
    if (s_traits[idx].type_count == s_traits[idx].type_cap)
    {
        int nc = s_traits[idx].type_cap ? s_traits[idx].type_cap * 2 : 4;
        s_traits[idx].type_names = (char **)tinybuf_realloc(s_traits[idx].type_names, sizeof(char *) * nc);
        s_traits[idx].type_cap = nc;
    }
    int nl = (int)strlen(type_name);
    s_traits[idx].type_names[s_traits[idx].type_count] = (char *)tinybuf_malloc(nl + 1);
    memcpy(s_traits[idx].type_names[s_traits[idx].type_count], type_name, nl);
    s_traits[idx].type_names[s_traits[idx].type_count][nl] = '\0';
    s_traits[idx].type_count++;
    return 0;
}
int tinybuf_oop_trait_register_op(const char *trait_name, const char *op_name, const char *sig, const char *desc, tinybuf_plugin_value_op_fn fn)
{
    if (!trait_name || !op_name || !fn)
        return -1;
    int idx = trait_index_by_name(trait_name);
    if (idx < 0)
    {
        if (tinybuf_oop_register_trait(trait_name) < 0)
            return -1;
        idx = trait_index_by_name(trait_name);
    }
    for (int i = 0; i < s_traits[idx].op_count; ++i)
    {
        if (s_traits[idx].op_names[i] && strcmp(s_traits[idx].op_names[i], op_name) == 0)
        {
            s_traits[idx].op_sigs[i] = NULL;
            if (sig)
            {
                int sl = (int)strlen(sig);
                s_traits[idx].op_sigs[i] = (char *)tinybuf_malloc(sl + 1);
                memcpy(s_traits[idx].op_sigs[i], sig, sl);
                s_traits[idx].op_sigs[i][sl] = '\0';
            }
            s_traits[idx].op_descs[i] = NULL;
            if (desc)
            {
                int dl = (int)strlen(desc);
                s_traits[idx].op_descs[i] = (char *)tinybuf_malloc(dl + 1);
                memcpy(s_traits[idx].op_descs[i], desc, dl);
                s_traits[idx].op_descs[i][dl] = '\0';
            }
            s_traits[idx].op_fns[i] = fn;
            return 0;
        }
    }
    if (trait_ensure_cap(idx) < 0)
        return -1;
    int nl = (int)strlen(op_name);
    s_traits[idx].op_names[s_traits[idx].op_count] = (char *)tinybuf_malloc(nl + 1);
    memcpy(s_traits[idx].op_names[s_traits[idx].op_count], op_name, nl);
    s_traits[idx].op_names[s_traits[idx].op_count][nl] = '\0';
    s_traits[idx].op_sigs[s_traits[idx].op_count] = NULL;
    if (sig)
    {
        int sl = (int)strlen(sig);
        s_traits[idx].op_sigs[s_traits[idx].op_count] = (char *)tinybuf_malloc(sl + 1);
        memcpy(s_traits[idx].op_sigs[s_traits[idx].op_count], sig, sl);
        s_traits[idx].op_sigs[s_traits[idx].op_count][sl] = '\0';
    }
    s_traits[idx].op_descs[s_traits[idx].op_count] = NULL;
    if (desc)
    {
        int dl = (int)strlen(desc);
        s_traits[idx].op_descs[s_traits[idx].op_count] = (char *)tinybuf_malloc(dl + 1);
        memcpy(s_traits[idx].op_descs[s_traits[idx].op_count], desc, dl);
        s_traits[idx].op_descs[s_traits[idx].op_count][dl] = '\0';
    }
    s_traits[idx].op_fns[s_traits[idx].op_count] = fn;
    s_traits[idx].op_count++;
    return 0;
}
int tinybuf_oop_trait_get_op_count(const char *trait_name)
{
    int idx = trait_index_by_name(trait_name);
    if (idx < 0)
        return -1;
    return s_traits[idx].op_count;
}
int tinybuf_oop_trait_get_op_meta(const char *trait_name, int index, const char **name, const char **sig, const char **desc)
{
    int idx = trait_index_by_name(trait_name);
    if (idx < 0)
        return -1;
    if (index < 0 || index >= s_traits[idx].op_count)
        return -1;
    if (name)
        *name = s_traits[idx].op_names[index];
    if (sig)
        *sig = s_traits[idx].op_sigs[index];
    if (desc)
        *desc = s_traits[idx].op_descs[index];
    return 0;
}
int tinybuf_oop_type_implements_trait(const char *type_name, const char *trait_name)
{
    int idx = trait_index_by_name(trait_name);
    if (idx < 0)
        return 0;
    for (int i = 0; i < s_traits[idx].type_count; ++i)
    {
        if (s_traits[idx].type_names[i] && strcmp(s_traits[idx].type_names[i], type_name) == 0)
            return 1;
    }
    return 0;
}
int tinybuf_oop_do_trait_op(const char *trait_name, const char *op_name, const char *type_name, tinybuf_value *self, const tinybuf_value *args, tinybuf_value *out)
{
    int idx = trait_index_by_name(trait_name);
    if (idx < 0)
        return -1;
    if (type_name && !tinybuf_oop_type_implements_trait(type_name, trait_name))
        return -1;
    int oi = trait_find_op_index(idx, op_name);
    if (oi < 0)
        return -1;
    if (!s_traits[idx].op_fns[oi])
        return -1;
    return s_traits[idx].op_fns[oi](self, args, out);
}
