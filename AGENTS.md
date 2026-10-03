# Agent instructions

This repository is an experimental RF-sensing firmware programme. Work must remain reproducible, evidence-driven and scoped to the active issue.

## Authority order

Before changing code:

1. reconcile live `main`, the active issue, existing branches/PRs and current CI;
2. read this file;
3. read `RAG.md`;
4. read `docs/00-PROGRAMME.md` and `docs/01-ARCHITECTURE.md`;
5. read every remaining document referenced by the issue;
6. read the complete issue body and all comments.

The active issue may narrow scope. It must not silently invalidate architecture, privacy, validation or evidence requirements. If a genuine authority correction is required, update the relevant documentation in the same PR and explain why.

## Scope discipline

- Implement only the active issue and its necessary support changes.
- Do not absorb work from dependency-ready sibling issues merely because it is convenient.
- Keep radio acquisition, signal processing, fusion and visualization as separable modules.
- Keep algorithms host-testable wherever practical.
- Avoid cloud services, accounts, external telemetry and remote backends unless a later issue explicitly adds them.
- Do not add a dependency solely to avoid writing a small, well-tested local abstraction.
- Do not silently change the pinned toolchain.

## Experimental claims

This project measures changes in a multipath RF environment. Therefore:

- do not describe a field visualization as physical localization unless physical experiments establish that claim;
- do not describe a temporal BLE/CSI coincidence as identifying a person or associating a device with a person;
- do not claim reliable static occupancy detection unless the acceptance evidence establishes it;
- label simulated, replayed and physical-hardware evidence distinctly.

## BLE constraints

The baseline BLE subsystem is passive observation only.

- No connection attempts.
- No active GATT interrogation.
- No attempts to defeat Bluetooth privacy addressing.
- No long-term persistent tracking identity derived from rotating/private addresses.
- Persist only what is required for experiments and follow `docs/06-PRIVACY-AND-SCOPE.md`.

## Hardware evidence

Never fabricate hardware results.

If an issue requires physical hardware and the executing environment does not have it:

1. complete all non-hardware work that can be validated honestly;
2. record the exact remaining hardware acceptance step;
3. do not mark that acceptance as passed;
4. follow the issue's merge gate.

## Testing

At minimum, run all tests required by `docs/04-VALIDATION.md` and the active issue.

Pure logic should have deterministic native tests. Firmware changes must compile against the pinned ESP-IDF target. Python tooling must have deterministic tests for parsing/replay behavior.

## Git and PR completion

For an implementation issue:

1. branch from current `main`;
2. make focused commits;
3. run required local checks;
4. open one PR linked to the issue;
5. inspect the complete diff for scope, accidental files and documentation drift;
6. wait for automated checks and fix failures;
7. merge only when the issue acceptance criteria and required checks are satisfied;
8. prefer squash merge unless the repository has since established a different explicit convention;
9. verify post-merge `main` CI when available;
10. close the issue through the merged PR.

Do not create duplicate PRs for the same issue.

## Dependency handling

An issue may start only when every dependency named in `docs/00-PROGRAMME.md` and the issue body is complete, unless the issue explicitly states that it may proceed in parallel.

When two issues are designed for parallel work, preserve their component boundaries to minimize merge conflicts.

## Documentation

Update documentation when behavior, wire format, configuration, build instructions, experiment procedure or acceptance evidence changes.

Do not write aspirational behavior as if it has already been implemented.
