#!/usr/bin/env python3
from __future__ import annotations
import argparse, json
from pathlib import Path
import numpy as np
import pandas as pd

def cols(text: str) -> list[str]:
    return [x.strip() for x in text.split(",") if x.strip()]

def main() -> int:
    ap = argparse.ArgumentParser(description="Compare baseline and candidate golden-output CSVs.")
    ap.add_argument("--baseline", required=True, type=Path)
    ap.add_argument("--candidate", required=True, type=Path)
    ap.add_argument("--key-cols", required=True)
    ap.add_argument("--exact-cols", default="")
    ap.add_argument("--numeric-cols", default="")
    ap.add_argument("--atol", type=float, default=1e-8)
    ap.add_argument("--rtol", type=float, default=1e-6)
    ap.add_argument("--output-json", type=Path)
    args = ap.parse_args()

    a = pd.read_csv(args.baseline, encoding="utf-8-sig")
    b = pd.read_csv(args.candidate, encoding="utf-8-sig")
    keys = cols(args.key_cols)
    exact = cols(args.exact_cols)
    numeric = cols(args.numeric_cols)

    needed = keys + exact + numeric
    for label, df in [("baseline", a), ("candidate", b)]:
        miss = [c for c in needed if c not in df.columns]
        if miss:
            raise SystemExit(f"{label} missing columns: {miss}")
        if df.duplicated(keys).any():
            raise SystemExit(f"{label} contains duplicate key rows for {keys}")

    ma = a.set_index(keys)
    mb = b.set_index(keys)
    only_a = ma.index.difference(mb.index)
    only_b = mb.index.difference(ma.index)
    common = ma.index.intersection(mb.index)

    mismatches = []
    for idx in common:
        ra, rb = ma.loc[idx], mb.loc[idx]
        for c in exact:
            va, vb = ra[c], rb[c]
            if (pd.isna(va) and pd.isna(vb)):
                continue
            if str(va) != str(vb):
                mismatches.append({"key": str(idx), "column": c, "baseline": str(va), "candidate": str(vb)})
        for c in numeric:
            va, vb = float(ra[c]), float(rb[c])
            if not np.isclose(va, vb, atol=args.atol, rtol=args.rtol, equal_nan=True):
                mismatches.append({"key": str(idx), "column": c, "baseline": va, "candidate": vb})

    status = "PASS" if not len(only_a) and not len(only_b) and not mismatches else "FAIL"
    report = {
        "status": status,
        "baseline_rows": int(len(a)),
        "candidate_rows": int(len(b)),
        "common_keys": int(len(common)),
        "missing_in_candidate": [str(x) for x in list(only_a[:20])],
        "extra_in_candidate": [str(x) for x in list(only_b[:20])],
        "mismatch_count": len(mismatches),
        "mismatch_preview": mismatches[:50],
        "atol": args.atol,
        "rtol": args.rtol,
    }
    print(json.dumps(report, ensure_ascii=False, indent=2))
    if args.output_json:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    return 0 if status == "PASS" else 1

if __name__ == "__main__":
    raise SystemExit(main())
