#include "tinybuf_private.h"
#include "tinybuf_buffer.h"
#include "tinybuf_plugin.h"
#include <string.h>

static inline int dump_double(double db, buffer *out)
{
    uint64_t encoded = 0;
    memcpy(&encoded, &db, 8);
    uint32_t val = htonl(encoded >> 32);
    buffer_append(out, (char *)&val, 4);
    val = htonl(encoded & 0xFFFFFFFF);
    buffer_append(out, (char *)&val, 4);
    return 8;
}

int dump_string(int len, const char *str, buffer *out)
{
    int ret = dump_int(len, out);
    buffer_append(out, str, len);
    return ret + len;
}

int int_serialize(uint64_t in, uint8_t *out)
{
    int index = 0;
    for (int i = 0; i <= (8 * sizeof(in)) / 7; ++i, ++index)
    {
        out[index] = (uint8_t)(in & 0x7F);
        in >>= 7;
        if (!in) break;
        out[index] |= 0x80;
    }
    return ++index;
}

static int avl_tree_for_each_node_dump_map(void *user_data, AVLTreeNode *node)
{
    buffer *out = (buffer *)user_data;
    buffer *key = (buffer *)avl_tree_node_key(node);
    tinybuf_value *val = (tinybuf_value *)avl_tree_node_value(node);
    dump_string(buffer_get_length_inline(key), buffer_get_data_inline(key), out);
    tinybuf_value_serialize(val, out);
    return 0;
}

static int avl_tree_for_each_node_dump_array(void *user_data, AVLTreeNode *node)
{
    buffer *out = (buffer *)user_data;
    tinybuf_value_serialize(avl_tree_node_value(node), out);
    return 0;
}

int tinybuf_value_serialize(const tinybuf_value *value, buffer *out)
{
    assert(value);
    assert(out);
    if (value && value->_custom_box_type >= 0)
    {
        tinybuf_result rr = tinybuf_plugins_try_write((uint8_t)value->_custom_box_type, value, out);
        if (rr.res <= 0)
        {
            s_last_error_msg = "plugin write failed";
        }
        return rr.res;
    }
    if (tinybuf_precache_is_redirect() && value)
    {
        int64_t start_pos = tinybuf_precache_find_start_for(out, value);
        if (start_pos >= 0)
        {
            return try_write_pointer_value(out, (enum offset_type)0, start_pos);
        }
    }
    switch (value->_type)
    {
    case tinybuf_null:
    {
        char type = serialize_null;
        buffer_append(out, &type, 1);
    }
    break;

    case tinybuf_int:
    {
        char type = value->_data._int > 0 ? serialize_positive_int : serialize_negtive_int;
        buffer_append(out, &type, 1);
        dump_int(value->_data._int > 0 ? value->_data._int : -value->_data._int, out);
    }
    break;

    case tinybuf_bool:
    {
        char type = value->_data._bool ? serialize_bool_true : serialize_bool_false;
        buffer_append(out, &type, 1);
    }
    break;

    case tinybuf_double:
    {
        char type = serialize_double;
        buffer_append(out, &type, 1);
        dump_double(value->_data._double, out);
    }
    break;

    case tinybuf_string:
    {
        int len = buffer_get_length_inline(value->_data._string);
        if (s_use_strpool)
        {
            int idx = strpool_add(buffer_get_data_inline(value->_data._string), len);
            char type = serialize_str_index;
            buffer_append(out, &type, 1);
            dump_int((uint64_t)idx, out);
        }
        else
        {
            char type = serialize_string;
            buffer_append(out, &type, 1);
            if (len)
            {
                dump_string(len, buffer_get_data_inline(value->_data._string), out);
            }
            else
            {
                dump_int(0, out);
            }
        }
    }
    break;

    case tinybuf_map:
    {
        char type = serialize_map;
        buffer_append(out, &type, 1);
        int map_size = avl_tree_num_entries(value->_data._map_array);
        dump_int(map_size, out);
        avl_tree_for_each_node(value->_data._map_array, out, avl_tree_for_each_node_dump_map);
    }
    break;

    case tinybuf_array:
    {
        char type = serialize_array;
        buffer_append(out, &type, 1);
        int array_size = avl_tree_num_entries(value->_data._map_array);
        dump_int(array_size, out);
        avl_tree_for_each_node(value->_data._map_array, out, avl_tree_for_each_node_dump_array);
    }
    break;
    case tinybuf_tensor:
    {
        tinybuf_tensor_t *t = (tinybuf_tensor_t *)value->_data._custom;
        if (!t)
        {
            break;
        }
        int64_t elem = t->count;
        if (t->dims == 1)
        {
            char type = serialize_vector_tensor;
            buffer_append(out, &type, 1);
            dump_int((uint64_t)elem, out);
            dump_int((uint64_t)t->dtype, out);
            if (t->dtype == 8)
            {
                const double *pd = (const double *)t->data;
                for (int64_t i = 0; i < elem; ++i)
                {
                    dump_double(pd[i], out);
                }
            }
            else if (t->dtype == 10)
            {
                const float *pf = (const float *)t->data;
                for (int64_t i = 0; i < elem; ++i)
                {
                    uint32_t raw = 0;
                    memcpy(&raw, &pf[i], 4);
                    raw = htonl(raw);
                    buffer_append(out, (char *)&raw, 4);
                }
            }
            else if (t->dtype == 11)
            {
                const uint8_t *pb = (const uint8_t *)t->data;
                int64_t bytes = (elem + 7) / 8;
                for (int64_t b = 0; b < bytes; ++b)
                {
                    uint8_t one = 0;
                    for (int k = 0; k < 8; ++k)
                    {
                        int64_t idx = b * 8 + k;
                        if (idx >= elem)
                            break;
                        uint8_t bit = pb[idx] ? 1 : 0;
                        one |= (uint8_t)(bit << (7 - k));
                    }
                    buffer_append(out, (const char *)&one, 1);
                }
            }
            else
            {
                const int64_t *pi64 = (const int64_t *)t->data;
                for (int64_t i = 0; i < elem; ++i)
                {
                    uint64_t v = (uint64_t)(pi64[i] < 0 ? -pi64[i] : pi64[i]);
                    char ty = (pi64[i] < 0) ? serialize_negtive_int : serialize_positive_int;
                    buffer_append(out, &ty, 1);
                    dump_int(v, out);
                }
            }
        }
        else
        {
            char type = serialize_dense_tensor;
            buffer_append(out, &type, 1);
            dump_int((uint64_t)t->dims, out);
            for (int i = 0; i < t->dims; ++i)
            {
                dump_int((uint64_t)t->shape[i], out);
            }
            dump_int((uint64_t)t->dtype, out);
            if (t->dtype == 8)
            {
                const double *pd = (const double *)t->data;
                for (int64_t i = 0; i < elem; ++i)
                {
                    dump_double(pd[i], out);
                }
            }
            else if (t->dtype == 10)
            {
                const float *pf = (const float *)t->data;
                for (int64_t i = 0; i < elem; ++i)
                {
                    uint32_t raw = 0;
                    memcpy(&raw, &pf[i], 4);
                    raw = htonl(raw);
                    buffer_append(out, (char *)&raw, 4);
                }
            }
            else if (t->dtype == 11)
            {
                const uint8_t *pb = (const uint8_t *)t->data;
                int64_t bytes = (elem + 7) / 8;
                for (int64_t b = 0; b < bytes; ++b)
                {
                    uint8_t one = 0;
                    for (int k = 0; k < 8; ++k)
                    {
                        int64_t idx = b * 8 + k;
                        if (idx >= elem)
                            break;
                        uint8_t bit = pb[idx] ? 1 : 0;
                        one |= (uint8_t)(bit << (7 - k));
                    }
                    buffer_append(out, (const char *)&one, 1);
                }
            }
            else
            {
                const int64_t *pi64 = (const int64_t *)t->data;
                for (int64_t i = 0; i < elem; ++i)
                {
                    uint64_t v = (uint64_t)(pi64[i] < 0 ? -pi64[i] : pi64[i]);
                    char ty = (pi64[i] < 0) ? serialize_negtive_int : serialize_positive_int;
                    buffer_append(out, &ty, 1);
                    dump_int(v, out);
                }
            }
        }
    }
    break;
    case tinybuf_bool_map:
    {
        tinybuf_bool_map_t *bm = (tinybuf_bool_map_t *)value->_data._custom;
        if (!bm)
        {
            break;
        }
        char type = serialize_bool_map;
        buffer_append(out, &type, 1);
        dump_int((uint64_t)bm->count, out);
        int64_t bytes = (bm->count + 7) / 8;
        if (bytes > 0)
        {
            buffer_append(out, (const char *)bm->bits, (int)bytes);
        }
    }
    break;
    case tinybuf_indexed_tensor:
    {
        tinybuf_indexed_tensor_t *it = (tinybuf_indexed_tensor_t *)value->_data._custom;
        if (!it || !it->tensor)
        {
            break;
        }
        char type = serialize_indexed_tensor;
        buffer_append(out, &type, 1);
        try_write_box(out, it->tensor);
        dump_int((uint64_t)it->dims, out);
        for (int i = 0; i < it->dims; ++i)
        {
            if (it->indices && it->indices[i])
            {
                dump_int(1, out);
                try_write_box(out, it->indices[i]);
            }
            else
            {
                dump_int(0, out);
            }
        }
    }
    break;

    default:
        assert(0);
        break;
    }

    return 0;
}
