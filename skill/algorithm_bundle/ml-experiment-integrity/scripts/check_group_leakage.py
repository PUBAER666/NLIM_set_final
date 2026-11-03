#!/usr/bin/env python3
from __future__ import annotations
import argparse, json
from pathlib import Path
import pandas as pd

def parse_cols(text: str) -> list[str]:
    return [x.strip() for x in text.split(",") if x.strip()]

def main() -> int:
    ap = argparse.ArgumentParser(description="Check whether logical groups cross dataset splits.")
    ap.add_argument("--csv", required=True, type=Path)
    ap.add_argument("--split-col", default="Split")
    ap.add_argument("--group-cols", required=True, help="Comma-separated grouping columns.")
    ap.add_argument("--sample-col", default="")
    ap.add_argument("--path-col", default="")
    ap.add_argument("--output-json", type=Path)
    args = ap.parse_args()

    df = pd.read_csv(args.csv, encoding="utf-8-sig")
    group_cols = parse_cols(args.group_cols)
    required = [args.split_col, *group_cols]
    for c in [args.sample_col, args.path_col]:
        if c:
            required.append(c)
    missing = [c for c in required if c not in df.columns]
    if missing:
        raise SystemExit(f"Missing columns: {missing}")

    if df.empty:
        raise SystemExit("Input CSV is empty.")

    split = df[args.split_col].astype(str).str.strip()
    blank_split = int((split == "").sum())

    grouped = (
        df.assign(_split=split)
          .groupby(group_cols, dropna=False)["_split"]
          .agg(lambda s: sorted(set(x for x in s if x)))
          .reset_index(name="splits")
    )
    grouped["split_count"] = grouped["splits"].map(len)
    leaked = grouped[grouped["split_count"] > 1].copy()

    duplicate_samples = 0
    if args.sample_col:
        duplicate_samples = int(df[args.sample_col].astype(str).duplicated(keep=False).sum())

    duplicate_paths = 0
    if args.path_col:
        norm = df[args.path_col].astype(str).str.replace("\\\\", "/", regex=False).str.strip()
        duplicate_paths = int(norm.duplicated(keep=False).sum())

    status = "PASS"
    if blank_split or len(leaked) or duplicate_samples or duplicate_paths:
        status = "FAIL"

    report = {
        "status": status,
        "rows": int(len(df)),
        "split_counts": split.value_counts(dropna=False).to_dict(),
        "group_columns": group_cols,
        "group_count": int(len(grouped)),
        "leaked_group_count": int(len(leaked)),
        "blank_split_rows": blank_split,
        "duplicate_sample_rows": duplicate_samples,
        "duplicate_path_rows": duplicate_paths,
        "leaked_groups_preview": leaked.head(20).to_dict(orient="records"),
    }

    print(json.dumps(report, ensure_ascii=False, indent=2, default=str))
    if args.output_json:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(
            json.dumps(report, ensure_ascii=False, indent=2, default=str),
            encoding="utf-8",
        )
    return 0 if status == "PASS" else 1

if __name__ == "__main__":
    raise SystemExit(main())
