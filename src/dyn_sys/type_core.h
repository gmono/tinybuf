#ifndef TYPE_CORE_H
#define TYPE_CORE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
    typedef void init_fn(void *self);
    typedef void deleter_fn(void *self);
    typedef void copy_fn(const void *from, void *to);
    typedef void move_fn(void *from, void *to);
    typedef void *alloc_fn(void);
    typedef size_t get_total_size_fn(const void *self);

    struct type_def_obj;
    struct meta_table;

    typedef struct field_def
    {
        const char *name;
        const struct type_def_obj *type;
        const struct meta_table *meta;
    } field_def;

    typedef struct param_def
    {
        const char *name;
        const struct type_def_obj *type;
        const struct meta_table *meta;
    } param_def;

    typedef struct method_def
    {
        const char *name;
        const struct type_def_obj *ret;
        const param_def *params;
        int param_count;
        const struct meta_table *meta;
        void *impl;
    } method_def;

    typedef enum type_def_kind
    {
        type_simple = 0,
        type_complex = 1
    } type_def_kind;

    typedef struct type_def_obj
    {
        const char *name;
        type_def_kind kind;
        size_t size;
        init_fn *init;
        deleter_fn *deleter;
        copy_fn *copy;
        move_fn *move;
        alloc_fn *alloc;
        get_total_size_fn *get_total_size;
        const field_def *fields;
        int field_count;
        const method_def *methods;
        int method_count;
    } type_def_obj;

    typedef struct typed_obj
    {
        void *ptr;
        const type_def_obj *type;
    } typed_obj;

    void default_mv(void *from, void *to, size_t s);
    void default_copy(const void *from, void *to, size_t s);
    void default_delete(void *obj);

    size_t type_total_size(const type_def_obj *def, const void *self);
    int object_mv(const type_def_obj *def, void *from, void *to);
    int object_copy(const type_def_obj *def, const void *from, void *to);
    int object_delete(const type_def_obj *def, void *obj);

    int typed_obj_init(typed_obj *o, const type_def_obj *def, void *ptr);
    int typed_obj_alloc(typed_obj *o, const type_def_obj *def);
    int typed_obj_copy(typed_obj *dst, const typed_obj *src);
    int typed_obj_move(typed_obj *dst, typed_obj *src);
    int typed_obj_delete(typed_obj *o);

    extern const type_def_obj i8_def;
    extern const type_def_obj u8_def;
    extern const type_def_obj i16_def;
    extern const type_def_obj u16_def;
    extern const type_def_obj i32_def;
    extern const type_def_obj u32_def;
    extern const type_def_obj i64_def;
    extern const type_def_obj u64_def;
    extern const type_def_obj f32_def;
    extern const type_def_obj f64_def;
    extern const type_def_obj bool_def;
    extern const type_def_obj char_def;
    extern const type_def_obj ptr_def;

#ifdef __cplusplus
}
#endif
#endif
