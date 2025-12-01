#ifndef TINYBUF_PRIVATE_H
#define TINYBUF_PRIVATE_H

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "avl-tree.h"
#include "tinybuf.h"
#include "tinybuf_memory.h"
#include "tinybuf_log.h"
#include "tinybuf_buffer_private.h"

typedef void (*free_handler)(void*);
//动态值表示
struct T_tinybuf_value{
    union {
        int64_t _int;
        int _bool;
        double _double;
        buffer *_string; //变长缓冲区
        AVLTree *_map_array; //kvpairs versionlist也会使用此字段保存不同版本的buf引用
        void* _custom; //自定义类型指针
        tinybuf_value *_ref; //引用类型指针 value_ref version都会使用此字段
    } _data;
    //自定义类型的释放函数
    free_handler _custom_free;
    //vid默认初始化-1表示不存在vid直接比较指针
    int64_t vid;
    tinybuf_type _type;
};

#endif//TINYBUF_PRIVATE_H



