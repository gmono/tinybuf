#include "tinybuf.h"
#include "tinybuf_buffer.h"
#include "tinybuf_private.h"

int tinybuf_value_get_child_size(const tinybuf_value *value)
{
    if (!value || (value->_type != tinybuf_map && value->_type != tinybuf_array))
    {
        return 0;
    }
    return avl_tree_num_entries(value->_data._map_array);
}

const tinybuf_value *tinybuf_value_get_array_child(const tinybuf_value *value, int index)
{
    if (!value || value->_type != tinybuf_array || !value->_data._map_array)
    {
        return NULL;
    }
    return (tinybuf_value *)avl_tree_lookup(value->_data._map_array, (AVLTreeKey)index);
}

const tinybuf_value *tinybuf_value_get_map_child(const tinybuf_value *value, const char *key)
{
    return tinybuf_value_get_map_child2(value, key, (int)strlen(key));
}

const tinybuf_value *tinybuf_value_get_map_child2(const tinybuf_value *value, const char *key, int key_len)
{
    if (!value || !key || value->_type != tinybuf_map || !value->_data._map_array)
    {
        return NULL;
    }
    struct T_buffer buf;
    buf._data = (char*)key;
    buf._len = key_len;
    buf._capacity = key_len + 1;
    return (tinybuf_value *)avl_tree_lookup(value->_data._map_array, &buf);
}

const tinybuf_value *tinybuf_value_get_map_child_and_key(const tinybuf_value *value, int index, buffer **key)
{
    if (!value || value->_type != tinybuf_map || !value->_data._map_array)
    {
        return NULL;
    }
    AVLTreeNode *node = avl_tree_get_node_by_index(value->_data._map_array, index);
    if (!node)
    {
        return NULL;
    }
    if (key)
    {
        *key = (buffer *)avl_tree_node_key(node);
    }
    return (tinybuf_value *)avl_tree_node_value(node);
}

void tinybuf_versionlist_add(tinybuf_value *versionlist, int64_t version, tinybuf_value *value)
{
    buffer* key_buf = buffer_alloc();
    dump_int((uint64_t)version, key_buf);
    tinybuf_value_map_set2(versionlist, key_buf, value);
}

void tinybuf_version_set(tinybuf_value *target, int64_t version, tinybuf_value *value)
{
    (void)version;
    target->_type = tinybuf_version;
    target->_data._ref = value;
}

const tinybuf_value* tinybuf_indexed_tensor_get_tensor_const(const tinybuf_value *value)
{
    if(!value || value->_type!=tinybuf_indexed_tensor) return NULL;
    tinybuf_indexed_tensor_t *it=(tinybuf_indexed_tensor_t*)value->_data._custom;
    return it ? it->tensor : NULL;
}

const tinybuf_value* tinybuf_indexed_tensor_get_index_const(const tinybuf_value *value, int dim)
{
    if(!value || value->_type!=tinybuf_indexed_tensor) return NULL;
    tinybuf_indexed_tensor_t *it=(tinybuf_indexed_tensor_t*)value->_data._custom;
    if(!it || dim<0 || dim>=it->dims) return NULL;
    return it->indices ? it->indices[dim] : NULL;
}
tinybuf_type tinybuf_value_get_type(const tinybuf_value *value)
{
    if (!value) return tinybuf_null;
    return value->_type;
}

int64_t tinybuf_value_get_int(const tinybuf_value *value)
{
    if (!value || value->_type != tinybuf_int) return 0;
    return value->_data._int;
}

double tinybuf_value_get_double(const tinybuf_value *value)
{
    if (!value || value->_type != tinybuf_double) return 0;
    return value->_data._double;
}

int tinybuf_value_get_bool(const tinybuf_value *value)
{
    if (!value || value->_type != tinybuf_bool) return 0;
    return value->_data._bool;
}

buffer *tinybuf_value_get_string(const tinybuf_value *value)
{
    if (!value || value->_type != tinybuf_string) return 0;
    return value->_data._string;
}

int tinybuf_value_init_bool(tinybuf_value *value, int flag)
{
    assert(value);
    tinybuf_value_clear(value);
    value->_type = tinybuf_bool;
    value->_data._bool = (flag != 0);
    return 0;
}

int tinybuf_value_init_int(tinybuf_value *value, int64_t int_val)
{
    assert(value);
    tinybuf_value_clear(value);
    value->_type = tinybuf_int;
    value->_data._int = int_val;
    return 0;
}

int tinybuf_value_init_intlike_with_type(tinybuf_value *value, tinybuf_type type, int64_t int_val)
{
    int ret = tinybuf_value_init_int(value, int_val);
    if (ret != 0) return ret;
    value->_type = type;
    return 0;
}

int tinybuf_value_set_type(tinybuf_value *value, tinybuf_type type)
{
    assert(value);
    value->_type = type;
    return 0;
}

int tinybuf_value_set_plugin_index(tinybuf_value *value, int index)
{
    assert(value);
    value->_plugin_index = index;
    return 0;
}

int tinybuf_value_get_plugin_index(const tinybuf_value *value)
{
    if(!value) return -1;
    return value->_plugin_index;
}

int tinybuf_value_set_custom_box_type(tinybuf_value *value, int type)
{
    assert(value);
    value->_custom_box_type = type;
    return 0;
}

int tinybuf_value_get_custom_box_type(const tinybuf_value *value)
{
    if(!value) return -1;
    return value->_custom_box_type;
}

int tinybuf_value_init_double(tinybuf_value *value, double db_val)
{
    assert(value);
    tinybuf_value_clear(value);
    value->_type = tinybuf_double;
    value->_data._double = db_val;
    return 0;
}

int tinybuf_value_init_string2(tinybuf_value *value, buffer *buf)
{
    assert(value);
    assert(buf);
    if (value->_type != tinybuf_string)
    {
        tinybuf_value_clear(value);
    }
    if (value->_data._string)
    {
        buffer_free(value->_data._string);
    }
    value->_type = tinybuf_string;
    value->_data._string = buf;
    return 0;
}

static inline int tinybuf_value_init_string_l(tinybuf_value *value, const char *str, int len, int use_strlen)
{
    assert(value);
    assert(str);
    if (value->_type != tinybuf_string)
    {
        tinybuf_value_clear(value);
    }
    if (!value->_data._string)
    {
        value->_data._string = buffer_alloc();
        assert(value->_data._string);
    }
    value->_type = tinybuf_string;
    if (len <= 0 && use_strlen)
    {
        len = (int)strlen(str);
    }
    if (len > 0)
    {
        buffer_assign(value->_data._string, str, len);
    }
    else
    {
        buffer_set_length(value->_data._string, 0);
    }
    return 0;
}

int tinybuf_value_init_string(tinybuf_value *value, const char *str, int len)
{
    return tinybuf_value_init_string_l(value, str, len, 1);
}
