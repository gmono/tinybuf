#include "tinybuf.h"
#include "tinybuf_buffer.h"
#include "tinybuf_plugin.h"
#include "tinybuf_log.h"
#include <assert.h>
#include <stdio.h>

static tinybuf_result xjson_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler){ (void)name; buf_ref br{ (const char*)data, (int64_t)len, (const char*)data, (int64_t)len }; return tinybuf_try_read_box(&br, out, contain_handler); }
static tinybuf_result xjson_write(const char *name, const tinybuf_value *in, buffer *out){ (void)name; buffer *tmp = buffer_alloc(); tinybuf_result r = tinybuf_try_write_box(tmp, in); if(r.res>0){ buffer_append(out, buffer_get_data(tmp), r.res); } buffer_free(tmp); return r; }
static tinybuf_result xjson_dump(const char *name, buf_ref *buf, buffer *out){ (void)name; int dw = tinybuf_dump_buffer_as_text(buf->ptr, (int)buf->size, out); return dw>0 ? tinybuf_result_ok(dw) : tinybuf_result_err(dw, "dump failed", NULL); }

static void test_custom_string()
{
    printf("[test_custom_string] begin\n");
    fflush(stdout);
    tinybuf_set_use_strpool(1);
    tinybuf_register_builtin_plugins();
    buffer *buf = buffer_alloc();
    tinybuf_value *s = tinybuf_value_alloc();
    tinybuf_value_init_string(s, "hello", 5);
    tinybuf_result w = tinybuf_try_write_custom_id_box(buf, "string", s);
    printf("[test_custom_string] write len=%d\n", w.res);
    fflush(stdout);
    assert(w.res > 0);
    printf("[test_custom_string] buf bytes len=%d\n", (int)buffer_get_length(buf));
    for (int i = 0; i < buffer_get_length(buf); ++i)
    {
        unsigned char c = (unsigned char)buffer_get_data(buf)[i];
        printf("%02X ", c);
    }
    printf("\n");
    fflush(stdout);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_result r = tinybuf_try_read_box(&br, out, NULL);
    printf("[test_custom_string] read len=%d\n", r.res);
    fflush(stdout);
    assert(r.res > 0);
    assert(tinybuf_value_get_type(out) == tinybuf_string);
    buffer *sv = tinybuf_value_get_string(out);
    assert(sv && buffer_get_length(sv) == 5);
    tinybuf_result_dispose(&r);
    tinybuf_result_dispose(&w);
    tinybuf_value_free(out);
    tinybuf_value_free(s);
    buffer_free(buf);
    tinybuf_set_use_strpool(0);
    printf("[test_custom_string] end\n");
    fflush(stdout);
}

static void test_oop_fallback()
{
    printf("[test_oop_fallback] begin\n");
    fflush(stdout);
    tinybuf_set_use_strpool(1);
    tinybuf_oop_attach_serializers("xjson", xjson_read, xjson_write, xjson_dump);
    tinybuf_oop_set_serializable("xjson", 1);
    tinybuf_register_builtin_plugins();
    tinybuf_value *m = tinybuf_value_alloc();
    tinybuf_value_init_int(m, 42);
    buffer *buf = buffer_alloc();
    tinybuf_result w = tinybuf_try_write_custom_id_box(buf, "xjson", m);
    printf("[test_oop_fallback] write len=%d\n", w.res);
    fflush(stdout);
    assert(w.res > 0);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    printf("[test_oop_fallback] buf bytes len=%d\n", (int)buffer_get_length(buf));
    for (int i = 0; i < buffer_get_length(buf); ++i)
    {
        unsigned char c = (unsigned char)buffer_get_data(buf)[i];
        printf("%02X ", c);
    }
    printf("\n");
    fflush(stdout);
    buffer *dump = buffer_alloc();
    int dw = tinybuf_dump_buffer_as_text(br.ptr, (int)br.size, dump);
    printf("[test_oop_fallback] dump len=%d text=%.*s\n", dw, buffer_get_length(dump), buffer_get_data(dump));
    fflush(stdout);
    buffer_free(dump);
    tinybuf_result r = tinybuf_try_read_box(&br, out, NULL);
    printf("[test_oop_fallback] read len=%d\n", r.res);
    fflush(stdout);
    assert(r.res > 0);
    assert(tinybuf_value_get_type(out) == tinybuf_int);
    assert(tinybuf_value_get_int(out) == 42);
    tinybuf_result_dispose(&r);
    tinybuf_result_dispose(&w);
    tinybuf_value_free(out);
    tinybuf_value_free(m);
    buffer_free(buf);
    tinybuf_set_use_strpool(0);
    printf("[test_oop_fallback] end\n");
    fflush(stdout);
}

static void test_custom_vs_oop_priority()
{
    printf("[test_custom_vs_oop_priority] begin\n");
    fflush(stdout);
    tinybuf_set_use_strpool(1);
    tinybuf_oop_attach_serializers("mytype", xjson_read, xjson_write, xjson_dump);
    tinybuf_oop_set_serializable("mytype", 1);
    auto custstr_read = [](const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler) -> tinybuf_result
    { (void)name; (void)contain_handler; tinybuf_value_init_string(out, (const char*)data, len); return tinybuf_result_ok(len); };
    auto custstr_write = [](const char *name, const tinybuf_value *in, buffer *out) -> tinybuf_result
    { (void)name; buffer *s = tinybuf_value_get_string(in); if(!s) return tinybuf_result_err(-1, "string write: not string", NULL); int sl = buffer_get_length(s); if(sl>0){ buffer_append(out, buffer_get_data(s), sl);} return tinybuf_result_ok(sl); };
    auto custstr_dump = [](const char *name, buf_ref *buf, buffer *out) -> tinybuf_result
    { (void)name; int64_t len = buf->size; buffer_append(out, "\"", 1); if(len>0){ buffer_append(out, buf->ptr, (int)len);} buffer_append(out, "\"", 1); return tinybuf_result_ok((int)len); };
    tinybuf_custom_register("mytype", custstr_read, custstr_write, custstr_dump);
    tinybuf_value *s = tinybuf_value_alloc();
    tinybuf_value_init_string(s, "override", 8);
    buffer *buf = buffer_alloc();
    tinybuf_result w = tinybuf_try_write_custom_id_box(buf, "mytype", s);
    printf("[test_custom_vs_oop_priority] write len=%d\n", w.res);
    fflush(stdout);
    assert(w.res > 0);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_result r = tinybuf_try_read_box(&br, out, NULL);
    printf("[test_custom_vs_oop_priority] read len=%d\n", r.res);
    fflush(stdout);
    assert(r.res > 0);
    assert(tinybuf_value_get_type(out) == tinybuf_string);
    buffer *sv = tinybuf_value_get_string(out);
    assert(sv && buffer_get_length(sv) == 8);
    tinybuf_result_dispose(&r);
    tinybuf_result_dispose(&w);
    tinybuf_value_free(out);
    tinybuf_value_free(s);
    buffer_free(buf);
    tinybuf_set_use_strpool(0);
    printf("[test_custom_vs_oop_priority] end\n");
    fflush(stdout);
}

static void test_hetero_tuple()
{
    printf("[test_hetero_tuple] begin\n");
    fflush(stdout);
    tinybuf_set_use_strpool(1);
    tinybuf_register_builtin_plugins();
    tinybuf_value *arr = tinybuf_value_alloc();
    tinybuf_value *i = tinybuf_value_alloc();
    tinybuf_value_init_int(i, 123);
    tinybuf_value *s = tinybuf_value_alloc();
    tinybuf_value_init_string(s, "abc", 3);
    tinybuf_value *b = tinybuf_value_alloc();
    tinybuf_value_init_bool(b, 1);
    tinybuf_value_array_append(arr, i);
    tinybuf_value_array_append(arr, s);
    tinybuf_value_array_append(arr, b);
    buffer *buf = buffer_alloc();
    tinybuf_result w = tinybuf_try_write_custom_id_box(buf, "hetero_tuple", arr);
    printf("[test_hetero_tuple] write len=%d\n", w.res);
    fflush(stdout);
    assert(w.res > 0);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_result r = tinybuf_try_read_box(&br, out, NULL);
    printf("[test_hetero_tuple] read len=%d\n", r.res);
    fflush(stdout);
    assert(r.res > 0);
    assert(tinybuf_value_get_type(out) == tinybuf_array);
    assert(tinybuf_value_get_child_size(out) == 3);
    const tinybuf_value *c0 = tinybuf_value_get_array_child(out, 0);
    const tinybuf_value *c1 = tinybuf_value_get_array_child(out, 1);
    const tinybuf_value *c2 = tinybuf_value_get_array_child(out, 2);
    assert(tinybuf_value_get_type(c0) == tinybuf_int && tinybuf_value_get_int(c0) == 123);
    assert(tinybuf_value_get_type(c1) == tinybuf_string && buffer_get_length(tinybuf_value_get_string((tinybuf_value *)c1)) == 3);
    assert(tinybuf_value_get_type(c2) == tinybuf_bool && tinybuf_value_get_bool(c2) == 1);
    tinybuf_result_dispose(&r);
    tinybuf_result_dispose(&w);
    tinybuf_value_free(out);
    buffer_free(buf);
    tinybuf_value_free(arr);
    tinybuf_set_use_strpool(0);
    printf("[test_hetero_tuple] end\n");
    fflush(stdout);
}

static void test_hetero_list()
{
    printf("[test_hetero_list] begin\n");
    fflush(stdout);
    tinybuf_set_use_strpool(1);
    tinybuf_register_builtin_plugins();
    tinybuf_value *arr = tinybuf_value_alloc();
    for (int k = 0; k < 5; ++k)
    {
        tinybuf_value *i = tinybuf_value_alloc();
        tinybuf_value_init_int(i, k);
        tinybuf_value_array_append(arr, i);
    }
    tinybuf_value *s = tinybuf_value_alloc();
    tinybuf_value_init_string(s, "z", 1);
    tinybuf_value_array_append(arr, s);
    buffer *buf = buffer_alloc();
    tinybuf_result w = tinybuf_try_write_custom_id_box(buf, "hetero_list", arr);
    printf("[test_hetero_list] write len=%d\n", w.res);
    fflush(stdout);
    assert(w.res > 0);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_result r = tinybuf_try_read_box(&br, out, NULL);
    printf("[test_hetero_list] read len=%d\n", r.res);
    fflush(stdout);
    assert(r.res > 0);
    assert(tinybuf_value_get_type(out) == tinybuf_array);
    assert(tinybuf_value_get_child_size(out) == 6);
    const tinybuf_value *last = tinybuf_value_get_array_child(out, 5);
    assert(tinybuf_value_get_type(last) == tinybuf_string);
    tinybuf_result_dispose(&r);
    tinybuf_result_dispose(&w);
    tinybuf_value_free(out);
    buffer_free(buf);
    tinybuf_value_free(arr);
    tinybuf_set_use_strpool(0);
    printf("[test_hetero_list] end\n");
    fflush(stdout);
}

static void test_dataframe()
{
    printf("[test_dataframe] begin\n");
    fflush(stdout);
    tinybuf_set_use_strpool(1);
    tinybuf_register_builtin_plugins();
    int64_t shape[2] = {2, 2};
    double data[4] = {1.0, 2.0, 3.0, 4.0};
    tinybuf_value *ten = tinybuf_value_alloc();
    tinybuf_value_init_tensor(ten, 8, shape, 2, data, 4);
    tinybuf_value *rows = tinybuf_value_alloc();
    tinybuf_value *r0 = tinybuf_value_alloc();
    tinybuf_value_init_string(r0, "r0", 2);
    tinybuf_value *r1 = tinybuf_value_alloc();
    tinybuf_value_init_string(r1, "r1", 2);
    tinybuf_value_array_append(rows, r0);
    tinybuf_value_array_append(rows, r1);
    tinybuf_value *cols = tinybuf_value_alloc();
    tinybuf_value *c0 = tinybuf_value_alloc();
    tinybuf_value_init_string(c0, "c0", 2);
    tinybuf_value *c1 = tinybuf_value_alloc();
    tinybuf_value_init_string(c1, "c1", 2);
    tinybuf_value_array_append(cols, c0);
    tinybuf_value_array_append(cols, c1);
    const tinybuf_value *idxs[2] = {rows, cols};
    tinybuf_value *df = tinybuf_value_alloc();
    tinybuf_value_init_indexed_tensor(df, ten, idxs, 2);
    buffer *buf = buffer_alloc();
    tinybuf_result w = tinybuf_try_write_custom_id_box(buf, "dataframe", df);
    printf("[test_dataframe] write len=%d\n", w.res);
    fflush(stdout);
    assert(w.res > 0);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_result r = tinybuf_try_read_box(&br, out, NULL);
    printf("[test_dataframe] read len=%d\n", r.res);
    fflush(stdout);
    assert(r.res > 0);
    assert(tinybuf_value_get_type(out) == tinybuf_indexed_tensor);
    assert(tinybuf_tensor_get_ndim(out) == 2 && tinybuf_tensor_get_count(out) == 4);
    tinybuf_result_dispose(&r);
    tinybuf_result_dispose(&w);
    tinybuf_value_free(out);
    buffer_free(buf);
    tinybuf_value_free(df);
    tinybuf_value_free(cols);
    tinybuf_value_free(rows);
    tinybuf_value_free(ten);
    tinybuf_set_use_strpool(0);
    printf("[test_dataframe] end\n");
    fflush(stdout);
}

int main()
{
    printf("[sys_tests] start\n");
    fflush(stdout);
    test_custom_string();
    printf("[sys_tests] after custom_string\n");
    fflush(stdout);
    test_oop_fallback();
    printf("[sys_tests] after oop_fallback\n");
    fflush(stdout);
    test_custom_vs_oop_priority();
    printf("[sys_tests] after custom_vs_oop_priority\n");
    fflush(stdout);
    test_hetero_tuple();
    printf("[sys_tests] after hetero_tuple\n");
    fflush(stdout);
    test_hetero_list();
    printf("[sys_tests] after hetero_list\n");
    fflush(stdout);
    test_dataframe();
    printf("[sys_tests] done\n");
    fflush(stdout);
    return 0;
}
