#include <catch2/catch_test_macros.hpp>
#include "tinybuf.h"
#include "tinybuf_plugin.h"
#include "tinybuf_buffer.h"

TEST_CASE("system.extend plugin loads and handles hetero_tuple", "[plugin]")
{
    tinybuf_set_use_strpool(1);
    tinybuf_register_builtin_plugins();
#ifdef _WIN32
    REQUIRE(tinybuf_plugin_register_from_dll("../tinybuf_plugins/system_extend.dll") == 0);
#endif
    tinybuf_value *arr = tinybuf_value_alloc();
    tinybuf_value *i = tinybuf_value_alloc();
    tinybuf_value_init_int(i, 7);
    tinybuf_value *s = tinybuf_value_alloc();
    tinybuf_value_init_string(s, "x", 1);
    tinybuf_value_array_append(arr, i);
    tinybuf_value_array_append(arr, s);
    buffer *buf = buffer_alloc();
    tinybuf_result w = tinybuf_try_write_custom_id_box(buf, "hetero_tuple", arr);
    REQUIRE(w.res > 0);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_result rr = tinybuf_try_read_box(&br, out, NULL);
    char msgs[256];
    tinybuf_result_format_msgs(&rr, msgs, sizeof(msgs));
    CAPTURE(rr.res, msgs);
    REQUIRE(rr.res > 0);
    REQUIRE(tinybuf_result_msg_count(&rr) == 0);
    REQUIRE(tinybuf_value_get_type(out) == tinybuf_array);
    REQUIRE(tinybuf_value_get_child_size(out) == 2);
    const tinybuf_value *c0 = tinybuf_value_get_array_child(out, 0);
    const tinybuf_value *c1 = tinybuf_value_get_array_child(out, 1);
    REQUIRE(tinybuf_value_get_type(c0) == tinybuf_int);
    REQUIRE(tinybuf_value_get_int(c0) == 7);
    REQUIRE(tinybuf_value_get_type(c1) == tinybuf_string);
    buffer *sv = tinybuf_value_get_string((tinybuf_value *)c1);
    REQUIRE(buffer_get_length(sv) == 1);
    tinybuf_result_unref(&rr);
    tinybuf_result_unref(&w);
    tinybuf_value_free(out);
    buffer_free(buf);
    tinybuf_value_free(arr);
    tinybuf_set_use_strpool(0);
}

TEST_CASE("system.extend plugin handles dataframe", "[plugin]")
{
    tinybuf_set_use_strpool(1);
    tinybuf_register_builtin_plugins();
#ifdef _WIN32
    REQUIRE(tinybuf_plugin_register_from_dll("../tinybuf_plugins/system_extend.dll") == 0);
#endif
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
    buffer *payload = buffer_alloc();
    tinybuf_result w = tinybuf_try_write_custom_id_box(payload, "dataframe", df);
    REQUIRE(w.res > 0);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br2{buffer_get_data(payload), (int64_t)buffer_get_length(payload), buffer_get_data(payload), (int64_t)buffer_get_length(payload)};
    tinybuf_result rr = tinybuf_try_read_box(&br2, out, NULL);
    char msgs2[256];
    tinybuf_result_format_msgs(&rr, msgs2, sizeof(msgs2));
    CAPTURE(rr.res, msgs2, tinybuf_last_error_message());
    REQUIRE(rr.res > 0);
    REQUIRE(tinybuf_result_msg_count(&rr) == 0);
    REQUIRE(tinybuf_value_get_type(out) == tinybuf_indexed_tensor);
    REQUIRE(tinybuf_tensor_get_ndim(out) == 2);
    REQUIRE(tinybuf_tensor_get_count(out) == 4);
    tinybuf_result_unref(&rr);
    tinybuf_result_unref(&w);
    tinybuf_value_free(out);
    buffer_free(payload);
    tinybuf_value_free(df);
    tinybuf_value_free(cols);
    tinybuf_value_free(rows);
    tinybuf_value_free(ten);
    tinybuf_set_use_strpool(0);
}

TEST_CASE("indexed_tensor roundtrip", "[tensor]")
{
    tinybuf_set_use_strpool(1);
    tinybuf_register_builtin_plugins();
    int64_t shape[2] = {3, 3};
    double data[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    tinybuf_value *ten = tinybuf_value_alloc();
    tinybuf_value_init_tensor(ten, 8, shape, 2, data, 9);
    tinybuf_value *rows = tinybuf_value_alloc();
    tinybuf_value *r0 = tinybuf_value_alloc();
    tinybuf_value_init_string(r0, "r0", 2);
    tinybuf_value *r1 = tinybuf_value_alloc();
    tinybuf_value_init_string(r1, "r1", 2);
    tinybuf_value *r2 = tinybuf_value_alloc();
    tinybuf_value_init_string(r2, "r2", 2);
    tinybuf_value_array_append(rows, r0);
    tinybuf_value_array_append(rows, r1);
    tinybuf_value_array_append(rows, r2);
    tinybuf_value *cols = tinybuf_value_alloc();
    tinybuf_value *c0 = tinybuf_value_alloc();
    tinybuf_value_init_string(c0, "c0", 2);
    tinybuf_value *c1 = tinybuf_value_alloc();
    tinybuf_value_init_string(c1, "c1", 2);
    tinybuf_value *c2 = tinybuf_value_alloc();
    tinybuf_value_init_string(c2, "c2", 2);
    tinybuf_value_array_append(cols, c0);
    tinybuf_value_array_append(cols, c1);
    tinybuf_value_array_append(cols, c2);
    const tinybuf_value *idxs[2] = {rows, cols};
    tinybuf_value *df = tinybuf_value_alloc();
    tinybuf_value_init_indexed_tensor(df, ten, idxs, 2);
    buffer *payload = buffer_alloc();
    tinybuf_result w = tinybuf_try_write_box(payload, df);
    REQUIRE(w.res > 0);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br{buffer_get_data(payload), (int64_t)buffer_get_length(payload), buffer_get_data(payload), (int64_t)buffer_get_length(payload)};
    tinybuf_result rr = tinybuf_try_read_box(&br, out, NULL);
    char msgs[256];
    tinybuf_result_format_msgs(&rr, msgs, sizeof(msgs));
    CAPTURE(rr.res, msgs, tinybuf_last_error_message());
    REQUIRE(rr.res > 0);
    REQUIRE(tinybuf_result_msg_count(&rr) == 0);
    REQUIRE(tinybuf_value_get_type(out) == tinybuf_indexed_tensor);
    REQUIRE(tinybuf_tensor_get_ndim(out) == 2);
    REQUIRE(tinybuf_tensor_get_count(out) == 9);
    tinybuf_result_unref(&rr);
    tinybuf_result_unref(&w);
    tinybuf_value_free(out);
    buffer_free(payload);
    tinybuf_value_free(df);
    tinybuf_value_free(cols);
    tinybuf_value_free(rows);
    tinybuf_value_free(ten);
    tinybuf_set_use_strpool(0);
}

TEST_CASE("custom result wrappers", "[result]")
{
    tinybuf_set_use_strpool(1);
    tinybuf_register_builtin_plugins();
    tinybuf_value *out = tinybuf_value_alloc();
    tinybuf_result rr = tinybuf_custom_try_read("string", (const uint8_t *)"abc", 3, out, NULL);
    REQUIRE(rr.res == 3);
    REQUIRE(tinybuf_result_msg_count(&rr) == 0);
    REQUIRE(tinybuf_value_get_type(out) == tinybuf_string);
    tinybuf_result rf = tinybuf_custom_try_read("unknown", (const uint8_t *)"x", 1, out, NULL);
    char msgs3[256];
    tinybuf_result_format_msgs(&rf, msgs3, sizeof(msgs3));
    CAPTURE(rf.res, msgs3);
    REQUIRE(rf.res < 0);
    REQUIRE(tinybuf_result_msg_count(&rf) > 0);
    tinybuf_result_unref(&rr);
    tinybuf_result_unref(&rf);
    tinybuf_value_free(out);
    tinybuf_set_use_strpool(0);
}
