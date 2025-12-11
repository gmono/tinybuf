#include <catch2/catch_test_macros.hpp>
#include "tinybuf.h"
#include "tinybuf_plugin.h"
#include "dyn_sys.h"
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
    if (dw > 0)
        return dw;
    tinybuf_error er = tinybuf_result_err(dw, "dump failed", NULL);
    tinybuf_result_append_merge(r, &er, tinybuf_merger_left);
    return dw;
}

TEST_CASE("custom string via type_idx", "[custom]")
{
    tinybuf_set_use_strpool(1);
    tinybuf_init();

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
    tinybuf_init();
#ifdef _WIN32
    const char *plugin_path = "../tinybuf_plugins/system_extend.dll";
#else
    const char *plugin_path = "../tinybuf_plugins/libsystem_extend.so";
#endif
    tinybuf_plugin_register_from_dll(plugin_path);

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

TEST_CASE("trait ops over multiple types", "[oop-trait]")
{
    tinybuf_set_use_strpool(1);
    tinybuf_init();
    auto rw_read = [](const char *name, const uint8_t *data, int len, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_error *r) -> int
    { (void)name; buf_ref br{(const char *)data, (int64_t)len, (const char *)data, (int64_t)len}; return tinybuf_try_read_box(&br, out, contain_handler, r); };
    auto rw_write = [](const char *name, const tinybuf_value *in, buffer *out, tinybuf_error *r) -> int
    { (void)name; return tinybuf_try_write_box(out, in, r); };
    auto rw_dump = [](const char *name, buf_ref *buf, buffer *out, tinybuf_error *r) -> int
    { (void)name; int d = tinybuf_dump_buffer_as_text(buf->ptr, (int)buf->size, out); if(d>0) return d; tinybuf_error er = tinybuf_result_err(d, "dump failed", NULL); tinybuf_result_append_merge(r, &er, tinybuf_merger_left); return d; };
    tinybuf_oop_attach_serializers("type_a", rw_read, rw_write, rw_dump);
    tinybuf_oop_set_serializable("type_a", 1);
    tinybuf_oop_attach_serializers("type_b", rw_read, rw_write, rw_dump);
    tinybuf_oop_set_serializable("type_b", 1);
    tinybuf_oop_register_types_to_custom();
    tinybuf_oop_register_trait("seq_like");
    tinybuf_oop_trait_add_type("seq_like", "type_a");
    tinybuf_oop_trait_add_type("seq_like", "type_b");
    auto op_len = [](tinybuf_value *self, const tinybuf_value *args, tinybuf_value *out) -> int
    { (void)args; tinybuf_error cr = tinybuf_result_ok(0); int n = tinybuf_value_get_child_size(self, &cr); tinybuf_value_init_int(out, n); return 0; };
    auto op_append = [](tinybuf_value *self, const tinybuf_value *args, tinybuf_value *out) -> int
    { tinybuf_value_clear(out); tinybuf_error cr = tinybuf_result_ok(0); int n = tinybuf_value_get_child_size(self, &cr); for(int i=0;i<n;++i){ tinybuf_error rr = tinybuf_result_ok(0); const tinybuf_value *ch = tinybuf_value_get_array_child(self, i, &rr); int wn = try_write_box(NULL, ch, NULL); (void)wn; tinybuf_value *cp = tinybuf_value_alloc(); buffer *tmp = buffer_alloc(); tinybuf_error w = tinybuf_result_ok(0); int wl = tinybuf_try_write_box(tmp, ch, &w); buf_ref br{buffer_get_data(tmp), (int64_t)buffer_get_length(tmp), buffer_get_data(tmp), (int64_t)buffer_get_length(tmp)}; tinybuf_error r = tinybuf_result_ok(0); tinybuf_try_read_box(&br, cp, NULL, &r); buffer_free(tmp); tinybuf_value_array_append(out, cp);} if(args){ tinybuf_error rr2 = tinybuf_result_ok(0); const tinybuf_value *extra = tinybuf_value_get_array_child(args, 0, &rr2); if(extra){ tinybuf_value *cp2 = tinybuf_value_alloc(); buffer *tmp2 = buffer_alloc(); tinybuf_error w2 = tinybuf_result_ok(0); int wl2 = tinybuf_try_write_box(tmp2, extra, &w2); (void)wl2; buf_ref br2{buffer_get_data(tmp2), (int64_t)buffer_get_length(tmp2), buffer_get_data(tmp2), (int64_t)buffer_get_length(tmp2)}; tinybuf_error r2 = tinybuf_result_ok(0); tinybuf_try_read_box(&br2, cp2, NULL, &r2); buffer_free(tmp2); tinybuf_value_array_append(out, cp2);} } return 0; };
    tinybuf_oop_trait_register_op("seq_like", "length", "(list)->int", "len", op_len);
    tinybuf_oop_trait_register_op("seq_like", "append_one", "(list,value)->list", "append", op_append);
    tinybuf_value *arr = tinybuf_value_alloc();
    for (int i = 0; i < 3; ++i)
    {
        tinybuf_value *v = tinybuf_value_alloc();
        tinybuf_value_init_int(v, i);
        tinybuf_value_array_append(arr, v);
    }
    tinybuf_value *out = arr;
    tinybuf_value *lenv = tinybuf_value_alloc();
    int dop = tinybuf_oop_do_trait_op("seq_like", "length", "type_a", out, NULL, lenv);
    REQUIRE(dop == 0);
    tinybuf_error gri = tinybuf_result_ok(0);
    REQUIRE(tinybuf_value_get_int(lenv, &gri) == 3);
    tinybuf_value *arg = tinybuf_value_alloc();
    tinybuf_value *one = tinybuf_value_alloc();
    tinybuf_value_init_int(one, 9);
    tinybuf_value_array_append(arg, one);
    tinybuf_value *app = tinybuf_value_alloc();
    REQUIRE(tinybuf_oop_do_trait_op("seq_like", "append_one", "type_a", out, arg, app) == 0);
    tinybuf_error crx = tinybuf_result_ok(0);
    REQUIRE(tinybuf_value_get_child_size(app, &crx) == 4);
    tinybuf_value_free(app);
    tinybuf_value_free(arg);
    tinybuf_value_free(one);
    tinybuf_value_free(lenv);
    tinybuf_value_free(out);
    buffer_free(buf);
    tinybuf_value_free(arr);
    tinybuf_set_use_strpool(0);
}
