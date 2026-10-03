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

Once NGN-001 lands, required automated checks should include:

- native C/C++ logic tests;
- ESP32-C3 firmware build under pinned ESP-IDF v5.5.5;
- Python tests/lint for host tooling when present;
- deterministic protocol/schema tests.

ESP8266 CI is added only by NGN-010.

## Native tests

Logic that does not require an ESP radio should be extracted behind narrow interfaces and tested on the host.

Required categories as features land:

### Protocol/scheduler

- encode/decode round trip;
- malformed length/version rejection;
- sequence wrap behavior;
- stale session/epoch rejection;
- deterministic slot schedule;
- timeout/degraded-node behavior.

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
