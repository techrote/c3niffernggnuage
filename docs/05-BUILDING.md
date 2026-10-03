# 05 — Building and toolchain

## ESP32-C3 baseline

Pinned SDK:

```text
ESP-IDF v5.5.5
target: esp32c3
```

Do not silently switch to a newer ESP-IDF release.

Typical environment:

```bash
git clone -b v5.5.5 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32c3
. ./export.sh
```

Windows users may use Espressif's supported ESP-IDF environment/PowerShell flow for the same pinned release.

The project structure established by NGN-001 should make the normal firmware path obvious, for example:

```bash
cd firmware/c3
idf.py set-target esp32c3
idf.py build
```

Exact paths are authoritative once NGN-001 lands.

## Configuration

Track `sdkconfig.defaults` or equivalent reproducible defaults.

Do not commit machine-specific generated configuration unless an issue explicitly requires a fixture.

Expected feature configuration eventually includes:

- Wi-Fi station mode;
- Wi-Fi CSI support;
- ESP-NOW;
- NimBLE;
- Wi-Fi/Bluetooth coexistence controls appropriate to the pinned IDF;
- serial console/logging.

NGN-001/003/004 own the actual Kconfig values and must verify them against v5.5.5 documentation rather than copying names from a different IDF release.

## Native tests

Pure signal/protocol/fusion/render logic should be buildable on a normal host compiler without ESP hardware.

Preferred pattern:

```text
components/
  protocol/
  sensing/
  fusion/
  render/
tests/host/
```

Hardware adapters may depend on ESP-IDF; core logic should not.

Use CMake/CTest or another minimal deterministic host harness selected in NGN-001.

## Python tooling

Host capture/replay tools live outside firmware and use a pinned dependency file.

Prefer the Python standard library where reasonable.

Required behavior:

- clear CLI help;
- deterministic parsing;
- no network/cloud requirement;
- test fixtures small enough for the repository;
- large experiment captures stored outside git or as deliberately curated compressed fixtures.

## CI

NGN-001 should establish GitHub Actions jobs for:

1. host tests;
2. ESP32-C3 build with Espressif's pinned v5.5.5 environment.

Later issues extend CI rather than replacing it.

Do not require attached hardware for ordinary PR CI.

## Flash/monitor

Once firmware exists, document exact commands in the repository. Typical form:

```bash
idf.py -p <PORT> flash monitor
```

Multiple-node experiments should use explicit node-role configuration so flashing order does not determine identity.

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
