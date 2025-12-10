#include "tinybuf_private.h"
#include "tinybuf_buffer.h"

static int avl_tree_for_each_node_is_same(void *user_data, AVLTreeNode *node)
{
    AVLTree *tree2 = (AVLTree *)user_data;
    AVLTreeKey key = avl_tree_node_key(node);
    tinybuf_value *child1 = (tinybuf_value *)avl_tree_node_value(node);
    tinybuf_value *child2 = avl_tree_lookup(tree2, key);
    if (!child2)
    {
        return 1;
    }
    if (!tinybuf_value_is_same(child1, child2))
    {
        return 1;
    }
    return 0;
}

int tinybuf_value_is_same(const tinybuf_value *value1, const tinybuf_value *value2)
{
    assert(value1);
    assert(value2);
    if (value1 == value2)
    {
        return 1;
    }
    if (value1->_type != value2->_type)
    {
        return 0;
    }

    switch (value1->_type)
    {
    case tinybuf_null:
        return 1;
    case tinybuf_int:
        return value1->_data._int == value2->_data._int;
    case tinybuf_bool:
        return value1->_data._bool == value2->_data._bool;
    case tinybuf_double:
    {
        return value1->_data._double == value2->_data._double;
    }
    case tinybuf_string:
    {
        int len1 = buffer_get_length_inline(value1->_data._string);
        int len2 = buffer_get_length_inline(value2->_data._string);
        if (len1 != len2)
        {
            return 0;
        }
        if (!len1)
        {
            return 1;
        }
        return buffer_is_same(value1->_data._string, value2->_data._string);
    }

    case tinybuf_array:
    case tinybuf_map:
    {
        int map_size1 = avl_tree_num_entries(value1->_data._map_array);
        int map_size2 = avl_tree_num_entries(value2->_data._map_array);
        if (map_size1 != map_size2)
        {
            return 0;
        }
        if (!map_size1)
        {
            return 1;
        }
        return avl_tree_for_each_node(value1->_data._map_array, value2->_data._map_array, avl_tree_for_each_node_is_same) == 0;
    }

    default:
        assert(0);
        return 0;
    }
}

static int avl_tree_for_each_node_clone_map(void *user_data, AVLTreeNode *node)
{
    tinybuf_value *ret = (tinybuf_value *)user_data;
    buffer *key = (buffer *)avl_tree_node_key(node);
    tinybuf_value *val = (tinybuf_value *)avl_tree_node_value(node);

    buffer *key_clone = buffer_alloc();
    buffer_append_buffer(key_clone, key);
    tinybuf_value_map_set2(ret, key_clone, tinybuf_value_clone(val));

    return 0;
}

static int avl_tree_for_each_node_clone_array(void *user_data, AVLTreeNode *node)
{
    tinybuf_value_array_append((tinybuf_value *)user_data, tinybuf_value_clone(avl_tree_node_value(node)));
    return 0;
}

tinybuf_value *tinybuf_value_clone(const tinybuf_value *value)
{
    tinybuf_value *ret = tinybuf_value_alloc_with_type(value->_type);
    switch (value->_type)
    {
    case tinybuf_null:
    case tinybuf_int:
    case tinybuf_bool:
    case tinybuf_double:
    {
        memcpy(ret, value, sizeof(tinybuf_value));
        return ret;
    }
    case tinybuf_string:
        if (buffer_get_length_inline(value->_data._string))
        {
            ret->_data._string = buffer_alloc();
            assert(ret->_data._string);
            buffer_append_buffer(ret->_data._string, value->_data._string);
        }
        return ret;

    case tinybuf_map:
    {
        avl_tree_for_each_node(value->_data._map_array, ret, avl_tree_for_each_node_clone_map);
        return ret;
    }

    case tinybuf_array:
    {
        avl_tree_for_each_node(value->_data._map_array, ret, avl_tree_for_each_node_clone_array);
        return ret;
    }
    default:
        memcpy(ret, value, sizeof(tinybuf_value));
        return ret;
    }
}

