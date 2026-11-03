# -*- coding: utf-8 -*-
"""
gen_kicad6_sch.py — 生成 KiCad 6 (.kicad_sch) 原理图, 符号全内嵌, 供嘉立创EDA专业版导入

为什么需要它:
  嘉立创EDA 接受的 KiCad 原理图格式是 .kicad_sch(符号内嵌), 不是 legacy .sch。
  实测把 legacy .sch 导入专业版 → $Comp 引用的 -cache.lib 解析不到 →
  元件被整块丢弃(导线/标签/注释还在, 符号与位号全无)。
  .kicad_sch 把符号定义写在文件自己的 lib_symbols 段里, 不依赖任何外部库。

模型来源:
  **复用 gen_kicad.py 的原理图模型**(exec 其源码到"发射"标记之前),
  保证与已交叉审计(PCB↔原理图 0 差异)的那套完全同源。gen_kicad.py 本身只被读取, 不被修改。

坐标:
  模型是 mil(整数), 图纸 y 向下; KiCad 6 用 mm, 库坐标 y 向上。
  发射库符号时对 y 取反, 实例不加 mirror —— 两侧约定相抵, 结果与模型一致(见 verify())。

文字布局(v3, 2026-10-05):
  legacy .sch 把实例字段一律钉在 (x+150, y-200)/(x+150, y+220) 并左对齐 ——
  密集区必然压字; 库内 F0/F1 的小偏移直接当实例位置更会压在符号本体上。
  本版改为 KiCad 惯例: **位号在本体上方居中, 数值在下方居中**, 并做
  文字包围盒碰撞检测 + 自动避让(先上下后两侧, 先避文字/本体/导线, 退一步只避文字/本体)。
  电源符号的 0 长度引脚按 KiCad 惯例发 hide(否则 "GND" 引脚名会多画一份压在符号上)。

图框边界(v3.1, 2026-10-05):
  A4 横向 297x210mm, KiCad 默认 drawing sheet 四边各留 10mm。
  frame_walls() 把四条页边距当作"硬墙"喂进同一套避让打分 ——
  顶到框线的候选与"压元件本体"同级扣分, 于是长注释会自动左退/上挪进框内。
  注释另补了一组"整段左移"候选(步进 12mm), 避免长文案无解。
  自检④ 独立复核全部可见文字都在框内。

用法:
  python tools/gen_kicad6_sch.py [-v]      (-v 打印重叠明细/避让明细)
输出:
  lceda_pro/nilm_afe_v0.kicad_sch
自检:
  ① 回读文件, 按 KiCad 的变换公式重算每个引脚绝对坐标 → 与模型逐个比对
  ② 两侧各自跑并查集重建网络表 → 必须完全一致
  ③ 可见文字(位号/数值/注释/标签)包围盒两两不重叠、且不压元件本体 → 0 处
  ④ 可见文字全部落在 A4 图框内(不探进 10mm 页边距) → 0 处
"""
import io, os, sys, uuid, collections

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = os.path.dirname(HERE)
SRC_GEN = os.path.join(HERE, "gen_kicad.py")
OUTDIR = os.path.join(BASE, "lceda_pro")
PROJ = "nilm_afe_v0"
LIBN = "nilm_afe_v0"          # 内嵌库 nickname(不再用 -cache)
MIL = 0.0254

# 图纸与页边距(A4 横向, KiCad 默认 drawing sheet: 四边各 10mm 框线)
SHEET_W, SHEET_H, SHEET_M = 297.0, 210.0, 10.0

VERBOSE = False


# ---------------------------------------------------------------- 模型装载
def load_model():
    src = io.open(SRC_GEN, encoding="utf-8").read()
    marker = "# ===================== 发射: cache.lib"
    assert marker in src, "gen_kicad.py 结构变了, 找不到发射标记"
    prefix = src.split(marker)[0]          # 不含任何写盘代码
    ns = {"__name__": "_model", "__file__": SRC_GEN}
    exec(compile(prefix, SRC_GEN, "exec"), ns)
    return ns


# ---------------------------------------------------------------- 基础工具
def fmt(v):
    s = "%.4f" % v
    s = s.rstrip("0").rstrip(".")
    return "0" if s in ("", "-0", "-") else s


def mm(v):
    return fmt(v * MIL)


def q(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


NS_UUID = uuid.UUID("6e696c6d-6166-6530-8000-000000000001")


def U(key):
    return str(uuid.uuid5(NS_UUID, key))


# 模型矩阵 -> KiCad 旋转角(度, eeschema 存储角为视觉逆时针)
# 推导: legacy(标准版实测正确)矩阵直接作用于 y-down 库坐标; KiCad 6 库坐标 y-up、
# 图纸 y-down。库点 (lx,ly)[模型系] → 文件系 (lx,-ly) → 逆时针转 A → 翻回 y-down:
#   sheet_delta = (cA*lx + sA*ly, -sA*lx + cA*ly)
# 对照模型 delta = (m0*lx + m1*ly, m2*lx + m3*ly) 解得:
#   视觉顺时针(CW) ↔ A=270, 视觉逆时针(CCW) ↔ A=90
ANG = {(1, 0, 0, 1): 0, (0, -1, 1, 0): 270, (0, 1, -1, 0): 90, (-1, 0, 0, -1): 180}

# KiCad 引脚电气类型
ETYPE = {"I": "input", "O": "output", "B": "bidirectional", "T": "tristate",
         "P": "passive", "U": "unspecified", "W": "power_in", "w": "power_out",
         "C": "open_collector", "E": "open_emitter", "N": "no_connect"}
PANG = {"R": 0, "U": 90, "L": 180, "D": 270}
FILL = {"N": "none", "F": "outline", "f": "background"}


def parse_def(defline):
    t = defline.split()
    # DEF name ref unused text_off draw_num draw_name units [locked] [option]
    # 末尾可选两字段: locked(L/F) 与 option(P=power/N); 只认 option
    option = "N"
    if len(t) > 9:
        option = t[9]
    elif len(t) > 8 and t[8] in ("P", "N"):
        option = t[8]
    return dict(name=t[1], prefix=t[2], text_off=int(t[4]),
                draw_num=t[5], draw_name=t[6], units=int(t[7]), option=option)


# ---------------------------------------------------------------- 库符号
def lib_symbol(name, s):
    d = parse_def(s["defline"])
    L = []
    L.append("    (symbol %s" % q("%s:%s" % (LIBN, name)))
    if d["option"] == "P":
        L.append("      (power)")
    if d["draw_num"] == "N":
        L.append("      (pin_numbers hide)")
    names = "(pin_names (offset %s)%s)" % (
        mm(d["text_off"]), " hide" if d["draw_name"] == "N" else "")
    L.append("      " + names)
    L.append("      (in_bom yes) (on_board yes)")

    # 属性: F0->Reference, F1->Value, 另补 Footprint/Datasheet
    # 电源符号(option=P)的库内 Value 默认值用网络名(引脚名), 与真实 KiCad power:GND 一致
    props = {"Reference": (d["prefix"], None),
             "Value": (s["pins"][0][1] if d["option"] == "P" else name, None)}
    for ftag, ftxt, fx, fy, ffmt in s["fields"]:
        key = "Reference" if ftag == "F0" else ("Value" if ftag == "F1" else None)
        if key:
            props[key] = (ftxt.strip('"'), (fx, fy, ffmt))
    if d["option"] == "P":
        props["Value"] = (s["pins"][0][1], props["Value"][1])  # 电源: 网络名优先于 F1 符号名
    order = [("Reference", "Reference", 0), ("Value", "Value", 1),
             ("Footprint", "", 2), ("Datasheet", "", 3)]
    for pname, pdefault, pid in order:
        val, pos = props.get(pname, (pdefault, None))
        if pos:
            px, py, ffmt = pos
            ft = ffmt.split()
            rot = 90 if ft[0] == "V" else 0
            hide = " hide" if len(ft) > 1 and ft[1] == "I" else ""
            at = "(at %s %s %d)" % (mm(px), mm(-py), rot)   # 库 y 取反
        else:
            at, hide = "(at 0 0 0)", (" hide" if pname in ("Footprint", "Datasheet") else "")
        L.append("      (property %s %s (id %d) %s (effects (font (size 1.27 1.27))%s))"
                 % (q(pname), q(val), pid, at, hide))

    # 按 unit 分组: 图形 + 引脚
    gfx = collections.defaultdict(list)
    pins = collections.defaultdict(list)
    for g in s["draw"]:
        t = g.split()
        if t[0] == "S":
            x1, y1, x2, y2, u = int(t[1]), int(t[2]), int(t[3]), int(t[4]), int(t[5])
            th, fill = int(t[7]), t[8]
            gfx[u].append("        (rectangle (start %s %s) (end %s %s)\n"
                          "          (stroke (width %s) (type default)) (fill (type %s)))"
                          % (mm(x1), mm(-y1), mm(x2), mm(-y2), mm(th), FILL.get(fill, "none")))
        elif t[0] == "P":
            n, u, th = int(t[1]), int(t[2]), int(t[4])
            coords = t[5:5 + 2 * n]
            fill = t[5 + 2 * n]
            pts = " ".join("(xy %s %s)" % (mm(int(coords[i])), mm(-int(coords[i + 1])))
                           for i in range(0, 2 * n, 2))
            gfx[u].append("        (polyline (pts %s)\n"
                          "          (stroke (width %s) (type default)) (fill (type %s)))"
                          % (pts, mm(th), FILL.get(fill, "none")))
        elif t[0] == "C":
            x, y, r, u, th, fill = (int(t[1]), int(t[2]), int(t[3]), int(t[4]),
                                    int(t[5]), t[6])
            gfx[u].append("        (circle (center %s %s) (radius %s)\n"
                          "          (stroke (width %s) (type default)) (fill (type %s)))"
                          % (mm(x), mm(-y), mm(r), mm(th), FILL.get(fill, "none")))
    for p in s["pins"]:
        num, pname, x, y, ln, o, et = p[:7]
        u = p[7] if len(p) > 7 else 1
        pins[u].append((num, pname, x, y, ln, o, et))

    for u in sorted(set(list(gfx) + list(pins))):
        L.append("      (symbol %s" % q("%s_%d_1" % (name, u)))
        L += gfx.get(u, [])
        for num, pname, x, y, ln, o, et in pins.get(u, []):
            # 0 长度引脚(电源符号)按 KiCad 惯例发 hide, 免得引脚名多画一份
            hide = " hide" if ln == 0 else ""
            L.append("        (pin %s line (at %s %s %d) (length %s)%s"
                     % (ETYPE.get(et, "passive"), mm(x), mm(-y), PANG.get(o, 0), mm(ln), hide))
            L.append("          (name %s (effects (font (size 1.27 1.27))))" % q(pname))
            L.append("          (number %s (effects (font (size 1.27 1.27)))))" % q(num))
        L.append("      )")
    L.append("    )")
    return "\n".join(L)


# ================================================================ 文字度量与避让
TXT_W = 0.85          # 字宽/字高 经验比(笔画字体); 全角按 1.7 计
TXT_GAP = 0.25        # 包围盒判重间隙(mm)
FIELD_SIZE = 1.27     # 位号/数值字号(50 mil)
PIN_NAME_SIZE = 1.27
PIN_NUM_SIZE = 1.0


def tw(text, size):
    n = 0.0
    for ch in text:
        n += 1.7 if ord(ch) > 0x2E80 else 1.0
    return TXT_W * size * n


def tbox(text, x, y, size, justify=""):
    """文字包围盒 (mm, y 向下)。justify 含 left/right 与 top/bottom, 缺省居中"""
    w, h = tw(text, size), size
    j = justify or ""
    x1 = x if "left" in j else (x - w if "right" in j else x - w / 2.0)
    y1 = y - h if "bottom" in j else (y if "top" in j else y - h / 2.0)
    return (x1, y1, x1 + w, y1 + h)


def hit(a, b, gap=TXT_GAP):
    return not (a[2] + gap <= b[0] or b[2] + gap <= a[0]
                or a[3] + gap <= b[1] or b[3] + gap <= a[1])


# 画幅外的四条"硬墙": 文字盒只要探进页边距, 就算一次硬冲突(与压本体同级)
_BIG = 1000.0
def frame_walls():
    W, H, M = SHEET_W, SHEET_H, SHEET_M
    return [(-_BIG, -_BIG, M, H + _BIG),          # 左
            (W - M, -_BIG, W + _BIG, H + _BIG),   # 右
            (-_BIG, -_BIG, W + _BIG, M),          # 上
            (-_BIG, H - M, W + _BIG, H + _BIG)]   # 下


def out_of_frame(box):
    """返回越界方向字符串('left/right/top/bottom' 空格拼接), 全在框内则返回 ''"""
    M, W, H = SHEET_M, SHEET_W, SHEET_H
    bad = []
    if box[0] < M - 0.01: bad.append("left")
    if box[2] > W - M + 0.01: bad.append("right")
    if box[1] < M - 0.01: bad.append("top")
    if box[3] > H - M + 0.01: bad.append("bottom")
    return " ".join(bad)


def ov_area(a, b):
    w = min(a[2], b[2]) - max(a[0], b[0])
    h = min(a[3], b[3]) - max(a[1], b[1])
    return max(0.0, w) * max(0.0, h)


# ---------------------------------------------------------------- 元件几何
DIR = {"R": (1, 0), "L": (-1, 0), "U": (0, -1), "D": (0, 1)}   # 引脚朝向 -> 指向本体(模型系)


def xf(c, lx, ly):
    """模型库坐标 -> 图纸坐标(mil)"""
    m = c["mat"]
    return (c["x"] + m[0] * lx + m[1] * ly, c["y"] + m[2] * lx + m[3] * ly)


def gfx_pts(sym, unit):
    """元件图形(unit 0 为公共)的模型坐标点集"""
    pts = []
    for g in sym["draw"]:
        t = g.split()
        if t[0] == "S":
            if int(t[5]) in (0, unit):
                pts += [(int(t[1]), int(t[2])), (int(t[3]), int(t[4]))]
        elif t[0] == "P":
            n = int(t[1])
            if int(t[2]) in (0, unit):
                cs = t[5:5 + 2 * n]
                pts += [(int(cs[i]), int(cs[i + 1])) for i in range(0, 2 * n, 2)]
        elif t[0] == "C":
            if int(t[4]) in (0, unit):
                cx, cy, r = int(t[1]), int(t[2]), int(t[3])
                pts += [(cx - r, cy - r), (cx + r, cy + r)]
    return pts


def body_box(c, SYMS):
    """元件本体(图形, 不含引脚)包围盒, mm"""
    pts = gfx_pts(SYMS[c["sym"]], c["unit"])
    xs = [xf(c, p[0], p[1])[0] * MIL for p in pts]
    ys = [xf(c, p[0], p[1])[1] * MIL for p in pts]
    if not xs:
        return (c["x"] * MIL - 0.5, c["y"] * MIL - 0.5,
                c["x"] * MIL + 0.5, c["y"] * MIL + 0.5)
    return (min(xs), min(ys), max(xs), max(ys))


def pin_geo(c, SYMS, P):
    """本实例某 unit 的引脚几何(连接点/本体端点/指入本体的方向), mm"""
    sym = SYMS[c["sym"]]
    d = parse_def(sym["defline"])
    m = c["mat"]
    ispwr = c["ref"].startswith("#PWR")      # 电源符号不在模型 comps 里, 引脚即在原点
    out = []
    for p in sym["pins"]:
        num, pname, x, y, ln, o = p[:6]
        u = p[7] if len(p) > 7 else 1
        if u != c["unit"]:
            continue
        dx, dy = DIR.get(o, (1, 0))
        ix, iy = m[0] * dx + m[1] * dy, m[2] * dx + m[3] * dy
        cx, cy = (c["x"], c["y"]) if ispwr else P(c["key"], num)
        out.append(dict(num=num, name=pname, d=d, length=ln * MIL,
                        conn=(cx * MIL, cy * MIL), inward=(ix, iy)))
    return out


def inst_obstacles(c, SYMS, P):
    """本体 + 引脚名/引脚号 的包围盒(文字避让硬障碍)
    返回 [(kind, owner, tag, box)]; owner = 位号|unit (多单元器件要区分单元)"""
    own = "%s|%s" % (c["ref"], c["unit"])
    out = [("本体", own, c["ref"], body_box(c, SYMS))]
    for pg in pin_geo(c, SYMS, P):
        if pg["length"] == 0:            # 0 长度引脚(电源)在文件里是 hide, 不占位
            continue
        ix, iy = pg["inward"]
        ln = pg["length"]
        conn = pg["conn"]
        end = (conn[0] + ix * ln, conn[1] + iy * ln)     # 引脚与本体相接的一端
        mid = ((conn[0] + end[0]) / 2.0, (conn[1] + end[1]) / 2.0)
        if pg["d"]["draw_num"] == "Y":                   # 引脚号: 水平引脚画上方, 竖直画左侧
            if abs(ix) > abs(iy):
                out.append(("引脚号", own, "%s.%s" % (c["ref"], pg["num"]),
                            tbox(pg["num"], mid[0], mid[1] - 0.9, PIN_NUM_SIZE, "")))
            else:
                out.append(("引脚号", own, "%s.%s" % (c["ref"], pg["num"]),
                            tbox(pg["num"], mid[0] - 0.9, mid[1], PIN_NUM_SIZE, "")))
        nm = pg["name"]
        if pg["d"]["draw_name"] == "Y" and nm not in ("~", ""):
            off = pg["d"]["text_off"] * MIL
            ax, ay = end[0] + ix * off, end[1] + iy * off   # 名字从本体端点再往里 text_off
            if abs(ix) > abs(iy):
                j = "left" if ix > 0 else "right"
                out.append(("引脚名", own, "%s.%s" % (c["ref"], nm),
                            tbox(nm, ax, ay, PIN_NAME_SIZE, j)))
            else:
                w = tw(nm, PIN_NAME_SIZE)
                y2 = ay + (w if iy > 0 else -w)
                out.append(("引脚名", own, "%s.%s" % (c["ref"], nm),
                            (ax - PIN_NAME_SIZE / 2.0, min(ay, y2),
                             ax + PIN_NAME_SIZE / 2.0, max(ay, y2))))
    return out


def place_candidates(b, kind):
    """位号/数值的候选锚点(mm): 本体外侧优先, 由近及远; 顺序 = 偏好顺序"""
    x1, y1, x2, y2 = b
    cx, cy = (x1 + x2) / 2.0, (y1 + y2) / 2.0
    up = [(cx, y1 - 0.7 - k * 2.6, "bottom") for k in range(4)]
    dn = [(cx, y2 + 0.7 + k * 2.6, "top") for k in range(4)]
    rt = [(x2 + 0.8 + k * 2.8, cy, "left") for k in range(3)]
    lt = [(x1 - 0.8 - k * 2.8, cy, "right") for k in range(3)]
    ct = [(x2 + 0.8, y1 - 0.7, "left"), (x1 - 0.8, y1 - 0.7, "right"),
          (x2 + 0.8, y2 + 0.7, "left"), (x1 - 0.8, y2 + 0.7, "right")]
    if kind == "Reference":
        head = up[:1] + ct[:2] + dn[:1]
    else:
        head = dn[:1] + ct[2:] + up[:1]
    tail = []
    for k in range(3):                     # 交织: 同一距离圈里轮着试四个方向
        if k + 1 < len(up):
            tail.append(up[k + 1])
        if k + 1 < len(dn):
            tail.append(dn[k + 1])
        if k < len(rt):
            tail.append(rt[k])
        if k < len(lt):
            tail.append(lt[k])
    return head + tail


def pick(cands, text, size, placed, ob_boxes, wire_boxes, use_wires=True):
    """打分选位: 硬冲突 > 压导线 > 距离。返回 (choice, hard_cnt)"""
    best = None
    for k, (cx, cy, j) in enumerate(cands):
        box = tbox(text, cx, cy, size, j)
        hard = sum(1 for p in placed if hit(box, p)) \
            + sum(1 for o in ob_boxes if hit(box, o))
        soft = sum(1 for wb in wire_boxes if hit(box, wb, 0.0)) if use_wires else 0
        score = (hard, k + 2 * soft)
        if best is None or score < best[0]:
            best = (score, (cx, cy, j, k, box), hard)
    return best[1], best[2]


# ---------------------------------------------------------------- 组装文件
def build(ns):
    SYMS, comps = ns["SYMS"], ns["comps"]
    wires, labels = ns["wires"], ns["labels"]
    powers, dots, ncs, notes = ns["powers"], ns["dots"], ns["ncs"], ns["notes"]
    P = ns["P"]

    root = U("sheet/root")
    L = ["(kicad_sch (version 20211123) (generator eeschema)",
         "",
         "  (uuid %s)" % root,
         "",
         '  (paper "A4")',
         "",
         "  (title_block",
         '    (title %s)' % q(PROJ),
         '    (date "2026-09-23")',
         '    (rev "V0")',
         '    (company "NILM Project")',
         '    (comment 1 "AFE V0 analog front end: CT current + transformer voltage")',
         '    (comment 2 "0.5V midpoint, <=1Vpp, for XADC or AD7606")',
         "  )",
         "",
         "  (lib_symbols"]
    for name in SYMS:
        L.append(lib_symbol(name, SYMS[name]))
    L.append("  )")
    L.append("")

    # 结点 / 不连接
    for (x, y) in dots:
        L.append("  (junction (at %s %s) (diameter 0) (color 0 0 0 0)" % (mm(x), mm(y)))
        L.append("    (uuid %s)" % U("junction/%d,%d" % (x, y)))
        L.append("  )")
    for (x, y) in ncs:
        L.append("  (no_connect (at %s %s) (uuid %s))" % (mm(x), mm(y), U("nc/%d,%d" % (x, y))))
    if dots or ncs:
        L.append("")

    # 导线
    for (x1, y1, x2, y2) in wires:
        L.append("  (wire (pts (xy %s %s) (xy %s %s))" % (mm(x1), mm(y1), mm(x2), mm(y2)))
        L.append("    (stroke (width 0) (type default))")
        L.append("    (uuid %s)" % U("wire/%d,%d-%d,%d" % (x1, y1, x2, y2)))
        L.append("  )")
    L.append("")

    # ---- 实例表(含电源符号) ----
    inst = []
    for c in comps:
        inst.append(dict(ref=c["ref"], sym=c["sym"], value=c["value"], fp=c["fp"],
                         x=c["x"], y=c["y"], mat=c["mat"], unit=c["unit"], key=c["key"]))
    pwr_n = 0
    for (sym, x, y) in powers:
        pwr_n += 1
        ref = "#PWR%04d" % pwr_n
        net = SYMS[sym]["pins"][0][1]
        inst.append(dict(ref=ref, sym=sym, value=net, fp="", x=x, y=y,
                         mat=(1, 0, 0, 1), unit=1, key=ref))

    # ---- 固定文本: 网络标签(锚点必须落在导线上, 不可挪) / 注释(可小幅平移避让) ----
    labels_f, notes_f = [], []
    for (x, y, size, text) in notes:
        notes_f.append(dict(text=text, x=x * MIL, y=y * MIL, size=size * MIL,
                            justify="left bottom", kind="note"))
    for (name, x, y, orient) in labels:
        ang = 180 if orient == 2 else 0
        labels_f.append(dict(text=name, x=x * MIL, y=y * MIL, size=1.27,
                             justify=("right" if ang == 180 else "left") + " bottom",
                             kind="label", ang=ang))

    # ---- 障碍: 元件本体 + 引脚名/号; 导线(软障碍); 页边距(硬墙, 只约束文字) ----
    obstacles = []
    for c in inst:
        obstacles += inst_obstacles(c, SYMS, P)
    walls = frame_walls()
    ob_boxes = [o[3] for o in obstacles] + walls
    wire_boxes = [(min(x1, x2) * MIL - 0.2, min(y1, y2) * MIL - 0.2,
                   max(x1, x2) * MIL + 0.2, max(y1, y2) * MIL + 0.2)
                  for (x1, y1, x2, y2) in wires]

    # 标签先占位(不可动)
    placed = [tbox(f["text"], f["x"], f["y"], f["size"], f["justify"]) for f in labels_f]

    # 注释: 先原位, 撞了再小幅平移(保持与所注对象的关联); 顶到图框就先往左退, 保证不越界
    NOTE_SHIFT = [(0, 0), (0, -2.6), (0, 2.6), (0, -5.2), (0, 5.2), (0, -7.8), (0, 7.8),
                  (0, -10.4), (0, 10.4), (0, -13.0), (0, 13.0), (3.0, 0), (-3.0, 0)]
    # 长注释(如电源说明)可能比图框还宽: 追加一组"整段左移"候选, 步进 12mm 直到贴到左框
    for dx in (-12.0, -24.0, -36.0, -48.0, -60.0, -72.0, -84.0):
        for dy in (0.0, -2.6, 2.6, -5.2, 5.2):
            NOTE_SHIFT.append((dx, dy))
    note_moved, note_stuck = [], []
    for f in notes_f:
        cands = [(f["x"] + dx, f["y"] + dy, f["justify"]) for (dx, dy) in NOTE_SHIFT]
        (cx, cy, j, k, box), hard = pick(cands, f["text"], f["size"], placed, ob_boxes, [])
        f["x"], f["y"] = cx, cy
        if k > 0:
            note_moved.append((f, k))
        if hard:
            note_stuck.append((f, hard))
        placed.append(box)

    # ---- 可移动文本: 位号 / 数值 ----
    mov = []
    for c in inst:
        if c["ref"].startswith("#PWR"):     # 电源符号: 数值(网络名)居中放本体上方
            cx, cy = c["x"] * MIL, c["y"] * MIL
            cands = [(cx, cy - 3.9 - k * 2.6, "") for k in range(6)]
            cands += [(cx, cy + 3.9 + k * 2.6, "") for k in range(3)]
            cands += [(cx + 2.6, cy - 3.9, "left"), (cx - 2.6, cy - 3.9, "right"),
                      (cx + 5.2, cy - 3.9, "left"), (cx - 5.2, cy - 3.9, "right")]
            mov.append(dict(owner=c["ref"], own=c["key"], kind="Value", text=c["value"],
                            size=FIELD_SIZE, cands=cands))
        else:
            b = body_box(c, SYMS)
            mov.append(dict(owner=c["ref"], own=c["key"], kind="Reference", text=c["ref"],
                            size=FIELD_SIZE, cands=place_candidates(b, "Reference")))
            mov.append(dict(owner=c["ref"], own=c["key"], kind="Value", text=c["value"],
                            size=FIELD_SIZE, cands=place_candidates(b, "Value")))

    # ---- 避让: 打分选位(硬冲突 > 压导线 > 距离) ----
    nudged, failed = [], []
    for it in sorted(mov, key=lambda d: (d["cands"][0][1], d["cands"][0][0], d["kind"])):
        (cx, cy, j, k, box), hard = pick(it["cands"], it["text"], it["size"],
                                         placed, ob_boxes, wire_boxes)
        it["x"], it["y"], it["justify"] = cx, cy, j
        if k > 0:
            nudged.append((it, k))
        if hard:
            failed.append((it, hard))
        placed.append(box)

    # ---- 发射文本: 注释 ----
    for f in notes_f:
        L.append("  (text %s (at %s %s 0)" % (q(f["text"]), fmt(f["x"]), fmt(f["y"])))
        L.append("    (effects (font (size %s %s)) (justify left bottom))"
                 % (fmt(f["size"]), fmt(f["size"])))
        L.append("    (uuid %s)" % U("note/%s" % f["text"][:24]))
        L.append("  )")
    L.append("")

    # ---- 发射文本: 网络标签 ----
    for f in labels_f:
        L.append("  (label %s (at %s %s %d)" % (q(f["text"]), fmt(f["x"]), fmt(f["y"]), f["ang"]))
        L.append("    (effects (font (size 1.27 1.27)) (justify %s))" % f["justify"])
        L.append("    (uuid %s)" % U("label/%s@%s,%s" % (f["text"], fmt(f["x"]), fmt(f["y"]))))
        L.append("  )")
    L.append("")

    # ---- 元件实例 ----
    fld = {}
    for it in mov:
        fld[(it["own"], it["kind"])] = it                     # 多单元器件必须按实例 key 索引

    for c in inst:
        ang = ANG[tuple(c["mat"])]
        ispwr = c["ref"].startswith("#PWR")
        L.append("  (symbol (lib_id %s) (at %s %s %d) (unit %d)"
                 % (q("%s:%s" % (LIBN, c["sym"])), mm(c["x"]), mm(c["y"]), ang, c["unit"]))
        L.append("    (in_bom yes) (on_board yes)")
        L.append("    (uuid %s)" % U("sym/%s" % c["key"]))
        for kind, pid in (("Reference", 0), ("Value", 1)):
            it = fld.get((c["key"], kind))
            if it is None:               # 电源符号的位号隐藏, 锚点放原点即可
                it = dict(text=c["ref"] if kind == "Reference" else c["value"],
                          x=c["x"] * MIL, y=c["y"] * MIL, justify="")
            L.append("    (property %s %s (id %d) (at %s %s 0)"
                     % (q(kind), q(it["text"]), pid, fmt(it["x"]), fmt(it["y"])))
            eff = " (justify %s)" % it["justify"] if it["justify"] else ""
            hide = " hide" if (kind == "Reference" and ispwr) else ""
            L.append("      (effects (font (size 1.27 1.27))%s%s)" % (eff, hide))
            L.append("    )")
        ox, oy = c["x"] * MIL, c["y"] * MIL
        for kind, pid, val in (("Footprint", 2, c["fp"]), ("Datasheet", 3, "~")):
            L.append("    (property %s %s (id %d) (at %s %s 0)"
                     % (q(kind), q(val), pid, fmt(ox), fmt(oy)))
            L.append("      (effects (font (size 1.27 1.27)) hide)")
            L.append("    )")
        for pg in pin_geo(c, SYMS, P):
            L.append("    (pin %s (uuid %s))" % (q(pg["num"]), U("pin/%s/%s" % (c["key"], pg["num"]))))
        L.append("    (instances")
        L.append("      (project %s" % q(PROJ))
        L.append("        (path %s" % q("/" + root))
        L.append("          (reference %s) (unit %d)" % (q(c["ref"]), c["unit"]))
        L.append("        )")
        L.append("      )")
        L.append("    )")
        L.append("  )")
        L.append("")

    L.append("  (sheet_instances")
    L.append('    (path "/" (page "1"))')
    L.append("  )")
    L.append(")")
    stats = dict(n_inst=len(inst), n_note=len(notes), n_label=len(labels),
                 n_mov=len(mov), nudged=nudged, failed=failed,
                 notes_f=notes_f, labels_f=labels_f,
                 note_moved=note_moved, note_stuck=note_stuck, obstacles=obstacles)
    return "\n".join(L) + "\n", stats


# ---------------------------------------------------------------- 自检① ②
def on_seg(px, py, x1, y1, x2, y2):
    if x1 == x2 and px == x1:
        return min(y1, y2) <= py <= max(y1, y2)
    if y1 == y2 and py == y1:
        return min(x1, x2) <= px <= max(x1, x2)
    return False


def netlist(pins, wires, lbls, juncs):
    """并查集重建网络表(与 audit_pcb.py 同一套三条规则);
    pins: {key:(x,y)}, lbls: [(name,x,y)], juncs: [(x,y)]"""
    par = {}

    def find(a):
        par.setdefault(a, a)
        while par[a] != a:
            par[a] = par[par[a]]
            a = par[a]
        return a

    def uni(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            par[rb] = ra

    for (x1, y1, x2, y2) in wires:
        uni((x1, y1), (x2, y2))
    pts = list(pins.values()) + [(x, y) for (_, x, y) in lbls] + list(juncs) \
        + [(w[0], w[1]) for w in wires] + [(w[2], w[3]) for w in wires]
    for (x1, y1, x2, y2) in wires:
        for (px, py) in pts:
            if on_seg(px, py, x1, y1, x2, y2):
                uni((px, py), (x1, y1))
    groups = collections.defaultdict(set)
    for key, p in pins.items():
        groups[find(p)].add(key)
    named = {}
    for (nm, x, y) in lbls:
        named.setdefault(find((x, y)), set()).add(nm)
    out = {}
    for root, mem in groups.items():
        names = sorted(named.get(root, []))
        out[",".join(names) if names else "?"] = frozenset(mem)
    return out


def sexpr(text):
    import re
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


def verify(ns, txt):
    SYMS, comps = ns["SYMS"], ns["comps"]
    doc = sexpr(txt)
    # 1) 库引脚索引
    libpins = {}
    libsec = next(n for n in doc if isinstance(n, list) and n and n[0] == "lib_symbols")
    for sym in libsec[1:]:
        if not (isinstance(sym, list) and sym and sym[0] == "symbol"):
            continue
        fullname = sym[1].split(":")[-1]
        d = {}
        for sub in sym:
            if isinstance(sub, list) and sub and sub[0] == "symbol":
                unit = int(sub[1].rsplit("_", 2)[1])
                for e in sub:
                    if isinstance(e, list) and e and e[0] == "pin":
                        at = next(a for a in e if isinstance(a, list) and a[0] == "at")
                        numnode = next(a for a in e if isinstance(a, list) and a[0] == "number")
                        d.setdefault(unit, []).append((numnode[1], float(at[1]), float(at[2])))
        libpins[fullname] = d

    # 2) 实例 -> 绝对引脚坐标
    #    KiCad 6 几何真值(与 ANG 推导同源):
    #      sheet = P + (cA*lx - sA*ly, -sA*lx - cA*ly)   [lx,ly 为文件库坐标(y 向上)]
    import math
    fpins, mism = {}, []
    for node in doc:
        if not (isinstance(node, list) and node and node[0] == "symbol"):
            continue
        libid = next(a[1] for a in node if isinstance(a, list) and a[0] == "lib_id")
        at = next(a for a in node if isinstance(a, list) and a[0] == "at")
        unit = int(next(a[1] for a in node if isinstance(a, list) and a[0] == "unit"))
        ref = next(a[2] for a in node if isinstance(a, list) and a[0] == "property"
                   and a[1] == "Reference")
        x, y, th = float(at[1]), float(at[2]), math.radians(float(at[3]))
        c, s = round(math.cos(th)), round(math.sin(th))
        for num, lx, ly in libpins[libid.split(":")[-1]].get(unit, []):
            fpins[(ref, num)] = (x + c * lx - s * ly, y - s * lx - c * ly)

    # 3) 与模型比对引脚坐标
    model_pins = {}
    for cm in comps:
        sym = SYMS[cm["sym"]]
        for p in sym["pins"]:
            pu = p[7] if len(p) > 7 else 1
            if pu != cm["unit"]:
                continue
            mx, my = ns["P"](cm["key"], p[0])
            model_pins[(cm["ref"], p[0])] = (mx * MIL, my * MIL)
    pwr_n = 0
    for (sym, x, y) in ns["powers"]:
        pwr_n += 1
        model_pins[("#PWR%04d" % pwr_n, "1")] = (x * MIL, y * MIL)

    if set(model_pins) != set(fpins):
        mism.append("引脚集合不同: 模型多 %s / 文件多 %s"
                    % (sorted(set(model_pins) - set(fpins))[:5],
                       sorted(set(fpins) - set(model_pins))[:5]))
    for k in sorted(set(model_pins) & set(fpins)):
        (ax, ay), (bx, by) = model_pins[k], fpins[k]
        if abs(ax - bx) > 0.002 or abs(ay - by) > 0.002:
            mism.append("%s 模型(%.3f,%.3f) vs 文件(%.3f,%.3f)" % (k, ax, ay, bx, by))

    # 4) 网络表比对
    fwires, flabels, fjuncs = [], [], []
    for node in doc:
        if not isinstance(node, list) or not node:
            continue
        if node[0] == "wire":
            pts = next(a for a in node if isinstance(a, list) and a[0] == "pts")
            fwires.append((float(pts[1][1]), float(pts[1][2]),
                           float(pts[2][1]), float(pts[2][2])))
        elif node[0] == "label":
            at = next(a for a in node if isinstance(a, list) and a[0] == "at")
            flabels.append((node[1], float(at[1]), float(at[2])))
        elif node[0] == "junction":
            at = next(a for a in node if isinstance(a, list) and a[0] == "at")
            fjuncs.append((float(at[1]), float(at[2])))

    lbls_mm = [(n, x * MIL, y * MIL) for (n, x, y, o) in ns["labels"]]
    fs_model = netlist({k: (round(v[0], 3), round(v[1], 3)) for k, v in model_pins.items()},
                       [(round(w[0] * MIL, 3), round(w[1] * MIL, 3),
                         round(w[2] * MIL, 3), round(w[3] * MIL, 3)) for w in ns["wires"]],
                       [(n, round(x, 3), round(y, 3)) for n, x, y in lbls_mm],
                       [(round(x * MIL, 3), round(y * MIL, 3)) for (x, y) in ns["dots"]])
    fs_file = netlist({k: (round(v[0], 3), round(v[1], 3)) for k, v in fpins.items()},
                      [(round(w[0], 3), round(w[1], 3), round(w[2], 3), round(w[3], 3)) for w in fwires],
                      [(n, round(x, 3), round(y, 3)) for n, x, y in flabels],
                      [(round(x, 3), round(y, 3)) for x, y in fjuncs])

    def canon(nt):
        return sorted(tuple(sorted(v)) for v in nt.values())
    return mism, canon(fs_model) == canon(fs_file), len(fs_file), len(fwires), len(flabels), len(fjuncs)


# ---------------------------------------------------------------- 自检③ 文字重叠
def eff_info(eff):
    size, just, hide = 1.27, "", False
    if eff is None:
        return size, just, hide
    for a in eff:
        if isinstance(a, list) and a[0] == "font":
            for b in a:
                if isinstance(b, list) and b[0] == "size":
                    size = float(b[1])
        elif isinstance(a, list) and a[0] == "justify":
            just = " ".join(str(v) for v in a[1:])
        elif a == "hide":
            hide = True
    return size, just, hide


def check_text(doc, stats):
    """文件侧复核: 可见文字两两不重叠, 且不压元件本体/引脚文字"""
    items = []
    for node in doc:
        if not isinstance(node, list) or not node:
            continue
        if node[0] == "symbol":
            unit = next((a[1] for a in node if isinstance(a, list) and a[0] == "unit"), "1")
            ref = next((a[2] for a in node if isinstance(a, list) and a[0] == "property"
                        and a[1] == "Reference"), "?")
            for sub in node:
                if isinstance(sub, list) and sub[0] == "property" and sub[1] in ("Reference", "Value"):
                    at = next(a for a in sub if isinstance(a, list) and a[0] == "at")
                    eff = next((a for a in sub if isinstance(a, list) and a[0] == "effects"), None)
                    size, just, hide = eff_info(eff)
                    if hide:
                        continue
                    items.append(("%s.%s" % (ref, sub[1]), sub[2],
                                  float(at[1]), float(at[2]), size, just))
        elif node[0] in ("text", "label"):
            at = next(a for a in node if isinstance(a, list) and a[0] == "at")
            eff = next((a for a in node if isinstance(a, list) and a[0] == "effects"), None)
            size, just, hide = eff_info(eff)
            if hide:
                continue
            items.append((node[0] + ":" + node[1][:18], node[1],
                          float(at[1]), float(at[2]), size, just))
    boxes = [(nm, tbox(t, x, y, s, j)) for (nm, t, x, y, s, j) in items]
    pairs = []
    for i in range(len(boxes)):
        for j in range(i + 1, len(boxes)):
            if hit(boxes[i][1], boxes[j][1]):
                pairs.append((boxes[i][0], boxes[j][0]))
    # 文字 ↔ 元件本体/引脚文字(避让时按同一套障碍处理, 这里逐条复核)
    ob_boxes = [o[3] for o in stats["obstacles"]]
    onbody = []
    for nm, b in boxes:
        for (kind, own, tag, ob) in stats["obstacles"]:
            gap = 0.0 if kind == "本体" else TXT_GAP
            if hit(b, ob, gap):
                onbody.append((nm, "%s %s" % (kind, tag)))
    # 文字 ↔ 图纸边框(页边距, A4 默认四边各 10mm)
    outof = [(nm, out_of_frame(b)) for nm, b in boxes]
    outof = [(nm, d) for nm, d in outof if d]
    return items, pairs, onbody, outof


# ---------------------------------------------------------------- main
def main():
    global VERBOSE
    VERBOSE = "-v" in sys.argv or "--verbose" in sys.argv
    ns = load_model()
    txt, stats = build(ns)
    os.makedirs(OUTDIR, exist_ok=True)
    path = os.path.join(OUTDIR, PROJ + ".kicad_sch")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(txt)

    mism, nets_ok, n_nets, n_w, n_l, n_j = verify(ns, txt)
    items, pairs, onbody, outof = check_text(sexpr(txt), stats)

    print("=" * 72)
    print("  KiCad 6 原理图 (符号内嵌) 已生成")
    print("=" * 72)
    print("  输出: %s" % os.path.relpath(path, BASE))
    print("  规模: 符号库 %d 个 / 元件实例 %d / 导线 %d / 结点 %d / 标签 %d / 注释 %d"
          % (len(ns["SYMS"]), stats["n_inst"], n_w, n_j, n_l, stats["n_note"]))
    print("")
    print("  自检①  回读文件重算引脚坐标 ↔ 模型            : %s"
          % ("✓ 全部一致" if not mism else "✗ %d 处不符" % len(mism)))
    for m in mism[:10]:
        print("        ", m)
    print("  自检②  两侧独立重建网络表(并查集)比对          : %s"
          % ("✓ 完全一致" if nets_ok else "✗ 不一致"))
    print("          (文件侧解析线 %d / 标签 %d / 结点 %d)" % (n_w, n_l, n_j))
    print("  自检③  可见文字零重叠/不压本体(文件侧复核)     : %s"
          % ("✓ 干净" if (not pairs and not onbody)
             else "✗ 重叠 %d 对 / 压本体 %d 处" % (len(pairs), len(onbody))))
    print("          (可见文字 %d 条 = 位号+数值+注释+标签; 位号/数值避让微调 %d 处)"
          % (len(items), len(stats["nudged"])))
    if stats["failed"]:
        print("          ⚠ 位号/数值无解 %d 处(候选位全撞, 已取最轻): %s"
              % (len(stats["failed"]),
                 ", ".join("%s.%s" % (f[0]["owner"], f[0]["kind"]) for f in stats["failed"][:6])))
    if stats["note_moved"]:
        print("          · 注释避让微调 %d 条: %s"
              % (len(stats["note_moved"]),
                 ", ".join("%s…#%d" % (f["text"][:14], k) for f, k in stats["note_moved"][:5])))
    if stats["note_stuck"]:
        print("          ⚠ 注释无解 %d 条(已取最轻): %s"
              % (len(stats["note_stuck"]),
                 ", ".join(f["text"][:16] for f, _ in stats["note_stuck"][:4])))
    print("  自检④  可见文字不越图框(A4 页边距 %.0fmm)      : %s"
          % (SHEET_M, "✓ 全在框内" if not outof else "✗ 越界 %d 条" % len(outof)))
    for nm, d in outof[:8]:
        print("        %s 越 %s" % (nm, d))
    if VERBOSE:
        print("")
        print("  [明细] 叠加对(文件侧):")
        for a, b in pairs[:40]:
            print("      %s ↔ %s" % (a, b))
        for a, b in onbody[:40]:
            print("      %s 压 %s" % (a, b))
        print("  [明细] 位号/数值避让:")
        for it, k in stats["nudged"][:60]:
            print("      %-8s %-9s 候选位 #%d" % (it["owner"], it["kind"], k))
    print("")
    if mism or not nets_ok or pairs or onbody or outof:
        sys.exit(1)


if __name__ == "__main__":
    main()
