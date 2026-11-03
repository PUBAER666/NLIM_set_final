# -*- coding: utf-8 -*-
"""
gen_kicad.py — 生成 nilm_afe_v0 的 KiCad v5 legacy 原理图工程
输出:
  kicad/nilm_afe_v0.pro
  kicad/nilm_afe_v0.sch
  kicad/nilm_afe_v0-cache.lib
  nilm_afe_v0_kicad.zip  (供 lceda.cn 标准版 文件→导入→KiCad 使用)

电路来源: docs/hardware/afe_v0_schematic_spec.md (V0, 2026-09-23)
"""
import os, zipfile, itertools

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT  = os.path.join(BASE, "kicad")
PROJ = "nilm_afe_v0"
LIBN = PROJ + "-cache"
os.makedirs(OUT, exist_ok=True)

# ---------------- 矩阵 (lib 坐标 -> 图纸坐标, y 向下) ----------------
def M(m, x, y):
    return (m[0]*x + m[1]*y, m[2]*x + m[3]*y)
NAT  = (1, 0, 0, 1)     # 原样
CW   = (0, -1, 1, 0)    # 顺时针 90° (屏幕上)
CCW  = (0, 1, -1, 0)    # 逆时针 90°
R180 = (-1, 0, 0, -1)

# ---------------- 符号库定义 ----------------
# pins: (num, name, x, y, length, orient, etype)
# draw: 原样写入 DRAW 段的行
SYMS = {}

def S(name, defline, fields, draw, pins):
    SYMS[name] = dict(defline=defline, fields=fields, draw=draw, pins=pins)

S("R",
  "DEF R R 0 0 N Y 1 F N",
  [("F0", '"R"', -120, -80, "H V C CNN"), ("F1", '"R"', 120, 90, "H V C CNN")],
  ["S -150 -40 150 40 0 1 10 N"],
  [("1", "~", -250, 0, 100, "L", "P"), ("2", "~", 250, 0, 100, "R", "P")])

S("RV",
  "DEF RV RV 0 0 N Y 1 F N",
  [("F0", '"RV"', -180, -140, "H V C CNN"), ("F1", '"RV"', 180, 140, "H V C CNN")],
  ["S -150 -60 150 60 0 1 10 N",
   "P 3 0 0 10 -25 -95 0 -60 25 -95 N"],
  [("1", "~", -250, 0, 100, "L", "P"),
   ("2", "~", 0, -160, 100, "U", "P"),
   ("3", "~", 250, 0, 100, "R", "P")])

S("C",
  "DEF C C 0 0 N Y 1 F N",
  [("F0", '"C"', -120, -150, "H V C CNN"), ("F1", '"C"', 120, 150, "H V C CNN")],
  ["P 2 0 0 20 -50 -100 -50 100 N",
   "P 2 0 0 20 50 -100 50 100 N"],
  [("1", "~", -250, 0, 200, "L", "P"), ("2", "~", 250, 0, 200, "R", "P")])

S("C_Pol",
  "DEF C_Pol C 0 0 N Y 1 F N",
  [("F0", '"C"', -120, -150, "H V C CNN"), ("F1", '"C"', 120, 150, "H V C CNN")],
  ["P 2 0 0 20 -50 -100 -50 100 N",
   "P 2 0 0 20 50 -100 50 100 F",
   "P 2 0 0 10 -120 -30 -120 30 N",
   "P 2 0 0 10 -150 0 -90 0 N"],
  [("1", "~", -250, 0, 200, "L", "P"), ("2", "~", 250, 0, 200, "R", "P")])

_diode_draw = ["P 4 0 0 10 -100 -100 -100 100 100 0 -100 -100 F",
               "P 2 0 0 20 100 -100 100 100 N"]
S("D",
  "DEF D D 0 0 N Y 1 F N",
  [("F0", '"D"', 0, -160, "H V C CNN"), ("F1", '"D"', 0, 160, "H V C CNN")],
  _diode_draw,
  [("1", "A", -250, 0, 150, "L", "P"), ("2", "K", 250, 0, 150, "R", "P")])

S("LED",
  "DEF LED D 0 0 N Y 1 F N",
  [("F0", '"D"', 0, -220, "H V C CNN"), ("F1", '"D"', 0, 160, "H V C CNN")],
  _diode_draw + ["P 3 0 0 10 40 -130 90 -180 70 -140 N",
                 "P 3 0 0 10 110 -60 160 -110 140 -70 N"],
  [("1", "A", -250, 0, 150, "L", "P"), ("2", "K", 250, 0, 150, "R", "P")])

def _opamp_unit(u, pins):
    d = ["P 4 %d 0 10 -100 -150 -100 150 200 0 -100 -150 N" % u,
         "P 2 %d 0 10 -190 -100 -130 -100 N" % u,
         "P 2 %d 0 10 -190 100 -130 100 N" % u,
         "P 2 %d 0 10 -160 70 -160 130 N" % u]
    p = []
    for num, nm, x, y, ln, o in pins:
        p.append((num, nm, x, y, ln, o, "I" if x < 0 else "O", u))
    return d, p

_d1, _p1 = _opamp_unit(1, [("2", "IN-", -300, -100, 200, "L"),
                           ("3", "IN+", -300, 100, 200, "L"),
                           ("1", "OUT", 300, 0, 100, "R")])
_d2, _p2 = _opamp_unit(2, [("6", "IN-", -300, -100, 200, "L"),
                           ("5", "IN+", -300, 100, 200, "L"),
                           ("7", "OUT", 300, 0, 100, "R")])
_d3 = ["S -100 -100 100 100 3 1 10 N"]
_p3 = [("8", "VDD", 0, -200, 100, "U", "W", 3), ("4", "VSS", 0, 200, 100, "D", "W", 3)]
S("MCP6002",
  "DEF MCP6002 U 0 40 Y Y 3 L N",
  [("F0", '"U"', -350, -250, "H V C CNN"), ("F1", '"MCP6002"', -350, 300, "H V C CNN")],
  _d1 + _d2 + _d3,
  _p1 + _p2 + _p3)

# XKB PJ-320A（立创 C2884926）—— **4 个端子，编号 2/3/4/5，没有 1 脚**
#   落地依据：从 easyeda.com/api 解出的**立创EDA官方封装**（立创C2884926 引用的
#   AUDIO-TH_XKB_PJ-320A），坐标已按脚距 3.000/4.000mm 与图纸尺寸链核对。
#   ⚠ 脚位**功能**未定：图纸示意图显示 5 脚直通本体公共端（大概率 SLEEVE），
#     其余 3 脚要在**上电前用万用表量**（spec §9.1：T-R / T-S / R-S 三组，
#     能量到几十~几百欧的那一对才是 CT 次级）。所以这里刻意**不写死 TIP/RING/SLEEVE**，
#     只按"信号脚 + 3 个备用脚"命名 —— 名字说错了不会误导人。
#   2 脚当信号是**先猜的**；猜错不用割板：3 个备用脚各经一颗 0Ω 到 GND，
#   拆哪一颗都能把那一脚解放出来飞线到 R1。
S("JACK_PJ320A",
  "DEF JACK_PJ320A J 0 40 Y Y 1 F N",
  [("F0", '"J"', 50, -160, "H V C CNN"), ("F1", '"JACK_PJ320A"', 50, 160, "H V C CNN")],
  ["S -100 -100 200 100 0 1 10 N"],
  [("2", "SIG",    300,  -50, 100, "L", "P"),    # → R1（信号）
   ("3", "SPARE1", 100, -250, 150, "D", "P"),    # → R12(0R) → GND
   ("4", "SPARE2",   0, -250, 150, "D", "P"),    # → R15(0R) → GND
   ("5", "SPARE3",-100, -250, 150, "D", "P")])   # → R16(0R) → GND

S("SCREW_2P",
  "DEF SCREW_2P J 0 40 Y Y 1 F N",
  [("F0", '"J"', 0, -160, "H V C CNN"), ("F1", '"SCREW_2P"', 0, 160, "H V C CNN")],
  ["S -100 -100 100 100 0 1 10 N"],
  [("1", "~", 200, -50, 100, "R", "P"),
   ("2", "~", 200, 50, 100, "R", "P")])

S("CONN_2X03",
  "DEF CONN_2X03 J 0 40 Y Y 1 F N",
  [("F0", '"J"', 0, -260, "H V C CNN"), ("F1", '"CONN_2X03"', 0, 260, "H V C CNN")],
  ["S -100 -200 100 200 0 1 10 N"],
  [("1", "~", -200, -150, 100, "L", "P"), ("3", "~", -200, 0, 100, "L", "P"),
   ("5", "~", -200, 150, 100, "L", "P"),
   ("2", "~", 200, -150, 100, "R", "P"), ("4", "~", 200, 0, 100, "R", "P"),
   ("6", "~", 200, 150, 100, "R", "P")])

S("TESTP",
  "DEF TESTP TP 0 40 N Y 1 F N",
  [("F0", '"TP"', 120, 0, "H V C CNN"), ("F1", '"TESTP"', 120, 100, "H V C CNN")],
  ["C 0 100 40 0 1 10 N"],
  [("1", "~", 0, 0, 60, "D", "P")])

def _power_sym(name, pin_net, draw):
    S(name,
      "DEF %s #PWR 0 0 Y Y 1 F P" % name,
      [("F0", '"#PWR"', 0, 200, "H I C CNN"), ("F1", '"%s"' % name, 0, -120, "H V C CNN")],
      draw,
      [("1", pin_net, 0, 0, 0, "U", "W")])

_power_sym("power_GND", "GND",
  ["P 2 0 0 0 0 0 0 50 N",
   "P 2 0 0 0 -75 50 75 50 N",
   "P 2 0 0 0 -40 80 40 80 N",
   "P 2 0 0 0 -12 108 12 108 N"])
_power_sym("power_+3V3", "+3V3",
  ["P 2 0 0 0 0 0 0 -50 N",
   "P 2 0 0 0 -75 -50 75 -50 N"])

# ---------------- 原理图模型 ----------------
comps = []   # (key, ref, sym, value, fp, x, y, mat, unit)
wires = []   # (x1, y1, x2, y2)
labels = []  # (name, x, y, orient)  orient: 0=向右 2=向左
powers = []  # (sym, x, y)
dots = []    # (x, y)
ncs = []     # (x, y)
notes = []   # (x, y, size, text)

def C(key, ref, sym, value, fp, x, y, mat=NAT, unit=1):
    comps.append(dict(key=key, ref=ref, sym=sym, value=value, fp=fp,
                      x=x, y=y, mat=mat, unit=unit))

def P(key, pin):
    c = next(c for c in comps if c["key"] == key)
    s = SYMS[c["sym"]]
    p = next(p for p in s["pins"] if p[0] == str(pin))
    dx, dy = M(c["mat"], p[2], p[3])
    return (c["x"] + dx, c["y"] + dy)

def W(x1, y1, x2, y2):
    assert (x1 == x2) or (y1 == y2), "wire must be orthogonal"
    assert (x1, y1) != (x2, y2), "zero-length wire"
    wires.append((x1, y1, x2, y2))

def WP(*pts):
    for a, b in zip(pts, pts[1:]):
        W(a[0], a[1], b[0], b[1])

def LAB(name, pt, orient=0):
    labels.append((name, pt[0], pt[1], orient))

def PWR_AT(sym, pt):
    powers.append((sym, pt[0], pt[1]))

def DOT(pt):
    dots.append((pt[0], pt[1]))

def NC(pt):
    ncs.append((pt[0], pt[1]))

def NOTE(x, y, size, text):
    notes.append((x, y, size, text))

# 封装名 (文档级, 导入立创EDA后按 BOM 重选)
FP_R    = "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal"
FP_RV   = "Potentiometer_THT:Potentiometer_Bourns_3296W_Vertical"
FP_C    = "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P2.50mm"
FP_CPOL = "Capacitor_THT:CP_Radial_D5.0mm_P2.50mm"
FP_D    = "Diode_THT:D_DO-35_SOD27_P10.16mm_Horizontal"
FP_LED  = "LED_THT:LED_D3.0mm"
FP_DIP8 = "Package_DIP:DIP-8_W7.62mm"
FP_TO220= "Package_TO_SOT_THT:TO-220-3_Vertical"
FP_JACK = "Connector_Audio:AudioJack3.5mm_XKB_PJ-320A_THT"
FP_SCRW = "TerminalBlock:TerminalBlock_bornier-2_P5.08mm"
FP_HDR6 = "Connector_PinHeader_2.54mm:PinHeader_2x03_P2.54mm_Vertical"
FP_SOCK = "Connector_PinSocket_2.54mm:PinSocket_1x02_P2.54mm_Vertical"
FP_TP   = "TestPoint:TestPoint_Pad_D1.5mm"

# ===================== 元件放置 =====================
# --- 电流通道 (U1A), 主信号线 y=2150 ---
C("J1", "J1", "JACK_PJ320A", "3.5mm CT in", FP_JACK, 1100, 2200)
# 2026-10-05: J1 由 3 脚占位换成真件 XKB PJ-320A（4 端子 2/3/4/5）。
#   3 个备用脚各串一颗 0Ω 到 GND —— 信号脚万一是其中任何一个，
#   拆掉对应那颗 0Ω 飞线到 R1 即可，**不用割铜、不用重打板**。
#   代价 2 颗 0Ω（约 0.15 元）；收益是一次打样就能覆盖"CT 信号到底出在哪两脚"的不确定性。
C("R12", "R12", "R", "0R", FP_R, 1200, 1450, CCW)   # J1.SPARE1 -> GND 保险
C("R15", "R15", "R", "0R", FP_R,  900, 1450, CCW)   # J1.SPARE2 -> GND 保险
C("R16", "R16", "R", "0R", FP_R,  650, 1450, CCW)   # J1.SPARE3 -> GND 保险
C("R1", "R1", "R", "1k", FP_R, 2400, 2150)
C("D1", "D1", "D", "1N4148", FP_D, 3850, 2400, CCW)   # 阴极朝节点, 阳极 GND
C("D2", "D2", "D", "1N4148", FP_D, 3850, 1900, CCW)   # 阳极朝节点, 阴极 3V3
C("R2", "R2", "R", "10k", FP_R, 3450, 2150)
C("U1A", "U1", "MCP6002", "MCP6002-I/P", FP_DIP8, 4500, 2250, NAT, 1)
C("RV1", "RV1", "RV", "10k 3296W", FP_RV, 4450, 1750)
C("R10", "R10", "R", "22k", FP_R, 3300, 3200, CCW)
C("RV2", "RV2", "RV", "10k 3296W", FP_RV, 3300, 3750, CW)
C("C1", "C1", "C_Pol", "10uF/16V", FP_CPOL, 3700, 4100, CW)
C("C2", "C2", "C", "100nF", FP_C, 4100, 4100, CW)
C("R3", "R3", "R", "100R", FP_R, 5750, 2250)
C("R4", "R4", "R", "1k", FP_R, 6750, 2250)
C("C4", "C4", "C", "100nF (socket)", FP_SOCK, 7300, 2550, CW)
C("TP1", "TP1", "TESTP", "I_IN", FP_TP, 1400, 2150)
C("TP2", "TP2", "TESTP", "I_VB", FP_TP, 4200, 2350)
C("TP4", "TP4", "TESTP", "I_OUT", FP_TP, 7600, 2250)
C("R13", "R13", "R", "1k", FP_R, 7800, 2500, CW)   # 输出负载电阻 I_OUT->GND (必须有, spec §12)

# --- 电压通道 (U1B), 主信号线 y=4550 ---
C("J2", "J2", "SCREW_2P", "5.08 2P V-in", FP_SCRW, 1100, 4600)
C("R5", "R5", "R", "1k", FP_R, 2400, 4550)
C("D3", "D3", "D", "1N4148", FP_D, 3850, 4800, CCW)
C("D4", "D4", "D", "1N4148", FP_D, 3850, 4300, CCW)
C("R6", "R6", "R", "100k", FP_R, 3450, 4550)
C("U1B", "U1", "MCP6002", "MCP6002-I/P", FP_DIP8, 4500, 4650, NAT, 2)
C("RV3", "RV3", "RV", "10k 3296W", FP_RV, 4450, 4150)
C("R11", "R11", "R", "22k", FP_R, 3300, 5600, CCW)
C("RV4", "RV4", "RV", "10k 3296W", FP_RV, 3300, 6150, CW)
C("C5", "C5", "C_Pol", "10uF/16V", FP_CPOL, 3700, 6500, CW)
C("C6", "C6", "C", "100nF", FP_C, 4100, 6500, CW)
C("R7", "R7", "R", "100R", FP_R, 5750, 4650)
C("R8", "R8", "R", "1k", FP_R, 6750, 4650)
C("C8", "C8", "C", "100nF (socket)", FP_SOCK, 7300, 4950, CW)
C("TP5", "TP5", "TESTP", "V_IN", FP_TP, 1400, 4550)
C("TP6", "TP6", "TESTP", "V_OUT", FP_TP, 7600, 4650)
C("R14", "R14", "R", "1k", FP_R, 7800, 4900, CW)   # 输出负载电阻 V_OUT->GND (必须有, spec §12)
C("TP7", "TP7", "TESTP", "V_VB", FP_TP, 4200, 4750)

# --- U1 电源单元 ---
C("U1P", "U1", "MCP6002", "MCP6002-I/P", FP_DIP8, 4500, 2900, NAT, 3)
C("C3", "C3", "C", "100nF (spare)", FP_C, 4800, 2950, CW)
C("C7", "C7", "C", "100nF (spare)", FP_C, 10100, 6950, CW)

# --- 电源 ---
C("C10", "C10", "C", "100nF", FP_C, 10500, 3700, CW)
C("R9", "R9", "R", "1k", FP_R, 9300, 4800, CCW)
C("D5", "D5", "LED", "LED red 3mm", FP_LED, 9300, 5350, CW)
C("C11", "C11", "C", "100nF", FP_C, 9500, 4300, CW)
C("TP8", "TP8", "TESTP", "3V3", FP_TP, 10700, 3300)
C("TP3", "TP3", "TESTP", "GND", FP_TP, 8300, 5700)

# --- 输出连接器 ---
C("J4", "J4", "CONN_2X03", "2.54 2x03 IF", FP_HDR6, 8600, 1300)

# ===================== 连线: 电流通道 =====================
# J1 的 2 脚 = 先猜的信号脚 -> R1 (I_IN)
WP(P("J1", "2"), (1850, 2150))
WP((1850, 2150), P("R1", "1"))
LAB("I_IN", (1850, 2150), 0); DOT((1850, 2150))
# J1 的 3/4/5 脚 = 备用脚 -> 各串一颗 0Ω -> GND
#   （spec §6.0 的"飞线后路"；三脚各留一颗，猜错哪一脚都救得回来）
#   三条走线各占一条横向通道，互不相交 —— 原理图里"看起来没接"和"真没接"是两回事，
#   并查集会把交叉点当 T 型搭接合并，所以宁可绕也不能让它们交叉。
WP(P("J1", "3"), P("R12", "1"))
PWR_AT("power_GND", P("R12", "2"))
WP(P("J1", "4"), (1100, 1850), (900, 1850), P("R15", "1"))
PWR_AT("power_GND", P("R15", "2"))
WP(P("J1", "5"), (1000, 1900), (650, 1900), P("R16", "1"))
PWR_AT("power_GND", P("R16", "2"))
# R1 -> R2 直连（中间不再挂任何东西）
WP(P("R1", "2"), P("R2", "1"))
# 钳位挂在 R2 之后的「虚拟地」节点 (3850,2150)
# ⚠️ 不能挂在 R1-R2 之间：那里的信号摆幅约 ±1.25V，会被 D1 削掉负峰。
#    仿真实测（tools/sim_afe.py）：挂中间 THD=11.5%、幅度损失 16%；
#    挂虚拟地 THD=0%。虚拟地电位≈偏置电压、信号摆幅≈0，钳位管永不导通，
#    而 CT 开路的高压仍被 R1+R2=11k 限流后由钳位吸收 —— 保护效果不减。
DOT((3850, 2150))
# D1: 阴极(2) 上 -> 节点, 阳极(1) 下 -> GND
PWR_AT("power_GND", P("D1", "1"))
# D2: 阳极(1) 下 -> 节点(直接落在节点上), 阴极(2) 上 -> +3V3
assert P("D2", "1") == (3850, 2150)
PWR_AT("power_+3V3", P("D2", "2"))
# R2 -> U1A.IN-  (直线)
WP(P("R2", "2"), P("U1A", "2"))
# 反馈: IN- 节点 (4000,2150) 上抽头 -> RV1.1
DOT((4000, 2150))
WP((4000, 2150), (4000, 1750), P("RV1", "1"))
# RV1.3 -> (5100,1750);  抽头(2) -> 汇流排 x=5100 -> OUT
WP(P("RV1", "3"), (5100, 1750))
WP(P("RV1", "2"), (4450, 1500), (5100, 1500))
WP((5100, 1500), (5100, 2250))
DOT((5100, 1750))
WP(P("U1A", "1"), (5100, 2250))
DOT((5100, 2250))
# 偏置: +3V3 -> R10 -> RV2 -> GND, 抽头 -> IN+
PWR_AT("power_+3V3", P("R10", "2"))
WP(P("R10", "1"), P("RV2", "1"))
PWR_AT("power_GND", P("RV2", "3"))
WP(P("RV2", "2"), (3900, 3750))
DOT((3900, 3750))
WP((3900, 3750), (3900, 3000))
WP((3900, 3000), (3900, 2350), P("U1A", "3"))
LAB("VBIAS_I", (3900, 3000), 0); DOT((3900, 3000))
# C1/C2 从偏置节点到 GND
WP(P("C1", "1"), (3700, 3750))
DOT((3700, 3750))
PWR_AT("power_GND", P("C1", "2"))
WP(P("C2", "1"), (4100, 3750), (3900, 3750))
PWR_AT("power_GND", P("C2", "2"))
# 输出链: OUT -> R3 -> R4 -> I_OUT
WP((5100, 2250), P("R3", "1"))
WP(P("R3", "2"), P("R4", "1"))
WP(P("R4", "2"), (7300, 2250))
WP((7300, 2250), P("TP4", "1"))
DOT((7300, 2250))
LAB("I_OUT", P("TP4", "1"), 0)
# C4 (排针座) I_OUT -> GND
WP(P("C4", "1"), (7300, 2250))
PWR_AT("power_GND", P("C4", "2"))
# R13: I_OUT -> GND 输出负载电阻 (spec §12.4，必须有)
#   MCP6002 输出摆幅指标是在 10kΩ 负载下标定的；不加负载时输出级近空载
#   （只有 0.5µA），波形底部会出现「鼓包」畸变。C4 是电容，顶不了这个位置。
#   2026-10-02 阻值由 10kΩ 改为 1kΩ (spec §12.9)
assert P("R13", "1") == (7800, 2250), "R13 pin1 必须落在 I_OUT 节点上"
WP(P("TP4", "1"), (7800, 2250))
DOT((7800, 2250))
PWR_AT("power_GND", P("R13", "2"))

# ===================== 连线: 电压通道 =====================
WP(P("J2", "1"), (1850, 4550))
WP((1850, 4550), P("R5", "1"))
LAB("V_IN", (1850, 4550), 0); DOT((1850, 4550))
PWR_AT("power_GND", P("J2", "2"))
WP(P("R5", "2"), P("R6", "1"))
# 钳位挂虚拟地节点 (3850,4550)，同电流通道。挂中间会严重削波
# （电压通道信号大，仿真实测 THD=39%、幅度只剩 19%）
DOT((3850, 4550))
PWR_AT("power_GND", P("D3", "1"))
assert P("D4", "1") == (3850, 4550)
PWR_AT("power_+3V3", P("D4", "2"))
WP(P("R6", "2"), P("U1B", "6"))
DOT((4000, 4550))
WP((4000, 4550), (4000, 4150), P("RV3", "1"))
WP(P("RV3", "3"), (5100, 4150))
WP(P("RV3", "2"), (4450, 3900), (5100, 3900))
WP((5100, 3900), (5100, 4650))
DOT((5100, 4150))
WP(P("U1B", "7"), (5100, 4650))
DOT((5100, 4650))
PWR_AT("power_+3V3", P("R11", "2"))
WP(P("R11", "1"), P("RV4", "1"))
PWR_AT("power_GND", P("RV4", "3"))
WP(P("RV4", "2"), (3900, 6150))
DOT((3900, 6150))
WP((3900, 6150), (3900, 5400))
WP((3900, 5400), (3900, 4750), P("U1B", "5"))
LAB("VBIAS_V", (3900, 5400), 0); DOT((3900, 5400))
WP(P("C5", "1"), (3700, 6150))
DOT((3700, 6150))
PWR_AT("power_GND", P("C5", "2"))
WP(P("C6", "1"), (4100, 6150), (3900, 6150))
PWR_AT("power_GND", P("C6", "2"))
WP((5100, 4650), P("R7", "1"))
WP(P("R7", "2"), P("R8", "1"))
WP(P("R8", "2"), (7300, 4650))
WP((7300, 4650), P("TP6", "1"))
DOT((7300, 4650))
LAB("V_OUT", P("TP6", "1"), 0)
WP(P("C8", "1"), (7300, 4650))
PWR_AT("power_GND", P("C8", "2"))
# R14: V_OUT -> GND 输出负载电阻 (spec §12.4，必须有)
assert P("R14", "1") == (7800, 4650), "R14 pin1 必须落在 V_OUT 节点上"
WP(P("TP6", "1"), (7800, 4650))
DOT((7800, 4650))
PWR_AT("power_GND", P("R14", "2"))

# ===================== 连线: 运放电源 / 备用 =====================
PWR_AT("power_+3V3", P("U1P", "8"))
PWR_AT("power_GND", P("U1P", "4"))
# C3: U1 附近备用去耦 (BOM 6x100nF 之一)
PWR_AT("power_+3V3", P("C3", "1"))
PWR_AT("power_GND", P("C3", "2"))
# U2 已删除（2026-10-04）—— 见上面元件定义处的说明。
# C7: 3V3 备用去耦位（通用，不再依附 U2）
PWR_AT("power_+3V3", P("C7", "1"))
PWR_AT("power_GND", P("C7", "2"))

# ===================== 连线: 电源 =====================
# 2026-10-04: J3 / C9 / U3(LM1117-3.3) / TP9(V5) 已删除。
#   3V3 已定案由 EES-331 J5-35/36 经 J4-5 供入，本板不再自己发电（spec §10 的 V1 改动提前执行）。
#   +3V3 网络靠网络标签汇合（J4-5 处已打 LAB("+3V3")），这里只需把 C10 / TP8 挂上去。
#   TP8 保留：它现在是 3V3 的注入点 —— 万一 EES-331 的 3.3V 不能用，可从台电源直接喂。
PWR_AT("power_+3V3", P("C10", "1"))
PWR_AT("power_GND", P("C10", "2"))
PWR_AT("power_+3V3", P("TP8", "1"))
# LED 指示: +3V3 -> R9 -> D5 -> GND
PWR_AT("power_+3V3", (9300, 4300))
WP((9300, 4300), P("R9", "2"))
WP(P("R9", "1"), P("D5", "1"))
PWR_AT("power_GND", P("D5", "2"))
# C11: U1 pin8 附近去耦
PWR_AT("power_+3V3", P("C11", "1"))
PWR_AT("power_GND", P("C11", "2"))
# TP3 GND 测试焊盘
WP(P("TP3", "1"), (8300, 5900))
PWR_AT("power_GND", (8300, 5900))

# ===================== 连线: J4 输出 =====================
WP(P("J4", "1"), (8200, 1150)); LAB("I_OUT", (8200, 1150), 2)
WP(P("J4", "3"), (8200, 1300)); LAB("V_OUT", (8200, 1300), 2)
WP(P("J4", "5"), (8200, 1450)); LAB("+3V3", (8200, 1450), 2)
for pin in ("2", "4", "6"):
    p = P("J4", pin)
    WP(p, (9000, p[1]))
    PWR_AT("power_GND", (9000, p[1]))

# ===================== 注释 =====================
NOTE(1400, 1500, 60, "Channel I: SCT-013-030 CT (30A/1V) -> inverting attenuator -> I_OUT (0.5V bias, <=1Vpp)")
NOTE(1400, 3900, 60, "Channel V: 9VAC transformer -> inverting attenuator -> V_OUT (0.5V bias, <=1Vpp)")
NOTE(7700, 2200, 60, "Power: +3V3 from EES-331 J5-35 via J4-5 (no on-board regulator)")
NOTE(4700, 1400, 50, "RV1: 25A fullscale @ 1k load -> Rf ~8.8k (see spec 2.3)")
NOTE(3450, 4350, 50, "RV2: tune I_OUT DC = 0.500V")
NOTE(4700, 3800, 50, "RV3: set by measured secondary (~3k)")
NOTE(3450, 6750, 50, "RV4: tune V_OUT DC = 0.500V")
NOTE(6500, 2950, 50, "C4/C8: plug-in socket, default 100nF (anti-alias tune)")
NOTE(6500, 5350, 50, "C3/C7: spare decoupling, DNP ok")

# ===================== 发射: cache.lib =====================
def emit_lib(path):
    L = ["EESchema-LIBRARY Version 2.4", "#encoding utf-8"]
    for name in SYMS:
        s = SYMS[name]
        L += ["#", "# %s" % name, "#", s["defline"]]
        for ftag, ftxt, fx, fy, ffmt in s["fields"]:
            L.append('%s %s %d %d 50 %s' % (ftag, ftxt, fx, fy, ffmt))
        L.append('F2 "" 0 0 50 H I C CNN')
        L.append('F3 "" 0 0 50 H I C CNN')
        L.append("DRAW")
        L += s["draw"]
        for p in s["pins"]:
            (num, nm, x, y, ln, o, et) = p[:7]
            pu = p[7] if len(p) > 7 else 1
            L.append("X %s %s %d %d %d %s 50 50 %d 1 %s" % (nm, num, x, y, ln, o, pu, et))
        L += ["ENDDRAW", "ENDDEF"]
    L.append("#End Library")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L) + "\n")

# ===================== 发射: .sch =====================
_ts = itertools.count(1)

def emit_comp(c, fp_visible=False):
    ts = "65%06X" % next(_ts)
    x, y, m = c["x"], c["y"], c["mat"]
    L = ["$Comp",
         "L %s:%s %s" % (LIBN, c["sym"], c["ref"]),
         "U %d 1 %s" % (c["unit"], ts),
         "P %d %d" % (x, y),
         'F 0 "%s" H %d %d 50  0000 L CNN' % (c["ref"], x + 150, y - 200),
         'F 1 "%s" H %d %d 50  0000 L CNN' % (c["value"], x + 150, y + 220),
         'F 2 "%s" H %d %d 50  0001 C CNN' % (c["fp"], x, y),
         'F 3 "~" H %d %d 50  0001 C CNN' % (x, y),
         "\t1    %d    %d" % (x, y),
         "\t%-5d%-5d%-5d%-5d" % m,
         "$EndComp"]
    return L

def emit_sch(path):
    L = ["EESchema Schematic File Version 4",
         "EELAYER 30 0",
         "EELAYER END",
         "$Descr A4 11693 8268",
         "encoding utf-8",
         "Sheet 1 1",
         'Title "nilm_afe_v0"',
         'Date "2026-09-23"',
         'Rev "V0"',
         'Comp "NILM Project"',
         'Comment1 "AFE V0 analog front end: CT current + transformer voltage"',
         'Comment2 "0.5V midpoint, <=1Vpp, for XADC or AD7606"',
         'Comment3 ""',
         'Comment4 ""',
         "$EndDescr"]
    for c in comps:
        L += emit_comp(c)
    pwr_n = itertools.count(1)
    for (sym, x, y) in powers:
        net = SYMS[sym]["pins"][0][1]
        ref = "#PWR%04d" % next(pwr_n)
        ts = "65%06X" % next(_ts)
        L += ["$Comp",
              "L %s:%s %s" % (LIBN, sym, ref),
              "U 1 1 %s" % ts,
              "P %d %d" % (x, y),
              'F 0 "%s" H %d %d 50  0001 C CNN' % (ref, x, y),
              'F 1 "%s" H %d %d 50  0000 C CNN' % (net, x, y - 140),
              "\t1    %d    %d" % (x, y),
              "\t1    0    0    1    ",
              "$EndComp"]
    for (x, y, size, text) in notes:
        L.append("Text Notes %d %d 0 %d ~ 0" % (x, y, size))
        L.append(text)
    for (x1, y1, x2, y2) in wires:
        L.append("Wire Wire Line")
        L.append("\t%d %d %d %d" % (x1, y1, x2, y2))
    for (x, y) in dots:
        L.append("Connection ~ %d %d" % (x, y))
    for (name, x, y, o) in labels:
        L.append("Text Label %d %d %d 50 ~ 0" % (x, y, o))
        L.append(name)
    for (x, y) in ncs:
        L.append("NoConn ~ %d %d" % (x, y))
    L.append("$EndSCHEMATC")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L) + "\n")

# ===================== 发射: .pro =====================
def emit_pro(path):
    L = ["update=23/09/2026 20:00:00",
         "version=1",
         "last_client=kicad",
         "[general]",
         "version=1",
         "RootSch=",
         "BoardNm=",
         "[eeschema]",
         "version=1",
         "LibDir=",
         "[eeschema/libraries]",
         "LibName1=%s" % LIBN,
         "[schematic_editor]",
         "version=1",
         "PageLayoutDescrFile=",
         "PlotDirectoryName=",
         "SubpartIdSeparator=0",
         "SubpartFirstId=65",
         "NetFmtName=",
         "SpiceAjustPassiveValues=0",
         "LabSize=50",
         "ERC_TestSimilarLabels=1"]
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L) + "\n")

emit_lib(os.path.join(OUT, LIBN + ".lib"))
emit_sch(os.path.join(OUT, PROJ + ".sch"))
emit_pro(os.path.join(OUT, PROJ + ".pro"))

zip_path = os.path.join(BASE, PROJ + "_kicad.zip")
with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
    for fn in (PROJ + ".pro", PROJ + ".sch", LIBN + ".lib"):
        z.write(os.path.join(OUT, fn), arcname=fn)

print("comps=%d wires=%d labels=%d powers=%d dots=%d ncs=%d notes=%d"
      % (len(comps), len(wires), len(labels), len(powers), len(dots), len(ncs), len(notes)))
print("written:", OUT)
print("zip:", zip_path)
