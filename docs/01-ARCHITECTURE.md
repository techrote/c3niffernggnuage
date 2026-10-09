# 01 — Architecture

NGN-002 implements node/session control, scheduled probes and node health. NGN-004 supplies the independent passive BLE observer, and NGN-007 supplies the independent display renderer/adapter. Their end-to-end sensing pipeline is still a programme target: NGN-002 starts neither BLE scanning nor OLED presentation and contains no CSI acquisition or fusion.

## Nodes

The baseline uses three ESP32-C3 nodes arranged approximately as a triangle.

- **Node A** — sensing/probe node.
- **Node B** — sensing/probe node.
- **Node C / coordinator** — sensing/probe node plus OLED and coordinator duties.

Roles are logical. The radio/data components should remain common across all C3 firmware profiles.

Node identity must be explicit and configurable; do not rely on discovery order.

## Radio model

ESP32-C3 uses a single shared 2.4 GHz RF resource for Wi-Fi and BLE. The implementation therefore treats radio time as a scheduled resource.

Wi-Fi operates on one configured 2.4 GHz channel for the sensing session. NGN-002 supports channels 1–11 and defaults to channel 6; all three nodes must be configured on the same channel before discovery.

The implemented adapter uses broadcast ESP-NOW with Wi-Fi in station mode, disables Wi-Fi power saving for this schedule, and requires no access point or router. It creates no IP interface, makes no association attempt, and neither reads nor persists a saved AP configuration. Startup rejects existing Wi-Fi ownership and preserves NVS contents rather than erasing stored data to recover an initialization error.

## Implemented runtime and worker boundaries

| Component | NGN-002 responsibility |
| --- | --- |
| `ngn_core/ngn_protocol.c` | Explicit protocol-v1 bytes, typed payload validation and CRC |
| `ngn_core/ngn_schedule.c` | Validated configuration and a deterministic, fixed-capacity epoch plan |
| `ngn_core/ngn_radio.c` | Session/epoch state, source-MAC bindings, presence, scheduling and node-health snapshots through a mockable transport |
| `ngn_radio_esp` | Sole ESP-NOW/Wi-Fi owner, bounded queues and driver submission/completion |
| `main/app_main.c` | Explicit role/configuration selection, coordinator nonce generation, runtime service and separate diagnostic output |

The unconfigured role is safe: startup logs the missing A/B/C selection and returns without starting the radio. With a configured role, startup validates the complete schedule before initializing the adapter. C obtains a nonzero 64-bit session identifier after Wi-Fi is active; A/B wait for its SYNC. The pure runtime receives this identifier as an input and remains deterministic on a host.

The RX callback validates pointers, lengths and the broadcast destination, copies at most 96 frame bytes plus source MAC/receive timestamp into a fixed queue, and returns. The TX callback validates/captures completion status and notifies the worker. Neither callback parses the protocol, changes session state, invokes an event sink, scans BLE, or logs to the console.

RX, TX and completion-status queues each default to 16 records and can be configured from 1 to 64. The runtime drains at most 16 RX and 16 status records and walks at most 59 schedule events per service call. It owns three fixed peer records and an eight-entry retired-session guard. Its event sink runs only in the normal runtime worker and must not block or re-enter the runtime.

A separate TX worker admits one driver send at a time. It checks session identity and the exclusive submission deadline, refreshes copied SYNC timing immediately before submission, and reports completion, failure, expiry or session cancellation. A session change invalidates obsolete queued frames; a frame already claimed for submission may still finish. A missing completion keeps its token owned, records a stalled-send diagnostic and prevents a late callback from completing a different send. RX and diagnostic polling remain available.

The runtime task yields at least one RTOS tick per iteration. Console work uses a separate lower-priority task and a one-record overwrite queue, so a slow console cannot block the runtime's event sink. Diagnostic snapshots expose session, epoch, presence and actual node-to-station-MAC mappings. This is a bounded software service path, not a measured RF timing or loss guarantee.

## Epoch model

A sensing **epoch** is the unit used to correlate distributed observations.

The implemented epoch contains C's SYNC slot, A/B/C probe slots, a coexistence-opportunity placeholder and staggered A/B/C NODE_HEALTH slots. Defaults give a 390 ms epoch: 40 ms SYNC, three 80 ms probe slots, 80 ms coexistence opportunity and three 10 ms health slots. Each node has four probes spaced 10 ms apart.

The intended later pipeline adds CSI capture at non-transmitting nodes, local reduction and bounded summaries, scheduled use of the independent passive BLE observer, coordinator fusion, and display/capture publication. Those actions are not performed by NGN-002 slot notifications.

The exact timing is configurable and must be tuned from measurements. Do not encode timing assumptions into data structures.

The coordinator supplies:

- session ID/nonce;
- epoch counter;
- radio configuration version;
- schedule parameters.

Nodes retain local monotonic timestamps as well as the epoch ID. Microsecond-perfect clock synchronization is not a V1 requirement.

C advances the plan without waiting for missing peers. A/B synchronize only from C's accepted SYNC, require a strictly newer SYNC epoch each cycle, and enter silent `WAIT_SYNC` at the end of an accepted epoch. After the longer configured timeout they enter discovery while retaining the current session and pinned MAC identities; a newer same-session SYNC can restore operation. A new session from the pinned C MAC resets the session's other bindings and sequence state and retires the old session in the bounded replay guard. CRC and first-observed binding are not authentication.

Expired transmissions are dropped rather than replayed as catch-up bursts. The adapter checks deadlines again before driver submission; a packet already submitted cannot be recalled. No precise per-source RX-slot enforcement or physical clock/airtime guarantee is claimed. Exact wire fields, configuration bounds, default offsets, timeout/rejoin behavior and health-counter semantics are defined in `docs/02-PROTOCOL-AND-DATA.md`.

## Wi-Fi probe traffic

Probe frames must be deterministic and attributable.

The implemented baseline is:

- ESP-NOW;
- fixed Wi-Fi channel;
- known node MAC/source mapping;
- bounded burst size;
- per-source sequence numbers with explicit modulo-2^32 freshness rules.

Broadcast gives both non-transmitting C3s an opportunity to observe the same source burst. Its driver completion does not acknowledge reception by either node. A future move to directed traffic would require explicit evidence, documentation and preserved source attribution.

Every v1 probe is 30 bytes including its envelope and CRC. Its payload contains only burst index/count and reserved zero bytes, with no application data. Keep probe shape stable so it does not become an uncontrolled sensing variable.

## CSI acquisition pipeline

This pipeline remains NGN-003/005 work. NGN-002 does not register a CSI callback or emit CSI measurements.

The Wi-Fi/CSI callback is a capture boundary, not a signal-processing workspace.

Callback responsibilities should be bounded:

1. validate pointers/lengths;
2. record source and receive metadata;
3. copy or reference the bounded CSI payload safely;
4. enqueue for worker processing;
5. return.

Worker responsibilities include I/Q decoding, magnitude derivation, quality filtering, baseline/perturbation processing and record emission.

## BLE acquisition pipeline

Baseline BLE scanning is passive. NGN-004 implements the independent observer/track components; see `docs/07-BLE-OBSERVATION.md`. Its coordinated activation and inter-node summary transport remain later integration work. NGN-002 only emits a coexistence opportunity and never calls the observer's start path.

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

The programme assigns these responsibilities to the coordinator. NGN-002 implements session/epoch orchestration and the node-health table; the remaining sensing/fusion/publication responsibilities belong to later issues:

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

NGN-007 implements an abstract width/height framebuffer and renderer plus a default-disabled ESP adapter seam; see `docs/08-DISPLAY.md`. The physical 0.42-inch board's controller, dimensions, interface and pins remain provisional until verified. NGN-002 assigns no display GPIOs or controller and does not initialize that adapter.

## Serial / host tooling

Machine-readable event output is a first-class interface.

NGN-002 currently provides bounded diagnostic snapshots and node/MAC mapping logs. The final capture/replay format and the fuller event stream below remain NGN-008 and later integration work.

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
