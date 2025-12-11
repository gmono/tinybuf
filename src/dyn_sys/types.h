#include <cstddef>
#include "dyncall.h"
#include <cstdint>
/**
 * dyn系统 支持对void*指针的调用 使用dyncall库 基于bin_value类型
 *
 */

typedef enum bin_type
{
    I32,
    U32,
    U64,
    I64,
    POINTER,
    I8,
    U8,
    I16,
    U16,
    FLOAT,
    DOUBLE,
    // 其他二进制值
    OTHER_BINARY

} bin_type;

struct bin_value
{
    int size;
    union
    {
        uint64_t value;
        void *ptr; // 数据指针 既可以表示指针也可以表示数据段
    } data;
    bin_type wtype; // 二进制值类型
};

void bin_value_set_value(bin_value*ptr,uint64_t value_or_ptr){
    
}
void bin_value_set_type(bin_value *ptr, bin_type tp, int custom_len)
{
    ptr->wtype = tp;
    switch (tp)
    {
    case I8:
    case U8:
        ptr->size = 1;
        break;
    case I16:
    case U16:
        ptr->size = 2;
        break;

    case I32:
    case U32:
        ptr->size = 4;
        break;
    case I64:
    case U64:
        ptr->size = 8;
        break;
    case POINTER:
        ptr->size = sizeof(void *);
        break;
    case OTHER_BINARY:
        ptr->size = custom_len;
        break;
    default:
        break;
    }
}
DCCallVM *new_vm()
{
    DCCallVM *vm = dcNewCallVM(4096);
    dcMode(vm, DC_CALL_C_DEFAULT);
    dcReset(vm);
    return vm;
}
void free_vm(DCCallVM *vm)
{
    dcFree(vm);
}
// 调用目标函数
void call(void *funcptr, struct bin_value *valueptr, size_t valuelen, bin_type rettype)
{
    DCCallVM *vm = new_vm();
    for (size_t i = 0; i < valuelen; i++)
    {
        struct bin_value *curr = valueptr + i;
        switch (curr->wtype)
        {
        case U32:
        case I32:
            dcArgInt(vm, curr->data.value);
            break;
        case I16:
        case U16:
            dcArgShort(vm, curr->data.value);
            break;
        case I8:
        case U8:
            dcArgChar(vm, curr->data.value);
            break;

        case I64:
        case U64:
            dcArgLongLong(vm, curr->data.value);
            break;
        case FLOAT:
            dcArgFloat(vm, curr->data.value);
            break;
        case DOUBLE:
            dcArgDouble(vm, curr->data.value);
            break;
        case POINTER:
            dcArgPointer(vm, curr->data.ptr);
            break;
        case OTHER_BINARY:
        {
            if (curr->size > 0)
            {
            }
        }
        default:
            break;
        }
    }
}

// 基本字符串

struct bin_str
{
    const char *dataptr;
    int size; //-1表示没有计算长度
};

// 1 带有deleter的纯字符串 2 纯字符串列表 3 带有hole的纯字符串 中间可以穿插binvalue
//  struct hole_str{

// }