#include <catch2/catch_test_macros.hpp>
#include "tinybuf.h"
#include "tinybuf_plugin.h"
#include "tinybuf_buffer.h"

TEST_CASE("system.extend plugin loads and handles hetero_tuple", "[plugin]"){
    tinybuf_set_use_strpool(1);
    tinybuf_init();
    tinybuf_value *arr = tinybuf_value_alloc();
    tinybuf_value *i = tinybuf_value_alloc(); tinybuf_value_init_int(i, 7);
    tinybuf_value *s = tinybuf_value_alloc(); tinybuf_value_init_string(s, "x", 1);
    tinybuf_value_array_append(arr, i); tinybuf_value_array_append(arr, s);
    buffer *buf = buffer_alloc(); int w = tinybuf_try_write_custom_id_box(buf, "hetero_tuple", arr);
    REQUIRE(w > 0);
    tinybuf_value *out = tinybuf_value_alloc(); buf_ref br{ buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf) };
    int r = tinybuf_try_read_box(&br, out, NULL);
    REQUIRE(r > 0);
    REQUIRE(tinybuf_value_get_type(out) == tinybuf_array);
    REQUIRE(tinybuf_value_get_child_size(out) == 2);
    const tinybuf_value *c0 = tinybuf_value_get_array_child(out, 0);
    const tinybuf_value *c1 = tinybuf_value_get_array_child(out, 1);
    REQUIRE(tinybuf_value_get_type(c0) == tinybuf_int);
    REQUIRE(tinybuf_value_get_int(c0) == 7);
    REQUIRE(tinybuf_value_get_type(c1) == tinybuf_string);
    buffer *sv = tinybuf_value_get_string((tinybuf_value*)c1);
    REQUIRE(buffer_get_length(sv) == 1);
    tinybuf_value_free(out); buffer_free(buf); tinybuf_value_free(arr); tinybuf_set_use_strpool(0);
}

TEST_CASE("system.extend plugin handles dataframe", "[plugin]"){
    tinybuf_set_use_strpool(1);
    tinybuf_init();
    int64_t shape[2] = {2,2}; double data[4] = {1.0,2.0,3.0,4.0};
    tinybuf_value *ten = tinybuf_value_alloc(); tinybuf_value_init_tensor(ten, 8, shape, 2, data, 4);
    tinybuf_value *rows = tinybuf_value_alloc(); tinybuf_value *r0 = tinybuf_value_alloc(); tinybuf_value_init_string(r0, "r0", 2); tinybuf_value *r1 = tinybuf_value_alloc(); tinybuf_value_init_string(r1, "r1", 2); tinybuf_value_array_append(rows, r0); tinybuf_value_array_append(rows, r1);
    tinybuf_value *cols = tinybuf_value_alloc(); tinybuf_value *c0 = tinybuf_value_alloc(); tinybuf_value_init_string(c0, "c0", 2); tinybuf_value *c1 = tinybuf_value_alloc(); tinybuf_value_init_string(c1, "c1", 2); tinybuf_value_array_append(cols, c0); tinybuf_value_array_append(cols, c1);
    const tinybuf_value *idxs[2] = { rows, cols };
    tinybuf_value *df = tinybuf_value_alloc(); tinybuf_value_init_indexed_tensor(df, ten, idxs, 2);
    buffer *buf = buffer_alloc(); int w = tinybuf_try_write_custom_id_box(buf, "dataframe", df);
    REQUIRE(w > 0);
    tinybuf_value *out = tinybuf_value_alloc(); buf_ref br{ buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf) };
    int r = tinybuf_try_read_box(&br, out, NULL);
    REQUIRE(r > 0);
    REQUIRE(tinybuf_value_get_type(out) == tinybuf_indexed_tensor);
    REQUIRE(tinybuf_tensor_get_ndim(out) == 2);
    REQUIRE(tinybuf_tensor_get_count(out) == 4);
    tinybuf_value_free(out); buffer_free(buf); tinybuf_value_free(df); tinybuf_value_free(cols); tinybuf_value_free(rows); tinybuf_value_free(ten); tinybuf_set_use_strpool(0);
}
