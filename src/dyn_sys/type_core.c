#include "type_core.h"
#include <string.h>
#include <stdlib.h>

static inline void *ptr_add(void *p, size_t off) { return (void *)((uint8_t *)p + off); }
static inline const void *cptr_add(const void *p, size_t off) { return (const void *)((const uint8_t *)p + off); }

void default_mv(void *from, void *to, size_t s)
{
    if (!from || !to || s == 0) return;
    memcpy(to, from, s);
    memset(from, 0, s);
}

void default_copy(const void *from, void *to, size_t s)
{
    if (!from || !to || s == 0) return;
    memcpy(to, from, s);
}

void default_delete(void *obj)
{
    if (!obj) return;
    free(obj);
}

size_t type_total_size(const type_def_obj *def, const void *self)
{
    if (!def) return 0;
    if (def->get_total_size) return def->get_total_size(self);
    return def->size;
}

static inline void call_init_if_any(const type_def_obj *def, void *self)
{
    if (def && def->init && self) def->init(self);
}

int object_mv(const type_def_obj *def, void *from, void *to)
{
    if (!def || !to || !from) return -1;
    if (def->move)
    {
        def->move(from, to);
        return 0;
    }
    size_t sz = type_total_size(def, from);
    default_mv(from, to, sz);
    return 0;
}

int object_copy(const type_def_obj *def, const void *from, void *to)
{
    if (!def || !to || !from) return -1;
    if (def->copy)
    {
        def->copy(from, to);
        return 0;
    }
    size_t sz = type_total_size(def, from);
    default_copy(from, to, sz);
    return 0;
}

int object_delete(const type_def_obj *def, void *obj)
{
    if (!def || !obj) return -1;
    if (def->deleter)
    {
        def->deleter(obj);
        return 0;
    }
    default_delete(obj);
    return 0;
}

int typed_obj_init(typed_obj *o, const type_def_obj *def, void *ptr)
{
    if (!o || !def) return -1;
    o->type = def;
    o->ptr = ptr;
    call_init_if_any(def, o->ptr);
    return 0;
}

int typed_obj_alloc(typed_obj *o, const type_def_obj *def)
{
    if (!o || !def) return -1;
    void *mem = NULL;
    if (def->alloc) mem = def->alloc();
    else
    {
        size_t sz = def->size ? def->size : 1;
        mem = malloc(sz);
        if (mem) memset(mem, 0, sz);
    }
    if (!mem) return -1;
    o->type = def;
    o->ptr = mem;
    call_init_if_any(def, o->ptr);
    return 0;
}

int typed_obj_copy(typed_obj *dst, const typed_obj *src)
{
    if (!dst || !src || !src->type || !src->ptr) return -1;
    if (!dst->ptr)
    {
        size_t sz = type_total_size(src->type, src->ptr);
        dst->ptr = malloc(sz);
        if (!dst->ptr) return -1;
        memset(dst->ptr, 0, sz);
    }
    dst->type = src->type;
    return object_copy(src->type, src->ptr, dst->ptr);
}

int typed_obj_move(typed_obj *dst, typed_obj *src)
{
    if (!dst || !src || !src->type || !src->ptr) return -1;
    if (!dst->ptr)
    {
        size_t sz = type_total_size(src->type, src->ptr);
        dst->ptr = malloc(sz);
        if (!dst->ptr) return -1;
        memset(dst->ptr, 0, sz);
    }
    dst->type = src->type;
    int rc = object_mv(src->type, src->ptr, dst->ptr);
    if (rc == 0)
    {
        free(src->ptr);
        src->ptr = NULL;
        src->type = NULL;
    }
    return rc;
}

int typed_obj_delete(typed_obj *o)
{
    if (!o || !o->type || !o->ptr) return -1;
    int rc = object_delete(o->type, o->ptr);
    o->ptr = NULL;
    o->type = NULL;
    return rc;
}

static void init_noop(void *self) { (void)self; }

const type_def_obj i8_def = {
    "i8", type_simple, sizeof(int8_t), init_noop,
    NULL, NULL, NULL, NULL, NULL,
    NULL, 0, NULL, 0
};
const type_def_obj u8_def = {
    "u8", type_simple, sizeof(uint8_t), init_noop,
    NULL, NULL, NULL, NULL, NULL,
    NULL, 0, NULL, 0
};
const type_def_obj i16_def = {
    "i16", type_simple, sizeof(int16_t), init_noop,
    NULL, NULL, NULL, NULL, NULL,
    NULL, 0, NULL, 0
};
const type_def_obj u16_def = {
    "u16", type_simple, sizeof(uint16_t), init_noop,
    NULL, NULL, NULL, NULL, NULL,
    NULL, 0, NULL, 0
};
const type_def_obj i32_def = {
    "i32", type_simple, sizeof(int32_t), init_noop,
    NULL, NULL, NULL, NULL, NULL,
    NULL, 0, NULL, 0
};
const type_def_obj u32_def = {
    "u32", type_simple, sizeof(uint32_t), init_noop,
    NULL, NULL, NULL, NULL, NULL,
    NULL, 0, NULL, 0
};
const type_def_obj i64_def = {
    "i64", type_simple, sizeof(int64_t), init_noop,
    NULL, NULL, NULL, NULL, NULL,
    NULL, 0, NULL, 0
};
const type_def_obj u64_def = {
    "u64", type_simple, sizeof(uint64_t), init_noop,
    NULL, NULL, NULL, NULL, NULL,
    NULL, 0, NULL, 0
};
const type_def_obj f32_def = {
    "f32", type_simple, sizeof(float), init_noop,
    NULL, NULL, NULL, NULL, NULL,
    NULL, 0, NULL, 0
};
const type_def_obj f64_def = {
    "f64", type_simple, sizeof(double), init_noop,
    NULL, NULL, NULL, NULL, NULL,
    NULL, 0, NULL, 0
};
const type_def_obj bool_def = {
    "bool", type_simple, sizeof(uint8_t), init_noop,
    NULL, NULL, NULL, NULL, NULL,
    NULL, 0, NULL, 0
};
const type_def_obj char_def = {
    "char", type_simple, sizeof(char), init_noop,
    NULL, NULL, NULL, NULL, NULL,
    NULL, 0, NULL, 0
};
const type_def_obj ptr_def = {
    "ptr", type_simple, sizeof(void *), init_noop,
    NULL, NULL, NULL, NULL, NULL,
    NULL, 0, NULL, 0
};
