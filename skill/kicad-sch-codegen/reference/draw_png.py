# -*- coding: utf-8 -*-
"""AFE V0 原理图 —— 出三张给人看的 PNG

符号库在 `schematic_dsl.py`（可复用，与电路无关）。
本文件只放**这块板子特有**的东西：元件表 CH_I / CH_V、画法 draw_*、坐标。

换一个电路：把 schematic_dsl.py 一起拷过去，重写下面的 draw_* 即可。

跑：
    PYTHONUTF8=1 D:\\Dev\\Python314\\python.exe draw_schematic.py

输出到 ./img/。脚本末尾会自动跑 audit() 检查有没有悬空引脚。
"""
import os

from schematic_dsl import *   # noqa: F401,F403  —— DSL 的用法就是星号导入

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'img')
os.makedirs(OUT, exist_ok=True)

X_LIM, Y_LIM = (-4, 208), (18, 156)     # 两张通道图共用的画布范围


# ================= 一个通道（电流 / 电压共用同一套画法） =================
def draw_channel(ax, p, hot=None):
    """p = 元件位号表；hot = 要标红的位号集合（用来突出两通道的差异）。"""
    def col(ref):
        return RED if (hot and ref in hot) else CLR

    Y_SIG, Y_OPC, Y_BI, Y_FB, Y_RAIL = 90, 84, 46, 124, 68

    # ---- 输入连接器 ----
    if p.get('jack'):
        # 3.5mm 立体声母座：TIP 走信号，RING / SLEEVE 都接地
        jp = connector(ax, 2, 70, 8, 30, p['J'],
                       [(20, 'TIP'), (10, 'RING'), (2, 'SLEEVE')], side='right')
        # 接到 R1 的引脚（14, Y_SIG），不是接到 16 —— 连接器的引脚端点在
        # x+w+6=16，而 R1 从 14 起，写 (16, Y_SIG) 会变成零长度线段
        wire(ax, [jp['TIP'], (14, Y_SIG)])
        wire(ax, [jp['RING'], (16, 80), (16, 78)])
        res_v(ax, 16, 58, 78, p['Rz'], '0Ω', side='right')   # 0Ω：RING 到地
        wire(ax, [(16, 58), (16, 46)])
        dot(ax, 16, 46)
        wire(ax, [jp['SLEEVE'], (12, 72), (12, 46), (16, 46)])
        gnd(ax, 16, 46)
    else:
        # 5.08mm 螺钉端子：2 脚
        jp = connector(ax, 2, 74, 8, 26, p['J'],
                       [(16, 'SIG'), (6, 'GND')], side='right')
        wire(ax, [jp['SIG'], (14, Y_SIG)])
        wire(ax, [jp['GND'], (14, 80)])
        gnd(ax, 14, 80)

    # ---- 保护电阻 + 输入电阻 ----
    res_h(ax, 14, 28, Y_SIG, p['Rp'], '1k', col(p['Rp']))
    wire(ax, [(28, Y_SIG), (32, Y_SIG)])
    res_h(ax, 32, 46, Y_SIG, p['Ri'], p['Riv'], col(p['Ri']))

    # ---- 主线 ----
    wire(ax, [(46, Y_SIG), (84, Y_SIG)])
    dot(ax, 66, Y_SIG)
    dot(ax, 74, Y_SIG)

    # ---- 钳位二极管：把输入夹在 GND 与 3V3 之间 ----
    wire(ax, [(66, Y_SIG), (66, 94)])
    diode_v(ax, 66, 94, 101, p['D1'], side='right')      # 阳极在下、阴极朝 3V3
    wire(ax, [(66, 101), (66, 104)])
    vcc(ax, 66, 104, '3V3')

    wire(ax, [(66, Y_SIG), (66, 86)])
    diode_v(ax, 66, 79, 86, p['D2'], side='right')       # 阳极朝 GND、阴极在上
    wire(ax, [(66, 79), (66, 76)])
    gnd(ax, 66, 76)

    # ---- 运放 ----
    # hh=6 不是随便取的：引脚在三角形左上/左下角，即 (90, 84±6) = (90,90)/(90,78)。
    # 这两个 y 正好是主信号线和偏置线的电平，所以导线是直的、不用绕。
    # 老版本写 hh=12（引脚在 96/72），但导线画在 90/78 —— 差了 6 个单位，
    # 图上看着像接上了，实际没接到引脚。改 hh 到 6 就对齐了。
    op = opamp(ax, 90, Y_OPC, 6, p['U'])
    wire(ax, [(84, Y_SIG), op['in-']])

    # ---- 反馈：可变电阻从输出接回反相输入 ----
    wire(ax, [op['out'], (122, Y_OPC)])
    dot(ax, 122, Y_OPC)
    wire(ax, [(122, Y_OPC), (122, Y_FB), (107, Y_FB)])
    var_res_h(ax, 89, 107, Y_FB, p['RVg'], '10k', col(p['RVg']))
    wire(ax, [(89, Y_FB), (74, Y_FB), (74, Y_SIG)])

    # ---- 偏置支路：3V3 → R → 电位器 → GND，抽头给同相输入 ----
    # ⚠️ 3V3 顶端必须低于主信号线 y=90，否则引线会与信号线交叉短路
    vcc(ax, 54, 78, '3V3', h=4.0, label_dy=8.5)
    res_v(ax, 54, 58, 78, p['Rb'], '22k', c=col(p['Rb']), side='left')
    pv = pot_v(ax, 54, 34, 58, p['RVb'], '10k', col(p['RVb']), side='left')
    gnd(ax, 54, 34)
    wire(ax, [pv['w'], (88, Y_BI), (88, op['in+'][1]), op['in+']])

    # 抽头上的两个去耦电容
    dot(ax, 72, Y_BI)
    cap_v(ax, 72, 36, Y_BI, '', '')
    txt(ax, 64, 58, p['Cb1'], col(p['Cb1']), ha='right')
    txt(ax, 64, 52, '10µF', col(p['Cb1']), FSV, ha='right')
    gnd(ax, 72, 36)

    dot(ax, 84, Y_BI)
    cap_v(ax, 84, 36, Y_BI, '', '')
    txt(ax, 93, 58, p['Cb2'], col(p['Cb2']), ha='left')
    txt(ax, 93, 52, '100nF', col(p['Cb2']), FSV, ha='left')
    gnd(ax, 84, 36)

    # ---- 输出：隔离 + 抗混叠 ----
    wire(ax, [(122, Y_OPC), (126, Y_OPC)])
    res_h(ax, 126, 142, Y_OPC, p['Rs'], '100Ω')
    wire(ax, [(142, Y_OPC), (148, Y_OPC)])
    res_h(ax, 148, 164, Y_OPC, p['Rf'], '1k')
    wire(ax, [(164, Y_OPC), (170, Y_OPC)])
    dot(ax, 170, Y_OPC)

    # ---- GND 汇流条 ----
    wire(ax, [(166, Y_RAIL), (186, Y_RAIL)])
    wire(ax, [(170, Y_OPC), (170, Y_RAIL)])
    dot(ax, 170, Y_RAIL)
    cap_v(ax, 170, 74, Y_OPC, '', '')
    txt(ax, 170, 99, p['Cf'], col(p['Cf']))
    txt(ax, 170, 92.5, p['Cfv'], col(p['Cf']), FSV)
    gnd(ax, 186, Y_RAIL)

    # ---- 输出连接器 ----
    jop = connector(ax, 190, 74, 8, 20, p['Jo'],
                    [(10, 'OUT'), (2, 'GND')], side='left')
    wire(ax, [(166, Y_OPC), jop['OUT']])
    wire(ax, [jop['GND'], (180, 76), (180, Y_RAIL)])
    dot(ax, 180, Y_RAIL)


CH_I = dict(J='J1', Jo='J4', U='U1A', jack=True, Rz='R12',
            Rp='R1', Ri='R2', Riv='10k',
            D1='D1', D2='D2',
            RVg='RV1', Rb='R10', RVb='RV2',
            Cb1='C1', Cb2='C2',
            Rs='R3', Rf='R4', Cf='C4', Cfv='100nF')

CH_V = dict(J='J2', Jo='J4', U='U1B',
            Rp='R5', Ri='R6', Riv='100k',
            D1='D3', D2='D4',
            RVg='RV3', Rb='R11', RVb='RV4',
            Cb1='C5', Cb2='C6',
            Rs='R7', Rf='R8', Cf='C8', Cfv='100nF')


# ================= 电源部分 =================
def draw_power(ax):
    RAIL = 30   # GND 汇流条的 y

    wire(ax, [(16, RAIL), (162, RAIL)])
    gnd(ax, 162, RAIL)

    # ---- J3 5V 输入 ----
    j3 = connector(ax, 2, 42, 8, 20, 'J3',
                   [(12, '5V'), (4, 'GND')], side='right')
    wire(ax, [j3['5V'], (52, 54)])
    wire(ax, [j3['GND'], (16, 46), (16, RAIL)])
    dot(ax, 16, RAIL)

    # ---- C9 输入去耦 ----
    dot(ax, 24, 54)
    cap_v(ax, 24, RAIL, 54, '', '')
    dot(ax, 24, RAIL)
    txt(ax, 36, 49, 'C9', CLR, ha='left')
    txt(ax, 36, 42, '10µF', CLR, FSV, ha='left')

    # ---- U3 LDO ----
    block(ax, 52, 44, 26, 26, 'U3', 'LM1117-3.3')
    txt(ax, 50, 59, 'IN', BLU, FSV - 2, ha='right')
    txt(ax, 80, 59, 'OUT', BLU, FSV - 2, ha='left')
    txt(ax, 65, 38, 'GND', BLU, FSV - 1.5)
    wire(ax, [(65, 44), (65, RAIL)])
    dot(ax, 65, RAIL)

    # ---- C10 / C11 输出去耦 ----
    wire(ax, [(78, 54), (120, 54)])
    dot(ax, 86, 54)
    cap_v(ax, 86, RAIL, 54, '', '')
    dot(ax, 86, RAIL)
    txt(ax, 86, 60, 'C10')
    txt(ax, 86, 67, '100nF', CLR, FSV)

    dot(ax, 108, 54)
    cap_v(ax, 108, RAIL, 54, '', '')
    dot(ax, 108, RAIL)
    txt(ax, 108, 60, 'C11')
    txt(ax, 108, 67, '100nF', CLR, FSV)

    # ---- 3V3 轨 ----
    vcc(ax, 120, 54, '3V3', h=20.0, label_dy=26.0, half_w=6.0)

    # ---- 电源指示灯 ----
    dot(ax, 120, 64)
    wire(ax, [(120, 64), (134, 64)])
    res_h(ax, 134, 150, 64, 'R9', '1k')
    wire(ax, [(150, 64), (158, 64), (158, 60)])
    led_down(ax, 158, 60, 48, 'D5')
    wire(ax, [(158, 48), (158, RAIL)])
    dot(ax, 158, RAIL)

    txt(ax, 130, 84, '去 U1 pin8 (VDD)', BLU, FSV - 1, ha='left')
    txt(ax, 130, 78, '去 R10 / R11 上端', BLU, FSV - 1, ha='left')


# ================= 出图 =================
if __name__ == '__main__':
    # ---- 图1 电流通道 ----
    audit_reset()
    render(lambda ax: draw_channel(ax, CH_I),
           os.path.join(OUT, 'sch_ch_i.png'),
           title='图1  电流通道 (Channel I)  · AFE V0',
           xlim=X_LIM, ylim=Y_LIM)
    print('--- 图1 电流通道 ---')
    audit()

    # ---- 图2 电压通道（标红两处与电流通道不同处）----
    audit_reset()

    def _ch_v(ax):
        draw_channel(ax, CH_V, hot={'R6', 'R11'})
        txt(ax, 126, 137, '红色：R6 = 100kΩ、R11 = 22kΩ', RED, FS + 2)

    render(_ch_v, os.path.join(OUT, 'sch_ch_v.png'),
           title='图2  电压通道 (Channel V)  · 红色为与电流通道不同之处',
           xlim=X_LIM, ylim=Y_LIM)
    print('--- 图2 电压通道 ---')
    audit()

    # ---- 图3 电源 ----
    audit_reset()
    render(draw_power, os.path.join(OUT, 'sch_power.png'),
           title='图3  电源部分  · AFE V0',
           xlim=(0, 185), ylim=(22, 90), figsize=(16, 8))
    print('--- 图3 电源 ---')
    audit()
    print('done')
