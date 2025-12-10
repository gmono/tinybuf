#include <catch2/catch_test_macros.hpp>
#include "tinybuf.h"
#include "tinybuf_plugin.h"
#include "tinybuf_buffer.h"

static int xjson_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_error *r)
{
    (void)name;
    buf_ref br{(const char *)data, (int64_t)len, (const char *)data, (int64_t)len};
    int rl = tinybuf_try_read_box(&br, out, contain_handler, r);
    return rl;
}

static int xjson_write(const char *name, const tinybuf_value *in, buffer *out, tinybuf_error *r)
{
    (void)name;
    int wl = tinybuf_try_write_box(out, in, r);
    return wl;
}

static int xjson_dump(const char *name, buf_ref *buf, buffer *out, tinybuf_error *r)
{
    (void)name;
    int dw = tinybuf_dump_buffer_as_text(buf->ptr, (int)buf->size, out);
    if (dw > 0) return dw;
    tinybuf_error er = tinybuf_result_err(dw, "dump failed", NULL);
    tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
    return dw;
}

TEST_CASE("custom string via type_idx", "[custom]")
{
    tinybuf_set_use_strpool(1);
    tinybuf_register_builtin_plugins();

    buffer *buf = buffer_alloc();

    tinybuf_value *s = tinybuf_value_alloc();
    tinybuf_value_init_string(s, "hello", 5);

    tinybuf_error w = tinybuf_result_ok(0);
    int wl = tinybuf_try_write_custom_id_box(buf, "string", s, &w);
    REQUIRE(wl > 0);

    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_value *out = tinybuf_value_alloc();
    tinybuf_error rr = tinybuf_result_ok(0);
    int rl = tinybuf_try_read_box(&br, out, NULL, &rr);
    char msgs[128];
    tinybuf_result_format_msgs(&rr, msgs, sizeof(msgs));
    CAPTURE(rr.res, msgs, tinybuf_last_error_message());
    REQUIRE(rl > 0);
    REQUIRE(tinybuf_result_msg_count(&rr) == 0);
    REQUIRE(tinybuf_value_get_type(out) == tinybuf_string);
    tinybuf_error gr = tinybuf_result_ok(0);
    buffer *sv = tinybuf_value_get_string(out, &gr);
    REQUIRE(sv != NULL);
    REQUIRE(buffer_get_length(sv) == 5);

    tinybuf_result_unref(&rr);
    tinybuf_result_unref(&w);
    tinybuf_value_free(out);
    tinybuf_value_free(s);
    buffer_free(buf);
    tinybuf_set_use_strpool(0);
}

TEST_CASE("hetero_list concatenated boxes", "[custom]")
{
    tinybuf_set_use_strpool(1);
    tinybuf_register_builtin_plugins();
    tinybuf_plugin_register_from_dll("../tinybuf_plugins/system_extend.dll");

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
    tinybuf_error w = tinybuf_result_ok(0);
    int wl = tinybuf_try_write_custom_id_box(buf, "hetero_list", arr, &w);
    REQUIRE(wl > 0);

    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_value *out = tinybuf_value_alloc();
    tinybuf_error rr2 = tinybuf_result_ok(0);
    int rl2 = tinybuf_try_read_box(&br, out, NULL, &rr2);
    char msgs2[128];
    tinybuf_result_format_msgs(&rr2, msgs2, sizeof(msgs2));
    CAPTURE(rr2.res, msgs2, tinybuf_last_error_message());
    REQUIRE(rl2 > 0);
    REQUIRE(tinybuf_result_msg_count(&rr2) == 0);
    REQUIRE(tinybuf_value_get_type(out) == tinybuf_array);
    tinybuf_error csr0 = tinybuf_result_ok(0);
    REQUIRE(tinybuf_value_get_child_size(out, &csr0) == 6);
    tinybuf_error ar0 = tinybuf_result_ok(0);
    const tinybuf_value *last = tinybuf_value_get_array_child(out, 5, &ar0);
    REQUIRE(tinybuf_value_get_type(last) == tinybuf_string);

    tinybuf_result_unref(&rr2);
    tinybuf_result_unref(&w);
    tinybuf_value_free(out);
    buffer_free(buf);
    tinybuf_value_free(arr);
    tinybuf_set_use_strpool(0);
}
