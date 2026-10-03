# 05 — Building and toolchain

## ESP32-C3 baseline

Pinned SDK:

```text
ESP-IDF v5.5.5
target: esp32c3
```

Do not silently switch to a newer ESP-IDF release.

Espressif's v5.5.5 release is the source authority for the pinned SDK. A typical source installation is:

```bash
git clone -b v5.5.5 --recursive https://github.com/espressif/esp-idf.git esp-idf-v5.5.5
cd esp-idf-v5.5.5
./install.sh esp32c3
. ./export.sh
```

Windows users may use Espressif's supported ESP-IDF environment/PowerShell flow for the same pinned release.

## Repository layout

NGN-001 establishes:

```text
firmware/c3/
  CMakeLists.txt
  sdkconfig.defaults
  config/
    node-a.defaults
    node-b.defaults
    node-c.defaults
  main/
    Kconfig.projbuild
  components/
    ngn_core/

tests/
  host/
  fixtures/

tools/
  python/
```

ESP-specific adapters live inside the firmware project. Project-specific Kconfig lives in the `main` component so ESP-IDF discovers it. Reusable production logic should remain in components that can be compiled by the native host harness when practical.

## Firmware build

From an activated ESP-IDF v5.5.5 environment:

```bash
cd firmware/c3
idf.py set-target esp32c3
idf.py build
```

The generated `firmware/c3/sdkconfig` is machine/build state and is ignored. Reproducible defaults live in `sdkconfig.defaults`.

### Logical node role

The default is intentionally **unconfigured**. A/B/C identity must be selected explicitly and never depends on flashing order.

Interactive selection:

```bash
cd firmware/c3
idf.py menuconfig
```

Choose **C3niffer NGGUNAGE foundation → Logical node role**.

For a clean reproducible role overlay:

```bash
cd firmware/c3
rm -f sdkconfig
idf.py set-target esp32c3
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;config/node-a.defaults" reconfigure
idf.py build
```

Use `node-b.defaults` or `node-c.defaults` for the other roles.

### Board profile

`CONFIG_NGN_BOARD_PROFILE` defaults to `generic-esp32c3`.

NGN-001 defines no OLED, button, battery/BMS, ADC or board-specific GPIO constants. Later issues must establish concrete profiles from verified evidence.

## Native tests

The native harness compiles the same `ngn_core` production source used by ESP-IDF without ESP-IDF headers.

From the repository root:

```bash
cmake -S tests/host -B build/host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host --parallel
ctest --test-dir build/host --output-on-failure
```

GCC/Clang builds use `-Wall -Wextra -Werror -pedantic`.

As protocol, sensing, fusion and rendering logic lands, extend this harness rather than embedding pure algorithms in radio/display adapters.

## Configuration ownership

Track `sdkconfig.defaults` and deliberate role/profile overlays. Do not commit machine-specific generated configuration unless an issue explicitly requires a fixture.

Expected feature configuration in later issues includes:

- Wi-Fi station mode;
- Wi-Fi CSI support;
- ESP-NOW;
- NimBLE;
- Wi-Fi/Bluetooth coexistence controls appropriate to the pinned IDF;
- serial console/logging.

NGN-002/003/004 own those actual Kconfig settings and must verify names/behavior against v5.5.5 documentation rather than copying settings from another release.

## Python tooling

`tools/python/` is reserved for NGN-008 capture/replay tooling.

NGN-001 deliberately introduces no Python runtime dependency. When Python tooling lands:

- prefer the standard library where reasonable;
- pin required third-party dependencies;
- keep behavior deterministic and local/offline;
- keep repository fixtures small;
- store large experiment captures outside git unless deliberately curated.

## CI

`.github/workflows/ci.yml` runs:

1. **Host tests** — configure/build `tests/host`, then run CTest.
2. **ESP32-C3 / ESP-IDF v5.5.5** — use Espressif's official `esp-idf-ci-action` with:
   - `esp_idf_version: v5.5.5`
   - `target: esp32c3`
   - `path: firmware/c3`

No attached hardware is required for ordinary PR CI.

Later issues extend these checks rather than replacing them.

## Flash and monitor

Once a role is configured and a board is connected:

```bash
cd firmware/c3
idf.py -p <PORT> flash monitor
```

The NGN-001 firmware only logs build/node/profile identity. It does not initialize Wi-Fi CSI, ESP-NOW, BLE, fusion or OLED logic.

## Test fixtures

`tests/fixtures/` holds small deterministic fixtures as their owning issues define real formats. NGN-001 intentionally does not invent CSI/BLE fixtures before those contracts exist.

## ESP8266

No ESP8266 build system is pinned in the baseline.

NGN-010 must:

- select and pin a maintained reproducible ESP8266/D1 mini toolchain;
- add its own CI job;
- keep it isolated from the C3 firmware build;
- document flash/build commands;
- avoid making the C3 baseline depend on it.

## Toolchain upgrades

An upgrade issue must provide:

- reason for upgrade;
- upstream compatibility evidence;
- clean build/test evidence;
- migration notes;
- rollback path;
- explicit change to this file and RAG.
