#include "tinybuf.h"
#include "tinybuf_buffer.h"
#include "tinybuf_memory.h"
#include "tinybuf_private.h"

// 张量接口
int tinybuf_tensor_get_dtype(const tinybuf_value *value, tinybuf_error *r)
{
    assert(r);
    const tinybuf_value *v = value;
    if(!v){ tinybuf_result_add_msg_const(r, "tinybuf_tensor_get_dtype: null"); return -1; }
    if(v->_type == tinybuf_indexed_tensor)
    {
        tinybuf_indexed_tensor_t *it = (tinybuf_indexed_tensor_t*)v->_data._custom;
        v = it ? it->tensor : NULL;
        if(!v){ tinybuf_result_add_msg_const(r, "tinybuf_tensor_get_dtype: no inner tensor"); return -1; }
    }
    if(v->_type != tinybuf_tensor){ tinybuf_result_add_msg_const(r, "tinybuf_tensor_get_dtype: not tensor"); return -1; }
    tinybuf_tensor_t *t = (tinybuf_tensor_t*)v->_data._custom;
    return t ? t->dtype : -1;
}

int tinybuf_tensor_get_ndim(const tinybuf_value *value, tinybuf_error *r)
{
    assert(r);
    const tinybuf_value *v = value;
    if(!v){ tinybuf_result_add_msg_const(r, "tinybuf_tensor_get_ndim: null"); return -1; }
    if(v->_type == tinybuf_indexed_tensor)
    {
        tinybuf_indexed_tensor_t *it = (tinybuf_indexed_tensor_t*)v->_data._custom;
        v = it ? it->tensor : NULL;
        if(!v){ tinybuf_result_add_msg_const(r, "tinybuf_tensor_get_ndim: no inner tensor"); return -1; }
    }
    if(v->_type != tinybuf_tensor){ tinybuf_result_add_msg_const(r, "tinybuf_tensor_get_ndim: not tensor"); return -1; }
    tinybuf_tensor_t *t = (tinybuf_tensor_t*)v->_data._custom;
    return t ? t->dims : -1;
}

const int64_t* tinybuf_tensor_get_shape(const tinybuf_value *value, tinybuf_error *r)
{
    assert(r);
    const tinybuf_value *v = value;
    if(!v){ tinybuf_result_add_msg_const(r, "tinybuf_tensor_get_shape: null"); return NULL; }
    if(v->_type == tinybuf_indexed_tensor)
    {
        tinybuf_indexed_tensor_t *it = (tinybuf_indexed_tensor_t*)v->_data._custom;
        v = it ? it->tensor : NULL;
        if(!v){ tinybuf_result_add_msg_const(r, "tinybuf_tensor_get_shape: no inner tensor"); return NULL; }
    }
    if(v->_type != tinybuf_tensor){ tinybuf_result_add_msg_const(r, "tinybuf_tensor_get_shape: not tensor"); return NULL; }
    tinybuf_tensor_t *t = (tinybuf_tensor_t*)v->_data._custom;
    return t ? t->shape : NULL;
}

int64_t tinybuf_tensor_get_count(const tinybuf_value *value, tinybuf_error *r)
{
    assert(r);
    const tinybuf_value *v = value;
    if(!v){ tinybuf_result_add_msg_const(r, "tinybuf_tensor_get_count: null"); return -1; }
    if(v->_type == tinybuf_indexed_tensor)
    {
        tinybuf_indexed_tensor_t *it = (tinybuf_indexed_tensor_t*)v->_data._custom;
        v = it ? it->tensor : NULL;
        if(!v){ tinybuf_result_add_msg_const(r, "tinybuf_tensor_get_count: no inner tensor"); return -1; }
    }
    if(v->_type != tinybuf_tensor){ tinybuf_result_add_msg_const(r, "tinybuf_tensor_get_count: not tensor"); return -1; }
    tinybuf_tensor_t *t = (tinybuf_tensor_t*)v->_data._custom;
    return t ? t->count : -1;
}

void* tinybuf_tensor_get_data(tinybuf_value *value, tinybuf_error *r)
{
    assert(r);
    if(!value || value->_type != tinybuf_tensor){ tinybuf_result_add_msg_const(r, "tinybuf_tensor_get_data: not tensor"); return NULL; }
    tinybuf_tensor_t *t = (tinybuf_tensor_t*)value->_data._custom;
    return t ? t->data : NULL;
}

const void* tinybuf_tensor_get_data_const(const tinybuf_value *value, tinybuf_error *r)
{
    assert(r);
    if(!value || value->_type != tinybuf_tensor){ tinybuf_result_add_msg_const(r, "tinybuf_tensor_get_data_const: not tensor"); return NULL; }
    tinybuf_tensor_t *t = (tinybuf_tensor_t*)value->_data._custom;
    return t ? t->data : NULL;
}

int tinybuf_value_init_tensor(tinybuf_value *value, int dtype, const int64_t *shape, int dims, const void *data, int64_t elem_count)
{
    if (!value || !shape || dims <= 0 || elem_count < 0) return -1;
    tinybuf_value_clear(value);
    tinybuf_tensor_t *t = (tinybuf_tensor_t*)tinybuf_malloc(sizeof(tinybuf_tensor_t));
    t->dtype = dtype;
    t->dims = dims;
    t->shape = (int64_t*)tinybuf_malloc(sizeof(int64_t)*(size_t)dims);
    for (int i = 0; i < dims; ++i) { t->shape[i] = shape[i]; }
    t->count = elem_count;
    size_t bytes;
    switch (dtype) {
    case 8:  bytes = (size_t)(elem_count * 8); break;
    case 10: bytes = (size_t)(elem_count * 4); break;
    case 11: bytes = (size_t)(elem_count);     break;
    default: bytes = (size_t)(elem_count * sizeof(int64_t)); break;
    }
    t->data = tinybuf_malloc(bytes);
    if (data && bytes) { memcpy(t->data, data, bytes); }
    value->_type = tinybuf_tensor;
    value->_data._custom = t;
    value->_custom_free = NULL;
    return 0;
}

// 布尔位图接口
int64_t tinybuf_bool_map_get_count(const tinybuf_value *value, tinybuf_error *r)
{
    assert(r);
    if(!value || value->_type != tinybuf_bool_map){ tinybuf_result_add_msg_const(r, "tinybuf_bool_map_get_count: not bool_map"); return -1; }
    tinybuf_bool_map_t *bm = (tinybuf_bool_map_t*)value->_data._custom;
    return bm ? bm->count : -1;
}

const uint8_t* tinybuf_bool_map_get_bits_const(const tinybuf_value *value, tinybuf_error *r)
{
    assert(r);
    if(!value || value->_type != tinybuf_bool_map){ tinybuf_result_add_msg_const(r, "tinybuf_bool_map_get_bits_const: not bool_map"); return NULL; }
    tinybuf_bool_map_t *bm = (tinybuf_bool_map_t*)value->_data._custom;
    return bm ? bm->bits : NULL;
}

int tinybuf_value_init_bool_map(tinybuf_value *value, const uint8_t *bits, int64_t count)
{
    if(!value || count<0) return -1;
    tinybuf_value_clear(value);
    tinybuf_bool_map_t *bm=(tinybuf_bool_map_t*)tinybuf_malloc(sizeof(tinybuf_bool_map_t));
    bm->count=count;
    int64_t bytes=(count+7)/8;
    bm->bits=(uint8_t*)tinybuf_malloc((int)bytes);
    if(bits && bytes>0)
    {
        memcpy(bm->bits, bits, (size_t)bytes);
    }
    else if(bytes>0)
    {
        memset(bm->bits, 0, (size_t)bytes);
    }
    value->_type=tinybuf_bool_map; value->_data._custom=bm; value->_custom_free=NULL;
    return 0;
}

// 索引张量释放
static void tinybuf_indexed_tensor_free(void *p)
{
    if(!p) return;
    tinybuf_indexed_tensor_t *it = (tinybuf_indexed_tensor_t*)p;
    if(it->tensor) tinybuf_value_free(it->tensor);
    if(it->indices)
    {
        for(int i=0;i<it->dims;++i)
        {
            if(it->indices[i]) tinybuf_value_free(it->indices[i]);
        }
        tinybuf_free(it->indices);
    }
    tinybuf_free(it);
}

int tinybuf_value_init_indexed_tensor(tinybuf_value *value, const tinybuf_value *tensor, const tinybuf_value **indices, int dims)
{
    if(!value || !tensor || dims<0) return -1;
    tinybuf_value_clear(value);
    tinybuf_indexed_tensor_t *it=(tinybuf_indexed_tensor_t*)tinybuf_malloc(sizeof(tinybuf_indexed_tensor_t));
    it->dims=dims;
    it->tensor=tinybuf_value_clone(tensor);
    it->indices=NULL;
    if(dims>0){
        it->indices=(tinybuf_value**)tinybuf_malloc(sizeof(tinybuf_value*)*(size_t)dims);
        for(int i=0;i<dims;++i)
        {
            it->indices[i] = indices ? (indices[i] ? tinybuf_value_clone(indices[i]) : NULL) : NULL;
        }
    }
    value->_type=tinybuf_indexed_tensor; value->_data._custom=it; value->_custom_free=tinybuf_indexed_tensor_free;
    return 0;
}
