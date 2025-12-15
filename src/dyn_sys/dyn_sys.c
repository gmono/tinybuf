#include "dyn_sys.h"
#include <string.h>
#include <stdlib.h>
#define TB_MALLOC malloc
#define TB_REALLOC realloc

typedef struct
{
    char *name;
    char **op_names;
    char **op_sigs;
    char **op_descs;
    dyn_method_call_fn *typed_op_fns;
    int op_count;
    int op_cap;
    dyn_method_sig *typed_sigs;
} oop_entry;
static oop_entry *s_oop = NULL;
static int s_oop_count = 0;
static int s_oop_cap = 0;

static char *build_sig_string(const dyn_method_sig *sig)
{
    if (!sig) return NULL;
    int cap = 128;
    char *buf = (char *)TB_MALLOC(cap);
    int len = 0;
    buf[len++] = '(';
    for (int i = 0; i < sig->param_count; ++i)
    {
        const char *tn = sig->params[i].type_name ? sig->params[i].type_name : "value";
        int tl = (int)strlen(tn);
        if (len + tl + 4 >= cap)
        {
            cap = cap * 2 + tl + 8;
            buf = (char *)TB_REALLOC(buf, cap);
        }
        memcpy(buf + len, tn, tl);
        len += tl;
        if (i != sig->param_count - 1)
        {
            buf[len++] = ',';
        }
    }
    buf[len++] = ')';
    buf[len++] = '-';
    buf[len++] = '>';
    const char *rn = sig->ret_type_name ? sig->ret_type_name : "value";
    int rl = (int)strlen(rn);
    if (len + rl + 1 >= cap)
    {
        cap = cap * 2 + rl + 8;
        buf = (char *)TB_REALLOC(buf, cap);
    }
    memcpy(buf + len, rn, rl);
    len += rl;
    buf[len] = '\0';
    return buf;
}

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
static int oop_ensure_cap(int idx)
{
    if (idx < 0 || idx >= s_oop_count)
        return -1;
    if (s_oop[idx].op_count == s_oop[idx].op_cap)
    {
        int nc = s_oop[idx].op_cap ? s_oop[idx].op_cap * 2 : 4;
        s_oop[idx].op_names = (char **)TB_REALLOC(s_oop[idx].op_names, sizeof(char *) * nc);
        s_oop[idx].op_sigs = (char **)TB_REALLOC(s_oop[idx].op_sigs, sizeof(char *) * nc);
        s_oop[idx].op_descs = (char **)TB_REALLOC(s_oop[idx].op_descs, sizeof(char *) * nc);
        s_oop[idx].typed_op_fns = (dyn_method_call_fn *)TB_REALLOC(s_oop[idx].typed_op_fns, sizeof(dyn_method_call_fn) * nc);
        s_oop[idx].typed_sigs = (dyn_method_sig *)TB_REALLOC(s_oop[idx].typed_sigs, sizeof(dyn_method_sig) * nc);
        s_oop[idx].op_cap = nc;
    }
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


static int validate_typed_args_obj(const dyn_method_sig *sig, const typed_obj *args, int argc)
{
    if (!sig) return 0;
    if (sig->param_count < 0) return 0;
    if (sig->param_count == 0) return 1;
    if (!args || argc < sig->param_count) return 0;
    for (int i = 0; i < sig->param_count; ++i)
    {
        const char *tn = sig->params[i].type_name ? sig->params[i].type_name : "value";
        if (!args[i].type && strcmp(tn, "value") != 0 && strcmp(tn, "any") != 0) return 0;
        if (args[i].type && tn && strcmp(tn, "any") != 0 && strcmp(tn, "value") != 0)
        {
            if (!args[i].type->name || strcmp(args[i].type->name, tn) != 0) return 0;
        }
    }
    return 1;
}
int dyn_oop_register_type(const char *type_name)
{
    if (!type_name)
        return -1;
    int idx = oop_index_by_name(type_name);
    if (idx >= 0)
        return 0;
    if (s_oop_count == s_oop_cap)
    {
        int nc = s_oop_cap ? s_oop_cap * 2 : 8;
        s_oop = (oop_entry *)TB_REALLOC(s_oop, sizeof(oop_entry) * nc);
        s_oop_cap = nc;
    }
    int tl = (int)strlen(type_name);
    oop_entry e;
    e.name = (char *)TB_MALLOC(tl + 1);
    memcpy(e.name, type_name, tl);
    e.name[tl] = '\0';
    e.op_names = NULL;
    e.op_sigs = NULL;
    e.op_descs = NULL;
    e.typed_op_fns = NULL;
    e.op_count = 0;
    e.op_cap = 0;
    e.typed_sigs = NULL;
    s_oop[s_oop_count++] = e;
    return 0;
}
int dyn_oop_get_type_count(void) { return s_oop_count; }
const char *dyn_oop_get_type_name(int index)
{
    if (index < 0 || index >= s_oop_count)
        return NULL;
    return s_oop[index].name;
}
int dyn_oop_get_op_count(const char *type_name)
{
    int idx = oop_index_by_name(type_name);
    if (idx < 0)
        return -1;
    return s_oop[idx].op_count;
}
const char *dyn_oop_get_op_name(const char *type_name, int index)
{
    int idx = oop_index_by_name(type_name);
    if (idx < 0) return NULL;
    if (index < 0 || index >= s_oop[idx].op_count) return NULL;
    return s_oop[idx].op_names[index];
}
int dyn_oop_get_op_meta(const char *type_name, int index, const char **name, const char **sig, const char **desc)
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
int dyn_oop_register_op_typed(const char *type_name, const char *op_name, const dyn_method_sig *sig, const char *op_desc, dyn_method_call_fn fn)
{
    if (!type_name || !op_name || !sig || !fn) return -1;
    int idx = oop_index_by_name(type_name);
    if (idx < 0)
    {
        if (dyn_oop_register_type(type_name) < 0) return -1;
        idx = oop_index_by_name(type_name);
    }
    int oi = oop_find_op_index(idx, op_name);
    if (oi < 0)
    {
        if (oop_ensure_cap(idx) < 0) return -1;
        int nl = (int)strlen(op_name);
        s_oop[idx].op_names[s_oop[idx].op_count] = (char *)TB_MALLOC(nl + 1);
        memcpy(s_oop[idx].op_names[s_oop[idx].op_count], op_name, nl);
        s_oop[idx].op_names[s_oop[idx].op_count][nl] = '\0';
        s_oop[idx].op_sigs[s_oop[idx].op_count] = build_sig_string(sig);
        s_oop[idx].op_descs[s_oop[idx].op_count] = NULL;
        if (op_desc)
        {
            int dl = (int)strlen(op_desc);
            s_oop[idx].op_descs[s_oop[idx].op_count] = (char *)TB_MALLOC(dl + 1);
            memcpy(s_oop[idx].op_descs[s_oop[idx].op_count], op_desc, dl);
            s_oop[idx].op_descs[s_oop[idx].op_count][dl] = '\0';
        }
        s_oop[idx].typed_op_fns[s_oop[idx].op_count] = fn;
        oi = s_oop[idx].op_count;
        s_oop[idx].op_count++;
    }
    if (!s_oop[idx].typed_sigs) s_oop[idx].typed_sigs = (dyn_method_sig *)TB_MALLOC(sizeof(dyn_method_sig) * s_oop[idx].op_cap);
    dyn_method_sig *dst = &s_oop[idx].typed_sigs[oi];
    *dst = *sig;
    if (sig->param_count > 0 && sig->params)
    {
        dyn_param_desc *ps = (dyn_param_desc *)TB_MALLOC(sizeof(dyn_param_desc) * sig->param_count);
        for (int i = 0; i < sig->param_count; ++i) ps[i] = sig->params[i];
        dst->params = ps;
    }
    return 0;
}
int dyn_oop_get_op_typed(const char *type_name, const char *op_name, const dyn_method_sig **sig)
{
    if (!type_name || !op_name || !sig) return -1;
    int idx = oop_index_by_name(type_name);
    if (idx < 0) return -1;
    int oi = oop_find_op_index(idx, op_name);
    if (oi < 0) return -1;
    if (!s_oop[idx].typed_sigs) return -1;
    *sig = &s_oop[idx].typed_sigs[oi];
    return 0;
}
int dyn_oop_do_op(const char *type_name, const char *op_name, typed_obj *self, const typed_obj *args, int argc, typed_obj *out)
{
    int idx = oop_index_by_name(type_name);
    if (idx < 0)
        return -1;
    int oi = oop_find_op_index(idx, op_name);
    if (oi < 0)
        return -1;
    if (s_oop[idx].typed_sigs)
    {
        const dyn_method_sig *sig = &s_oop[idx].typed_sigs[oi];
        if (sig && sig->params && sig->param_count > 0)
        {
            if (!validate_typed_args_obj(sig, args, argc)) return -1;
        }
    }
    if (!s_oop[idx].typed_op_fns[oi])
        return -1;
    return s_oop[idx].typed_op_fns[oi](self, args, argc, out);
}
