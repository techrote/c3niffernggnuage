## Progress checkpoint — 2026-10-04

Work on NGN-002 is paused at a clean pre-publication checkpoint.

### Repository state

- Current `main`: `72a1c5c2c680c23471e67270c715a7f93f6f9c92` — NGN-007 / PR #13 has merged since this task began. NGN-004 changes are also now present on live `main`.
- NGN-002 branch: `ngn-002-protocol-scheduler`
- Branch head is still the original dependency-ready base: `6d7a7430dc6b94d8b9810e716d8fb0caaa5c0cff`.
- No NGN-002 PR exists yet.
- Therefore the prepared NGN-002 candidate must be reconciled onto current `main` before any commit/PR is published. Preserve the intentionally parallel NGN-004/NGN-007 component boundaries.

### Prepared NGN-002 implementation

A reviewed local candidate has been prepared for the issue scope only:

- protocol v1 explicit byte framing with A/B/C source ID, non-zero session ID, epoch, per-source sequence, explicit payload length and CRC-16/CCITT-FALSE;
- exact v1 payload contracts for `SYNC`, `PROBE` and `NODE_HEALTH`;
- wrap-aware 32-bit sequence/epoch freshness handling and deterministic malformed/stale rejection;
- coordinator/follower session state, fresh coordinator session ID, timeout/discovery/rejoin handling and stale previous-session rejection;
- first-valid-observation A/B/C → station-MAC binding with node/MAC collision rejection and observable bindings;
- deterministic epoch scheduler with bounded A/B/C probe bursts, configurable spacing, BLE/coexistence opportunity placeholder only, staggered node-health slots and missing-node tolerance;
- narrow host-mockable transport seam;
- ESP32-C3 fixed-channel broadcast ESP-NOW adapter in Wi-Fi station mode, with bounded RX/TX/status queues and callback work limited to copy/enqueue/status capture;
- node-health payload/counters without invented battery telemetry;
- Kconfig/runtime defaults and documentation for wire format, schedule, channel and queue bounds;
- no CSI processing, BLE scanning/tracking, field fusion or OLED behavior added.

### Validation completed on the prepared candidate

- **HOST:** native protocol/scheduler/session/transport suites reported 5/5 passing under the repository warning policy (`-Wall -Wextra -Werror -pedantic`).
- Covered: encode/decode, version/length/CRC rejection, sequence wrap, stale session/epoch, deterministic schedule, timeout/rejoin, MAC-binding conflicts, missing-node health behavior and mock transport.
- A tightening pass also made the encoder reject a caller-declared payload length that does not exactly match the supplied bytes, and removed a padding-sensitive struct `memcmp` from health tests.

### Remaining work

1. Reconcile/rebase the prepared NGN-002 candidate onto live `main` at `72a1c5c2...`, resolving only genuine integration changes from NGN-004/007.
2. Re-run all native tests after reconciliation.
3. Commit/push the exact reviewed tree to `ngn-002-protocol-scheduler`.
4. Open one linked PR for #2.
5. Run GitHub Actions and satisfy both required gates:
   - **HOST** deterministic suites;
   - **BUILD** ESP32-C3 firmware under pinned ESP-IDF v5.5.5.
6. Fix any target/API integration failures, review the complete final diff for scope/documentation drift, then squash-merge if acceptance is satisfied.
7. Verify post-merge `main` CI and close #2 through the merged PR.

No hardware acceptance has been claimed or fabricated; #2 does not require the NGN-009 physical sensing gate.