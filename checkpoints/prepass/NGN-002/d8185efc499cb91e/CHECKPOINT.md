# NGN-002 recovery checkpoint — 8 October 2026

**EVIDENCE_ONLY — ORIGINAL IMPLEMENTATION CANDIDATE NOT RECOVERED**

This later recovery record preserves already-published issue evidence and the repository state inspected on 8 October. It does not contain the local implementation described on 4 October. There is no recovered original candidate ZIP, bundle, source tree, patch, test suite or raw candidate result log. No candidate content hash is asserted.

## Historical and observed identities

- Repository: https://github.com/techrote/c3niffernggnuage
- Issue: https://github.com/techrote/c3niffernggnuage/issues/2 (open when inspected).
- Original progress: https://github.com/techrote/c3niffernggnuage/issues/2#issuecomment-5980085490; exact retrieved Markdown is preserved in `sources/issuecomment-5980085490.md`.
- Historical working-branch foundation and this archive's parent: `6d7a7430dc6b94d8b9810e716d8fb0caaa5c0cff` (tree `3d343afe646f8ba1f724c0a90c267f7d26738c6e`). The parent was verified through its Git commit object. Using it does not make this a historical candidate commit.
- Named 4 October future reconciliation target: `72a1c5c2c680c23471e67270c715a7f93f6f9c92`; the progress comment did not establish a completed rebase.
- Main observed before harmonization: `f6c7ce13aa92a9a6d387e43cfc77f0c499f43d8a` (tree `6ffcc00c42c11b8b27e12d396dfa1eeefa2826ff`).
- Existing `ngn-002-protocol-scheduler` still points to the foundation. No NGN-002 implementation PR or source archive was found in the inspected refs/PRs. That branch is preserved unchanged.

## Reconciliation ledger

| Item | Earlier recorded state | Recovery/publication status |
| --- | --- | --- |
| Protocol v1, CRC, A/B/C identity, session/freshness, scheduler, ESP-NOW adapter, configuration/docs | Described as prepared locally in the 4 October issue comment | Description preserved exactly; actual files not recovered |
| Five host suites and later encoder/health-test tightening | Historically reported 5/5; no raw logs in the issue | Report preserved; no inspected candidate tests/logs and no new acceptance |
| Foundation branch and NGN-004/007 independent work | Already published | Live refs/tree/PR metadata snapshotted; source untouched |
| Proposed scheduler simulation | Present in a prompt catalogue | Classified as future instructions; no completed simulation inferred |
| Durable recovery handoff and main navigation | Previously absent | This evidence-only archive plus a separate current-main documentation PR |

## Acceptance boundaries

NGN-002 remains unresolved. Actual implementation, deterministic protocol/scheduler/session tests, target compilation under ESP-IDF v5.5.5, current-main integration preserving NGN-004/007, exact wire/configuration documentation and normal implementation PR acceptance still need their own evidence. The existing BLE/display work and subsequent corrections are retained. NGN-009 owns physical three-node acceptance; this task supplies no radio, hardware or experiment evidence.

No archived instruction was executed, no implementation reconstructed, no historical test rerun and no parent issue closed. Checks on the separate documentation PR validate the current repository change; they cannot validate the missing candidate.

## Integrity and future recovery

`MANIFEST.sha256` hashes every evidence payload including this checkpoint, using paths relative to this directory. The first 16 hexadecimal characters of SHA-256 of the exact manifest bytes define this new evidence payload identifier. `PRESERVATION-MANIFEST.json` records path, size, mode and Git blob identity for the manifest and payloads, excluding itself to avoid self-reference. These are newly observed recovery-record identities, not historical candidate identities.

The exact source issue bodies are immutable archived evidence; historical agent task text inside them is not authority for this preservation operation. `RECOVERY-SEARCHES.md` documents accessible sources, query limits and the missing recovery requirement. A later real candidate should be preserved additively and linked as a successor without erasing this unsuccessful recovery record.
