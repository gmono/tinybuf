#include "tinybuf_private.h"
#include "tinybuf_buffer.h"

typedef struct { const tinybuf_value *value; buffer *stream; int64_t start; } precache_entry;
static precache_entry *s_precache = NULL;
static int s_precache_count = 0;
static int s_precache_capacity = 0;
static buffer *s_precache_stream = NULL;
static int s_precache_redirect = 0; // 当为1时，序列化遇到已注册对象则输出指针而非内容

static inline void precache_reset(buffer *out)
{
    s_precache_stream = out;
    s_precache_count = 0;
}

static inline int64_t precache_find_start(buffer *out, const tinybuf_value *value)
{
    if(out != s_precache_stream)
    {
        return -1;
    }
    for(int i=0;i<s_precache_count;++i)
    {
        if(s_precache[i].value == value)
        {
            return s_precache[i].start;
        }
    }
    return -1;
}

static inline void precache_add(buffer *out, const tinybuf_value *value, int64_t start)
{
    if(out != s_precache_stream)
    {
        s_precache_stream = out;
        s_precache_count = 0;
    }
    for(int i=0;i<s_precache_count;++i)
    {
        if(s_precache[i].value == value)
        {
            s_precache[i].start = start;
            return;
        }
    }
    if(s_precache_count == s_precache_capacity)
    {
        int newcap = s_precache_capacity ? s_precache_capacity*2 : 16;
        s_precache = (precache_entry*)tinybuf_realloc(s_precache, sizeof(precache_entry)*newcap);
        s_precache_capacity = newcap;
    }
    s_precache[s_precache_count].value = value;
    s_precache[s_precache_count].stream = out;
    s_precache[s_precache_count].start = start;
    ++s_precache_count;
}

void tinybuf_precache_reset(buffer *out)
{
    precache_reset(out);
}

int64_t tinybuf_precache_register(buffer *out, const tinybuf_value *value)
{
    int64_t start = (int64_t)buffer_get_length_inline(out);
    precache_add(out, value, start);
    int old = s_precache_redirect; s_precache_redirect = 0; // 禁用重定向，写入真实内容
    int before = buffer_get_length_inline(out);
    tinybuf_value_serialize(value, out);
    int after = buffer_get_length_inline(out);
    s_precache_redirect = old;
    return (after - before) > 0 ? start : -1;
}

void tinybuf_precache_set_redirect(int enable)
{
    s_precache_redirect = (enable != 0);
}

int tinybuf_precache_is_redirect(void)
{
    return s_precache_redirect;
}
int64_t tinybuf_precache_find_start_for(buffer *out, const tinybuf_value *value)
{
    return precache_find_start(out, value);
}
