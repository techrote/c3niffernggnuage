#include "ngn_node.h"

bool ngn_node_id_is_valid(ngn_node_id_t node_id)
{
    return node_id == NGN_NODE_A ||
           node_id == NGN_NODE_B ||
           node_id == NGN_NODE_C;
}

const char *ngn_node_id_name(ngn_node_id_t node_id)
{
    switch (node_id) {
    case NGN_NODE_A:
        return "A";
    case NGN_NODE_B:
        return "B";
    case NGN_NODE_C:
        return "C";
    case NGN_NODE_UNCONFIGURED:
        return "unconfigured";
    default:
        return "invalid";
    }
}

bool ngn_node_id_from_u8(uint8_t value, ngn_node_id_t *out_node_id)
{
    if (out_node_id == 0 || value > (uint8_t)NGN_NODE_C) {
        return false;
    }

    *out_node_id = (ngn_node_id_t)value;
    return true;
}
