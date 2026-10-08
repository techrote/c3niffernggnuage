# NGN-002 — Three-node protocol, session discovery and deterministic radio scheduler

**Dependencies:** NGN-001 complete.  
**Parallelism:** may proceed in parallel with NGN-004 and NGN-007. Do not absorb BLE or display scope.

## Agent task prompt

Implement the three-C3 control/probe protocol and deterministic sensing scheduler. Establish versioned message framing, explicit A/B/C identities, coordinator session/epoch synchronization, fixed-channel ESP-NOW transport in Wi-Fi station mode, bounded probe bursts, node health, and a host-testable scheduler/state machine. Do not implement CSI processing, BLE scanning or OLED behavior. Finish through PR, green checks and merge.


## Authority and execution rules

Start by reconciling live `main`, this issue, branches/PRs and CI. Read, in order:

- `AGENTS.md`
- `RAG.md`
- `docs/00-PROGRAMME.md`
- `docs/01-ARCHITECTURE.md`
- `docs/02-PROTOCOL-AND-DATA.md`
- `docs/03-SENSING.md`
- `docs/04-VALIDATION.md`
- `docs/05-BUILDING.md`
- `docs/06-PRIVACY-AND-SCOPE.md`
- `docs/REFERENCES.md`
- this complete issue body and comments

Implement only this issue plus strictly necessary support changes. Preserve the component boundaries and privacy/evidence constraints in the authority docs.

Complete the task end-to-end: implementation, deterministic tests, relevant documentation, linked PR, automated checks, fixes, final diff review and merge. Prefer squash merge unless the repository has since established another explicit convention. Verify post-merge `main` CI when available.

Never fabricate hardware evidence. If this issue has a physical-hardware gate and the required boards are unavailable, complete honest preparatory work, record the exact remaining gate, and do not mark the physical acceptance as passed.


## Required implementation

1. **Versioned message envelope**
   - protocol version;
   - message type;
   - source node ID;
   - session identifier/nonce reference;
   - epoch ID;
   - sequence number;
   - explicit payload length;
   - deterministic malformed/stale rejection.
2. **Node identities**
   - support logical A, B and C/coordinator roles;
   - role configured explicitly;
   - source-MAC mapping is observable and validated.
3. **Session lifecycle**
   - coordinator begins a session with fresh session nonce/ID;
   - nodes join/synchronize without an external router;
   - stale previous-session messages are ignored.
4. **Epoch/scheduler**
   - deterministic state machine for A/B/C probe slots;
   - bounded configurable burst count/spacing;
   - configurable BLE/coexistence opportunity placeholder only — do not start BLE scans here;
   - tolerate a temporarily missing node;
   - expose slot/epoch events to higher layers.
5. **ESP-NOW adapter**
   - Wi-Fi station mode;
   - one configured 2.4 GHz channel;
   - bounded send/receive queues;
   - probe packets small and stable;
   - avoid performing substantial work in radio callbacks.
6. **Node health**
   - session/epoch, probe TX/RX sequence, queue/drop/error counters and firmware/protocol version;
   - no invented battery measurement.
7. **Tests**
   - protocol encode/decode;
   - version/length rejection;
   - stale session/epoch;
   - sequence wrap;
   - deterministic schedule;
   - node timeout/rejoin behavior;
   - transport adapter can be mocked on host.
8. Update protocol/architecture docs to match exact landed encoding and configuration.

## Explicit non-scope

- CSI callback/data reduction;
- BLE scanning or advertiser tracking;
- field/activity algorithms;
- OLED;
- ESP8266.

## Acceptance

- **HOST:** protocol and scheduler suites pass deterministically.
- **BUILD:** ESP32-C3 build passes under the pinned toolchain.
- No router is required by the baseline schedule.
- Radio callbacks remain bounded.
- Protocol wire format and version are documented.
