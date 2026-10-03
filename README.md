# C3niffer NGGUNAGE

**C3niffer NGGUNAGE** is an experimental distributed 2.4 GHz ambient-sensing array built around three ESP32-C3 nodes.

The project combines:

- Wi-Fi Channel State Information (CSI) perturbation sensing;
- passive Bluetooth Low Energy advertisement observations;
- short-lived correlation/fusion of those two signal sources;
- a tiny on-device graphical view on the OLED-equipped C3;
- serial data capture and replay for proper experiments;
- optional ESP8266 D1 mini probe/illumination nodes after the three-C3 baseline is proven.

The name **NGGUNAGE** originated as a gloriously meaningless automatic-title artefact and is intentionally retained.

## Intended hardware

Baseline programme:

- 2× ESP32-C3 battery/BMS boards;
- 1× ESP32-C3 board with a 0.42-inch OLED;
- the three C3s arranged roughly as a triangle around a sensing area.

Optional later extension:

- 2× ESP8266 D1 mini boards as deterministic Wi-Fi packet sources.

No external router is required by the intended C3-to-C3 sensing mode.

## What the system should show

The OLED is not intended to claim radar-like absolute positioning. It should render an **abstract RF field view** derived from measured link perturbations plus short-lived BLE observations.

Examples of useful states include:

- quiet / calibrated field;
- movement or channel perturbation concentrated toward particular triangle links;
- a newly observed BLE advertiser;
- increasing/decreasing BLE RSSI;
- a temporal coincidence between a new BLE observation and a Wi-Fi CSI disturbance;
- health/diagnostic pages for link quality, packet rate, calibration state and radio scheduling.

The system must distinguish **measured observations** from **inferences**. A BLE advertiser appearing at the same time as a CSI disturbance is a correlation event; it is not proof that the device belongs to the person causing the disturbance.

## Baseline technical choices

- ESP32-C3 firmware: **ESP-IDF v5.5.5**, pinned for reproducibility.
- BLE host: **NimBLE**, passive scanning only for the baseline.
- Wi-Fi sensing: ESP-IDF CSI APIs with fixed-channel scheduled probe traffic.
- Inter-node/control transport: ESP-NOW in Wi-Fi station mode unless an implementation issue produces measured evidence for a better supported arrangement.
- CSI V1: amplitude/magnitude-derived features first; phase-dependent methods are deferred.
- BLE V1: short-lived observation tracks; no connection attempts and no long-term identity reconstruction.
- Host tooling: Python for capture, replay, experiment labelling and offline analysis.

See [RAG.md](RAG.md) and the numbered documents under [docs/](docs/) for programme authority.

## Foundation

NGN-001 establishes:

- the ESP-IDF v5.5.5 project under `firmware/c3`;
- a safe explicit A/B/C node-role configuration seam;
- a board-profile seam with no invented OLED/BMS/GPIO values;
- reusable ESP-independent production logic under `firmware/c3/components`;
- native CMake/CTest coverage under `tests/host`;
- reserved `tools/python` and `tests/fixtures` paths for later issues;
- GitHub Actions for native tests and an ESP32-C3 firmware build.

The default node role is intentionally **unconfigured**. See [firmware/c3/README.md](firmware/c3/README.md) and [docs/05-BUILDING.md](docs/05-BUILDING.md).

## Programme shape

The implementation is deliberately staged:

1. [NGN-001 / #1](https://github.com/techrote/c3niffernggnuage/issues/1) — repository/build/test foundation;
2. [NGN-002 / #2](https://github.com/techrote/c3niffernggnuage/issues/2) — deterministic node protocol and radio scheduler;
3. [NGN-003 / #3](https://github.com/techrote/c3niffernggnuage/issues/3) — CSI acquisition;
4. [NGN-004 / #4](https://github.com/techrote/c3niffernggnuage/issues/4) — passive BLE acquisition;
5. [NGN-005 / #5](https://github.com/techrote/c3niffernggnuage/issues/5) — CSI baseline and perturbation metrics;
6. [NGN-006 / #6](https://github.com/techrote/c3niffernggnuage/issues/6) — coordinator fusion;
7. [NGN-007 / #7](https://github.com/techrote/c3niffernggnuage/issues/7) — OLED rendering;
8. [NGN-008 / #8](https://github.com/techrote/c3niffernggnuage/issues/8) — capture/replay tooling;
9. [NGN-009 / #9](https://github.com/techrote/c3niffernggnuage/issues/9) — three-C3 hardware integration and acceptance;
10. [NGN-010 / #10](https://github.com/techrote/c3niffernggnuage/issues/10) — optional ESP8266 illumination extension.

After NGN-001, #2, #4 and #7 are designed to proceed independently.

## Experimental posture

Indoor 2.4 GHz propagation is strongly affected by multipath. The project therefore treats spatial interpretation as an empirical problem.

The first useful success criterion is **repeatable field perturbation visualization**, not centimetre-scale localization or guaranteed static occupancy detection. Collected data should be retained in a replayable form so later algorithms can be evaluated against the same observations.

## Authority

Read in this order before implementation work:

1. `AGENTS.md`
2. `RAG.md`
3. `docs/00-PROGRAMME.md`
4. `docs/01-ARCHITECTURE.md`
5. the remaining relevant `docs/` material
6. the complete GitHub issue body and comments for the task being executed

When issue-specific instructions conflict with general documentation, the issue may narrow scope but must not silently override architectural, privacy or validation constraints. Record any necessary authority change in the same PR.

## Status

The NGN-001 foundation is implemented without radio-sensing or display behavior. No physical sensing acceptance is implied; hardware acceptance remains owned by NGN-009.
