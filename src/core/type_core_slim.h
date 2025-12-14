
// 基本类型定义 只能表示普通二进制类型 可以表示所有内置类型
#include <stdint.h>
/* 基础函数指针 */
typedef void deleter_fn(void *self);
typedef void copy_fn(const void *from, void *to);
typedef void move_fn(void *from, void *to);
typedef void *alloc_fn(void);
typedef size_t get_total_size_fn(const void *self);

// 用于初始化某个对象
typedef void initer(void *def, void *target);

enum type_def_type
{
    basic,
    external
};
struct binary_type_def
{
    enum type_def_type def_type;
    const char *name;
    size_t size;
};

// 一个缓冲区描述符
struct buff_desc
{
    void *dataptr;
    size_t size;
    deleter_fn *deleter;
};

// 用于表示类似string这样的一个指针+一个数据块的东西
struct external_type_def
{
    enum type_def_type def_type;
    const char *name;
    size_t self_size;
    // 一个函数指针用于获取目标缓冲区大小
    struct buff_desc (*get_buffer_ptr)();
};
inline enum type_def_type get_def_type(void *target)
{
    return *(enum type_def_type *)target;
}

#define DEF_BINARY(n, tartype) struct binary_type_def def_##n = { \
                                   .name = #n,                    \
                                   .size = sizeof(tartype)};

DEF_BINARY(i32, int32_t)
DEF_BINARY(i8, int8_t)

// 函数定义 包含完整的函数签名
struct function_def
{

};
// 方法描述符
struct method_info
{
};