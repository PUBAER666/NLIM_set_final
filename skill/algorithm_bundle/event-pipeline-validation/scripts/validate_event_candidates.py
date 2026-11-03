#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path
import pandas as pd
import numpy as np

ALIASES = {
    "recording": ["Recording_File", "Recording", "File", "Source_File"],
    "time": ["Event_Time_S", "Event_Time", "Time_S", "Time"],
    "dp": ["Delta_P_W", "DeltaP_W", "Delta_P", "dP_W"],
    "di": ["Delta_Irms_A", "DeltaI_A", "Delta_I_A", "dI_A"],
}

def pick(df, preferred, kind):
    if preferred and preferred in df.columns:
        return preferred
    lower = {str(c).lower(): c for c in df.columns}
    for c in ALIASES[kind]:
        if c in df.columns:
            return c
        if c.lower() in lower:
            return lower[c.lower()]
    raise SystemExit(f"Cannot find {kind} column. Tried {ALIASES[kind]}")

def main() -> int:
    ap = argparse.ArgumentParser(description="Manifest-level helper for event candidate triage; GT is ignored.")
    ap.add_argument("--input", required=True, type=Path)
    ap.add_argument("--output", required=True, type=Path)
    ap.add_argument("--recording-col", default="")
    ap.add_argument("--time-col", default="")
    ap.add_argument("--dp-col", default="")
    ap.add_argument("--di-col", default="")
    ap.add_argument("--micro-dp", type=float, default=8.0)
    ap.add_argument("--micro-di", type=float, default=0.04)
    ap.add_argument("--duplicate-window", type=float, default=0.16)
    ap.add_argument("--episode-min-gap", type=float, default=0.08)
    ap.add_argument("--episode-max-gap", type=float, default=0.60)
    args = ap.parse_args()

    df = pd.read_csv(args.input, encoding="utf-8-sig")
    if df.empty:
        raise SystemExit("Input event table is empty.")

    rc = pick(df, args.recording_col, "recording")
    tc = pick(df, args.time_col, "time")
    pc = pick(df, args.dp_col, "dp")
    ic = pick(df, args.di_col, "di")

    out = df.copy()
    out["_event_time"] = pd.to_numeric(out[tc], errors="raise")
    out["_dp"] = pd.to_numeric(out[pc], errors="raise")
    out["_di"] = pd.to_numeric(out[ic], errors="raise")
    if not np.isfinite(out[["_event_time", "_dp", "_di"]].to_numpy(dtype=float)).all():
        raise SystemExit("Event table contains NaN/Inf in time/delta columns.")

    out["Decision"] = "KEEP"
    out["Review_Flags"] = ""

    # Micro-change is the only unconditional auto-reject in this generic helper.
    micro = (out["_dp"].abs() < args.micro_dp) & (out["_di"].abs() < args.micro_di)
    out.loc[micro, "Decision"] = "REJECT_MICRO"
    out.loc[micro, "Review_Flags"] = "MICRO_CHANGE"

    # Nearby candidates: mark the weaker of two; do not use any label/GT column.
    for rec, idxs in out.groupby(rc, sort=False).groups.items():
        ordered = list(out.loc[idxs].sort_values("_event_time").index)
        for i in range(1, len(ordered)):
            a, b = ordered[i-1], ordered[i]
            gap = float(out.at[b, "_event_time"] - out.at[a, "_event_time"])
            if gap < 0:
                continue
            if gap <= args.duplicate_window:
                strength_a = abs(float(out.at[a, "_dp"])) + 100.0 * abs(float(out.at[a, "_di"]))
                strength_b = abs(float(out.at[b, "_dp"])) + 100.0 * abs(float(out.at[b, "_di"]))
                weaker = a if strength_a <= strength_b else b
                if out.at[weaker, "Decision"] == "KEEP":
                    out.at[weaker, "Decision"] = "REJECT_DUPLICATE"
                flag = out.at[weaker, "Review_Flags"]
                out.at[weaker, "Review_Flags"] = (flag + ";" if flag else "") + "NEAR_DUPLICATE"
            elif args.episode_min_gap <= gap <= args.episode_max_gap:
                for j in (a, b):
                    flag = out.at[j, "Review_Flags"]
                    if "CLOSE_EPISODE" not in flag:
                        out.at[j, "Review_Flags"] = (flag + ";" if flag else "") + "CLOSE_EPISODE"

    out.drop(columns=["_event_time", "_dp", "_di"], inplace=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    out.to_csv(args.output, index=False, encoding="utf-8-sig")

    summary = out["Decision"].value_counts().to_dict()
    print("Decision counts:", summary)
    print(f"Saved: {args.output}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
