# 09 — Wi-Fi CSI acquisition

NGN-003 implements the ESP32-C3 Wi-Fi CSI acquisition boundary on top of the
NGN-002 fixed-channel probe/session runtime. It acquires and normalizes CSI
records; it does **not** calibrate a baseline, calculate perturbation/activity
scores, fuse BLE evidence or drive the display.

## Pinned SDK contract

The firmware remains pinned to **ESP-IDF v5.5.5** and target `esp32c3`.

Tracked defaults enable the SDK CSI feature with:

```text
CONFIG_ESP_WIFI_CSI_ENABLED=y
```

The implementation uses the v5.5.5 APIs:

- `esp_wifi_set_csi_config()`;
- `esp_wifi_set_csi_rx_cb()`;
- `esp_wifi_set_csi()`;
- `esp_wifi_set_promiscuous()`.

The NGN-002 radio adapter remains the sole Wi-Fi initializer. CSI startup
requires that Wi-Fi is already running in `WIFI_MODE_STA` on the configured
fixed channel. CSI creates no IP interface, does not associate with an access
point and does not require a router.

For the disconnected/no-router sensing baseline the adapter enables
promiscuous receive after Wi-Fi startup. The CSI callback then admits only
source MACs already bound by the accepted NGN protocol runtime, so unrelated
ambient traffic cannot enter the raw CSI queue.

## ESP-IDF CSI configuration

The ESP32-C3 v5.5.5 configuration is:

```text
lltf_en             = true
htltf_en             = true
stbc_htltf2_en       = true
ltf_merge_en         = false
channel_filter_en    = false
manu_scale           = false
shift                = 0
dump_ack_en           = false
```

LLTF/HT-LTF/STBC HT-LTF data are retained rather than merged. The adjacent
subcarrier channel filter is disabled so NGN-005 receives the unsmoothed
per-subcarrier representation. Scaling remains under the ESP-IDF automatic
path. These are acquisition choices, not claims that every received frame will
contain every LTF family.

## Callback boundary

ESP-IDF invokes the CSI callback from the Wi-Fi task. The callback therefore
does only bounded acquisition work:

1. reject inactive/null callbacks;
2. copy the current three-entry logical-node/MAC admission map;
3. reject a source MAC that is not currently bound to A, B or C;
4. copy the relevant `wifi_pkt_rx_ctrl_t` metadata;
5. copy at most **612 CSI bytes** plus source/destination MAC into one fixed
   raw record;
6. attempt one zero-wait queue insertion;
7. update bounded counters and return.

It performs no protocol decoding, baseline processing, feature extraction,
console logging, BLE work, fusion or display work. A full raw queue drops the
newest record and increments `capture.queue_drops`.

The maximum 612-byte capacity covers the largest CSI byte count documented by
the pinned ESP-IDF CSI format table. Oversize input is rejected rather than
truncated.

## Source admission and sessions

CSI source identity is not inferred from discovery order. The normal
`ngn_radio` runtime owns protocol acceptance and the logical A/B/C to station
MAC mapping.

Its event sink supplies CSI with:

- `SESSION` — selects the current nonzero session and clears old CSI
  attribution/binding state;
- `BOUND` — copies a protocol-accepted logical-node/MAC mapping;
- `PROBE_RX` — records an already accepted probe's source, session, epoch,
  source sequence and callback capture timestamp.

A different MAC cannot replace a bound logical source inside that CSI session,
and one MAC cannot claim two logical nodes. Unknown promiscuous CSI frames are
counted but never copied into the raw queue.

The first observable packet from a newly seen source can legitimately be
filtered before the protocol runtime has bound that source. Later accepted
probes establish the mapping. This is a bounded startup loss, not an identity
fallback.

## Probe attribution

A CSI record is correlated with the nearest already accepted `PROBE_RX`
observation from the same logical source. The configurable window is:

```text
CONFIG_NGN_CSI_ATTRIBUTION_WINDOW_MS
default: 5
range:   1..50
```

The adapter retains 16 recent accepted probes per logical source. If the CSI
worker initially finds no match, it yields for one RTOS tick and retries once;
this accommodates ordering between the independent ESP-NOW receive path and CSI
callback without blocking the Wi-Fi callback.

An attributed record carries:

- session ID;
- epoch;
- source/probe sequence;
- absolute callback-time delta to the selected accepted probe.

A known-source CSI record with no accepted probe inside the window is still a
valid **diagnostic** record, but it is marked
`NGN_CSI_QUALITY_UNATTRIBUTED`. It must not silently enter the primary
NGN-005 sensing stream as attributed evidence. The 5 ms default is a software
starting value and requires physical validation; NGN-003 does not claim it is
optimal or sufficient on real boards.

## Worker-side I/Q decoding

For the pinned ESP32-C3 CSI representation, each complex sample is stored as:

```text
[ imaginary:int8, real:int8 ]
```

The host-testable decoder preserves the raw bytes and calculates squared
magnitude:

```text
power = imaginary^2 + real^2
```

Squared magnitude is exact for the signed 8-bit input domain and avoids a
floating-point square root. NGN-005 may normalize or transform this vector
later; NGN-003 performs no baseline or activity scoring.

If `wifi_csi_info_t.first_word_invalid` is true, the first four raw CSI bytes
are retained for debugging but excluded from the decoded power vector and the
record is marked `NGN_CSI_QUALITY_FIRST_WORD_SKIPPED`. After this exclusion,
the usable byte count must be positive and even.

Records are also rejected when the SDK reports a nonzero RX state or a channel
different from the configured NGN fixed channel.

## Normalized packet record

The in-memory `ngn_csi_packet_t` contract retains:

- logical source node;
- source and destination MACs;
- callback monotonic milliseconds;
- ESP32-C3 RX metadata: RSSI, noise floor, rate, signal mode, MCS, channel
  bandwidth, smoothing/sounding flags, aggregation/STBC/FEC/SGI, AMPDU count,
  primary/secondary channel, antenna, RX state, signal length, hardware
  timestamp and Wi-Fi RX sequence;
- raw CSI byte count and valid-data offset;
- raw signed I/Q bytes;
- squared-magnitude vector and sample count;
- optional session/epoch/probe sequence attribution and observed delta;
- quality flags.

This is a local production interface for NGN-005 and NGN-008. It is **not** a
new protocol-v1 wire message and assigns no new NGN message type.

## Queues and workers

Project Kconfig defaults:

| Setting | Default | Range |
| --- | ---: | ---: |
| `NGN_CSI_RAW_QUEUE_DEPTH` | 8 | 1–32 |
| `NGN_CSI_PACKET_QUEUE_DEPTH` | 4 | 1–16 |
| `NGN_CSI_ATTRIBUTION_WINDOW_MS` | 5 ms | 1–50 ms |

The raw queue separates the Wi-Fi callback from decoding. The decoded packet
queue separates acquisition from later consumers. Both use zero-wait producer
operations and explicit drop counters.

The CSI worker is lower priority than the NGN radio runtime. Until NGN-005 or
NGN-008 becomes the production consumer, a separate low-priority task drains
the decoded queue. This prevents the temporary diagnostic consumer from
back-pressuring acquisition indefinitely.

## Diagnostic output

`CONFIG_NGN_CSI_DIAGNOSTIC_RAW` defaults **off**.

When deliberately enabled for development, the low-priority output task emits
a machine-readable `NGN_CSI_RAW v=1 ...` line with attribution/metadata and
the bounded signed raw I/Q vector. This can produce substantial local serial
traffic and is not the normal operating mode or the final NGN-008 log format.

Periodic radio diagnostics report aggregate CSI callback, output, unknown
source, unattributed and queue-drop counters without emitting raw CSI.

## Host evidence

The native `ngn_csi` suite compiles the same core CSI source used by firmware
and covers:

- logical source/MAC mapping and collision rejection;
- unknown/multicast/zero source filtering;
- bounded capture and queue-full/drop accounting;
- known I/Q vectors and squared magnitude;
- first-word invalid handling;
- metadata and destination-MAC propagation;
- malformed/odd lengths;
- SDK RX-state and channel rejection;
- nearest accepted-probe attribution;
- explicit unattributed records;
- history/input bounds and unchanged output on decode failure.

The existing radio suite also checks that accepted probes publish the new
`PROBE_RX` acquisition event without changing protocol-v1 framing.

## Evidence boundary and hardware follow-up

NGN-003 HOST/BUILD acceptance proves deterministic parsing and that the pinned
ESP32-C3 firmware links the CSI path. It does **not** prove that the three
physical boards produce CSI for every scheduled link, establish real callback
timing, validate the 5 ms attribution window, measure queue pressure in a real
RF environment or establish sensing performance.

Those immediate acquisition sanity checks belong to the separate NGN-0031
hardware task. Baseline calibration and perturbation algorithms remain NGN-005;
full physical sensing acceptance remains NGN-009.
