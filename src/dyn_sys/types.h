#include <cstddef>
#include "3rdpart/dyncall-1.4/dyncall/dyncall.h"
/**
 * dyn系统 支持对void*指针的调用 使用dyncall库 基于bin_value类型
 *
 */


typedef enum bin_type
{
    INT,
    FLOAT
} bin_type;

struct bin_value
{
    size_t size;
    void *ptr; // 数据指针
    bin_type wtype;//传参方式
    bin_type rtype;//返回方式
};


void call(void* funcptr,bin_value* valueptr,size_t valuelen){
}