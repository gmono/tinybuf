#include "tinybuf_private.h"
#include "tinybuf_buffer.h"
#include "tinybuf_plugin.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <stdatomic.h>
#include <time.h>
#include <sched.h>
#endif

static inline void pool_lock(void);
static inline void pool_unlock(void);
static inline BOOL is_simple_pointer_type(serialize_type type);
static inline bool is_pointer_neg(serialize_type type);
static inline enum offset_type get_offset_type(serialize_type type);

const char *buf_end_ptr(buf_ref *buf)
{
    return buf->base + buf->all_size;
}
int64_t buf_current_offset(buf_ref *buf)
{
    return buf->ptr - buf->base;
}
static inline BOOL validate_buf(buf_ref *buf)
{
    return buf->base <= buf->ptr &&
           buf->size <= buf->all_size &&
           buf->all_size + buf->base == buf->ptr + buf->size;
}
static inline BOOL buf_ptr_ok(buf_ref *buf)
{
    return buf->ptr >= buf->base && buf->ptr < buf->base + buf->all_size;
}
static inline void maybe_validate(buf_ref *buf)
{
#ifdef ENABLE_STRICT_VALIDATE
    assert(validate_buf(buf));
#endif
}
int buf_offset(buf_ref *buf, int64_t offset)
{
    const char *temp = buf->ptr;
    buf->ptr += offset;
    if (!buf_ptr_ok(buf))
    {
        buf->ptr = temp;
        return -1;
    }
    buf->size -= offset;
    maybe_validate(buf);
    return 0;
}

int try_read_type(buf_ref *buf, serialize_type *type)
{
    if (buf->size < 1)
    {
        return 0;
    }
    *type = buf->ptr[0];
    buf_offset(buf, 1);
    return 1;
}

void pointer_to_start(const buf_ref *buf, pointer_value *ptr)
{
    switch (ptr->type)
    {
    case start:
        break;
    case end:
        ptr->offset = buf->all_size - ptr->offset;
        ptr->type = start;
        break;
    case current:
        ptr->offset = buf_current_offset(buf) + ptr->offset;
        ptr->type = start;
        break;
    default:
        break;
    }
    maybe_validate((buf_ref *)buf);
}

typedef struct { int len; const char *reason; } read_result;
#define RESULT_OK(x) (x.len > 0)

typedef struct { int64_t offset; tinybuf_value *value; int complete; } offset_pool_entry;
static const char *s_pool_base = NULL;
static offset_pool_entry *s_pool = NULL;
static int s_pool_count = 0;
static int s_pool_capacity = 0;

static inline void pool_reset(const buf_ref *buf)
{
    if (s_pool_base != buf->base)
    {
        if (s_pool)
        {
            tinybuf_free(s_pool);
            s_pool = NULL;
            s_pool_capacity = 0;
        }
        s_pool_base = buf->base;
    }
    s_pool_count = 0;
}

const char *tinybuf_last_error_message(void);

static inline offset_pool_entry *pool_find(int64_t offset)
{
    pool_lock();
    for (int i = 0; i < s_pool_count; ++i)
    {
        if (s_pool[i].offset == offset)
        {
            offset_pool_entry *ret = &s_pool[i];
            pool_unlock();
            return ret;
        }
    }
    pool_unlock();
    return NULL;
}

static offset_pool_entry *pool_register(int64_t offset, tinybuf_value *value)
{
    pool_lock();
    offset_pool_entry *e = NULL;
    for (int i = 0; i < s_pool_count; ++i)
    {
        if (s_pool[i].offset == offset)
        {
            e = &s_pool[i];
            break;
        }
    }
    if (e)
    {
        if (value)
        {
            e->value = value;
        }
        pool_unlock();
        return e;
    }
    if (s_pool_count == s_pool_capacity)
    {
        int newcap = s_pool_capacity ? (s_pool_capacity * 2) : 16;
        s_pool = (offset_pool_entry *)tinybuf_realloc(s_pool, sizeof(offset_pool_entry) * newcap);
        s_pool_capacity = newcap;
    }
    s_pool[s_pool_count].offset = offset;
    s_pool[s_pool_count].value = value;
    s_pool[s_pool_count].complete = 0;
    offset_pool_entry *ret = &s_pool[s_pool_count++];
    pool_unlock();
    return ret;
}

static inline void pool_mark_complete(int64_t offset)
{
    pool_lock();
    offset_pool_entry *e = NULL;
    for (int i = 0; i < s_pool_count; ++i)
    {
        if (s_pool[i].offset == offset)
        {
            e = &s_pool[i];
            break;
        }
    }
    if (e)
    {
        e->complete = 1;
    }
    pool_unlock();
}

static inline void set_out_ref(tinybuf_value *out, tinybuf_value *target)
{
    out->_type = tinybuf_value_ref;
    out->_data._ref = target;
}
static inline void set_out_deref(tinybuf_value *out, const tinybuf_value *target)
{
    tinybuf_value *clone = tinybuf_value_clone(target);
    tinybuf_value_clear(out);
    memcpy(out, clone, sizeof(tinybuf_value));
    tinybuf_free(clone);
}
static inline void set_out_by_mode(tinybuf_value *out, tinybuf_value *target, int deref)
{
    if (deref)
        set_out_deref(out, target);
    else
        set_out_ref(out, target);
}

int _read_box_by_offset(buf_ref *buf, int64_t offset, tinybuf_value *out, CONTAIN_HANDLER contain_handler)
{
    INIT_STATE
    const char *cur = buf->ptr;
    int64_t cursize = buf->size;
    buf->ptr = buf->base + offset;
    buf->size = buf->all_size - offset;
    pool_register(offset, out);
    if (OK_AND_ADDTO(try_read_box(buf, out, contain_handler), &len))
    {
        pool_mark_complete(offset);
        SET_SUCCESS();
    }
    else
        SET_FAILED("read box error");
    CHECK_FAILED
    buf->ptr = cur;
    buf->size = cursize;
    READ_RETURN
}

int read_box_by_pointer(buf_ref *buf, pointer_value pointer, tinybuf_value *out, CONTAIN_HANDLER contain_handler)
{
    static int s_read_pointer_depth = 0;
    s_read_pointer_depth++;
    int len = -1;
    if (s_read_pointer_depth <= 64)
    {
        pointer_to_start(buf, &pointer);
        assert(pointer.type == start);
        len = _read_box_by_offset(buf, pointer.offset, out, contain_handler);
    }
    s_read_pointer_depth--;
    return len;
}

inline int try_read_int_data(BOOL isneg, buf_ref *buf, QWORD *out)
{
    INIT_STATE
    int temp = 0;
    if (OK_AND_ADDTO(try_read_int_tovar(isneg, buf->ptr, buf->size, out), &temp))
    {
        len += temp;
        buf_offset(buf, temp);
    }
    else
        SET_FAILED("read data error");
    CHECK_FAILED
    READ_RETURN
}

inline int try_read_intbox(buf_ref *buf, QWORD *saveptr)
{
    serialize_type type;
    INIT_STATE
    if (OK_AND_ADDTO(try_read_type(buf, &type), &len))
    {
        if (type == serialize_positive_int || type == serialize_negtive_int)
        {
            BOOL isneg = type == serialize_negtive_int;
            if (OK_AND_ADDTO(try_read_int_data(isneg, buf, saveptr), &len))
            {
                SET_SUCCESS();
            }
            else
                SET_FAILED("read int error");
        }
        else
            SET_FAILED("type error");
    }
    else
        SET_FAILED("read type error");
    CHECK_FAILED
    READ_RETURN
}

inline int try_read_pointer_value(buf_ref *buf, QWORD *saveptr)
{
    INIT_STATE
    serialize_type type;
    if (OK_AND_ADDTO(try_read_type(buf, &type), &len))
    {
        if (is_simple_pointer_type(type))
        {
            if (OK_AND_ADDTO(try_read_int_data(FALSE, buf, saveptr), &len))
            {
                SET_SUCCESS();
            }
            else
                SET_FAILED("read pointer error");
        }
        else
            SET_FAILED("type error : not simple pointer");
    }
    else
        SET_FAILED("read type error");
    CHECK_FAILED
    READ_RETURN
}

inline int try_read_pointer(buf_ref *buf, pointer_value *pointer)
{
    INIT_STATE
    serialize_type type;
    if (OK_AND_ADDTO(try_read_type(buf, &type), &len))
    {
        if (is_simple_pointer_type(type))
        {
            QWORD offset;
            bool isneg = is_pointer_neg(type);
            if (OK_AND_ADDTO(try_read_int_data(isneg, buf, &offset), &len))
            {
                pointer->offset = (int64_t)offset;
                pointer->type = get_offset_type(type);
                SET_SUCCESS();
            }
            else
                SET_FAILED("read pointer error");
        }
        else
            SET_FAILED("type error : not simple pointer");
    }
    else
        SET_FAILED("read type error");
    CHECK_FAILED
    READ_RETURN
}

tinybuf_result tinybuf_try_read_box_with_mode(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler, tinybuf_read_pointer_mode mode)
{
    return tinybuf_try_read_box(buf, out, contain_handler);
}

tinybuf_result tinybuf_try_read_box(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler)
{
    if (buf->size >= 1 && (uint8_t)buf->ptr[0] == serialize_str_pool_table)
    {
        buf_ref hb = *buf;
        serialize_type t;
        int c = try_read_type(&hb, &t);
        if (c > 0 && t == serialize_str_pool_table)
        {
            QWORD off;
            int c2 = try_read_int_data(FALSE, &hb, &off);
            if (c2 > 0){ s_strpool_offset_read = (int64_t)off; buf_offset(buf, c + c2); }
        }
    }
    tinybuf_result rr = tinybuf_result_err(-1, "tinybuf_try_read_box failed", NULL);
    tinybuf_result_add_msg_const(&rr, "tinybuf_try_read_box_r");
    buf_ref raw = *buf;
    int r2 = tinybuf_value_deserialize(raw.ptr, (int)raw.size, out);
    if (r2 > 0)
    {
        buf_offset(buf, r2);
        return tinybuf_result_ok(r2);
    }
    if (buf && buf->size > 1 && (uint8_t)buf->ptr[0] == serialize_type_idx)
    {
        int r3 = tinybuf_value_deserialize(buf->ptr, (int)buf->size, out);
        if (r3 > 0)
        {
            buf_offset(buf, r3);
            return tinybuf_result_ok(r3);
        }
    }
    return rr;
}

int try_read_box(buf_ref *buf, tinybuf_value *out, CONTAIN_HANDLER contain_handler)
{
    if (buf->size >= 1 && (uint8_t)buf->ptr[0] == serialize_str_pool_table)
    {
        buf_ref hb = *buf;
        serialize_type t;
        int c = try_read_type(&hb, &t);
        if (c > 0 && t == serialize_str_pool_table)
        {
            QWORD off;
            int c2 = try_read_int_data(FALSE, &hb, &off);
            if (c2 > 0){ s_strpool_offset_read = (int64_t)off; buf_offset(buf, c + c2); }
        }
    }
    buf_ref raw = *buf;
    int r2 = tinybuf_value_deserialize(raw.ptr, (int)raw.size, out);
    if (r2 > 0)
    {
        buf_offset(buf, r2);
        return r2;
    }
    if (buf && buf->size > 1 && (uint8_t)buf->ptr[0] == serialize_type_idx)
    {
        int r3 = tinybuf_value_deserialize(buf->ptr, (int)buf->size, out);
        if (r3 > 0)
        {
            buf_offset(buf, r3);
            return r3;
        }
    }
    return -1;
}
static inline void pool_lock(void)
{
#ifdef _WIN32
    static volatile LONG s_pool_lock_var = 0;
    int spins = 0;
    while (InterlockedCompareExchange(&s_pool_lock_var, 1, 0) != 0)
    {
        if (spins < 64) { ++spins; }
        else if (spins < 1024) { Sleep(0); ++spins; }
        else { Sleep(1); }
    }
#else
    static atomic_flag s_pool_lock_var = ATOMIC_FLAG_INIT;
    int spins = 0;
    while (atomic_flag_test_and_set(&s_pool_lock_var))
    {
        if (spins < 64) { ++spins; }
        else if (spins < 1024) { sched_yield(); ++spins; }
        else { struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL); }
    }
#endif
}
static inline void pool_unlock(void)
{
#ifdef _WIN32
    static volatile LONG s_pool_lock_var = 0;
    InterlockedExchange(&s_pool_lock_var, 0);
#else
    static atomic_flag s_pool_lock_var = ATOMIC_FLAG_INIT;
    atomic_flag_clear(&s_pool_lock_var);
#endif
}
static inline int OK_AND_ADDTO(int x, int *s){ if(x>0){ *s += x; return 1;} return 0; }
static inline BOOL is_simple_pointer_type(serialize_type type){
    return type == serialize_pointer_from_current_n ||
           type == serialize_pointer_from_start_n ||
           type == serialize_pointer_from_end_n ||
           type == serialize_pointer_from_current_n ||
           type == serialize_pointer_from_start_p ||
           type == serialize_pointer_from_end_p ||
           type == serialize_pointer_from_current_p;
}
static inline bool is_pointer_neg(serialize_type type){
    return type == serialize_pointer_from_current_n ||
           type == serialize_pointer_from_start_n ||
           type == serialize_pointer_from_end_n;
}
static inline enum offset_type get_offset_type(serialize_type type){
    switch(type){
        case serialize_pointer_from_current_n:
        case serialize_pointer_from_current_p: return current;
        case serialize_pointer_from_start_n:
        case serialize_pointer_from_start_p: return start;
        case serialize_pointer_from_end_n:
        case serialize_pointer_from_end_p: return end;
        default: return start;
    }
}
