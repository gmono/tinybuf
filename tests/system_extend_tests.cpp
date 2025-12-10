#include <catch2/catch_test_macros.hpp>
#include "tinybuf.h"
#include "tinybuf_buffer.h"
#include "tinybuf_plugin.h"
#include "tinybuf_log.h"

static tinybuf_result xjson_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler){ (void)name; buf_ref br{ (const char*)data, (int64_t)len, (const char*)data, (int64_t)len }; return tinybuf_try_read_box(&br, out, contain_handler); }
static tinybuf_result xjson_write(const char *name, const tinybuf_value *in, buffer *out){ (void)name; buffer *tmp = buffer_alloc(); tinybuf_result r = tinybuf_try_write_box(tmp, in); if(r.res>0){ buffer_append(out, buffer_get_data(tmp), r.res); } buffer_free(tmp); return r; }
static tinybuf_result xjson_dump(const char *name, buf_ref *buf, buffer *out){ (void)name; int dw = tinybuf_dump_buffer_as_text(buf->ptr, (int)buf->size, out); return dw>0 ? tinybuf_result_ok(dw) : tinybuf_result_err(dw, "dump failed", NULL); }

TEST_CASE("custom string", "[system]"){
    tinybuf_set_use_strpool(1);
    tinybuf_register_builtin_plugins();
    buffer *buf = buffer_alloc();
    tinybuf_value *s = tinybuf_value_alloc();
    tinybuf_value_init_string(s, "hello", 5);
    tinybuf_result w = tinybuf_try_write_custom_id_box(buf, "string", s);
    {
        char wmsgs[256];
        tinybuf_result_format_msgs(&w, wmsgs, sizeof(wmsgs));
        CAPTURE(w.res, wmsgs, tinybuf_last_error_message());
    }
    REQUIRE(w.res > 0);
    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_value *out = tinybuf_value_alloc();
    tinybuf_result r = tinybuf_try_read_box(&br, out, NULL);
    {
        char msgs[256];
        tinybuf_result_format_msgs(&r, msgs, sizeof(msgs));
        CAPTURE(r.res, msgs, tinybuf_last_error_message());
    }
    REQUIRE(r.res > 0);
    REQUIRE(tinybuf_value_get_type(out) == tinybuf_string);
    buffer *sv = tinybuf_value_get_string(out);
    REQUIRE(sv != NULL);
    REQUIRE(buffer_get_length(sv) == 5);
    tinybuf_result_dispose(&r);
    tinybuf_result_dispose(&w);
    tinybuf_value_free(out);
    tinybuf_value_free(s);
    buffer_free(buf);
    tinybuf_set_use_strpool(0);
}

TEST_CASE("oop fallback", "[system]"){
    tinybuf_set_use_strpool(1);
    tinybuf_oop_attach_serializers("xjson", xjson_read, xjson_write, xjson_dump);
    tinybuf_oop_set_serializable("xjson", 1);
    tinybuf_register_builtin_plugins();
    tinybuf_value *m = tinybuf_value_alloc();
    tinybuf_value_init_int(m, 42);
    buffer *buf = buffer_alloc();
    tinybuf_result w = tinybuf_try_write_custom_id_box(buf, "xjson", m);
    {
        char wmsgs[256];
        tinybuf_result_format_msgs(&w, wmsgs, sizeof(wmsgs));
        CAPTURE(w.res, wmsgs, tinybuf_last_error_message());
    }
    REQUIRE(w.res > 0);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_result r = tinybuf_try_read_box(&br, out, NULL);
    {
        char msgs[256];
        tinybuf_result_format_msgs(&r, msgs, sizeof(msgs));
        CAPTURE(r.res, msgs, tinybuf_last_error_message());
    }
    REQUIRE(r.res > 0);
    REQUIRE(tinybuf_value_get_type(out) == tinybuf_int);
    REQUIRE(tinybuf_value_get_int(out) == 42);
    tinybuf_result_dispose(&r);
    tinybuf_result_dispose(&w);
    tinybuf_value_free(out);
    tinybuf_value_free(m);
    buffer_free(buf);
    tinybuf_set_use_strpool(0);
}

TEST_CASE("custom vs oop priority", "[system]"){
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
    {
        char wmsgs[256];
        tinybuf_result_format_msgs(&w, wmsgs, sizeof(wmsgs));
        CAPTURE(w.res, wmsgs, tinybuf_last_error_message());
    }
    REQUIRE(w.res > 0);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_result r = tinybuf_try_read_box(&br, out, NULL);
    {
        char msgs[256];
        tinybuf_result_format_msgs(&r, msgs, sizeof(msgs));
        CAPTURE(r.res, msgs, tinybuf_last_error_message());
    }
    REQUIRE(r.res > 0);
    REQUIRE(tinybuf_value_get_type(out) == tinybuf_string);
    buffer *sv = tinybuf_value_get_string(out);
    REQUIRE(sv != NULL);
    REQUIRE(buffer_get_length(sv) == 8);
    tinybuf_result_dispose(&r);
    tinybuf_result_dispose(&w);
    tinybuf_value_free(out);
    tinybuf_value_free(s);
    buffer_free(buf);
    tinybuf_set_use_strpool(0);
}

TEST_CASE("hetero tuple", "[system]"){
    tinybuf_set_use_strpool(1);
    tinybuf_register_builtin_plugins();
#ifdef _WIN32
    REQUIRE(tinybuf_plugin_register_from_dll("build/lib/Debug/system_extend.dll") == 0);
#endif
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
    {
        char wmsgs[256];
        tinybuf_result_format_msgs(&w, wmsgs, sizeof(wmsgs));
        CAPTURE(w.res, wmsgs, tinybuf_last_error_message());
    }
    REQUIRE(w.res > 0);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_result r = tinybuf_try_read_box(&br, out, NULL);
    {
        char msgs[256];
        tinybuf_result_format_msgs(&r, msgs, sizeof(msgs));
        CAPTURE(r.res, msgs, tinybuf_last_error_message());
    }
    REQUIRE(r.res > 0);
    REQUIRE(tinybuf_value_get_type(out) == tinybuf_array);
    REQUIRE(tinybuf_value_get_child_size(out) == 3);
    const tinybuf_value *c0 = tinybuf_value_get_array_child(out, 0);
    const tinybuf_value *c1 = tinybuf_value_get_array_child(out, 1);
    const tinybuf_value *c2 = tinybuf_value_get_array_child(out, 2);
    REQUIRE(tinybuf_value_get_type(c0) == tinybuf_int);
    REQUIRE(tinybuf_value_get_int(c0) == 123);
    REQUIRE(tinybuf_value_get_type(c1) == tinybuf_string);
    buffer *sv = tinybuf_value_get_string((tinybuf_value *)c1);
    REQUIRE(buffer_get_length(sv) == 3);
    REQUIRE(tinybuf_value_get_type(c2) == tinybuf_bool);
    REQUIRE(tinybuf_value_get_bool(c2) == 1);
    tinybuf_result_dispose(&r);
    tinybuf_result_dispose(&w);
    tinybuf_value_free(out);
    buffer_free(buf);
    tinybuf_value_free(arr);
    tinybuf_set_use_strpool(0);
}

TEST_CASE("hetero list", "[system]"){
    tinybuf_set_use_strpool(1);
    tinybuf_register_builtin_plugins();
#ifdef _WIN32
    REQUIRE(tinybuf_plugin_register_from_dll("build/lib/Debug/system_extend.dll") == 0);
#endif
    tinybuf_value *arr = tinybuf_value_alloc();
    for (int k = 0; k < 5; ++k){ tinybuf_value *i = tinybuf_value_alloc(); tinybuf_value_init_int(i, k); tinybuf_value_array_append(arr, i); }
    tinybuf_value *s = tinybuf_value_alloc();
    tinybuf_value_init_string(s, "z", 1);
    tinybuf_value_array_append(arr, s);
    buffer *buf = buffer_alloc();
    tinybuf_result w = tinybuf_try_write_custom_id_box(buf, "hetero_list", arr);
    {
        char wmsgs[256];
        tinybuf_result_format_msgs(&w, wmsgs, sizeof(wmsgs));
        CAPTURE(w.res, wmsgs, tinybuf_last_error_message());
    }
    REQUIRE(w.res > 0);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_result r = tinybuf_try_read_box(&br, out, NULL);
    {
        char msgs[256];
        tinybuf_result_format_msgs(&r, msgs, sizeof(msgs));
        CAPTURE(r.res, msgs, tinybuf_last_error_message());
    }
    REQUIRE(r.res > 0);
    REQUIRE(tinybuf_value_get_type(out) == tinybuf_array);
    REQUIRE(tinybuf_value_get_child_size(out) == 6);
    const tinybuf_value *last = tinybuf_value_get_array_child(out, 5);
    REQUIRE(tinybuf_value_get_type(last) == tinybuf_string);
    tinybuf_result_dispose(&r);
    tinybuf_result_dispose(&w);
    tinybuf_value_free(out);
    buffer_free(buf);
    tinybuf_value_free(arr);
    tinybuf_set_use_strpool(0);
}

TEST_CASE("dataframe", "[system]"){
    tinybuf_set_use_strpool(1);
    tinybuf_register_builtin_plugins();
#ifdef _WIN32
    REQUIRE(tinybuf_plugin_register_from_dll("build/lib/Debug/system_extend.dll") == 0);
#endif
    int64_t shape[2] = {2, 2};
    double data[4] = {1.0, 2.0, 3.0, 4.0};
    tinybuf_value *ten = tinybuf_value_alloc();
    tinybuf_value_init_tensor(ten, 8, shape, 2, data, 4);
    tinybuf_value *rows = tinybuf_value_alloc();
    tinybuf_value *r0 = tinybuf_value_alloc(); tinybuf_value_init_string(r0, "r0", 2);
    tinybuf_value *r1 = tinybuf_value_alloc(); tinybuf_value_init_string(r1, "r1", 2);
    tinybuf_value_array_append(rows, r0);
    tinybuf_value_array_append(rows, r1);
    tinybuf_value *cols = tinybuf_value_alloc();
    tinybuf_value *c0 = tinybuf_value_alloc(); tinybuf_value_init_string(c0, "c0", 2);
    tinybuf_value *c1 = tinybuf_value_alloc(); tinybuf_value_init_string(c1, "c1", 2);
    tinybuf_value_array_append(cols, c0);
    tinybuf_value_array_append(cols, c1);
    const tinybuf_value *idxs[2] = {rows, cols};
    tinybuf_value *df = tinybuf_value_alloc();
    tinybuf_value_init_indexed_tensor(df, ten, idxs, 2);
    buffer *buf = buffer_alloc();
    tinybuf_result w = tinybuf_try_write_custom_id_box(buf, "dataframe", df);
    {
        char wmsgs[256];
        tinybuf_result_format_msgs(&w, wmsgs, sizeof(wmsgs));
        CAPTURE(w.res, wmsgs, tinybuf_last_error_message());
    }
    REQUIRE(w.res > 0);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_result r = tinybuf_try_read_box(&br, out, NULL);
    {
        char msgs[256];
        tinybuf_result_format_msgs(&r, msgs, sizeof(msgs));
        CAPTURE(r.res, msgs, tinybuf_last_error_message());
    }
    REQUIRE(r.res > 0);
    REQUIRE(tinybuf_value_get_type(out) == tinybuf_indexed_tensor);
    REQUIRE(tinybuf_tensor_get_ndim(out) == 2);
    REQUIRE(tinybuf_tensor_get_count(out) == 4);
    tinybuf_result_dispose(&r);
    tinybuf_result_dispose(&w);
    tinybuf_value_free(out);
    buffer_free(buf);
    tinybuf_value_free(df);
    tinybuf_value_free(cols);
    tinybuf_value_free(rows);
    tinybuf_value_free(ten);
    tinybuf_set_use_strpool(0);
}
