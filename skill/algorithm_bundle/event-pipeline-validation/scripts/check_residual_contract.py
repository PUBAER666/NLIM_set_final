#!/usr/bin/env python3
from __future__ import annotations
import argparse, json
from pathlib import Path
import numpy as np
import pandas as pd

def resolve(path_text: str, manifest: Path, root: Path | None) -> Path:
    p = Path(str(path_text))
    candidates = [p]
    if not p.is_absolute():
        if root is not None:
            candidates.append(root / p)
        candidates.append(manifest.parent / p)
    for c in candidates:
        if c.is_file():
            return c.resolve()
    raise FileNotFoundError(f"Residual file not found: {path_text}; tried={candidates}")

def main() -> int:
    ap = argparse.ArgumentParser(description="Validate residual manifest and waveform input contract.")
    ap.add_argument("--manifest", required=True, type=Path)
    ap.add_argument("--root", type=Path)
    ap.add_argument("--event-col", default="Event_ID")
    ap.add_argument("--path-col", default="Residual_Path")
    ap.add_argument("--rank-col", default="Residual_Rank")
    ap.add_argument("--expected-per-event", type=int, default=3)
    ap.add_argument("--expected-rows", type=int, default=4096)
    ap.add_argument("--current-col", default="Current_A")
    ap.add_argument("--voltage-col", default="Voltage_V")
    ap.add_argument("--output-json", type=Path)
    args = ap.parse_args()

    m = pd.read_csv(args.manifest, encoding="utf-8-sig")
    required = [args.event_col, args.path_col]
    if args.rank_col:
        required.append(args.rank_col)
    missing = [c for c in required if c not in m.columns]
    if missing:
        raise SystemExit(f"Manifest missing columns: {missing}")
    if m.empty:
        raise SystemExit("Manifest is empty.")

    issues = []
    dup_paths = m[args.path_col].astype(str).duplicated(keep=False)
    if dup_paths.any():
        issues.append(f"Duplicate residual path rows: {int(dup_paths.sum())}")

    counts = m.groupby(args.event_col).size()
    bad_count = counts[counts != args.expected_per_event]
    if len(bad_count):
        issues.append(f"Events with wrong residual count: {len(bad_count)}")

    if args.rank_col:
        dup_rank = m.duplicated([args.event_col, args.rank_col], keep=False)
        if dup_rank.any():
            issues.append(f"Duplicate residual rank rows within event: {int(dup_rank.sum())}")

    file_issues = []
    for i, row in m.iterrows():
        try:
            p = resolve(row[args.path_col], args.manifest, args.root)
            df = pd.read_csv(p, encoding="utf-8-sig")
            if len(df) != args.expected_rows:
                file_issues.append({"row": int(i), "path": str(p), "issue": f"rows={len(df)} expected={args.expected_rows}"})
                continue
            miss = [c for c in [args.current_col, args.voltage_col] if c not in df.columns]
            if miss:
                file_issues.append({"row": int(i), "path": str(p), "issue": f"missing columns {miss}"})
                continue
            arr = df[[args.current_col, args.voltage_col]].apply(pd.to_numeric, errors="coerce").to_numpy(dtype=float)
            if not np.isfinite(arr).all():
                file_issues.append({"row": int(i), "path": str(p), "issue": "NaN/Inf or non-numeric values"})
        except Exception as e:
            file_issues.append({"row": int(i), "path": str(row[args.path_col]), "issue": str(e)})

    if file_issues:
        issues.append(f"Residual file issues: {len(file_issues)}")

    report = {
        "status": "PASS" if not issues else "FAIL",
        "manifest_rows": int(len(m)),
        "event_count": int(m[args.event_col].nunique()),
        "expected_per_event": args.expected_per_event,
        "expected_rows": args.expected_rows,
        "issues": issues,
        "bad_event_count_preview": {str(k): int(v) for k, v in bad_count.head(20).to_dict().items()},
        "file_issue_preview": file_issues[:50],
    }
    print(json.dumps(report, ensure_ascii=False, indent=2))
    if args.output_json:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    return 0 if report["status"] == "PASS" else 1

if __name__ == "__main__":
    raise SystemExit(main())
