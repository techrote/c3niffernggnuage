#ifndef NGN_DISPLAY_H
#define NGN_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t width;
    uint16_t height;
    size_t stride;
    uint8_t *data;
    size_t data_size;
} ngn_framebuffer_t;

typedef enum {
    NGN_SYMBOL_DOT = 0,
    NGN_SYMBOL_SQUARE,
    NGN_SYMBOL_DIAMOND,
    NGN_SYMBOL_CROSS,
    NGN_SYMBOL_RING
} ngn_symbol_t;

size_t ngn_fb_storage_bytes(uint16_t width, uint16_t height);
bool ngn_fb_init(ngn_framebuffer_t *fb,
                 uint16_t width,
                 uint16_t height,
                 uint8_t *storage,
                 size_t storage_size);
void ngn_fb_clear(ngn_framebuffer_t *fb, bool on);
void ngn_fb_set_pixel(ngn_framebuffer_t *fb, int32_t x, int32_t y, bool on);
bool ngn_fb_get_pixel(const ngn_framebuffer_t *fb, int32_t x, int32_t y);
void ngn_fb_draw_line(ngn_framebuffer_t *fb,
                      int32_t x0,
                      int32_t y0,
                      int32_t x1,
                      int32_t y1,
                      bool on);
void ngn_fb_fill_rect(ngn_framebuffer_t *fb,
                      int32_t x,
                      int32_t y,
                      int32_t width,
                      int32_t height,
                      bool on);
void ngn_fb_draw_triangle(ngn_framebuffer_t *fb,
                          int32_t ax,
                          int32_t ay,
                          int32_t bx,
                          int32_t by,
                          int32_t cx,
                          int32_t cy,
                          bool on);
void ngn_fb_draw_symbol(ngn_framebuffer_t *fb,
                        ngn_symbol_t symbol,
                        int32_t x,
                        int32_t y,
                        uint8_t size,
                        bool on);

typedef enum {
    NGN_DISPLAY_VIEW_FIELD = 0,
    NGN_DISPLAY_VIEW_BLE,
    NGN_DISPLAY_VIEW_LINKS,
    NGN_DISPLAY_VIEW_DEBUG,
    NGN_DISPLAY_VIEW_COUNT
} ngn_display_view_t;

typedef enum {
    NGN_DISPLAY_TRACK_NEW = 0,
    NGN_DISPLAY_TRACK_ACTIVE,
    NGN_DISPLAY_TRACK_AGING,
    NGN_DISPLAY_TRACK_EXPIRED
} ngn_display_track_state_t;

typedef enum {
    NGN_DISPLAY_NODE_MISSING = 0,
    NGN_DISPLAY_NODE_DEGRADED,
    NGN_DISPLAY_NODE_HEALTHY
} ngn_display_node_state_t;

typedef enum {
    NGN_DISPLAY_CAL_UNKNOWN = 0,
    NGN_DISPLAY_CAL_COLLECTING,
    NGN_DISPLAY_CAL_READY,
    NGN_DISPLAY_CAL_STALE
} ngn_display_calibration_state_t;

typedef struct {
    bool valid;
    uint8_t activity;
    uint8_t quality;
} ngn_display_edge_t;

#define NGN_DISPLAY_MAX_BLE_TRACKS 8u

typedef struct {
    uint16_t session_tag;
    ngn_display_track_state_t state;
    uint8_t x_q8;
    uint8_t y_q8;
    uint8_t emphasis;
    uint8_t receiver_mask;
    bool coincidence;
} ngn_display_ble_track_t;

typedef struct {
    ngn_display_edge_t edge_ab;
    ngn_display_edge_t edge_bc;
    ngn_display_edge_t edge_ca;
    uint8_t global_activity;

    ngn_display_ble_track_t ble_tracks[NGN_DISPLAY_MAX_BLE_TRACKS];
    uint8_t ble_track_count;

    ngn_display_node_state_t node_a;
    ngn_display_node_state_t node_b;
    ngn_display_node_state_t node_c;

    uint16_t session_tag;
    uint16_t epoch_tag;
    bool radio_healthy;
    ngn_display_calibration_state_t calibration;
} ngn_display_state_t;

bool ngn_display_render(ngn_framebuffer_t *fb,
                        const ngn_display_state_t *state,
                        ngn_display_view_t view);

#ifdef __cplusplus
}
#endif

#endif
