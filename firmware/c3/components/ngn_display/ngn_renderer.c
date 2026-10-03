#include "ngn_display.h"

#include <stddef.h>
#include <stdint.h>

static int32_t map_q8(uint8_t value, int32_t low, int32_t high)
{
    if (high <= low) {
        return low;
    }
    return low + (int32_t)(((int64_t)(high - low) * value + 127) / 255);
}

static uint8_t symbol_size(uint8_t emphasis)
{
    if (emphasis >= 192u) {
        return 3u;
    }
    if (emphasis >= 80u) {
        return 2u;
    }
    return 1u;
}

static uint8_t glyph_row(char c, uint8_t row)
{
    static const struct {
        char c;
        uint8_t row[5];
    } glyphs[] = {
        {'0', {7, 5, 5, 5, 7}}, {'1', {2, 6, 2, 2, 7}},
        {'2', {7, 1, 7, 4, 7}}, {'3', {7, 1, 7, 1, 7}},
        {'4', {5, 5, 7, 1, 1}}, {'5', {7, 4, 7, 1, 7}},
        {'6', {7, 4, 7, 5, 7}}, {'7', {7, 1, 2, 2, 2}},
        {'8', {7, 5, 7, 5, 7}}, {'9', {7, 5, 7, 1, 7}},
        {'A', {2, 5, 7, 5, 5}}, {'B', {6, 5, 6, 5, 6}},
        {'C', {3, 4, 4, 4, 3}}, {'D', {6, 5, 5, 5, 6}},
        {'E', {7, 4, 6, 4, 7}}, {'F', {7, 4, 6, 4, 4}},
        {'G', {3, 4, 5, 5, 3}}, {'I', {7, 2, 2, 2, 7}},
        {'L', {4, 4, 4, 4, 7}}, {'N', {5, 7, 7, 7, 5}},
        {'R', {6, 5, 6, 5, 5}}, {'S', {3, 4, 7, 1, 6}},
        {'U', {5, 5, 5, 5, 7}}, {'X', {5, 5, 2, 5, 5}},
        {'+', {0, 2, 7, 2, 0}}, {'-', {0, 0, 7, 0, 0}},
        {'?', {6, 1, 2, 0, 2}}, {' ', {0, 0, 0, 0, 0}},
    };

    if (row >= 5u) {
        return 0u;
    }
    for (size_t i = 0u; i < sizeof(glyphs) / sizeof(glyphs[0]); ++i) {
        if (glyphs[i].c == c) {
            return glyphs[i].row[row];
        }
    }
    return 0u;
}

static void draw_char(ngn_framebuffer_t *fb, int32_t x, int32_t y, char c)
{
    for (uint8_t row = 0u; row < 5u; ++row) {
        const uint8_t bits = glyph_row(c, row);
        for (uint8_t col = 0u; col < 3u; ++col) {
            if ((bits & (uint8_t)(1u << (2u - col))) != 0u) {
                ngn_fb_set_pixel(fb, x + col, y + row, true);
            }
        }
    }
}

static void draw_text(ngn_framebuffer_t *fb, int32_t x, int32_t y, const char *text)
{
    if (text == NULL) {
        return;
    }
    for (; *text != '\0'; ++text, x += 4) {
        draw_char(fb, x, y, *text);
    }
}

static char hex_digit(uint8_t value)
{
    value &= 0x0fu;
    return value < 10u ? (char)('0' + value) : (char)('A' + (value - 10u));
}

static void draw_hex16(ngn_framebuffer_t *fb, int32_t x, int32_t y, uint16_t value)
{
    char text[5];
    text[0] = hex_digit((uint8_t)(value >> 12));
    text[1] = hex_digit((uint8_t)(value >> 8));
    text[2] = hex_digit((uint8_t)(value >> 4));
    text[3] = hex_digit((uint8_t)value);
    text[4] = '\0';
    draw_text(fb, x, y, text);
}

static ngn_display_node_state_t node_at(const ngn_display_state_t *state, uint8_t index)
{
    switch (index) {
    case 0u:
        return state->node_a;
    case 1u:
        return state->node_b;
    default:
        return state->node_c;
    }
}

static void triangle_points(const ngn_framebuffer_t *fb,
                            int32_t *ax,
                            int32_t *ay,
                            int32_t *bx,
                            int32_t *by,
                            int32_t *cx,
                            int32_t *cy)
{
    const int32_t xmax = (int32_t)fb->width - 1;
    const int32_t ymax = (int32_t)fb->height - 1;
    const int32_t mx = fb->width > 6u ? 2 : 0;
    const int32_t my = fb->height > 6u ? 1 : 0;

    *ax = xmax / 2;
    *ay = my;
    *bx = mx;
    *by = ymax - my;
    *cx = xmax - mx;
    *cy = ymax - my;
}

static void edge_marker(ngn_framebuffer_t *fb,
                        int32_t x0,
                        int32_t y0,
                        int32_t x1,
                        int32_t y1,
                        const ngn_display_edge_t *edge)
{
    const int32_t mx = (x0 + x1) / 2;
    const int32_t my = (y0 + y1) / 2;

    if (!edge->valid) {
        ngn_fb_draw_symbol(fb, NGN_SYMBOL_CROSS, mx, my, 1u, true);
        return;
    }

    if (edge->activity >= 48u) {
        ngn_fb_draw_symbol(fb, NGN_SYMBOL_DOT, mx, my,
                           edge->activity >= 176u ? 2u : 1u, true);
    }
    if (edge->quality < 64u) {
        ngn_fb_set_pixel(fb, mx + 2, my, true);
    }
}

static void node_marker(ngn_framebuffer_t *fb,
                        int32_t x,
                        int32_t y,
                        ngn_display_node_state_t node)
{
    switch (node) {
    case NGN_DISPLAY_NODE_HEALTHY:
        ngn_fb_draw_symbol(fb, NGN_SYMBOL_DOT, x, y, 1u, true);
        break;
    case NGN_DISPLAY_NODE_DEGRADED:
        ngn_fb_draw_symbol(fb, NGN_SYMBOL_RING, x, y, 2u, true);
        break;
    case NGN_DISPLAY_NODE_MISSING:
    default:
        ngn_fb_draw_symbol(fb, NGN_SYMBOL_CROSS, x, y, 2u, true);
        break;
    }
}

static void render_field(ngn_framebuffer_t *fb, const ngn_display_state_t *state)
{
    int32_t ax;
    int32_t ay;
    int32_t bx;
    int32_t by;
    int32_t cx;
    int32_t cy;
    uint32_t total = 0u;
    int64_t weighted_x = 0;
    int64_t weighted_y = 0;

    triangle_points(fb, &ax, &ay, &bx, &by, &cx, &cy);
    ngn_fb_draw_triangle(fb, ax, ay, bx, by, cx, cy, true);

    edge_marker(fb, ax, ay, bx, by, &state->edge_ab);
    edge_marker(fb, bx, by, cx, cy, &state->edge_bc);
    edge_marker(fb, cx, cy, ax, ay, &state->edge_ca);

    if (state->edge_ab.valid) {
        total += state->edge_ab.activity;
        weighted_x += (int64_t)state->edge_ab.activity * ((ax + bx) / 2);
        weighted_y += (int64_t)state->edge_ab.activity * ((ay + by) / 2);
    }
    if (state->edge_bc.valid) {
        total += state->edge_bc.activity;
        weighted_x += (int64_t)state->edge_bc.activity * ((bx + cx) / 2);
        weighted_y += (int64_t)state->edge_bc.activity * ((by + cy) / 2);
    }
    if (state->edge_ca.valid) {
        total += state->edge_ca.activity;
        weighted_x += (int64_t)state->edge_ca.activity * ((cx + ax) / 2);
        weighted_y += (int64_t)state->edge_ca.activity * ((cy + ay) / 2);
    }

    if (total > 0u) {
        const int32_t field_x = (int32_t)(weighted_x / (int64_t)total);
        const int32_t field_y = (int32_t)(weighted_y / (int64_t)total);
        ngn_fb_draw_symbol(fb, NGN_SYMBOL_DIAMOND, field_x, field_y,
                           symbol_size(state->global_activity), true);
    }

    node_marker(fb, ax, ay, state->node_a);
    node_marker(fb, bx, by, state->node_b);
    node_marker(fb, cx, cy, state->node_c);
}

static ngn_symbol_t ble_symbol(ngn_display_track_state_t state)
{
    switch (state) {
    case NGN_DISPLAY_TRACK_NEW:
        return NGN_SYMBOL_DIAMOND;
    case NGN_DISPLAY_TRACK_ACTIVE:
        return NGN_SYMBOL_SQUARE;
    case NGN_DISPLAY_TRACK_AGING:
        return NGN_SYMBOL_CROSS;
    case NGN_DISPLAY_TRACK_EXPIRED:
    default:
        return NGN_SYMBOL_DOT;
    }
}

static void render_ble(ngn_framebuffer_t *fb, const ngn_display_state_t *state)
{
    const int32_t xmax = (int32_t)fb->width - 1;
    const int32_t ymax = (int32_t)fb->height - 1;
    const int32_t margin = (fb->width > 10u && fb->height > 10u) ? 3 : 0;
    const uint8_t count = state->ble_track_count > NGN_DISPLAY_MAX_BLE_TRACKS
                              ? NGN_DISPLAY_MAX_BLE_TRACKS
                              : state->ble_track_count;

    for (uint8_t i = 0u; i < count; ++i) {
        const ngn_display_ble_track_t *track = &state->ble_tracks[i];
        const uint8_t size = symbol_size(track->emphasis);
        int32_t x;
        int32_t y;

        if (track->state == NGN_DISPLAY_TRACK_EXPIRED) {
            continue;
        }

        x = map_q8(track->x_q8, margin, xmax - margin);
        y = map_q8(track->y_q8, margin, ymax - margin);
        ngn_fb_draw_symbol(fb, ble_symbol(track->state), x, y, size, true);

        if (track->coincidence) {
            const uint8_t halo = (uint8_t)(size + 2u);
            ngn_fb_draw_symbol(fb, NGN_SYMBOL_RING, x, y, halo, true);
        }

        if ((track->receiver_mask & 0x07u) != 0u && fb->height >= 8u) {
            const uint8_t receivers = (uint8_t)(((track->receiver_mask & 1u) != 0u) +
                                                ((track->receiver_mask & 2u) != 0u) +
                                                ((track->receiver_mask & 4u) != 0u));
            for (uint8_t tick = 0u; tick < receivers; ++tick) {
                ngn_fb_set_pixel(fb, x - 1 + tick, y + (int32_t)size + 2, true);
            }
        }
    }

    if (count == 0u) {
        draw_text(fb, 0, 0, "BLE-");
    }
}

static void render_link_row(ngn_framebuffer_t *fb,
                            int32_t y,
                            char left,
                            char right,
                            const ngn_display_edge_t *edge)
{
    int32_t bar_start = 9;
    int32_t bar_end = (int32_t)fb->width - 5;
    int32_t filled_end;
    char label[3] = {left, right, '\0'};

    draw_text(fb, 0, y - 2, label);
    if (bar_end <= bar_start) {
        bar_start = 0;
        bar_end = (int32_t)fb->width - 1;
    }

    ngn_fb_draw_line(fb, bar_start, y, bar_end, y, true);
    if (!edge->valid) {
        ngn_fb_draw_symbol(fb, NGN_SYMBOL_CROSS, (bar_start + bar_end) / 2, y, 1u, true);
        return;
    }

    filled_end = map_q8(edge->activity, bar_start, bar_end);
    ngn_fb_fill_rect(fb, bar_start, y - 1, filled_end - bar_start + 1, 3, true);

    if (bar_end + 1 < (int32_t)fb->width) {
        const uint8_t ticks = edge->quality >= 192u ? 3u : (edge->quality >= 96u ? 2u : 1u);
        for (uint8_t i = 0u; i < ticks; ++i) {
            ngn_fb_set_pixel(fb, bar_end + 1 + i, y, true);
        }
    }
}

static void render_links(ngn_framebuffer_t *fb, const ngn_display_state_t *state)
{
    const int32_t h = (int32_t)fb->height;
    render_link_row(fb, h / 6, 'A', 'B', &state->edge_ab);
    render_link_row(fb, h / 2, 'B', 'C', &state->edge_bc);
    render_link_row(fb, (h * 5) / 6, 'C', 'A', &state->edge_ca);
}

static char node_status_char(ngn_display_node_state_t state)
{
    switch (state) {
    case NGN_DISPLAY_NODE_HEALTHY:
        return '+';
    case NGN_DISPLAY_NODE_DEGRADED:
        return '?';
    case NGN_DISPLAY_NODE_MISSING:
    default:
        return '-';
    }
}

static char calibration_char(ngn_display_calibration_state_t state)
{
    switch (state) {
    case NGN_DISPLAY_CAL_COLLECTING:
        return 'C';
    case NGN_DISPLAY_CAL_READY:
        return 'R';
    case NGN_DISPLAY_CAL_STALE:
        return 'S';
    case NGN_DISPLAY_CAL_UNKNOWN:
    default:
        return 'U';
    }
}

static void render_debug(ngn_framebuffer_t *fb, const ngn_display_state_t *state)
{
    char pair[3] = {'R', state->radio_healthy ? '+' : '-', '\0'};
    char cal[3] = {'C', calibration_char(state->calibration), '\0'};
    char nodes[9] = {
        'A', node_status_char(node_at(state, 0u)), ' ',
        'B', node_status_char(node_at(state, 1u)), ' ',
        'C', node_status_char(node_at(state, 2u)), '\0'
    };

    draw_text(fb, 0, 0, "DBG");
    draw_text(fb, 0, 6, "S");
    draw_hex16(fb, 4, 6, state->session_tag);
    draw_text(fb, 0, 12, "E");
    draw_hex16(fb, 4, 12, state->epoch_tag);
    draw_text(fb, 0, 18, pair);
    draw_text(fb, 12, 18, cal);
    draw_text(fb, 0, 24, nodes);
}

bool ngn_display_render(ngn_framebuffer_t *fb,
                        const ngn_display_state_t *state,
                        ngn_display_view_t view)
{
    if (fb == NULL || state == NULL || view >= NGN_DISPLAY_VIEW_COUNT ||
        fb->data == NULL || fb->width == 0u || fb->height == 0u) {
        return false;
    }

    ngn_fb_clear(fb, false);

    switch (view) {
    case NGN_DISPLAY_VIEW_FIELD:
        render_field(fb, state);
        break;
    case NGN_DISPLAY_VIEW_BLE:
        render_ble(fb, state);
        break;
    case NGN_DISPLAY_VIEW_LINKS:
        render_links(fb, state);
        break;
    case NGN_DISPLAY_VIEW_DEBUG:
        render_debug(fb, state);
        break;
    case NGN_DISPLAY_VIEW_COUNT:
    default:
        return false;
    }
    return true;
}
