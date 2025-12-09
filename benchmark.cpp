//
// Created by xzl on 2019/9/26.
//

#include "tinybuf.h"
#include "tinybuf_plugin.h"
#include "tinybuf_log.h"
#include "tinybuf_oop.h"

// OOP infra demonstration types and trait
TB_STRUCT_BEGIN(Counter)
    int v;
    tb_spinlock_t lk;
TB_STRUCT_END(Counter)

TB_STATIC_DEF(Counter, Counter, make){ Counter c; c.v = 0; tb_spinlock_init(&c.lk); return c; }
TB_METHOD_DEF(Counter, void, add, int x){ TB_WITH_LOCK(self->lk){ self->v += x; } }

TB_TRAIT_BEGIN(Addable)
    TB_TRAIT_METHOD(void, add, int x)
TB_TRAIT_END(Addable)

static const Addable_vtable Counter_Addable_vt = { (void(*)(void*,int))Counter_add };
TB_TRAIT_IMPL(Counter, Addable, Counter_Addable_vt);
#include "jsoncpp/json.h"
#include <sstream>
#ifndef _WIN32
#include <sys/time.h>
#else
#include <chrono>
#endif
#include <assert.h>
#include <thread>
#include <mutex>


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

static void precache_and_read_mode_tests(){
    // build a big map value
    tinybuf_value *big = tinybuf_value_alloc();
    tinybuf_value_map_set(big, "name", tinybuf_make_test_value());
    tinybuf_value_map_set(big, "n", tinybuf_value_clone(tinybuf_make_test_value()));

    // prepare buffer and precache the big object
    buffer *buf = buffer_alloc();
    tinybuf_precache_reset(buf);
    int64_t start = tinybuf_precache_register(buf, big);
    assert(start >= 0);
    tinybuf_precache_set_redirect(1);

    // write array with two references to big; second is nested map with big again
    tinybuf_try_write_array_header(buf, 3);
    tinybuf_try_write_box(buf, big); // redirected to pointer
    tinybuf_try_write_box(buf, big); // redirected again
    tinybuf_try_write_map_header(buf, 1);
    tinybuf_try_write_string_raw(buf, "again", 5);
    tinybuf_try_write_box(buf, big); // redirected inside map

    // dump serialized
    LOGI("\r\nprecache-serialized");
    buffer *text = buffer_alloc(); tinybuf_dump_buffer_as_text(buffer_get_data(buf), buffer_get_length(buf), text);
    LOGI("\r\n%s", buffer_get_data(text)); buffer_free(text);

    // read: ordinary pointers must be transparent (deref), regardless of mode
    LOGI("\r\nprecache-read-transparent");
    {
        tinybuf_set_read_pointer_mode(tinybuf_read_pointer_ref);
        tinybuf_value *out = tinybuf_value_alloc();
        buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
        int r = tinybuf_try_read_box_with_mode(&br, out, any_version, tinybuf_read_pointer_ref);
        assert(r > 0);
        assert(tinybuf_value_get_type(out) == tinybuf_array);
        int sz = tinybuf_value_get_child_size(out);
        assert(sz == 3);
        const tinybuf_value *c0 = tinybuf_value_get_array_child(out, 0);
        const tinybuf_value *c1 = tinybuf_value_get_array_child(out, 1);
        const tinybuf_value *c2 = tinybuf_value_get_array_child(out, 2);
        assert(tinybuf_value_get_type(c0) != tinybuf_value_ref);
        assert(tinybuf_value_get_type(c1) != tinybuf_value_ref);
        assert(tinybuf_value_get_type(c2) == tinybuf_map);
        buffer *k = NULL; const tinybuf_value *mchild = tinybuf_value_get_map_child_and_key(c2, 0, &k);
        assert(mchild && tinybuf_value_get_type(mchild) != tinybuf_value_ref);
        tinybuf_value_free(out);
    }

    // read in deref mode
    LOGI("\r\nprecache-read-deref");
    {
        tinybuf_set_read_pointer_mode(tinybuf_read_pointer_deref);
        tinybuf_value *out = tinybuf_value_alloc();
        buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
        int r = tinybuf_try_read_box_with_mode(&br, out, any_version, tinybuf_read_pointer_deref);
        assert(r > 0);
        assert(tinybuf_value_get_type(out) == tinybuf_array);
        int sz = tinybuf_value_get_child_size(out);
        assert(sz == 3);
        const tinybuf_value *c0 = tinybuf_value_get_array_child(out, 0);
        const tinybuf_value *c1 = tinybuf_value_get_array_child(out, 1);
        const tinybuf_value *c2 = tinybuf_value_get_array_child(out, 2);
        assert(tinybuf_value_get_type(c0) != tinybuf_value_ref);
        assert(tinybuf_value_get_type(c1) != tinybuf_value_ref);
        buffer *k = NULL; const tinybuf_value *mchild = tinybuf_value_get_map_child_and_key(c2, 0, &k);
        assert(mchild && tinybuf_value_get_type(mchild) != tinybuf_value_ref);
        tinybuf_value_free(out);
    }

    tinybuf_value_free(big);
    buffer_free(buf);
}

static void pointer_auto_mode_mixed_tests(){
    tinybuf_value *big = tinybuf_make_test_value();
    buffer *mixed = buffer_alloc();
    tinybuf_precache_reset(mixed);
    int64_t start = tinybuf_precache_register(mixed, big);
    assert(start >= 0);
    tinybuf_precache_set_redirect(1);

    tinybuf_try_write_array_header(mixed, 2);
    tinybuf_try_write_box(mixed, big); // backward pointer (precache)

    tinybuf_value *later = tinybuf_make_test_value();
    buffer *tmp = buffer_alloc(); tinybuf_try_write_box(tmp, later); int fwd = buffer_get_length(tmp); buffer_free(tmp);
    tinybuf_try_write_pointer(mixed, tinybuf_offset_current, fwd); // forward pointer to later value
    tinybuf_try_write_box(mixed, later);

    tinybuf_set_read_pointer_mode(tinybuf_read_pointer_auto);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br{buffer_get_data(mixed), (int64_t)buffer_get_length(mixed), buffer_get_data(mixed), (int64_t)buffer_get_length(mixed)};
    int r = tinybuf_try_read_box_with_mode(&br, out, any_version, tinybuf_read_pointer_auto);
    assert(r > 0);
    assert(tinybuf_value_get_type(out) == tinybuf_array);
    assert(tinybuf_value_get_child_size(out) == 2);
    const tinybuf_value *c0 = tinybuf_value_get_array_child(out, 0);
    const tinybuf_value *c1 = tinybuf_value_get_array_child(out, 1);
    // ordinary pointers are always deref (transparent)
    assert(tinybuf_value_get_type(c0) != tinybuf_value_ref);
    assert(tinybuf_value_get_type(c1) != tinybuf_value_ref);

    tinybuf_value_free(out);
    tinybuf_value_free(later);
    buffer_free(mixed);
    tinybuf_value_free(big);
}

static void pointer_subref_tests(){
    tinybuf_value *later = tinybuf_make_test_value();
    buffer *tmp = buffer_alloc();
    tinybuf_try_write_box(tmp, later);
    int fwd = buffer_get_length(tmp);
    buffer_free(tmp);

    buffer *buf = buffer_alloc();
    tinybuf_try_write_array_header(buf, 2);
    // forward subref to later element
    tinybuf_try_write_sub_ref(buf, tinybuf_offset_current, fwd);
    int pos_before_later = buffer_get_length(buf);
    tinybuf_try_write_box(buf, later);
    int pos_later = pos_before_later;
    // backward subref to earlier 'later'
    tinybuf_try_write_sub_ref(buf, tinybuf_offset_start, pos_later);

    // verify in ref mode
    tinybuf_set_read_pointer_mode(tinybuf_read_pointer_ref);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    int r = tinybuf_try_read_box_with_mode(&br, out, any_version, tinybuf_read_pointer_ref);
    assert(r > 0);
    assert(tinybuf_value_get_type(out) == tinybuf_array);
    assert(tinybuf_value_get_child_size(out) == 2);
    const tinybuf_value *c0 = tinybuf_value_get_array_child(out, 0);
    const tinybuf_value *c1 = tinybuf_value_get_array_child(out, 1);
    assert(tinybuf_value_get_type(c0) == tinybuf_value_ref);
    assert(tinybuf_value_get_type(c1) == tinybuf_value_ref);
    tinybuf_value_free(out);

    // verify in deref mode (subref must still be ref)
    tinybuf_set_read_pointer_mode(tinybuf_read_pointer_deref);
    out = tinybuf_value_alloc();
    br = buf_ref{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    r = tinybuf_try_read_box_with_mode(&br, out, any_version, tinybuf_read_pointer_deref);
    assert(r > 0);
    assert(tinybuf_value_get_type(out) == tinybuf_array);
    assert(tinybuf_value_get_type(tinybuf_value_get_array_child(out, 0)) == tinybuf_value_ref);
    assert(tinybuf_value_get_type(tinybuf_value_get_array_child(out, 1)) == tinybuf_value_ref);
    tinybuf_value_free(out);

    // verify in auto mode (subref must still be ref)
    tinybuf_set_read_pointer_mode(tinybuf_read_pointer_auto);
    out = tinybuf_value_alloc();
    br = buf_ref{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    r = tinybuf_try_read_box_with_mode(&br, out, any_version, tinybuf_read_pointer_auto);
    assert(r > 0);
    assert(tinybuf_value_get_type(out) == tinybuf_array);
    assert(tinybuf_value_get_type(tinybuf_value_get_array_child(out, 0)) == tinybuf_value_ref);
    assert(tinybuf_value_get_type(tinybuf_value_get_array_child(out, 1)) == tinybuf_value_ref);
    tinybuf_value_free(out);

    tinybuf_value_free(later);
    buffer_free(buf);
}

static void pointer_transparent_across_modes_tests(){
    tinybuf_value *base = tinybuf_value_alloc(); tinybuf_value_init_int(base, 42);
    buffer *buf = buffer_alloc();
    tinybuf_try_write_array_header(buf, 2);
    tinybuf_try_write_box(buf, base);
    tinybuf_try_write_pointer(buf, tinybuf_offset_start, 0);

    // default read (no mode): ordinary pointer must be deref
    {
        tinybuf_value *out = tinybuf_value_alloc();
        buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
        int r = tinybuf_try_read_box(&br, out, any_version);
        assert(r > 0);
        assert(tinybuf_value_get_type(out) == tinybuf_array);
        assert(tinybuf_value_get_child_size(out) == 2);
        const tinybuf_value *c1 = tinybuf_value_get_array_child(out, 1);
        assert(tinybuf_value_get_type(c1) != tinybuf_value_ref);
        tinybuf_value_free(out);
    }

    // explicit modes: still deref for ordinary pointers
    for(int m=0;m<3;++m){
        tinybuf_read_pointer_mode mode = (m==0)?tinybuf_read_pointer_ref:((m==1)?tinybuf_read_pointer_deref:tinybuf_read_pointer_auto);
        tinybuf_value *out = tinybuf_value_alloc();
        buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
        int r = tinybuf_try_read_box_with_mode(&br, out, any_version, mode);
        assert(r > 0);
        assert(tinybuf_value_get_type(out) == tinybuf_array);
        const tinybuf_value *c1 = tinybuf_value_get_array_child(out, 1);
        assert(tinybuf_value_get_type(c1) != tinybuf_value_ref);
        tinybuf_value_free(out);
    }

    buffer_free(buf);
    tinybuf_value_free(base);
}

static int xjson_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler){ (void)name; buf_ref br{ (const char*)data, (int64_t)len, (const char*)data, (int64_t)len }; return tinybuf_try_read_box(&br, out, contain_handler); }
static int xjson_write(const char *name, const tinybuf_value *in, buffer *out){ (void)name; buffer *tmp = buffer_alloc(); int l = tinybuf_try_write_box(tmp, in); if(l>0){ buffer_append(out, buffer_get_data(tmp), l); } buffer_free(tmp); return l; }
static int xjson_dump(const char *name, buf_ref *buf, buffer *out){ (void)name; return tinybuf_dump_buffer_as_text(buf->ptr, (int)buf->size, out); }

static int custstr_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler){ (void)name; (void)contain_handler; tinybuf_value_init_string(out, (const char*)data, len); return len; }
static int custstr_write(const char *name, const tinybuf_value *in, buffer *out){ (void)name; buffer *s = tinybuf_value_get_string(in); if(!s) return -1; int sl = buffer_get_length(s); if(sl>0){ buffer_append(out, buffer_get_data(s), sl); } return sl; }
static int custstr_dump(const char *name, buf_ref *buf, buffer *out){ (void)name; int64_t len = buf->size; buffer_append(out, "\"", 1); if(len>0){ buffer_append(out, buf->ptr, (int)len); } buffer_append(out, "\"", 1); return (int)len; }

static void custom_type_idx_string_tests(){
    tinybuf_set_use_strpool(1);
    buffer *buf = buffer_alloc();
    tinybuf_value *s = tinybuf_value_alloc(); tinybuf_value_init_string(s, "hello", 5);
    tinybuf_register_builtin_plugins();
    int w = tinybuf_try_write_custom_id_box(buf, "string", s); assert(w > 0);
    tinybuf_value *out = tinybuf_value_alloc(); buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    int r = tinybuf_try_read_box(&br, out, any_version); assert(r > 0);
    assert(tinybuf_value_get_type(out) == tinybuf_string);
    buffer *sv = tinybuf_value_get_string(out); assert(sv && buffer_get_length(sv) == 5);
    tinybuf_value_free(out); tinybuf_value_free(s); buffer_free(buf);
    tinybuf_set_use_strpool(0);
}

static void oop_serializable_fallback_tests(){
    tinybuf_set_use_strpool(1);
    tinybuf_oop_attach_serializers("xjson", xjson_read, xjson_write, xjson_dump);
    tinybuf_oop_set_serializable("xjson", 1);
    tinybuf_register_builtin_plugins();
    tinybuf_value *m = tinybuf_make_test_value(); buffer *buf = buffer_alloc(); buffer *text = buffer_alloc();
    int w = tinybuf_try_write_custom_id_box(buf, "xjson", m); assert(w > 0);
    tinybuf_dump_buffer_as_text(buffer_get_data(buf), buffer_get_length(buf), text);
    LOGI("\r\n%s", buffer_get_data(text));
    tinybuf_value *out = tinybuf_value_alloc(); buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    int r = tinybuf_try_read_box(&br, out, any_version); assert(r > 0);
    assert(tinybuf_value_get_type(out) == tinybuf_map);
    tinybuf_value_free(out); tinybuf_value_free(m); buffer_free(text); buffer_free(buf);
    tinybuf_set_use_strpool(0);
}

static void custom_vs_oop_override_tests(){
    tinybuf_set_use_strpool(1);
    tinybuf_oop_attach_serializers("mytype", xjson_read, xjson_write, xjson_dump);
    tinybuf_oop_set_serializable("mytype", 1);
    tinybuf_custom_register("mytype", custstr_read, custstr_write, custstr_dump);
    tinybuf_value *s = tinybuf_value_alloc(); tinybuf_value_init_string(s, "override", 8);
    buffer *buf = buffer_alloc(); int w = tinybuf_try_write_custom_id_box(buf, "mytype", s); assert(w > 0);
    tinybuf_value *out = tinybuf_value_alloc(); buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    int r = tinybuf_try_read_box(&br, out, any_version); assert(r > 0);
    assert(tinybuf_value_get_type(out) == tinybuf_string);
    buffer *sv = tinybuf_value_get_string(out); assert(sv && buffer_get_length(sv) == 8);
    tinybuf_value_free(out); tinybuf_value_free(s); buffer_free(buf);
    tinybuf_set_use_strpool(0);
}

static void strpool_efficiency_tests(){
    tinybuf_set_use_strpool(1);
    tinybuf_value *val = tinybuf_make_test_value();
    buffer *b1 = buffer_alloc(); buffer *b2 = buffer_alloc();
    int w1 = tinybuf_try_write_custom_id_box(b1, "xjson", val); assert(w1 > 0);
    int w2 = tinybuf_try_write_custom_id_box(b2, "xjson", val); assert(w2 > 0);
    int len1 = buffer_get_length(b1); int len2 = buffer_get_length(b2);
    assert(len1 > 0 && len2 > 0);
    tinybuf_value_free(val); buffer_free(b1); buffer_free(b2);
    tinybuf_set_use_strpool(0);
}

static void strpool_basic_tests(){
    tinybuf_set_use_strpool(1);
    tinybuf_value *v = tinybuf_value_alloc();
    tinybuf_value *m = tinybuf_value_alloc();
    tinybuf_value_init_string(v, "hello world string", (int)strlen("hello world string"));
    tinybuf_value_map_set(m, "k1", tinybuf_value_clone(v));
    tinybuf_value_map_set(m, "k2", tinybuf_value_clone(v));
    buffer *buf = buffer_alloc();
    tinybuf_try_write_box(buf, m);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    int r = tinybuf_try_read_box(&br, out, any_version);
    assert(r > 0);
    const tinybuf_value *c1 = tinybuf_value_get_map_child(out, "k1");
    const tinybuf_value *c2 = tinybuf_value_get_map_child(out, "k2");
    assert(c1 && c2);
    assert(tinybuf_value_get_type(c1) == tinybuf_string);
    assert(tinybuf_value_get_type(c2) == tinybuf_string);
    buffer *s1 = tinybuf_value_get_string((tinybuf_value*)c1);
    buffer *s2 = tinybuf_value_get_string((tinybuf_value*)c2);
    assert(buffer_get_length(s1) == buffer_get_length(s2));
    assert(memcmp(buffer_get_data(s1), buffer_get_data(s2), buffer_get_length(s1))==0);
    tinybuf_value_free(out);
    buffer_free(buf);
    tinybuf_value_free(m);
    tinybuf_value_free(v);
}

static void strpool_perf_tests(){
    tinybuf_set_use_strpool(0);
    {
        LOGI("\r\nperf: small strings naive");
        TimePrinter tp("[perf small naive] ");
        for(int i=0;i<MAX_COUNT;i++){
            tinybuf_value *m = tinybuf_value_alloc();
            {
                tinybuf_value *s = tinybuf_value_alloc(); tinybuf_value_init_string(s, "x", 1);
                tinybuf_value_map_set(m, "a", s);
            }
            {
                tinybuf_value *s = tinybuf_value_alloc(); tinybuf_value_init_string(s, "x", 1);
                tinybuf_value_map_set(m, "b", s);
            }
            {
                tinybuf_value *s = tinybuf_value_alloc(); tinybuf_value_init_string(s, "x", 1);
                tinybuf_value_map_set(m, "c", s);
            }
            buffer *b = buffer_alloc();
            tinybuf_try_write_box(b, m);
            tinybuf_value *out = tinybuf_value_alloc();
            buf_ref br{buffer_get_data(b), (int64_t)buffer_get_length(b), buffer_get_data(b), (int64_t)buffer_get_length(b)};
            tinybuf_try_read_box(&br, out, any_version);
            tinybuf_value_free(out);
            buffer_free(b);
            tinybuf_value_free(m);
        }
    }
    tinybuf_set_use_strpool(1);
    {
        LOGI("\r\nperf: small strings strpool");
        TimePrinter tp("[perf small strpool] ");
        for(int i=0;i<MAX_COUNT;i++){
            tinybuf_value *m = tinybuf_value_alloc();
            {
                tinybuf_value *s = tinybuf_value_alloc(); tinybuf_value_init_string(s, "x", 1);
                tinybuf_value_map_set(m, "a", s);
            }
            {
                tinybuf_value *s = tinybuf_value_alloc(); tinybuf_value_init_string(s, "x", 1);
                tinybuf_value_map_set(m, "b", s);
            }
            {
                tinybuf_value *s = tinybuf_value_alloc(); tinybuf_value_init_string(s, "x", 1);
                tinybuf_value_map_set(m, "c", s);
            }
            buffer *b = buffer_alloc();
            tinybuf_try_write_box(b, m);
            tinybuf_value *out = tinybuf_value_alloc();
            buf_ref br{buffer_get_data(b), (int64_t)buffer_get_length(b), buffer_get_data(b), (int64_t)buffer_get_length(b)};
            tinybuf_try_read_box(&br, out, any_version);
            tinybuf_value_free(out);
            buffer_free(b);
            tinybuf_value_free(m);
        }
        // RAII prints on destructor
    }

    // large object with many repeated strings
    tinybuf_set_use_strpool(0);
    {
        LOGI("\r\nperf: large strings naive");
        TimePrinter tp("[perf large naive] ");
        for(int i=0;i<MAX_COUNT;i++){
            tinybuf_value *arr = tinybuf_value_alloc_with_type(tinybuf_array);
            for(int k=0;k<1000;k++){ tinybuf_value *s = tinybuf_value_alloc(); tinybuf_value_init_string(s, "long-long-long-string", 22); tinybuf_value_array_append(arr, s); }
            buffer *b = buffer_alloc(); tinybuf_try_write_box(b, arr);
            tinybuf_value *out = tinybuf_value_alloc();
            buf_ref br{buffer_get_data(b), (int64_t)buffer_get_length(b), buffer_get_data(b), (int64_t)buffer_get_length(b)};
            tinybuf_try_read_box(&br, out, any_version);
            tinybuf_value_free(out); buffer_free(b); tinybuf_value_free(arr);
        }
        
    }
    tinybuf_set_use_strpool(1);
    {
        LOGI("\r\nperf: large strings strpool");
        TimePrinter tp("[perf large strpool] ");
        for(int i=0;i<MAX_COUNT;i++){
            tinybuf_value *arr = tinybuf_value_alloc_with_type(tinybuf_array);
            for(int k=0;k<1000;k++){ tinybuf_value *s = tinybuf_value_alloc(); tinybuf_value_init_string(s, "long-long-long-string", 22); tinybuf_value_array_append(arr, s); }
            buffer *b = buffer_alloc(); tinybuf_try_write_box(b, arr);
            tinybuf_value *out = tinybuf_value_alloc();
            buf_ref br{buffer_get_data(b), (int64_t)buffer_get_length(b), buffer_get_data(b), (int64_t)buffer_get_length(b)};
            tinybuf_try_read_box(&br, out, any_version);
            tinybuf_value_free(out); buffer_free(b); tinybuf_value_free(arr);
        }
        
    }
}

static void plugin_basic_tests(){
    LOGI("\r\nplugin_basic_tests");
    tinybuf_plugin_unregister_all();
    tinybuf_register_builtin_plugins();
    buffer *b = buffer_alloc();
    tinybuf_try_write_array_header(b, 2);
    tinybuf_value *s = tinybuf_value_alloc(); tinybuf_value_init_string(s, "hello", 5);
    tinybuf_plugins_try_write(200, s, b);
    tinybuf_try_write_string_raw(b, "world", 5);
    tinybuf_value_free(s);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br{buffer_get_data(b), (int64_t)buffer_get_length(b), buffer_get_data(b), (int64_t)buffer_get_length(b)};
    int r = tinybuf_try_read_box_with_plugins(&br, out, any_version);
    assert(r > 0);
    assert(tinybuf_value_get_type(out) == tinybuf_array);
    const tinybuf_value *c0 = tinybuf_value_get_array_child(out, 0);
    const tinybuf_value *c1 = tinybuf_value_get_array_child(out, 1);
    assert(tinybuf_value_get_type(c0) == tinybuf_string);
    assert(tinybuf_value_get_type(c1) == tinybuf_string);
    buffer *bs0 = tinybuf_value_get_string((tinybuf_value*)c0);
    buffer *bs1 = tinybuf_value_get_string((tinybuf_value*)c1);
    assert(memcmp(buffer_get_data(bs0), "HELLO", 5) == 0);
    assert(memcmp(buffer_get_data(bs1), "world", 5) == 0);
    tinybuf_value_free(out);
    buffer_free(b);
    LOGI("plugin_basic_tests done");
}

static void partition_basic_tests(){
    LOGI("\r\npartition_basic_tests");
    tinybuf_value *mainv = tinybuf_value_alloc(); tinybuf_value_init_string(mainv, "hello", 5);
    tinybuf_value *s1 = tinybuf_value_alloc(); tinybuf_value_init_int(s1, 1);
    tinybuf_value *s2 = tinybuf_value_alloc(); tinybuf_value_init_int(s2, 2);
    tinybuf_value *s3 = tinybuf_value_alloc(); tinybuf_value_init_int(s3, 3);
    const tinybuf_value *subs[3] = { s1, s2, s3 };
    buffer *b = buffer_alloc();
    {
        buffer *single = buffer_alloc();
        tinybuf_try_write_part(single, mainv);
        {
            buffer *text = buffer_alloc();
            tinybuf_dump_buffer_as_text(buffer_get_data(single), buffer_get_length(single), text);
            LOGI("\r\n%s", buffer_get_data(text));
            buffer_free(text);
        }
        tinybuf_value *out1 = tinybuf_value_alloc();
        buf_ref br1{buffer_get_data(single), (int64_t)buffer_get_length(single), buffer_get_data(single), (int64_t)buffer_get_length(single)};
        int r1 = tinybuf_try_read_box(&br1, out1, any_version);
        assert(r1 > 0);
        assert(tinybuf_value_is_same(mainv, out1));
        tinybuf_value_free(out1);
        buffer_free(single);
        LOGI("partition_single_part ok");
    }
    tinybuf_try_write_partitions(b, mainv, subs, 3);
    {
        buffer *text = buffer_alloc();
        tinybuf_dump_buffer_as_text(buffer_get_data(b), buffer_get_length(b), text);
        LOGI("\r\n%s", buffer_get_data(text));
        buffer_free(text);
    }
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br{buffer_get_data(b), (int64_t)buffer_get_length(b), buffer_get_data(b), (int64_t)buffer_get_length(b)};
    int r = tinybuf_try_read_box(&br, out, any_version);
    assert(r > 0);
    assert(tinybuf_value_is_same(mainv, out));
    tinybuf_value_free(out);
    buffer_free(b);
    tinybuf_value_free(mainv);
    tinybuf_value_free(s1);
    tinybuf_value_free(s2);
    tinybuf_value_free(s3);
    LOGI("partition_basic_tests done");
}

static int varint_deserialize_local(const uint8_t *in, int in_size, uint64_t *out){ if(in_size<1) return 0; *out=0; int index=0; while(1){ uint8_t byte=in[index]; (*out) |= (uint64_t)(byte & 0x7F) << ((index++) * 7); if((byte & 0x80)==0) break; if(index>=in_size) return 0; if(index*7>56) return -1; } return index; }
static int varint_serialize_local(uint64_t in, uint8_t *out){ int index=0; for(int i=0;i<= (8*sizeof(in))/7; ++i, ++index){ out[index] = (uint8_t)(in & 0x7F); in >>= 7; if(!in){ break; } out[index] |= 0x80; } return ++index; }

static void partition_concurrent_read_tests(){
    LOGI("\r\npartition_concurrent_read_tests");
    tinybuf_value *mainv = tinybuf_value_alloc(); tinybuf_value_init_string(mainv, "hello", 5);
    tinybuf_value *s1 = tinybuf_value_alloc(); tinybuf_value_init_int(s1, 1);
    tinybuf_value *s2 = tinybuf_value_alloc(); tinybuf_value_init_int(s2, 2);
    tinybuf_value *s3 = tinybuf_value_alloc(); tinybuf_value_init_int(s3, 3);
    const tinybuf_value *subs[3] = { s1, s2, s3 };
    buffer *b = buffer_alloc();
    tinybuf_try_write_partitions(b, mainv, subs, 3);
    const uint8_t *base = (const uint8_t*)buffer_get_data(b);
    int64_t all = (int64_t)buffer_get_length(b);
    int pos = 0; assert(base[0] == 22); pos++;
    uint64_t cnt = 0; int c = varint_deserialize_local(base+pos, (int)(all-pos), &cnt); pos += c; assert(cnt>=1);
    std::vector<uint64_t> offs(cnt);
    for(uint64_t i=0;i<cnt;++i){ uint64_t off=0; int k = varint_deserialize_local(base+pos, (int)(all-pos), &off); pos += k; offs[(size_t)i] = off; }
    std::vector<tinybuf_value*> outs(cnt-1);
    std::vector<std::thread> th;
    for(size_t i=1;i<offs.size();++i){
        outs[i-1] = tinybuf_value_alloc();
        th.emplace_back([&,i]{
            buf_ref br{(const char*)base, all, (const char*)base + (int64_t)offs[i], all - (int64_t)offs[i]};
            int r = tinybuf_try_read_box(&br, outs[i-1], any_version);
            assert(r>0);
        });
    }
    for(auto &t: th) t.join();
    tinybuf_value *out = tinybuf_value_alloc();
    {
        buf_ref br{(const char*)base, all, (const char*)base, all};
        int r = tinybuf_try_read_box(&br, out, any_version);
        assert(r>0);
    }
    tinybuf_value_free(out);
    for(auto *p: outs) tinybuf_value_free(p);
    buffer_free(b);
    tinybuf_value_free(mainv);
    tinybuf_value_free(s1);
    tinybuf_value_free(s2);
    tinybuf_value_free(s3);
    LOGI("partition_concurrent_read_tests done");
}

static void plugin_dll_tests(){
    LOGI("\r\nplugin_dll_tests");
    tinybuf_plugin_unregister_all();
    tinybuf_register_builtin_plugins();
#ifdef _WIN32
    char path[256]; snprintf(path, sizeof(path), "%s", "build\\lib\\Release\\upper_plugin.dll");
    tinybuf_plugin_register_from_dll(path);
#endif
    buffer *b = buffer_alloc();
    tinybuf_try_write_array_header(b, 3);
    tinybuf_value *s = tinybuf_value_alloc(); tinybuf_value_init_string(s, "hello", 5);
    tinybuf_plugins_try_write(201, s, b);
    tinybuf_try_write_string_raw(b, "world", 5);
    tinybuf_value *sv = tinybuf_value_alloc(); tinybuf_value_init_string(sv, "AbCd", 4);
    tinybuf_plugin_do_value_op_by_type(201, "to_lower", sv, NULL, sv);
    tinybuf_try_write_box(b, sv);
    tinybuf_value_free(s);
    tinybuf_value_free(sv);
    buffer *text = buffer_alloc(); tinybuf_dump_buffer_as_text(buffer_get_data(b), buffer_get_length(b), text); LOGI("\r\n%s", buffer_get_data(text)); buffer_free(text);
    tinybuf_value *out = tinybuf_value_alloc(); buf_ref br{buffer_get_data(b), (int64_t)buffer_get_length(b), buffer_get_data(b), (int64_t)buffer_get_length(b)}; int r = tinybuf_try_read_box_with_plugins(&br, out, any_version); assert(r>0);
    const tinybuf_value *c0 = tinybuf_value_get_array_child(out, 0); const tinybuf_value *c1 = tinybuf_value_get_array_child(out, 1); const tinybuf_value *c2 = tinybuf_value_get_array_child(out, 2);
    buffer *bs0 = tinybuf_value_get_string((tinybuf_value*)c0); buffer *bs1 = tinybuf_value_get_string((tinybuf_value*)c1); buffer *bs2 = tinybuf_value_get_string((tinybuf_value*)c2);
    assert(memcmp(buffer_get_data(bs0), "HELLO", 5) == 0);
    assert(memcmp(buffer_get_data(bs1), "world", 5) == 0);
    assert(memcmp(buffer_get_data(bs2), "abcd", 4) == 0);
    tinybuf_value_free(out); buffer_free(b);
    LOGI("plugin_dll_tests done");
}

static void plugin_custom_box_tests(){
    LOGI("\r\nplugin_custom_box_tests");
    tinybuf_plugin_unregister_all();
    tinybuf_register_builtin_plugins();
#ifdef _WIN32
    char path[256]; snprintf(path, sizeof(path), "%s", "build\\lib\\Release\\upper_plugin.dll");
    tinybuf_plugin_register_from_dll(path);
#endif
    tinybuf_value *v = tinybuf_value_alloc(); tinybuf_value_init_string(v, "hello", 5);
    tinybuf_value_set_custom_box_type(v, 201);
    buffer *b = buffer_alloc(); tinybuf_try_write_box(b, v);
    buf_ref br{buffer_get_data(b), (int64_t)buffer_get_length(b), buffer_get_data(b), (int64_t)buffer_get_length(b)};
    tinybuf_value *out = tinybuf_value_alloc(); int r = tinybuf_try_read_box_with_plugins(&br, out, any_version); assert(r>0);
    buffer *bs = tinybuf_value_get_string(out); assert(buffer_get_length(bs)==5); assert(memcmp(buffer_get_data(bs), "HELLO", 5)==0);
    tinybuf_value_free(out); tinybuf_value_free(v); buffer_free(b);
    LOGI("plugin_custom_box_tests done");
}

static void tensor_tests(){
    LOGI("\r\ntensor_tests");
    int64_t shape1[1] = {3};
    int64_t data1[3] = {1,2,3};
    tinybuf_value *v1 = tinybuf_value_alloc();
    tinybuf_value_init_tensor(v1, 1, shape1, 1, data1, 3);
    buffer *b1 = buffer_alloc();
    tinybuf_try_write_box(b1, v1);
    buf_ref br1{buffer_get_data(b1), (int64_t)buffer_get_length(b1), buffer_get_data(b1), (int64_t)buffer_get_length(b1)};
    tinybuf_value *o1 = tinybuf_value_alloc();
    int r1 = tinybuf_try_read_box(&br1, o1, any_version);
    assert(r1>0);
    assert(tinybuf_value_get_type(o1) == tinybuf_tensor);
    assert(tinybuf_tensor_get_ndim(o1)==1);
    assert(tinybuf_tensor_get_count(o1)==3);
    const int64_t *sh1 = tinybuf_tensor_get_shape(o1);
    assert(sh1 && sh1[0]==3);
    int64_t *d1 = (int64_t*)tinybuf_tensor_get_data(o1);
    assert(d1 && d1[0]==1 && d1[1]==2 && d1[2]==3);
    tinybuf_value_free(o1);
    tinybuf_value_free(v1);
    buffer_free(b1);

    int64_t shape2[2] = {2,2};
    double data2[4] = {1.25, -2.5, 0.0, 3.0};
    tinybuf_value *v2 = tinybuf_value_alloc();
    tinybuf_value_init_tensor(v2, 8, shape2, 2, data2, 4);
    buffer *b2 = buffer_alloc();
    tinybuf_try_write_box(b2, v2);
    buf_ref br2{buffer_get_data(b2), (int64_t)buffer_get_length(b2), buffer_get_data(b2), (int64_t)buffer_get_length(b2)};
    tinybuf_value *o2 = tinybuf_value_alloc();
    int r2 = tinybuf_try_read_box(&br2, o2, any_version);
    assert(r2>0);
    assert(tinybuf_value_get_type(o2) == tinybuf_tensor);
    assert(tinybuf_tensor_get_ndim(o2)==2);
    assert(tinybuf_tensor_get_count(o2)==4);
    const int64_t *sh2 = tinybuf_tensor_get_shape(o2);
    assert(sh2 && sh2[0]==2 && sh2[1]==2);
    const double *d2 = (const double*)tinybuf_tensor_get_data_const(o2);
    assert(d2 && d2[0]==1.25 && d2[1]==-2.5 && d2[2]==0.0 && d2[3]==3.0);
    tinybuf_value_free(o2);
    tinybuf_value_free(v2);
    buffer_free(b2);
    LOGI("tensor_tests done");
}

static uint32_t be32(const uint8_t *p){ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|((uint32_t)p[3]); }
static double read_double_be_local(const uint8_t *ptr){ uint64_t val = ((uint64_t)be32(ptr) << 32) | be32(ptr + 4); double db = 0; memcpy(&db, &val, 8); return db; }

static void tensor_storage_tests(){
    // vector<bool> storage: header uses varints; payload packed as bitmap (MSB-first)
    {
        int64_t shape[1] = {4};
        uint8_t data[4] = {1,0,1,1};
        tinybuf_value *v = tinybuf_value_alloc();
        tinybuf_value_init_tensor(v, 11, shape, 1, data, 4);
        buffer *b = buffer_alloc();
        tinybuf_try_write_box(b, v);
        const uint8_t *ptr = (const uint8_t*)buffer_get_data(b);
        int len = buffer_get_length(b);
        assert(len > 0 && ptr[0] == 43);
        int pos = 1; uint64_t cnt=0, dt=0; int a = varint_deserialize_local(ptr+pos, len-pos, &cnt); assert(a>0); pos+=a; int bsz = varint_deserialize_local(ptr+pos, len-pos, &dt); assert(bsz>0); pos+=bsz; assert(cnt==4 && dt==11);
        int need = (int)((cnt + 7) / 8); assert(len - pos >= need);
        uint8_t one = ptr[pos];
        for(int i=0;i<4;++i){ int bit = 7 - (i % 8); uint8_t vv = (one >> bit) & 1; assert(vv == data[i]); }
        pos += need;
        tinybuf_value *out = tinybuf_value_alloc(); buf_ref br{buffer_get_data(b), (int64_t)buffer_get_length(b), buffer_get_data(b), (int64_t)buffer_get_length(b)}; int r = tinybuf_try_read_box(&br, out, any_version); assert(r>0); assert(tinybuf_value_get_type(out)==tinybuf_tensor); assert(tinybuf_tensor_get_count(out)==4);
        const uint8_t *rd = (const uint8_t*)tinybuf_tensor_get_data_const(out); assert(rd && rd[0]==1 && rd[1]==0 && rd[2]==1 && rd[3]==1);
        tinybuf_value_free(out); buffer_free(b); tinybuf_value_free(v);
    }
    // large vector<bool> storage: verify packed length and round-trip
    {
        const int N = 1000; std::vector<uint8_t> data(N);
        for(int i=0;i<N;++i) data[i] = (uint8_t)((i*37) % 2);
        int64_t shape[1] = {N}; tinybuf_value *v = tinybuf_value_alloc();
        tinybuf_value_init_tensor(v, 11, shape, 1, data.data(), N);
        buffer *b = buffer_alloc(); tinybuf_try_write_box(b, v);
        const uint8_t *ptr = (const uint8_t*)buffer_get_data(b); int len = buffer_get_length(b);
        assert(ptr[0]==43); int pos=1; uint64_t cnt=0, dt=0; int a = varint_deserialize_local(ptr+pos, len-pos, &cnt); assert(a>0 && cnt==N); pos+=a; int bsz = varint_deserialize_local(ptr+pos, len-pos, &dt); assert(bsz>0 && dt==11); pos+=bsz;
        int need = (int)((cnt + 7) / 8); assert(len - pos >= need);
        tinybuf_value *out = tinybuf_value_alloc(); buf_ref br{buffer_get_data(b), (int64_t)buffer_get_length(b), buffer_get_data(b), (int64_t)buffer_get_length(b)}; int r = tinybuf_try_read_box(&br, out, any_version); assert(r>0); assert(tinybuf_value_get_type(out)==tinybuf_tensor); assert(tinybuf_tensor_get_count(out)==N);
        const uint8_t *rd = (const uint8_t*)tinybuf_tensor_get_data_const(out); for(int i=0;i<N;++i) assert(rd[i]==data[i]);
        tinybuf_value_free(out); buffer_free(b); tinybuf_value_free(v);
    }
    // dense<double> storage: header varints then raw 8*count bytes
    {
        int64_t shape[2] = {2,2};
        double data[4] = {1.25, -2.5, 0.0, 3.0};
        tinybuf_value *v = tinybuf_value_alloc(); tinybuf_value_init_tensor(v, 8, shape, 2, data, 4);
        buffer *b = buffer_alloc(); tinybuf_try_write_box(b, v);
        const uint8_t *ptr = (const uint8_t*)buffer_get_data(b); int len = buffer_get_length(b);
        assert(len >= 37 && ptr[0] == 44); int pos=1; uint64_t dims=0; int a = varint_deserialize_local(ptr+pos, len-pos, &dims); assert(a>0 && dims==2); pos+=a; uint64_t s0=0,s1=0; int c0 = varint_deserialize_local(ptr+pos, len-pos, &s0); assert(c0>0 && s0==2); pos+=c0; int c1 = varint_deserialize_local(ptr+pos, len-pos, &s1); assert(c1>0 && s1==2); pos+=c1; uint64_t dt=0; int bsz = varint_deserialize_local(ptr+pos, len-pos, &dt); assert(bsz>0 && dt==8); pos+=bsz; assert(len-pos >= 32);
        double r0 = read_double_be_local(ptr+pos+0); double r1 = read_double_be_local(ptr+pos+8); double r2 = read_double_be_local(ptr+pos+16); double r3 = read_double_be_local(ptr+pos+24);
        assert(r0==data[0] && r1==data[1] && r2==data[2] && r3==data[3]);
        tinybuf_value *out = tinybuf_value_alloc(); buf_ref br{buffer_get_data(b), (int64_t)buffer_get_length(b), buffer_get_data(b), (int64_t)buffer_get_length(b)}; int r = tinybuf_try_read_box(&br, out, any_version); assert(r>0); assert(tinybuf_value_get_type(out)==tinybuf_tensor); assert(tinybuf_tensor_get_ndim(out)==2); assert(tinybuf_tensor_get_count(out)==4);
        tinybuf_value_free(out); buffer_free(b); tinybuf_value_free(v);
    }
    LOGI("tensor_storage_tests done");
}

static void bool_map_tests(){
    {
        const int N = 20; std::vector<uint8_t> bits((N+7)/8, 0);
        auto setbit=[&](int i,int v){ int b=i/8, bit=7-(i%8); if(v) bits[b] |= (uint8_t)(1<<bit); else bits[b] &= (uint8_t)~(1<<bit); };
        for(int i=0;i<N;++i) setbit(i, (i%3)==0);
        tinybuf_value *v = tinybuf_value_alloc(); tinybuf_value_init_bool_map(v, bits.data(), N);
        buffer *b = buffer_alloc(); tinybuf_try_write_box(b, v);
        const uint8_t *ptr = (const uint8_t*)buffer_get_data(b); int len = buffer_get_length(b);
        assert(ptr[0] == 46); int pos=1; uint64_t cnt=0; int a = varint_deserialize_local(ptr+pos, len-pos, &cnt); assert(a>0 && (int)cnt==N); pos+=a; int need = (int)((cnt+7)/8); assert(len-pos >= need);
        tinybuf_value *out = tinybuf_value_alloc(); buf_ref br{buffer_get_data(b), (int64_t)buffer_get_length(b), buffer_get_data(b), (int64_t)buffer_get_length(b)}; int r = tinybuf_try_read_box(&br, out, any_version); assert(r>0); assert(tinybuf_value_get_type(out)==tinybuf_bool_map); assert(tinybuf_bool_map_get_count(out)==N);
        const uint8_t *rb = tinybuf_bool_map_get_bits_const(out); for(int i=0;i<N;++i){ int b2=i/8, bit=7-(i%8); int v2 = (rb[b2] >> bit) & 1; int v1 = (bits[b2] >> bit) & 1; assert(v2 == v1); }
        tinybuf_value_free(out); buffer_free(b); tinybuf_value_free(v);
    }
    LOGI("bool_map_tests done");
}

static void hetero_tuple_tests(){
    tinybuf_set_use_strpool(1);
    tinybuf_register_builtin_plugins();
    tinybuf_value *arr = tinybuf_value_alloc();
    tinybuf_value *i = tinybuf_value_alloc(); tinybuf_value_init_int(i, 123);
    tinybuf_value *s = tinybuf_value_alloc(); tinybuf_value_init_string(s, "abc", 3);
    tinybuf_value *b = tinybuf_value_alloc(); tinybuf_value_init_bool(b, 1);
    tinybuf_value_array_append(arr, i); tinybuf_value_array_append(arr, s); tinybuf_value_array_append(arr, b);
    buffer *buf = buffer_alloc(); int w = tinybuf_try_write_custom_id_box(buf, "hetero_tuple", arr); assert(w>0);
    tinybuf_value *out = tinybuf_value_alloc(); buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)}; int r = tinybuf_try_read_box(&br, out, any_version); assert(r>0);
    assert(tinybuf_value_get_type(out)==tinybuf_array);
    assert(tinybuf_value_get_child_size(out)==3);
    const tinybuf_value *c0=tinybuf_value_get_array_child(out,0); const tinybuf_value *c1=tinybuf_value_get_array_child(out,1); const tinybuf_value *c2=tinybuf_value_get_array_child(out,2);
    assert(tinybuf_value_get_type(c0)==tinybuf_int && tinybuf_value_get_int(c0)==123);
    assert(tinybuf_value_get_type(c1)==tinybuf_string && buffer_get_length(tinybuf_value_get_string((tinybuf_value*)c1))==3);
    assert(tinybuf_value_get_type(c2)==tinybuf_bool && tinybuf_value_get_bool(c2)==1);
    tinybuf_value_free(out); buffer_free(buf); tinybuf_value_free(arr);
    tinybuf_set_use_strpool(0);
}

static void hetero_list_tests(){
    tinybuf_set_use_strpool(1);
    tinybuf_register_builtin_plugins();
    tinybuf_value *arr = tinybuf_value_alloc();
    for(int k=0;k<5;++k){ tinybuf_value *i = tinybuf_value_alloc(); tinybuf_value_init_int(i, k); tinybuf_value_array_append(arr, i);} tinybuf_value *s = tinybuf_value_alloc(); tinybuf_value_init_string(s, "z", 1); tinybuf_value_array_append(arr, s);
    buffer *buf = buffer_alloc(); int w = tinybuf_try_write_custom_id_box(buf, "hetero_list", arr); assert(w>0);
    tinybuf_value *out = tinybuf_value_alloc(); buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)}; int r = tinybuf_try_read_box(&br, out, any_version); assert(r>0);
    assert(tinybuf_value_get_type(out)==tinybuf_array);
    assert(tinybuf_value_get_child_size(out)==6);
    const tinybuf_value *last=tinybuf_value_get_array_child(out,5); assert(tinybuf_value_get_type(last)==tinybuf_string);
    tinybuf_value_free(out); buffer_free(buf); tinybuf_value_free(arr);
    tinybuf_set_use_strpool(0);
}

static void dataframe_extend_tests(){
    tinybuf_set_use_strpool(1);
    tinybuf_register_builtin_plugins();
    // build indexed tensor 2x2 with indices
    int64_t shape[2] = {2,2}; double data[4] = {1.0,2.0,3.0,4.0}; tinybuf_value *ten = tinybuf_value_alloc(); tinybuf_value_init_tensor(ten, 8, shape, 2, data, 4);
    tinybuf_value *rows = tinybuf_value_alloc(); tinybuf_value *r0 = tinybuf_value_alloc(); tinybuf_value_init_string(r0, "r0", 2); tinybuf_value *r1 = tinybuf_value_alloc(); tinybuf_value_init_string(r1, "r1", 2); tinybuf_value_array_append(rows, r0); tinybuf_value_array_append(rows, r1);
    tinybuf_value *cols = tinybuf_value_alloc(); tinybuf_value *c0 = tinybuf_value_alloc(); tinybuf_value_init_string(c0, "c0", 2); tinybuf_value *c1 = tinybuf_value_alloc(); tinybuf_value_init_string(c1, "c1", 2); tinybuf_value_array_append(cols, c0); tinybuf_value_array_append(cols, c1);
    const tinybuf_value *idxs[2] = { rows, cols };
    tinybuf_value *df = tinybuf_value_alloc(); tinybuf_value_init_indexed_tensor(df, ten, idxs, 2);
    buffer *buf = buffer_alloc(); int w = tinybuf_try_write_custom_id_box(buf, "dataframe", df); assert(w>0);
    tinybuf_value *out = tinybuf_value_alloc(); buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)}; int r = tinybuf_try_read_box(&br, out, any_version); assert(r>0);
    assert(tinybuf_value_get_type(out)==tinybuf_indexed_tensor);
    assert(tinybuf_tensor_get_ndim(out)==2 && tinybuf_tensor_get_count(out)==4);
    tinybuf_value_free(out); buffer_free(buf); tinybuf_value_free(df); tinybuf_value_free(cols); tinybuf_value_free(rows); tinybuf_value_free(ten);
    tinybuf_set_use_strpool(0);
}

static void oop_infra_tests(){
    Counter c = Counter_make();
    const int N = 8, R = 10000;
    std::vector<std::thread> th; th.reserve(N);
    for(int i=0;i<N;++i){ th.emplace_back([&]{ for(int k=0;k<R;++k) Counter_add(&c, 1); }); }
    for(auto &t: th) t.join();
    assert(c.v == N*R);
    LOGI("oop_infra_tests done");
}

static void indexed_tensor_tests(){
    // base dense<double> 2x2 with row/col string indices
    int64_t shape[2] = {2,2}; double data[4] = {1.25, -2.5, 0.0, 3.0};
    tinybuf_value *ten = tinybuf_value_alloc(); tinybuf_value_init_tensor(ten, 8, shape, 2, data, 4);
    tinybuf_value *rows = tinybuf_value_alloc(); tinybuf_value *r0 = tinybuf_value_alloc(); tinybuf_value_init_string(r0, "r0", 2); tinybuf_value *r1 = tinybuf_value_alloc(); tinybuf_value_init_string(r1, "r1", 2); tinybuf_value_array_append(rows, r0); tinybuf_value_array_append(rows, r1);
    tinybuf_value *cols = tinybuf_value_alloc(); tinybuf_value *c0 = tinybuf_value_alloc(); tinybuf_value_init_string(c0, "c0", 2); tinybuf_value *c1 = tinybuf_value_alloc(); tinybuf_value_init_string(c1, "c1", 2); tinybuf_value_array_append(cols, c0); tinybuf_value_array_append(cols, c1);
    const tinybuf_value *idxs[2] = { rows, cols };
    tinybuf_value *boxed = tinybuf_value_alloc(); tinybuf_value_init_indexed_tensor(boxed, ten, idxs, 2);
    buffer *b = buffer_alloc(); tinybuf_try_write_box(b, boxed);
    // header check
    const uint8_t *ptr = (const uint8_t*)buffer_get_data(b); int len = buffer_get_length(b); assert(len>0 && ptr[0]==47);
    // decode
    tinybuf_value *out = tinybuf_value_alloc(); buf_ref br{buffer_get_data(b), (int64_t)buffer_get_length(b), buffer_get_data(b), (int64_t)buffer_get_length(b)}; int r = tinybuf_try_read_box(&br, out, any_version); assert(r>0); assert(tinybuf_value_get_type(out)==tinybuf_indexed_tensor);
    // free
    tinybuf_value_free(out); buffer_free(b); tinybuf_value_free(boxed); tinybuf_value_free(cols); tinybuf_value_free(rows); tinybuf_value_free(ten);
    LOGI("indexed_tensor_tests done");
}

static void partition_concurrent_write_tests(){
    LOGI("\r\npartition_concurrent_write_tests");
    tinybuf_value *mainv = tinybuf_value_alloc(); tinybuf_value_init_string(mainv, "hello", 5);
    tinybuf_value *s1 = tinybuf_value_alloc(); tinybuf_value_init_int(s1, 1);
    tinybuf_value *s2 = tinybuf_value_alloc(); tinybuf_value_init_int(s2, 2);
    tinybuf_value *s3 = tinybuf_value_alloc(); tinybuf_value_init_int(s3, 3);
    const tinybuf_value *subs[3] = { s1, s2, s3 };
    std::vector<buffer*> parts(4);
    for(size_t i=0;i<parts.size();++i) parts[i] = buffer_alloc();
    {
        std::thread t0([&]{ tinybuf_try_write_part(parts[0], mainv); });
        std::thread t1([&]{ tinybuf_try_write_part(parts[1], subs[0]); });
        std::thread t2([&]{ tinybuf_try_write_part(parts[2], subs[1]); });
        std::thread t3([&]{ tinybuf_try_write_part(parts[3], subs[2]); });
        t0.join(); t1.join(); t2.join(); t3.join();
    }
    std::vector<int> lens(4);
    for(size_t i=0;i<parts.size();++i) lens[i] = buffer_get_length(parts[i]);
    std::vector<uint64_t> offs(4);
    std::vector<uint64_t> vlen(4,1);
    uint8_t tmp[32];
    while(1){
        uint64_t table_len = 1 + (uint64_t)varint_serialize_local((uint64_t)offs.size(), tmp);
        for(size_t i=0;i<offs.size();++i) table_len += vlen[i];
        offs[0] = table_len;
        for(size_t i=1;i<offs.size();++i) offs[i] = offs[i-1] + (uint64_t)lens[i-1];
        int stable = 1;
        for(size_t i=0;i<offs.size();++i){ int l = varint_serialize_local(offs[i], tmp); if((uint64_t)l != vlen[i]){ vlen[i]= (uint64_t)l; stable=0; } }
        if(stable) break;
    }
    buffer *out = buffer_alloc();
    {
        uint8_t t = 22; buffer_append(out, (const char*)&t, 1);
        uint8_t hdr[32]; int hl = varint_serialize_local((uint64_t)offs.size(), hdr);
        buffer_append(out, (const char*)hdr, hl);
        for(size_t i=0;i<offs.size();++i){ int l = varint_serialize_local(offs[i], hdr); buffer_append(out, (const char*)hdr, l); }
        for(size_t i=0;i<parts.size();++i){ buffer_append(out, buffer_get_data(parts[i]), buffer_get_length(parts[i])); }
    }
    tinybuf_value *readv = tinybuf_value_alloc();
    {
        buf_ref br{buffer_get_data(out), (int64_t)buffer_get_length(out), buffer_get_data(out), (int64_t)buffer_get_length(out)};
        int r = tinybuf_try_read_box(&br, readv, any_version);
        assert(r>0);
        tinybuf_value_free(readv);
    }
    for(auto *p: parts) buffer_free(p);
    buffer_free(out);
    tinybuf_value_free(mainv);
    tinybuf_value_free(s1);
    tinybuf_value_free(s2);
    tinybuf_value_free(s3);
    LOGI("partition_concurrent_write_tests done");
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
    precache_and_read_mode_tests();
    pointer_auto_mode_mixed_tests();
    pointer_subref_tests();
    pointer_transparent_across_modes_tests();
    custom_type_idx_string_tests();
    oop_serializable_fallback_tests();
    custom_vs_oop_override_tests();
    strpool_efficiency_tests();
    strpool_basic_tests();
    strpool_perf_tests();
    plugin_basic_tests();
    partition_basic_tests();
    partition_concurrent_read_tests();
    partition_concurrent_write_tests();
    plugin_dll_tests();
    plugin_custom_box_tests();
    tensor_tests();
    tensor_storage_tests();
    bool_map_tests();
    hetero_tuple_tests();
    hetero_list_tests();
    dataframe_extend_tests();
    oop_infra_tests();
    indexed_tensor_tests();
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
