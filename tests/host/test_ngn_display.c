#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ngn_display.h"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                    \
                    __FILE__, __LINE__, #condition);                            \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static uint32_t fnv1a(const uint8_t *data, size_t length)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0u; i < length; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static ngn_display_state_t demo_state(void)
{
    ngn_display_state_t state;
    memset(&state, 0, sizeof(state));

    state.edge_ab.valid = true;
    state.edge_ab.activity = 196u;
    state.edge_ab.quality = 230u;
    state.edge_bc.valid = true;
    state.edge_bc.activity = 92u;
    state.edge_bc.quality = 140u;
    state.edge_ca.valid = true;
    state.edge_ca.activity = 48u;
    state.edge_ca.quality = 72u;
    state.global_activity = 174u;

    state.ble_track_count = 4u;
    state.ble_tracks[0] = (ngn_display_ble_track_t){
        .session_tag = 0x1234u,
        .state = NGN_DISPLAY_TRACK_NEW,
        .x_q8 = 36u,
        .y_q8 = 80u,
        .emphasis = 220u,
        .receiver_mask = 0x07u,
        .coincidence = true,
    };
    state.ble_tracks[1] = (ngn_display_ble_track_t){
        .session_tag = 0x4567u,
        .state = NGN_DISPLAY_TRACK_ACTIVE,
        .x_q8 = 172u,
        .y_q8 = 120u,
        .emphasis = 128u,
        .receiver_mask = 0x03u,
        .coincidence = false,
    };
    state.ble_tracks[2] = (ngn_display_ble_track_t){
        .session_tag = 0x89abu,
        .state = NGN_DISPLAY_TRACK_AGING,
        .x_q8 = 226u,
        .y_q8 = 205u,
        .emphasis = 44u,
        .receiver_mask = 0x04u,
        .coincidence = false,
    };
    state.ble_tracks[3] = (ngn_display_ble_track_t){
        .session_tag = 0xcdefu,
        .state = NGN_DISPLAY_TRACK_EXPIRED,
        .x_q8 = 90u,
        .y_q8 = 220u,
        .emphasis = 255u,
        .receiver_mask = 0x07u,
        .coincidence = true,
    };

    state.node_a = NGN_DISPLAY_NODE_HEALTHY;
    state.node_b = NGN_DISPLAY_NODE_DEGRADED;
    state.node_c = NGN_DISPLAY_NODE_HEALTHY;
    state.session_tag = 0xa17cu;
    state.epoch_tag = 0x02f1u;
    state.radio_healthy = true;
    state.calibration = NGN_DISPLAY_CAL_READY;
    return state;
}

static int test_bounds_and_primitives(void)
{
    uint8_t guarded[24];
    ngn_framebuffer_t fb;
    uint32_t hash;

    memset(guarded, 0xa5, sizeof(guarded));
    CHECK(ngn_fb_storage_bytes(10u, 8u) == 16u);
    CHECK(ngn_fb_init(&fb, 10u, 8u, &guarded[4], 16u));
    ngn_fb_clear(&fb, false);

    CHECK(guarded[0] == 0xa5u && guarded[3] == 0xa5u);
    CHECK(guarded[20] == 0xa5u && guarded[23] == 0xa5u);

    ngn_fb_set_pixel(&fb, -1, 0, true);
    ngn_fb_set_pixel(&fb, 10, 0, true);
    ngn_fb_set_pixel(&fb, 0, 8, true);
    ngn_fb_draw_line(&fb, -1000, -1000, 1000, 1000, true);
    ngn_fb_fill_rect(&fb, -4, 6, 20, 20, true);
    ngn_fb_draw_triangle(&fb, 1, 1, 8, 1, 5, 6, true);
    ngn_fb_draw_symbol(&fb, NGN_SYMBOL_DIAMOND, 5, 4, 2u, true);
    ngn_fb_draw_symbol(&fb, NGN_SYMBOL_CROSS, 0, 0, 2u, true);

    CHECK(guarded[0] == 0xa5u && guarded[3] == 0xa5u);
    CHECK(guarded[20] == 0xa5u && guarded[23] == 0xa5u);
    CHECK(ngn_fb_get_pixel(&fb, 0, 0));
    CHECK(ngn_fb_get_pixel(&fb, 9, 7));
    CHECK(!ngn_fb_get_pixel(&fb, 10, 7));

    hash = fnv1a(fb.data, fb.data_size);
    CHECK(hash == 0x412bb599u);
    return 0;
}

static int test_extreme_symbol_coordinates(void)
{
    uint8_t guarded[32];
    ngn_framebuffer_t fb;

    memset(guarded, 0xa5, sizeof(guarded));
    CHECK(ngn_fb_init(&fb, 10u, 8u, &guarded[8], 16u));
    ngn_fb_clear(&fb, false);

    for (int value = NGN_SYMBOL_DOT; value <= NGN_SYMBOL_RING; ++value) {
        const ngn_symbol_t symbol = (ngn_symbol_t)value;
        ngn_fb_draw_symbol(&fb, symbol, INT32_MIN, INT32_MIN, 8u, true);
        ngn_fb_draw_symbol(&fb, symbol, INT32_MAX, INT32_MAX, 8u, true);
        ngn_fb_draw_symbol(&fb, symbol, INT32_MIN, 4, 8u, true);
        ngn_fb_draw_symbol(&fb, symbol, INT32_MAX, 4, 8u, true);
        ngn_fb_draw_symbol(&fb, symbol, 4, INT32_MIN, 8u, true);
        ngn_fb_draw_symbol(&fb, symbol, 4, INT32_MAX, 8u, true);
    }

    for (size_t i = 0u; i < 8u; ++i) {
        CHECK(guarded[i] == 0xa5u);
    }
    for (size_t i = 8u; i < 24u; ++i) {
        CHECK(guarded[i] == 0u);
    }
    for (size_t i = 24u; i < sizeof(guarded); ++i) {
        CHECK(guarded[i] == 0xa5u);
    }
    return 0;
}

static int test_render_golden(void)
{
    uint8_t storage[360];
    ngn_framebuffer_t fb;
    ngn_display_state_t state = demo_state();

    CHECK(ngn_fb_init(&fb, 72u, 40u, storage, sizeof(storage)));

    static const uint32_t expected[] = {
        0xde4e761cu,
        0x2e397ac4u,
        0x575f245eu,
        0xa7144efbu,
    };

    for (ngn_display_view_t view = NGN_DISPLAY_VIEW_FIELD;
         view < NGN_DISPLAY_VIEW_COUNT;
         view = (ngn_display_view_t)(view + 1)) {
        CHECK(ngn_display_render(&fb, &state, view));
        CHECK(fnv1a(fb.data, fb.data_size) == expected[view]);
    }

    return 0;
}

static int test_degraded_rendering(void)
{
    uint8_t healthy_storage[360];
    uint8_t degraded_storage[360];
    ngn_framebuffer_t healthy_fb;
    ngn_framebuffer_t degraded_fb;
    ngn_display_state_t healthy = demo_state();
    ngn_display_state_t degraded = healthy;

    healthy.node_b = NGN_DISPLAY_NODE_HEALTHY;
    degraded.node_b = NGN_DISPLAY_NODE_MISSING;
    degraded.edge_ab.valid = false;
    degraded.edge_bc.valid = false;
    degraded.radio_healthy = false;

    CHECK(ngn_fb_init(&healthy_fb, 72u, 40u, healthy_storage, sizeof(healthy_storage)));
    CHECK(ngn_fb_init(&degraded_fb, 72u, 40u, degraded_storage, sizeof(degraded_storage)));
    CHECK(ngn_display_render(&healthy_fb, &healthy, NGN_DISPLAY_VIEW_FIELD));
    CHECK(ngn_display_render(&degraded_fb, &degraded, NGN_DISPLAY_VIEW_FIELD));
    CHECK(memcmp(healthy_fb.data, degraded_fb.data, healthy_fb.data_size) != 0);

    CHECK(ngn_display_render(&healthy_fb, &healthy, NGN_DISPLAY_VIEW_DEBUG));
    CHECK(ngn_display_render(&degraded_fb, &degraded, NGN_DISPLAY_VIEW_DEBUG));
    CHECK(memcmp(healthy_fb.data, degraded_fb.data, healthy_fb.data_size) != 0);

    CHECK(fnv1a(degraded_storage, sizeof(degraded_storage)) == 0xf92e2babu);
    return 0;
}

static int test_tiny_dimensions(void)
{
    static const struct {
        uint16_t width;
        uint16_t height;
    } sizes[] = {{1u, 1u}, {3u, 2u}, {8u, 4u}, {13u, 7u}};
    ngn_display_state_t state = demo_state();

    for (size_t s = 0u; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        uint8_t guarded[96];
        ngn_framebuffer_t fb;
        const size_t needed = ngn_fb_storage_bytes(sizes[s].width, sizes[s].height);

        memset(guarded, 0x5a, sizeof(guarded));
        CHECK(needed > 0u && needed <= 80u);
        CHECK(ngn_fb_init(&fb, sizes[s].width, sizes[s].height, &guarded[8], needed));

        for (ngn_display_view_t view = NGN_DISPLAY_VIEW_FIELD;
             view < NGN_DISPLAY_VIEW_COUNT;
             view = (ngn_display_view_t)(view + 1)) {
            CHECK(ngn_display_render(&fb, &state, view));
            for (size_t i = 0u; i < 8u; ++i) {
                CHECK(guarded[i] == 0x5au);
            }
            for (size_t i = 8u + needed; i < sizeof(guarded); ++i) {
                CHECK(guarded[i] == 0x5au);
            }
        }
    }

    return 0;
}

int main(void)
{
    CHECK(test_bounds_and_primitives() == 0);
    CHECK(test_extreme_symbol_coordinates() == 0);
    CHECK(test_render_golden() == 0);
    CHECK(test_degraded_rendering() == 0);
    CHECK(test_tiny_dimensions() == 0);
    puts("ngn_display: ok");
    return 0;
}
