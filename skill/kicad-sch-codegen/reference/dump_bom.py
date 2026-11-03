"""从 .sch 提取全部元件 位号/符号/取值/封装, 供 BOM 交叉核对。"""
import re, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCH = os.path.join(HERE, "..", "kicad", "nilm_afe_v0.sch")
txt = open(SCH, encoding="utf-8").read()

comps = {}
for m in re.finditer(r"\$Comp\nL nilm_afe_v0-cache:(\S+) (\S+)\n(.*?)\$EndComp", txt, re.S):
    sym, ref, body = m.groups()
    if ref.startswith(("TP", "#")) or "power" in sym:
        continue
    if ref in comps:          # 多单元器件只记一次
        continue
    val = re.search(r'^F 1 "(.*?)"', body, re.M)
    fp = re.search(r'^F 2 "(.*?)"', body, re.M)
    comps[ref] = (sym, val.group(1) if val else "?", fp.group(1).split(":")[-1] if fp else "?")

def key(r):
    m = re.match(r"([A-Z]+)(\d+)", r)
    return (m.group(1), int(m.group(2)))

for ref in sorted(comps, key=key):
    s, v, f = comps[ref]
    print("%-4s %-12s %-24s %s" % (ref, s, v, f))
print("total refs:", len(comps))
