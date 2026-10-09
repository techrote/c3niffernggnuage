# 04 — Validation

## Evidence classes

Use these labels in issues/PRs/results:

- **HOST** — native deterministic unit/integration test.
- **BUILD** — pinned ESP-IDF firmware build.
- **REPLAY** — algorithm exercised against recorded/synthetic event streams.
- **HARDWARE** — result observed on the physical target boards.
- **EXPERIMENT** — labelled multi-node measurement run with retained data.

Do not substitute one class for another without saying so.

## CI baseline

The repository's `.github/workflows/ci.yml` runs native CTest and the pinned ESP32-C3 build on pull requests and pushes to `main`. Required automated coverage includes:

- native C/C++ logic tests;
- ESP32-C3 firmware build under pinned ESP-IDF v5.5.5;
- Python tests/lint for host tooling when present;
- deterministic protocol/schema tests.

ESP8266 CI is added only by NGN-010.

## NGN-002 acceptance

NGN-002 requires both **HOST** and **BUILD** evidence against the reviewed implementation. The former unpublished candidate's reported 5/5 result is historical context and is not acceptance evidence for reconstructed code.

The complete native CTest suite has seven entries:

| CTest entry | Responsibility |
| --- | --- |
| `ngn_node` | Explicit A/B/C identity and firmware/protocol version contract |
| `ngn_ble` | Existing NGN-004 observation keys, privacy boundaries and track lifecycle regression coverage |
| `ngn_protocol` | v1 wire-format goldens, CRC, lengths, malformed payloads and serial-wrap rules |
| `ngn_schedule` | Exact default chronology, configuration bounds, deterministic plans and exclusive deadlines |
| `ngn_radio` | Runtime/session/scheduler integration and mock transport behavior |
| `ngn_radio_adversarial` | Stale/invalid input, identity and sequence non-poisoning, timeout/rejoin, delayed/obsolete TX and a deterministic three-node mock bus |
| `ngn_display` | Existing NGN-007 framebuffer, clipping and synthetic renderer regression coverage |

Run the complete suite, including the existing BLE/display tests, rather than selecting only the newly added targets:

```bash
cmake -S tests/host -B build/host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host --parallel
ctest --test-dir build/host --output-on-failure
```

GCC/Clang use C11 with `-Wall -Wextra -Werror -pedantic`. Record the actual tested commit/tree and result. The table defines suite coverage; it is not a claim that a particular CI run has passed.

**BUILD** requires a complete firmware build for `esp32c3` under **ESP-IDF v5.5.5**, including the ESP-NOW adapter and application integration. A native compile, an adapter syntax check or an SDK version other than the pin does not satisfy that gate. Follow `docs/05-BUILDING.md` and record the actual build/CI result. The safe unconfigured runtime default still compiles the implementation; it does not represent an on-board radio test.

Before merge, inspect the complete diff for NGN-004/007 regressions, stale protocol/configuration documentation, generated artifacts and accidental CSI/BLE/fusion/OLED scope. Verify bounded callbacks and queue behavior from code as well as the host-mock tests: the mocked transport does not execute the real Wi-Fi task. The baseline must require no external router, and the documented byte format must match the encoder/decoder. Merge only after the actual required checks succeed; verify the resulting `main` CI and close #2 through that PR.

NGN-002 has no physical sensing gate. Its nominal 390 ms epoch and host mock-bus timings are configuration/HOST evidence, not measured RF airtime, delivery rate, coexistence performance or sensing quality. Those measurements remain NGN-009 and the owning later issues.

## Native tests

Logic that does not require an ESP radio should be extracted behind narrow interfaces and tested on the host.

Required categories as features land:

### Protocol/scheduler

- independent complete-frame goldens for SYNC, PROBE and NODE_HEALTH, known CRC vector and encode/decode agreement;
- exact typed payload lengths, reserved bytes/flags, incompatible source/version/type and corrupt CRC rejection, with no output mutation on failure;
- wrap-aware sequence/epoch freshness, including duplicates and ambiguous half-range differences;
- current-session/active-epoch receive checks, immutable session configuration, nonzero coordinator session and pinned source-MAC collision handling;
- exact 390 ms default plan, bounded bursts, arithmetic/configuration limits and exclusive transmission deadlines;
- silent follower `WAIT_SYNC`, longer discovery/presence timeouts, same-MAC reboot/rejoin and retired-session rejection;
- bounded RX/status processing, expiry instead of catch-up bursts, refreshed queued SYNC timing and cancellation of obsolete queued sessions;
- deterministic three-node mock-bus operation with health/probe observations and explicit missing-node behavior.

Keep transmit-submission deadlines separate from RF airtime claims. Test presence separately from a cached health snapshot, and queued probe sequence separately from driver completion. Session cancellation is a local diagnostic, not a new wire-health field or a late-drop count.

### CSI parsing/reduction

- known I/Q vectors -> expected magnitude;
- malformed/odd lengths rejected;
- source attribution;
- baseline initialization;
- zero/noise stability;
- synthetic perturbation raises expected metrics;
- missing/outlier sample handling.

### BLE

- passive observation normalization;
- session key stable within one session;
- session key changes with session nonce;
- private/random address rotation is not joined to old track;
- TTL lifecycle;
- multi-node RSSI aggregation with missing observations.

### Fusion

- edge-vector handling;
- age/quality weighting;
- coincidence-window boundaries;
- no BLE event does not suppress CSI visualization;
- no CSI event does not suppress BLE track visualization.

### Renderer

- framebuffer bounds;
- tiny-resolution clipping;
- deterministic snapshot/golden tests where practical;
- degraded/missing-node indicators;
- no dependence on live radio APIs.

## Physical acceptance setup

NGN-009 owns final three-C3 acceptance.

Suggested geometry:

- three nodes around the test space;
- roughly triangular placement;
- start with side lengths around 1 m or greater where practical;
- fixed orientation during a labelled run;
- record approximate geometry and device orientation in experiment metadata.

Exact geometry is an experimental variable, not a universal requirement.

## Required experiment scenarios

At minimum capture labelled sessions for:

1. empty/quiet baseline;
2. person traversing near each triangle edge;
3. person moving through the centre;
4. step-in then remain mostly still;
5. person moving without an intentionally carried BLE advertiser where feasible;
6. BLE-advertising device introduced/moved while observing CSI;
7. one C3 temporarily missing or rebooted;
8. repeated run after fresh calibration.

The purpose is comparison, not to force a predetermined outcome.

## Acceptance observations to report

NGN-009 should report measured values, not just "works":

- effective epoch rate;
- per-link CSI sample counts and rejection/drop counts;
- ESP-NOW/probe loss;
- BLE observations per node and obvious coexistence gaps;
- baseline stability distribution;
- activity-score distributions for labelled quiet/motion windows;
- event/display latency;
- node dropout/recovery behavior;
- false/ambiguous events observed;
- firmware memory/queue high-water marks if available.

## Threshold setting

Do not predeclare a precision or false-positive rate that has not been measured.

Use the labelled dataset to select initial thresholds, record them with the dataset/session, and preserve replay tests that demonstrate the selected behavior.

## Hardware gate

If a task requires HARDWARE or EXPERIMENT evidence and the executing environment lacks the boards, the task is not physically accepted. It may prepare tooling/firmware, but the issue's hardware gate remains explicit.

## Regression evidence

Once a labelled dataset exists, replay it in CI where practical. Future algorithm changes should compare against the retained baseline so improvements are measurable and regressions are visible.
