#include <catch2/catch_test_macros.hpp>
#include "tinybuf_support.h"
#include "tinybuf_memory.h"

static int g_del_cnt = 0;
static void custom_del(void *p){ g_del_cnt++; tinybuf_free(p); }

TEST_CASE("hole_string_basic", "[hole_string]"){
    hole_string *s = hole_string_new();
    hole_string_append_cstr(s, "hello", 0, NULL);
    hole_string_append_char(s, ' ');
    hole_string_append_cstr(s, "world", 0, NULL);
    tinybuf_str r = hole_string_get(s);
    REQUIRE(r.ptr != NULL);
    REQUIRE(std::string(r.ptr) == std::string("hello world"));
    if(r.deleter) r.deleter(r.ptr);
    hole_string_free(s);
}

TEST_CASE("hole_string_nested", "[hole_string]"){
    hole_string *sub = hole_string_new();
    hole_string_append_cstr(sub, "abc", 0, NULL);
    hole_string *s = hole_string_new();
    hole_string_append_cstr(s, "123", 0, NULL);
    hole_string_append_sub(s, sub);
    hole_string_append_cstr(s, "xyz", 0, NULL);
    tinybuf_str r = hole_string_get(s);
    REQUIRE(std::string(r.ptr) == std::string("123abcxyz"));
    if(r.deleter) r.deleter(r.ptr);
    hole_string_free(s);
}

TEST_CASE("hole_string_deleter_invocation", "[hole_string]"){
    g_del_cnt = 0;
    hole_string *s = hole_string_new();
    char *a = tinybuf_strdup("a");
    char *b = tinybuf_strdup("b");
    char *c = tinybuf_strdup("c");
    hole_string_append_cstr(s, a, 1, custom_del);
    hole_string_append_cstr(s, b, 1, custom_del);
    hole_string_append_cstr(s, c, 1, custom_del);
    tinybuf_str r = hole_string_get(s);
    REQUIRE(std::string(r.ptr) == std::string("abc"));
    if(r.deleter) r.deleter(r.ptr);
    hole_string_free(s);
    REQUIRE(g_del_cnt == 3);
}

TEST_CASE("hole_string_perf_concat", "[hole_string][performance]"){
    int N = 20000;
    hole_string *s = hole_string_new();
    for(int i=0;i<N;++i){ hole_string_append_char(s, 'a'); }
    tinybuf_str r = hole_string_get(s);
    REQUIRE((int)std::string(r.ptr).size() == N);
    if(r.deleter) r.deleter(r.ptr);
    hole_string_free(s);
}

TEST_CASE("hole_string_bytes_mixed", "[hole_string]"){
    hole_string *s = hole_string_new();
    hole_string_append_cstr(s, "A", 0, NULL);
    const char b[3] = {'X','\0','Y'};
    char *owned = (char*)tinybuf_malloc(3);
    memcpy(owned, b, 3);
    hole_string_append_bytes(s, owned, 3, (void (*)(void*))tinybuf_free);
    hole_string_append_cstr(s, "Z", 0, NULL);
    tinybuf_str r = hole_string_get(s);
    std::string expect("AX\0YZ", 5);
    REQUIRE(std::string(r.ptr, 5) == expect);
    if(r.deleter) r.deleter(r.ptr);
    hole_string_free(s);
}

TEST_CASE("hole_string_numbers_and_nested", "[hole_string]"){
    hole_string *sub = hole_string_new();
    hole_string_append_i64(sub, (int64_t)-123456789);
    hole_string_append_char(sub, ',');
    hole_string_append_u64(sub, (uint64_t)9876543210ULL);

    hole_string *s = hole_string_new();
    hole_string_append_cstr(s, "X=", 0, NULL);
    hole_string_append_i32(s, (int32_t)42);
    hole_string_append_char(s, ';');
    hole_string_append_sub(s, sub);
    hole_string_append_char(s, ';');
    hole_string_append_f64(s, 123.5);
    hole_string_append_char(s, ';');
    hole_string_append_f32(s, (float)1.25f);

    tinybuf_str r = hole_string_get(s);
    std::string sr(r.ptr);
    REQUIRE(sr.find("X=42;") == 0);
    REQUIRE(sr.find("-123456789,9876543210;") != std::string::npos);
    REQUIRE(sr.find("123.5;") != std::string::npos);
    REQUIRE(sr.find("1.25") != std::string::npos);
    if(r.deleter) r.deleter(r.ptr);
    hole_string_free(s);
}
