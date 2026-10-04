# 07 — Passive BLE observation component

NGN-004 implements the self-contained BLE V1 acquisition and local-track boundary. It deliberately does not define inter-node encoding, coordinator fusion or display behavior.

## Runtime split

The ESP32-C3 adapter is `firmware/c3/components/ngn_ble_esp`. Pure observation-key and track logic remains in `ngn_core` so it is compiled by the native host harness.

The GAP callback is intentionally small:

1. accept only NimBLE discovery reports;
2. select the current over-the-air address (including `ota_addr` when host privacy support exposes a resolved identity separately);
3. copy the bounded legacy advertisement payload and receive metadata into a fixed FreeRTOS queue;
4. return without hashing, tracking or sink callbacks.

A worker task parses advertisement fields, creates the normalized observation, derives the session key, updates the bounded tracker and emits the privacy-safe track event.

Queue overflow is a quality condition: the newest report is dropped and `queue_drops` increments. Duplicate filtering is disabled because repeated advertisements are useful RSSI samples; ESP-IDF's NimBLE host congestion check is enabled as a second bounded-overload guard.

## Passive-only NimBLE profile

The pinned ESP-IDF v5.5.5 configuration enables Bluetooth/NimBLE and the Observer role. It explicitly disables Central, Peripheral, Broadcaster, both GATT roles and the Security Manager. Scanning uses `ble_gap_disc()` with `passive = 1`; the component contains no connect, pairing or GATT-interrogation path.

ESP32-C3's v5.5.5 controller configuration is already BLE-only and does not define the ESP32-only `BTDM_CTRL_MODE_*` selectors. Those selectors are therefore intentionally absent from `sdkconfig.defaults`; passive narrowing uses the target-valid NimBLE role/profile/SMP controls and the scan parameters above.

`CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y` is retained. ESP-IDF v5.5.5 documents ESP32-C3 as a single shared 2.4 GHz RF resource and supports Wi-Fi STA + BLE scan coexistence. Missing observations during Wi-Fi activity therefore remain expected evidence-quality information rather than an acquisition failure.

## Session key

The caller must inject 8–32 bytes of session nonce when starting the ESP adapter. The component never creates or persists a fallback nonce.

Key material is versioned and includes:

- session nonce;
- current BLE address type;
- current over-the-air address;
- a bounded signature of the current advertisement payload;
- manufacturer company ID when present;
- a canonicalized bounded set of 16-bit service UUIDs.

The ESP adapter hashes that material with SHA-256 from the pinned mbedTLS component and exposes only a 64-bit session key. Receiver node ID is intentionally excluded, allowing different C3 nodes with the same nonce to derive the same key for the same current advertisement identity/signature.

A changed session nonce, changed current private/random address, or changed advertisement signature produces a different key. No attempt is made to join a rotated private address to an older track.

The payload signature and raw address are transient internal inputs; neither appears in the default track-event sink.

## Local track lifecycle

`ngn_ble_tracker_t` uses fixed storage with a runtime maximum up to `NGN_BLE_MAX_TRACKS` (32). The default configuration is:

- 24 tracks;
- `AGING` after 5 seconds without an observation;
- `EXPIRED` after 15 seconds;
- RSSI EWMA divisor 4.

Callers may supply other values. Expiration must remain later than aging.

Lifecycle is:

```text
first observation -> NEW
repeat observation -> ACTIVE
no observation for aging_after_ms -> AGING
no observation for expire_after_ms -> EXPIRED and removed
```

An observation returning while `AGING` moves the track back to `ACTIVE`. When the table is full, the least-recently-seen track is expired with reason `EVICTED`; equal-age ties are broken by session key, making eviction deterministic.

The track retains latest raw RSSI and an integer Q8.8 EWMA value. No dynamic allocation is used by the core tracker.

## Sink/privacy contract

`ngn_ble_track_event_t` contains:

- session-scoped key;
- receiver node;
- address type only (not address bytes);
- lifecycle and expiry reason;
- timestamps and observation count;
- latest and filtered RSSI;
- bounded advertisement metadata (type, flags, company ID, service UUID summary and payload length).

It contains no raw address and no unsalted payload signature. A future persistent logger should consume this event type rather than the transient acquisition report.

`CONFIG_NGN_BLE_DIAGNOSTIC_RAW_ADDRESS` is an explicit compile-time opt-in for local debug output. It defaults off and prints a warning when enabled.

## Parallel integration boundary

NGN-004 does not know the NGN-002 wire envelope, session-sync messages, epoch scheduler or node-discovery state. `ngn_ble_esp_start()` simply requires the already-coordinated nonce and node ID as inputs. NGN-002/006/008 can therefore consume the sink/stats interfaces later without this component encoding transport or fusion policy.

## Validation

Host tests cover:

- payload-signature determinism and observation canonicalization;
- same-session key stability across receiver nodes;
- session-nonce changes;
- private/random address rotation producing a new key;
- advertisement-signature changes producing a new key;
- NEW/ACTIVE/AGING/EXPIRED transitions;
- missing observations and duplicate advertiser updates;
- deterministic bounded eviction;
- RSSI filtering;
- out-of-order observation rejection.

The firmware CI build compiles the ESP adapter against ESP-IDF v5.5.5 for `esp32c3`. No physical BLE performance claim is made by NGN-004; multi-node coexistence measurement remains a later hardware/experiment gate.
