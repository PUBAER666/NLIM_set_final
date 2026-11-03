# -*- coding: utf-8 -*-
"""
schematic_dsl —— 用 matplotlib 手搓「标准电路符号」的最小 DSL

为什么不用 schemdraw
-------------------
schemdraw 的自动布局对模拟电路（运放 + 反馈 + 偏置支路）画出来的位置
很不听话，而且它自己有 4 个大坑（无方向元素继承 theta 导致框歪、
Rect 没有锚点、Ground 向下延伸 0.7 单位会撞线、Label 无文本偏移）。
手搓符号虽然土，但**坐标完全可控**，且零依赖（只要 matplotlib）。

三条设计约定（重要，别破坏）
---------------------------
1. **每个符号函数都返回它的引脚坐标。** 调用方一律用返回值连线，
   不要手写坐标。这样引脚和导线不可能对不上。
   （这条是踩过坑才加的：老版本里运放符号画在 yc±hh，导线却接在
   别的 y 上，差 6 个单位，肉眼看不出来但电气上就是没接上。）

2. **符号内部引线用 `_seg()` 画，不注册导线端点；只有公开的 `wire()`
   才注册。** 否则符号自己的引线会把引脚检查变成永真，自检就废了。

3. **每个符号的引脚通过 `_pin()` 注册。** 画完整张图调 `audit()`，
   它会报出「没有任何导线或其它引脚接触」的引脚 —— 漏连和坐标对不上
   的捕手。

坐标系
------
y 轴向上（matplotlib 默认），与图纸习惯一致。符号几何尺寸是**图纸单位**，
不是毫米 —— 整张图靠 `axis_setup()` 定的 xlim/ylim 缩放。

用法
----
    from schematic_dsl import *

    def draw_my_circuit(ax):
        r = res_h(ax, 10, 30, 50, 'R1', '10k')      # 返回引脚
        wire(ax, [r['a'], (40, 50)])
        ...

    render(draw_my_circuit, 'out.png', title='我的电路',
           xlim=(0, 80), ylim=(0, 100))
    audit()      # 一定要跑，看有没有悬空引脚
"""

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Polygon, Circle

# 中文字体，否则标题和注释全是方框
plt.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'SimHei', 'sans-serif']
plt.rcParams['axes.unicode_minus'] = False   # 否则负号显示成方框

# ---------------- 样式常量 ----------------
CLR = '#1a1a1a'   # 主线：黑
RED = '#c62828'   # 强调色，用于标出「与参考通道不同之处」
BLU = '#1565c0'   # 次要标注：引脚名、器件型号
LW = 1.8          # 线宽
FS = 11.0         # 位号字号
FSV = 10.5        # 取值字号

# ---------------- 引脚 / 导线登记表（供 audit 用） ----------------
_PINS = []      # [(x, y, name)]
_WIRES = []     # [(x1, y1, x2, y2)]
_AUDIT_ON = True


def audit_off():
    """关掉登记（画超复杂图嫌慢时可关，但就别指望 audit 了）。"""
    global _AUDIT_ON
    _AUDIT_ON = False


def _pin(x, y, name):
    if _AUDIT_ON:
        _PINS.append((float(x), float(y), name))


def _seg(ax, pts, c=CLR, lw=LW):
    """画折线，**不**登记。符号内部引线用这个。"""
    ax.plot([p[0] for p in pts], [p[1] for p in pts], color=c, lw=lw,
            solid_capstyle='round', solid_joinstyle='round', zorder=2)


def wire(ax, pts, c=CLR, lw=LW):
    """画折线，并登记每一段。**用户连线一律用这个。**"""
    _seg(ax, pts, c, lw)
    if _AUDIT_ON:
        for p, q in zip(pts, pts[1:]):
            _WIRES.append((float(p[0]), float(p[1]), float(q[0]), float(q[1])))


def dot(ax, x, y, c=CLR):
    """电气连接点（实心圆）。两根线交叉**没有**这个点就是没连。"""
    ax.add_patch(Circle((x, y), 1.3, color=c, zorder=6))


def txt(ax, x, y, s, c=CLR, fs=FS, ha='center', va='center'):
    ax.text(x, y, s, color=c, fontsize=fs, ha=ha, va=va, zorder=7)


# ---------------- 电源符号 ----------------
def gnd(ax, x, y, c=CLR):
    """接地。引脚在 (x, y)，符号向下延伸。"""
    _seg(ax, [(x, y), (x, y - 4.5)], c)
    for i, w in enumerate([11.0, 6.5, 2.4]):
        yy = y - 4.5 - i * 2.8
        ax.plot([x - w / 2, x + w / 2], [yy, yy], color=c, lw=LW, zorder=2)
    _pin(x, y, 'GND')
    return (x, y)


def vcc(ax, x, y, label, c=CLR, h=6.0, half_w=4.5, label_dy=None):
    """电源轨符号（一根短竖线 + 横杠 + 名字）。引脚在 (x, y)。

    h        —— 竖线长度。默认 6；想画得紧凑（比如被上方信号线挤住）就调小
    label_dy —— 文字相对引脚的偏移。默认 h+5
    """
    _seg(ax, [(x, y), (x, y + h)], c)
    ax.plot([x - half_w, x + half_w], [y + h, y + h], color=c, lw=LW, zorder=2)
    txt(ax, x, y + (h + 5 if label_dy is None else label_dy), label, c)
    _pin(x, y, label or 'VCC')
    return (x, y)


# ---------------- 分立元件 ----------------
def res_h(ax, x1, x2, y, ref, val, c=CLR):
    """水平电阻。返回 {'a': 左引脚, 'b': 右引脚}。"""
    bh = 6.0
    bw = min(13.0, (x2 - x1) - 6)
    cx = (x1 + x2) / 2.0
    ax.add_patch(Rectangle((cx - bw / 2, y - bh / 2), bw, bh, fill=False,
                           ec=c, lw=LW, zorder=3))
    _seg(ax, [(x1, y), (cx - bw / 2, y)], c)
    _seg(ax, [(cx + bw / 2, y), (x2, y)], c)
    txt(ax, cx, y + 8, ref, c)
    txt(ax, cx, y - 8.5, val, c, FSV)
    _pin(x1, y, f'{ref}.a'); _pin(x2, y, f'{ref}.b')
    return {'a': (x1, y), 'b': (x2, y)}


def var_res_h(ax, x1, x2, y, ref, val, c=CLR):
    """水平可变电阻（斜箭头穿过）。返回两个引脚。"""
    bw, bh = 15.0, 6.0
    cx = (x1 + x2) / 2.0
    ax.add_patch(Rectangle((cx - bw / 2, y - bh / 2), bw, bh, fill=False,
                           ec=c, lw=LW, zorder=3))
    _seg(ax, [(x1, y), (cx - bw / 2, y)], c)
    _seg(ax, [(cx + bw / 2, y), (x2, y)], c)
    ax.annotate('', xy=(cx + bw / 2 - 0.5, y + bh / 2 + 5.5),
                xytext=(cx - bw / 2 + 0.5, y - bh / 2 - 5.5),
                arrowprops=dict(arrowstyle='-|>', color=c, lw=1.5,
                                mutation_scale=14))
    txt(ax, cx, y + 16, ref, c)
    txt(ax, cx, y + 9, val, c, FSV)
    _pin(x1, y, f'{ref}.a'); _pin(x2, y, f'{ref}.b')
    return {'a': (x1, y), 'b': (x2, y)}


def res_v(ax, x, y1, y2, ref, val, c=CLR, side='left'):
    """垂直电阻，y1 在下、y2 在上。返回两个引脚。"""
    bw, bh = 6.0, 14
    cy = (y1 + y2) / 2.0
    ax.add_patch(Rectangle((x - bw / 2, cy - bh / 2), bw, bh, fill=False,
                           ec=c, lw=LW, zorder=3))
    _seg(ax, [(x, y1), (x, cy - bh / 2)], c)
    _seg(ax, [(x, cy + bh / 2), (x, y2)], c)
    off = 9 if side == 'right' else -9
    ha = 'left' if side == 'right' else 'right'
    txt(ax, x + off, cy + 5, ref, c, ha=ha)
    txt(ax, x + off, cy - 6.5, val, c, FSV, ha=ha)
    _pin(x, y1, f'{ref}.a'); _pin(x, y2, f'{ref}.b')
    return {'a': (x, y1), 'b': (x, y2)}


def pot_v(ax, x, y1, y2, ref, val, c=CLR, side='left', wiper=1):
    """垂直电位器。返回 {'a': 下端, 'b': 上端, 'w': 抽头}。

    wiper=1 箭头朝右，-1 朝左。
    """
    bw, bh = 6.0, 16
    cy = (y1 + y2) / 2.0
    ax.add_patch(Rectangle((x - bw / 2, cy - bh / 2), bw, bh, fill=False,
                           ec=c, lw=LW, zorder=3))
    _seg(ax, [(x, y1), (x, cy - bh / 2)], c)
    _seg(ax, [(x, cy + bh / 2), (x, y2)], c)
    tip = x + 13 * wiper
    _seg(ax, [(tip, cy), (x + (bw / 2 + 2) * wiper, cy)], c)
    ax.add_patch(Polygon([[x + (bw / 2 + 2) * wiper, cy],
                          [x + (bw / 2 + 6.5) * wiper, cy + 2.4],
                          [x + (bw / 2 + 6.5) * wiper, cy - 2.4]],
                         closed=True, color=c, zorder=4))
    off = 9 if side == 'right' else -9
    ha = 'left' if side == 'right' else 'right'
    txt(ax, x + off, cy + 8, ref, c, ha=ha)
    txt(ax, x + off, cy - 9.5, val, c, FSV, ha=ha)
    _pin(x, y1, f'{ref}.a'); _pin(x, y2, f'{ref}.b'); _pin(tip, cy, f'{ref}.w')
    return {'a': (x, y1), 'b': (x, y2), 'w': (tip, cy)}


def diode_v(ax, x, y_bot, y_top, ref, c=CLR, up=True, side='right'):
    """垂直二极管。up=True 表示阳极在下、阴极在上（三角朝上）。
    返回 {'a': 阳极, 'k': 阴极}。
    """
    cy = (y_bot + y_top) / 2.0
    h = 7.0
    if up:
        base_y, tip_y = cy - h / 2, cy + h / 2
    else:
        base_y, tip_y = cy + h / 2, cy - h / 2
    ax.add_patch(Polygon([[x - 5.0, base_y], [x + 5.0, base_y], [x, tip_y]],
                         closed=True, fill=False, ec=c, lw=LW, zorder=3))
    ax.plot([x - 5.0, x + 5.0], [tip_y, tip_y], color=c, lw=LW, zorder=3)
    lo, hi = min(base_y, tip_y), max(base_y, tip_y)
    _seg(ax, [(x, y_bot), (x, lo)], c)
    _seg(ax, [(x, hi), (x, y_top)], c)
    off = 9 if side == 'right' else -9
    ha = 'left' if side == 'right' else 'right'
    txt(ax, x + off, cy, ref, c, ha=ha)
    if up:
        _pin(x, y_bot, f'{ref}.a'); _pin(x, y_top, f'{ref}.k')
    else:
        _pin(x, y_bot, f'{ref}.k'); _pin(x, y_top, f'{ref}.a')
    return {'a': (x, y_bot if up else y_top), 'k': (x, y_top if up else y_bot)}


def cap_v(ax, x, y1, y2, ref='', val='', c=CLR, side='right'):
    """垂直无极性电容。返回两个引脚。ref/val 传空串则不标注。"""
    cy = (y1 + y2) / 2.0
    gap = 3.0
    ax.plot([x - 6, x + 6], [cy + gap / 2] * 2, color=c, lw=LW * 1.6, zorder=3)
    ax.plot([x - 6, x + 6], [cy - gap / 2] * 2, color=c, lw=LW * 1.6, zorder=3)
    _seg(ax, [(x, y1), (x, cy + gap / 2)], c)
    _seg(ax, [(x, cy - gap / 2), (x, y2)], c)
    off = 9 if side == 'right' else -9
    ha = 'left' if side == 'right' else 'right'
    if ref:
        txt(ax, x + off, cy + 7, ref, c, ha=ha)
    if val:
        txt(ax, x + off, cy - 7.5, val, c, FSV, ha=ha)
    _pin(x, y1, f'{ref or "C"}.a'); _pin(x, y2, f'{ref or "C"}.b')
    return {'a': (x, y1), 'b': (x, y2)}


def led_down(ax, x, y_top, y_bot, ref, c=CLR):
    """发光二极管，阴极朝下。返回 {'a': 上, 'k': 下}。"""
    cy = (y_top + y_bot) / 2.0
    h = 7.0
    ax.add_patch(Polygon([[x - 5.0, cy + h / 2], [x + 5.0, cy + h / 2], [x, cy - h / 2]],
                         closed=True, fill=False, ec=c, lw=LW, zorder=3))
    ax.plot([x - 5.0, x + 5.0], [cy - h / 2, cy - h / 2], color=c, lw=LW, zorder=3)
    _seg(ax, [(x, y_top), (x, cy + h / 2)], c)
    _seg(ax, [(x, cy - h / 2), (x, y_bot)], c)
    for dx, dy in [(8, 7), (11.5, 10.5)]:
        ax.annotate('', xy=(x + dx, cy + dy), xytext=(x + dx - 3.2, cy + dy - 3.2),
                    arrowprops=dict(arrowstyle='->', color=c, lw=1.4))
    txt(ax, x + 15, cy, ref, c, ha='left')
    _pin(x, y_top, f'{ref}.a'); _pin(x, y_bot, f'{ref}.k')
    return {'a': (x, y_top), 'k': (x, y_bot)}


# ---------------- 集成电路 / 接插件 ----------------
def opamp(ax, xl, yc, hh, ref, c=CLR, aspect=3.2):
    """运放三角形。xl = 左边（引脚所在边）的 x，yc = 中心线，hh = 半高。

    ⚠️ **引脚就在三角形的左上角 / 左下角**：(xl, yc+hh) 是反相输入（−），
    (xl, yc-hh) 是同相输入（+）。返回值必须拿来连线，不要自己写坐标。

    返回 {'in-': ..., 'in+': ..., 'out': ...}
    """
    xr = xl + hh * aspect
    ax.add_patch(Polygon([[xl, yc - hh], [xl, yc + hh], [xr, yc]],
                         closed=True, fill=False, ec=c, lw=LW, zorder=3))
    txt(ax, xl + hh * 0.75, yc + hh * 0.45, '−', c, FS + 4)
    txt(ax, xl + hh * 0.75, yc - hh * 0.45, '+', c, FS + 4)
    txt(ax, xl + hh * 0.35, yc + hh + 8, ref, c, ha='left')
    p_inm, p_inp, p_out = (xl, yc + hh), (xl, yc - hh), (xr, yc)
    _pin(*p_inm, f'{ref}.in-'); _pin(*p_inp, f'{ref}.in+'); _pin(*p_out, f'{ref}.out')
    return {'in-': p_inm, 'in+': p_inp, 'out': p_out}


def connector(ax, x, y, w, h, ref, pins, c=CLR, side='right'):
    """接插件方框。pins = [(相对 y 的偏移, 引脚名), ...]。

    引脚名画在方框外，**返回 {引脚名: 绝对坐标}** —— 连线用返回值，
    不要用方框边界坐标（那是老版本的坑：导线接在框上而不是引脚端）。
    """
    ax.add_patch(Rectangle((x, y), w, h, fill=False, ec=c, lw=LW, zorder=3))
    txt(ax, x + w / 2, y + h + 7, ref, c)
    out = {}
    sgn = 1 if side == 'right' else -1
    for dy, name in pins:
        px = x + w + 6 * sgn if side == 'right' else x - 6
        _seg(ax, [(x + w if side == 'right' else x, y + dy), (px, y + dy)], c)
        txt(ax, x + w + 1 if side == 'right' else x - 1, y + dy + 5.5, name,
            BLU, FSV - 2, ha='left' if side == 'right' else 'right')
        _pin(px, y + dy, f'{ref}.{name}')
        out[name] = (px, y + dy)
    return out


def block(ax, x, y, w, h, ref, label='', c=CLR):
    """通用功能块方框（比如 LDO 这类不好画成标准符号的器件）。
    引脚由调用方自己画并连线 —— 这里不注册引脚。
    """
    ax.add_patch(Rectangle((x, y), w, h, fill=False, ec=c, lw=LW, zorder=3))
    txt(ax, x + w / 2, y + h + 6, ref, c)
    if label:
        txt(ax, x + w / 2, y + h / 2, label, BLU, FSV - 1.5)
    return (x, y, w, h)


# ---------------- 画布 / 出图 ----------------
def axis_setup(ax, xlim, ylim):
    ax.set_xlim(*xlim)
    ax.set_ylim(*ylim)
    ax.set_aspect('equal')   # 必须，否则电阻会画成拉伸的
    ax.axis('off')


def render(draw_fn, path, title=None, xlim=None, ylim=None,
           figsize=(20, 12), dpi=130, title_size=20):
    """画一张图并保存。draw_fn(ax) 只负责画，画布参数在这里给。"""
    fig, ax = plt.subplots(figsize=figsize)
    draw_fn(ax)
    if xlim and ylim:
        axis_setup(ax, xlim, ylim)
    if title:
        ax.set_title(title, fontsize=title_size, pad=14)
    fig.tight_layout()
    fig.savefig(path, dpi=dpi, facecolor='white')
    plt.close(fig)
    return path


# ---------------- 自检 ----------------
def _pt_seg_dist(px, py, x1, y1, x2, y2):
    """点到线段的最短距离。T 型搭接（引脚落在导线中段）要能被认出来。"""
    dx, dy = x2 - x1, y2 - y1
    L2 = dx * dx + dy * dy
    if L2 == 0:
        return ((px - x1) ** 2 + (py - y1) ** 2) ** 0.5
    t = max(0.0, min(1.0, ((px - x1) * dx + (py - y1) * dy) / L2))
    cx, cy = x1 + t * dx, y1 + t * dy
    return ((px - cx) ** 2 + (py - cy) ** 2) ** 0.5


def audit(tol=0.05, verbose=True):
    """检查每个注册引脚是否被导线或另一个引脚接触。

    这是这个 DSL 存在的主要理由：**符号函数返回引脚坐标**只能保证
    "调用方拿到了正确坐标"，不能保证"调用方真的用了它"。audit 才能。

    ⚠️ audit 通过 ≠ 画对了。它只验证「引脚都被接上了」，
    **不验证接法是否符合你的设计意图** —— 后者要靠拿设计文档逐条核对。

    返回悬空引脚列表 [(x, y, name), ...]。
    """
    floating = []
    for (px, py, name) in _PINS:
        hit = False
        for (x1, y1, x2, y2) in _WIRES:
            if ((px - x1) ** 2 + (py - y1) ** 2) ** 0.5 <= tol or \
               ((px - x2) ** 2 + (py - y2) ** 2) ** 0.5 <= tol:
                hit = True
                break
            if _pt_seg_dist(px, py, x1, y1, x2, y2) <= tol:
                hit = True      # T 型搭接
                break
        if not hit:
            for (qx, qy, qname) in _PINS:
                if qname != name and ((px - qx) ** 2 + (py - qy) ** 2) ** 0.5 <= tol:
                    hit = True  # 引脚对引脚直接相连
                    break
        if not hit:
            floating.append((px, py, name))

    if verbose:
        print(f'引脚 {len(_PINS)} 个，导线 {len(_WIRES)} 段，'
              f'悬空 {len(floating)} 个')
        for (x, y, n) in floating:
            print(f'  [悬空] {n:>14s}  在 ({x:g}, {y:g})')
    return floating


def audit_reset():
    """清空登记表，画下一张图前调用。"""
    _PINS.clear()
    _WIRES.clear()
