# 00 — Programme

## Goal

Produce a reproducible experimental device that turns a small three-node ESP32-C3 arrangement into a live visualization of local 2.4 GHz RF activity.

The first release is successful when it can:

- maintain a deterministic three-C3 radio schedule;
- collect attributed CSI from known probe sources;
- derive stable empty-room baselines and useful perturbation metrics;
- passively discover BLE advertisements and maintain short-lived observation tracks;
- fuse those sources without overstating what they mean;
- show a responsive abstract field on the OLED node;
- export enough structured data to reproduce and inspect experiments on a PC;
- demonstrate the complete path on physical hardware.

## Planning review decisions

The initial concept was refined before implementation:

1. **ESP-IDF v5.5.5 is pinned** rather than immediately adopting 6.1. Current Espressif CSI examples are explicitly CI-tested on the 5.5 line, while current 6.1 package compatibility for some CSI/radar components has known gaps.
2. **ESP8266 nodes are optional phase-2 illuminators.** They do not block the useful three-C3 system and should not complicate the initial build/toolchain.
3. **BLE acquisition and CSI acquisition are separate modules.** ESP32-C3 has one shared 2.4 GHz RF resource, so coexistence effects must be measured.
4. **Raw/replayable evidence precedes clever inference.** A desktop replay path is part of V1, not a later luxury.
5. **Visualization is explicitly abstract.** Weighted triangle geometry is a visual encoding of RF evidence, not a localization claim.
6. **Amplitude-first CSI.** Phase-dependent algorithms are deferred until the magnitude-based baseline is characterized.
7. **Short-lived BLE identity only.** Address rotation is not something this project tries to defeat.

## Milestones

### M0 — Foundation

NGN-001.

Result: a pinned, reproducible repository skeleton with native tests and ESP32-C3 build CI.

### M1 — Independent radio evidence

NGN-002, NGN-003, NGN-004 and NGN-005.

Result: deterministic probe scheduling, attributed CSI and passive BLE observations with host-tested reduction logic.

### M2 — Productized experiment

NGN-006, NGN-007 and NGN-008.

Result: coordinator fusion, OLED renderer and replayable experiment tooling.

### M3 — Physical three-node acceptance

NGN-009.

Result: measured end-to-end behavior on the three actual C3 boards.

### M4 — Optional illumination expansion

NGN-010.

Result: two D1 mini packet sources may add additional CSI paths if measurement shows useful information gain.

## Dependency graph

```text
NGN-001
├── NGN-002 ── NGN-003 ── NGN-005 ─────┐
│      │          │                     │
│      │          └────── NGN-008 ──────┤
│      └────────────────── NGN-006 ──────┤
├── NGN-004 ────────────── NGN-006 ──────┤
│      └────────────────── NGN-008 ──────┤
└── NGN-007 ─────────────────────────────┤
                                        ▼
                                     NGN-009
                                        │
                                        ▼
                                     NGN-010
```

NGN-002, NGN-004 and NGN-007 are intentionally separable after NGN-001.

## Completion convention

Each implementation issue is an end-to-end unit:

- reconcile current repository/issue/PR state;
- implement only that issue;
- add/update tests and documentation;
- open a linked PR;
- satisfy automated checks;
- inspect/fix the final diff;
- merge when acceptance is genuinely met;
- confirm post-merge state.

Hardware-gated acceptance must never be replaced by simulated evidence.

## Change control

Architectural decisions may evolve as experiments produce evidence, but changes should be explicit.

A PR that changes any of these must update authority docs:

- pinned SDK/toolchain;
- radio transport;
- on-wire schema;
- BLE privacy behavior;
- calibration semantics;
- meaning of displayed visual elements;
- acceptance procedure.

## Release philosophy

The useful outcome is an instrument for experimentation. Prefer:

- observable state;
- logs;
- deterministic replay;
- bounded resource use;
- configuration with recorded defaults;
- simple algorithms that can be measured;

over complex inference that cannot be explained from captured evidence.
