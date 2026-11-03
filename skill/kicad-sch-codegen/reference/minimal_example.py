# -*- coding: utf-8 -*-
"""最小可跑示例：分压 + 电压跟随器。

用途：**新项目从这份抄起**。它演示了 DSL 的正确用法 ——
引脚一律从符号函数返回值取，不手写坐标。

跑：
    PYTHONUTF8=1 D:\\Dev\\Python314\\python.exe minimal_example.py

会生成 minimal_example.png，并打印引脚自检结果（应为「悬空 0 个」）。

布局要点（比例和路由是画 schematic 最花时间的部分）
------------------------------------------------
- 画布 xlim/ylim 要让元件**占满**，别留大片空地。这里 x 用 120 单位装
  两个电阻 + 一个运放，就够。
- 运放 hh 决定引脚 y。**先把要接的信号的 y 定下来，再反推 hh 和 yc**：
  这里分压中点定在 y=56，跟随器反馈回同相输入定在 y=30，
  所以 yc=(56+30)/2=43、hh=(56-30)/2=13。
- **绕线要绕开符号本体**，别从三角形身上穿过去。反馈线走下面和左边。
  （audit 不会报这种错 —— 它只管"接没接上"，不管"画得好不好看"。）
- **`dot()` 只在真有分支的地方放。** 实心圆点的含义是「这里电气连通」，
  不是装饰。在一根连续的导线上随手放一个，读者会以为那里有分支。
"""
import os

from schematic_dsl import *

HERE = os.path.dirname(os.path.abspath(__file__))


def draw(ax):
    # ---- 分压：3V3 → R1 → 中点 → R2 → GND ----
    vcc(ax, 20, 72, '3V3')
    r1 = res_v(ax, 20, 56, 72, 'R1', '10k')
    r2 = res_v(ax, 20, 30, 56, 'R2', '10k')
    gnd(ax, 20, 30)
    wire(ax, [r1['a'], r2['b']])       # 中点：两个引脚坐标直接相碰
    dot(ax, 20, 56)

    # ---- 跟随器 ----
    # 先定信号电平，再反推 hh / yc（见文件头说明）
    op = opamp(ax, 50, 43, 13, 'U1A')

    # 中点 → 反相输入（同一水平线，直连，不用绕）
    wire(ax, [(20, 56), op['in-']])

    # 输出 → 同相输入（绕开三角形：走右侧 → 下方 → 左侧 → 上去）
    wire(ax, [op['out'], (100, 43)])
    dot(ax, 100, 43)
    wire(ax, [(100, 43), (100, 18), (40, 18), (40, 30), op['in+']])

    # 输出端子
    wire(ax, [(100, 43), (110, 43)])
    txt(ax, 112, 43, 'VOUT', BLU, ha='left')


if __name__ == '__main__':
    audit_reset()
    render(draw, os.path.join(HERE, 'minimal_example.png'),
           title='最小示例：分压 + 电压跟随器',
           xlim=(5, 125), ylim=(10, 85), figsize=(12, 7.5))
    audit()
