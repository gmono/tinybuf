#include "tinybuf_common.h"
#include "tinybuf_support.h"
#include "tinybuf_memory.h"
#include <string.h>


static holestrlist *strlist_new(void)
{
    holestrlist *l = (holestrlist *)tinybuf_malloc(sizeof(holestrlist));
    l->items = NULL;
    l->count = 0;
    l->capacity = 0;
    return l;
}
static void strlist_ensure(holestrlist *l)
{
    if (!l)
        return;
    if (l->count == l->capacity)
    {
        int nc = l->capacity ? l->capacity * 2 : 4;
        l->items = (hole_string **)tinybuf_realloc(l->items, sizeof(hole_string *) * nc);
        l->capacity = nc;
    }
}
static void strlist_add_owned(holestrlist *l, const char *msg, tinybuf_deleter_fn d)
{
    if (!l || !msg)
        return;
    strlist_ensure(l);
    hole_string *hs = hole_string_new();
    hole_string_append_cstr(hs, msg, 0, (void (*)(void *))d);
    l->items[l->count++] = hs;
}
static void strlist_add_hole(holestrlist *l, hole_string *hs)
{
    if (!l || !hs)
        return;
    strlist_ensure(l);
    l->items[l->count++] = hs;
}
static void strlist_free(holestrlist *l)
{
    if (!l)
        return;
    for (int i = 0; i < l->count; ++i)
    {
        if (l->items[i])
            hole_string_free(l->items[i]);
    }
    tinybuf_free(l->items);
    tinybuf_free(l);
}

static inline int *_new_refcnt(void)
{
    int *p = (int *)tinybuf_malloc(sizeof(int));
    *p = 1;
    return p;
}

tinybuf_error tinybuf_result_ok(int res)
{
    tinybuf_error r;
    r.res = res;
    r.msgs = NULL;
    r.refcnt = _new_refcnt();
    return r;
}
tinybuf_error tinybuf_result_err(int res, const char *msg, tinybuf_deleter_fn deleter)
{
    tinybuf_error r;
    r.res = res;
    r.msgs = strlist_new();
    if (msg)
        strlist_add_owned(r.msgs, msg, deleter);
    r.refcnt = _new_refcnt();
    return r;
}
tinybuf_error tinybuf_result_create(int res, const char *msg, tinybuf_deleter_fn deleter)
{
    if (res > 0)
        return tinybuf_result_ok(res);
    return tinybuf_result_err(res, msg, deleter);
}
tinybuf_error tinybuf_result_create_ok(int res)
{
    return tinybuf_result_ok(res);
}
tinybuf_error tinybuf_result_create_err(int res, const char *msg, tinybuf_deleter_fn deleter)
{
    return tinybuf_result_err(res, msg, deleter);
}
int tinybuf_result_add_msg(tinybuf_error *r, const char *msg, tinybuf_deleter_fn deleter)
{
    if (!r || !msg)
        return -1;
    if (!r->msgs)
        r->msgs = strlist_new();
    strlist_add_owned(r->msgs, msg, deleter);
    return r->msgs->count;
}
int tinybuf_result_add_hole_msg(tinybuf_error *r, hole_string *msg)
{
    if (!r || !msg)
        return -1;
    if (!r->msgs)
        r->msgs = strlist_new();
    strlist_add_hole(r->msgs, msg);
    return r->msgs->count;
}
int tinybuf_result_add_msg_const(tinybuf_error *r, const char *msg)
{
    return tinybuf_result_add_msg(r, msg, NULL);
}
int tinybuf_result_msg_count(const tinybuf_error *r)
{
    return (r && r->msgs) ? r->msgs->count : 0;
}
tinybuf_str tinybuf_result_msg_at(const tinybuf_error *r, int idx)
{
    tinybuf_str ret; ret.ptr = NULL; ret.deleter = NULL;
    if (!r || !r->msgs || idx < 0 || idx >= r->msgs->count)
        return ret;
    hole_string *hs = r->msgs->items[idx];
    ret = hole_string_get(hs);
    return ret;
}

int tinybuf_result_format_msgs(const tinybuf_error *r, char *dst, int dst_len)
{
    if (!dst || dst_len <= 0)
        return -1;
    int off = 0;
    int n = tinybuf_result_msg_count(r);
    for (int i = 0; i < n; ++i)
    {
        tinybuf_str s = tinybuf_result_msg_at(r, i);
        if (!s.ptr)
            continue;
        int sl = (int)strlen(s.ptr);
        if (off + sl + (i + 1 < n ? 2 : 0) >= dst_len)
        {
            if (s.deleter)
                s.deleter(s.ptr);
            break;
        }
        memcpy(dst + off, s.ptr, (size_t)sl);
        off += sl;
        if (i + 1 < n)
        {
            dst[off++] = ';';
            dst[off++] = ' ';
        }
        if (s.deleter)
            s.deleter(s.ptr);
    }
    if (off < dst_len)
        dst[off] = '\0';
    return off;
}

int tinybuf_result_ref(tinybuf_error *r)
{
    if (!r || !r->refcnt)
        return -1;
    (*r->refcnt)++;
    return *r->refcnt;
}

int tinybuf_result_unref(tinybuf_error *r)
{
    if (!r || !r->refcnt)
        return -1;
    (*r->refcnt)--;
    int v = *r->refcnt;
    if (v <= 0)
    {
        if (r->msgs)
        {
            strlist_free(r->msgs);
            r->msgs = NULL;
        }
        tinybuf_free(r->refcnt);
        r->refcnt = NULL;
    }
    return v;
}

static inline char *_dup_str(const char *s)
{
    if (!s)
        return NULL;
    int n = (int)strlen(s);
    char *p = (char *)tinybuf_malloc(n + 1);
    memcpy(p, s, (size_t)n);
    p[n] = '\0';
    return p;
}

int tinybuf_result_append_merge(tinybuf_error *dst, tinybuf_error *src, int (*mergeres)(int, int))
{
    if (!dst || !src)
        return -1;
    if (src->msgs)
    {
        if (!dst->msgs)
        {
            dst->msgs = src->msgs;
            src->msgs = NULL;
        }
        else
        {
            int sc = src->msgs->count;
            for (int i = 0; i < sc; ++i)
            {
                tinybuf_result_add_hole_msg(dst, src->msgs->items[i]);
            }
            src->msgs->count = 0;
        }
    }
    if (dst->res <= 0 || src->res <= 0)
    {
        dst->res = -1;
    }
    else
    {
        int merged = mergeres ? mergeres(dst->res, src->res) : (dst->res + src->res);
        dst->res = merged;
    }
    src->res = 0;
    return dst->res;
}

int tinybuf_merger_sum(int a, int b) { return a + b; }
int tinybuf_merger_max(int a, int b) { return a > b ? a : b; }
int tinybuf_merger_left(int a, int b)
{
    (void)b;
    return a;
}
int tinybuf_merger_right(int a, int b)
{
    (void)a;
    return b;
}
