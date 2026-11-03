---
name: ps-pl-release-validation
description: Validate a Zynq PS–PL model deployment or release using interface contracts, golden outputs, staged event diagnostics and provenance-aware performance evidence. Use for migration, mismatch troubleshooting and delivery review; does not establish field accuracy or authorize programming hardware.
---

# PS PL deployment validation

Start by recording the firmware revision, bitstream hash, model identifier, input units, interface ABI, register map and evidence origin. Treat a requested future version as a target until its actual artifacts are available. Never repair a version mismatch by changing only the displayed version string.

## Choose a workflow

- Model migration: align preprocessing and tensor ordering, compare full numerical outputs with `scripts/compare_vectors.py`, then check interfaces and interrupt/cache behavior. A correct class label alone is insufficient. See [interface_contract.md](references/interface_contract.md).
- Failed recognition: split acquisition validity, event extraction, base rejection, registered matching and accounting. Investigate the earliest failed stage before tuning the model. See [event_triage.md](references/event_triage.md).
- Release review: run `scripts/verify_delivery.py` against a trusted checksum list; check BOOT, receiver and model compatibility separately. Hash equality alone does not prove functional correctness.
- Performance reporting: identify valid compute transactions before extracting latency. A report averaging rejected requests with inference requests cannot supply average inference time. See [performance_evidence.md](references/performance_evidence.md).

## Persistence and invalidation

When registration is persistent, test deletion across reboot as well as enrollment. Commit the new library before publishing it in RAM. Reject stale UI revisions. After deletion or compaction, cancel in-flight jobs and invalidate cached reference IDs; retained identities must be mapped by stable identity, not their old array positions. Use a bounded fault-injection test rather than deleting real field data.

## Evidence and stopping conditions

Label each result as analytic, simulation, host regression, or board measurement. Record the test input and actual result, including failures. Stop a comparison if units, ABI or output shape differ. If no board is available, complete local checks and state which board checks remain; do not infer hardware speedup or accuracy from local success. Do not flash, publish, or send artifacts externally without task authorization.

The generic scripts accept English-path files and JSON numbers. Their tests are in `scripts/test_tools.py`. NILM-specific symptom examples are transferable reasoning patterns, not universal thresholds.
