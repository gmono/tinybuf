#include "tinybuf.h"
#include "tinybuf_buffer.h"
#include "tinybuf_memory.h"
#include "tinybuf_private.h"

typedef struct { buffer *buf; } strpool_entry;
static strpool_entry *s_strpool = NULL;
static int s_strpool_count = 0;
static int s_strpool_capacity = 0;
static const char *s_strpool_base = NULL;
int s_use_strpool = 0;
static int s_use_strpool_trie = 1;

void strpool_reset_write(const buffer *out)
{
    s_strpool_count = 0;
    s_strpool_base = (const char*)out;
}

static inline int strpool_find(const char *data, int len)
{
    for(int i=0;i<s_strpool_count;++i)
    {
        int l = buffer_get_length_inline(s_strpool[i].buf);
        if(l==len && memcmp(buffer_get_data_inline(s_strpool[i].buf), data, len)==0)
        {
            return i;
        }
    }
    return -1;
}

int strpool_add(const char *data, int len)
{
    int idx = strpool_find(data, len);
    if(idx>=0) return idx;
    if(s_strpool_count==s_strpool_capacity)
    {
        int newcap = s_strpool_capacity? s_strpool_capacity*2:16;
        s_strpool = (strpool_entry*)tinybuf_realloc(s_strpool, sizeof(strpool_entry)*newcap);
        s_strpool_capacity = newcap;
    }
    buffer *b = buffer_alloc();
    buffer_assign(b, data, len);
    s_strpool[s_strpool_count].buf = b;
    return s_strpool_count++;
}

typedef struct { int parent; unsigned char ch; int is_leaf; int leaf_id; } trie_node;

tinybuf_error strpool_write_tail(buffer *out)
{
    if(!s_use_strpool || s_strpool_count==0) return tinybuf_result_ok(0);
    if(!s_use_strpool_trie)
    {
        int len = 0;
        tinybuf_error r1 = try_write_type(out, serialize_str_pool); if (r1.res <= 0) return r1; len += r1.res;
        tinybuf_error r2 = try_write_int_data(0, out, (uint64_t)s_strpool_count); if (r2.res <= 0) return r2; len += r2.res;
        for(int i=0;i<s_strpool_count;++i)
        {
            int sl = buffer_get_length_inline(s_strpool[i].buf);
            tinybuf_error r3 = try_write_type(out, serialize_string); if (r3.res <= 0) return r3; len += r3.res;
            tinybuf_error r4 = try_write_int_data(0, out, (uint64_t)sl); if (r4.res <= 0) return r4; len += r4.res;
            if(sl){ buffer_append(out, buffer_get_data_inline(s_strpool[i].buf), sl); }
        }
        return tinybuf_result_ok(len);
    }
    int before = buffer_get_length_inline(out);
    { tinybuf_error r0 = try_write_type(out, 27); if (r0.res <= 0) return r0; } /* trie pool */
    trie_node *nodes = NULL;
    int ncount = 1;
    nodes = (trie_node*)tinybuf_malloc(sizeof(trie_node)*1);
    nodes[0].parent=-1; nodes[0].ch=0; nodes[0].is_leaf=0; nodes[0].leaf_id=-1;
    for(int i=0;i<s_strpool_count;++i)
    {
        const char *p = buffer_get_data_inline(s_strpool[i].buf);
        int sl = buffer_get_length_inline(s_strpool[i].buf);
        int cur = 0;
        for(int k=0;k<sl;++k)
        {
            unsigned char c = (unsigned char)p[k];
            int found=-1;
            for(int j=1;j<ncount;++j)
            {
                if(nodes[j].parent==cur && nodes[j].ch==c){ found=j; break; }
            }
            if(found<0)
            {
                nodes = (trie_node*)tinybuf_realloc(nodes, sizeof(trie_node)*(ncount+1));
                nodes[ncount].parent=cur; nodes[ncount].ch=c; nodes[ncount].is_leaf=0; nodes[ncount].leaf_id=-1;
                found = ncount;
                ++ncount;
            }
            cur = found;
        }
        nodes[cur].is_leaf=1; nodes[cur].leaf_id=i;
    }
    { tinybuf_error rn = try_write_int_data(0, out, (uint64_t)ncount); if (rn.res <= 0) return rn; }
    for(int i=0;i<ncount;++i)
    {
        { tinybuf_error rp = try_write_int_data(0, out, (uint64_t)(nodes[i].parent<0?0:(uint64_t)nodes[i].parent)); if (rp.res <= 0) return rp; }
        buffer_append(out, (const char*)&nodes[i].ch, 1);
        uint8_t flag = nodes[i].is_leaf?1:0;
        buffer_append(out, (const char*)&flag, 1);
        if(nodes[i].is_leaf){ tinybuf_error rl = try_write_int_data(0, out, (uint64_t)nodes[i].leaf_id); if (rl.res <= 0) return rl; }
    }
    tinybuf_free(nodes);
    int after = buffer_get_length_inline(out);
    return tinybuf_result_ok(after - before);
}
