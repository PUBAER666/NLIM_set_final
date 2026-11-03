#!/usr/bin/env python3
from __future__ import annotations
import argparse, json
from pathlib import Path
import pandas as pd

def split_csv(text: str) -> list[str]:
    return [x.strip() for x in text.split(",") if x.strip()]

def main() -> int:
    ap = argparse.ArgumentParser(description="Audit split-manifest structure and basic reproducibility risks.")
    ap.add_argument("--csv", required=True, type=Path)
    ap.add_argument("--split-col", default="Split")
    ap.add_argument("--class-col", default="")
    ap.add_argument("--sample-col", default="")
    ap.add_argument("--path-col", default="")
    ap.add_argument("--required-cols", default="")
    ap.add_argument("--allowed-splits", default="Train,Val,Test")
    ap.add_argument("--output-json", type=Path)
    args = ap.parse_args()

    df = pd.read_csv(args.csv, encoding="utf-8-sig")
    if df.empty:
        raise SystemExit("Input CSV is empty.")

    required = split_csv(args.required_cols)
    for c in [args.split_col, args.class_col, args.sample_col, args.path_col]:
        if c and c not in required:
            required.append(c)

    missing = [c for c in required if c not in df.columns]
    if missing:
        raise SystemExit(f"Missing required columns: {missing}")

    issues = []
    split = df[args.split_col].astype(str).str.strip()
    allowed = set(split_csv(args.allowed_splits))
    illegal = sorted(set(split) - allowed - {""})
    if illegal:
        issues.append(f"Illegal split values: {illegal}")
    blank_split = int((split == "").sum())
    if blank_split:
        issues.append(f"Blank split rows: {blank_split}")

    duplicate_sample_rows = 0
    if args.sample_col:
        duplicate_sample_rows = int(df[args.sample_col].astype(str).duplicated(keep=False).sum())
        if duplicate_sample_rows:
            issues.append(f"Duplicate sample-id rows: {duplicate_sample_rows}")

    duplicate_path_rows = 0
    if args.path_col:
        norm = df[args.path_col].astype(str).str.replace("\\\\", "/", regex=False).str.strip()
        duplicate_path_rows = int(norm.duplicated(keep=False).sum())
        if duplicate_path_rows:
            issues.append(f"Duplicate path rows: {duplicate_path_rows}")

    class_by_split = {}
    if args.class_col:
        tab = pd.crosstab(df[args.class_col].astype(str), split)
        class_by_split = {
            str(idx): {str(k): int(v) for k, v in row.items()}
            for idx, row in tab.to_dict(orient="index").items()
        }

    report = {
        "status": "PASS" if not issues else "FAIL",
        "rows": int(len(df)),
        "columns": list(map(str, df.columns)),
        "split_counts": split.value_counts(dropna=False).to_dict(),
        "class_by_split": class_by_split,
        "blank_split_rows": blank_split,
        "duplicate_sample_rows": duplicate_sample_rows,
        "duplicate_path_rows": duplicate_path_rows,
        "issues": issues,
    }

    print(json.dumps(report, ensure_ascii=False, indent=2, default=str))
    if args.output_json:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(
            json.dumps(report, ensure_ascii=False, indent=2, default=str),
            encoding="utf-8",
        )
    return 0 if report["status"] == "PASS" else 1

if __name__ == "__main__":
    raise SystemExit(main())
