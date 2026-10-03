#ifndef NGN_NODE_H
#define NGN_NODE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NGN_NODE_A = 0,
    NGN_NODE_B = 1,
    NGN_NODE_C = 2,
    NGN_NODE_UNCONFIGURED = 0xff
} ngn_node_id_t;

bool ngn_node_id_is_valid(ngn_node_id_t node_id);
const char *ngn_node_id_name(ngn_node_id_t node_id);
bool ngn_node_id_from_u8(uint8_t value, ngn_node_id_t *out_node_id);

#ifdef __cplusplus
}
#endif

#endif
