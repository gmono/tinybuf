#include "tinybuf_private.h"
#include "tinybuf_buffer.h"
#include "tinybuf_common.h"
#include "tinybuf_plugin.h"
#include <stdio.h>

static inline void append_cstr(buffer *dst, const char *s)
{
    buffer_append(dst, s, (int)strlen(s));
}
static inline void append_int_dec(buffer *dst, int64_t v)
{
    char tmp[64];
    int n = snprintf(tmp, sizeof(tmp), "%lld", (long long)v);
    buffer_append(dst, tmp, n);
}
static inline void append_newline(buffer *dst)
{
    buffer_append(dst, "\r\n", 2);
}
static int s_dump_indent = 0;
static inline void append_indent(buffer *dst)
{
    for(int i=0;i<s_dump_indent;++i)
    {
        buffer_append(dst, "    ", 4);
    }
}

static int dump_box_text(buf_ref *buf, buffer *dst);
static int collect_box_labels(buf_ref *buf);

typedef struct { int64_t pos; int label; } dump_label;
static dump_label *s_dump_labels = NULL; static int s_dump_labels_count = 0; static int s_dump_labels_capacity = 0;
static const char *s_dump_base = NULL; static int64_t s_dump_total = 0;
static int64_t *s_dump_box_starts = NULL; static int s_dump_box_starts_count = 0; static int s_dump_box_starts_capacity = 0;

static inline void dump_labels_reset(const buf_ref *buf)
{
    s_dump_base = buf->base;
    s_dump_total = buf->all_size;
    s_dump_labels_count = 0;
    s_dump_box_starts_count = 0;
}
static inline void dump_labels_register(int64_t target_pos, int label)
{
    if(s_dump_labels_count == s_dump_labels_capacity)
    {
        int newcap = s_dump_labels_capacity ? (s_dump_labels_capacity * 2) : 16;
        s_dump_labels = (dump_label*)tinybuf_realloc(s_dump_labels, sizeof(dump_label) * newcap);
        s_dump_labels_capacity = newcap;
    }
    s_dump_labels[s_dump_labels_count].pos = target_pos;
    s_dump_labels[s_dump_labels_count].label = label;
    ++s_dump_labels_count;
}
static inline int64_t nearest_box_start(int64_t pos)
{
    int64_t best = -1;
    for(int i=0;i<s_dump_box_starts_count;++i)
    {
        int64_t v = s_dump_box_starts[i];
        if(v <= pos && v >= 0)
        {
            if(best < 0 || v > best)
            {
                best = v;
            }
        }
    }
    return best < 0 ? pos : best;
}
static inline void dump_labels_emit_prefix(int64_t cur_pos, buffer *dst)
{
    int first = 1;
    for(int i=0;i<s_dump_labels_count;++i)
    {
        if(s_dump_labels[i].pos == cur_pos)
        {
            if(first)
            {
                append_cstr(dst, "(p:");
                first = 0;
            }
            else
            {
                append_cstr(dst, ",");
            }
            append_int_dec(dst, (int64_t)s_dump_labels[i].label);
        }
    }
    if(!first)
    {
        append_cstr(dst, ") ");
    }
}

int tinybuf_dump_buffer_as_text(const char *data, int len, buffer *dst)
{
    buf_ref br;
    br.base = data;
    br.all_size = (int64_t)len;
    br.ptr = data;
    br.size = (int64_t)len;
    dump_labels_reset(&br);
    collect_box_labels(&br);
    br.ptr = data;
    br.size = (int64_t)len;
    return dump_box_text(&br, dst);
}

static int dump_string_text(buf_ref *buf, buffer *dst)
{
    QWORD slen = 0;
    int consumed = 0;
    int add = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &slen);
    if (add <= 0) return add;
    consumed += add;
    buf_offset(buf, add);

    append_cstr(dst, "\"");
    if ((int64_t)slen <= buf->size) {
        buffer_append(dst, buf->ptr, (int)slen);
        buf_offset(buf, (int)slen);
        consumed += (int)slen;
    }
    append_cstr(dst, "\"");
    return consumed;
}

static int dump_array_text(buf_ref *buf, buffer *dst)
{
    QWORD cnt = 0;
    int consumed = 0;
    int add = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &cnt);
    if (add <= 0) return add;
    consumed += add;
    buf_offset(buf, add);

    append_cstr(dst, "[");
    append_newline(dst);
    ++s_dump_indent;
    for (QWORD i = 0; i < cnt; ++i) {
        if (i) {
            append_cstr(dst, ",");
            append_newline(dst);
        }
        append_indent(dst);
        consumed += dump_box_text(buf, dst);
    }
    append_newline(dst);
    --s_dump_indent;
    append_indent(dst);
    append_cstr(dst, "]");
    return consumed;
}

static int dump_map_text(buf_ref *buf, buffer *dst)
{
    QWORD cnt = 0;
    int consumed = 0;
    int add = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &cnt);
    if (add <= 0) return add;
    consumed += add;
    buf_offset(buf, add);

    append_cstr(dst, "{");
    append_newline(dst);
    ++s_dump_indent;
    for (QWORD i = 0; i < cnt; ++i) {
        if (i) {
            append_cstr(dst, ",");
            append_newline(dst);
        }
        append_indent(dst);
        consumed += dump_string_text(buf, dst);
        append_cstr(dst, " : ");
        consumed += dump_box_text(buf, dst);
    }
    append_newline(dst);
    --s_dump_indent;
    append_indent(dst);
    append_cstr(dst, "}");
    return consumed;
}

static int dump_box_text(buf_ref *buf, buffer *dst)
{
    int consumed = 0;
    int64_t cur_pos = (int64_t)(buf->ptr - buf->base);
    dump_labels_emit_prefix(cur_pos, dst);

    serialize_type t = serialize_null;
    tinybuf_error rr = tinybuf_result_ok(0);
    int add = try_read_type(buf, &t, &rr);
    if (add <= 0) return add;
    consumed += add;

    switch (t) {
        case serialize_null:
            append_cstr(dst, "null");
            break;
        case serialize_positive_int:
        case serialize_negtive_int:
        {
            QWORD v = 0;
            int a = try_read_int_tovar(t == serialize_negtive_int, buf->ptr, (int)buf->size, &v);
            if (a <= 0) return a;
            consumed += a;
            buf_offset(buf, a);
            append_int_dec(dst, (int64_t)v);
            break;
        }
        case serialize_bool_true:
            append_cstr(dst, "true");
            break;
        case serialize_bool_false:
            append_cstr(dst, "false");
            break;
        case serialize_double:
        {
            if (buf->size < 8) return 0;
            double dv = 0;
            memcpy(&dv, buf->ptr, 8);
            buf_offset(buf, 8);
            consumed += 8;
            char tmp[64];
            int n = snprintf(tmp, sizeof(tmp), "%g", dv);
            buffer_append(dst, tmp, n);
            break;
        }
        case serialize_string:
            consumed += dump_string_text(buf, dst);
            break;
        case serialize_map:
            consumed += dump_map_text(buf, dst);
            break;
        case serialize_array:
            consumed += dump_array_text(buf, dst);
            break;
        case serialize_vector_tensor:
        {
            QWORD cnt = 0;
            int a = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &cnt);
            if (a <= 0) return a;
            buf_offset(buf, a);
            consumed += a;
            QWORD dt = 0;
            int b = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &dt);
            if (b <= 0) return b;
            buf_offset(buf, b);
            consumed += b;
            append_cstr(dst, "tensor(shape=[");
            append_int_dec(dst, (int64_t)cnt);
            append_cstr(dst, "], dtype=");
            append_int_dec(dst, (int64_t)dt);
            append_cstr(dst, ", count=");
            append_int_dec(dst, (int64_t)cnt);
            append_cstr(dst, ")");
            if ((int64_t)dt == 8) {
                int64_t need = (int64_t)cnt * 8;
                if (buf->size < need) return 0;
                buf_offset(buf, need);
                consumed += (int)need;
            } else if ((int64_t)dt == 10) {
                int64_t need = (int64_t)cnt * 4;
                if (buf->size < need) return 0;
                buf_offset(buf, need);
                consumed += (int)need;
            } else if ((int64_t)dt == 11) {
                int64_t need = ((int64_t)cnt + 7) / 8;
                if (buf->size < need) return 0;
                buf_offset(buf, (int)need);
                consumed += (int)need;
            } else {
                for (QWORD i = 0; i < cnt; ++i) {
                    serialize_type t2 = (serialize_type)buf->ptr[0];
                    buf_offset(buf, 1);
                    consumed += 1;
                    QWORD v = 0;
                    int c = try_read_int_tovar(t2 == serialize_negtive_int, buf->ptr, (int)buf->size, &v);
                    if (c <= 0) return c;
                    buf_offset(buf, c);
                    consumed += c;
                }
            }
            return consumed;
        }
        case serialize_dense_tensor:
        {
            QWORD dims = 0;
            int a = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &dims);
            if (a <= 0) return a;
            buf_offset(buf, a);
            consumed += a;
            int64_t prod = 1;
            for (QWORD i = 0; i < dims; ++i) {
                QWORD d = 0;
                int c = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &d);
                if (c <= 0) return c;
                buf_offset(buf, c);
                consumed += c;
                prod *= (int64_t)d;
            }
            QWORD dt = 0;
            int b = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &dt);
            if (b <= 0) return b;
            buf_offset(buf, b);
            consumed += b;
            append_cstr(dst, "tensor(shape=[");
            for (QWORD i = 0; i < dims; ++i) {
                if (i) append_cstr(dst, ",");
            }
            append_cstr(dst, "], dtype=");
            append_int_dec(dst, (int64_t)dt);
            append_cstr(dst, ", count=");
            append_int_dec(dst, prod);
            append_cstr(dst, ")");
            if ((int64_t)dt == 8) {
                int64_t need = prod * 8;
                if (buf->size < need) return 0;
                buf_offset(buf, need);
                consumed += (int)need;
            } else if ((int64_t)dt == 10) {
                int64_t need = prod * 4;
                if (buf->size < need) return 0;
                buf_offset(buf, need);
                consumed += (int)need;
            } else if ((int64_t)dt == 11) {
                int64_t need = (prod + 7) / 8;
                if (buf->size < need) return 0;
                buf_offset(buf, (int)need);
                consumed += (int)need;
            } else {
                for (int64_t i = 0; i < prod; ++i) {
                    serialize_type t2 = (serialize_type)buf->ptr[0];
                    buf_offset(buf, 1);
                    consumed += 1;
                    QWORD v = 0;
                    int c = try_read_int_tovar(t2 == serialize_negtive_int, buf->ptr, (int)buf->size, &v);
                    if (c <= 0) return c;
                    buf_offset(buf, c);
                    consumed += c;
                }
            }
            return consumed;
        }
        case serialize_name_idx:
        {
            QWORD idx = 0;
            int a = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &idx);
            if (a <= 0) return a;
            buf_offset(buf, a);
            consumed += a;
            QWORD blen = 0;
            int b = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &blen);
            if (b <= 0) return b;
            buf_offset(buf, b);
            consumed += b;
            const char *pool_start = s_strpool_base_read + s_strpool_offset_read;
            const char *q = pool_start;
            int64_t r = ((const char*)buf->ptr + buf->size) - pool_start;
            char *name_out = NULL;
            int name_len = 0;
            if (r > 0) {
                if ((uint8_t)q[0] == serialize_str_pool) {
                    ++q; --r;
                    QWORD cnt = 0;
                    int l2 = try_read_int_tovar(FALSE, q, (int)r, &cnt);
                    if (l2 > 0) {
                        q += l2; r -= l2;
                        for (QWORD i = 0; i < cnt; ++i) {
                            if (r < 1) break;
                            if ((uint8_t)q[0] != serialize_string) break;
                            ++q; --r;
                            QWORD sl = 0;
                            int l3 = try_read_int_tovar(FALSE, q, (int)r, &sl);
                            if (l3 <= 0) break;
                            q += l3; r -= l3;
                            if (r < (int64_t)sl) break;
                            if (i == idx) {
                                name_out = (char*)tinybuf_malloc((int)sl + 1);
                                memcpy(name_out, q, (size_t)sl);
                                name_out[sl] = '\0';
                                name_len = (int)sl;
                                break;
                            }
                            q += sl; r -= sl;
                        }
                    }
                } else if ((uint8_t)q[0] == serialize_str_trie_pool) {
                    ++q; --r;
                    QWORD ncount = 0;
                    int l2 = try_read_int_tovar(FALSE, q, (int)r, &ncount);
                    if (l2 > 0) {
                        const char *nodes_base = q + l2;
                        int64_t nodes_size = r - l2;
                        const char *p = nodes_base;
                        int64_t rr = nodes_size;
                        int found = -1;
                        for (QWORD i = 0; i < ncount; ++i) {
                            QWORD parent = 0;
                            int lp = try_read_int_tovar(FALSE, p, (int)rr, &parent);
                            if (lp <= 0) break;
                            p += lp; rr -= lp;
                            if (rr < 2) break;
                            unsigned char ch = (unsigned char)p[0];
                            unsigned char flag = (unsigned char)p[1];
                            p += 2; rr -= 2;
                            if (flag) {
                                QWORD leaf = 0;
                                int ll = try_read_int_tovar(FALSE, p, (int)rr, &leaf);
                                if (ll <= 0) break;
                                p += ll; rr -= ll;
                                if (leaf == (QWORD)idx) { found = (int)i; break; }
                            }
                        }
                        if (found >= 0) {
                            buffer *tmp = buffer_alloc();
                            int current = found;
                            while (1) {
                                const char *p2 = nodes_base;
                                int64_t rr2 = nodes_size;
                                int node_index = 0;
                                int parent_index = -1;
                                unsigned char ch = 0;
                                unsigned char flag = 0;
                                QWORD leaf = 0;
                                while (node_index <= current) {
                                    QWORD parent = 0;
                                    int lp = try_read_int_tovar(FALSE, p2, (int)rr2, &parent);
                                    p2 += lp; rr2 -= lp;
                                    if (rr2 < 2) break;
                                    ch = (unsigned char)p2[0];
                                    flag = (unsigned char)p2[1];
                                    p2 += 2; rr2 -= 2;
                                    if (flag) {
                                        int ll = try_read_int_tovar(FALSE, p2, (int)rr2, &leaf);
                                        p2 += ll; rr2 -= ll;
                                    }
                                    parent_index = (int)parent - 1;
                                    ++node_index;
                                }
                                if (ch) { buffer_append(tmp, (const char *)&ch, 1); }
                                if (parent_index < 0) break;
                                current = parent_index;
                            }
                            int tlen = buffer_get_length_inline(tmp);
                            const char *td = buffer_get_data_inline(tmp);
                            char *name_out2 = (char*)tinybuf_malloc(tlen + 1);
                            for (int i = 0; i < tlen; ++i) name_out2[i] = td[tlen - 1 - i];
                            name_out2[tlen] = '\0';
                            name_out = name_out2; name_len = tlen;
                            buffer_free(tmp);
                        }
                    }
                }
            }
            if (name_out) {
                buffer_append(dst, "custom:", 7);
                buffer_append(dst, name_out, name_len);
                tinybuf_free(name_out);
            }
            if ((int64_t)blen > buf->size) return 0;
            buf_offset(buf, (int)blen);
            consumed += (int)blen;
            break;
        }
        case serialize_bool_map:
        {
            QWORD cnt = 0;
            int a = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &cnt);
            if (a <= 0) return a;
            buf_offset(buf, a);
            consumed += a;
            int64_t need = ((int64_t)cnt + 7) / 8;
            if (buf->size < need) return 0;
            buf_offset(buf, (int)need);
            consumed += (int)need;
            append_cstr(dst, "bool_map(count=");
            append_int_dec(dst, (int64_t)cnt);
            append_cstr(dst, ")");
            return consumed;
        }
        case serialize_sparse_tensor:
        {
            QWORD dims = 0;
            int a = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &dims);
            if (a <= 0) return a;
            buf_offset(buf, a);
            consumed += a;
            for (QWORD i = 0; i < dims; ++i) {
                QWORD d = 0;
                int c = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &d);
                if (c <= 0) return c;
                buf_offset(buf, c);
                consumed += c;
            }
            QWORD dt = 0;
            int b = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &dt);
            if (b <= 0) return b;
            buf_offset(buf, b);
            consumed += b;
            QWORD k = 0;
            int e = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &k);
            if (e <= 0) return e;
            buf_offset(buf, e);
            consumed += e;
            append_cstr(dst, "tensor(sparse, entries=");
            append_int_dec(dst, (int64_t)k);
            append_cstr(dst, ")");
            for (QWORD i = 0; i < k; ++i) {
                for (QWORD j = 0; j < dims; ++j) {
                    QWORD idx = 0;
                    int c = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &idx);
                    if (c <= 0) return c;
                    buf_offset(buf, c);
                    consumed += c;
                }
                if ((int64_t)dt == 8) {
                    if (buf->size < 8) return 0;
                    buf_offset(buf, 8);
                    consumed += 8;
                } else if ((int64_t)dt == 10) {
                    if (buf->size < 4) return 0;
                    buf_offset(buf, 4);
                    consumed += 4;
                } else {
                    serialize_type t2 = (serialize_type)buf->ptr[0];
                    buf_offset(buf, 1);
                    consumed += 1;
                    QWORD v = 0;
                    int c = try_read_int_tovar(t2 == serialize_negtive_int, buf->ptr, (int)buf->size, &v);
                    if (c <= 0) return c;
                    buf_offset(buf, c);
                    consumed += c;
                }
            }
            return consumed;
        }
        case serialize_pointer_from_current_n:
        case serialize_pointer_from_start_n:
        case serialize_pointer_from_end_n:
        case serialize_pointer_from_current_p:
        case serialize_pointer_from_start_p:
        case serialize_pointer_from_end_p:
        {
            int neg = (t == serialize_pointer_from_current_n || t == serialize_pointer_from_start_n || t == serialize_pointer_from_end_n);
            QWORD mag = 0;
            int a = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &mag);
            if (a <= 0) return a;
            consumed += a;
            buf_offset(buf, a);
            int64_t off = neg ? -(int64_t)mag : (int64_t)mag;
            int64_t target_pos = 0;
            if (t == serialize_pointer_from_start_n || t == serialize_pointer_from_start_p) {
                target_pos = off;
            } else if (t == serialize_pointer_from_end_n || t == serialize_pointer_from_end_p) {
                target_pos = s_dump_total - off;
            } else {
                int64_t anchor = (int64_t)(buf->ptr - buf->base);
                target_pos = anchor + off;
            }
            if (target_pos >= 0 && target_pos <= s_dump_total) {
                int64_t adj = nearest_box_start(target_pos);
                dump_labels_register(adj, (int)adj);
                target_pos = adj;
            }
            const char *type_str = (t == serialize_pointer_from_start_n || t == serialize_pointer_from_start_p) ? "start" : (t == serialize_pointer_from_end_n || t == serialize_pointer_from_end_p) ? "end" : "current";
            append_cstr(dst, "[pointer ");
            append_cstr(dst, type_str);
            append_cstr(dst, " ");
            append_int_dec(dst, off);
            if (!(t == serialize_pointer_from_start_n || t == serialize_pointer_from_start_p)) {
                append_cstr(dst, " -> start ");
                append_int_dec(dst, target_pos);
            }
            append_cstr(dst, "]");
            break;
        }
        case serialize_version:
        {
            QWORD ver = 0;
            int a = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &ver);
            if (a <= 0) return a;
            consumed += a;
            buf_offset(buf, a);
            consumed += dump_box_text(buf, dst);
            break;
        }
        case serialize_version_list:
        {
            QWORD cnt = 0;
            int a = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &cnt);
            if (a <= 0) return a;
            consumed += a;
            buf_offset(buf, a);
            append_cstr(dst, "{""versions"":{");
            for (QWORD i = 0; i < cnt; ++i) {
                if (i) append_cstr(dst, ",");
                QWORD ver = 0;
                int b = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &ver);
                if (b <= 0) return b;
                consumed += b;
                buf_offset(buf, b);
                append_cstr(dst, "\"");
                append_int_dec(dst, (int64_t)ver);
                append_cstr(dst, "\":");
                consumed += dump_box_text(buf, dst);
            }
            append_cstr(dst, "}}");
            break;
        }
        case serialize_plugin_map_table:
        {
            QWORD cnt = 0;
            int a = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &cnt);
            if (a <= 0) return a;
            consumed += a;
            buf_offset(buf, a);
            append_cstr(dst, "{\"plugins\":[");
            for (QWORD i = 0; i < cnt; ++i) {
                if (buf->size < 1) return 0;
                uint8_t t2 = (uint8_t)buf->ptr[0];
                if (t2 != serialize_name_idx) return -1;
                buf_offset(buf, 1);
                consumed += 1;
                QWORD idx = 0;
                int l1 = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &idx);
                if (l1 <= 0) return l1;
                buf_offset(buf, l1);
                consumed += l1;
                QWORD blen = 0;
                int l2 = try_read_int_tovar(FALSE, buf->ptr, (int)buf->size, &blen);
                if (l2 <= 0) return l2;
                buf_offset(buf, l2);
                consumed += l2;
                const char *pool_start = s_strpool_base_read + s_strpool_offset_read;
                const char *q = pool_start;
                int64_t rleft = ((const char*)buf->ptr + buf->size) - pool_start;
                char *name_out = NULL;
                int name_len = 0;
                if (rleft > 0 && (uint8_t)q[0] == serialize_str_pool) {
                    ++q; --rleft;
                    QWORD scnt = 0;
                    int lsc = try_read_int_tovar(FALSE, q, (int)rleft, &scnt);
                    if (lsc > 0) {
                        q += lsc; rleft -= lsc;
                        for (QWORD si = 0; si < scnt; ++si) {
                            if (rleft < 1) break;
                            if ((uint8_t)q[0] != serialize_string) break;
                            ++q; --rleft;
                            QWORD sl = 0;
                            int lsl = try_read_int_tovar(FALSE, q, (int)rleft, &sl);
                            if (lsl <= 0) break;
                            q += lsl; rleft -= lsl;
                            if (rleft < (int64_t)sl) break;
                            if (si == idx) {
                                name_out = (char*)tinybuf_malloc((int)sl + 1);
                                memcpy(name_out, q, (size_t)sl);
                                name_out[sl] = '\0';
                                name_len = (int)sl;
                                break;
                            }
                            q += sl; rleft -= sl;
                        }
                    }
                }
                buffer_append(dst, "\"", 1);
                if (name_out) {
                    buffer_append(dst, name_out, name_len);
                    tinybuf_free(name_out);
                }
                buffer_append(dst, "\"", 1);
                if ((int64_t)blen > buf->size) return 0;
                buf_offset(buf, (int)blen);
                consumed += (int)blen;
                if (i + 1 < cnt) append_cstr(dst, ",");
            }
            append_cstr(dst, "]}");
            break;
        }
        default:
        {
            uint8_t raw = (uint8_t)t;
            tinybuf_error r1 = tinybuf_result_ok(0);
            int n1 = tinybuf_plugins_try_dump_by_tag(raw, buf, dst, &r1);
            if (n1 > 0) {
                consumed += n1;
            } else {
                append_cstr(dst, "<unknown>");
            }
            break;
        }
    }
    return consumed;
}

static int collect_string(buf_ref *br)
{
    QWORD slen = 0;
    int consumed = 0;
    int add = try_read_int_tovar(FALSE, br->ptr, (int)br->size, &slen);
    if (add <= 0) return add;
    consumed += add;
    buf_offset(br, add);
    if ((int64_t)slen <= br->size) {
        buf_offset(br, (int)slen);
        consumed += (int)slen;
    }
    return consumed;
}
static int collect_array(buf_ref *br)
{
    QWORD cnt = 0;
    int consumed = 0;
    int add = try_read_int_tovar(FALSE, br->ptr, (int)br->size, &cnt);
    if (add <= 0) return add;
    consumed += add;
    buf_offset(br, add);
    for (QWORD i = 0; i < cnt; ++i) {
        consumed += collect_box_labels(br);
    }
    return consumed;
}
static int collect_map(buf_ref *br)
{
    QWORD cnt = 0;
    int consumed = 0;
    int add = try_read_int_tovar(FALSE, br->ptr, (int)br->size, &cnt);
    if (add <= 0) return add;
    consumed += add;
    buf_offset(br, add);
    for (QWORD i = 0; i < cnt; ++i) {
        consumed += collect_string(br);
        consumed += collect_box_labels(br);
    }
    return consumed;
}

static int collect_box_labels(buf_ref *br)
{
    int consumed = 0;
    int64_t start_pos = (int64_t)(br->ptr - br->base);
    if (s_dump_box_starts_count == s_dump_box_starts_capacity) {
        int newcap = s_dump_box_starts_capacity ? (s_dump_box_starts_capacity * 2) : 32;
        s_dump_box_starts = (int64_t*)tinybuf_realloc(s_dump_box_starts, sizeof(int64_t) * newcap);
        s_dump_box_starts_capacity = newcap;
    }
    s_dump_box_starts[s_dump_box_starts_count++] = start_pos;

    serialize_type t = serialize_null;
    tinybuf_error rr = tinybuf_result_ok(0);
    int add = try_read_type(br, &t, &rr);
    if (add <= 0) return add;
    consumed += add;

    switch (t) {
        case serialize_null:
            break;
        case serialize_positive_int:
        case serialize_negtive_int:
        {
            QWORD v = 0;
            int a = try_read_int_tovar(t == serialize_negtive_int, br->ptr, (int)br->size, &v);
            if (a <= 0) return a;
            consumed += a;
            buf_offset(br, a);
            break;
        }
        case serialize_bool_true:
            break;
        case serialize_bool_false:
            break;
        case serialize_double:
        {
            if (br->size < 8) return 0;
            buf_offset(br, 8);
            consumed += 8;
            break;
        }
        case serialize_string:
            consumed += collect_string(br);
            break;
        case serialize_map:
            consumed += collect_map(br);
            break;
        case serialize_array:
            consumed += collect_array(br);
            break;
        case serialize_pointer_from_current_n:
        case serialize_pointer_from_start_n:
        case serialize_pointer_from_end_n:
        case serialize_pointer_from_current_p:
        case serialize_pointer_from_start_p:
        case serialize_pointer_from_end_p:
        {
            int neg = (t == serialize_pointer_from_current_n || t == serialize_pointer_from_start_n || t == serialize_pointer_from_end_n);
            QWORD mag = 0;
            int a = try_read_int_tovar(FALSE, br->ptr, (int)br->size, &mag);
            if (a <= 0) return a;
            consumed += a;
            buf_offset(br, a);
            int64_t off = neg ? -(int64_t)mag : (int64_t)mag;
            int64_t target_pos = 0;
            if (t == serialize_pointer_from_start_n || t == serialize_pointer_from_start_p) {
                target_pos = off;
            } else if (t == serialize_pointer_from_end_n || t == serialize_pointer_from_end_p) {
                target_pos = s_dump_total - off;
            } else {
                int64_t anchor = (int64_t)(br->ptr - br->base);
                target_pos = anchor + off;
            }
            if (target_pos >= 0 && target_pos <= s_dump_total) {
                int64_t adj = nearest_box_start(target_pos);
                dump_labels_register(adj, (int)adj);
            }
            break;
        }
        case serialize_version:
        {
            QWORD ver = 0;
            int a = try_read_int_tovar(FALSE, br->ptr, (int)br->size, &ver);
            if (a <= 0) return a;
            consumed += a;
            buf_offset(br, a);
            consumed += collect_box_labels(br);
            break;
        }
        case serialize_version_list:
        {
            QWORD cnt = 0;
            int a = try_read_int_tovar(FALSE, br->ptr, (int)br->size, &cnt);
            if (a <= 0) return a;
            consumed += a;
            buf_offset(br, a);
            for (QWORD i = 0; i < cnt; ++i) {
                QWORD ver = 0;
                int b = try_read_int_tovar(FALSE, br->ptr, (int)br->size, &ver);
                if (b <= 0) return b;
                consumed += b;
                buf_offset(br, b);
                consumed += collect_box_labels(br);
            }
            break;
        }
        case serialize_name_idx:
        {
            QWORD idx = 0;
            int a = try_read_int_tovar(FALSE, br->ptr, (int)br->size, &idx);
            if (a <= 0) return a;
            consumed += a;
            buf_offset(br, a);
            QWORD blen = 0;
            int b = try_read_int_tovar(FALSE, br->ptr, (int)br->size, &blen);
            if (b <= 0) return b;
            consumed += b;
            buf_offset(br, b);
            if (br->size < (int64_t)blen) return 0;
            buf_offset(br, (int)blen);
            consumed += (int)blen;
            break;
        }
        default:
            break;
    }
    return consumed;
}
