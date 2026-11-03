# -*- coding: utf-8 -*-
"""
erc_check.py — 对生成的 KiCad v5 原理图做 ERC-lite:
  1. 重新解析 .sch + -cache.lib (不信任生成器内存状态)
  2. 并查集重建网络 (导线端点共享 / 端点与图元落在线段中点 / 同名标签合并)
  3. 校验: 期望网络成员、内部节点纯度、悬空引脚、NC 引脚隔离、网络名冲突
输出: 控制台摘要 + nilm_afe_v0_netlist_check.txt
"""
import os, sys, collections

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT  = os.path.join(BASE, "kicad")
PROJ = "nilm_afe_v0"
LIBN = PROJ + "-cache"

# ---------- 解析 cache.lib: 符号 -> {pin_num: (x, y, unit)} ----------
def parse_lib(path):
    syms, cur, in_draw = {}, None, False
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("DEF "):
                cur = line.split()[1]
                syms[cur] = {"pins": {}, "power": line.rstrip().endswith(" P")}
            elif line == "DRAW":
                in_draw = True
            elif line == "ENDDRAW":
                in_draw = False
            elif in_draw and line.startswith("X "):
                t = line.split()
                # X name num x y len orient nsz nusz unit convert etype ...
                num, x, y, unit = t[2], int(t[3]), int(t[4]), int(t[9])
                syms[cur]["pins"][num] = (x, y, unit)
            elif line.startswith("ENDDEF"):
                cur = None
    return syms

# ---------- 解析 .sch ----------
def parse_sch(path):
    comps, wires, dots, labels, ncs = [], [], [], [], []
    with open(path, encoding="utf-8") as f:
        lines = [l.rstrip("\n") for l in f]
    i = 0
    while i < len(lines):
        l = lines[i]
        if l == "$Comp":
            blk = []
            while lines[i] != "$EndComp":
                blk.append(lines[i]); i += 1
            ref = sym = x = y = mat = None
            unit = 1
            for b in blk:
                if b.startswith("L "):
                    t = b.split()
                    sym = t[1].split(":")[-1]; ref = t[2]
                elif b.startswith("U "):
                    unit = int(b.split()[1])
                elif b.startswith("P "):
                    t = b.split(); x, y = int(t[1]), int(t[2])
                elif b.startswith("\t") and len(b.split()) == 4:
                    mat = tuple(int(v) for v in b.split())
            comps.append(dict(ref=ref, sym=sym, x=x, y=y, mat=mat, unit=unit))
        elif l.startswith("Wire "):
            i += 1
            t = lines[i].split()
            wires.append(tuple(int(v) for v in t[:4]))
        elif l.startswith("Connection ~"):
            t = l.split(); dots.append((int(t[2]), int(t[3])))
        elif l.startswith("Text Label"):
            t = l.split()
            labels.append((lines[i + 1].strip(), int(t[2]), int(t[3])))
            i += 1
        elif l.startswith("NoConn ~"):
            t = l.split(); ncs.append((int(t[2]), int(t[3])))
        i += 1
    return comps, wires, dots, labels, ncs

# ---------- 并查集 ----------
class UF:
    def __init__(self): self.p = {}
    def find(self, a):
        self.p.setdefault(a, a)
        while self.p[a] != a:
            self.p[a] = self.p[self.p[a]]; a = self.p[a]
        return a
    def union(self, a, b):
        ra, rb = self.find(a), self.find(b)
        if ra != rb: self.p[rb] = ra

def on_seg(px, py, x1, y1, x2, y2):
    if x1 == x2 and px == x1:
        return min(y1, y2) <= py <= max(y1, y2)
    if y1 == y2 and py == y1:
        return min(x1, x2) <= px <= max(x1, x2)
    return False

syms = parse_lib(os.path.join(OUT, LIBN + ".lib"))
comps, wires, dots, labels, ncs = parse_sch(os.path.join(OUT, PROJ + ".sch"))

def pwr_net(sym):
    return sym.replace("power_", "")

uf = UF()
pin_pts = {}   # (ref, pinnum) -> (x, y)
pwr_pts = []   # (net, x, y)

for c in comps:
    s = syms[c["sym"]]
    for num, (px, py, punit) in s["pins"].items():
        if punit != c["unit"]:
            continue
        gx = c["mat"][0] * px + c["mat"][1] * py + c["x"]
        gy = c["mat"][2] * px + c["mat"][3] * py + c["y"]
        if s["power"]:
            pwr_pts.append((pwr_net(c["sym"]), gx, gy))
        else:
            key = (c["ref"], num)
            if key not in pin_pts:
                pin_pts[key] = (gx, gy)

# 1) 导线两端
for (x1, y1, x2, y2) in wires:
    uf.union((x1, y1), (x2, y2))

# 2) 导线端点落在其它线段中点 (T 型搭接)
ends = [(x1, y1) for (x1, y1, x2, y2) in wires] + [(x2, y2) for (x1, y1, x2, y2) in wires]
for (ex, ey) in ends:
    for (x1, y1, x2, y2) in wires:
        if (ex, ey) != (x1, y1) and (ex, ey) != (x2, y2) and on_seg(ex, ey, x1, y1, x2, y2):
            uf.union((ex, ey), (x1, y1))

# 3) 引脚/标签/电源/连接点 落在线上 -> 连通
attach = ([(px, py) for (px, py) in pin_pts.values()]
          + [(x, y) for (_, x, y) in labels]
          + [(x, y) for (_, x, y) in pwr_pts]
          + list(dots))
for (ax, ay) in attach:
    for (x1, y1, x2, y2) in wires:
        if on_seg(ax, ay, x1, y1, x2, y2):
            uf.union((ax, ay), (x1, y1))

# 4) 同名网络合并 (同页标签语义) —— 先查冲突再合并
root_names = collections.defaultdict(set)
for (net, x, y) in labels:
    root_names[uf.find((x, y))].add(net)
for (net, x, y) in pwr_pts:
    root_names[uf.find((x, y))].add(net)

errors, warns = [], []
for root, names in root_names.items():
    if len(names) > 1:
        errors.append("网络名冲突: %s 短接为同一电气网络" % sorted(names))

name_root = {}
for root, names in root_names.items():
    for n in names:
        if n in name_root:
            uf.union(root, name_root[n])
        else:
            name_root[n] = root

# 成员汇总
net_pins = collections.defaultdict(set)
for (ref, num), (px, py) in pin_pts.items():
    net_pins[uf.find((px, py))].add("%s.%s" % (ref, num))
root_pwr = collections.defaultdict(set)
for (net, x, y) in pwr_pts:
    root_pwr[uf.find((x, y))].add(net)
root_lab = collections.defaultdict(set)
for (net, x, y) in labels:
    root_lab[uf.find((x, y))].add(net)
root_wire_cnt = collections.Counter()
for (x1, y1, x2, y2) in wires:
    root_wire_cnt[uf.find((x1, y1))] += 1

def net_name(root):
    names = root_lab.get(root, set()) | root_pwr.get(root, set())
    if names:
        return sorted(names)[0]
    members = sorted(net_pins.get(root, []))
    return "Net-(%s)" % (members[0].replace(".", "-Pad") if members else "?")

by_name = collections.defaultdict(set)
for root, pins in net_pins.items():
    by_name[net_name(root)].update(pins)

# ---------- 校验 ----------
EXPECT = {
    "I_IN":    {"J1.1", "R1.1", "TP1.1"},
    "VBIAS_I": {"U1.3", "RV2.2", "C1.1", "C2.1", "TP2.1"},
    "I_OUT":   {"R4.2", "C4.1", "TP4.1", "J4.1"},
    "V_IN":    {"J2.1", "R5.1", "TP5.1"},
    "VBIAS_V": {"U1.5", "RV4.2", "C5.1", "C6.1", "TP7.1"},
    "V_OUT":   {"R8.2", "C8.1", "TP6.1", "J4.3"},
    "+5V":     {"J3.1", "C9.1", "U3.3", "TP9.1"},
    "+3V3":    {"U1.8", "U2.8", "U3.2", "D2.2", "D4.2", "R10.2", "R11.2",
                "C11.1", "C3.1", "C7.1", "C10.1", "R9.2", "J4.5", "TP8.1"},
    "GND":     {"U1.4", "U2.4", "U3.1", "J1.2", "J2.2", "J3.2", "J4.2", "J4.4",
                "J4.6", "D1.1", "D3.1", "D5.2", "C1.2", "C2.2", "C4.2", "C5.2",
                "C6.2", "C8.2", "C9.2", "C10.2", "C11.2", "C3.2", "C7.2",
                "RV2.3", "RV4.3", "TP3.1", "R12.2"},
}
for net, want in EXPECT.items():
    got = by_name.get(net, set())
    miss, extra = want - got, got - want
    if miss:
        errors.append("网络 %s 缺少成员: %s (实际: %s)" % (net, sorted(miss), sorted(got)))
    if extra:
        warns.append("网络 %s 多出成员: %s" % (net, sorted(extra)))

INTERNAL = [
    ("J1_RING", {"J1.3", "R12.1"}),
    ("I_PROT", {"R1.2", "D1.2", "D2.1", "R2.1"}),
    ("I_INV",  {"R2.2", "U1.2", "RV1.1"}),
    ("I_FB",   {"U1.1", "RV1.2", "RV1.3", "R3.1"}),
    ("I_MID",  {"R3.2", "R4.1"}),
    ("V_PROT", {"R5.2", "D3.2", "D4.1", "R6.1"}),
    ("V_INV",  {"R6.2", "U1.6", "RV3.1"}),
    ("V_FB",   {"U1.7", "RV3.2", "RV3.3", "R7.1"}),
    ("V_MID",  {"R7.2", "R8.1"}),
    ("BIAS_I_DIV", {"R10.1", "RV2.1"}),
    ("BIAS_V_DIV", {"R11.1", "RV4.1"}),
    ("LED_N",  {"R9.1", "D5.1"}),
]
for tag, want in INTERNAL:
    for root, pins in net_pins.items():
        if want <= pins:
            if pins != want:
                errors.append("内部节点 %s 混入额外引脚: %s" % (tag, sorted(pins - want)))
            break
    else:
        errors.append("内部节点 %s 不完整, 期望 %s" % (tag, sorted(want)))

# 悬空: 单引脚 + 无电源/标签 + 无导线
nc_set = set(ncs)
for (ref, num), (px, py) in sorted(pin_pts.items()):
    if (px, py) in nc_set:
        continue
    root = uf.find((px, py))
    if (len(net_pins[root]) == 1 and not root_pwr.get(root)
            and not root_lab.get(root) and root_wire_cnt.get(root, 0) == 0):
        errors.append("悬空引脚: %s.%s @ (%d,%d)" % (ref, num, px, py))

# NC 引脚必须孤立
for (nx, ny) in nc_set:
    root = uf.find((nx, ny))
    members = net_pins.get(root, set())
    if len(members) > 1 or root_pwr.get(root) or root_lab.get(root) or root_wire_cnt.get(root, 0):
        errors.append("NC 引脚未隔离 @ (%d,%d): net=%s pins=%s"
                      % (nx, ny, net_name(root), sorted(members)))

# ---------- 报告 ----------
rep = []
rep.append("nilm_afe_v0 ERC-lite / netlist check  (2026-09-23)")
rep.append("=" * 64)
rep.append("components=%d wires=%d labels=%d pwr=%d dots=%d ncs=%d"
           % (len([c for c in comps if not c['ref'].startswith('#')]),
              len(wires), len(labels), len(pwr_pts), len(dots), len(ncs)))
rep.append("")
rep.append("== 网络表 (%d 个网络) ==" % len(net_pins))
for root in sorted(net_pins, key=lambda r: net_name(r)):
    rep.append("%-10s : %s" % (net_name(root), ", ".join(sorted(net_pins[root]))))
rep.append("")
rep.append("== 检查 ==")
rep.append("ERRORS: %d" % len(errors))
rep += ["  [E] " + e for e in errors]
rep.append("WARNINGS: %d" % len(warns))
rep += ["  [W] " + w for w in warns]

txt = "\n".join(rep)
with open(os.path.join(BASE, PROJ + "_netlist_check.txt"), "w", encoding="utf-8", newline="\n") as f:
    f.write(txt + "\n")
print(txt)
sys.exit(1 if errors else 0)
