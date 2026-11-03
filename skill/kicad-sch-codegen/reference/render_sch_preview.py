# -*- coding: utf-8 -*-
"""
render_sch_preview.py — 把生成的 .kicad_sch 画成布局预览图(纯几何, 非最终渲染)

用途:
  嘉立创/KiCad 里没有的"我能不能先看一眼"环节。它独立解析 .kicad_sch
  (库符号 + 实例 + 导线 + 结点 + 文字), 按 KiCad 的变换公式画到 A4 画布上,
  用文字包围盒(与生成器同一套估算)把每段文字框出来 —— 于是"有没有压字/文字飞太远"
  可以肉眼验收, 不必等导入。

用法:
  python tools/render_sch_preview.py [-o 输出.png]
输出:
  lceda_pro/preview_layout.png
"""
import io, os, sys, math
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Circle, Polygon

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import gen_kicad6_sch as G          # 只借它的 s-expr 解析与文字度量

SCH = os.path.join(BASE, "lceda_pro", G.PROJ + ".kicad_sch")
OUT = os.path.join(BASE, "lceda_pro", "preview_layout.png")
A4 = (297.0, 210.0)


def find(node, key):
    return next((a for a in node if isinstance(a, list) and a and a[0] == key), None)


def xform(at, lx, ly):
    """库坐标(y 向上) -> 图纸坐标(mm, y 向下)"""
    x, y = float(at[1]), float(at[2])
    th = math.radians(float(at[3]) if len(at) > 3 else 0)
    c, s = round(math.cos(th)), round(math.sin(th))
    return (x + c * lx - s * ly, y - s * lx - c * ly)


def main():
    doc = G.sexpr(io.open(SCH, encoding="utf-8").read())
    zoom = None
    for a in sys.argv[1:]:
        if a.startswith("--zoom"):
            v = a.split("=", 1)[1] if "=" in a else sys.argv[sys.argv.index(a) + 1]
            zoom = [float(t) for t in v.replace(",", " ").split()]

    # 1) 库符号 -> unit -> 图元
    libsec = find(doc, "lib_symbols")
    libs = {}
    for sym in libsec[1:]:
        if not (isinstance(sym, list) and sym and sym[0] == "symbol"):
            continue
        name = sym[1].split(":")[-1]
        units = {}
        for sub in sym:
            if not (isinstance(sub, list) and sub and sub[0] == "symbol"):
                continue
            u = int(sub[1].rsplit("_", 2)[1])
            for e in sub:
                if not isinstance(e, list) or not e:
                    continue
                if e[0] == "rectangle":
                    st, en = find(e, "start"), find(e, "end")
                    units.setdefault(u, []).append(
                        ("rect", (float(st[1]), float(st[2]), float(en[1]), float(en[2]))))
                elif e[0] == "polyline":
                    pts = [(float(p[1]), float(p[2])) for p in find(e, "pts")[1:]]
                    units.setdefault(u, []).append(("poly", pts))
                elif e[0] == "circle":
                    ce, r = find(e, "center"), find(e, "radius")
                    units.setdefault(u, []).append(
                        ("circ", (float(ce[1]), float(ce[2]), float(r[1]))))
        libs[name] = units

    fig, ax = plt.subplots(figsize=(A4[0] / 25.4 * 1.6, A4[1] / 25.4 * 1.6), dpi=140)
    ax.set_xlim(0, A4[0])
    ax.set_ylim(A4[1], 0)          # y 向下
    ax.add_patch(Rectangle((5, 5), A4[0] - 10, A4[1] - 10, fill=False, ec="#bbbbbb", lw=0.6))

    # 2) 导线 / 结点
    for node in doc:
        if not isinstance(node, list) or not node:
            continue
        if node[0] == "wire":
            pts = [(float(p[1]), float(p[2])) for p in find(node, "pts")[1:]]
            ax.plot([p[0] for p in pts], [p[1] for p in pts], c="#1a7f37", lw=0.9, zorder=2)
        elif node[0] == "junction":
            at = find(node, "at")
            ax.add_patch(Circle((float(at[1]), float(at[2])), 0.35, color="#1a7f37", zorder=3))

    # 3) 元件图形
    for node in doc:
        if not (isinstance(node, list) and node and node[0] == "symbol"):
            continue
        lib = find(node, "lib_id")[1].split(":")[-1]
        at = find(node, "at")
        unit = int(find(node, "unit")[1])
        for u, items in libs.get(lib, {}).items():
            if u not in (0, unit):
                continue
            for kind, dat in items:
                if kind == "rect":
                    x1, y1, x2, y2 = dat
                    p = [xform(at, x1, y1), xform(at, x2, y1), xform(at, x2, y2), xform(at, x1, y2)]
                    ax.add_patch(Polygon(p, closed=True, fill=False, ec="#8b1a1a", lw=1.1, zorder=4))
                elif kind == "poly":
                    p = [xform(at, a, b) for (a, b) in dat]
                    ax.plot([q[0] for q in p], [q[1] for q in p], c="#8b1a1a", lw=1.1, zorder=4)
                else:
                    cxp, cyp, r = dat
                    cx, cy = xform(at, cxp, cyp)
                    ax.add_patch(Circle((cx, cy), r, fill=False, ec="#8b1a1a", lw=1.1, zorder=4))

    # 4) 文字(注释/标签/位号/数值)
    n_txt = 0
    for node in doc:
        if not isinstance(node, list) or not node:
            continue
        entries = []
        if node[0] in ("text", "label"):
            entries = [(node[1], find(node, "at"), find(node, "effects"))]
        elif node[0] == "symbol":
            for sub in node:
                if isinstance(sub, list) and sub[0] == "property" and sub[1] in ("Reference", "Value"):
                    entries.append((sub[2], find(sub, "at"), find(sub, "effects")))
        for (txt, at, eff) in entries:
            size, just, hide = G.eff_info(eff)
            if hide or at is None:
                continue
            x, y = float(at[1]), float(at[2])
            b = G.tbox(txt, x, y, size, just)
            n_txt += 1
            ax.add_patch(Rectangle((b[0], b[1]), b[2] - b[0], b[3] - b[1],
                                   fill=True, fc="#eef4ff", ec="#4477cc", lw=0.4,
                                   alpha=0.85, zorder=5))
            ax.text(b[0] + 0.15, (b[1] + b[3]) / 2.0, txt, fontsize=2.6,
                    va="center", ha="left", color="#10306b", zorder=6)

    ax.set_title("nilm_afe_v0.kicad_sch layout preview (%d texts) — 仅供布局验收" % n_txt,
                 fontsize=7)
    ax.set_xticks([])
    ax.set_yticks([])
    if zoom:
        ax.set_xlim(zoom[0], zoom[2])
        ax.set_ylim(zoom[3], zoom[1])
    fig.tight_layout()
    out = OUT if not zoom else OUT.replace(".png", "_zoom.png")
    fig.savefig(out, dpi=140)
    print("预览已生成: %s  (文字 %d 条%s)"
          % (os.path.relpath(out, BASE), n_txt, ", 放大区" if zoom else ""))


if __name__ == "__main__":
    main()
