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

At programme bootstrap:

- repository contains planning/authority material only;
- no firmware implementation or hardware acceptance exists;
- ESP32-C3 toolchain is pinned to **ESP-IDF v5.5.5**;
- C3 target is `esp32c3`;
- BLE baseline uses NimBLE passive scanning;
- Wi-Fi CSI baseline uses fixed-channel scheduled probe traffic;
- ESP-NOW in station mode is the intended C3 transport/probe mechanism;
- CSI V1 uses magnitude/amplitude-derived information before phase-dependent work;
- the OLED board's exact controller, resolution and pins must be verified from the actual board before driver constants are treated as authoritative;
- ESP8266 support is optional and outside the critical path.

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

- **NGN-001** — repository/firmware foundation and CI. No dependencies.
- **NGN-002** — node protocol, discovery and radio scheduler. Depends on NGN-001.
- **NGN-003** — CSI acquisition and source attribution. Depends on NGN-002.
- **NGN-004** — passive BLE observation pipeline. Depends on NGN-001; designed to proceed in parallel with NGN-002/003 while preserving boundaries.
- **NGN-005** — baseline/perturbation signal engine. Depends on NGN-003.
- **NGN-006** — coordinator fusion and event model. Depends on NGN-002, NGN-004 and NGN-005.
- **NGN-007** — OLED driver/renderer/UI using synthetic state first. Depends on NGN-001; may proceed in parallel with sensing work.
- **NGN-008** — serial capture, replay and experiment tooling. Depends on NGN-002, NGN-003 and NGN-004.
- **NGN-009** — three-C3 integration and physical acceptance. Depends on NGN-005, NGN-006, NGN-007 and NGN-008.
- **NGN-010** — optional ESP8266 illumination extension. Depends on NGN-009.

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
- upstream references: `docs/REFERENCES.md`
