#ifndef TYPE_CORE_H
#define TYPE_CORE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* 基础函数指针 */
typedef void deleter_fn(void *self);
typedef void copy_fn(const void *from, void *to);
typedef void move_fn(void *from, void *to);
typedef void *alloc_fn(void);
typedef size_t get_total_size_fn(const void *self);

/* 子成员描述 */
typedef enum {
    field_inline_binary = 0,   /* 直接内嵌二进制子结构 */
    field_typed_obj_inline = 1,/* 内嵌 typed_obj */
    field_typed_obj_ptr = 2    /* 指向 typed_obj* 的指针 */
} field_kind;

struct type_def_obj; /* 前置声明 */

typedef struct field_desc {
    const char *name;
    size_t offset;
    field_kind kind;
    const struct type_def_obj *child_type; /* 对于 inline_binary 有效；typed_obj由其自身保存类型 */
    int owns; /* 1表示在删除时递归删除该子对象；0则不管理生命周期 */
} field_desc;

/* 类型定义对象 */
typedef struct type_def_obj
{
    const char *name;
    deleter_fn *deleter;
    copy_fn *copy;
    move_fn *move;
    alloc_fn *alloc;
    size_t size; /* 固定大小结构的基础尺寸 */
    get_total_size_fn *get_total_size; /* 可选：返回整体大小（用于不定长结构，如string/buffer） */
    const field_desc *fields; /* 可选：级联子成员描述，用于深拷贝/删除 */
    int field_count;
} type_def_obj;

/* 带类型的胖指针对象 */
typedef struct typed_obj
{
    void *ptr;
    const type_def_obj *type;
} typed_obj;

/* 默认操作实现（基础内存层） */
void default_mv(void *from, void *to, size_t s);
void default_copy(const void *from, void *to, size_t s);
void default_delete(void *obj);

/* 运行时操作（遵循类型定义与子成员级联） */
size_t type_total_size(const type_def_obj *def, const void *self);
int object_mv(const type_def_obj *def, void *from, void *to);
int object_copy(const type_def_obj *def, const void *from, void *to);
int object_delete(const type_def_obj *def, void *obj);

/* typed_obj 操作 */
int typed_obj_init(typed_obj *o, const type_def_obj *def, void *ptr);
int typed_obj_alloc(typed_obj *o, const type_def_obj *def);
int typed_obj_copy(typed_obj *dst, const typed_obj *src);
int typed_obj_move(typed_obj *dst, typed_obj *src);
int typed_obj_delete(typed_obj *o);

/* 字段辅助宏 */
#define FIELD_INLINE(struct_t, field_name, child_type_ptr, owns_flag) \
    (field_desc){ #field_name, offsetof(struct_t, field_name), field_inline_binary, (child_type_ptr), (owns_flag) }
#define FIELD_OBJ_INLINE(struct_t, field_name, owns_flag) \
    (field_desc){ #field_name, offsetof(struct_t, field_name), field_typed_obj_inline, NULL, (owns_flag) }
#define FIELD_OBJ_PTR(struct_t, field_name, owns_flag) \
    (field_desc){ #field_name, offsetof(struct_t, field_name), field_typed_obj_ptr, NULL, (owns_flag) }

/* 类型定义辅助宏 */
#define TYPEDEF_BINARY(name_str, struct_t) \
    (type_def_obj){ (name_str), NULL, NULL, NULL, NULL, sizeof(struct_t), NULL, NULL, 0 }
#define TYPEDEF_VAR_BINARY(name_str, base_size, getsize_fn) \
    (type_def_obj){ (name_str), NULL, NULL, NULL, NULL, (base_size), (getsize_fn), NULL, 0 }
#define TYPEDEF_COMPOSITE(name_str, struct_t, fields_array, fields_cnt) \
    (type_def_obj){ (name_str), NULL, NULL, NULL, NULL, sizeof(struct_t), NULL, (fields_array), (fields_cnt) }

#ifdef __cplusplus
}
#endif
#endif
