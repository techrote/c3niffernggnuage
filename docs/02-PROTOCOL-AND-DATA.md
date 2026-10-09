# 02 — Protocol and data contracts

NGN-002 implements protocol-v1 `SYNC`, `PROBE` and `NODE_HEALTH`, the deterministic epoch plan and its host-testable session runtime. NGN-003 adds a **local** CSI acquisition/normalization record without changing protocol-v1 bytes or assigning a new wire message type. The codecs, scheduler and host-testable CSI core live in `firmware/c3/components/ngn_core`; ESP32-C3 adapters live in `ngn_radio_esp` and `ngn_csi_esp`.

The on-wire types remain control, probe and diagnostic contracts. NGN-003's CSI packet is an in-memory production interface for later signal processing/capture; BLE summaries, CSI summaries, fusion and OLED behavior remain separate later contracts.

## Protocol-v1 envelope

Every multi-byte wire integer is **unsigned, big-endian**. Native C struct layout, enum width, alignment and padding never enter the encoding. Logical source IDs are A = `0`, B = `1`, C/coordinator = `2`; the local unconfigured ID `0xff` is invalid in a source field.

| Byte offset | Width | Field | Contract |
| --- | ---: | --- | --- |
| 0 | 2 | Magic | `4e 47` (`NG`) |
| 2 | 1 | Protocol version | Exactly `1` |
| 3 | 1 | Message type | `1` SYNC, `2` PROBE, `3` NODE_HEALTH |
| 4 | 1 | Source node | A/B/C; SYNC requires C |
| 5 | 1 | Reserved | Zero |
| 6 | 2 | Payload length | Exact byte count for the selected type |
| 8 | 8 | Session ID | Nonzero coordinator-created identifier |
| 16 | 4 | Epoch | Modulo-2^32 counter; zero is valid |
| 20 | 4 | Source sequence | Modulo-2^32 counter shared across that source's message types; zero is valid |
| 24 | Payload length | Typed payload | Exact layout below |
| 24 + payload length | 2 | CRC | CRC-16/CCITT-FALSE over header and payload |

CRC parameters: polynomial `0x1021`, initial value `0xffff`, no input or output reflection, final XOR `0x0000`. The check value for ASCII `123456789` is `0x29b1`. The CRC bytes themselves are excluded from the calculation and stored most-significant byte first.

| Type | Payload bytes | Complete frame bytes |
| --- | ---: | ---: |
| SYNC | 20 | 46 |
| PROBE | 4 | 30 |
| NODE_HEALTH | 52 | 78 |

The bounded frame capacity is 96 bytes. Extra trailing bytes are rejected; the capacity is not an allowance to append fields to v1. Other type values have no v1 encoding and are rejected.

The codec rejects incompatible version/type/source, wrong magic, nonzero reserved fields, zero session, incorrect declared or actual length, corrupt CRC and invalid typed payloads. The encoder also requires the caller's declared payload length to equal the exact typed size. Both encoder and decoder leave caller outputs unchanged on failure.

The codec establishes structural validity. Session, epoch, sequence, channel and source-MAC acceptance are additional runtime checks; passing a CRC does not make a packet current or trusted.

## SYNC payload — 20 bytes

Offsets in the following tables are relative to the beginning of the payload.

| Offset | Width | Field | Contract |
| --- | ---: | --- | --- |
| 0 | 1 | Schedule version | Exactly `1` |
| 1 | 1 | Wi-Fi channel | `1`–`11` |
| 2 | 1 | Probe burst count | `1`–`16` per node |
| 3 | 1 | Missing-node timeout | `1`–`32` epochs |
| 4 | 2 | Probe spacing | Positive milliseconds |
| 6 | 2 | SYNC slot duration | Positive milliseconds |
| 8 | 2 | Per-node probe-slot duration | Positive milliseconds |
| 10 | 2 | Coexistence opportunity duration | Milliseconds; zero allowed |
| 12 | 2 | Per-node health-slot duration | Positive milliseconds |
| 14 | 2 | Reserved | Zero |
| 16 | 4 | Epoch elapsed time | Milliseconds since the coordinator's epoch start; strictly less than the SYNC slot duration |

Timing values are 16-bit on the wire, with additional combined bounds:

- `burst_count * probe_spacing_ms <= probe_slot_ms`;
- `epoch_ms = sync_slot_ms + 3 * probe_slot_ms + coexist_ms + 3 * health_slot_ms`;
- `epoch_ms <= 60000`.

Every probe owns a complete spacing interval, including the last probe in a burst. Any remaining probe-slot time is unused guard time. These checks use wider arithmetic before comparison, so large encoded fields cannot wrap into an apparently short valid schedule.

The transport's optional bounded `prepare` hook refreshes a copied SYNC frame's elapsed time and CRC in the TX worker before driver submission. It does not access mutable session state or execute in a radio callback. Both the runtime and adapter enforce the frame's exclusive local transmission deadline. This provides a software timing reference, not a claim of precise RF airtime or synchronized physical clocks.

## PROBE payload — 4 bytes

| Offset | Width | Field | Contract |
| --- | ---: | --- | --- |
| 0 | 1 | Probe index | Zero-based; strictly less than count |
| 1 | 1 | Burst count | `1`–`16`; runtime must match the current session schedule |
| 2 | 2 | Reserved | Zero |

The source, session, epoch and sequence come from the envelope. The payload contains no application data, CSI, BLE observations or inference. Every v1 probe frame has the same 30-byte length.

## NODE_HEALTH payload — 52 bytes

| Offset | Width | Field |
| --- | ---: | --- |
| 0 | 4 | Uptime in milliseconds |
| 4 | 4 | Last successfully queued probe TX sequence |
| 8 | 4 | Last accepted probe RX sequence |
| 12 | 4 | Completed driver transmissions |
| 16 | 4 | Accepted protocol packets |
| 20 | 4 | RX queue drops |
| 24 | 4 | TX queue drops |
| 28 | 4 | Status queue drops |
| 32 | 4 | TX errors |
| 36 | 4 | Rejected RX packets |
| 40 | 4 | Late TX drops |
| 44 | 1 | Wi-Fi channel (`1`–`11`) |
| 45 | 1 | Locally observed present-node mask |
| 46 | 1 | Firmware major version |
| 47 | 1 | Firmware minor version |
| 48 | 1 | Firmware patch version |
| 49 | 1 | Sequence-validity flags |
| 50 | 1 | Last received probe's source node, or `0xff` when unavailable |
| 51 | 1 | Reserved zero |

The envelope supplies the reporting node, session, epoch and protocol version. The presence mask uses bits 0/1/2 for A/B/C; bits 3–7 must be zero. It is that node's observation, not an assertion that all receivers have the same view.

Validity flag bit 0 means the last probe TX sequence is available; bit 1 means the last probe RX sequence and source are available. Bits 2–7 must be zero. If TX validity is clear, its sequence must be zero. If RX validity is clear, its sequence must be zero and source must be `0xff`. If RX validity is set, the source must be A/B/C. Sequence zero with its validity bit set is a valid wraparound value.

The field semantics are:

- TX sequence records the last probe accepted into the transport queue. A later driver failure, expiry or session cancellation does not turn that value into proof of transmission or reception.
- RX sequence and source record the last probe accepted by all runtime checks.
- Completed transmissions come from the adapter's successful driver completions. Broadcast completion is a local driver result, not an acknowledgement from A/B/C receivers.
- Accepted packets are the runtime's accepted protocol packet count.
- The three queue-drop fields are adapter counters for their respective RX/TX/status queues.
- TX errors combine adapter TX errors and runtime encoding errors. Queue-full drops are counted separately.
- Rejected RX packets combine runtime rejections and adapter invalid-RX metadata/length/destination rejections.
- Late TX drops combine runtime-expired schedule transmissions and adapter-expired queued frames.

All 32-bit diagnostics wrap modulo 2^32. Uptime is milliseconds since radio-runtime initialization, truncated modulo 2^32; it is not a wall-clock timestamp. A session change resets probe-sequence availability but retains these runtime/adapter lifetime counters. A peer's last accepted health snapshot can remain cached after it becomes missing; its separate presence flag must be consulted. BLE observation count, CSI capture drops, measured coexistence performance, free-memory watermark and battery telemetry have no field in this payload. Their absence is not a measurement of zero, and no battery ADC path is invented.

## Deterministic epoch schedule

The configurable defaults are experimental timing choices, not measured sensing-performance results.

| Parameter | Default |
| --- | ---: |
| Fixed channel | 6 |
| Probes per A/B/C slot | 4 |
| Probe spacing | 10 ms |
| SYNC slot | 40 ms |
| Each A/B/C probe slot | 80 ms |
| Coexistence opportunity | 80 ms |
| Each A/B/C health slot | 10 ms |
| Missing-node timeout | 4 epochs |
| Total epoch | **390 ms** |

| Epoch offset | Event / interval |
| --- | --- |
| 0 ms | C's SYNC transmission becomes eligible |
| `[0, 40)` ms | SYNC slot |
| 40, 50, 60, 70 ms | A's probes in its `[40, 120)` ms slot |
| 120, 130, 140, 150 ms | B's probes in its `[120, 200)` ms slot |
| 200, 210, 220, 230 ms | C's probes in its `[200, 280)` ms slot |
| `[280, 360)` ms | Coexistence opportunity notification |
| `[360, 370)` ms | A's health slot; TX eligible at 360 ms |
| `[370, 380)` ms | B's health slot; TX eligible at 370 ms |
| `[380, 390)` ms | C's health slot; TX eligible at 380 ms |

The stateless plan contains 23 events with the defaults and at most 59 events with 16 probes per node. A slot-start event precedes the TX event at the same offset. Higher layers can observe slot, coexistence, session, epoch, binding, presence and timeout events. These notifications start no sensing, scanning or presentation work themselves.

A transmission is eligible only in `[offset_ms, deadline_ms)`. A probe's deadline is one probe-spacing interval after its nominal offset. SYNC expires at the end of its slot; each health transmission expires at the end of its own slot. Expired work is dropped instead of replaying overdue bursts. The ESP adapter checks the deadline again when dequeuing and immediately before driver submission. Frames already handed to the driver cannot be recalled, so deadlines are submission limits rather than measured airtime guarantees.

The coexistence interval is an opportunity placeholder. NGN-002 does not invoke NimBLE scanning or promise an RF reservation. The NGN-004 passive observer remains independently controlled. No external AP or router is required: the adapter uses fixed-channel broadcast ESP-NOW in Wi-Fi station mode, without associating or reading a saved AP configuration. All participants must start on the same configured channel; the protocol does not scan channels to discover C.

## Session, freshness and node mapping

C starts with an injected fresh nonzero 64-bit session ID; A/B start without a session and discover C. Firmware, rather than the pure host-testable core, generates C's ID. Epoch and per-source sequence counters are unsigned 32-bit values. Freshness uses the strict serial-number relation:

```text
delta = (candidate - reference) modulo 2^32
newer iff 0 < delta < 2^31
```

Equality is a duplicate; an exact half-range difference is ambiguous and rejected. Sequence state is per logical source and covers all three message types. A source consumes a sequence when preparing an eligible transmission, even if encoding or queue admission then fails; gaps are valid. A valid CRC or a numerically large unsigned value cannot bypass these rules.

An accepted non-SYNC packet must match the current session and exact active epoch while the receiver is `SYNCHRONIZED`. Its callback receive timestamp must fall within that receiver's `[epoch_start, epoch_end)` interval. A PROBE must announce the current burst count; NODE_HEALTH must announce the current channel. The runtime does not enforce an individual source's precise RX slot or infer RF airtime from arrival timestamps. The deterministic local TX plan and active-epoch acceptance checks are separate contracts.

Receive timestamps in the future, before runtime initialization or before that peer's last accepted observation are rejected. A SYNC queued for at least its announced `epoch_ms * missing_epochs` duration is rejected. The follower derives its local epoch start from `receive_timestamp - epoch_elapsed_ms`, with an underflow check. Validation finishes before changing bindings, sequence state, presence or session state.

Followers require a strictly newer accepted SYNC for each epoch. At the end of an accepted epoch they enter `WAIT_SYNC` and stop transmitting until C provides the next accepted epoch reference. They do not extrapolate transmission slots indefinitely after missed synchronization. C advances without waiting for every node, so a missing follower cannot stall the schedule. If a late service call spans several coordinator epochs, C advances directly to the current epoch and records skipped epochs instead of replaying earlier plans.

The longer timeout is `missing_epochs * epoch_ms`: 1,560 ms with the defaults. For a follower's synchronization timeout it is measured since the last accepted SYNC receive timestamp; for peer presence it is measured since that peer's last accepted packet. At equality the timeout has expired. Missing synchronization enters `DISCOVERING`; peer timeout emits a missing event without blocking the schedule. The local node remains present.

A timeout retains the current session ID and node/MAC bindings. A peer's sequence-valid flag clears when that peer changes from present to missing, allowing its freshly restarted sequence to be observed again. A follower can rejoin the same session through a strictly newer C SYNC epoch. The last accepted SYNC epoch therefore still prevents an old same-session synchronization packet from restarting the schedule.

A new non-retired session from the pinned coordinator MAC can be accepted immediately, including while synchronized. The previous session ID enters an eight-entry FIFO replay guard in RAM. A session change resets other remote bindings and sequence state, starts the local source sequence at zero and retains the pinned coordinator identity. Timing configuration may change with the new session; the announced channel must match the locally configured radio. Configuration is immutable within one session.

The runtime also updates the transport's active session at initialization and adoption. Queued transmissions retain the session ID in which they were created, so adopting a new session cannot silently submit old-session queue entries under the new schedule. The adapter cancels obsolete queued work before claiming it for driver submission. A send already claimed in flight can finish; no software queue operation can withdraw a frame already handed to the driver.

The retired-session cache is bounded and lost on local restart. It does not reject every session ever seen. CRC integrity, first-observed MAC binding and this replay guard are not cryptographic authentication or protection against an active sender inventing a new session.

Identity is explicit in configuration and headers. The runtime observes actual station source MACs, accepts only nonzero unicast station addresses and binds a logical node on its first accepted observation. A different MAC claiming an already-bound node or one MAC claiming two nodes is rejected. The coordinator binding remains pinned when missing; replacing that device requires a local restart. Bindings and presence are inspectable through the peer table and normal-worker events. Parsing and mapping are outside callbacks.

## Bounded transport and diagnostics

`ngn_transport_t` provides required nonblocking send, receive, status-poll, stats and `set_session` operations. A queued TX record owns copied bytes, its session ID, a source-sequence token, an exclusive local monotonic deadline and an optional pure preparation hook. An RX record carries copied bytes, actual source MAC and the callback's local receive timestamp. The ESP adapter has separate fixed RX, TX and status queues, each configurable from 1 to 64 records.

The runtime services at most 16 RX records, 16 status records and the fixed 59-event schedule capacity per call. Radio callbacks capture bounded bytes or completion status; parsing, identity checks, session transitions, scheduling and event sinks run in normal workers.

The adapter allows one driver send in flight. The active-session check and claiming that in-flight slot are atomic with respect to `set_session`. Only a matching nonzero session can claim a send; active session zero is the initial discovery state. Work whose recorded session is obsolete receives `NGN_TRANSPORT_TX_CANCELED` instead of being submitted. Work claimed before the session switch may still be submitted or complete; changing the session does not release or reassign that in-flight token. `set_session` changes admission identity and wakes the worker; it never blocks while draining queues in the caller.

Completion statuses carry the source-sequence token. A full status queue drops its newest status without retaining the completed in-flight slot. A missing callback increments `tx_stalled` once after one second and keeps the token owned until the callback arrives; a late callback cannot falsely complete a different transmission. RX and diagnostics remain serviceable while driver sends are stalled.

Adapter statistics also expose invalid RX metadata, unexpected callbacks, expired queued frames, session-canceled work (`tx_canceled`) and queue/drop/error counts. Runtime diagnostics expose protocol, time, identity, session, epoch, sequence and configuration rejection categories, skipped epochs, synchronization timeouts and canceled-status counts. Cancellations remain local diagnostics: they are not added to `late_tx_drops` or a new NODE_HEALTH field. Detailed local diagnostics are broader than the unchanged 52-byte NODE_HEALTH payload.

## Later data contracts — no protocol-v1 encoding yet

`CSI_SUMMARY`, `BLE_OBS`/track updates and optional `CONTROL` remain unimplemented inter-node message families. They have no assigned type value or wire layout here. Their owning issues must introduce explicit versioned encodings and tests when integrating those components; native structs are not an extension mechanism.

### CSI packet record — implemented locally by NGN-003

NGN-003 does **not** add a protocol-v1 message type. It implements
`ngn_csi_packet_t` as a bounded local record consumed by later NGN-005/008
work.

The record retains:

- receiver-local callback monotonic milliseconds;
- logical transmitter node plus accepted source station MAC;
- destination MAC;
- RSSI and noise floor;
- PHY rate, signal mode, MCS, channel bandwidth, smoothing/sounding,
  aggregation, STBC, FEC and SGI metadata;
- AMPDU count, primary/secondary channel, antenna, RX state, received signal
  length, hardware timestamp and Wi-Fi RX sequence;
- raw CSI byte length and complete signed raw I/Q bytes;
- valid-data offset (four when ESP-IDF marks the first word invalid);
- squared-magnitude vector and complex-sample count;
- capture-time nonzero session ID plus optional epoch and accepted PROBE source sequence;
- absolute callback-time delta to the selected accepted PROBE;
- quality flags for first-word exclusion and unattributed known-source CSI.

The pinned byte convention is signed `[imaginary, real]`. Squared magnitude is
`imaginary^2 + real^2`. The raw capacity is 612 bytes.

Source admission is inherited from the runtime's current logical-node/station
MAC mapping. Unknown source MACs are filtered before the CSI payload is copied.
An accepted protocol PROBE produces a separate normal-worker `PROBE_RX` event;
the CSI worker correlates by same logical source and nearest capture timestamp
inside the configured bounded window.

The capture-time session is retained even for delayed/unattributed records, and only probe observations from that same session are eligible for correlation. Known-source CSI without a matching accepted probe is explicitly marked
unattributed and is diagnostic evidence only. It must not be interpreted as a
session/epoch/probe measurement by NGN-005. The 5 ms default correlation window
is configurable and is not a measured hardware guarantee.

See `docs/09-CSI-ACQUISITION.md` for queue, callback and SDK details.

### CSI summary record

The later transport-friendly summary must be bounded and versioned; NGN-002 has no CSI summary payload.

Candidate fields:

- directed link ID;
- sample count in the aggregation window;
- valid/rejected sample counts;
- mean/median RSSI;
- baseline age/state;
- normalized perturbation score;
- short-window variance/activity score;
- optional correlation-distance metric;
- quality flags;
- timestamp/epoch.

Keep enough sub-metrics to debug the final score; do not transport only a single opaque scalar.

### BLE observation record

NGN-004 already defines independent passive-observation and track-event contracts in `docs/07-BLE-OBSERVATION.md`. Its privacy-safe event output excludes raw addresses and unsalted payload signatures. Inter-node encoding and epoch integration remain later work; NGN-002 does not start the observer or encode its events. A later normalized inter-node observation should retain:

- local timestamp;
- session/epoch;
- receiver node ID;
- address type;
- session-scoped observation key;
- RSSI;
- advertisement type/flags where available;
- service UUID summary;
- manufacturer/company ID when present;
- bounded advertisement/length metadata, without exposing the transient unsalted payload signature;
- first-seen / update / expired lifecycle;
- quality flags.

By default, do not persist the raw BLE address in experiment logs. A debug option may expose it transiently for local development, but it must be opt-in and visibly marked.

### Session-scoped BLE key

The existing BLE component requires an injected 8–32-byte nonce. A later integration must provide the same fresh nonce to participating nodes so they can correlate a current advertiser without creating a durable identity. NGN-002 does not choose a new BLE key derivation or silently start that integration.

Baseline contract:

- coordinator generates a fresh session nonce at boot/session start;
- nodes derive a bounded key from session nonce + address type + current advertiser address + selected advertisement signature fields;
- key derivation must be deterministic within the session;
- the same source should normally map to the same key across the three nodes during that address lifetime;
- the key must intentionally change when the session nonce changes;
- do not attempt to join a newly rotated private address back to an older key.

A standard hash implementation already available in the pinned platform is preferred over bespoke cryptography.

### Fusion/display state

NGN-007 already consumes its independent display-state contract. Later coordinator fusion should expose:

- three primary undirected edge activity values: AB, BC, CA;
- optional directed detail retained separately;
- global activity score;
- quality/age per edge;
- active ephemeral BLE display tracks;
- temporal coincidence flags;
- calibration state;
- node-health/degraded state.

### Host log format

NGN-008 defines the final encoding, but it must be:

- streamable;
- line/frame recoverable after a malformed/truncated record;
- versioned;
- timestamped;
- convenient to parse in Python;
- able to round-trip through replay without semantic loss for normalized evidence.

JSON Lines is acceptable for later control/summary capture if measured throughput is sufficient. Raw CSI may require a more compact framed representation. NGN-002 diagnostic console messages do not establish that capture format.
