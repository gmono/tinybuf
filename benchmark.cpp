//
// Created by xzl on 2019/9/26.
//

#include "tinybuf.h"
#include "tinybuf_plugin.h"
#include "tinybuf_log.h"
#include "jsoncpp/json.h"
#include <sstream>
#ifndef _WIN32
#include <sys/time.h>
#else
#include <chrono>
#endif
#include <assert.h>


using namespace std;
using namespace Json;

#define JSON_COMPACT 0
#define MAX_COUNT 1 * 10000

static inline uint64_t getCurrentMicrosecondOrigin() {
#ifndef _WIN32
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000LL + tv.tv_usec;
#else
    return  std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
#endif
}

class TimePrinter {
public:
    TimePrinter(const string &str){
        _str = str;
        _start_time = getCurrentMicrosecondOrigin();
    }
    ~TimePrinter(){
        LOGD("%stime spent:%lld ms",_str.data(),(long long)((getCurrentMicrosecondOrigin() - _start_time) / 1000));
    }

private:
    string _str;
    uint64_t _start_time;
};


#ifndef _WIN32
#else
#endif

static int any_version(uint64_t){ return 1; }
static int is_v1(uint64_t v){ return v == 1; }
static int not_v1(uint64_t v){ return v != 1; }

tinybuf_value *tinybuf_make_test_value(){
    tinybuf_value *value = tinybuf_value_alloc();
    {
        tinybuf_value *child = tinybuf_value_alloc();
        tinybuf_value_init_double(child,3.1415);
        tinybuf_value_map_set(value,"double",child);
    }

    {
        tinybuf_value *child = tinybuf_value_alloc();
        tinybuf_value_init_double(child,0.000000012345);
        tinybuf_value_map_set(value,"little double",child);
    }

    {
        tinybuf_value *child = tinybuf_value_alloc();
        tinybuf_value_map_set(value,"null",child);
    }

    {
        tinybuf_value *child = tinybuf_value_alloc();
        tinybuf_value_init_int(child,123456789);
        tinybuf_value_map_set(value,"+int",child);
    }

    {
        tinybuf_value *child = tinybuf_value_alloc();
        tinybuf_value_init_int(child,-123456789);
        tinybuf_value_map_set(value,"-int",child);
    }

    {
        tinybuf_value *child = tinybuf_value_alloc();
        tinybuf_value_init_bool(child,1);
        tinybuf_value_map_set(value,"bool true",child);
    }

    {
        tinybuf_value *child = tinybuf_value_alloc();
        tinybuf_value_init_bool(child,0);
        tinybuf_value_map_set(value,"bool false",child);
    }

    {
        tinybuf_value *child = tinybuf_value_alloc();
        tinybuf_value_init_string(child,"hello world string",0);
        tinybuf_value_map_set(value,"string",child);
    }

    {
        tinybuf_value *child = tinybuf_value_alloc();
        tinybuf_value_init_string(child,"",0);
        tinybuf_value_map_set(value,"empty string",child);
    }

    {
        tinybuf_value *child = tinybuf_value_alloc_with_type(tinybuf_map);
        tinybuf_value_map_set(value,"empty map",child);
    }

    {
        tinybuf_value *child = tinybuf_value_alloc_with_type(tinybuf_array);
        tinybuf_value_map_set(value,"empty array",child);
    }

    {
        tinybuf_value *child = tinybuf_value_alloc();
        char bytes[] = "\x1F \xAF 1234\r\nabc\t\b\f\\\"ssds\"\x00\x01\x02中文13kxsdlasdl21";
        tinybuf_value_init_string(child,bytes, sizeof(bytes));
        tinybuf_value_map_set(value,"bytes\r\n",child);
    }

    {
        tinybuf_value *child = tinybuf_value_alloc();
        char bytes[] = "bytes\r\n\x1F \xAF 1234\r\nabc\t\b\f\\\"ssds\"\x03\x01\x02中文13kxsdlasdl21";
        tinybuf_value_init_string(child,bytes, sizeof(bytes));
        //json cpp 不支持key中带'\0'
        tinybuf_value_map_set2(value,buffer_alloc2(bytes, sizeof(bytes) - 1),child);
    }


    {
        tinybuf_value *child = tinybuf_value_alloc();
        {
            tinybuf_value *array_child = tinybuf_value_alloc();
            tinybuf_value_init_double(array_child,3.1415);
            tinybuf_value_array_append(child,array_child);
        }

        {
            tinybuf_value *array_child = tinybuf_value_alloc();
            tinybuf_value_init_int(array_child,123456789);
            tinybuf_value_array_append(child,array_child);
        }

        {
            tinybuf_value *array_child = tinybuf_value_alloc();
            tinybuf_value_init_int(array_child,-123456789);
            tinybuf_value_array_append(child,array_child);
        }

        {
            tinybuf_value *array_child = tinybuf_value_alloc();
            tinybuf_value_init_bool(array_child,1);
            tinybuf_value_array_append(child,array_child);
        }

        {
            tinybuf_value *array_child = tinybuf_value_alloc();
            tinybuf_value_init_bool(array_child,0);
            tinybuf_value_array_append(child,array_child);
        }

        tinybuf_value_array_append(child,tinybuf_value_clone(child));
        tinybuf_value_array_append(child,tinybuf_value_clone(value));
        tinybuf_value_map_set(value,"array",child);
    }

    tinybuf_value_map_set(value,"map",tinybuf_value_clone(value));
    return value;
}


void tinybuf_value_printf(const tinybuf_value *value){
    buffer *out = buffer_alloc();
    tinybuf_value_serialize_as_json(value, out,JSON_COMPACT);
    LOGI("\r\n%s",buffer_get_data(out));
    buffer_free(out);
}

tinybuf_value *deserialize_from_jsoncpp(const char *json){
    Value obj;
    stringstream ss(json);
    ss >> obj;
    string new_json = obj.toStyledString();

    tinybuf_value *ret = tinybuf_value_alloc();
    tinybuf_value_deserialize_from_json(new_json.data(),new_json.size(),ret);
    return ret;
}


void tinybuf_value_test(){
    tinybuf_value *value_origin = tinybuf_make_test_value();
    tinybuf_value *value_deserialize_try = tinybuf_value_alloc();
    tinybuf_value *value_deserialize_ver = tinybuf_value_alloc();

    buffer *buf = buffer_alloc();

    // trywrite/tryread 基础box
    tinybuf_try_write_box(buf, value_origin);
    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_try_read_box(&br, value_deserialize_try, any_version);

    // 版本box读写
    buffer_set_length(buf,0);
    tinybuf_try_write_version_box(buf, (uint64_t)1, value_origin);
    buf_ref br2{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_try_read_box(&br2, value_deserialize_ver, is_v1);

    assert(tinybuf_value_is_same(value_origin,value_deserialize_try));
    assert(tinybuf_value_is_same(value_origin,value_deserialize_ver));

    tinybuf_value_printf(value_origin);

    tinybuf_value_free(value_origin);
    tinybuf_value_free(value_deserialize_try);
    tinybuf_value_free(value_deserialize_ver);
    buffer_free(buf);
}

static void ring_self_pointer_test(){
    buffer *ring = buffer_alloc();
    tinybuf_try_write_pointer(ring, tinybuf_offset_start, 0);
    buf_ref br{buffer_get_data(ring), (int64_t)buffer_get_length(ring), buffer_get_data(ring), (int64_t)buffer_get_length(ring)};
    tinybuf_value *out = tinybuf_value_alloc();
    int r = tinybuf_try_read_box(&br, out, any_version);
    if(r > 0){
        tinybuf_value_free(out);
    }else{
        tinybuf_value_free(out);
    }
    buffer *text = buffer_alloc();
    tinybuf_dump_buffer_as_text(buffer_get_data(ring), buffer_get_length(ring), text);
    LOGI("\r\n%s", buffer_get_data(text));
    buffer_free(text);
    buffer_free(ring);
}

static void version_box_tests(){
    buffer *buf = buffer_alloc();
    tinybuf_value *val = tinybuf_make_test_value();
    tinybuf_try_write_version_box(buf, (uint64_t)1, val);

    buf_ref br_ok{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_value *out_ok = tinybuf_value_alloc();
    int r1 = tinybuf_try_read_box(&br_ok, out_ok, is_v1);
    assert(r1 > 0);
    tinybuf_value_free(out_ok);

    buf_ref br_fail{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_value *out_fail = tinybuf_value_alloc();
    int r2 = tinybuf_try_read_box(&br_fail, out_fail, not_v1);
    assert(r2 <= 0);
    tinybuf_value_free(out_fail);

    tinybuf_value_free(val);
    buffer_free(buf);
}

static void version_box_pointer_tests(){
    // version wraps a pointer; dump should show [circle 0] transparently
    buffer *buf = buffer_alloc();
    buffer *text = buffer_alloc();
    tinybuf_value *inner = tinybuf_value_alloc(); tinybuf_value_init_int(inner, 123);
    tinybuf_try_write_box(buf, inner);
    tinybuf_try_write_version_box(buf, (uint64_t)1, inner);
    tinybuf_try_write_pointer(buf, tinybuf_offset_start, 0);
    tinybuf_dump_buffer_as_text(buffer_get_data(buf), buffer_get_length(buf), text);
    LOGI("\r\n%s", buffer_get_data(text));
    buffer_free(text);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    int r = tinybuf_try_read_box(&br, out, is_v1);
    assert(r > 0);
    tinybuf_value_free(out);
    tinybuf_value_free(inner);
    buffer_free(buf);
}

static void version_list_pointer_tests(){
    buffer *buf = buffer_alloc();
    // version 1: pointer to start 0; version 2: int 99
    tinybuf_value *v1 = tinybuf_value_alloc(); tinybuf_value_init_int(v1, 1);
    tinybuf_value *v2 = tinybuf_value_alloc(); tinybuf_value_init_int(v2, 99);
    const tinybuf_value *boxes[2] = {v1, v2};
    uint64_t vers[2] = {1, 2};
    tinybuf_try_write_version_list(buf, vers, boxes, 2);
    tinybuf_try_write_pointer(buf, tinybuf_offset_start, 0);

    // dump shows versions mapping; pointer shown as [circle 0]
    buffer *text = buffer_alloc(); tinybuf_dump_buffer_as_text(buffer_get_data(buf), buffer_get_length(buf), text);
    LOGI("\r\n%s", buffer_get_data(text)); buffer_free(text);

    // read with is_v1
    tinybuf_value *out1 = tinybuf_value_alloc(); buf_ref br1{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    int r1 = tinybuf_try_read_box(&br1, out1, is_v1); assert(r1 > 0);
    tinybuf_value_free(out1);
    // read with not_v1 => expect version 2
    tinybuf_value *out2 = tinybuf_value_alloc(); buf_ref br2{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    int r2 = tinybuf_try_read_box(&br2, out2, not_v1); assert(r2 > 0);
    tinybuf_value_free(out2);

    tinybuf_value_free(v1); tinybuf_value_free(v2); buffer_free(buf);
}

static void pointer_types_tests(){
    buffer *buf = buffer_alloc();
    tinybuf_value *base = tinybuf_value_alloc();
    tinybuf_value_init_int(base, 42);
    tinybuf_try_write_box(buf, base);
    int64_t off_base = 0; // base starts at 0

    // from start positive
    tinybuf_try_write_pointer(buf, tinybuf_offset_start, off_base);
    // from end positive: point back to base
    int64_t from_end_back = (int64_t)buffer_get_length(buf) - off_base; // end->base distance
    tinybuf_try_write_pointer(buf, tinybuf_offset_end, from_end_back);
    // from current positive: append pointer that points back to base
    int64_t cur_to_base = (int64_t)buffer_get_length(buf) - off_base;
    tinybuf_try_write_pointer(buf, tinybuf_offset_current, cur_to_base);

    // read and verify each pointer yields a ref
    for(int i=0;i<3;++i){
        buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf) + off_base, (int64_t)buffer_get_length(buf) - off_base};
        tinybuf_value *out = tinybuf_value_alloc();
        int r = tinybuf_try_read_box(&br, out, any_version);
        assert(r > 0);
        assert(tinybuf_value_get_type(out) == tinybuf_value_ref || tinybuf_value_get_type(out) == tinybuf_int || tinybuf_value_get_type(out) == tinybuf_map || tinybuf_value_get_type(out) == tinybuf_array);
        tinybuf_value_free(out);
        off_base = r; // move to next pointer location
    }

    tinybuf_value_free(base);
    buffer_free(buf);
}

static void self_pointer_clear_test(){
    buffer *buf = buffer_alloc();
    tinybuf_try_write_pointer(buf, tinybuf_offset_start, 0);
    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_value *out = tinybuf_value_alloc();
    int r = tinybuf_try_read_box(&br, out, any_version);
    assert(r > 0);
    tinybuf_value_clear(out);
    assert(tinybuf_value_get_type(out) == tinybuf_null);
    tinybuf_value_free(out);
    buffer_free(buf);
}

static void container_pointer_dump_tests(){
    // array: [42, [circle 0]]
    {
        buffer *buf = buffer_alloc();
        tinybuf_try_write_array_header(buf, 2);
        tinybuf_value *v = tinybuf_value_alloc(); tinybuf_value_init_int(v, 42);
        tinybuf_try_write_box(buf, v);
        tinybuf_try_write_pointer(buf, tinybuf_offset_start, 0);
        buffer *text = buffer_alloc(); tinybuf_dump_buffer_as_text(buffer_get_data(buf), buffer_get_length(buf), text);
        LOGI("\r\n%s", buffer_get_data(text));
        buffer_free(text); tinybuf_value_free(v); buffer_free(buf);
    }
    // map: {"self": [circle 0]}
    {
        buffer *buf = buffer_alloc();
        tinybuf_try_write_map_header(buf, 1);
        const char *key = "self";
        tinybuf_try_write_string_raw(buf, key, (int)strlen(key));
        tinybuf_try_write_pointer(buf, tinybuf_offset_start, 0);
        buffer *text = buffer_alloc(); tinybuf_dump_buffer_as_text(buffer_get_data(buf), buffer_get_length(buf), text);
        LOGI("\r\n%s", buffer_get_data(text));
        buffer_free(text); buffer_free(buf);
    }
}

static void complex_pointer_mix_tests(){
    {
        buffer *buf = buffer_alloc();
        tinybuf_value *base = tinybuf_value_alloc(); tinybuf_value_init_int(base, 7);
        tinybuf_try_write_box(buf, base);
        tinybuf_try_write_pointer(buf, tinybuf_offset_start, 0);
        int64_t cur = (int64_t)buffer_get_length(buf);
        tinybuf_try_write_pointer(buf, tinybuf_offset_current, -cur);
        int64_t end_back = (int64_t)buffer_get_length(buf) - 0;
        tinybuf_try_write_pointer(buf, tinybuf_offset_end, end_back);
        buffer *text = buffer_alloc(); tinybuf_dump_buffer_as_text(buffer_get_data(buf), buffer_get_length(buf), text);
        LOGI("\r\n%s", buffer_get_data(text));
        buffer_free(text); tinybuf_value_free(base); buffer_free(buf);
    }
    {
        buffer *buf = buffer_alloc();
        tinybuf_try_write_array_header(buf, 3);
        tinybuf_value *v1 = tinybuf_value_alloc(); tinybuf_value_init_int(v1, 1);
        tinybuf_try_write_box(buf, v1);
        int64_t cur = (int64_t)buffer_get_length(buf);
        tinybuf_try_write_pointer(buf, tinybuf_offset_current, -cur);
        tinybuf_try_write_pointer(buf, tinybuf_offset_start, 0);
        buffer *text = buffer_alloc(); tinybuf_dump_buffer_as_text(buffer_get_data(buf), buffer_get_length(buf), text);
        LOGI("\r\n%s", buffer_get_data(text));
        buffer_free(text); tinybuf_value_free(v1); buffer_free(buf);
    }
    {
        buffer *buf = buffer_alloc();
        tinybuf_try_write_map_header(buf, 2);
        const char *keyA = "A"; const char *keyB = "B";
        tinybuf_try_write_string_raw(buf, keyA, (int)strlen(keyA));
        tinybuf_try_write_pointer(buf, tinybuf_offset_start, 0);
        tinybuf_try_write_string_raw(buf, keyB, (int)strlen(keyB));
        int64_t cur = (int64_t)buffer_get_length(buf);
        tinybuf_try_write_pointer(buf, tinybuf_offset_current, -cur);
        buffer *text = buffer_alloc(); tinybuf_dump_buffer_as_text(buffer_get_data(buf), buffer_get_length(buf), text);
        LOGI("\r\n%s", buffer_get_data(text));
        buffer_free(text); buffer_free(buf);
    }
}

static void compare_serialized_vs_deserialized(){
    buffer *buf = buffer_alloc();
    // array level-0 with 4 elements
    tinybuf_try_write_array_header(buf, 4);
    // elem0: map with id/self/nested
    tinybuf_try_write_map_header(buf, 3);
    {
        const char *k = "id"; tinybuf_try_write_string_raw(buf, k, (int)strlen(k));
        tinybuf_value *idv = tinybuf_value_alloc(); tinybuf_value_init_int(idv, 1); tinybuf_try_write_box(buf, idv); tinybuf_value_free(idv);
    }
    {
        const char *k = "self"; tinybuf_try_write_string_raw(buf, k, (int)strlen(k));
        tinybuf_try_write_pointer(buf, tinybuf_offset_start, 0);
    }
    {
        const char *k = "nested"; tinybuf_try_write_string_raw(buf, k, (int)strlen(k));
        // nested array with 2 children: relative pointer back to start; version box of int
        tinybuf_try_write_array_header(buf, 2);
        int64_t cur = (int64_t)buffer_get_length(buf);
        tinybuf_try_write_pointer(buf, tinybuf_offset_current, -cur);
        tinybuf_value *vv = tinybuf_value_alloc(); tinybuf_value_init_int(vv, 77);
        tinybuf_try_write_version_box(buf, (uint64_t)1, vv);
        tinybuf_value_free(vv);
    }
    // elem1: version box int 99
    {
        tinybuf_value *v = tinybuf_value_alloc(); tinybuf_value_init_int(v, 99);
        tinybuf_try_write_version_box(buf, (uint64_t)1, v); tinybuf_value_free(v);
    }
    // elem2: pointer from end back to start
    {
        int64_t end_back = (int64_t)buffer_get_length(buf) - 0;
        tinybuf_try_write_pointer(buf, tinybuf_offset_end, end_back);
    }
    // elem3: small array with pointer to start
    {
        tinybuf_try_write_array_header(buf, 1);
        tinybuf_try_write_pointer(buf, tinybuf_offset_start, 0);
    }

    // deserialize and print
    LOGI("\r\ncompare-deserialized");
    {
        tinybuf_value *out = tinybuf_value_alloc();
        buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
        int r = tinybuf_try_read_box(&br, out, any_version);
        assert(r > 0);
        tinybuf_value_printf(out);
        tinybuf_value_free(out);
    }
    // serialized display (pretty, with pointer and prefixes)
    LOGI("\r\ncompare-serialized");
    {
        buffer *text = buffer_alloc();
        tinybuf_dump_buffer_as_text(buffer_get_data(buf), buffer_get_length(buf), text);
        LOGI("\r\n%s", buffer_get_data(text));
        buffer_free(text);
    }

    buffer_free(buf);
}

int main(int argc,char *argv[]){
    tinybuf_value_test();
    ring_self_pointer_test();
    version_box_tests();
    version_box_pointer_tests();
    version_list_pointer_tests();
    pointer_types_tests();
    self_pointer_clear_test();
    container_pointer_dump_tests();
    complex_pointer_mix_tests();
    compare_serialized_vs_deserialized();
    // dump readable for pointer types first pointer
    {
        buffer *buf = buffer_alloc();
        tinybuf_value *base = tinybuf_value_alloc(); tinybuf_value_init_int(base, 42);
        tinybuf_try_write_box(buf, base);
        tinybuf_try_write_pointer(buf, tinybuf_offset_start, 0);
        buffer *text = buffer_alloc();
        tinybuf_dump_buffer_as_text(buffer_get_data(buf), buffer_get_length(buf), text);
        LOGI("\r\n%s", buffer_get_data(text));
        buffer_free(text);
        tinybuf_value_free(base);
        buffer_free(buf);
    }

    //几米对象
    tinybuf_value *value = tinybuf_make_test_value();
    //数据缓冲区
    buffer *buf_binary = buffer_alloc();
    buffer *buf_json = buffer_alloc();

    //trywrite 序列化
    tinybuf_try_write_box(buf_binary, value);
    //json序列化用于对比
    tinybuf_value_serialize_as_json(value,buf_json,JSON_COMPACT);


    {
        //测试 trywrite 序列化性能
        TimePrinter printer("trywrite box");
        buffer *buf = buffer_alloc();
        for(int i = 0 ; i < MAX_COUNT; ++i ){
            buffer_set_length(buf,0);
            tinybuf_try_write_box(buf,value);
        }
        buffer_free(buf);
    }

    {
        //测试几米序列化成json性能
        TimePrinter printer("serialize to json");
        buffer *buf = buffer_alloc();
        for(int i = 0 ; i < MAX_COUNT; ++i ){
            buffer_set_length(buf,0);
            tinybuf_value_serialize_as_json(value,buf,JSON_COMPACT);
        }
        buffer_free(buf);
    }

    {
        //测试json列化性能
        TimePrinter printer("jsoncpp serialize");
        //json对象
        Value obj;
        stringstream ss(buffer_get_data(buf_json));
        ss >> obj;
        for(int i = 0 ; i < MAX_COUNT; ++i ){
            obj.toStyledString();
        }
    }

    {
        //测试 tryread 反序列化性能
        TimePrinter printer("tryread box");
        tinybuf_value *value_deserialize = tinybuf_value_alloc();
        for(int i = 0 ; i < MAX_COUNT; ++i ){
            tinybuf_value_clear(value_deserialize);
            buf_ref br{buffer_get_data(buf_binary), (int64_t)buffer_get_length(buf_binary), buffer_get_data(buf_binary), (int64_t)buffer_get_length(buf_binary)};
            tinybuf_try_read_box(&br, value_deserialize, any_version);
        }
        tinybuf_value_free(value_deserialize);
    }

    {
        //测试几米反序列json化性能
        TimePrinter printer("json deserialize");
        tinybuf_value *value_deserialize = tinybuf_value_alloc();
        for(int i = 0 ; i < MAX_COUNT; ++i ){
            tinybuf_value_clear(value_deserialize);
            tinybuf_value_deserialize_from_json(buffer_get_data(buf_json),buffer_get_length(buf_json),value_deserialize);
        }
        tinybuf_value_free(value_deserialize);
    }

    {
        //测试json反序列化性能
        TimePrinter printer("jsoncpp deserialize");
        Value obj;
        Json::Reader reader;
        string str(buffer_get_data(buf_json));
        for(int i = 0 ; i < MAX_COUNT; ++i ){
            reader.parse(str, obj);
        }
    }

    buffer_free(buf_binary);
    buffer_free(buf_json);
    tinybuf_value_free(value);
    return 0;
}
