#!/usr/bin/env python3
"""Hoja didáctica del muro en voladizo (Das Ej. 8.1 + NSR-10) para Xournal JackMath.

Cada paso: qué se hace y por qué, consideraciones, cálculos con su figura y, en cada
chequeo, qué hacer si no cumple. Uso: python3 generar_muro.py salida.xopp
"""
import gzip, math, os, sys
from xml.sax.saxutils import escape

W, H = 595.27559, 841.88976
LH = 28.34
Y0 = 6.8354325
X0 = 85.02
XR = 578.0          # borde derecho útil
FONT = "Arial Condensed"
MAXROW = 28

BLUE = "#1c71d8ff"
RED = "#c01c28ff"
GREEN = "#26a269ff"
ORANGE = "#e66100ff"
GRAY = "#5e5c64ff"
SOIL = "#986a44ff"
CONC = "#3d3846ff"
PRESS = "#1a5fb4ff"
LOADC = RED
BAR = RED
SECT = ORANGE

pages = []


class Page:
    def __init__(self):
        self.items = []
        pages.append(self)

    def text(self, row, x, s, calc=True, bold=False, size=12, color="#000000ff"):
        y = Y0 + row * LH
        a = f'font="{FONT}" size="{size}" x="{x:.2f}" y="{y:.4f}" color="{color}" justify="true"'
        if calc:
            a += ' calc="true"'
        a += f' lineHeight="{LH}"'
        if bold:
            a += f' jmFormat="b,0,{len(s.encode())},1"'
        self.items.append(f"<text {a}>{escape(s)}</text>")

    def label(self, x, y, s, size=9, color="#000000ff", bold=False):
        a = f'font="{FONT}" size="{size}" x="{x:.2f}" y="{y:.2f}" color="{color}"'
        if bold:
            a += f' jmFormat="b,0,{len(s.encode())},1"'
        self.items.append(f"<text {a}>{escape(s)}</text>")

    def stroke(self, pts, width=1.2, color="#000000ff", style=None, fill=None):
        a = f'tool="pen" color="{color}" width="{width}"'
        if fill is not None:
            a += f' fill="{fill}"'
        if style:
            a += f' style="{style}"'
        self.items.append(f"<stroke {a}>" + " ".join(f"{x:.2f} {y:.2f}" for x, y in pts) + "</stroke>")

    def arrow(self, p, q, width=1.4, color="#000000ff", head=7):
        self.stroke([p, q], width, color)
        ang = math.atan2(q[1] - p[1], q[0] - p[0])
        for s in (+1, -1):
            a = ang + math.pi - s * 0.4
            self.stroke([q, (q[0] + head * math.cos(a), q[1] + head * math.sin(a))], width, color)

    def xml(self):
        return (f'<page width="{W}" height="{H}">\n'
                '<background type="solid" color="#ffffffff" style="graph" config="vm=70.85"/>\n'
                '<layer>\n' + "\n".join(self.items) + '\n</layer>\n</page>')


def col(n):
    return X0 + n * 14.17


def ytop(r):
    return Y0 + r * LH


def poly(pg, pts, w=1.0, c=CONC, fill=None, style=None):
    pg.stroke(pts + [pts[0]] if fill is not None else pts, w, c, style, fill)


# ---------------------------------------------------------------- flujo de la hoja
class Flow:
    def __init__(self):
        self.p = Page()
        self.r = 0

    def new(self):
        self.p = Page()
        self.r = 0

    def need(self, n):
        if self.r + n > MAXROW + 1:
            self.new()

    def skip(self, n=1):
        self.r += n

    def title(self, s, sub=None):
        self.need(2)
        self.p.text(self.r, X0, s, calc=False, bold=True, size=14)
        self.r += 1
        if sub:
            self.p.text(self.r, X0, sub, calc=False, size=10)
            self.r += 1

    def step(self, s):
        """Encabezado de paso: siempre deja espacio para al menos 4 renglones más."""
        self.need(5)
        self.p.text(self.r, X0, s, calc=False, bold=True, color=BLUE)
        self.r += 1

    def sub(self, s):
        self.need(3)
        self.p.text(self.r, X0, s, calc=False, bold=True, size=11)
        self.r += 1

    def eq(self, *items, h=1):
        """items: texto (columna 0) o (columna, texto). '#' al inicio = texto normal."""
        self.need(h)
        rr = self.r + (h - 1) / 2
        for it in items:
            c, t = it if isinstance(it, tuple) else (0, it)
            if t.startswith("#"):
                self.p.text(rr, col(c), t[1:], calc=False, size=10)
            else:
                self.p.text(rr, col(c), t)
        self.r += h

    def box(self, kind, lines, head=None):
        """Recuadro explicativo. kind: 'que' (qué y por qué), 'ojo' (consideraciones), 'no' (si no cumple), 'ok'."""
        style = {"que": ("Qué hacemos y por qué", "#1c71d8ff", "#1c71d8ff"),
                 "ojo": ("Consideraciones", "#9c6d03ff", "#c88800ff"),
                 "no": ("Si no cumple", "#c01c28ff", "#c01c28ff"),
                 "ok": ("Resultado", "#26a269ff", "#26a269ff")}[kind]
        h0, tcol, bcol = style
        head = head or h0
        rows = math.ceil((31 + 11.0 * len(lines)) / LH)
        self.need(rows)
        y0 = ytop(self.r) + 7
        y1 = ytop(self.r + rows) - 4
        poly(self.p, [(X0 - 4, y0), (XR, y0), (XR, y1), (X0 - 4, y1)], 0.8, bcol, fill=22)
        self.p.label(X0 + 2, y0 + 4, head, 9.5, tcol, True)
        self.p.label(X0 + 2, y0 + 16, "\n".join(lines), 9, "#241f31ff")
        self.r += rows

    def fig(self, fn, rows, *args):
        self.need(rows)
        fn(self.p, self.r, *args)
        self.r += rows


F = Flow()


# ---------------------------------------------------------------- dibujo genérico del muro
def wall_geom(B=4.0, tp=0.7, b2=0.2, b1=0.5, H2=6.0, H3=0.7, alpha=10, key=None):
    Lt = B - tp - b2 - b1
    H1 = Lt * math.tan(math.radians(alpha))
    return dict(B=B, tp=tp, b2=b2, b1=b1, H2=H2, H3=H3, Lt=Lt, H1=H1, key=key)


def draw_wall(pg, ox, oy, S, g, fill=60, soil=True, front=True, color=CONC, width=1.4, style=None):
    def P(x, y):
        return (ox + x * S, oy - y * S)
    B, tp, b2, b1, H2, H3 = g["B"], g["tp"], g["b2"], g["b1"], g["H2"], g["H3"]
    xs = tp + b2 + b1
    pts = [P(0, 0)]
    if g["key"]:
        k0, kb, kh = g["key"]
        pts += [P(k0, 0), P(k0, -kh), P(k0 + kb, -kh), P(k0 + kb, 0)]
    pts += [P(B, 0), P(B, H3), P(xs, H3), P(xs, H3 + H2), P(tp + b2, H3 + H2), P(tp, H3), P(0, H3)]
    if fill is None:
        pg.stroke(pts + [P(0, 0)], width, color, style)
    else:
        poly(pg, pts, width, color, fill=fill)
    if soil:
        pg.stroke([P(xs, H3 + H2), P(B, H3 + H2 + g["H1"]), P(B + 1.0, H3 + H2 + g["H1"] + 1.0 * g["H1"] / g["Lt"])], 1.1, SOIL)
    if front:
        xf = tp + b2 * (1.5 - H3) / H2
        pg.stroke([P(-0.9, 1.5), P(xf, 1.5)], 1.1, SOIL)
        for i in range(4):
            x = -0.8 + i * 0.35
            pg.stroke([P(x, 1.5), P(x - 0.12, 1.38)], 0.6, SOIL)
    return P


def dim_h(pg, a, b, txt, size=8):
    pg.stroke([a, b], 0.6)
    for q in (a, b):
        pg.stroke([(q[0], q[1] - 3), (q[0], q[1] + 3)], 0.6)
    pg.label((a[0] + b[0]) / 2 - 2.2 * len(txt), a[1] + 3, txt, size)


def dim_v(pg, a, b, txt, dx=4, size=8):
    pg.stroke([a, b], 0.6)
    for q in (a, b):
        pg.stroke([(q[0] - 3, q[1]), (q[0] + 3, q[1])], 0.6)
    pg.label(a[0] + dx, (a[1] + b[1]) / 2 - 5, txt, size)


G0 = wall_geom()
G1 = wall_geom(B=5.2, tp=1.1, key=(1.7, 0.4, 0.8))


# ---------------------------------------------------------------- figuras
def fig_datos(pg, r0):
    S = 44.0
    ox, oy = X0 + 95, ytop(r0) + 8 + 7.6 * S
    P = draw_wall(pg, ox, oy, S, G0)
    pg.stroke([P(0.9, 0.7), P(0.9, 6.7)], 0.6, GRAY, "dash")
    pg.stroke([P(0.7, 0.7), P(1.4, 0.7)], 0.6, GRAY, "dash")
    pg.stroke([P(1.4, 6.7), P(4, 6.7)], 0.6, SOIL, "dash")
    pg.stroke([P(4, 0), P(4, 7.158)], 1.0, RED, "dash")
    pg.stroke([P(-1.0, 0), P(5.0, 0)], 0.6, SOIL, "dot")
    pg.label(*P(4.08, 7.6), "A", 10, RED, True)
    pg.label(*P(3.82, -0.06), "B", 10, RED, True)
    pg.label(*P(-0.28, -0.06), "C", 10, RED, True)
    for n, (x, y) in {1: (1.08, 3.6), 2: (0.45, 2.6), 3: (1.9, 0.5), 4: (2.6, 3.0), 5: (3.5, 7.0)}.items():
        pg.label(*P(x, y), str(n), 9, "#1a5fb4ff", True)
    pg.label(*P(1.6, 5.6), "Relleno:\nγ1 = 18 kN/m³\nφ1 = 30°", 8.5, SOIL)
    pg.label(*P(4.75, 1.25), "Fundación:\nγ2 = 19 kN/m³\nφ2 = 20°, c2 = 40 kPa", 8.5, SOIL)
    dim_h(pg, P(0, -0.85), P(0.7, -0.85), "0.7")
    dim_h(pg, P(0.7, -0.85), P(0.9, -0.85), "")
    dim_h(pg, P(0.9, -0.85), P(1.4, -0.85), "0.5")
    dim_h(pg, P(1.4, -0.85), P(4, -0.85), "2.6")
    dim_h(pg, P(0, -1.25), P(4, -1.25), "B = 4.0 m")
    dim_v(pg, P(4.6, 0), P(4.6, 7.158), "H′ = 7.158 m")
    dim_v(pg, P(-1.25, 0.7), P(-1.25, 6.7), "H2 = 6", -36)
    dim_v(pg, P(-0.55, 0), P(-0.55, 0.7), "0.7", -18)
    dim_v(pg, P(-1.25, 0), P(-1.25, 1.5), "D = 1.5", -38)
    tx = X0 + 330
    pg.label(tx, ytop(r0) + 20, "Figura 2. Sección del muro del ejemplo.", 9, bold=True)
    pg.label(tx, ytop(r0) + 34, "Secciones 1 y 2: vástago (rectángulo y\ntriángulo). 3: base. 4 y 5: suelo sobre el\n"
             "talón, que se mueve con el muro.\n\nAB: plano virtual vertical por el extremo\ndel talón, donde se calcula el empuje.\n\n"
             "C: punta de la puntera, eje de giro\npara el volcamiento.", 9)


def fig_predim(pg, r0):
    S = 40.0
    ox, oy = X0 + 70, ytop(r0) + 10 + 7.4 * S
    P = draw_wall(pg, ox, oy, S, G0, fill=35)
    c = "#9c6d03ff"
    dim_h(pg, P(0, -0.55), P(4, -0.55), "B = 0.5H′ a 0.7H′")
    dim_h(pg, P(0, 1.15), P(0.7, 1.15), "≈0.1H′")
    dim_h(pg, P(0.9, 7.25), P(1.4, 7.25), "≥ 0.30 m")
    dim_v(pg, P(4.35, 0), P(4.35, 0.7), "≈ 0.1H′", 5)
    dim_v(pg, P(-1.1, 0), P(-1.1, 1.5), "D ≥ 1.0 m", -48)
    pg.label(*P(-1.0, 4.2), "inclinación\n≥ 0.02 : 1", 8, c)
    pg.arrow(P(-0.1, 3.75), P(0.74, 3.45), 0.7, c, 4)
    tx = X0 + 290
    pg.label(tx, ytop(r0) + 18, "Figura 3. Reglas de predimensionamiento", 9, bold=True)
    pg.label(tx, ytop(r0) + 31, "(Das, figura 8.6) para H′ ≈ 7 m.\n\nSon proporciones que la experiencia sabe\n"
             "que funcionan: sirven para arrancar. Los\nchequeos de los pasos 4 a 6 dicen si la\n"
             "geometría sirve o hay que cambiarla.", 9)


def fig_empuje(pg, r0):
    S = 34.0
    ox, oy = X0 + 40, ytop(r0) + 10 + 7.6 * S
    P = draw_wall(pg, ox, oy, S, G0, fill=30, front=False)
    xa = P(4, 0)[0]
    top = P(4, 7.158)
    pg.stroke([P(4, 0), P(4, 7.158)], 1.0, RED, "dash")
    w = 95
    poly(pg, [top, (xa + w, P(4, 0)[1]), P(4, 0)], 0.9, PRESS, fill=40)
    pg.label(xa + w - 40, P(4, 0)[1] + 3, "Ka·γ1·H′", 8.5, PRESS)
    yP = P(4, 7.158 / 3)[1]
    a = math.radians(10)
    tip = (xa + 2, yP)
    tail = (tip[0] + 85 * math.cos(a), tip[1] - 85 * math.sin(a))
    pg.arrow(tail, tip, 1.8, RED)
    pg.label(tail[0] + 4, tail[1] - 7, "Pa", 10, RED, True)
    pg.arrow((tip[0] + 70, tip[1] + 22), (tip[0] + 2, tip[1] + 22), 1.0, GREEN, 5)
    pg.label(tip[0] + 74, tip[1] + 16, "Ph = Pa·cos α (voltea y desliza)", 8.5, GREEN)
    pg.arrow((tip[0] + 100, tip[1] - 62), (tip[0] + 100, tip[1] - 30), 1.0, GREEN, 5)
    pg.label(tip[0] + 106, tip[1] - 58, "Pv = Pa·sen α (estabiliza)", 8.5, GREEN)
    dim_v(pg, P(4.0, 0), P(4.0, 7.158 / 3), "", 3)
    pg.label(xa - 34, yP + 18, "H′/3", 8.5, RED)
    pg.label(X0, oy + 18, "Figura 4. Empuje activo de Rankine sobre AB: distribución triangular, resultante a H′/3,\n"
             "inclinada α (paralela al relleno). Se descompone en Ph (horizontal) y Pv (vertical).", 9)


def fig_fuerzas(pg, r0):
    S = 42.0
    ox, oy = X0 + 70, ytop(r0) + 10 + 7.5 * S
    P = draw_wall(pg, ox, oy, S, G0, fill=None, front=False)
    shades = {1: [P(0.9, 0.7), P(1.4, 0.7), P(1.4, 6.7), P(0.9, 6.7)],
              2: [P(0.7, 0.7), P(0.9, 0.7), P(0.9, 6.7)],
              3: [P(0, 0), P(4, 0), P(4, 0.7), P(0, 0.7)],
              4: [P(1.4, 0.7), P(4, 0.7), P(4, 6.7), P(1.4, 6.7)],
              5: [P(1.4, 6.7), P(4, 6.7), P(4, 7.158)]}
    cols = {1: "#77767bff", 2: "#9a9996ff", 3: "#5e5c64ff", 4: "#b5835aff", 5: "#cdab8fff"}
    for n, pts in shades.items():
        poly(pg, pts, 0.6, cols[n], fill=70)
    cg = {1: (1.15, 3.7), 2: (0.833, 2.7), 3: (2.0, 0.35), 4: (2.7, 3.7), 5: (3.133, 6.85)}
    for n, (x, y) in cg.items():
        c_ = P(x, y)
        pg.arrow((c_[0], c_[1] - 16), (c_[0], c_[1] + 14), 1.2, RED, 5)
        lx, ly = {1: (P(1.45, 0)[0], c_[1] - 6), 2: (P(0.36, 0)[0], c_[1] - 6), 3: (c_[0] + 5, c_[1] - 6)}.get(n, (c_[0] + 3, c_[1] - 20))
        pg.label(lx, ly, f"W{n}", 8.5, RED, True)
        pg.stroke([P(0, -0.25 - 0.18 * n), P(x, -0.25 - 0.18 * n)], 0.6, GRAY)
        pg.stroke([P(x, -0.25 - 0.18 * n), (c_[0], c_[1] + 14)], 0.4, GRAY, "dot")
        pg.label(*P(-0.55, -0.12 - 0.18 * n), f"x{n}", 7.5, GRAY)
    pg.arrow(P(4.0, 1.6), P(4.0, 0.05), 1.4, GREEN, 5)
    pg.label(*P(4.08, 1.3), "Pv a B de C", 8.5, GREEN)
    pg.label(*P(-0.28, 0.25), "C", 10, RED, True)
    tx = X0 + 300
    pg.label(tx, ytop(r0) + 20, "Figura 5. Cada figura simple pesa\nWi = γ·área y actúa en su centroide.", 9, bold=True)
    pg.label(tx, ytop(r0) + 50, "El brazo xi se mide en horizontal desde\nla punta C. Su momento Wi·xi es\n"
             "resistente: tiende a mantener el muro\nen su sitio. Pv también ayuda, con\nbrazo B.", 9)


def fig_volc(pg, r0):
    S = 32.0
    ox, oy = X0 + 70, ytop(r0) + 10 + 7.4 * S
    P = draw_wall(pg, ox, oy, S, G0, fill=40, front=False)
    c = P(0, 0)
    for r_, a0, a1, colr in [(48, 200, 330, RED)]:
        pts = [(c[0] + r_ * math.cos(math.radians(t)), c[1] + r_ * math.sin(math.radians(t))) for t in range(a0, a1, 10)]
        pg.stroke(pts, 1.2, colr)
    pg.label(c[0] - 60, c[1] - 64, "giro alrededor de C", 8.5, RED)
    yP = P(4, 7.158 / 3)
    pg.arrow((yP[0] + 70, yP[1]), (yP[0] + 2, yP[1]), 1.8, RED)
    pg.label(yP[0] + 10, yP[1] - 14, "Ph·H′/3 = Mo (voltea)", 8.5, RED)
    for x, y in [(1.15, 3.7), (2.7, 3.7), (2.0, 0.35)]:
        q = P(x, y)
        pg.arrow((q[0], q[1] - 14), (q[0], q[1] + 12), 1.2, GREEN, 5)
    pg.label(*P(1.6, 5.6), "ΣWi·xi + Pv·B = MR\n(resisten)", 8.5, GREEN)
    pg.label(*P(-0.28, 0.3), "C", 10, RED, True)
    tx = X0 + 300
    pg.label(tx, ytop(r0) + 20, "Figura 6. Volcamiento: el empuje\nhorizontal hace girar el muro\nalrededor de la punta C.", 9, bold=True)
    pg.label(tx, ytop(r0) + 62, "FS = MR / Mo. La NSR-10 (Tabla H.6.9-1)\npide 3.0 en el caso estático y 2.0 con\n"
             "sismo. Pv va en el numerador (opción\nmás exigente, la de la norma).", 9)


def fig_desl(pg, r0):
    k = 70.0
    x0, y0 = X0 + 90, ytop(r0) + 70
    poly(pg, [(x0, y0), (x0 + 4 * k, y0), (x0 + 4 * k, y0 + 0.7 * 40), (x0, y0 + 0.7 * 40)], 1.3, CONC, fill=50)
    poly(pg, [(x0 + 0.7 * k, y0), (x0 + 1.4 * k, y0), (x0 + 1.4 * k, y0 - 50), (x0 + 0.9 * k, y0 - 50)], 1.1, CONC, fill=50)
    yb = y0 + 28
    pg.stroke([(x0 - 70, yb), (x0 + 4 * k + 40, yb)], 0.8, SOIL, "dot")
    pg.arrow((x0 + 4 * k + 75, y0 + 6), (x0 + 4 * k + 3, y0 + 6), 1.8, RED)
    pg.label(x0 + 4 * k + 10, y0 - 10, "Ph (empuja)", 9, RED, True)
    for i in range(6):
        xx = x0 + 20 + i * 45
        pg.arrow((xx + 30, yb + 6), (xx, yb + 6), 1.0, GREEN, 5)
    pg.label(x0 + 30, yb + 12, "ΣV·tan(k1·φ2) + B·k2·c2  (fricción + adhesión en la base)", 8.5, GREEN)
    poly(pg, [(x0 - 3, y0 - 30), (x0 - 50, y0 + 28), (x0 - 3, y0 + 28)], 0.8, GRAY, fill=25)
    pg.arrow((x0 - 55, y0 + 10), (x0 - 4, y0 + 10), 1.2, GRAY, 5)
    pg.stroke([(x0 - 60, y0 - 34), (x0 + 2, y0 + 36)], 1.4, RED)
    pg.stroke([(x0 + 2, y0 - 34), (x0 - 60, y0 + 36)], 1.4, RED)
    pg.label(x0 - 75, y0 + 40, "Pp frente al pie:\nno cuenta (H.6.7b)", 8.5, RED)
    pg.label(X0, y0 + 75, "Figura 7. Deslizamiento: lo que empuja (Ph) contra lo que resiste en el contacto base-suelo.\n"
             "La NSR-10 no deja contar el pasivo frente al pie: puede excavarse y exige mucho desplazamiento.", 9)


def fig_presiones(pg, r0):
    k = 80.0
    x0, y0 = X0 + 50, ytop(r0) + 40
    B_ = 4.0
    poly(pg, [(x0, y0), (x0 + B_ * k, y0), (x0 + B_ * k, y0 + 22), (x0, y0 + 22)], 1.3, CONC, fill=50)
    for t in (1, 2):
        xx = x0 + t * B_ * k / 3
        pg.stroke([(xx, y0 - 30), (xx, y0 + 90)], 0.7, GRAY, "dash")
    pg.label(x0 + B_ * k / 2 + 6, y0 - 30, "tercio\ncentral", 8.5, GRAY)
    xr = x0 + 1.595 * k
    pg.arrow((xr, y0 - 40), (xr, y0 - 2), 2.0, RED, 7)
    pg.label(xr + 4, y0 - 44, "R = ΣV", 9, RED, True)
    pg.stroke([(x0 + B_ * k / 2, y0 - 8), (x0 + B_ * k / 2, y0 + 30)], 0.8, BLUE)
    pg.label(x0 + B_ * k / 2 + 3, y0 + 6, "centro", 8, BLUE)
    dim_h(pg, (xr, y0 - 14), (x0 + B_ * k / 2, y0 - 14), "e")
    dim_h(pg, (x0, y0 - 14), (xr, y0 - 14), "xR")
    sc = 0.33
    qa, qb = 189.1, 46.1
    yb = y0 + 22
    poly(pg, [(x0, yb), (x0 + B_ * k, yb), (x0 + B_ * k, yb + qb * sc), (x0, yb + qa * sc)], 0.9, PRESS, fill=45)
    for i in range(8):
        xx = x0 + (i + 0.5) * B_ * k / 8
        qq = qa - (qa - qb) * (i + 0.5) / 8
        pg.arrow((xx, yb + qq * sc - 2), (xx, yb + 2), 0.7, PRESS, 4)
    pg.label(x0 - 5, yb + qa * sc + 3, "qmáx (puntera)", 8.5, PRESS)
    pg.label(x0 + B_ * k + 4, yb + qb * sc - 6, "qmín", 8.5, PRESS)
    pg.label(*(x0 + 0 * k - 12, y0 + 4), "C", 9, RED, True)
    pg.label(X0, yb + 80, "Figura 8. Si la resultante cae dentro del tercio central (e ≤ B/6) toda la base queda comprimida\n"
             "y la presión es un trapecio. Para la capacidad portante, Meyerhof usa el ancho efectivo B′ = B − 2e.", 9)


def fig_familias(pg, r0):
    y0 = ytop(r0) + 10
    hh = 120
    for i, (name, kind) in enumerate([("a) Estático", "tri"), ("b) Sobrecarga q", "rect"), ("c) Sismo (M-O)", "seis")]):
        x0 = X0 + 20 + i * 165
        pg.stroke([(x0, y0), (x0, y0 + hh)], 1.2, RED, "dash")
        pg.label(x0 - 10, y0 + hh + 4, name, 9, bold=True)
        poly(pg, [(x0, y0), (x0 + 60, y0 + hh), (x0, y0 + hh)], 0.8, PRESS, fill=40)
        pg.arrow((x0 + 80, y0 + hh * 2 / 3), (x0 + 2, y0 + hh * 2 / 3), 1.3, PRESS, 5)
        pg.label(x0 + 82, y0 + hh * 2 / 3 - 6, "Pa", 8.5, PRESS)
        if kind == "rect":
            poly(pg, [(x0 + 60, y0 + hh), (x0 + 60, y0), (x0 + 78, y0), (x0 + 78, y0 + hh)], 0.8, GREEN, fill=40)
            pg.arrow((x0 + 96, y0 + hh / 2), (x0 + 2, y0 + hh / 2), 1.3, GREEN, 5)
            pg.label(x0 + 98, y0 + hh / 2 - 6, "Pq", 8.5, GREEN)
            pg.label(x0 + 82, y0 + 4, "Ka·q", 8, GREEN)
        if kind == "seis":
            poly(pg, [(x0, y0), (x0 + 40, y0), (x0, y0 + hh)], 0.8, RED, fill=35)
            pg.arrow((x0 + 110, y0 + hh * 0.4), (x0 + 2, y0 + hh * 0.4), 1.3, RED, 5)
            pg.label(x0 + 112, y0 + hh * 0.4 - 12, "ΔPAE\na 0.6H′", 8.5, RED)
            pg.arrow((x0 - 4, y0 + hh * 0.75), (x0 - 26, y0 + hh * 0.75), 1.0, GRAY, 4)
            pg.label(x0 - 30, y0 + hh * 0.75 + 4, "kh·W", 8, GRAY)
    pg.label(X0, y0 + hh + 22, "Figura 10. Las tres familias de empuje sobre AB. La sobrecarga suma un rectángulo Ka·q; el sismo\n"
             "suma ΔPAE a 0.6H′ (Seed y Whitman) y las inercias kh·W del concreto y del suelo sobre el talón.", 9)


def fig_rediseno(pg, r0):
    S = 34.0
    ox, oy = X0 + 70, ytop(r0) + 10 + 7.6 * S
    draw_wall(pg, ox, oy, S, G1, fill=45, front=True)
    draw_wall(pg, ox + 0.4 * S, oy, S, G0, fill=None, soil=False, front=False, color=RED, width=1.0, style="dash")
    P = lambda x, y: (ox + x * S, oy - y * S)
    dim_h(pg, P(0, -1.2), P(5.2, -1.2), "B = 5.2 m (antes 4.0)")
    dim_h(pg, P(0, -0.85), P(1.1, -0.85), "1.1")
    pg.label(*P(1.5, -0.95), "dentellón 0.4 × 0.8", 8, CONC)
    tx = X0 + 300
    pg.label(tx, ytop(r0) + 20, "Figura 9. Rediseño (gris) contra el\nmuro original (rojo punteado).", 9, bold=True)
    pg.label(tx, ytop(r0) + 50, "Talón más largo: más suelo encima,\nmás peso y más brazo (volcamiento).\n"
             "Puntera más larga: la resultante se\ncentra (excentricidad, presiones).\n"
             "Dentellón: pasivo bajo la base que sí\ncuenta para el deslizamiento.", 9)


# ---------------------------------------------------------------- figuras nuevas
def fig_ruta(pg, r0):
    y0 = ytop(r0) + 8
    bw, gp, bh = 88.0, 13.25, 34.0
    names = [("0", "Datos"), ("1", "Predimensionar"), ("2", "Empujes"), ("3", "Pesos y momentos"), ("4", "Volcamiento"),
             ("5", "Deslizamiento"), ("6", "Presiones y\ncapacidad"), ("7", "Rediseño, q\ny sismo"), ("8", "Diseño\nestructural"),
             ("9", "Detallado")]
    pos = {}
    for i, (n, t) in enumerate(names):
        rr, cc = divmod(i, 5)
        x = X0 - 4 + cc * (bw + gp)
        y = y0 + rr * (bh + 40)
        chk = n in "456"
        colr = RED if chk else BLUE
        poly(pg, [(x, y), (x + bw, y), (x + bw, y + bh), (x, y + bh)], 0.9, colr, fill=18)
        pg.label(x + 4, y + 3, f"Paso {n}", 8, colr, True)
        pg.label(x + 4, y + 13, t, 8.5)
        pos[i] = (x, y)
        if cc < 4:
            pg.arrow((x + bw + 1, y + bh / 2), (x + bw + gp - 1, y + bh / 2), 0.9, GRAY, 4)
    # de 4 a 5 (cambio de fila)
    x4, y4 = pos[4]
    x5, y5 = pos[5]
    pg.stroke([(x4 + bw / 2, y4 + bh + 1), (x4 + bw / 2, y4 + bh + 16), (x5 + bw / 2, y4 + bh + 16)], 0.9, GRAY)
    pg.arrow((x5 + bw / 2, y4 + bh + 16), (x5 + bw / 2, y5 - 1), 0.9, GRAY, 4)
    # regresos
    yb = y5 + bh + 12
    pg.stroke([(pos[5][0] + bw / 2, y5 + bh + 1), (pos[5][0] + bw / 2, yb)], 0.9, RED, "dash")
    pg.stroke([(pos[6][0] + bw / 2, y5 + bh + 1), (pos[6][0] + bw / 2, yb)], 0.9, RED, "dash")
    pg.stroke([(pos[6][0] + bw / 2, yb), (pos[7][0] + bw / 2, yb)], 0.9, RED, "dash")
    pg.arrow((pos[7][0] + bw / 2, yb), (pos[7][0] + bw / 2, y5 + bh + 2), 0.9, RED, 4)
    pg.label(pos[5][0] + 2, yb + 3, "si no cumple: se cambia la geometría (paso 1) y se rehace todo; aquí, rediseño del paso 7", 8, RED)
    ya = y0 - 6
    pg.stroke([(x4 + bw / 2, y0 - 1), (x4 + bw / 2, ya)], 0.9, RED, "dash")
    pg.stroke([(x4 + bw / 2, ya), (pos[1][0] + bw / 2, ya)], 0.9, RED, "dash")
    pg.arrow((pos[1][0] + bw / 2, ya), (pos[1][0] + bw / 2, y0 - 1), 0.9, RED, 4)
    x8, y8 = pos[8]
    pg.stroke([(x8 + bw / 2, y8 + bh + 1), (x8 + bw / 2, yb + 18)], 0.9, ORANGE, "dash")
    pg.label(x8 - 150, yb + 18, "si una sección no alcanza: se engruesa y se vuelve al paso 3", 8, ORANGE)
    pg.label(X0, yb + 34, "Figura 1. Ruta de diseño. Los pasos rojos son chequeos de estabilidad: si uno falla se devuelve a la geometría.", 9)


def fig_voladizos(pg, r0):
    S = 34.0
    ox, oy = X0 + 60, ytop(r0) + 12 + 7.4 * S
    P = draw_wall(pg, ox, oy, S, G1, fill=30, front=False)
    xs = 1.1 + 0.7
    pg.stroke([P(1.1 - 0.15, 0.7), P(xs + 0.15, 0.7)], 1.8, SECT, "dash")
    pg.stroke([P(1.1, -0.15), P(1.1, 0.85)], 1.8, SECT, "dash")
    pg.stroke([P(xs, -0.15), P(xs, 0.85)], 1.8, SECT, "dash")
    pg.stroke([P(1.55, 0.0), P(2.25, 0.0)], 1.8, SECT, "dash")
    for i in range(6):
        x = 2.05 + i * 0.55
        pg.arrow(P(x, 1.7), P(x, 0.75), 0.8, SOIL, 4)
    for i in range(3):
        x = 0.2 + i * 0.35
        pg.arrow(P(x, -0.9), P(x, -0.05), 0.8, PRESS, 4)
    for yy in (2.9, 4.1, 5.3):
        pg.arrow(P(3.2, yy), P(1.85, yy), 0.8, PRESS, 4)
    pg.label(*P(2.0, 6.25), "empuje sobre el vástago", 8, PRESS)
    pg.label(*P(-1.4, -0.95), "suelo empuja\nla puntera ↑", 8, PRESS)
    pg.label(*P(2.3, 2.35), "suelo carga el talón ↓", 8, SOIL)
    pg.label(*P(-1.5, 3.4), "vástago:\nvoladizo vertical", 8.5, SECT, True)
    pg.label(*P(-1.5, 1.2), "puntera", 8.5, SECT, True)
    pg.label(*P(3.6, 0.35), "talón", 8.5, SECT, True)
    pg.label(*P(2.3, -0.5), "dentellón", 8.5, SECT, True)
    tx = X0 + 300
    pg.label(tx, ytop(r0) + 18, "Figura 11. Con la geometría fija, el muro\nse diseña como cuatro voladizos.", 9, bold=True)
    pg.label(tx, ytop(r0) + 46, "Cada uno se empotra en una sección crítica\n(naranja): ahí se calculan Mu y Vu.\n\n"
             "El lado que se tracciona decide dónde va\nel acero principal:\n· vástago: cara del relleno\n"
             "· talón: arriba (el suelo lo carga)\n· puntera: abajo (el suelo la levanta)\n· dentellón: cara frontal", 9)


def fig_armado(pg, r0):
    S2 = 40.0
    OX2, OY2 = X0 + 90, ytop(r0) + 10 + 7.3 * S2

    def Q(x, y):
        return (OX2 + x * S2, OY2 - y * S2)
    wall2 = [Q(0, 0), Q(1.7, 0), Q(1.7, -0.8), Q(2.1, -0.8), Q(2.1, 0), Q(5.2, 0), Q(5.2, 0.7), Q(1.8, 0.7),
             Q(1.8, 6.7), Q(1.3, 6.7), Q(1.1, 0.7), Q(0, 0.7), Q(0, 0)]
    pg.stroke(wall2, 1.6, CONC, fill=40)
    pg.stroke([Q(1.8, 6.7), Q(5.2, 7.3)], 1.2, SOIL)
    pg.stroke([Q(-0.6, 1.5), Q(1.13, 1.5)], 1.2, SOIL)
    c5, c7 = 0.075, 0.05
    pg.stroke([Q(1.8 - c7, 6.65), Q(1.8 - c7, 0.11), Q(2.35, 0.11)], 1.4, BAR)
    pg.stroke([Q(1.3 + c7 * 0.9, 6.65), Q(1.1 + c7, 0.75), Q(1.1 + c7, 0.11), Q(0.8, 0.11)], 1.0, BAR)
    pg.stroke([Q(0.075, 0.7 - c7), Q(5.2 - 0.075, 0.7 - c7), Q(5.2 - 0.075, 0.3)], 1.4, BAR)
    pg.stroke([Q(0.075, c5), Q(5.2 - 0.075, c5)], 1.2, BAR)
    pg.stroke([Q(1.7 + c5, 0.45), Q(1.7 + c5, -0.8 + c5), Q(2.1 - c5, -0.8 + c5), Q(2.1 - c5, 0.3)], 1.1, BAR)
    pg.stroke([Q(1.7 + c5, 0.45), Q(1.4, 0.45)], 1.1, BAR)
    for k in range(1, 12):
        y = 0.7 + k * 0.5
        if y > 6.5:
            break
        xb = 1.8 - c7 - 0.06
        xf = 1.1 + 0.2 * (y - 0.7) / 6 + c7 + 0.06
        for xx in (xb, xf):
            cx, cy = Q(xx, y)
            pg.stroke([(cx - 1.2, cy), (cx + 1.2, cy)], 2.4, PRESS)
    lbl = [((1.95, 4.2), "V1 No.7 c/0.14"), ((-1.6, 4.4), "V2 No.4 c/0.28"), ((-1.6, 3.8), "H1 No.4 c/0.18"),
           ((3.0, 0.95), "T1 No.8 c/0.14"), ((2.4, -0.4), "P1 No.5 c/0.14"), ((-0.4, -0.4), "K1 No.6 c/0.28")]
    for (x, y), s_ in lbl:
        pg.label(*Q(x, y), s_, 8.5, BAR)
    pg.stroke([Q(0.0, 4.25), Q(1.2, 4.25)], 0.5, BAR)
    pg.stroke([Q(0.0, 3.65), Q(1.15, 3.65)], 0.5, PRESS)
    for x1, x2, t, yy in [(0, 1.1, "1.1", -1.15), (1.1, 1.8, "0.7", -1.15), (1.8, 5.2, "3.4", -1.15), (0, 5.2, "B = 5.2 m", -1.6)]:
        dim_h(pg, Q(x1, yy), Q(x2, yy), t)
    pg.label(*Q(2.2, -0.75), "dentellón 0.40 × 0.80", 8)
    pg.label(X0, OY2 + 1.95 * S2 + 6, "Figura 17. Muro rediseñado con su refuerzo (esquema, diámetros exagerados). Rojo: barras\n"
             "transversales y verticales; azul: barras horizontales vistas de punta.", 9)


exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "generar_muro_figuras.py"), encoding="utf-8").read())

# ================================================================= CONTENIDO
F.title("Muro de contención en voladizo · Ejemplo 8.1 de Das con NSR-10",
        "Guía paso a paso: estabilidad externa (cargas de servicio) y diseño estructural (cargas mayoradas) · kN, m, kPa")
F.box("que", ["Cómo leer esta hoja. Cada paso empieza con un recuadro azul que dice qué se calcula y por qué. Los amarillos",
              "son consideraciones (de dónde sale un dato, qué suposición se hace, qué pide la norma). Los rojos aparecen en",
              "cada chequeo: si no cumple, dicen a qué paso volver y qué variable mover, y hacia dónde.",
              "Los renglones de cálculo están vivos: si cambia un dato, toda la hoja se recalcula y los ✓/✗ se actualizan."],
      head="Cómo usar esta guía")
F.fig(fig_ruta, 7)
F.box("ojo", ["Estabilidad externa (pasos 2 a 7): el muro como bloque rígido. Se usan cargas de servicio y factores de seguridad",
              "(NSR-10 H.6.9, tabla H.6.9-1). Diseño estructural (paso 8): cada parte como elemento de concreto, con cargas",
              "mayoradas del Título B y resistencias reducidas φ del Título C. No se mezclan: primero la geometría estable,",
              "después el acero."])

# ---------------------------------------------------------------- paso 0
F.step("Paso 0. Datos del ejercicio")
F.box("que", ["Se fija la geometría de partida, las propiedades del relleno (el suelo que empuja), del suelo de fundación",
              "(el que soporta) y del concreto. Todo se trabaja por metro de muro (franja de 1 m perpendicular al dibujo)."])
F.eq("#Geometría:", (6, "H2:=6m"), (12, "H1:=0.458m"), (19, "H3:=0.7m"), (25, "B:=4m"), (30, "D:=1.5m"))
F.eq((6, "b_1:=0.5m"), (12, "b_2:=0.2m"), (19, "t_p:=0.7m"), (25, "L_t:=2.6m"))
F.eq("#Relleno:", (6, "γ_1:=18kN/m^3"), (14, "φ_1:=30°"), (19, "α:=10°"))
F.eq("#Fundación:", (6, "γ_2:=19kN/m^3"), (14, "φ_2:=20°"), (19, "c_2:=40kPa"))
F.eq("#Concreto:", (6, "γ_c:=23.58kN/m^3"), (14, "H′:=H1+H2+H3="))
F.box("ojo", ["· γ, φ y c salen del estudio geotécnico (NSR-10 H.2 y H.6.2). φ1 es el dato más sensible: 2° menos sube el",
              "  empuje cerca de un 8 %. Use valores de diseño, no los más optimistas del laboratorio.",
              "· El relleno debe ser granular y drenado; un relleno arcilloso o saturado empuja mucho más (agua aparte).",
              "· α es la inclinación del relleno sobre la corona; H1 = Lt·tan α es la cuña de suelo sobre el talón.",
              "· D es la profundidad de desplante frente al pie: no menos de 1.0 m por erosión, raíces y excavaciones."])
F.fig(fig_datos, 15)

# ---------------------------------------------------------------- paso 1
F.step("Paso 1. Predimensionamiento")
F.box("que", ["Antes de calcular nada se da al muro unas proporciones que la experiencia sabe que funcionan. No garantizan",
              "nada: son el punto de arranque que los pasos 4 a 6 confirman o mandan a corregir. Por eso, cada vez que un",
              "chequeo falla, se vuelve aquí a cambiar alguna de estas medidas."])
F.eq("B/H′=", (8, "B≥0.5*H′="), (17, "B≤0.7*H′="))
F.eq("t_p/H′=", (8, "H3/H′="), (17, "#ideal ≈ 0.1 para los dos"))
F.eq("b_1≥0.3m=", (8, "D≥1m="), (17, "L_t/B="))
F.box("ojo", ["· Una base corta (B cerca de 0.5H′) ahorra concreto pero suele fallar al volcamiento y al deslizamiento.",
              "· El talón (Lt) es la parte más eficiente: carga suelo encima, que pesa gratis y tiene brazo grande.",
              "· La puntera (tp) centra la resultante y baja la presión máxima, pero aporta poco al volcamiento.",
              "· La corona (b1) mínima de 0.30 m es constructiva: permite vaciar y vibrar el concreto.",
              "· Espesor de la base H3 ≈ 0.1H′: debe alcanzar para el cortante del talón (se revisa en el paso 8.4)."])
F.fig(fig_predim, 12)

# ---------------------------------------------------------------- paso 2
F.step("Paso 2. Empuje activo de Rankine sobre el plano AB")
F.box("que", ["El suelo retenido empuja el muro. Como el muro en voladizo puede girar un poco, el suelo llega al estado activo",
              "(el empuje mínimo). El empuje se calcula sobre el plano vertical AB que pasa por el extremo del talón: así",
              "el suelo sobre el talón cuenta como parte del muro (peso) y no como carga. Con relleno inclinado α,",
              "Rankine da Ka (Das ec. 7.21) y el empuje es paralelo a la superficie del relleno."])
F.eq("K_a:=COS(α)*(COS(α)-RAIZ(COS(α)^2-COS(φ_1)^2))/(COS(α)+RAIZ(COS(α)^2-COS(φ_1)^2))=", h=2)
F.eq("P_a:=1/2*γ_1*H′^2*K_a=")
F.eq("P_h:=P_a*COS(α)=", (16, "P_v:=P_a*SENO(α)="))
F.fig(fig_empuje, 11)
F.box("ojo", ["· Das usa Ka = 0.3532 (fila de α = 11° de su tabla); la fórmula con α = 10° da 0.3495. Aquí va la fórmula.",
              "· Si el muro no puede moverse (apoyado arriba, o sobre roca) el empuje es en reposo, K0 = 1 − sen φ ≈ 0.5,",
              "  bastante mayor (NSR-10 H.6.4). Un voladizo normal sí se desplaza lo necesario para el estado activo.",
              "· Ph voltea y desliza el muro; Pv lo aprieta contra el suelo y ayuda (paso 3).",
              "· Este empuje supone el relleno drenado. Sin drenes, el agua suma γw·h²/2 y casi duplica la carga."])

# ---------------------------------------------------------------- paso 3
F.step("Paso 3. Fuerzas verticales y momento resistente respecto a la punta C")
F.box("que", ["El muro y el suelo sobre el talón se parten en figuras simples (rectángulos y triángulos). Cada una pesa",
              "Wi = γ·área y actúa en su centroide. Su momento respecto a la punta C (Wi·xi) es el que impide que el muro",
              "gire. Se suman todos, más Pv con brazo B, para tener ΣV y el momento resistente MR."])
F.fig(fig_fuerzas, 14)
F.eq("#Peso por metro de muro", (14, "#Brazo a C"), (24, "#Momento"))
tab = [("W_1:=γ_c*b_1*H2=", "x_1:=t_p+b_2+b_1/2="), ("W_2:=γ_c*b_2*H2/2=", "x_2:=t_p+2*b_2/3="),
       ("W_3:=γ_c*B*H3=", "x_3:=B/2="), ("W_4:=γ_1*L_t*H2=", "x_4:=B-L_t/2="), ("W_5:=γ_1*L_t*H1/2=", "x_5:=B-L_t/3=")]
for i, (w, x) in enumerate(tab):
    F.eq(w, (14, x), (24, f"M_{i+1}:=W_{i+1}*x_{i+1}=?kN*m/m"))
F.eq("ΣV:=W_1+W_2+W_3+W_4+W_5+P_v=")
F.eq("M_R:=M_1+M_2+M_3+M_4+M_5+P_v*B=?kN*m/m")
F.box("ojo", ["· Fíjese en qué figura pesa más: W4 (suelo sobre el talón) da casi la mitad de MR. Por eso alargar el talón",
              "  es la forma más barata de ganar estabilidad.",
              "· Los brazos se miden en horizontal desde C, nunca desde el vástago.",
              "· El suelo frente al pie (sobre la puntera) se ignora: puede excavarse en cualquier momento.",
              "· Si cambia una medida en el paso 0 o 1, estas fuerzas se recalculan solas."])

# ---------------------------------------------------------------- paso 4
F.step("Paso 4. Volcamiento (NSR-10 tabla H.6.9-1: FS ≥ 3.0 estático, ≥ 2.0 con sismo)")
F.box("que", ["El empuje horizontal Ph, aplicado a H′/3, tiende a hacer girar el muro alrededor de la punta C. Se compara el",
              "momento que voltea (Mo) con el que resiste (MR, del paso 3). La norma pide que MR sea al menos 3 veces Mo."])
F.fig(fig_volc, 9)
F.eq("M_o:=P_h*H′/3=?kN*m/m", (13, "FS_volc:=M_R/M_o="), (22, "FS_volc≥3="))
F.box("no", ["Da 2.98 < 3.0. Das lo acepta porque usa FS ≥ 2.0; la NSR-10 pide 3.0. Hay que volver al paso 1:",
             "· Alargar el talón Lt (y B con él): es lo más eficiente. Aquí cada 0.10 m de talón sube el FS unas 0.13;",
             "  con B = 4.1 m (Lt = 2.7 m) ya cumple el caso estático.",
             "· Aumentar la puntera tp ayuda poco (mueve C hacia afuera, pero sin suelo encima).",
             "· No sirve engrosar el vástago: pesa poco y casi no tiene brazo.",
             "· Después de cambiar, repetir los pasos 2 a 6: un talón más largo sube H1, H′ y también el empuje."])

# ---------------------------------------------------------------- paso 5
F.step("Paso 5. Deslizamiento sobre la base")
F.box("que", ["Ph empuja el muro hacia adelante. Lo resiste la fricción entre la base y el suelo (ΣV·tan δ) y la adhesión",
              "(B·ca). Como el concreto se vacía contra el suelo y la interfaz no es perfecta, se reducen φ2 y c2 con",
              "δ = k1·φ2 y ca = k2·c2 (Das recomienda k1 = k2 entre 1/2 y 2/3)."])
F.fig(fig_desl, 6)
F.eq("k_1:=2/3", (6, "k_2:=2/3"), (12, "K_p:=TAN(45°+φ_2/2)^2="))
F.eq("P_p:=1/2*K_p*γ_2*D^2+2*c_2*RAIZ(K_p)*D=")
F.eq("#Como Das, contando el pasivo frente al pie:")
F.eq("FS_Das:=(ΣV*TAN(k_1*φ_2)+B*k_2*c_2+P_p)/P_h=", h=2)
F.eq("#Como exige la NSR-10 H.6.7(b), sin el pasivo frente al pie:")
F.eq("FS_desl:=(ΣV*TAN(k_1*φ_2)+B*k_2*c_2)/P_h=", (22, "FS_desl≥1.6="), h=2)
F.box("no", ["Da 1.37 < 1.6 (sin pasivo). Opciones, de la más usada a la menos:",
             "· Dentellón (llave) bajo la base: el pasivo del suelo frente a él sí cuenta, porque nadie lo excava. Es la",
             "  solución de la guía (paso 7). Variables: su altura hk (el pasivo crece con hk²) y su posición.",
             "· Alargar B (paso 1): sube la adhesión B·k2·c2 y el peso. Aquí haría falta B ≈ 4.8 m solo para el caso",
             "  estático; con sobrecarga y sismo, más. Sale más caro que el dentellón.",
             "· Inclinar la base hacia el relleno o mejorar el suelo de apoyo (subir φ2, c2) con un mejoramiento."])

# ---------------------------------------------------------------- paso 6
F.step("Paso 6. Excentricidad, presiones bajo la base y capacidad portante")
F.box("que", ["La resultante de todas las fuerzas corta la base a xR de la punta. Su distancia al centro es la excentricidad e.",
              "Si e ≤ B/6 (tercio central) toda la base está comprimida y la presión es un trapecio entre qmáx y qmín.",
              "Luego se compara qmáx con la capacidad última qu del suelo (Meyerhof, con ancho reducido y carga inclinada)."])
F.fig(fig_presiones, 7)
F.eq("x_R:=(M_R-M_o)/ΣV=", (13, "e:=B/2-x_R="), (22, "e≤B/6="))
F.eq("q_max:=ΣV/B*(1+6*e/B)=", (16, "q_min:=ΣV/B*(1-6*e/B)="), h=2)
F.sub("6a. Capacidad portante (Meyerhof, Das ec. 8.22)")
F.eq("#Factores de capacidad para φ2:")
F.eq("N_q:=TAN(45°+φ_2/2)^2*EXP(π*TAN(φ_2))=", (19, "N_c:=(N_q-1)/TAN(φ_2)="))
F.eq("N_γ:=2*(N_q+1)*TAN(φ_2)=", (13, "q:=γ_2*D="), (21, "B′:=B-2*e="))
F.eq("#Factores de profundidad e inclinación de la carga:")
F.eq("F_qd:=1+2*TAN(φ_2)*(1-SENO(φ_2))^2*D/B′=", (19, "F_cd:=F_qd-(1-F_qd)/(N_c*TAN(φ_2))="), h=2)
F.eq("ψ:=ATAN(P_h/ΣV)=?°", (11, "F_ci:=(1-ψ/90°)^2="), (21, "F_γi:=(1-ψ/φ_2)^2="))
F.eq("q_u:=c_2*N_c*F_cd*F_ci+q*N_q*F_qd*F_ci+1/2*γ_2*B′*N_γ*F_γi=", h=2)
F.eq("FS_cap:=q_u/q_max=", (12, "FS_cap≥3="))
F.box("ojo", ["· El ancho efectivo B′ = B − 2e castiga la excentricidad: la carga se reparte en una base más angosta.",
              "· ψ es la inclinación de la resultante: una carga inclinada reduce mucho qu (factores Fci y Fγi).",
              "· Con el Ka de Das (0.3532) este FS da 2.98 y Das pide redimensionar; con el Ka correcto queda justo en 3.0."])
F.box("no", ["Si e > B/6: parte de la base se despega. Volver al paso 1 y alargar la puntera tp (o B), que acerca la",
             "resultante al centro. Si FS_cap < 3: alargar B (baja qmáx y sube B′), profundizar D (sube q y Fqd) o",
             "mejorar el suelo de apoyo. Si qmín sale muy bajo frente a qmáx, el muro tenderá a inclinarse hacia afuera."])

F.sub("Resumen del muro original")
F.eq("#Verificación", (11, "#Calculado"), (18, "#Mínimo NSR-10"), (24, "#Estado"))
for name, val, mn, chk in [("Volcamiento", "FS_volc=", "3.0", "FS_volc≥3="),
                           ("Deslizamiento sin pasivo", "FS_desl=", "1.6", "FS_desl≥1.6="),
                           ("Excentricidad", "e=", "B/6 = 0.667 m", "e≤B/6="),
                           ("Capacidad portante", "FS_cap=", "3.0", "FS_cap≥3=")]:
    F.eq("#" + name, (11, val), (18, "#" + mn), (24, chk))
F.box("no", ["El muro del ejemplo no cumple la NSR-10 en volcamiento ni en deslizamiento, y la capacidad queda al límite.",
             "Volvemos a la geometría (paso 1) con lo aprendido: talón más largo para el volcamiento, dentellón para el",
             "deslizamiento y puntera más larga para centrar la resultante. Eso es el paso 7."], head="Conclusión")

# ---------------------------------------------------------------- paso 7
F.step("Paso 7. Rediseño para cumplir la NSR-10")
F.box("que", ["Se cambia la geometría según lo que falló: B pasa de 4.0 a 5.2 m (talón de 2.6 a 3.4 m), la puntera de 0.7",
              "a 1.1 m y se agrega un dentellón de 0.40 × 0.80 m bajo el vástago. Solo se redefinen los datos que cambian:",
              "lo demás (Ka, Kp, Nc, Nq, Nγ, k1, k2) se reutiliza. Luego se revisan tres casos: estático, con sobrecarga",
              "y con sismo, porque el muro debe cumplir en todos (NSR-10 H.6.9)."])
F.fig(fig_rediseno, 12)
F.eq("B:=5.2m", (6, "t_p:=1.1m"), (12, "γ_c:=23.5kN/m^3"), (21, "h_k:=0.8m"), (26, "b_k:=0.4m"))
F.eq("L_t:=B-t_p-b_2-b_1=", (11, "H1:=L_t*TAN(α)="), (22, "H′:=H1+H2+H3="))
F.sub("7a. Caso estático")
F.eq("P_a:=1/2*γ_1*H′^2*K_a=")
F.eq("P_h:=P_a*COS(α)=", (16, "P_v:=P_a*SENO(α)="))
F.eq("#Peso", (11, "#Brazo a C"), (22, "#Altura del centroide (para el sismo)"))
tab = [("W_1:=γ_c*b_1*H2=", "x_1:=t_p+b_2+b_1/2=", "y_1:=H3+H2/2="),
       ("W_2:=γ_c*b_2*H2/2=", "x_2:=t_p+2*b_2/3=", "y_2:=H3+H2/3="),
       ("W_3:=γ_c*B*H3=", "x_3:=B/2=", "y_3:=H3/2="),
       ("W_4:=γ_1*L_t*H2=", "x_4:=B-L_t/2=", "y_4:=H3+H2/2="),
       ("W_5:=γ_1*L_t*H1/2=", "x_5:=B-L_t/3=", "y_5:=H3+H2+H1/3=")]
for w, x, y in tab:
    F.eq(w, (11, x), (22, y))
F.eq("ΣV:=W_1+W_2+W_3+W_4+W_5+P_v=", (19, "M_o:=P_h*H′/3=?kN*m/m"))
F.eq("M_R:=W_1*x_1+W_2*x_2+W_3*x_3+W_4*x_4+W_5*x_5+P_v*B=?kN*m/m")
F.eq("FS_volc:=M_R/M_o=", (12, "FS_volc≥3="))
F.eq("#Deslizamiento: sin el pasivo frente al pie, pero sí el del dentellón (suelo bajo la base, que no se excava):")
F.eq("R_b:=ΣV*TAN(k_1*φ_2)+B*k_2*c_2=")
F.eq("P_pk:=1/2*K_p*γ_2*h_k^2+2*c_2*RAIZ(K_p)*h_k=")
F.eq("FS_sin:=R_b/P_h=", (10, "FS_desl:=(R_b+P_pk)/P_h="), (22, "FS_desl≥1.6="))
F.eq("e:=B/2-(M_R-M_o)/ΣV=", (13, "e≤B/6="), (20, "B′:=B-2*e="))
F.eq("q_max:=ΣV/B*(1+6*e/B)=", (17, "q_min:=ΣV/B*(1-6*e/B)="), h=2)
F.eq("F_qd:=1+2*TAN(φ_2)*(1-SENO(φ_2))^2*D/B′=", (19, "F_cd:=F_qd-(1-F_qd)/(N_c*TAN(φ_2))="), h=2)
F.eq("ψ:=ATAN(P_h/ΣV)=?°", (10, "F_ci:=(1-ψ/90°)^2="), (20, "F_γi:=(1-ψ/φ_2)^2="))
F.eq("q_u:=c_2*N_c*F_cd*F_ci+q*N_q*F_qd*F_ci+1/2*γ_2*B′*N_γ*F_γi=", h=2)
F.eq("FS_cap:=q_u/q_max=", (12, "FS_cap≥3="))
F.box("ok", ["Todo cumple con holgura en el caso estático: volcamiento 4.63, deslizamiento 2.31, e = 0.11 m, capacidad 4.73.",
             "Observe que sin el dentellón (FS_sin) el deslizamiento ya pasa en estático, pero no pasaría con la sobrecarga."])

F.sub("7b. Caso con sobrecarga q = 10 kPa (NSR-10 H.6.3)")
F.box("que", ["Una sobrecarga uniforme sobre el relleno (tráfico, bodegaje) suma un empuje rectangular Ka·q en toda la altura,",
              "con resultante a H′/2. Para volcamiento y deslizamiento su peso sobre el talón no se cuenta como estabilizante",
              "(puede no estar), pero para las presiones sí se suma (es el caso más cargado)."])
F.fig(fig_familias, 7)
F.eq("q_s:=10kPa", (8, "P_q:=K_a*q_s*H′="))
F.eq("P_qh:=P_q*COS(α)=", (16, "P_qv:=P_q*SENO(α)="))
F.eq("ΣV_q:=ΣV+P_qv=", (14, "M_Rq:=M_R+P_qv*B=?kN*m/m"))
F.eq("M_oq:=M_o+P_qh*H′/2=?kN*m/m", (17, "FS_vq:=M_Rq/M_oq="), (25, "FS_vq≥3="))
F.eq("FS_dq:=(ΣV_q*TAN(k_1*φ_2)+B*k_2*c_2+P_pk)/(P_h+P_qh)=", (23, "FS_dq≥1.6="), h=2)
F.eq("e_q:=B/2-(M_Rq+q_s*L_t*x_4-M_oq)/(ΣV_q+q_s*L_t)=", (22, "e_q≤B/6="), h=2)
F.eq("q_maxq:=(ΣV_q+q_s*L_t)/B*(1+6*e_q/B)=", (19, "B′_q:=B-2*e_q="), h=2)
F.eq("ψ_q:=ATAN((P_h+P_qh)/(ΣV_q+q_s*L_t))=?°", h=2)
F.eq("F_qdq:=1+2*TAN(φ_2)*(1-SENO(φ_2))^2*D/B′_q=", (19, "F_cdq:=F_qdq-(1-F_qdq)/(N_c*TAN(φ_2))="), h=2)
F.eq("q_uq:=c_2*N_c*F_cdq*(1-ψ_q/90°)^2+q*N_q*F_qdq*(1-ψ_q/90°)^2+1/2*γ_2*B′_q*N_γ*(1-ψ_q/φ_2)^2=", h=2)
F.eq("FS_cq:=q_uq/q_maxq=", (12, "FS_cq≥3="))
F.box("no", ["Aquí el deslizamiento es el que manda: sin el dentellón daría 1.46 < 1.6. Si no cumple, aumentar hk",
             "(el pasivo crece con hk²) antes que B. Si falla la capacidad, alargar B o profundizar D (vuelta al paso 7)."],
      head="Si no cumpliera")

F.sub("7c. Caso sísmico (Mononobe-Okabe, NSR-10 H.6.8 y H.5.2.5)")
F.box("que", ["Durante un sismo el suelo empuja más (KAE > Ka) y el propio muro y el suelo sobre el talón sufren fuerzas de",
              "inercia kh·W. El incremento ΔPAE = PAE − Pa se aplica a 0.6H′ (Seed y Whitman). Los FS mínimos bajan porque",
              "el sismo es una carga rara y breve: 2.0 al volcamiento, 1.05 al deslizamiento y 1.5 a la capacidad."])
F.box("ojo", ["· Santa Marta: Aa = 0.15 y perfil C con Fa = 1.2, así amáx = Aa·Fa. Para muros que pueden desplazarse",
              "  un poco, kh = 0.8·amáx (H.6.8). Revise Aa y el perfil de su sitio en el Título A y el estudio de suelos.",
              "· Sobre AB: β = 90° (plano vertical) y δ = α. Se usa la forma con cos α en el denominador.",
              "· kv se toma 0 (lo usual); con kv ≠ 0 el sismo vertical alivia o agrava según el signo."])
F.eq("a_max:=0.15*1.2=", (9, "k_h:=0.8*a_max="), (19, "θ:=ATAN(k_h)=?°"))
F.eq("K_AE:=COS(φ_1-θ)^2/(COS(θ)*COS(α+θ)*(1+RAIZ(SENO(φ_1+α)*SENO(φ_1-θ-α)/(COS(α+θ)*COS(α))))^2)=", h=3)
F.eq("P_AE:=1/2*γ_1*H′^2*K_AE=", (17, "ΔP:=P_AE-P_a="))
F.eq("F_i:=k_h*(W_1+W_2+W_3+W_4+W_5)=")
F.eq("M_i:=k_h*(W_1*y_1+W_2*y_2+W_3*y_3+W_4*y_4+W_5*y_5)=?kN*m/m")
F.eq("ΣV_E:=ΣV+ΔP*SENO(α)=", (17, "M_RE:=M_R+ΔP*SENO(α)*B=?kN*m/m"))
F.eq("M_oE:=M_o+ΔP*COS(α)*0.6*H′+M_i=?kN*m/m")
F.eq("FS_vE:=M_RE/M_oE=", (12, "FS_vE≥2="))
F.eq("F_dE:=P_h+ΔP*COS(α)+F_i=")
F.eq("FS_dE:=(ΣV_E*TAN(k_1*φ_2)+B*k_2*c_2+P_pk)/F_dE=", (21, "FS_dE≥1.05="), h=2)
F.eq("e_E:=B/2-(M_RE-M_oE)/ΣV_E=", (15, "e_E≤B/4="))
F.eq("L_E:=3*(B/2-e_E)=", (12, "#Como e > B/6 la base se despega en parte: presión triangular de largo L."))
F.eq("q_maxE:=2*ΣV_E/L_E=", (15, "B′_E:=B-2*e_E="), h=2)
F.eq("ψ_E:=ATAN(F_dE/ΣV_E)=?°", (15, "#Como ψ > φ2, el término de Nγ se anula (Fγi = 0)."))
F.eq("F_qdE:=1+2*TAN(φ_2)*(1-SENO(φ_2))^2*D/B′_E=", (19, "F_cdE:=F_qdE-(1-F_qdE)/(N_c*TAN(φ_2))="), h=2)
F.eq("q_uE:=c_2*N_c*F_cdE*(1-ψ_E/90°)^2+q*N_q*F_qdE*(1-ψ_E/90°)^2=", h=2)
F.eq("FS_cE:=q_uE/q_maxE=", (12, "FS_cE≥1.5="))
F.box("no", ["El sismo gobierna: el volcamiento queda en 2.03, al límite, y e = 0.96 m pasa de B/6 (se acepta hasta B/4",
             "con sismo). Si no cumpliera: alargar el talón (paso 7, sube MR y el peso), no la puntera. Si falla el",
             "deslizamiento, subir hk. Si e > B/4 o falla la capacidad, alargar B. Tras cada cambio, repetir 7a, 7b y 7c."],
      head="Si no cumpliera")

F.sub("Resumen de estabilidad del muro rediseñado (B = 5.2 m)")
F.eq("#Verificación", (11, "#Estático"), (18, "#Con sobrecarga"), (25, "#Sismo"))
for a_, b_, c_, d_ in [("#Volcamiento (≥3.0 / ≥2.0)", "FS_volc=", "FS_vq=", "FS_vE="),
                       ("#Deslizamiento (≥1.6 / ≥1.05)", "FS_desl=", "FS_dq=", "FS_dE="),
                       ("#Excentricidad (B/6 / B/4)", "e=", "e_q=", "e_E="),
                       ("#Capacidad (≥3.0 / ≥1.5)", "FS_cap=", "FS_cq=", "FS_cE=")]:
    F.eq(a_, (11, b_), (18, c_), (25, d_))
F.box("ok", ["Todo cumple. Con la geometría ya estable se pasa al acero. Falta revisar aparte la estabilidad general",
             "(deslizamiento profundo de todo el talud con el muro, FS ≥ 1.5 estático) con un programa de taludes."])

# ---------------------------------------------------------------- paso 8
F.step("Paso 8. Diseño estructural (NSR-10 Título C, cargas del Título B)")
F.box("que", ["Ahora cada parte del muro se diseña como elemento de concreto reforzado. Las cargas se mayoran con las",
              "combinaciones del Título B y la resistencia se reduce con φ. Se usan dos combinaciones: U1 con la sobrecarga",
              "(gravedad máxima) y U2 con el sismo (empuje máximo, concreto con 0.9 para no exagerar su peso favorable).",
              "En cada voladizo: momento Mu → acero As; cortante Vu → revisar que el concreto solo lo resista (φVc)."])
F.fig(fig_voladizos, 11)
F.eq("#U1 = 1.2D + 1.6(L+H)  ·  U2 = 0.9D + 1.0E + 1.6H  ·  φ = 0.90 flexión, 0.75 cortante")
F.eq("f_c:=28MPa", (6, "f_y:=420MPa"), (12, "φ_f:=0.9"), (17, "φ_v:=0.75"), (22, "b:=1m"))
F.eq("#Cuantía para un momento Mu por metro:  ρ = 0.85·f′c/fy·(1 − √(1 − 2·Mu/(0.85·φ·f′c·b·d²)))")
F.eq("#Cortante sin estribos:  φVc = φ·0.17·√f′c·b·d  (C.11.2.1.1)  ·  en muros no se ponen estribos")
F.box("ojo", ["· Recubrimientos (C.7.7.1): 75 mm en caras vaciadas contra el suelo (bajo la base, dentellón); 50 mm en caras",
              "  en contacto con el suelo pero encofradas (vástago, parte superior de la base).",
              "· d = espesor − recubrimiento − db/2. El cortante se revisa a una distancia d de la cara (C.11.1.3).",
              "· Acero mínimo: flexión 1.4/fy·b·d (C.10.5) en el vástago; 0.0018·b·h por retracción en la base (C.7.12).",
              "· Separación máxima 3h o 450 mm. Áreas de barra: No.4 129, No.5 199, No.6 284, No.7 387, No.8 510 mm²."])

F.sub("8.1 Vástago: sección crítica en la unión con la base (h = H2)")
F.box("que", ["El vástago es un voladizo vertical. Sobre él actúa solo el empuje de su propia altura h = H2 (no el de H′,",
              "que incluye la base y la cuña). La cara del relleno queda traccionada: ahí va el acero principal V1."])
F.fig(fig_vastago, 6)
F.eq("h:=H2", (5, "P_hs:=1/2*γ_1*h^2*K_a*COS(α)="))
F.eq("P_qs:=K_a*q_s*h*COS(α)=", (17, "ΔP_s:=1/2*γ_1*h^2*(K_AE-K_a)*COS(α)="))
F.eq("M_is:=k_h*(W_1*H2/2+W_2*H2/3)=?kN*m/m")
F.eq("M_u1:=1.6*(P_hs*h/3+P_qs*h/2)=?kN*m/m", (17, "M_u2:=1.6*P_hs*h/3+ΔP_s*0.6*h+M_is=?kN*m/m"))
F.eq("M_us:=MAX(M_u1;M_u2)=?kN*m/m", (17, "d_s:=700mm-50mm-22.2mm/2="))
F.eq("ρ_s:=0.85*f_c/f_y*(1-RAIZ(1-2*M_us*b/(0.85*φ_f*f_c*b*d_s^2)))=", h=2)
F.eq("A_ss:=ρ_s*b*d_s=?mm^2")
F.eq("A_smin:=MAX(0.25*RAIZ(f_c/MPa)*MPa/f_y;1.4MPa/f_y)*b*d_s=?mm^2", h=2)
F.eq("#V1: No.7 c/0.14 en la cara del relleno")
F.eq("A_V1:=387mm^2/0.14m*b=?mm^2", (17, "A_V1≥A_ss="))
F.eq("#Cortante a una distancia d de la base (gobierna el sismo): parte estática más parte sísmica")
F.eq("V_Hs:=1.6*1/2*γ_1*(h-d_s)^2*K_a*COS(α)=")
F.eq("V_Es:=1/2*γ_1*(h-d_s)^2*(K_AE-K_a)*COS(α)+k_h*(W_1+W_2)*(h-d_s)/h=", h=2)
F.eq("V_us:=V_Hs+V_Es=")
F.eq("φV_cs:=φ_v*0.17*RAIZ(f_c/MPa)*MPa*d_s=?kN/m", (19, "φV_cs≥V_us="), h=2)
F.eq("#Refuerzo horizontal (C.14.3.3), repartido en las dos caras:")
F.eq("A_sh:=0.0020*b*700mm=?mm^2", (17, "#H1: No.4 c/0.18 por cada cara"))
F.box("no", ["· Si A_V1 < A_ss: cerrar la separación (0.14 → 0.12 m) o subir a No.8. Mantenga múltiplos del módulo 0.14 m",
             "  para que las barras no choquen con las de la base (paso 9).",
             "· Si la raíz de ρ da negativa (sección insuficiente) o φVc < Vu: engrosar el vástago en la base (b2 ↑) para",
             "  subir d, y volver al paso 3: cambia W1, W2 y la estabilidad. También ayuda subir f′c a 35 MPa.",
             "· El momento cae rápido con la altura (∝ h³): la mitad de V1 puede cortarse a 2.57 m sobre la base."])

F.sub("8.2 Base: presiones con cargas mayoradas")
F.box("que", ["Para diseñar la puntera y el talón se necesita la presión del suelo con cargas mayoradas (no la de servicio",
              "del paso 6). Se separan las cargas por tipo: D = concreto, H = suelo y empuje, L = sobrecarga, E = sismo, y",
              "con sus momentos respecto a C se obtiene la excentricidad y el trapecio de presiones de cada combinación."])
F.eq("W_D:=W_1+W_2+W_3=", (17, "M_D:=W_1*x_1+W_2*x_2+W_3*x_3=?kN*m/m"))
F.eq("W_H:=W_4+W_5+P_v=", (17, "M_H:=W_4*x_4+W_5*x_5+P_v*B=?kN*m/m"))
F.eq("#U1 = 1.2D + 1.6(L+H), con sobrecarga:")
F.eq("V_1:=1.2*W_D+1.6*(W_H+q_s*L_t+P_qv)=")
F.eq("N_1:=1.2*M_D+1.6*(M_H+q_s*L_t*x_4+P_qv*B)=?kN*m/m")
F.eq("O_1:=1.6*(M_o+P_qh*H′/2)=?kN*m/m", (19, "e_1:=B/2-(N_1-O_1)/V_1="), h=2)
F.eq("q_a1:=V_1/B*(1+6*e_1/B)=", (17, "q_b1:=V_1/B*(1-6*e_1/B)="), h=2)
F.eq("#U2 = 0.9D + 1.0E + 1.6H, con sismo:")
F.eq("V_2:=0.9*W_D+1.6*W_H+ΔP*SENO(α)=")
F.eq("N_2:=0.9*M_D+1.6*M_H+ΔP*SENO(α)*B=?kN*m/m")
F.eq("O_2:=1.6*M_o+ΔP*COS(α)*0.6*H′+M_i=?kN*m/m", (21, "e_2:=B/2-(N_2-O_2)/V_2="), h=2)
F.eq("q_a2:=V_2/B*(1+6*e_2/B)=", (17, "q_b2:=V_2/B*(1-6*e_2/B)="), h=2)
F.fig(fig_base, 13)
F.box("ojo", ["En los dos casos e ≤ B/6, así que la presión es un trapecio. Si alguna combinación diera e > B/6, use la",
              "presión triangular (q = 2V/L, con L = 3(B/2 − e)) como en el caso sísmico de servicio. U2 da la presión",
              "más alta bajo la punta (puntera) y U1 la más alta bajo el talón: por eso se revisan las dos."])

F.sub("8.3 Puntera (voladizo desde la cara frontal del vástago, tracción abajo)")
F.box("que", ["La puntera es un voladizo que el suelo empuja hacia arriba; su peso propio la alivia un poco. Se corta en",
              "la cara frontal del vástago. La presión en esa cara, qt, se interpola linealmente en el trapecio."])
F.eq("q_t1:=q_a1-(q_a1-q_b1)*t_p/B=", (17, "q_t2:=q_a2-(q_a2-q_b2)*t_p/B="), h=2)
F.eq("M_p1:=t_p^2*(2*q_a1+q_t1)/6-1.2*γ_c*H3*t_p^2/2=?kN*m/m", h=2)
F.eq("M_p2:=t_p^2*(2*q_a2+q_t2)/6-0.9*γ_c*H3*t_p^2/2=?kN*m/m", h=2)
F.eq("M_up:=MAX(M_p1;M_p2)=?kN*m/m", (17, "d_p:=700mm-75mm-15.9mm/2="))
F.eq("A_sp:=0.85*f_c/f_y*(1-RAIZ(1-2*M_up*b/(0.85*φ_f*f_c*b*d_p^2)))*b*d_p=?mm^2", h=2)
F.eq("#Gobierna el mínimo de retracción y temperatura sobre el área bruta (C.7.12.2.1):")
F.eq("A_spmin:=0.0018*b*H3=?mm^2", (17, "#P1: No.5 c/0.14"))
F.eq("A_P1:=199mm^2/0.14m*b=?mm^2", (17, "A_P1≥MAX(A_sp;A_spmin)="))
F.eq("V_up:=t_p*(q_a2+q_t2)/2-0.9*γ_c*H3*t_p=", h=2)
F.eq("φV_cp:=φ_v*0.17*RAIZ(f_c/MPa)*MPa*d_p=?kN/m", (19, "φV_cp≥V_up="), h=2)
F.skip(1)
F.fig(fig_puntera, 7)
F.box("no", ["La puntera rara vez falla: es corta. Si Vu > φVc, aumentar el espesor de la base H3 (vuelve al paso 3,",
             "porque cambia W3). Si el acero no alcanza, cerrar la separación de P1 o subir el diámetro. Note que el",
             "recubrimiento inferior es 75 mm (vaciada contra el suelo), por eso d es menor que en el talón."])

F.sub("8.4 Talón (voladizo desde la cara posterior del vástago, tracción arriba)")
F.box("que", ["El talón carga hacia abajo el suelo que tiene encima (1.6H), su peso, la sobrecarga y Pv en el extremo. La",
              "reacción del suelo sube, pero es menor porque la presión es baja en ese lado. Neto: el talón se dobla hacia",
              "abajo y queda traccionada la cara superior. Suele ser la pieza que define el espesor de la base."])
F.eq("x_0:=t_p+b_2+b_1=", (10, "M_s:=1.6*(W_4*L_t/2+W_5*2*L_t/3+P_v*L_t)=?kN*m/m"))
F.eq("q_01:=q_a1-(q_a1-q_b1)*x_0/B=", (17, "q_02:=q_a2-(q_a2-q_b2)*x_0/B="), h=2)
F.eq("M_t1:=M_s+1.2*γ_c*H3*L_t^2/2+1.6*(q_s*L_t^2/2+P_qv*L_t)-L_t^2*(q_01+2*q_b1)/6=?kN*m/m", h=2)
F.eq("M_t2:=M_s+0.9*γ_c*H3*L_t^2/2+ΔP*SENO(α)*L_t-L_t^2*(q_02+2*q_b2)/6=?kN*m/m", h=2)
F.eq("M_ut:=MAX(M_t1;M_t2)=?kN*m/m", (17, "d_t:=700mm-50mm-25.4mm/2="))
F.eq("A_st:=0.85*f_c/f_y*(1-RAIZ(1-2*M_ut*b/(0.85*φ_f*f_c*b*d_t^2)))*b*d_t=?mm^2", h=2)
F.eq("#T1: No.8 c/0.14 arriba")
F.eq("A_T1:=510mm^2/0.14m*b=?mm^2", (17, "A_T1≥A_st="))
F.eq("V_ut:=1.6*(W_4+W_5+P_v)+0.9*γ_c*H3*L_t+ΔP*SENO(α)-L_t*(q_02+q_b2)/2=", h=2)
F.eq("φV_ct:=φ_v*0.17*RAIZ(f_c/MPa)*MPa*d_t=?kN/m", (19, "φV_ct≥V_ut="))
F.fig(fig_talon, 9)
F.box("no", ["· Si φVc < Vu: engrosar la base (H3 ↑, sube d). Es el cambio típico; obliga a volver al paso 3 porque",
             "  cambia W3 y H′. No se ponen estribos en la base de un muro.",
             "· Si As sale muy alto (ρ > 0.0125, aprox.) también conviene engrosar en vez de llenar de acero.",
             "· Si solo falta acero: No.8 c/0.12 m, o subir f′c. El talón más largo del rediseño (3.4 m) es lo que hace",
             "  que esta sea la pieza más exigida: es el precio de la estabilidad ganada en el paso 7."])

F.sub("8.5 Dentellón (voladizo corto empotrado en la base, empujado por el pasivo)")
F.box("que", ["El dentellón resiste el deslizamiento con el pasivo del suelo frente a él, así que debe aguantar ese empuje",
              "como voladizo colgado de la base. Se diseña con el pasivo completo y factor 1.6 (es carga de suelo, H).",
              "La parte triangular actúa a 2hk/3 de la base; la de cohesión, uniforme, a hk/2."])
F.eq("P_kt:=1/2*K_p*γ_2*h_k^2=", (13, "P_kc:=2*c_2*RAIZ(K_p)*h_k="))
F.eq("V_uk:=1.6*(P_kt+P_kc)=", (13, "M_uk:=1.6*(P_kt*2*h_k/3+P_kc*h_k/2)=?kN*m/m"), h=2)
F.eq("d_k:=400mm-75mm-19.1mm/2=")
F.eq("A_sk:=0.85*f_c/f_y*(1-RAIZ(1-2*M_uk*b/(0.85*φ_f*f_c*b*d_k^2)))*b*d_k=?mm^2", h=2)
F.eq("#K1: No.6 c/0.28 en U")
F.eq("A_K1:=284mm^2/0.28m*b=?mm^2", (17, "A_K1≥A_sk="))
F.eq("φV_ck:=φ_v*0.17*RAIZ(f_c/MPa)*MPa*d_k=?kN/m", (19, "φV_ck≥V_uk="), h=2)
F.eq("#Acero longitudinal de la base (C.7.12.2.1):")
F.eq("A_L:=0.0018*b*H3=?mm^2", (17, "#L1: No.5 c/0.30 por cada cara"))
F.fig(fig_dentellon, 9)
F.box("no", ["Si φVc < Vu o el acero no cabe: ensanchar el dentellón (bk ↑, sube d). Si se alarga hk para ganar",
             "deslizamiento (paso 7), el momento crece con hk² a hk³: revise aquí de nuevo. Conviene ubicarlo bajo el",
             "vástago, donde V1 puede anclarse dentro de él y la base es más rígida."])

# ---------------------------------------------------------------- paso 9
F.step("Paso 9. Detallado: marcas de refuerzo (módulo común s0 = 0.14 m)")
F.box("que", ["Se traduce el acero calculado en barras concretas, con marca, diámetro, separación y forma. Se escoge un",
              "módulo común de separación (0.14 m) para que las barras de piezas distintas no choquen al armar."])
cols_ = [0, 3, 13, 19]
F.need(9)
for c, h_ in zip(cols_, ["Marca", "Ubicación", "Barra", "Forma"]):
    F.p.text(F.r, col(c), h_, calc=False, bold=True, size=10)
F.r += 1
for r_ in [("V1", "Vástago, cara del relleno", "No.7 c/0.14", "L con gancho en la base"),
           ("V2", "Vástago, cara frontal", "No.4 c/0.28", "L, gancho hacia la puntera"),
           ("H1", "Vástago, horizontal", "No.4 c/0.18", "Recta, en ambas caras"),
           ("T1", "Talón, arriba", "No.8 c/0.14", "L: todo B + gancho"),
           ("P1", "Puntera, abajo", "No.5 c/0.14", "Recta, todo B"),
           ("L1", "Base, longitudinal", "No.5 c/0.30", "Recta, en ambas caras"),
           ("K1", "Dentellón", "No.6 c/0.28", "U con gancho"),
           ("K2", "Dentellón, longitudinal", "6 No.4", "Recta, una por esquina")]:
    for c, v in zip(cols_, r_):
        F.p.text(F.r, col(c), v, calc=False, size=10)
    F.r += 1
F.fig(fig_armado, 15)
F.box("ojo", ["· Anclaje de V1 en la base: ldh = 0.24·fy/√f′c·db = 423 mm ≤ 593 mm disponibles (C.12.5). Si no cupiera,",
              "  engrosar la base o bajar V1 dentro del dentellón.",
              "· T1 corre todo B: ld = 1.54 m ≤ 1.725 m desde la cara del vástago. P1 también corre todo B.",
              "· Separaciones múltiplos de 0.14 m: las verticales (V1, V2, K1) nunca caen en el plano de T1 y P1.",
              "· Drenaje (H.6.5): filtro granular, tubo perforado al pie del relleno y lloraderos cada 2 a 3 m. Sin",
              "  drenaje, el empuje del agua invalida todo el diseño (paso 2).",
              "· Juntas de contracción cada 8 a 10 m a lo largo del muro."])

# ---------------------------------------------------------------- salida
doc = ('<?xml version="1.0" standalone="no"?>\n<xournal creator="xournalpp 1.3.8+dev" fileversion="5">\n'
       '<title>Xournal++ document - see https://xournalpp.github.io/</title>\n'
       + "\n".join(pg.xml() for pg in pages) + '\n</xournal>\n')
with gzip.open(sys.argv[1], "wt", encoding="utf-8") as f:
    f.write(doc)
print("ok", len(pages), "páginas")
