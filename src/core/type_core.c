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

static int deep_copy_fields(const type_def_obj *def, const void *from, void *to)
{
    if (!def || !def->fields || def->field_count <= 0) return 0;
    for (int i = 0; i < def->field_count; ++i)
    {
        const field_desc *fd = &def->fields[i];
        if (!fd->owns) continue;
        switch (fd->kind)
        {
        case field_inline_binary:
            if (fd->child_type)
            {
                const void *src = cptr_add(from, fd->offset);
                void *dst = ptr_add(to, fd->offset);
                object_copy(fd->child_type, src, dst);
            }
            break;
        case field_typed_obj_inline:
        {
            const typed_obj *src = (const typed_obj *)cptr_add(from, fd->offset);
            typed_obj *dst = (typed_obj *)ptr_add(to, fd->offset);
            if (src && src->type && src->ptr)
            {
                typed_obj_copy(dst, src);
            }
        }
        break;
        case field_typed_obj_ptr:
        {
            typed_obj *const *psrc = (typed_obj *const *)cptr_add(from, fd->offset);
            typed_obj **pdst = (typed_obj **)ptr_add(to, fd->offset);
            if (psrc && *psrc && (*psrc)->type && (*psrc)->ptr)
            {
                *pdst = (typed_obj *)malloc(sizeof(typed_obj));
                typed_obj_copy(*pdst, *psrc);
            }
        }
        break;
        default:
            break;
        }
    }
    return 0;
}

static int deep_delete_fields(const type_def_obj *def, void *self)
{
    if (!def || !def->fields || def->field_count <= 0) return 0;
    for (int i = 0; i < def->field_count; ++i)
    {
        const field_desc *fd = &def->fields[i];
        if (!fd->owns) continue;
        switch (fd->kind)
        {
        case field_inline_binary:
            if (fd->child_type)
            {
                void *sub = ptr_add(self, fd->offset);
                object_delete(fd->child_type, sub);
            }
            break;
        case field_typed_obj_inline:
        {
            typed_obj *sub = (typed_obj *)ptr_add(self, fd->offset);
            typed_obj_delete(sub);
        }
        break;
        case field_typed_obj_ptr:
        {
            typed_obj **psub = (typed_obj **)ptr_add(self, fd->offset);
            if (psub && *psub)
            {
                typed_obj_delete(*psub);
                free(*psub);
                *psub = NULL;
            }
        }
        break;
        default:
            break;
        }
    }
    return 0;
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
    deep_copy_fields(def, from, to);
    return 0;
}

int object_delete(const type_def_obj *def, void *obj)
{
    if (!def || !obj) return -1;
    deep_delete_fields(def, obj);
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
