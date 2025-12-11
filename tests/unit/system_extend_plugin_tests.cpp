#include <catch2/catch_test_macros.hpp>
#include "tinybuf.h"
#include "tinybuf_plugin.h"
#include "dyn_sys.h"
#include "tinybuf_buffer.h"
#include "tinybuf_log.h"
#include <sstream>
#include <string>
#include <chrono>
static std::string tb_fmt(const tinybuf_error &r){ char msgs[512]; tinybuf_result_format_msgs(&r, msgs, sizeof(msgs)); std::ostringstream os; os << "res=" << r.res << " last=" << (tinybuf_last_error_message()? tinybuf_last_error_message(): "") << " msgs=" << msgs; return os.str(); }

static void tb_log(const char* tag, const tinybuf_error &r){ char msgs[512]; tinybuf_result_format_msgs(&r, msgs, sizeof(msgs)); LOGI("%s: res=%d last=%s msgs=%s", tag, r.res, tinybuf_last_error_message()? tinybuf_last_error_message(): "", msgs); }

TEST_CASE("system.extend plugin loads and handles hetero_tuple", "[plugin]")
{
    LOGI("case begin: unit hetero_tuple");
    tinybuf_set_use_strpool(1);
    tinybuf_init();
#ifdef _WIN32
    const char *plugin_path = "../tinybuf_plugins/system_extend.dll";
#else
    const char *plugin_path = "../tinybuf_plugins/libsystem_extend.so";
#endif
    int pl = tinybuf_plugin_register_from_dll(plugin_path);
    INFO("plugin_load_ret=" << pl);
    LOGI("plugin register ret=%d", pl);
    REQUIRE(pl == 0);
    tinybuf_value *arr = tinybuf_value_alloc();
    tinybuf_value *i = tinybuf_value_alloc();
    tinybuf_value_init_int(i, 7);
    tinybuf_value *s = tinybuf_value_alloc();
    tinybuf_value_init_string(s, "x", 1);
    tinybuf_value_array_append(arr, i);
    tinybuf_value_array_append(arr, s);
    buffer *buf = buffer_alloc();
    tinybuf_error w = tinybuf_result_ok(0);
    int wl = tinybuf_try_write_custom_id_box(buf, "hetero_tuple", arr, &w);
    INFO(tb_fmt(w));
    tb_log("write hetero_tuple", w);
    REQUIRE(wl > 0);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_error rr = tinybuf_result_ok(0);
    int rl = tinybuf_try_read_box(&br, out, NULL, &rr);
    INFO(tb_fmt(rr));
    tb_log("read hetero_tuple", rr);
    REQUIRE(rl > 0);
    REQUIRE(tinybuf_result_msg_count(&rr) == 0);
    REQUIRE(tinybuf_value_get_type(out) == tinybuf_array);
    tinybuf_error csr0 = tinybuf_result_ok(0);
    REQUIRE(tinybuf_value_get_child_size(out, &csr0) == 2);
    tinybuf_error ar0 = tinybuf_result_ok(0);
    tinybuf_error ar1 = tinybuf_result_ok(0);
    const tinybuf_value *c0 = tinybuf_value_get_array_child(out, 0, &ar0);
    const tinybuf_value *c1 = tinybuf_value_get_array_child(out, 1, &ar1);
    REQUIRE(tinybuf_value_get_type(c0) == tinybuf_int);
    tinybuf_error gri = tinybuf_result_ok(0);
    REQUIRE(tinybuf_value_get_int(c0, &gri) == 7);
    REQUIRE(tinybuf_value_get_type(c1) == tinybuf_string);
    tinybuf_error grs = tinybuf_result_ok(0);
    buffer *sv = tinybuf_value_get_string((tinybuf_value *)c1, &grs);
    REQUIRE(buffer_get_length(sv) == 1);
    tinybuf_result_unref(&rr);
    tinybuf_result_unref(&w);
    tinybuf_value_free(out);
    buffer_free(buf);
    tinybuf_value_free(arr);
    tinybuf_set_use_strpool(0);
    LOGI("case end: unit hetero_tuple");
}

TEST_CASE("system.extend plugin handles dataframe", "[plugin]")
{
    LOGI("case begin: unit dataframe");
    tinybuf_set_use_strpool(1);
    tinybuf_init();
#ifdef _WIN32
    const char *plugin_path2 = "../tinybuf_plugins/system_extend.dll";
#else
    const char *plugin_path2 = "../tinybuf_plugins/libsystem_extend.so";
#endif
    int pl = tinybuf_plugin_register_from_dll(plugin_path2);
    INFO("plugin_load_ret=" << pl);
    LOGI("plugin register ret=%d", pl);
    REQUIRE(pl == 0);
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
    tinybuf_error w = tinybuf_result_ok(0);
    int wl = tinybuf_try_write_custom_id_box(payload, "dataframe", df, &w);
    INFO(tb_fmt(w));
    REQUIRE(wl > 0);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br2{buffer_get_data(payload), (int64_t)buffer_get_length(payload), buffer_get_data(payload), (int64_t)buffer_get_length(payload)};
    tinybuf_error rr = tinybuf_result_ok(0);
    int rl = tinybuf_try_read_box(&br2, out, NULL, &rr);
    INFO(tb_fmt(rr));
    REQUIRE(rl > 0);
    REQUIRE(tinybuf_result_msg_count(&rr) == 0);
    REQUIRE(tinybuf_value_get_type(out) == tinybuf_indexed_tensor);
    tinybuf_error tr1 = tinybuf_result_ok(0);
    tinybuf_error tr2 = tinybuf_result_ok(0);
    REQUIRE(tinybuf_tensor_get_ndim(out, &tr1) == 2);
    REQUIRE(tinybuf_tensor_get_count(out, &tr2) == 4);
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
    tinybuf_error w = tinybuf_result_ok(0);
    int wl = tinybuf_try_write_box(payload, df, &w);
    INFO(tb_fmt(w));
    tb_log("write dataframe", w);
    REQUIRE(wl > 0);
    tinybuf_value *out = tinybuf_value_alloc();
    buf_ref br{buffer_get_data(payload), (int64_t)buffer_get_length(payload), buffer_get_data(payload), (int64_t)buffer_get_length(payload)};
    tinybuf_error rr = tinybuf_result_ok(0);
    auto t0 = std::chrono::steady_clock::now();
    int rl = tinybuf_try_read_box(&br, out, NULL, &rr);
    INFO(tb_fmt(rr));
    tb_log("read dataframe", rr);
    auto t1 = std::chrono::steady_clock::now();
    LOGI("read dataframe bytes=%d dur_ms=%lld", rl, (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count());
    REQUIRE(rl > 0);
    REQUIRE(tinybuf_result_msg_count(&rr) == 0);
    REQUIRE(tinybuf_value_get_type(out) == tinybuf_indexed_tensor);
    tinybuf_error tr3 = tinybuf_result_ok(0);
    tinybuf_error tr4 = tinybuf_result_ok(0);
    REQUIRE(tinybuf_tensor_get_ndim(out, &tr3) == 2);
    REQUIRE(tinybuf_tensor_get_count(out, &tr4) == 9);
    tinybuf_result_unref(&rr);
    tinybuf_result_unref(&w);
    tinybuf_value_free(out);
    buffer_free(payload);
    tinybuf_value_free(df);
    tinybuf_value_free(cols);
    tinybuf_value_free(rows);
    tinybuf_value_free(ten);
    tinybuf_set_use_strpool(0);
    LOGI("case end: unit dataframe");
}

TEST_CASE("custom result wrappers", "[result]")
{
    LOGI("case begin: unit custom result wrappers");
    tinybuf_set_use_strpool(1);
    tinybuf_register_builtin_plugins();
    tinybuf_value *out = tinybuf_value_alloc();
    tinybuf_error rr = tinybuf_result_ok(0);
    int rrl = tinybuf_custom_try_read("string", (const uint8_t *)"abc", 3, out, NULL, &rr);
    INFO(tb_fmt(rr));
    tb_log("custom read string", rr);
    REQUIRE(rrl == 3);
    REQUIRE(tinybuf_result_msg_count(&rr) == 0);
    REQUIRE(tinybuf_value_get_type(out) == tinybuf_string);
    tinybuf_error rf = tinybuf_result_ok(0);
    int rfl = tinybuf_custom_try_read("unknown", (const uint8_t *)"x", 1, out, NULL, &rf);
    INFO(tb_fmt(rf));
    tb_log("custom read unknown", rf);
    REQUIRE(rfl < 0);
    REQUIRE(tinybuf_result_msg_count(&rf) > 0);
    tinybuf_result_unref(&rr);
    tinybuf_result_unref(&rf);
    tinybuf_value_free(out);
    tinybuf_set_use_strpool(0);
    LOGI("case end: unit custom result wrappers");
}
