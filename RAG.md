# C3niffer NGGUNAGE — RAG authority

This file is the compact retrieval/index document for implementation work.

## Mission

Build a three-ESP32-C3 experimental ambient RF sensing array that:

- measures Wi-Fi CSI perturbations across a triangular arrangement;
- passively observes BLE advertisements;
- derives short-lived, explicitly non-identifying BLE observation tracks;
- correlates radio events without claiming person/device identity;
- renders an abstract live field on the OLED-equipped C3;
- records replayable data so sensing algorithms can be evaluated offline;
- optionally adds two ESP8266 D1 mini probe sources only after the C3 baseline works.

## Current baseline

Current implementation state:

- ESP32-C3 firmware foundation lives under `firmware/c3`;
- ESP32-C3 toolchain is pinned to **ESP-IDF v5.5.5**;
- C3 target is `esp32c3`;
- logical A/B/C identity is explicit, with a safe unconfigured default;
- board-specific OLED/button/BMS/GPIO values remain deliberately unassigned until verified;
- `ngn_core` is ESP-independent production code and is compiled by native host tests;
- GitHub Actions run native CTest and a pinned ESP32-C3 firmware build;
- NGN-004 implements BLE V1 as a NimBLE passive-observer adapter plus ESP-independent session-key/track logic;
- NGN-007 implements the hardware-independent monochrome framebuffer, FIELD/BLE/LINKS/DEBUG renderer and a configurable/default-disabled ESP32-C3 display-adapter seam;
- NGN-003 implements fixed-channel ESP32-C3 CSI acquisition, known-source admission, bounded callback capture, host-tested I/Q decoding and accepted-probe attribution;
- NGN-002 implements protocol v1, coordinator session discovery, MAC bindings and the deterministic A/B/C scheduler over fixed-channel broadcast ESP-NOW in station mode;
- configured A/B/C roles start that radio path; the unconfigured role remains radio-inactive;
- the default 390 ms epoch reserves a coexistence opportunity but does not start BLE scanning;
- CSI V1 remains magnitude/amplitude-first;
- BLE track events are privacy-safe by default: raw addresses remain transient, session keys change with the injected session nonce, and rotated/private addresses are not rejoined;
- CSI baseline/calibration, fusion and physical sensing acceptance remain unimplemented; physical CSI acquisition sanity is separated into NGN-0031 and full sensing/OLED acceptance remains NGN-009;
- ESP8266 support is optional and outside the critical path.

**NGN-002 recovery status (8 October 2026): [evidence-only checkpoint](https://github.com/techrote/c3niffernggnuage/blob/9ae3c066660a1254343efdf50c6efb5650327af6/checkpoints/prepass/NGN-002/d8185efc499cb91e/CHECKPOINT.md).** The original issue progress report and inspected repository/recovery evidence are preserved, but the reported local implementation candidate and raw candidate-test results were not recovered. The historical 5/5 report is not verified candidate evidence. NGN-002 has now been reconstructed from current main and the issue contract, with fresh native tests and a separately reviewable implementation PR linked to #2. The preserved archive remains a historical record, not the source of the new implementation or its acceptance results.

Do not silently upgrade ESP-IDF. A toolchain change needs its own evidence and documentation update.

## Architecture in one page

Three C3 nodes form logical vertices A, B and C. The OLED node is the coordinator/UI but remains a sensing participant.

A repeating radio epoch contains:

1. coordinator synchronization/control;
2. scheduled Wi-Fi probe bursts from A/B/C;
3. CSI capture at the non-transmitting nodes;
4. local reduction of CSI into bounded link evidence;
5. passive BLE observation opportunities under Wi-Fi/BLE coexistence;
6. transport of summaries to the coordinator;
7. fusion into a display state;
8. serial export of machine-readable records.

Timing values are configurable and experimental. The architecture must not assume that nominal BLE scan windows are fully serviced while Wi-Fi is active.

## Evidence layers

Keep these layers distinct:

### Layer 0 — raw observation

Examples:

- CSI I/Q bytes and Wi-Fi metadata;
- BLE advertisement address type, ephemeral address, RSSI and payload fields;
- local timestamps and packet sequence numbers.

### Layer 1 — normalized evidence

Examples:

- per-subcarrier magnitude;
- link perturbation score;
- link variance/correlation metrics;
- short-lived BLE observation key and per-node RSSI;
- radio health and missing-sample information.

### Layer 2 — fusion

Examples:

- triangle edge activity vector;
- aggregate activity score;
- ephemeral BLE track state;
- temporal BLE/CSI coincidence event;
- confidence/quality flags.

### Layer 3 — presentation

Examples:

- abstract triangle heat/field;
- peripheral BLE dots;
- diagnostic pages.

Presentation must never be the only representation of an observation.

## Privacy model

BLE is used as ambient radio evidence, not identity collection.

- Passive scan only in the baseline.
- Random/private-address rotation is accepted as a new short-lived observation when it cannot be safely joined.
- Session-scoped hashes/keys should intentionally change across sessions.
- Do not persist raw addresses by default.
- Do not infer ownership of a BLE device.

See `docs/06-PRIVACY-AND-SCOPE.md`.

## Programme IDs and dependencies

- **[NGN-001 / #1](https://github.com/techrote/c3niffernggnuage/issues/1)** — repository/firmware foundation and CI. No dependencies.
- **[NGN-002 / #2](https://github.com/techrote/c3niffernggnuage/issues/2)** — node protocol, discovery and radio scheduler. Depends on NGN-001.
- **[NGN-003 / #3](https://github.com/techrote/c3niffernggnuage/issues/3)** — bounded CSI acquisition, I/Q decoding and accepted-probe source attribution. Depends on NGN-002. Its software contract is documented in `docs/09-CSI-ACQUISITION.md`.
- **[NGN-004 / #4](https://github.com/techrote/c3niffernggnuage/issues/4)** — passive BLE observation pipeline. Depends on NGN-001; designed to proceed in parallel with NGN-002/003 while preserving boundaries.
- **[NGN-005 / #5](https://github.com/techrote/c3niffernggnuage/issues/5)** — baseline/perturbation signal engine. Depends on NGN-003.
- **[NGN-006 / #6](https://github.com/techrote/c3niffernggnuage/issues/6)** — coordinator fusion and event model. Depends on NGN-002, NGN-004 and NGN-005.
- **[NGN-007 / #7](https://github.com/techrote/c3niffernggnuage/issues/7)** — OLED driver/renderer/UI using synthetic state first. Depends on NGN-001; may proceed in parallel with sensing work.
- **[NGN-008 / #8](https://github.com/techrote/c3niffernggnuage/issues/8)** — serial capture, replay and experiment tooling. Depends on NGN-002, NGN-003 and NGN-004.
- **[NGN-009 / #9](https://github.com/techrote/c3niffernggnuage/issues/9)** — three-C3 integration and physical acceptance. Depends on NGN-005, NGN-006, NGN-007 and NGN-008.
- **[NGN-010 / #10](https://github.com/techrote/c3niffernggnuage/issues/10)** — optional ESP8266 illumination extension. Depends on NGN-009.

## Recommended parallel execution

After NGN-001:

- NGN-002, NGN-004 and NGN-007 can proceed independently.
- NGN-003 follows NGN-002.
- NGN-005 follows NGN-003.
- NGN-008 can begin once NGN-002/003/004 schemas are stable.
- NGN-006 waits for NGN-002/004/005.
- NGN-009 is the convergence/physical acceptance point.

## Key non-goals for V1

- absolute position in metres;
- centimetre-scale ranging;
- biometric identification;
- associating a BLE transmitter with a person;
- defeating BLE privacy mechanisms;
- phase-coherent multi-radio sensing;
- ML as a prerequisite for the first usable display;
- cloud dashboards;
- ESP8266 as a required component.

## Required reading map

- programme/dependencies: `docs/00-PROGRAMME.md`
- runtime/radio architecture: `docs/01-ARCHITECTURE.md`
- records and protocol contracts: `docs/02-PROTOCOL-AND-DATA.md`
- CSI/BLE/fusion algorithms: `docs/03-SENSING.md`
- test and acceptance rules: `docs/04-VALIDATION.md`
- toolchain/build/CI: `docs/05-BUILDING.md`
- BLE/privacy boundaries: `docs/06-PRIVACY-AND-SCOPE.md`
- implemented passive BLE component: `docs/07-BLE-OBSERVATION.md`
- hardware-independent OLED presentation: `docs/08-DISPLAY.md`
- implemented CSI acquisition: `docs/09-CSI-ACQUISITION.md`
- upstream references: `docs/REFERENCES.md`
