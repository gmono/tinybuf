#include "tinybuf_support.h"

hole_value *hole_value_new(void) {
    hole_value *p = (hole_value *)tinybuf_malloc(sizeof(hole_value));
    if (p) memset(p, 0, sizeof(hole_value));
    return p;
}

hole_string *hole_string_new(void) {
    hole_string *s = (hole_string *)tinybuf_malloc(sizeof(hole_string));
    if (s) memset(s, 0, sizeof(hole_string));
    return s;
}

void hole_string_append_node(hole_string *s, hole_value *n) {
    if (!s || !n) return;
    n->next = NULL;
    if (!s->head) {
        s->head = s->tail = n;
    } else {
        s->tail->next = n;
        s->tail = n;
    }
    s->count++;
}

void hole_string_append_cstr(hole_string *s, const char *str, int len, void (*deleter)(void *)) {
    if (!s || !str) return;
    hole_value *n = hole_value_new();
    if (!n) return;
    n->tpid = 0;
    n->data.ptr = str;
    n->len = (len > 0) ? len : (int)strlen(str);
    n->deleter = deleter;
    hole_string_append_node(s, n);
}

void hole_string_append_char(hole_string *s, uint8_t ch) {
    if (!s) return;
    hole_value *n = hole_value_new();
    if (!n) return;
    n->tpid = 2;
    n->data.value_u8 = ch;
    n->len = 1;
    hole_string_append_node(s, n);
}

void hole_string_append_sub(hole_string *s, hole_string *sub) {
    if (!s || !sub) return;
    hole_value *n = hole_value_new();
    if (!n) return;
    n->tpid = 3;
    n->data.sub_ptr = sub;
    n->len = 0;
    hole_string_append_node(s, n);
}

void hole_string_append_i16(hole_string *s, int16_t v) {
    if (!s) return;
    hole_value *n = hole_value_new();
    if (!n) return;
    n->tpid = 5;
    n->data.value_i16 = v;
    n->len = 0;
    hole_string_append_node(s, n);
}

void hole_string_append_u16(hole_string *s, uint16_t v) {
    if (!s) return;
    hole_value *n = hole_value_new();
    if (!n) return;
    n->tpid = 6;
    n->data.value_u16 = v;
    n->len = 0;
    hole_string_append_node(s, n);
}

void hole_string_append_i32(hole_string *s, int32_t v) {
    if (!s) return;
    hole_value *n = hole_value_new();
    if (!n) return;
    n->tpid = 7;
    n->data.value_i32 = v;
    n->len = 0;
    hole_string_append_node(s, n);
}

void hole_string_append_u32(hole_string *s, uint32_t v) {
    if (!s) return;
    hole_value *n = hole_value_new();
    if (!n) return;
    n->tpid = 8;
    n->data.value_u32 = v;
    n->len = 0;
    hole_string_append_node(s, n);
}

void hole_string_append_i64(hole_string *s, int64_t v) {
    if (!s) return;
    hole_value *n = hole_value_new();
    if (!n) return;
    n->tpid = 9;
    n->data.value_i64 = v;
    n->len = 0;
    hole_string_append_node(s, n);
}

void hole_string_append_u64(hole_string *s, uint64_t v) {
    if (!s) return;
    hole_value *n = hole_value_new();
    if (!n) return;
    n->tpid = 10;
    n->data.value_u64 = v;
    n->len = 0;
    hole_string_append_node(s, n);
}

void hole_string_append_f32(hole_string *s, float v) {
    if (!s) return;
    hole_value *n = hole_value_new();
    if (!n) return;
    n->tpid = 11;
    n->data.value_f32 = v;
    n->len = 0;
    hole_string_append_node(s, n);
}

void hole_string_append_bytes(hole_string *s, const void *bytes, int len, void (*deleter)(void *)) {
    if (!s || !bytes || len <= 0) return;
    hole_value *n = hole_value_new();
    if (!n) return;
    n->tpid = 4;
    n->data.bytes_ptr = bytes;
    n->len = len;
    n->deleter = deleter;
    hole_string_append_node(s, n);
}

int hole_string_calc_len(const hole_string *s) {
    int total = 0;
    const hole_value *p = s ? s->head : NULL;
    for (; p; p = p->next) {
        if (p->tpid == 0) total += p->len;
        else if (p->tpid == 1 || p->tpid == 2) total += 1;
        else if (p->tpid == 4) total += p->len;
        else if (p->tpid == 5) {
            char tmp[64];
            int n = snprintf(tmp, sizeof(tmp), "%hd", p->data.value_i16);
            total += n > 0 ? n : 0;
        } else if (p->tpid == 6) {
            char tmp[64];
            int n = snprintf(tmp, sizeof(tmp), "%hu", p->data.value_u16);
            total += n > 0 ? n : 0;
        } else if (p->tpid == 7) {
            char tmp[64];
            int n = snprintf(tmp, sizeof(tmp), "%d", p->data.value_i32);
            total += n > 0 ? n : 0;
        } else if (p->tpid == 8) {
            char tmp[64];
            int n = snprintf(tmp, sizeof(tmp), "%u", p->data.value_u32);
            total += n > 0 ? n : 0;
        } else if (p->tpid == 9) {
            char tmp[64];
            int n = snprintf(tmp, sizeof(tmp), "%lld", (long long)p->data.value_i64);
            total += n > 0 ? n : 0;
        } else if (p->tpid == 10) {
            char tmp[64];
            int n = snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)p->data.value_u64);
            total += n > 0 ? n : 0;
        } else if (p->tpid == 11) {
            char tmp[64];
            int n = snprintf(tmp, sizeof(tmp), "%g", (double)p->data.value_f32);
            total += n > 0 ? n : 0;
        } else if (p->tpid == 12) {
            char tmp[64];
            int n = snprintf(tmp, sizeof(tmp), "%g", p->data.value_f64);
            total += n > 0 ? n : 0;
        }
        else if (p->tpid == 3 && p->data.sub_ptr) total += hole_string_calc_len(p->data.sub_ptr);
    }
    return total;
}

void hole_string_copy_to(char *dst, int *off, const hole_string *s) {
    const hole_value *p = s ? s->head : NULL;
    for (; p; p = p->next) {
        if (p->tpid == 0 && p->data.ptr) {
            memcpy(dst + *off, p->data.ptr, p->len);
            *off += p->len;
        } else if (p->tpid == 1) {
            dst[(*off)++] = (char)p->data.value_i8;
        } else if (p->tpid == 2) {
            dst[(*off)++] = (char)p->data.value_u8;
        } else if (p->tpid == 4 && p->data.bytes_ptr) {
            memcpy(dst + *off, p->data.bytes_ptr, p->len);
            *off += p->len;
        } else if (p->tpid == 5) {
            char tmp[64];
            int n = snprintf(tmp, sizeof(tmp), "%hd", p->data.value_i16);
            if (n > 0) { memcpy(dst + *off, tmp, n); *off += n; }
        } else if (p->tpid == 6) {
            char tmp[64];
            int n = snprintf(tmp, sizeof(tmp), "%hu", p->data.value_u16);
            if (n > 0) { memcpy(dst + *off, tmp, n); *off += n; }
        } else if (p->tpid == 7) {
            char tmp[64];
            int n = snprintf(tmp, sizeof(tmp), "%d", p->data.value_i32);
            if (n > 0) { memcpy(dst + *off, tmp, n); *off += n; }
        } else if (p->tpid == 8) {
            char tmp[64];
            int n = snprintf(tmp, sizeof(tmp), "%u", p->data.value_u32);
            if (n > 0) { memcpy(dst + *off, tmp, n); *off += n; }
        } else if (p->tpid == 9) {
            char tmp[64];
            int n = snprintf(tmp, sizeof(tmp), "%lld", (long long)p->data.value_i64);
            if (n > 0) { memcpy(dst + *off, tmp, n); *off += n; }
        } else if (p->tpid == 10) {
            char tmp[64];
            int n = snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)p->data.value_u64);
            if (n > 0) { memcpy(dst + *off, tmp, n); *off += n; }
        } else if (p->tpid == 11) {
            char tmp[64];
            int n = snprintf(tmp, sizeof(tmp), "%g", (double)p->data.value_f32);
            if (n > 0) { memcpy(dst + *off, tmp, n); *off += n; }
        } else if (p->tpid == 12) {
            char tmp[64];
            int n = snprintf(tmp, sizeof(tmp), "%g", p->data.value_f64);
            if (n > 0) { memcpy(dst + *off, tmp, n); *off += n; }
        } else if (p->tpid == 3 && p->data.sub_ptr) {
            hole_string_copy_to(dst, off, p->data.sub_ptr);
        }
    }
}

tinybuf_str hole_string_get(const hole_string *s) {
    int total = hole_string_calc_len(s);
    char *buf = (char *)tinybuf_malloc(total + 1);
    if (!buf) {
        tinybuf_str r; r.ptr = NULL; r.deleter = NULL; return r;
    }
    int off = 0;
    hole_string_copy_to(buf, &off, s);
    buf[total] = '\0';
    tinybuf_str r; r.ptr = buf; r.deleter = (tinybuf_deleter_fn)tinybuf_free; return r;
}

void hole_string_clear(hole_string *s) {
    hole_value *p = s ? s->head : NULL;
    while (p) {
        hole_value *n = p->next;
        if (p->tpid == 0 && p->data.ptr && p->deleter) p->deleter((void *)p->data.ptr);
        if (p->tpid == 4 && p->data.bytes_ptr && p->deleter) p->deleter((void *)p->data.bytes_ptr);
        if (p->tpid == 3 && p->data.sub_ptr) hole_string_clear(p->data.sub_ptr);
        tinybuf_free(p);
        p = n;
    }
    if (s) { s->head = s->tail = NULL; s->count = 0; }
}

void hole_string_free(hole_string *s) {
    if (!s) return;
    hole_string_clear(s);
    tinybuf_free(s);
}

