#ifndef TINYBUF_SUPPORT_H
#define TINYBUF_SUPPORT_H
typedef struct hole_string hole_string;
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "tinybuf_common.h"
#include "tinybuf_memory.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct hole_value {
    union {
        const char *ptr;
        hole_string *sub_ptr;
        int8_t value_i8;
        uint8_t value_u8;
        int16_t value_i16;
        uint16_t value_u16;
        int32_t value_i32;
        uint32_t value_u32;
        int64_t value_i64;
        uint64_t value_u64;
        float value_f32;
        double value_f64;
        const void *bytes_ptr;
    } data;
    int tpid;
    int len;
    struct hole_value *next;
    void (*deleter)(void *);
} hole_value;

typedef struct hole_string {
    struct hole_value *head;
    struct hole_value *tail;
    size_t count;
} hole_string;

hole_value *hole_value_new(void);
hole_string *hole_string_new(void);
void hole_string_append_node(hole_string *s, hole_value *n);
void hole_string_append_cstr(hole_string *s, const char *str, int len, void (*deleter)(void *));
void hole_string_append_char(hole_string *s, uint8_t ch);
void hole_string_append_sub(hole_string *s, hole_string *sub);
void hole_string_append_i16(hole_string *s, int16_t v);
void hole_string_append_u16(hole_string *s, uint16_t v);
void hole_string_append_i32(hole_string *s, int32_t v);
void hole_string_append_u32(hole_string *s, uint32_t v);
void hole_string_append_i64(hole_string *s, int64_t v);
void hole_string_append_u64(hole_string *s, uint64_t v);
void hole_string_append_f32(hole_string *s, float v);

static inline void hole_string_append_f64(hole_string *s, double v) {
    if (!s) return;
    hole_value *n = hole_value_new();
    if (!n) return;
    n->tpid = 12;
    n->data.value_f64 = v;
    n->len = 0;
    hole_string_append_node(s, n);
}

void hole_string_append_bytes(hole_string *s, const void *bytes, int len, void (*deleter)(void *));

int hole_string_calc_len(const hole_string *s);

void hole_string_copy_to(char *dst, int *off, const hole_string *s);

tinybuf_str hole_string_get(const hole_string *s);

void hole_string_clear(hole_string *s);
void hole_string_free(hole_string *s);

#ifdef __cplusplus
}
#endif

#endif

