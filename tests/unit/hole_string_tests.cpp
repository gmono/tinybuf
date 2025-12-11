#include <catch2/catch_test_macros.hpp>
#include "tinybuf_support.h"
#include "tinybuf_memory.h"
#include <chrono>
#include <cstdio>

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
    int N = 10000;
    auto t0 = std::chrono::high_resolution_clock::now();
    for(int i=0;i<N;++i){ tinybuf_str rr = hole_string_get(s); if(rr.deleter) rr.deleter(rr.ptr);} 
    auto t1 = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
    REQUIRE(ms >= 0);
    std::printf("perf: hole_string_basic get x%d %lld ms\n", N, (long long)ms);
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
    int N = 5000;
    auto t0 = std::chrono::high_resolution_clock::now();
    for(int i=0;i<N;++i){ tinybuf_str rr = hole_string_get(s); if(rr.deleter) rr.deleter(rr.ptr);} 
    auto t1 = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
    REQUIRE(ms >= 0);
    std::printf("perf: hole_string_nested get x%d %lld ms\n", N, (long long)ms);
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
    int N = 3000;
    auto t0 = std::chrono::high_resolution_clock::now();
    for(int i=0;i<N;++i){ tinybuf_str rr = hole_string_get(s); if(rr.deleter) rr.deleter(rr.ptr);} 
    auto t1 = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
    REQUIRE(ms >= 0);
    std::printf("perf: hole_string_deleter_invocation get x%d %lld ms\n", N, (long long)ms);
    hole_string_free(s);
    REQUIRE(g_del_cnt == 3);
}

TEST_CASE("hole_string_perf_concat", "[hole_string][performance]"){
    int N = 20000;
    hole_string *s = hole_string_new();
    for(int i=0;i<N;++i){ hole_string_append_char(s, 'a'); }
    auto t0 = std::chrono::high_resolution_clock::now();
    tinybuf_str r = hole_string_get(s);
    auto t1 = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
    REQUIRE((int)std::string(r.ptr).size() == N);
    REQUIRE(ms >= 0);
    std::printf("perf: hole_string_perf_concat get x1 %lld ms len=%d\n", (long long)ms, N);
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
    int N = 5000;
    auto t0 = std::chrono::high_resolution_clock::now();
    for(int i=0;i<N;++i){ tinybuf_str rr = hole_string_get(s); if(rr.deleter) rr.deleter(rr.ptr);} 
    auto t1 = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
    REQUIRE(ms >= 0);
    std::printf("perf: hole_string_bytes_mixed get x%d %lld ms\n", N, (long long)ms);
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
    int N = 5000;
    auto t0 = std::chrono::high_resolution_clock::now();
    for(int i=0;i<N;++i){ tinybuf_str rr = hole_string_get(s); if(rr.deleter) rr.deleter(rr.ptr);} 
    auto t1 = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
    REQUIRE(ms >= 0);
    std::printf("perf: hole_string_numbers_and_nested get x%d %lld ms\n", N, (long long)ms);
    hole_string_free(s);
}

TEST_CASE("error_messages_static_and_hole", "[error][hole_string]"){
    tinybuf_error r = tinybuf_result_err(-1, "static failed", NULL);
    hole_string *hs = hole_string_new();
    hole_string_append_cstr(hs, "write ", 0, NULL);
    hole_string_append_u32(hs, 123u);
    hole_string_append_cstr(hs, " bytes", 0, NULL);
    tinybuf_result_add_hole_msg(&r, hs);
    char msgs[256];
    int n = tinybuf_result_format_msgs(&r, msgs, sizeof(msgs));
    REQUIRE(n > 0);
    REQUIRE(std::string(msgs).find("static failed") != std::string::npos);
    REQUIRE(std::string(msgs).find("write 123 bytes") != std::string::npos);
    int K = 10000;
    auto t0 = std::chrono::high_resolution_clock::now();
    for(int i=0;i<K;++i){ tinybuf_result_format_msgs(&r, msgs, sizeof(msgs)); }
    auto t1 = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
    REQUIRE(ms >= 0);
    std::printf("perf: error_messages_static_and_hole format x%d %lld ms\n", K, (long long)ms);
    tinybuf_result_unref(&r);
}

static int g_err_del = 0;
static void err_del(void *p){ g_err_del++; tinybuf_free(p);} 
TEST_CASE("error_messages_cascade_deleter", "[error][hole_string]"){
    g_err_del = 0;
    tinybuf_error r = tinybuf_result_err(-2, NULL, NULL);
    hole_string *inner = hole_string_new();
    char *owned = tinybuf_strdup("owned");
    hole_string_append_cstr(inner, owned, 0, err_del);
    hole_string *outer = hole_string_new();
    hole_string_append_cstr(outer, "prefix:", 0, NULL);
    hole_string_append_sub(outer, inner);
    tinybuf_result_add_hole_msg(&r, outer);
    char msgs[128];
    tinybuf_result_format_msgs(&r, msgs, sizeof(msgs));
    REQUIRE(std::string(msgs).find("prefix:owned") != std::string::npos);
    int ref1 = tinybuf_result_ref(&r);
    REQUIRE(ref1 >= 2);
    int ref2 = tinybuf_result_unref(&r);
    REQUIRE(ref2 >= 1);
    int ref3 = tinybuf_result_unref(&r);
    REQUIRE(ref3 == 0);
    REQUIRE(g_err_del == 1);
    tinybuf_error r2 = tinybuf_result_err(-3, NULL, NULL);
    hole_string *s2 = hole_string_new();
    hole_string_append_cstr(s2, "x", 0, NULL);
    hole_string_append_sub(s2, hole_string_new());
    tinybuf_result_add_hole_msg(&r2, s2);
    char msgs2[64];
    int K = 8000;
    auto t0 = std::chrono::high_resolution_clock::now();
    for(int i=0;i<K;++i){ tinybuf_result_format_msgs(&r2, msgs2, sizeof(msgs2)); }
    auto t1 = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
    REQUIRE(ms >= 0);
    std::printf("perf: error_messages_cascade_deleter format x%d %lld ms\n", K, (long long)ms);
    tinybuf_result_unref(&r2);
}
