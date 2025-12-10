#include "tinybuf.h"
#include "tinybuf_buffer.h"
#include "tinybuf_private.h"

static tinybuf_value **s_clear_stack = NULL;
static int s_clear_stack_count = 0;
static int s_clear_stack_capacity = 0;

static inline int clear_stack_contains(tinybuf_value *v)
{
    for (int i = 0; i < s_clear_stack_count; ++i)
    {
        if (s_clear_stack[i] == v)
        {
            return 1;
        }
    }
    return 0;
}

static inline void clear_stack_push(tinybuf_value *v)
{
    if (s_clear_stack_count == s_clear_stack_capacity)
    {
        int newcap = s_clear_stack_capacity ? (s_clear_stack_capacity * 2) : 16;
        s_clear_stack = (tinybuf_value **)tinybuf_realloc(s_clear_stack, sizeof(tinybuf_value *) * newcap);
        s_clear_stack_capacity = newcap;
    }
    s_clear_stack[s_clear_stack_count++] = v;
}

static inline void clear_stack_pop(tinybuf_value *v)
{
    if (s_clear_stack_count == 0)
    {
        return;
    }
    if (s_clear_stack[s_clear_stack_count - 1] == v)
    {
        --s_clear_stack_count;
        return;
    }
    for (int i = 0; i < s_clear_stack_count; ++i)
    {
        if (s_clear_stack[i] == v)
        {
            for (int j = i + 1; j < s_clear_stack_count; ++j)
            {
                s_clear_stack[j - 1] = s_clear_stack[j];
            }
            --s_clear_stack_count;
            return;
        }
    }
}

inline bool has_sub_ref(const tinybuf_value *value)
{
    return value->_data._ref != NULL && (value->_type == tinybuf_value_ref || value->_type == tinybuf_version);
}

int tinybuf_value_clear(tinybuf_value *value);
inline bool maybe_free_sub_ref(tinybuf_value *value)
{
    if (has_sub_ref(value))
    {
        tinybuf_value *sub = value->_data._ref;
        value->_data._ref = NULL;
        tinybuf_value_clear(sub);
        return true;
    }
    return false;
}

inline bool need_custom_free(const tinybuf_value *value)
{
    return value->_data._custom != NULL && (value->_type == tinybuf_custom || value->_type == tinybuf_tensor || value->_type == tinybuf_bool_map || value->_type == tinybuf_indexed_tensor);
}
inline bool maybe_free_custom(tinybuf_value *value)
{
    if (need_custom_free(value))
    {
        if (value->_custom_free)
        {
            value->_custom_free(value->_data._custom);
            value->_data._custom = NULL;
            value->_type = tinybuf_null;
        }
        else
        {
            free(value->_data._custom);
            value->_data._custom = NULL;
            value->_type = tinybuf_null;
        }
        return true;
    }
    return false;
}

tinybuf_value *tinybuf_value_alloc(void)
{
    tinybuf_value *ret = tinybuf_malloc(sizeof(tinybuf_value));
    assert(ret);
    memset(ret, 0, sizeof(tinybuf_value));
    ret->_type = tinybuf_null;
    ret->_plugin_index = -1;
    ret->_custom_box_type = -1;
    return ret;
}

tinybuf_value *tinybuf_value_alloc_with_type(tinybuf_type type)
{
    tinybuf_value *value = tinybuf_value_alloc();
    value->_type = type;
    return value;
}

int tinybuf_value_free(tinybuf_value *value)
{
    assert(value);
    tinybuf_value_clear(value);
    tinybuf_free(value);
    return 0;
}

int tinybuf_value_clear(tinybuf_value *value)
{
    assert(value);
    if (clear_stack_contains(value))
    {
        return 0;
    }
    clear_stack_push(value);
    switch (value->_type)
    {
    case tinybuf_string:
    {
        if (value->_data._string)
        {
            buffer_free(value->_data._string);
            value->_data._string = NULL;
        }
    }
    break;

    case tinybuf_map:
    case tinybuf_array:
    {
        if (value->_data._map_array)
        {
            avl_tree_free(value->_data._map_array);
            value->_data._map_array = NULL;
        }
    }
    break;

    default:
        break;
    }

    maybe_free_sub_ref(value);
    maybe_free_custom(value);

    if (value->_type != tinybuf_null)
    {
        memset(value, 0, sizeof(tinybuf_value));
        value->_type = tinybuf_null;
    }
    clear_stack_pop(value);
    return 0;
}

static int mapKeyCompare(AVLTreeKey key1, AVLTreeKey key2)
{
    buffer *buf_key1 = (buffer *)key1;
    buffer *buf_key2 = (buffer *)key2;
    int buf_len1 = buffer_get_length_inline(buf_key1);
    int buf_len2 = buffer_get_length_inline(buf_key2);
    int min_len = buf_len1 < buf_len2 ? buf_len1 : buf_len2;
    int ret = memcmp(buffer_get_data_inline(buf_key1), buffer_get_data_inline(buf_key2), min_len);
    if (ret != 0)
    {
        return ret;
    }
    return buf_len1 - buf_len2;
}

static void mapFreeValueFunc(AVLTreeValue value)
{
    tinybuf_value_free(value);
}

static void buffer_key_free(AVLTreeKey key)
{
    buffer_free((buffer *)key);
}

int tinybuf_value_map_set2(tinybuf_value *parent, buffer *key, tinybuf_value *value)
{
    assert(parent);
    assert(key);
    assert(value);
    if (parent->_type != tinybuf_map && parent->_type != tinybuf_versionlist)
    {
        tinybuf_value_clear(parent);
        parent->_type = tinybuf_map;
    }
    if (!parent->_data._map_array)
    {
        parent->_data._map_array = avl_tree_new(mapKeyCompare);
    }
    avl_tree_insert(parent->_data._map_array, key, value, buffer_key_free, mapFreeValueFunc);
    return 0;
}

int tinybuf_value_map_set(tinybuf_value *parent, const char *key, tinybuf_value *value)
{
    assert(key);
    buffer *key_buf = buffer_alloc();
    assert(key_buf);
    buffer_assign(key_buf, key, 0);
    return tinybuf_value_map_set2(parent, key_buf, value);
}

int tinybuf_value_map_set_int(tinybuf_value *parent, uint64_t key, tinybuf_value *value)
{
    buffer *key_buf = buffer_alloc();
    assert(key_buf);
    dump_int(key, key_buf);
    return tinybuf_value_map_set2(parent, key_buf, value);
}

static int arrayKeyCompare(AVLTreeKey key1, AVLTreeKey key2)
{
    return (int)(((intptr_t)key1) - ((intptr_t)key2));
}

int tinybuf_value_array_append(tinybuf_value *parent, tinybuf_value *value)
{
    assert(parent);
    assert(value);
    if (parent->_type != tinybuf_array)
    {
        tinybuf_value_clear(parent);
        parent->_type = tinybuf_array;
    }
    if (!parent->_data._map_array)
    {
        parent->_data._map_array = avl_tree_new(arrayKeyCompare);
    }
    int key = avl_tree_num_entries(parent->_data._map_array);
    avl_tree_insert(parent->_data._map_array, (AVLTreeKey)key, value, NULL, mapFreeValueFunc);
    return 0;
}
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
