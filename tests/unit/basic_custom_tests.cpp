#include <catch2/catch_test_macros.hpp>
#include "tinybuf.h"
#include "tinybuf_plugin.h"
#include "tinybuf_buffer.h"

static int xjson_read(const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler)
{
    (void)name;
    buf_ref br{(const char *)data, (int64_t)len, (const char *)data, (int64_t)len};
    tinybuf_result r = tinybuf_try_read_box(&br, out, contain_handler);
    return r.res;
}

static int xjson_write(const char *name, const tinybuf_value *in, buffer *out)
{
    (void)name;
    tinybuf_result r = tinybuf_try_write_box(out, in);
    return r.res;
}

static int xjson_dump(const char *name, buf_ref *buf, buffer *out)
{
    (void)name;
    return tinybuf_dump_buffer_as_text(buf->ptr, (int)buf->size, out);
}

TEST_CASE("custom string via type_idx", "[custom]")
{
    tinybuf_set_use_strpool(1);
    tinybuf_register_builtin_plugins();

    buffer *buf = buffer_alloc();

    tinybuf_value *s = tinybuf_value_alloc();
    tinybuf_value_init_string(s, "hello", 5);

    tinybuf_result w = tinybuf_try_write_custom_id_box(buf, "string", s);
    REQUIRE(w.res > 0);

    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_value *out = tinybuf_value_alloc();
    tinybuf_result rr = tinybuf_try_read_box(&br, out, NULL);
    char msgs[128];
    tinybuf_result_format_msgs(&rr, msgs, sizeof(msgs));
    CAPTURE(rr.res, msgs, tinybuf_last_error_message());
    REQUIRE(rr.res > 0);
    REQUIRE(tinybuf_result_msg_count(&rr) == 0);
    REQUIRE(tinybuf_value_get_type(out) == tinybuf_string);
    buffer *sv = tinybuf_value_get_string(out);
    REQUIRE(sv != NULL);
    REQUIRE(buffer_get_length(sv) == 5);

    tinybuf_result_dispose(&rr);
    tinybuf_result_dispose(&w);
    tinybuf_value_free(out);
    tinybuf_value_free(s);
    buffer_free(buf);
    tinybuf_set_use_strpool(0);
}

TEST_CASE("hetero_list concatenated boxes", "[custom]")
{
    tinybuf_set_use_strpool(1);
    tinybuf_register_builtin_plugins();
    tinybuf_plugin_register_from_dll("build-vcpkg/lib/Debug/system_extend.dll");

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
    REQUIRE(w.res > 0);

    buf_ref br{buffer_get_data(buf), (int64_t)buffer_get_length(buf), buffer_get_data(buf), (int64_t)buffer_get_length(buf)};
    tinybuf_value *out = tinybuf_value_alloc();
    tinybuf_result rr2 = tinybuf_try_read_box(&br, out, NULL);
    char msgs2[128];
    tinybuf_result_format_msgs(&rr2, msgs2, sizeof(msgs2));
    CAPTURE(rr2.res, msgs2, tinybuf_last_error_message());
    REQUIRE(rr2.res > 0);
    REQUIRE(tinybuf_result_msg_count(&rr2) == 0);
    REQUIRE(tinybuf_value_get_type(out) == tinybuf_array);
    REQUIRE(tinybuf_value_get_child_size(out) == 6);
    const tinybuf_value *last = tinybuf_value_get_array_child(out, 5);
    REQUIRE(tinybuf_value_get_type(last) == tinybuf_string);

    tinybuf_result_dispose(&rr2);
    tinybuf_result_dispose(&w);
    tinybuf_value_free(out);
    buffer_free(buf);
    tinybuf_value_free(arr);
    tinybuf_set_use_strpool(0);
}
