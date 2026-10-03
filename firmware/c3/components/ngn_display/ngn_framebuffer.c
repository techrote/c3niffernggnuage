#include "ngn_display.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

static bool fb_valid(const ngn_framebuffer_t *fb)
{
    if (fb == NULL || fb->data == NULL || fb->width == 0u || fb->height == 0u ||
        fb->stride == 0u || fb->stride > SIZE_MAX / (size_t)fb->height) {
        return false;
    }
    return fb->data_size >= fb->stride * (size_t)fb->height;
}

size_t ngn_fb_storage_bytes(uint16_t width, uint16_t height)
{
    size_t stride;

    if (width == 0u || height == 0u) {
        return 0u;
    }

    stride = ((size_t)width + 7u) / 8u;
    if (stride > SIZE_MAX / (size_t)height) {
        return 0u;
    }
    return stride * (size_t)height;
}

bool ngn_fb_init(ngn_framebuffer_t *fb,
                 uint16_t width,
                 uint16_t height,
                 uint8_t *storage,
                 size_t storage_size)
{
    const size_t needed = ngn_fb_storage_bytes(width, height);

    if (fb == NULL || storage == NULL || needed == 0u || storage_size < needed) {
        return false;
    }

    fb->width = width;
    fb->height = height;
    fb->stride = ((size_t)width + 7u) / 8u;
    fb->data = storage;
    fb->data_size = needed;
    return true;
}

void ngn_fb_clear(ngn_framebuffer_t *fb, bool on)
{
    if (!fb_valid(fb)) {
        return;
    }
    memset(fb->data, on ? 0xff : 0x00, fb->data_size);

    if (on && (fb->width & 7u) != 0u) {
        const uint8_t mask = (uint8_t)(0xffu << (8u - (fb->width & 7u)));
        for (uint16_t y = 0u; y < fb->height; ++y) {
            fb->data[(size_t)y * fb->stride + fb->stride - 1u] &= mask;
        }
    }
}

void ngn_fb_set_pixel(ngn_framebuffer_t *fb, int32_t x, int32_t y, bool on)
{
    size_t offset;
    uint8_t mask;

    if (!fb_valid(fb) || x < 0 || y < 0 || x >= (int32_t)fb->width ||
        y >= (int32_t)fb->height) {
        return;
    }

    offset = (size_t)y * fb->stride + (size_t)x / 8u;
    mask = (uint8_t)(0x80u >> ((uint32_t)x & 7u));
    if (on) {
        fb->data[offset] |= mask;
    } else {
        fb->data[offset] &= (uint8_t)~mask;
    }
}

bool ngn_fb_get_pixel(const ngn_framebuffer_t *fb, int32_t x, int32_t y)
{
    size_t offset;
    uint8_t mask;

    if (!fb_valid(fb) || x < 0 || y < 0 || x >= (int32_t)fb->width ||
        y >= (int32_t)fb->height) {
        return false;
    }

    offset = (size_t)y * fb->stride + (size_t)x / 8u;
    mask = (uint8_t)(0x80u >> ((uint32_t)x & 7u));
    return (fb->data[offset] & mask) != 0u;
}

enum {
    CLIP_LEFT = 1,
    CLIP_RIGHT = 2,
    CLIP_TOP = 4,
    CLIP_BOTTOM = 8
};

static uint8_t clip_code(const ngn_framebuffer_t *fb, int32_t x, int32_t y)
{
    uint8_t code = 0u;

    if (x < 0) {
        code |= CLIP_LEFT;
    } else if (x >= (int32_t)fb->width) {
        code |= CLIP_RIGHT;
    }
    if (y < 0) {
        code |= CLIP_TOP;
    } else if (y >= (int32_t)fb->height) {
        code |= CLIP_BOTTOM;
    }
    return code;
}

static bool clip_line(const ngn_framebuffer_t *fb,
                      int32_t *x0,
                      int32_t *y0,
                      int32_t *x1,
                      int32_t *y1)
{
    uint8_t code0;
    uint8_t code1;

    if (!fb_valid(fb) || x0 == NULL || y0 == NULL || x1 == NULL || y1 == NULL) {
        return false;
    }

    code0 = clip_code(fb, *x0, *y0);
    code1 = clip_code(fb, *x1, *y1);

    for (unsigned iteration = 0u; iteration < 8u; ++iteration) {
        int32_t x;
        int32_t y;
        uint8_t outside;
        const int64_t dx = (int64_t)*x1 - (int64_t)*x0;
        const int64_t dy = (int64_t)*y1 - (int64_t)*y0;

        if ((code0 | code1) == 0u) {
            return true;
        }
        if ((code0 & code1) != 0u) {
            return false;
        }

        outside = code0 != 0u ? code0 : code1;

        if ((outside & CLIP_TOP) != 0u) {
            if (dy == 0) {
                return false;
            }
            y = 0;
            x = (int32_t)((int64_t)*x0 + dx * (-(int64_t)*y0) / dy);
        } else if ((outside & CLIP_BOTTOM) != 0u) {
            const int32_t boundary = (int32_t)fb->height - 1;
            if (dy == 0) {
                return false;
            }
            y = boundary;
            x = (int32_t)((int64_t)*x0 + dx * ((int64_t)boundary - *y0) / dy);
        } else if ((outside & CLIP_RIGHT) != 0u) {
            const int32_t boundary = (int32_t)fb->width - 1;
            if (dx == 0) {
                return false;
            }
            x = boundary;
            y = (int32_t)((int64_t)*y0 + dy * ((int64_t)boundary - *x0) / dx);
        } else {
            if (dx == 0) {
                return false;
            }
            x = 0;
            y = (int32_t)((int64_t)*y0 + dy * (-(int64_t)*x0) / dx);
        }

        if (outside == code0) {
            *x0 = x;
            *y0 = y;
            code0 = clip_code(fb, *x0, *y0);
        } else {
            *x1 = x;
            *y1 = y;
            code1 = clip_code(fb, *x1, *y1);
        }
    }

    return false;
}

static int32_t abs_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

void ngn_fb_draw_line(ngn_framebuffer_t *fb,
                      int32_t x0,
                      int32_t y0,
                      int32_t x1,
                      int32_t y1,
                      bool on)
{
    int32_t dx;
    int32_t sx;
    int32_t dy;
    int32_t sy;
    int32_t error;

    if (!clip_line(fb, &x0, &y0, &x1, &y1)) {
        return;
    }

    dx = abs_i32(x1 - x0);
    sx = x0 < x1 ? 1 : -1;
    dy = -abs_i32(y1 - y0);
    sy = y0 < y1 ? 1 : -1;
    error = dx + dy;

    for (;;) {
        const int32_t twice_error = error * 2;
        ngn_fb_set_pixel(fb, x0, y0, on);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        if (twice_error >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice_error <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void ngn_fb_fill_rect(ngn_framebuffer_t *fb,
                      int32_t x,
                      int32_t y,
                      int32_t width,
                      int32_t height,
                      bool on)
{
    int64_t right64;
    int64_t bottom64;
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;

    if (!fb_valid(fb) || width <= 0 || height <= 0) {
        return;
    }

    right64 = (int64_t)x + (int64_t)width - 1;
    bottom64 = (int64_t)y + (int64_t)height - 1;
    if (right64 < 0 || bottom64 < 0 || x >= (int32_t)fb->width ||
        y >= (int32_t)fb->height) {
        return;
    }

    left = x < 0 ? 0 : x;
    top = y < 0 ? 0 : y;
    right = right64 >= (int64_t)fb->width ? (int32_t)fb->width - 1 : (int32_t)right64;
    bottom = bottom64 >= (int64_t)fb->height ? (int32_t)fb->height - 1 : (int32_t)bottom64;

    for (int32_t yy = top; yy <= bottom; ++yy) {
        for (int32_t xx = left; xx <= right; ++xx) {
            ngn_fb_set_pixel(fb, xx, yy, on);
        }
    }
}

void ngn_fb_draw_triangle(ngn_framebuffer_t *fb,
                          int32_t ax,
                          int32_t ay,
                          int32_t bx,
                          int32_t by,
                          int32_t cx,
                          int32_t cy,
                          bool on)
{
    ngn_fb_draw_line(fb, ax, ay, bx, by, on);
    ngn_fb_draw_line(fb, bx, by, cx, cy, on);
    ngn_fb_draw_line(fb, cx, cy, ax, ay, on);
}

static void draw_ring(ngn_framebuffer_t *fb, int32_t cx, int32_t cy, uint8_t radius, bool on)
{
    int32_t x;
    int32_t y;
    int32_t decision;

    if (radius == 0u) {
        ngn_fb_set_pixel(fb, cx, cy, on);
        return;
    }

    x = (int32_t)radius;
    y = 0;
    decision = 1 - x;

    while (y <= x) {
        ngn_fb_set_pixel(fb, cx + x, cy + y, on);
        ngn_fb_set_pixel(fb, cx + y, cy + x, on);
        ngn_fb_set_pixel(fb, cx - y, cy + x, on);
        ngn_fb_set_pixel(fb, cx - x, cy + y, on);
        ngn_fb_set_pixel(fb, cx - x, cy - y, on);
        ngn_fb_set_pixel(fb, cx - y, cy - x, on);
        ngn_fb_set_pixel(fb, cx + y, cy - x, on);
        ngn_fb_set_pixel(fb, cx + x, cy - y, on);

        ++y;
        if (decision <= 0) {
            decision += 2 * y + 1;
        } else {
            --x;
            decision += 2 * (y - x) + 1;
        }
    }
}

void ngn_fb_draw_symbol(ngn_framebuffer_t *fb,
                        ngn_symbol_t symbol,
                        int32_t x,
                        int32_t y,
                        uint8_t size,
                        bool on)
{
    const int32_t radius = size == 0u ? 1 : (size > 8u ? 8 : (int32_t)size);

    switch (symbol) {
    case NGN_SYMBOL_DOT:
        ngn_fb_fill_rect(fb, x - radius + 1, y - radius + 1,
                         radius * 2 - 1, radius * 2 - 1, on);
        break;
    case NGN_SYMBOL_SQUARE:
        ngn_fb_draw_line(fb, x - radius, y - radius, x + radius, y - radius, on);
        ngn_fb_draw_line(fb, x + radius, y - radius, x + radius, y + radius, on);
        ngn_fb_draw_line(fb, x + radius, y + radius, x - radius, y + radius, on);
        ngn_fb_draw_line(fb, x - radius, y + radius, x - radius, y - radius, on);
        break;
    case NGN_SYMBOL_DIAMOND:
        ngn_fb_draw_line(fb, x, y - radius, x + radius, y, on);
        ngn_fb_draw_line(fb, x + radius, y, x, y + radius, on);
        ngn_fb_draw_line(fb, x, y + radius, x - radius, y, on);
        ngn_fb_draw_line(fb, x - radius, y, x, y - radius, on);
        break;
    case NGN_SYMBOL_CROSS:
        ngn_fb_draw_line(fb, x - radius, y - radius, x + radius, y + radius, on);
        ngn_fb_draw_line(fb, x - radius, y + radius, x + radius, y - radius, on);
        break;
    case NGN_SYMBOL_RING:
        draw_ring(fb, x, y, (uint8_t)radius, on);
        break;
    default:
        break;
    }
}
