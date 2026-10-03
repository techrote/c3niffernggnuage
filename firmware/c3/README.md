# ESP32-C3 firmware

This directory contains the shared ESP32-C3 firmware foundation.

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

Then choose **C3niffer NGGUNAGE foundation → Logical node role**.

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

The foundation firmware only logs:

- firmware identity;
- protocol identity;
- ESP-IDF version;
- selected logical node;
- board-profile name.

It does not initialize ESP-NOW, Wi-Fi CSI, BLE, fusion or OLED behavior.
