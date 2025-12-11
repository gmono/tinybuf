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


//基本字符串

struct bin_str{
    const char* dataptr;
    int size; //-1表示没有计算长度
};

//1 带有deleter的纯字符串 2 纯字符串列表 3 带有hole的纯字符串 中间可以穿插binvalue
// struct hole_str{

// }