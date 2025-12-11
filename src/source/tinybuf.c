#include "tinybuf_private.h"
#include "tinybuf_plugin.h"
#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif
#include <stdbool.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <stdatomic.h>
#include <sched.h>
#include <time.h>
#endif

// 1 支持变长数字格式并支持配置开启 2 支持KVPair通用格式
// 3 支持自描述结构和向前兼容支持
// int类型包括1到8字节整数 包括有符号无符号 且以无符号方式存储

// dll扩展支持 支持dll提交 sign支持列表  并提供read与write接口执行基于插件的序列化

typedef int64_t ssize;
typedef uint64_t usize;

static tinybuf_read_pointer_mode s_read_pointer_mode = tinybuf_read_pointer_ref;

typedef uint64_t QWORD;
typedef int64_t SQWORD;

/* s_last_error_msg is defined in tinybuf_deserialize.c */

// 是否含有子引用


// 是否需要执行custom 指针释放

//---压缩boxlist数据集合的序列化
static inline int boxlist_serialize()
{
}
static inline int boxlist_deserialize()
{
}
//--压缩kvpairs的序列化

static inline int kvpairs_serialize()
{
}
static inline int kvpairs_deserialize()
{
}
static inline int kvpairs_boxkey_serialize()
{
}
static inline int kvpairs_boxkey_deserialize()
{
}




 





/* strpool declarations are at the top of file */


// 支持自定义描述序列支持的反序列化操作
// 描述子是一串 serialize_type 类型的字节序列
// 用于支持集中类型表示 集中类型表示可优化内存与寄存器
//  int read_box_by_descriptor(const  char* ptr,int size,const serialize_type* desc,int desclen){

// }

//--核心反序列化路由函数 初级版本 只能处理纯初级格式 内部不能嵌套二级box
 

//! --------------------------高级反序列化系列--------------

//>0 表示成功 0表示无法读取（看作失败） 负数表示失败
#define OK(x) x > 0
#define OK_AND_SAVE(x, s) ((s = x) > 0)
// 必须保证s>=0 成功则add 0或失败则不add
inline int OK_AND_ADDTO(int x, int *s)
{
    if (x > 0)
    {
        *s += x;
        return 1;
    }
    return 0;
}

// 从指针位置开始读取box 提供开始位置+偏移
// 返回值表示消耗的字节数

typedef tinybuf_value value;

//------------utils结束------

// 所有read函数 返回-1表示失败 否则返回消耗的字节数返回0表示数据不够 负数表示具体错误

//--读取一个type 标记

// 指针偏移系统：types are declared in tinybuf_private.h

// 转换pointervalue为从start开始的pointervalue
// 直接修改ptr指针对象

// 支持带错误信息的返回
typedef struct
{
    int len;
    const char *reason;
} read_result;
#define RESULT_OK(x) (x.len > 0)
//--等效ok系列
BOOL RESULT_OK_AND_ADDTO(read_result x, int *s)
{
    if (RESULT_OK(x))
    {
        *s += x.len;
        return TRUE;
    }
    return FALSE;
}

// 高级序列化read入口
// 二级版本 可处理二级格式 readbox标准 成功则修改buf指针 返回>0 否则不修改并返回<=0


// 写入总入口由 tinybuf_write.c 提供实现
 

// public wrappers
void tinybuf_set_read_pointer_mode(tinybuf_read_pointer_mode mode)
{
    s_read_pointer_mode = mode;
}

void tinybuf_set_use_strpool(int enable)
{
    s_use_strpool = enable ? 1 : 0;
}

static inline tinybuf_error _err_with(const char *msg, int rc)
{
    return tinybuf_result_err(rc, msg, NULL);
}


static int avl_tree_for_each_node_is_same(void *user_data, AVLTreeNode *node)
{
    AVLTree *tree2 = (AVLTree *)user_data;
    AVLTreeKey key = avl_tree_node_key(node);
    tinybuf_value *child1 = (tinybuf_value *)avl_tree_node_value(node);
    tinybuf_value *child2 = avl_tree_lookup(tree2, key);
    if (!child2)
    {
        // 不存在相应的key
        return 1;
    }
    if (!tinybuf_value_is_same(child1, child2))
    {
        // 相同key下，value不一致
        return 1;
    }

    // 继续遍历
    return 0;
}

/////////////////////////////读接口//////////////////////////////////

int contain_any(uint64_t v)
{
    (void)v;
    return 1;
}
