#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json
from pathlib import Path
import pandas as pd
from pandas.testing import assert_frame_equal

def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

def compare_json(a: Path, b: Path) -> tuple[bool, str]:
    try:
        ja = json.loads(a.read_text(encoding="utf-8-sig"))
        jb = json.loads(b.read_text(encoding="utf-8-sig"))
        return ja == jb, "semantic-json"
    except Exception as e:
        return False, f"json-error: {e}"

def compare_csv(a: Path, b: Path) -> tuple[bool, str]:
    try:
        da = pd.read_csv(a, encoding="utf-8-sig")
        db = pd.read_csv(b, encoding="utf-8-sig")
        assert_frame_equal(da, db, check_dtype=False, check_like=False)
        return True, "dataframe-equal"
    except Exception as e:
        return False, f"csv-diff: {e}"

def main() -> int:
    ap = argparse.ArgumentParser(description="Compare frozen model assets using a manifest.")
    ap.add_argument("--baseline-root", required=True, type=Path)
    ap.add_argument("--candidate-root", required=True, type=Path)
    ap.add_argument("--manifest", required=True, type=Path)
    ap.add_argument("--output-json", type=Path)
    args = ap.parse_args()

    spec = json.loads(args.manifest.read_text(encoding="utf-8-sig"))
    assets = spec.get("assets", [])
    if not assets:
        raise SystemExit("Manifest contains no assets.")

    results = []
    failed = False
    for item in assets:
        name = item.get("name") or item.get("baseline") or item.get("path")
        base_rel = item.get("baseline", item.get("path"))
        cand_rel = item.get("candidate", item.get("path"))
        mode = item.get("mode", "sha256").lower()
        if not base_rel or not cand_rel:
            results.append({"name": name, "status": "FAIL", "reason": "missing path in manifest"})
            failed = True
            continue
        a = args.baseline_root / base_rel
        b = args.candidate_root / cand_rel
        if not a.is_file() or not b.is_file():
            results.append({
                "name": name,
                "status": "FAIL",
                "reason": "missing file",
                "baseline": str(a),
                "candidate": str(b),
            })
            failed = True
            continue

        if mode == "sha256":
            ha, hb = sha256(a), sha256(b)
            ok = ha == hb
            detail = {"baseline_sha256": ha, "candidate_sha256": hb}
        elif mode == "json":
            ok, why = compare_json(a, b)
            detail = {"comparison": why}
        elif mode == "csv":
            ok, why = compare_csv(a, b)
            detail = {"comparison": why}
        elif mode == "text":
            ta = a.read_text(encoding="utf-8-sig").replace("\r\n", "\n")
            tb = b.read_text(encoding="utf-8-sig").replace("\r\n", "\n")
            ok = ta == tb
            detail = {"comparison": "normalized-text"}
        else:
            ok = False
            detail = {"comparison": f"unsupported mode: {mode}"}

        if not ok:
            failed = True
        results.append({
            "name": name,
            "mode": mode,
            "status": "PASS" if ok else "FAIL",
            "baseline": str(a),
            "candidate": str(b),
            **detail,
        })

    report = {
        "status": "FAIL" if failed else "PASS",
        "asset_count": len(results),
        "results": results,
    }
    print(json.dumps(report, ensure_ascii=False, indent=2))
    if args.output_json:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    return 1 if failed else 0

if __name__ == "__main__":
    raise SystemExit(main())
