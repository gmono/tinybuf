/**
 * 定义核心操作函数
 */

// 类型操作函数
#include <cstring>
#include <cstdlib>
typedef void deleter_fn(void *);
typedef void copy_fn(void *from, void *to);
typedef void move_fn(void *from, void *to);
typedef void *alloc_fn();

// 类型定义对象
typedef struct
{
    // 类型名 唯一
    const char *name;
    deleter_fn* deleter;
    copy_fn* copy;
    move_fn* move;
    alloc_fn* alloc;
    size_t size;
} type_def_obj;

void default_mv(void *from, void *to, size_t s)
{
    memcpy(to, from, s);
    memset(from, 0, s);
}
void default_copy(void *f, void *t, size_t s)
{
    memcpy(t, f, s);
}

void default_delete(void *obj)
{
    free(obj);
}
//--对象移动 入口 优先使用def里的
inline void object_mv(type_def_obj *def, void *from, void *to)
{
    if (def->move) {
        // 使用用户自定义的 move 函数
        def->move(from, to);
    } else {
        // 使用默认的移动函数
        default_mv(from, to, def->size);
    }
}


// 对象复制 入口 优先使用def里的
inline void object_copy(type_def_obj *def, void *from, void *to)
{
    if (def->copy) {
        // 使用用户自定义的 copy 函数
        def->copy(from, to);
    } else {
        // 使用默认的复制函数
        default_copy(from, to, def->size);
    }
}

// 对象删除 入口 优先使用def里的
void object_delete(type_def_obj *def, void *obj)
{
    if (def->deleter) {
        // 使用用户自定义的 deleter 函数
        def->deleter(obj);
    } else {
        // 使用默认的删除函数
        default_delete(obj);
    }
}




//纯二进制 其他全部默认
#define TYPEDEF_FOR_BINARY_STRUCT(s) type_def_obj obj={.name=#s};