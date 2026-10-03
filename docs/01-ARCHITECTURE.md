# 01 — Architecture

## Nodes

The baseline uses three ESP32-C3 nodes arranged approximately as a triangle.

- **Node A** — sensing/probe node.
- **Node B** — sensing/probe node.
- **Node C / coordinator** — sensing/probe node plus OLED and coordinator duties.

Roles are logical. The radio/data components should remain common across all C3 firmware profiles.

Node identity must be explicit and configurable; do not rely on discovery order.

## Radio model

ESP32-C3 uses a single shared 2.4 GHz RF resource for Wi-Fi and BLE. The implementation therefore treats radio time as a scheduled resource.

Wi-Fi operates on one configured 2.4 GHz channel for the sensing session.

The baseline does not require association with an access point. ESP-NOW should run with Wi-Fi in station mode so the implementation remains compatible with Espressif's documented coexistence behavior.

## Epoch model

A sensing **epoch** is the unit used to correlate distributed observations.

Conceptual sequence:

```text
SYNC / epoch start
  |
  +-- A probe burst --> B and C capture CSI
  |
  +-- B probe burst --> A and C capture CSI
  |
  +-- C probe burst --> A and B capture CSI
  |
  +-- BLE observation opportunity / coexistence dwell
  |
  +-- nodes emit bounded summaries
  |
  +-- coordinator fuses current state
  |
  +-- OLED update + serial records
```

The exact timing is configurable and must be tuned from measurements. Do not encode timing assumptions into data structures.

The coordinator supplies:

- session ID/nonce;
- epoch counter;
- radio configuration version;
- schedule parameters.

Nodes retain local monotonic timestamps as well as the epoch ID. Microsecond-perfect clock synchronization is not a V1 requirement.

## Wi-Fi probe traffic

Probe frames must be deterministic and attributable.

Preferred baseline:

- ESP-NOW;
- fixed Wi-Fi channel;
- known node MAC/source mapping;
- bounded burst size;
- monotonically increasing per-source sequence number.

A broadcast-style probe is attractive because both non-transmitting C3s can observe the same source burst. If implementation evidence shows a reliability problem, directed frames may be used, but the change must preserve source attribution and be documented.

Probe payloads are not application data. Keep them small and stable so changes in packet shape do not become an uncontrolled sensing variable.

## CSI acquisition pipeline

The Wi-Fi/CSI callback is a capture boundary, not a signal-processing workspace.

Callback responsibilities should be bounded:

1. validate pointers/lengths;
2. record source and receive metadata;
3. copy or reference the bounded CSI payload safely;
4. enqueue for worker processing;
5. return.

Worker responsibilities include I/Q decoding, magnitude derivation, quality filtering, baseline/perturbation processing and record emission.

## BLE acquisition pipeline

Baseline BLE scanning is passive.

Each node may scan so the same advertiser can produce a three-node RSSI observation vector. Scanning and Wi-Fi sensing share RF resources, so missing/shortened scan opportunities are an expected quality condition, not an exceptional crash condition.

BLE events flow through:

```text
controller/host scan result
  -> normalized advertisement observation
  -> session-scoped observation key
  -> short-lived local track
  -> bounded summary to coordinator
```

Raw addresses must not become persistent identity.

## Coordinator

The coordinator owns:

- session/epoch orchestration;
- node-health table;
- incoming link evidence;
- incoming BLE summaries;
- fusion state;
- display-state publication;
- primary serial event stream.

It must tolerate one temporarily missing sensor without blocking the main loop. Missing evidence should be represented explicitly in quality flags.

## Fusion boundary

Fusion consumes normalized evidence, never driver callbacks directly.

Inputs:

- directed/undirected link perturbation metrics;
- evidence age and quality;
- per-node BLE RSSI observations;
- short-lived BLE track lifecycle events.

Outputs:

- edge activity values;
- global activity score;
- optional abstract field centroid/weights;
- current BLE display objects;
- BLE/CSI coincidence events;
- system quality/degraded flags.

## OLED boundary

The renderer consumes a compact display-state object.

No CSI parsing, BLE parsing or radio ownership belongs in the display driver.

The expected 0.42-inch board is likely a very small monochrome display, but controller, dimensions, I2C/SPI interface and pins remain provisional until verified on the actual board. The renderer should support an abstract width/height framebuffer.

## Serial / host tooling

Machine-readable event output is a first-class interface.

The coordinator should be able to emit:

- session metadata;
- configuration;
- node health;
- CSI summary events;
- BLE track events;
- fusion state;
- calibration transitions;
- optional raw/debug records.

Raw CSI may also be emitted by individual nodes during development when connected separately.

## Optional ESP8266 illuminators

The two D1 minis are deliberately outside the core architecture.

If NGN-010 proceeds, they act as deterministic Wi-Fi packet sources only. They do not participate in BLE sensing or coordinator logic. The C3 array must remain useful if both are powered off.
