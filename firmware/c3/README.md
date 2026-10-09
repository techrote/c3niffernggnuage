# ESP32-C3 firmware

This directory contains the shared ESP32-C3 firmware, including the NGN-002 protocol and scheduler.

## Toolchain

Use exactly:

- ESP-IDF v5.5.5
- target `esp32c3`

From an activated v5.5.5 ESP-IDF environment:

```bash
cd firmware/c3
idf.py set-target esp32c3
idf.py build
```

Generated `sdkconfig` is intentionally ignored. Reproducible project defaults live in `sdkconfig.defaults`.

## Logical node role

The safe default is **unconfigured**. A/B/C identity must be selected explicitly and never depends on flashing or discovery order.

Interactive configuration:

```bash
idf.py menuconfig
```

Then choose **C3niffer NGGUNAGE → Logical node role**.

For reproducible role-specific configuration from a clean generated configuration, use the supplied overlays:

```bash
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;config/node-a.defaults" reconfigure
idf.py build
```

Replace `node-a.defaults` with `node-b.defaults` or `node-c.defaults` as required. If `sdkconfig` already exists, remove it before expecting defaults to change the selected role.

## Board profile seam

`CONFIG_NGN_BOARD_PROFILE` currently defaults to `generic-esp32c3`.

NGN-001 intentionally defines **no OLED, button, battery/BMS, ADC or board-specific GPIO values**. Later issues must add concrete board profiles only from verified hardware/documentation evidence.

## Current behavior

The unconfigured default logs build/node/profile identity and starts no radio. Configured A/B/C roles start fixed-channel broadcast ESP-NOW in Wi-Fi station mode without an access point. C creates a session and advances the epoch; A/B discover C and transmit only in an epoch established by its accepted SYNC.

Protocol v1 carries exact SYNC, PROBE and NODE_HEALTH messages. The experimental default is channel 6 with a 390 ms epoch, four probes per node at 10 ms spacing, an 80 ms coexistence opportunity placeholder and staggered health slots. RX/TX/status queues default to 16 records each. Expired or obsolete-session queued work is dropped. Session, logical-node/station-MAC bindings and radio health are observable in serial diagnostics.

Configure **C3niffer NGGUNAGE → Three-node radio schedule** and keep all nodes on the same channel. See [building/configuration](../../docs/05-BUILDING.md) and [the exact protocol contract](../../docs/02-PROTOCOL-AND-DATA.md) for all bounds and wire offsets.

This path starts no CSI acquisition, BLE scanning, fusion or OLED behavior. The NGN-004/007 components remain separate. No physical radio or sensing acceptance is implied.
