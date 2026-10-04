# 08 — Tiny OLED presentation

NGN-007 implements the presentation layer without making any claim about the final OLED module fitted to the physical board.

## Boundaries

The display stack has two independent components:

- `ngn_display` is pure C and has no ESP-IDF, radio, BLE or CSI dependency. It owns the monochrome framebuffer, drawing primitives, the compact display-state contract and the four views.
- `ngn_oled_esp` is an ESP32-C3 I2C/GPIO adapter. It owns only bus/device setup, optional reset/button GPIO handling and a controller-driver callback seam.

The renderer consumes `ngn_display_state_t`. Radio callbacks, BLE observations, CSI samples and protocol packets must be normalized/fused elsewhere before presentation.

## Framebuffer contract

`ngn_framebuffer_t` is a one-bit row-major framebuffer. Width and height are supplied at runtime. Each row is padded to the next byte and pixels are stored most-significant-bit first within each byte.

All public drawing operations clip to the framebuffer. Out-of-range pixels are ignored, and line segments are clipped before rasterization so a large off-screen coordinate does not create a long draw loop.

The framebuffer format is intentionally independent of an OLED controller's native page layout. A controller driver is responsible for any page/column conversion needed when presenting it.

## Display-state contract

The presentation state contains only bounded display-oriented values:

- AB, BC and CA activity plus quality/validity;
- a global activity emphasis value;
- up to eight ephemeral BLE display tracks;
- A/B/C node health;
- short session/epoch diagnostic tags;
- radio-health and calibration state.

A BLE display track contains a session-scoped tag, lifecycle state, normalized screen coordinates, relative emphasis, receiver-presence mask and a temporal-coincidence flag. It contains no raw Bluetooth address and no distance field.

Normalized BLE screen coordinates are presentation inputs. They are not metres, range estimates or calibrated physical positions.

## Views

### FIELD

Renders the abstract A/B/C triangle. AB/BC/CA activity changes edge-midpoint emphasis. A renderer-only weighted marker is placed from the three edge midpoints and their activity values. This is a visual encoding only; the marker is not exported as a location estimate.

Healthy, degraded and missing nodes use different symbols. Missing edge evidence is marked explicitly rather than silently treated as zero activity.

### BLE

Renders bounded ephemeral advertiser symbols:

- `NEW` — diamond;
- `ACTIVE` — square;
- `AGING` — cross;
- `EXPIRED` — omitted.

Relative emphasis changes symbol size. A temporal BLE/CSI coincidence may add a brief ring/halo. Receiver-count ticks describe evidence availability only; they are not a ranging scale.

### LINKS

Renders AB, BC and CA as compact activity bars with quality ticks. Missing evidence is crossed out.

### DEBUG

Renders compact session/epoch tags, radio state, calibration state and A/B/C node health using a 3x5 font. The layout is deliberately sparse and clips cleanly on very small displays.

## ESP32-C3 adapter and unresolved board evidence

No authoritative source in the repository currently establishes the physical 0.42-inch board's:

- controller model;
- pixel dimensions;
- I2C address;
- SDA/SCL GPIOs;
- reset wiring;
- page-button GPIO or polarity.

NGN-007 therefore does not assign any of those values.

`ngn_oled_esp_config_t` requires an explicit runtime configuration containing dimensions, I2C port/address/speed and SDA/SCL pins. Reset and button pins are optional behind explicit `reset_enabled` / `button_enabled` flags. Their pin numbers, polarity and button pull-up/pull-down behavior are explicit fields; leaving either enable flag false touches no GPIO for that feature.

Controller-specific command streams are supplied through `ngn_oled_controller_ops_t`. NGN-007 deliberately provides no guessed SSD1306/SH1106/etc. controller implementation. A zero-initialized adapter configuration is disabled (`enabled == false`) and cannot touch hardware.
The `ngn_oled_esp_t` adapter object itself must also be zero-initialized before its first `ngn_oled_esp_init()` call.

This preserves a compile-tested ESP-IDF seam while deferring the physical controller/profile binding to verified board evidence and NGN-009 hardware acceptance.

## Development and tests

Synthetic display state exists only in `tests/host/test_ngn_display.c`; it is not a production sensing path.

Native tests cover:

- guarded framebuffer bounds and off-screen clipping;
- line, triangle and symbol primitives;
- deterministic 72x40 snapshot hashes for FIELD, BLE, LINKS and DEBUG;
- missing/degraded node and link rendering;
- 1x1, 3x2, 8x4 and 13x7 rendering with canary bytes around framebuffer storage.

The normal repository CI also compiles the ESP adapter for the pinned ESP-IDF v5.5.5 / ESP32-C3 target. Physical display confirmation remains NGN-009 work.
