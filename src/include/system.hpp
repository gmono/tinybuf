#include <cstdint>
#include <memory>
#define CRTP_BASE            \
    Self *get_self()         \
    {                        \
        return (Self *)this; \
    }

typedef unsigned char byte;
// 类boost的序列化接口
template <class Self>
class ISerializer
{
public:
    Self *get_self()
    {
        return (Self *)this;
    }
    // 与deserializer同接口
    template <class T>
    void operator&&(T &t)
    {
        get_self()->write<T>(t);
    }
};

template <class Self>
class IDeserializer
{
public:
    Self *get_self()
    {
        return (Self *)this;
    }
    template <class T>
    void operator&&(T &t)
    {
        get_self()->read<T>(t);
    }
};

// 可写入对象
template <class Self>
class IWritable
{
public:
    CRTP_BASE
    void write_bytes(byte *ptr, size_t len);
};

// 可读取对象
template <class Self>
class IReadable
{
public:
    CRTP_BASE
    void read_bytes(byte *ptr, size_t len);
};

/**
 * @brief 标准序列化器
 * 支持基本类型、字符串、数组、结构体、枚举、类等类型的序列化
 * 支持自动搜索serializable 特化实现支持自定义类型
 *
 */
class standard_serializer : public ISerializer<standard_serializer>
{
public:
    template <class T>
    bool is_binary()
    {
        return std::is_integral<T>::value || std::is_floating_point<T>::value;
    }

    // 纯二进制写入
    template <class T>
    void write_binary(T &t)
    {
    }
    // 通用入口
    template <class T>
    void write(T &t)
    {
        if (is_binary<T>())
        {
        }
    }
};
// 可序列化类型 支持&&表示 可实现serializable<T> 来支持T 的readwrite

template <class T>
class serializable
{
public:
    void write(T &t);
    void read(T &t);
};

typedef uint64_t usize;
typedef char *dataptr;
// 抽象缓冲区 用于支持非连续内存区域
// 这里不假设内存连续因此不能直接修改指针设置地址 应该通过offset等函数设置
// 提供抽象连续空间 base永远是0  提供allsize和size ptr是抽象ptr
template <class Self>
class IBuffer
{
public:
    CRTP_BASE
    // 剩余大小 根据currpos计算
    usize rest_size() { return allsize() - curr_offset(); }
    dataptr curr_ptr() { return ptr(curr_offset()); } //如果优化 内部做最近访问cache

    //----代理方法
    // 这里考虑可以直接返回当前指针不需要计算的情况
    usize all_size() { return get_self()->all_size(); }
    // 获取实际指针 通过偏移
    dataptr ptr(usize offset) { return get_self()->ptr(offset); }
    // 当前抽象空间指针
    usize curr_offset() { return get_self()->curr_offset(); }
    // 不可用于计算只能用于访问
    void offset(usize n) { get_self()->offset(n); }
    // 类似修改指针 但是是相对偏移 相对base
    void set_offset(usize n) { get_self()->set_offset(n); }
};

// 子缓冲区 支持在缓冲区内添加限制性的子空间
template <class Target>
class SubBuffer : public IBuffer<SubBuffer<Target>>
{
public:
    std::shared_ptr<Target> target;
    usize _offset;
    usize _limit;
    SubBuffer(std::shared_ptr<Target> target, usize offset, usize size) : target(target),
                                                                          _offset(offset),
                                                                          _limit(size)
    {
    }
    //----实现IBuffer的CRTP代理方法
    usize all_size() override { return _limit; }
    dataptr ptr(usize offset) override { return target->ptr(_offset + offset); }
    usize curr_offset() override { return target->curr_offset() - _offset; }
    void offset(usize n) override { target->offset(n); }
    void set_offset(usize n) override { target->set_offset(_offset + n); }
};

//引用表


//序列化支持中 可以直接serialize(){xx&xx x&xx} 如果使用级联对象 则会自动查找对应的serializer
//如果要实现指针引用方式而非数据级联则应该使用-> serializer<->xxxx 来实现
//其中xxxx必须是一个类指针 可以是普通对象指针 或可序列化对象指针或对应智能指针如uniqueptr和sharedptr intrusiveptr
