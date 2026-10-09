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

The current project includes:

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
    ngn_core/       # node, BLE, protocol/schedule/session and host-testable CSI core
    ngn_radio_esp/  # bounded ESP-NOW transport adapter
    ngn_csi_esp/    # bounded pinned-SDK CSI callback/worker adapter
    ngn_ble_esp/    # separate passive BLE adapter
    ngn_display/   # pure framebuffer/renderer
    ngn_oled_esp/  # separately configured display adapter

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

Choose **C3niffer NGGUNAGE → Logical node role**.

For a clean reproducible role overlay:

```bash
cd firmware/c3
rm -f sdkconfig
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;config/node-a.defaults" set-target esp32c3
idf.py build
```

Use `node-b.defaults` or `node-c.defaults` for the other roles.

### Radio configuration

Under **C3niffer NGGUNAGE → Three-node radio schedule**, all durations are milliseconds:

| Kconfig suffix (prefix `CONFIG_NGN_RADIO_`) | Default | Allowed range |
| --- | ---: | ---: |
| `CHANNEL` | 6 | 1–11 |
| `BURST_COUNT` | 4 | 1–16 |
| `PROBE_SPACING_MS` | 10 | 1–65535 |
| `SYNC_SLOT_MS` | 40 | 1–65535 |
| `PROBE_SLOT_MS` | 80 | 1–65535 |
| `COEXIST_MS` | 80 | 0–65535 |
| `HEALTH_SLOT_MS` | 10 | 1–65535 |
| `MISSING_EPOCHS` | 4 | 1–32 |
| `RX_QUEUE_DEPTH`, `TX_QUEUE_DEPTH`, `STATUS_QUEUE_DEPTH` | 16 each | 1–64 each |

Before radio startup, the complete configuration must satisfy:

- `BURST_COUNT * PROBE_SPACING_MS <= PROBE_SLOT_MS`;
- `SYNC_SLOT_MS + 3 * PROBE_SLOT_MS + COEXIST_MS + 3 * HEALTH_SLOT_MS <= 60000`.

Invalid combinations fail closed before Wi-Fi starts. All nodes must use the same physical channel. A/B adopt C's valid timing configuration from SYNC on first/new session; timing cannot change inside one session. The default epoch is 390 ms, and the default missing timeout is four epochs (1560 ms). These are configurable experimental defaults. The coexistence interval is a placeholder and starts no BLE scan. See [the exact wire and schedule contract](02-PROTOCOL-AND-DATA.md).

### CSI acquisition configuration

NGN-003 enables the pinned Wi-Fi driver CSI feature in tracked defaults:

```text
CONFIG_ESP_WIFI_CSI_ENABLED=y
```

Project settings under **C3niffer NGGUNAGE → Wi-Fi CSI acquisition** are:

| Kconfig suffix (prefix `CONFIG_NGN_CSI_`) | Default | Allowed range |
| --- | ---: | ---: |
| `RAW_QUEUE_DEPTH` | 8 | 1–32 |
| `PACKET_QUEUE_DEPTH` | 4 | 1–16 |
| `ATTRIBUTION_WINDOW_MS` | 5 ms | 1–50 ms |
| `DIAGNOSTIC_RAW` | off | boolean |

The CSI adapter starts only after NGN-002 has initialized Wi-Fi station mode on
the fixed channel. It enables promiscuous receive and CSI on that existing
interface; it does not associate with an AP or create an IP interface.
`DIAGNOSTIC_RAW` is a development-only local serial stream and can be
high-volume. It is not the NGN-008 capture format.

See `docs/09-CSI-ACQUISITION.md` for the exact callback/worker and record
contracts.

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

Eight suites cover node identity, the unchanged BLE and display contracts, protocol v1, deterministic scheduling, radio health/integration, adversarial session/transport behavior and NGN-003 CSI acquisition/attribution. These compile production core sources; no ESP-IDF headers or physical radio are required. Later sensing and fusion work extends this harness.

## Configuration ownership

Track `sdkconfig.defaults` and deliberate role/profile overlays. Do not commit machine-specific generated configuration unless an issue explicitly requires a fixture.

NGN-002 selects Wi-Fi station mode, fixed channel, RAM configuration and no power saving through the pinned SDK APIs. It does not create an IP interface or associate with an AP. NGN-003 enables the pinned Wi-Fi CSI feature and, after radio startup, enables promiscuous receive plus bounded CSI capture on that same interface. NGN-004's NimBLE observer/coexistence configuration remains intact; compiling those components does not start BLE scanning.

`CONFIG_FREERTOS_HZ=1000` supplies a nominal one-millisecond scheduler poll. The runtime always delays at least one tick, so a user-selected slower tick remains safe but can skip more expired opportunities. This is a software scheduling default, not a measured airtime-accuracy claim.

For `esp32c3`, do not copy `CONFIG_BTDM_CTRL_MODE_*` selectors from ESP32 or multi-target examples. ESP-IDF v5.5.5 does not define those controller-mode symbols for ESP32-C3; assigning them only produces unknown-symbol warnings. The NGN-004 observer profile is narrowed with the C3-valid NimBLE role, GATT and Security Manager controls plus passive GAP discovery.

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
3. **ESP32-C3 node C / ESP-IDF v5.5.5** — the same pinned target/action, with
   `idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;config/node-c.defaults" build`.
   This explicit coordinator profile links the actual radio startup, random
   session and NGN-003 CSI startup path into the final image, while the original
   job retains coverage of the safe unconfigured default.

No attached hardware is required for ordinary PR CI.

Later issues extend these checks rather than replacing them.

## Flash and monitor

Once a role is configured and a board is connected:

```bash
cd firmware/c3
idf.py -p <PORT> flash monitor
```

Unconfigured firmware logs its identity and returns before radio startup. Configured A/B/C firmware starts fixed-channel broadcast ESP-NOW and the NGN-003 CSI adapter; C creates a fresh nonzero 64-bit session after Wi-Fi starts, while A/B wait for C's SYNC. A follower becomes silent after its accepted epoch until a newer SYNC; the longer default 1560 ms silence threshold enters discovery.

The runtime has static storage and a 6144-byte priority-5 worker. The transport worker has a 4096-byte priority-5 stack. NGN-003 adds a 6144-byte priority-2 CSI decode worker plus bounded raw/output queues and a 4096-byte priority-1 decoded-output drain. A separate priority-1 logger with a 3072-byte stack receives a one-record overwrite snapshot queue; the core event sink only marks diagnostics pending. Logs report session/epoch/state, present mask, counters and logical-node/station-MAC mappings. State values are 0 discovering, 1 synchronized and 2 waiting for SYNC. Stack watermarks and physical timing remain hardware measurements.

The radio/CSI adapters have no normal stop/reconfiguration lifecycle yet. A fatal task-creation, CSI-startup or nonce-generation failure after radio startup requires restart. BLE scans, baseline/perturbation processing, fusion and OLED behavior remain inactive.

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
