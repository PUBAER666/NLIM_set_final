# -*- coding: utf-8 -*-
"""
audit_pcb.py — AFE V0 · PCB 只读审计
    1. 解析当前原理图 .sch (+cache.lib) → 并查集重建网络表 (不信任中间产物)
    2. 解析 PCB .kicad_pcb → 封装/焊盘/网络/几何
    3. 三方比对:
       a) 原理图 ↔ PCB        网络表逐焊盘比对
       b) 生成器意图 ↔ PCB    封装名 / 坐标比对 (识别手改)
       c) 几何与可制造性      外框 / 碰撞 / 禁布 / 走线铺铜状态
"""
import os, re, sys, collections

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KICAD = os.path.join(BASE, "kicad")
PROJ, LIBN = "nilm_afe_v0", "nilm_afe_v0-cache"
PCB = os.path.join(KICAD, "nilm_afe_pcb.kicad_pcb")
GEN = os.path.join(BASE, "tools", "gen_pcb.py")
W, H = 100.0, 60.0

# 命令行可覆盖路径 (默认 = 本项目); 换项目/换板时不用改代码
import argparse, time
_ap = argparse.ArgumentParser(description="PCB ↔ 原理图 只读交叉审计")
_ap.add_argument("--pcb", default=PCB, help="PCB 文件 .kicad_pcb")
_ap.add_argument("--sch", default=os.path.join(KICAD, PROJ + ".sch"), help="原理图 .sch")
_ap.add_argument("--lib", default=os.path.join(KICAD, LIBN + ".lib"), help="原理图符号库 -cache.lib")
_ap.add_argument("--gen", default=GEN, help="PCB 生成器脚本 (读其 PLACE 表以识别手改; 可不存在)")
_ap.add_argument("--board-w", type=float, default=W, help="板宽 mm (几何检查用)")
_ap.add_argument("--board-h", type=float, default=H, help="板高 mm")
_ap.add_argument("--out", default=None,
                 help="原始报告输出路径 (默认 pcb_audit_raw_<时间戳>.txt, 不覆盖历史)")
_a = _ap.parse_args()
PCB, GEN, SCHP, LIBP = _a.pcb, _a.gen, _a.sch, _a.lib
W, H = _a.board_w, _a.board_h
OUTTXT = _a.out or os.path.join(
    BASE, "pcb_audit_raw_%s.txt" % time.strftime("%Y%m%d_%H%M%S"))

# 无参运行时若默认文件不存在 -> 明确报错退出, 而不是静默去读本示例项目的路径(会误判)
_missing = [p for p in (SCHP, LIBP, PCB) if not os.path.exists(p)]
if _missing:
    sys.stderr.write(
        "错误: 缺少必需输入文件, 无法审计:\n"
        + "".join("  - %s\n" % p for p in _missing)
        + "\n默认路径指向本示例项目 '%s'。换项目请显式传参:\n" % PROJ
        + "  python %s --pcb <x.kicad_pcb> --sch <x.sch> --lib <x-cache.lib>\n"
        % os.path.basename(sys.argv[0]))
    sys.exit(2)

out = []
def say(s=""):
    out.append(s)

# ────────────────────────────────── 1. 原理图 → 网络表
def parse_lib(path):
    syms, cur, in_draw = {}, None, False
    for line in open(path, encoding="utf-8"):
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
            syms[cur]["pins"][t[2]] = (int(t[3]), int(t[4]), int(t[9]))
    return syms

def parse_sch(path):
    comps, wires, labels, dots, ncs, noconn = [], [], [], [], [], []
    lines = [l.rstrip("\n") for l in open(path, encoding="utf-8")]
    i = 0
    while i < len(lines):
        l = lines[i]
        if l == "$Comp":
            blk = []
            while lines[i] != "$EndComp":
                blk.append(lines[i]); i += 1
            ref = sym = mat = None; x = y = 0; unit = 1; val = fp = None
            for b in blk:
                if b.startswith("L "):
                    t = b.split(); sym = t[1].split(":")[-1]; ref = t[2]
                elif b.startswith("U "):
                    unit = int(b.split()[1])
                elif b.startswith("P "):
                    t = b.split(); x, y = int(t[1]), int(t[2])
                elif b.startswith("F 1 "):
                    val = b.split('"')[1] if '"' in b else None
                elif b.startswith("F 2 "):
                    fp = b.split('"')[1] if '"' in b else None
                elif b.startswith("\t") and len(b.split()) == 4:
                    mat = tuple(int(v) for v in b.split())
            comps.append(dict(ref=ref, sym=sym, x=x, y=y, mat=mat, unit=unit, val=val, fp=fp))
        elif l.startswith("Wire "):
            i += 1
            t = lines[i].split()
            wires.append(tuple(int(v) for v in t[:4]))
        elif l.startswith("Connection ~"):
            t = l.split(); dots.append((int(t[2]), int(t[3])))
        elif l.startswith("Text Label"):
            t = l.split(); labels.append((lines[i + 1].strip(), int(t[2]), int(t[3]))); i += 1
        elif l.startswith("NoConn ~"):
            t = l.split(); ncs.append((int(t[2]), int(t[3])))
        elif l.startswith("Text GLabel") or l.startswith("Text HLabel"):
            t = l.split(); labels.append((lines[i + 1].strip(), int(t[2]), int(t[3]))); i += 1
        i += 1
    return comps, wires, labels, dots, ncs

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
    if x1 == x2 and px == x1: return min(y1, y2) <= py <= max(y1, y2)
    if y1 == y2 and py == y1: return min(x1, x2) <= px <= max(x1, x2)
    return False

syms = parse_lib(LIBP)
comps, wires, labels, dots, ncs = parse_sch(SCHP)

uf = UF()
pin_pts, pwr_pts = {}, []
for c in comps:
    s = syms[c["sym"]]
    for num, (px, py, punit) in s["pins"].items():
        if punit != c["unit"]: continue
        gx = c["mat"][0] * px + c["mat"][1] * py + c["x"]
        gy = c["mat"][2] * px + c["mat"][3] * py + c["y"]
        if s["power"]: pwr_pts.append((c["sym"].replace("power_", ""), gx, gy))
        else: pin_pts.setdefault((c["ref"], num), (gx, gy))

# 导线连通
for (x1, y1, x2, y2) in wires:
    uf.union((x1, y1), (x2, y2))
# 引脚 / 电源 / 标签 / 导线端点 四个点族都要并到导线上
# (漏"导线端点落在另一条导线中段"这条 T 型搭接规则 = 分组全错, 会报一堆假差异)
allpts = list(pin_pts.items()) \
    + [(("PWR", i), (x, y)) for i, (_, x, y) in enumerate(pwr_pts)] \
    + [(("LBL", i), (x, y)) for i, (_, x, y) in enumerate(labels)] \
    + [(("END", i), (x, y)) for i, (x, y, _, _) in enumerate(wires)] \
    + [(("END2", i), (x, y)) for i, (_, _, x, y) in enumerate(wires)]
for (x1, y1, x2, y2) in wires:
    for key, (px, py) in allpts:
        if on_seg(px, py, x1, y1, x2, y2):
            uf.union((px, py), (x1, y1))
lab_root = {}
for name, x, y in labels:
    r = uf.find((x, y))
    lab_root.setdefault(r, set()).add(name)
for name, x, y in pwr_pts:
    uf.union((x, y), (x, y))
    lab_root.setdefault(uf.find((x, y)), set()).add(name)

sch_net = collections.defaultdict(set)      # net -> {REF.pin}
net_of_pin = {}                             # REF.pin -> net
roots = {}
for (ref, num), (x, y) in pin_pts.items():
    roots.setdefault(uf.find((x, y)), set()).add(ref + "." + num)
for name, x, y in pwr_pts:
    roots.setdefault(uf.find((x, y)), set())
netnames = {}
for r, names in lab_root.items():
    netnames[r] = sorted(n for n in names if n != "+3V3" or True)[0] if names else None
for r, pins in roots.items():
    nm = None
    for cand in sorted(lab_root.get(r, [])):
        if cand: nm = cand; break
    if nm is None:
        nm = "Net-(%s)" % (sorted(pins)[0].replace(".", "-Pad") if pins else "?")
    sch_net[nm] |= pins
for r, pins in roots.items():
    nm = None
    for cand in sorted(lab_root.get(r, [])):
        if cand: nm = cand; break
    if nm is None:
        nm = "Net-(%s)" % (sorted(pins)[0].replace(".", "-Pad") if pins else "?")
    for p in pins:
        net_of_pin[p] = nm

sch_comps = {}
for c in comps:
    if c["sym"].startswith("power_"): continue
    sch_comps.setdefault(c["ref"], dict(val=c["val"], fp=c["fp"]))

# ────────────────────────────────── 2. PCB → 封装/焊盘
def sexpr(text):
    """极简 S 表达式解析 (KiCad pcb): 返回嵌套 list; 支持带引号字符串"""
    toks = re.findall(r'"(?:[^"\\]|\\.)*"|\(|\)|[^\s()]+', text)
    def parse(it):
        node = []
        for t in it:
            if t == "(":
                node.append(parse(it))
            elif t == ")":
                return node
            else:
                node.append(t[1:-1] if t.startswith('"') else t)
        return node
    it = iter(toks)
    for t in it:
        if t == "(":
            return parse(it)
    return []

pcb = sexpr(open(PCB, encoding="utf-8").read())
assert pcb[0] == "kicad_pcb"

pcb_nets = {}
pcb_mods, gr_lines, gr_texts, segments, vias, zones = [], [], [], [], [], []
for node in pcb[1:]:
    if not isinstance(node, list) or not node: continue
    k = node[0]
    if k == "net":
        pcb_nets[node[1]] = node[2]
    elif k == "module":
        mod = dict(lib=node[1], pads=[], ref=None, val=None, layer=None, at=None,
                   crtyd=[], silk=[], fabtext=[], attr=None)
        for f in node[2:]:
            if not isinstance(f, list) or not f: continue
            if f[0] == "layer": mod["layer"] = f[1]
            elif f[0] == "at": mod["at"] = tuple(float(v) for v in f[1:4])
            elif f[0] == "attr": mod["attr"] = " ".join(f[1:])
            elif f[0] == "fp_text" and f[1] in ("reference", "value"):
                txt = f[2]
                lay = next((g[1] for g in f if isinstance(g, list) and g[0] == "layer"), "?")
                hide = "hide" in f
                mod["ref" if f[1] == "reference" else "val"] = txt
                mod.setdefault("texts", []).append((f[1], txt, lay, hide))
            elif f[0] == "fp_line":
                g = {a[0]: a for a in f if isinstance(a, list) and a[0] in ("start", "end", "layer")}
                if "layer" in g and g["layer"][1] == "F.CrtYd":
                    mod["crtyd"].append((float(g["start"][1]), float(g["start"][2]),
                                         float(g["end"][1]), float(g["end"][2])))
                if "layer" in g and g["layer"][1] in ("F.SilkS", "B.SilkS"):
                    mod["silk"].append((float(g["start"][1]), float(g["start"][2]),
                                        float(g["end"][1]), float(g["end"][2])))
            elif f[0] == "pad":
                d = dict(num=f[1], type=f[2], shape=f[3])
                for a in f[4:]:
                    if not isinstance(a, list) or not a: continue
                    if a[0] == "at": d["at"] = (float(a[1]), float(a[2]))
                    elif a[0] == "size": d["size"] = (float(a[1]), float(a[2]))
                    elif a[0] == "drill":
                        # (drill 1.00) 圆孔 / (drill oval 1.50 0.50) 腰形槽
                        # ⚠ 2026-10-05 补：J1 换成真 PJ-320A 后焊盘是腰形槽，
                        #   旧解析器把 a[1]="oval" 当数字 → float() 直接崩。
                        #   不认的语法要**报出来**，不能默默当 0 继续跑。
                        if a[1] == "oval":
                            d["drill"] = None
                            d["slot"] = (float(a[2]), float(a[3]))
                        elif re.match(r"^[\d.]+$", str(a[1])):
                            d["drill"] = a[1]
                        else:
                            d["drill"] = None
                            d["slot_bad"] = " ".join(str(v) for v in a[1:])
                    elif a[0] == "net": d["net"] = a[2]
                mod["pads"].append(d)
        pcb_mods.append(mod)
    elif k == "gr_line":
        g = {a[0]: a for a in node[1:] if isinstance(a, list)}
        gr_lines.append((g["start"][1:3], g["end"][1:3],
                         g["layer"][1] if "layer" in g else "?"))
    elif k == "gr_text":
        txt = node[1]
        lay = next((a[1] for a in node[2:] if isinstance(a, list) and a[0] == "layer"), "?")
        pos = next((a[1:3] for a in node[2:] if isinstance(a, list) and a[0] == "at"), None)
        gr_texts.append((txt, lay, pos))
    elif k == "segment": segments.append(node)
    elif k == "via": vias.append(node)
    elif k == "zone": zones.append(node)

# ────────────────────────────────── 3a. 原理图 ↔ PCB 网络比对
pcb_net_of_pad = {}
pcb_net_members = collections.defaultdict(set)
for m in pcb_mods:
    ref = m["ref"]
    for p in m["pads"]:
        nm = p.get("net", "")
        pcb_net_of_pad[(ref, p["num"])] = nm
        if nm: pcb_net_members[nm].add(ref + "." + p["num"])

say("=" * 78)
say("  AFE V0 · PCB 只读审计   (PCB: %s)" % os.path.basename(PCB))
say("=" * 78)
say("")
say("【0】规模概览")
say("  原理图: %d 个元件实例 / %d 个位号 / %d 个网络 (%d 个有名字)"
    % (len(comps), len(sch_comps), len(sch_net), len([n for n in sch_net if not n.startswith("Net-")])))
say("  PCB   : %d 个封装 / %d 个焊盘 / %d 个网络声明 / 走线 %d / 过孔 %d / 铺铜 %d / 板框线 %d / 丝印文字 %d"
    % (len(pcb_mods), sum(len(m["pads"]) for m in pcb_mods), len(pcb_nets),
       len(segments), len(vias), len(zones), len(gr_lines), len(gr_texts)))
pk = collections.Counter(p["type"] for m in pcb_mods for p in m["pads"])
say("  焊盘类型: %s" % dict(pk))

# 位号集合
sch_refs = set(sch_comps)
pcb_refs = set(m["ref"] for m in pcb_mods)
say("")
say("【1】位号集合比对 (原理图 ↔ PCB)")
say("  仅原理图有: %s" % (sorted(sch_refs - pcb_refs) or "无"))
say("  仅 PCB 有  : %s" % (sorted(pcb_refs - sch_refs) or "无"))

# 数值比对
say("")
say("【2】元件数值比对 (原理图 F1 ↔ PCB value)")
val_diff = []
for r in sorted(sch_refs & pcb_refs):
    sv = (sch_comps[r]["val"] or "").strip()
    pm = next(m for m in pcb_mods if m["ref"] == r)
    pv = (pm["val"] or "").strip()
    # 归一化: 大小写 / 下划线≡空格 / u≡µ / uF≡µF, 只留实质差异
    def norm(s):
        s = s.lower().replace("_", " ").replace("µ", "u").replace("μ", "u")
        s = re.sub(r"[()\[\]]", " ", s)
        return " ".join(s.split())
    if norm(sv) != norm(pv) and sv and pv:
        val_diff.append((r, sv, pv))
for r, sv, pv in val_diff:
    say("  ⚠ %-4s 原理图=%-14s PCB=%s" % (r, sv, pv))
if not val_diff:
    say("  ✓ 全部一致")

# ────────────────────────────────── 3a'. 四方比对第 ④ 项: 文本 ↔ 网络
# 网络表比对【3】查不出"丝印标错": 焊盘网络可以 100% 正确, 只有板上文字是错的。
# 这类错最隐蔽 —— 调试时按丝印去探, 会探到错的节点上。必须单独查。
say("")
say("【2b】丝印 / value 文本 ↔ 焊盘网络   (四方比对第 ④ 项)")
ALLNETS = set(n for n in pcb_nets.values() if n) | set(sch_net)
SILK_ALIAS = {                     # 丝印上的短名 -> 正式网络名 (纯命名习惯, 不算错)
    "I_VB": "VBIAS_I", "V_VB": "VBIAS_V", "3V3": "+3V3", "5V": "+5V",
}

def _ckey(s):
    """只做大小写归一: 下划线/空格**视为有意义**
    (丝印 'V IN' 是分区标题散文, 不等同于网络 'V_IN' —— 归一化过宽会造假阳性)"""
    return s.strip().upper()

def as_netname(txt):
    """文本本身是 / 可映射为 已知网络名 -> 返回网络名; 否则 None (不是网络名就不判)"""
    t = (txt or "").strip()
    if not t: return None
    if t in ALLNETS: return t
    k = _ckey(t)
    for n in ALLNETS:
        if _ckey(n) == k: return n
    for al, real in SILK_ALIAS.items():
        if _ckey(al) == k and real in ALLNETS: return real
    return None

def mod_nets(m):
    return set(p.get("net", "") for p in m["pads"] if p.get("net"))

# (a) 元件自带的 value 文本若"长得像网络名", 就必须属于本元件焊盘网络
bad_val = []
for m in pcb_mods:
    nn = as_netname(m.get("val"))
    if nn and nn not in mod_nets(m):
        bad_val.append((m["ref"], m["val"], nn, sorted(mod_nets(m))))

# (b) 板级功能丝印文字: 必须落在"它所指网络"的焊盘附近
NEAR_R = 8.0                       # mm; 超出此距离视为分区标题/图例, 不参与判定
board_silk = []
for (txt, lay, pos) in gr_texts:
    if lay not in ("F.SilkS", "B.SilkS") or pos is None: continue
    t = txt.strip()
    if len(t) > 18 or "=" in t: continue        # 图例行(如 "J4 1=I_OUT ...")
    nn = as_netname(t)
    if nn is None: continue                     # 不像网络名(板名/料值) -> 不判
    board_silk.append((t, nn, float(pos[0]), float(pos[1])))

all_pads = [(m["ref"], p["num"], p.get("net", ""),
             m["at"][0] + p["at"][0], m["at"][1] + p["at"][1])
            for m in pcb_mods if m["at"] for p in m["pads"] if "at" in p]

good_silk, bad_silk, skipped = [], [], []
for (t, nn, tx, ty) in board_silk:
    near = []
    for (ref, num, net, px, py) in all_pads:
        d = ((px - tx) ** 2 + (py - ty) ** 2) ** 0.5
        if d <= NEAR_R: near.append((d, ref, num, net))
    if not near: skipped.append((t, nn)); continue    # 附近无焊盘 -> 分区标题
    near.sort()
    if any(net == nn for _, _, _, net in near):
        good_silk.append((t, nn, "%s.%s" % (near[0][1], near[0][2]), round(near[0][0], 1)))
    else:
        bad_silk.append((t, nn, "%s.%s" % (near[0][1], near[0][2]),
                         round(near[0][0], 1), near[0][3]))

say("  (a) 元件 value 文本形如网络名却不属于本元件: %d 处" % len(bad_val))
for r, v, nn, nets in bad_val:
    say("      ⚠ %-4s value=%-10s 指向 %s, 但其焊盘在 %s" % (r, v, nn, nets))
if not bad_val: say("      ✓ 无")
say("  (b) 板级功能丝印 %d 条(可判定) → 位置自证:" % len(board_silk))
for t, nn, pad, d in good_silk:
    say("      ✓ %-8s (= %-9s) 最近焊盘 %-8s %.1fmm" % (t, nn, pad, d))
for t, nn, pad, d, got in bad_silk:
    say("      ⚠ %-8s (= %-9s) 但最近焊盘 %-8s 在 %s 上 (%.1fmm)" % (t, nn, pad, got, d))
if skipped:
    say("      · 距任何焊盘 >%.0fmm, 按分区标题忽略: %s" % (NEAR_R, [t for t, _ in skipped]))
n_silk_bad = len(bad_silk) + len(bad_val)
say("  ▶ 第④项结论: %s"
    % ("✓ 文本标注与焊盘网络全部一致" if not n_silk_bad
       else "⚠ %d 处「文字说的」与「实际连的」不符 —— 网络表比对查不出此类" % n_silk_bad))

# 网络成员比对
say("")
say("【3】网络表比对 (原理图引脚 ↔ PCB 焊盘)")
name_map = {}
for n in pcb_nets.values():
    name_map[n] = n
mism, only_sch, only_pcb = [], [], []
for nm in sorted(set(list(sch_net) + list(pcb_net_members))):
    a = set(sch_net.get(nm, set()))
    b = set(pcb_net_members.get(nm, set()))
    if a == b: continue
    if a - b: only_sch.append((nm, sorted(a - b)))
    if b - a: only_pcb.append((nm, sorted(b - a)))
say("  原理图有该网络成员而 PCB 未接: %d 处" % sum(len(v) for _, v in only_sch))
for nm, mem in only_sch:
    say("     %-22s 缺: %s" % (nm, ", ".join(mem)))
say("  PCB 有该网络成员而原理图没有: %d 处" % sum(len(v) for _, v in only_pcb))
for nm, mem in only_pcb:
    say("     %-22s 多: %s" % (nm, ", ".join(mem)))

# 焊盘级未连接 / 空网络
say("")
say("【4】焊盘级网络归属 (逐焊盘)")
# ⚠ 2026-10-05 修两个错：
#   (1) 元组里写的是 r，但循环变量是 m —— r 是上面某个推导式泄漏的残留值，
#       于是把 J1 的两个非金属化定位孔报成了 "J4"。**位号报错比不报还坏**：
#       会把人引到一块完全正常的元件上去查。
#   (2) 非金属化定位孔 (np_thru_hole) 本来就**不该有网络**，不是缺陷。
#       不排除掉的话每次审计都固定报两条 → 人们学会无视这条检查。
noname = [(m["ref"], p["num"]) for m in pcb_mods for p in m["pads"]
          if not p.get("net") and not (m["ref"] or "").startswith("H")
          and p.get("type") != "np_thru_hole"]
if noname: say("  ⚠ 无网络焊盘: %s" % noname)
else: say("  ✓ 所有元件焊盘都有网络名")
# ⚠ 必须与上面"无网络焊盘"用**同一套**排除标准（H* 安装孔 / 非金属化定位孔）。
#   2026-10-06 独立复核指出：这两条规则标准不一致 —— 一旦拿本规则去卡**实际 Gerber
#   的网表**，会固定误报 H1~H4 与 J1 的机械脚；固定误报会让人学会无视这条检查。
_mech_keys = {"%s.%s" % (m["ref"], p["num"]) for m in pcb_mods for p in m["pads"]
              if (m["ref"] or "").startswith("H") or p.get("type") == "np_thru_hole"}
one_pin = {n: v for n, v in pcb_net_members.items()
           if len(v) == 1 and not v <= _mech_keys}
if one_pin: say("  ⚠ 单端网络(只有 1 个焊盘, 等于悬空): %s" % one_pin)
else: say("  ✓ 无单端网络")

# ────────────────────────────────── 3b. 生成器意图 ↔ PCB (识别手改)
say("")
say("【5】与生成器意图比对 (识别手改/挪动)")
gen_txt = open(GEN, encoding="utf-8").read() if os.path.exists(GEN) else ""
m = re.search(r"PLACE\s*=\s*\[(.*?)\n\]", gen_txt, re.S)
gen_place = {}
if m:
    for line in m.group(1).splitlines():
        t = re.match(r'\s*\("([^"]+)",\s*"([^"]+)",\s*([\d.]+),\s*([\d.]+),\s*"([^"]*)"\)', line)
        if t:
            gen_place[t.group(1)] = dict(fp=t.group(2), x=float(t.group(3)), y=float(t.group(4)), v=t.group(5))
fpdefs = dict(re.findall(r'FP\("(\w+)",\s*"([^"]+)"', gen_txt))
fpdefs["M3"] = "MountingHole:MountingHole_3.2mm_M3"
m3 = re.search(r"M3_POS\s*=\s*\[(.*?)\]", gen_txt, re.S)
m3_pos = [(float(a), float(b)) for a, b in re.findall(r"\(([\d.]+),\s*([\d.]+)\)", m3.group(1))] if m3 else []

say("  生成器放置 %d 个元件 + %d 个安装孔 = %d, PCB 实际 %d 个封装"
    % (len(gen_place), len(m3_pos), len(gen_place) + len(m3_pos), len(pcb_mods)))

moved, fp_changed, extra, missing_gen = [], [], [], []
for ref, g in sorted(gen_place.items()):
    mods = [m for m in pcb_mods if m["ref"] == ref]
    if not mods:
        missing_gen.append(ref); continue
    pm = mods[0]
    exp_lib = fpdefs.get(g["fp"], "")
    if pm["lib"] != exp_lib:
        fp_changed.append((ref, exp_lib, pm["lib"]))
    ex, ey = g["x"], H - g["y"]
    if pm["at"] and (abs(pm["at"][0] - ex) > 0.01 or abs(pm["at"][1] - ey) > 0.01):
        moved.append((ref, (ex, ey), (pm["at"][0], pm["at"][1])))
for ref in sorted(pcb_refs - set(gen_place) - {"H%d" % (i + 1) for i in range(len(m3_pos))}):
    extra.append(ref)
say("  改装(封装被换)  : %s" % ("无" if not fp_changed else ""))
for r, e, a in fp_changed:
    say("     ⚠ %-4s 预期 %s\n             实际 %s" % (r, e, a))
say("  挪位            : %s" % ("无" if not moved else ""))
for r, e, a in moved:
    say("     · %-4s (%.1f, %.1f) → (%.1f, %.1f)" % (r, e[0], e[1], a[0], a[1]))
say("  生成器没放而 PCB 有: %s" % (extra or "无"))
say("  生成器有而 PCB 没放: %s" % (missing_gen or "无"))

# ────────────────────────────────── 3c. 几何 & 可制造性
say("")
say("【6】几何 / 可制造性")
edge = [(float(a[0]), float(a[1]), float(b[0]), float(b[1])) for a, b, lay in gr_lines if lay == "Edge.Cuts"]
if edge:
    xs = [p for l in edge for p in (l[0], l[2])]
    ys = [p for l in edge for p in (l[1], l[3])]
    say("  板框: %d 段直线, 范围 x %.1f~%.1f, y %.1f~%.1f (%.1f×%.1f mm)"
        % (len(edge), min(xs), max(xs), min(ys), max(ys), max(xs) - min(xs), max(ys) - min(ys)))
    # 闭合性: 每条边的端点应至少被两条边共享
    import collections as C
    cnt = C.Counter()
    for l in edge:
        cnt[(round(l[0], 2), round(l[1], 2))] += 1
        cnt[(round(l[2], 2), round(l[3], 2))] += 1
    openpts = [p for p, c in cnt.items() if c == 1]
    say("  板框闭合: %s" % ("✓ 闭合" if not openpts else "✗ 有 %d 个悬空端点 %s" % (len(openpts), openpts)))
else:
    say("  ✗ 没有 Edge.Cuts 板框线")

# 封装包围盒(用焊盘+ courty) 碰撞 & 越界
def mod_bbox(m):
    pts = []
    for (x1, y1, x2, y2) in m["crtyd"]:
        pts += [(x1, y1), (x2, y2)]
    if not pts:
        for p in m["pads"]:
            if "at" in p: pts.append(p["at"])
    if not pts: return None
    ax, ay = m["at"][0], m["at"][1]
    xs = [ax + p[0] for p in pts]; ys = [ay + p[1] for p in pts]
    return (min(xs), min(ys), max(xs), max(ys))

ov = []
boxes = {m["ref"]: mod_bbox(m) for m in pcb_mods}
refs = sorted(boxes)
for i in range(len(refs)):
    for j in range(i + 1, len(refs)):
        a, b = boxes[refs[i]], boxes[refs[j]]
        if not a or not b: continue
        ix = min(a[2], b[2]) - max(a[0], b[0])
        iy = min(a[3], b[3]) - max(a[1], b[1])
        if ix > 0.05 and iy > 0.05:
            ov.append((refs[i], refs[j], round(ix, 2), round(iy, 2)))
say("  元件外框重叠: %s" % ("✓ 无" if not ov else "%d 对" % len(ov)))
for a, b, ix, iy in ov:
    say("     ⚠ %s ↔ %s  重叠 %.2f × %.2f mm" % (a, b, ix, iy))

oob, near_edge = [], []
for r, bb in boxes.items():
    if not bb: continue
    if bb[0] < 0 or bb[1] < 0 or bb[2] > W or bb[3] > H:
        oob.append((r, tuple(round(v, 1) for v in bb)))
    elif min(bb[0], bb[1], W - bb[2], H - bb[3]) < 2.5:
        near_edge.append((r, round(min(bb[0], bb[1], W - bb[2], H - bb[3]), 2)))
say("  越出板框: %s" % (oob or "✓ 无"))
say("  距板边 <2.5mm: %s" % (near_edge or "✓ 无"))

m3hits = []
for r, bb in boxes.items():
    if not bb or r.startswith("H"): continue
    for i, (hx, hy) in enumerate(m3_pos):
        cx = max(bb[0], min(hx, bb[2])); cy = max(bb[1], min(hy, bb[3]))
        d = ((cx - hx) ** 2 + (cy - hy) ** 2) ** 0.5
        if d < 2.8:
            m3hits.append((r, i + 1, round(d, 2)))
say("  压安装孔禁布区(<2.8mm): %s" % (m3hits or "✓ 无"))

# 测试点 / 丝印
tp = sorted(r for r in pcb_refs if r.startswith("TP"))
say("  测试焊盘: %d 个 %s" % (len(tp), tp))
silk_names = [t for t in gr_texts if t[1] in ("F.SilkS", "B.SilkS")]
say("  丝印文字 %d 条: %s" % (len(silk_names), [t[0] for t in silk_names][:12]))
hpads = sum(1 for m in pcb_mods if m["ref"] and m["ref"].startswith("H") for p in m["pads"])
say("  安装孔: %d 个 (孔径 %s)" % (len(m3_pos), [p.get("drill") for m in pcb_mods if (m["ref"] or "").startswith("H") for p in m["pads"]][:4]))

say("")
say("【7】布线/铺铜状态")
say("  走线 %d 段 / 过孔 %d / 铺铜 %d" % (len(segments), len(vias), len(zones)))
if not segments and not zones:
    say("  ⚠ 完全未布线、无铺铜 —— 属「只摆好位置的底板」，导入后必须自己布线")
# 每层走线长度
lay_len = collections.defaultdict(float)
for s in segments:
    g = {a[0]: a for a in s[1:] if isinstance(a, list)}
    if "layer" in g and "start" in g and "end" in g:
        x1, y1 = float(g["start"][1]), float(g["start"][2])
        x2, y2 = float(g["end"][1]), float(g["end"][2])
        lay_len[g["layer"][1]] += ((x2 - x1) ** 2 + (y2 - y1) ** 2) ** 0.5
if lay_len: say("  各层走线长度: %s" % {k: round(v, 1) for k, v in lay_len.items()})

say("")
say("【8】关键器件封装核对 (尺寸 vs 实物)")
KEY = {
    "R":  ("脚距 10.16mm", lambda m: True),
    "RV": ("3296W 顶调", lambda m: True),
    "J1": ("XKB PJ-320A 直插 4 端子 (C2884926)", lambda m: True),
    "J2": ("KF301 5.08mm", lambda m: True),
    "J4": ("2×3 2.54mm", lambda m: True),
    "U1": ("DIP-8 W7.62", lambda m: True),
}
for m in pcb_mods:
    r = m["ref"] or ""
    if r in ("J1", "J2", "J4", "U1") or r.startswith("RV"):
        pad_pitch = []
        pats = [p["at"] for p in m["pads"] if "at" in p]
        for i in range(len(pats)):
            for j in range(i + 1, len(pats)):
                d = ((pats[i][0] - pats[j][0]) ** 2 + (pats[i][1] - pats[j][1]) ** 2) ** 0.5
                pad_pitch.append(round(d, 2))
        say("  %-4s %-52s 引脚间距=%s" % (r, m["lib"], sorted(set(pad_pitch))[:5]))

# ────────────────────────────────── 3d. 与原理图 F2 封装 / 引脚数 比对
say("")
say("【9】原理图 F2 封装 ↔ PCB 实际封装 (逐位号)")
sch_fp_pins = {}          # ref -> (F2, pin numbers set)
for c in comps:
    if c["sym"].startswith("power_"): continue
    s = syms[c["sym"]]
    pins = set(n for n, (_, _, u) in s["pins"].items() if u == c["unit"])
    if c["ref"] in sch_fp_pins:
        sch_fp_pins[c["ref"]][1] |= pins
    else:
        sch_fp_pins[c["ref"]] = [c["fp"] or "", pins]
fp_mismatch, padcount = [], []
for r in sorted(sch_fp_pins):
    exp_fp, exp_pins = sch_fp_pins[r]
    mods = [m for m in pcb_mods if m["ref"] == r]
    if not mods: continue
    got_fp = mods[0]["lib"]
    short = exp_fp.split(":")[-1]
    if short and short != got_fp.split(":")[-1] and got_fp.split(":")[-1] not in exp_fp:
        fp_mismatch.append((r, exp_fp, got_fp))
    got_pads = set(p["num"] for p in mods[0]["pads"])
    if not exp_pins <= got_pads:
        padcount.append((r, sorted(exp_pins), sorted(got_pads)))
for r, e, a in fp_mismatch:
    say("  ⚠ %-4s 原理图 F2 = %s\n              PCB 实际 = %s" % (r, e, a))
if not fp_mismatch: say("  ✓ 封装字段全部对应 (占位待换的见下)")
for r, e, a in padcount:
    say("  ⚠ %-4s 引脚缺失: 原理图 %s / PCB %s" % (r, e, a))
if not padcount: say("  ✓ 每位号焊盘数覆盖原理图引脚数")

# 占位封装点名
placeholders = [(m["ref"], m["lib"]) for m in pcb_mods
                if any(k in m["lib"].upper() for k in ("GENERIC", "NEEDS_VERIFY", "REPLACE"))]
say("  ⚠ 需换真封装的占位件: %s" % (placeholders or "无"))

# ────────────────────────────────── 3e. 焊盘间距 DRC-lite (未布线板的主要风险)
say("")
say("【10】焊盘间距 DRC-lite (不同网络 / 阈值 0.20mm)")
allpads = []
for m in pcb_mods:
    for p in m["pads"]:
        if "at" not in p or "size" not in p: continue
        _sl = p.get("slot")
        allpads.append(dict(ref=m["ref"], num=p["num"], net=p.get("net", ""),
                            np=(p.get("type") == "np_thru_hole"),
                            x=m["at"][0] + p["at"][0], y=m["at"][1] + p["at"][1],
                            w=p["size"][0], h=p["size"][1],
                            slot=_sl,
                            # 腰形槽取短边当"有效孔径"（环宽按最窄处算，偏保守）
                            drill=min(_sl) if _sl else
                                  (float(p["drill"]) if p.get("drill") else 0.0)))
viol = []
for i in range(len(allpads)):
    for j in range(i + 1, len(allpads)):
        a, b = allpads[i], allpads[j]
        if a["ref"] == b["ref"] and a["net"] == b["net"]: continue
        gap = max(0.0, ((a["x"] - b["x"]) ** 2 + (a["y"] - b["y"]) ** 2) ** 0.5
                  - (max(a["w"], a["h"]) + max(b["w"], b["h"])) / 2)
        if gap < 0.20:
            viol.append((a["ref"] + "." + a["num"], b["ref"] + "." + b["num"],
                         round(gap, 3), a["net"] or "-", b["net"] or "-"))
say("  违反 <0.20mm 的焊盘对: %d" % len(viol))
for v in viol[:20]:
    say("     ⚠ %-9s ↔ %-9s 间隙 %.3fmm  (%s / %s)" % v)
if not viol: say("  ✓ 无过近焊盘 (含不同网络)")

# 最小孔径 / 焊盘环宽
# 非金属化孔没有铜环（ring 恒为 0），不算缺陷 —— 同上，别让它变成噪音
ring = [(p["ref"] + "." + p["num"], round((min(p["w"], p["h"]) - p["drill"]) / 2, 3))
        for p in allpads if p["drill"] and p["ref"] and not p["ref"].startswith("H")
        and not p.get("np")]
ring = [r for r in ring if r[1] < 0.15]
say("  焊盘环宽 <0.15mm 的焊盘: %s" % (ring or "✓ 无"))

# 腰形槽必须装得进焊盘：槽比焊盘长 = 铜被切穿，做出来是断的。
# 2026-10-05 随 J1 换真 PJ-320A 新增（那批焊盘是 2.0×1.0 + 1.5×0.5 槽）。
_slotbad, _slotbad_syn = [], [p for p in allpads if p.get("slot_bad")]
for p in allpads:
    if p["slot"]:
        if p["slot"][0] > p["w"] + 1e-9 or p["slot"][1] > p["h"] + 1e-9:
            _slotbad.append("%s.%s 槽 %.2f×%.2f > 焊盘 %.2f×%.2f"
                            % (p["ref"], p["num"], p["slot"][0], p["slot"][1], p["w"], p["h"]))
say("  腰形槽超出焊盘的: %s" % (_slotbad or "✓ 无"))
if _slotbad_syn:
    say("  ⚠ 有 %d 个焊盘的 drill 语法未被识别（可能漏查）: %s"
        % (len(_slotbad_syn), [p["ref"] + "." + p["num"] for p in _slotbad_syn][:4]))

print("\n".join(out))
_outdir = os.path.dirname(os.path.abspath(OUTTXT))
if _outdir and not os.path.isdir(_outdir):
    os.makedirs(_outdir, exist_ok=True)                 # --out 指向不存在的目录也能写
if os.path.exists(OUTTXT):
    sys.stderr.write("[注意] 输出文件已存在, 将被覆盖: %s  (换 --out 可保留旧文件)\n" % OUTTXT)
open(OUTTXT, "w", encoding="utf-8").write("\n".join(out))
sys.stderr.write("[原始报告已写入] %s\n" % OUTTXT)
