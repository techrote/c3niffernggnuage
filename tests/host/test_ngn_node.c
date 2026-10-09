#include <stdio.h>
#include <string.h>

#include "ngn_node.h"
#include "ngn_version.h"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                    \
                    __FILE__, __LINE__, #condition);                            \
            return 1;                                                           \
        }                                                                       \
    } while (0)

int main(void)
{
    ngn_node_id_t parsed = NGN_NODE_UNCONFIGURED;

    CHECK(ngn_node_id_is_valid(NGN_NODE_A));
    CHECK(ngn_node_id_is_valid(NGN_NODE_B));
    CHECK(ngn_node_id_is_valid(NGN_NODE_C));
    CHECK(!ngn_node_id_is_valid(NGN_NODE_UNCONFIGURED));
    CHECK(!ngn_node_id_is_valid((ngn_node_id_t)42));

    CHECK(strcmp(ngn_node_id_name(NGN_NODE_A), "A") == 0);
    CHECK(strcmp(ngn_node_id_name(NGN_NODE_B), "B") == 0);
    CHECK(strcmp(ngn_node_id_name(NGN_NODE_C), "C") == 0);
    CHECK(strcmp(ngn_node_id_name(NGN_NODE_UNCONFIGURED), "unconfigured") == 0);
    CHECK(strcmp(ngn_node_id_name((ngn_node_id_t)42), "invalid") == 0);

    CHECK(ngn_node_id_from_u8(0, &parsed));
    CHECK(parsed == NGN_NODE_A);
    CHECK(ngn_node_id_from_u8(1, &parsed));
    CHECK(parsed == NGN_NODE_B);
    CHECK(ngn_node_id_from_u8(2, &parsed));
    CHECK(parsed == NGN_NODE_C);
    CHECK(!ngn_node_id_from_u8(3, &parsed));
    CHECK(!ngn_node_id_from_u8(0, NULL));

    CHECK(NGN_PROTOCOL_VERSION == 1u);
    CHECK(strlen(NGN_FIRMWARE_VERSION) > 0u);

    puts("ngn_node: ok");
    return 0;
}
