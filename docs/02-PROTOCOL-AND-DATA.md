# 02 — Protocol and data contracts

This document defines semantic contracts. Exact C structs/encoding are established by NGN-002 and then versioned.

## General rules

Every inter-node message must include enough information to reject stale or incompatible data.

Minimum common envelope:

- protocol version;
- message type;
- source node ID;
- session ID or session nonce identifier;
- epoch ID;
- sequence number;
- payload length;
- integrity check appropriate to the transport/encoding.

All counters use explicit wrap behavior. Parsers reject malformed length/version combinations.

## Message families

Expected families:

- `SYNC` / session configuration;
- `PROBE` Wi-Fi sensing packet;
- `NODE_HEALTH`;
- `CSI_SUMMARY`;
- `BLE_OBS` / BLE track update;
- optional `CONTROL` for calibration or diagnostic mode.

Do not use raw C struct layout as a wire format unless packing, endianness and versioning are explicitly controlled and tested.

## Node health

At minimum:

- node ID;
- uptime;
- current epoch;
- Wi-Fi channel;
- last probe RX/TX sequence;
- dropped capture/work-queue counters;
- BLE observation count;
- radio/coexistence diagnostic counters;
- free-memory watermark if useful;
- firmware/protocol version.

Battery telemetry is optional until actual BMS board sensing capability is established. Do not invent a battery ADC path.

## CSI sample record

A raw/debug CSI record should be able to represent:

- local monotonic timestamp;
- session/epoch;
- receiver node;
- transmitter/source identity;
- probe sequence;
- RSSI;
- noise floor when available;
- channel/bandwidth/rate metadata needed to interpret CSI;
- CSI byte length;
- raw I/Q data or a reference to it;
- capture quality flags.

## CSI summary record

The transport-friendly summary is bounded and versioned.

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

## BLE observation record

Normalize scan results into:

- local timestamp;
- session/epoch;
- receiver node ID;
- address type;
- session-scoped observation key;
- RSSI;
- advertisement type/flags where available;
- service UUID summary;
- manufacturer/company ID when present;
- bounded payload signature/length metadata;
- first-seen / update / expired lifecycle;
- quality flags.

By default, do not persist the raw BLE address in experiment logs. A debug option may expose it transiently for local development, but it must be opt-in and visibly marked.

## Session-scoped BLE key

Nodes need a way to correlate the same current advertiser across receivers without creating a durable identity.

Baseline contract:

- coordinator generates a fresh session nonce at boot/session start;
- nodes derive a bounded key from session nonce + address type + current advertiser address + selected advertisement signature fields;
- key derivation must be deterministic within the session;
- the same source should normally map to the same key across the three nodes during that address lifetime;
- the key must intentionally change when the session nonce changes;
- do not attempt to join a newly rotated private address back to an older key.

A standard hash implementation already available in the pinned platform is preferred over bespoke cryptography.

## Fusion/display state

The internal coordinator state should expose:

- three primary undirected edge activity values: AB, BC, CA;
- optional directed detail retained separately;
- global activity score;
- quality/age per edge;
- active ephemeral BLE display tracks;
- temporal coincidence flags;
- calibration state;
- node-health/degraded state.

## Host log format

NGN-008 defines the final encoding, but it must be:

- streamable;
- line/frame recoverable after a malformed/truncated record;
- versioned;
- timestamped;
- convenient to parse in Python;
- able to round-trip through replay without semantic loss for normalized evidence.

JSON Lines is acceptable for V1 control/summary data if measured throughput is sufficient. Raw CSI may require a more compact framed representation.
