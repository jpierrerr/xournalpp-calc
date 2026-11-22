#!/usr/bin/env python3
"""Genera la hoja completa del muro (Das Ej. 8.1 + NSR-10) como .xopp de Xournal JackMath."""
import gzip, math, re, sys
from xml.sax.saxutils import escape

W, H = 595.27559, 841.88976
LH = 28.34          # renglón de la cuadrícula
Y0 = 6.8354325      # origen y del primer renglón (igual que la app)
X0 = 85.02          # margen izquierdo (después de la línea vertical)
FONT = "Arial Condensed"

pages = []


class Page:
    def __init__(self):
        self.items = []
        pages.append(self)

    def text(self, row, x, s, calc=True, bold=False, size=12, color="#000000ff"):
        y = Y0 + row * LH
        attrs = f'font="{FONT}" size="{size}" x="{x}" y="{y:.4f}" color="{color}" justify="true"'
        if calc:
            attrs += ' calc="true"'
        attrs += f' lineHeight="{LH}"'
        if bold:
            attrs += f' jmFormat="b,0,{len(s.encode())},1"'
        self.items.append(f"<text {attrs}>{escape(s)}</text>")

    def label(self, x, y, s, size=9, color="#000000ff", bold=False):
        attrs = f'font="{FONT}" size="{size}" x="{x:.2f}" y="{y:.2f}" color="{color}"'
        if bold:
            attrs += f' jmFormat="b,0,{len(s.encode())},1"'
        self.items.append(f"<text {attrs}>{escape(s)}</text>")

    def stroke(self, pts, width=1.2, color="#000000ff", style=None, fill=None):
        a = f'tool="pen" color="{color}" width="{width}"'
        if fill is not None:
            a += f' fill="{fill}"'
        if style:
            a += f' style="{style}"'
        coords = " ".join(f"{x:.2f} {y:.2f}" for x, y in pts)
        self.items.append(f"<stroke {a}>{coords}</stroke>")

    def arrow(self, p, q, width=1.4, color="#000000ff", head=7):
        self.stroke([p, q], width, color)
        ang = math.atan2(q[1] - p[1], q[0] - p[0])
        for s in (+1, -1):
            a = ang + math.pi - s * 0.4
            self.stroke([q, (q[0] + head * math.cos(a), q[1] + head * math.sin(a))], width, color)

    def gap(self, *rows):
        """Inserta un renglón vacío antes de cada renglón dado (numeración original)."""
        for r in sorted(rows, reverse=True):
            lim = Y0 + r * LH - 0.5
            def f(m):
                it = m.group(0)
                if 'lineHeight=' not in it:
                    return it
                y = float(re.search(r' y="([\d.]+)"', it).group(1))
                return re.sub(r' y="[\d.]+"', f' y="{y + LH:.4f}"', it, 1) if y >= lim else it
            self.items = [f(re.match(r'.*', it, re.S)) for it in self.items]

    def xml(self):
        return (f'<page width="{W}" height="{H}">\n'
                f'<background type="solid" color="#ffffffff" style="graph" config="vm=70.85"/>\n'
                '<layer>\n' + "\n".join(self.items) + '\n</layer>\n</page>')


def col(n):  # columnas sobre la cuadrícula de 14.17 pt
    return X0 + n * 14.17


BLUE = "#1c71d8ff"

# ---------------- Página 1: datos y dibujo ----------------
p = Page()
p.text(0, X0, "Muro de contención en voladizo · Ejemplo 8.1 de Das con NSR-10", calc=False, bold=True, size=14)
p.text(1, X0, "Unidades kN, m, kPa · estabilidad externa con cargas de servicio (pasos 0 a 6 de la guía)", calc=False, size=10)

p.text(2, X0, "0. Datos del ejercicio", calc=False, bold=True, color=BLUE)
p.text(3, X0, "Geometría:", calc=False, size=10)
p.text(3, col(6), "H2:=6m")
p.text(3, col(12), "H1:=0.458m")
p.text(3, col(19), "H3:=0.7m")
p.text(3, col(25), "B:=4m")
p.text(3, col(30), "D:=1.5m")
p.text(4, col(6), "b_1:=0.5m")
p.text(4, col(12), "b_2:=0.2m")
p.text(4, col(19), "t_p:=0.7m")
p.text(4, col(25), "L_t:=2.6m")
p.text(5, X0, "Relleno:", calc=False, size=10)
p.text(5, col(6), "γ_1:=18kN/m^3")
p.text(5, col(14), "φ_1:=30°")
p.text(5, col(19), "α:=10°")
p.text(6, X0, "Fundación:", calc=False, size=10)
p.text(6, col(6), "γ_2:=19kN/m^3")
p.text(6, col(14), "φ_2:=20°")
p.text(6, col(19), "c_2:=40kPa")
p.text(7, X0, "Concreto:", calc=False, size=10)
p.text(7, col(6), "γ_c:=23.58kN/m^3")
p.text(7, col(14), "H′:=H1+H2+H3=")


# ---- dibujo ----
S = 55.0
OX, OY = 175.0, 690.0      # punto C en la página


def P(x, y):
    return (OX + x * S, OY - y * S)


GRAY = "#5e5c64ff"
SOIL = "#986a44ff"
# concreto: base + vástago (relleno gris claro)
wall = [P(0, 0), P(4, 0), P(4, 0.7), P(1.4, 0.7), P(1.4, 6.7), P(0.9, 6.7), P(0.7, 0.7), P(0, 0.7), P(0, 0)]
p.stroke(wall, 1.6, "#3d3846ff", fill=70)
# divisiones de las secciones 1, 2, 3
p.stroke([P(0.9, 0.7), P(0.9, 6.7)], 0.6, GRAY, "dash")
p.stroke([P(0.7, 0.7), P(1.4, 0.7)], 0.6, GRAY, "dash")
# relleno sobre el talón (secciones 4 y 5)
p.stroke([P(1.4, 6.7), P(4, 7.158)], 1.2, SOIL)
p.stroke([P(1.4, 6.7), P(4, 6.7)], 0.6, SOIL, "dash")
p.stroke([P(4, 7.158), P(5.3, 7.387)], 1.2, SOIL)
# plano AB
p.stroke([P(4, 0), P(4, 7.158)], 1.0, "#c01c28ff", "dash")
p.label(*P(4.08, 7.55), "A", 10, "#c01c28ff", True)
p.label(*P(3.85, -0.07), "B", 10, "#c01c28ff", True)
p.label(*P(-0.25, -0.07), "C", 10, "#c01c28ff", True)
# terreno frente al pie
p.stroke([P(-1.2, 1.5), P(0.7 + 0.2 * 0.8 / 6, 1.5)], 1.2, SOIL)
for i in range(6):
    x = -1.1 + i * 0.3
    p.stroke([P(x, 1.5), P(x - 0.15, 1.35)], 0.6, SOIL)
# suelo de fundación
p.stroke([P(-1.2, 0), P(5.3, 0)], 0.6, SOIL, "dot")
p.label(*P(0.2, -1.45), "Fundación: γ2 = 19 kN/m³, φ2 = 20°, c2 = 40 kPa", 8.5, SOIL)
p.label(*P(2.0, 5.4), "Relleno", 8.5, SOIL)
p.label(*P(2.0, 5.0), "γ1 = 18 kN/m³", 8.5, SOIL)
p.label(*P(2.0, 4.6), "φ1 = 30°", 8.5, SOIL)
# números de sección
for n, (x, y) in {1: (1.08, 3.6), 2: (0.78, 1.4), 3: (1.9, 0.2), 4: (2.6, 3.0), 5: (3.5, 6.99)}.items():
    p.label(*P(x, y), str(n), 9, "#1a5fb4ff", True)
# empuje Pa a H'/3, inclinado 10°
h3 = 7.158 / 3
a = math.radians(10)
tip = P(4, h3)
tail = (tip[0] + 70 * math.cos(a), tip[1] - 70 * math.sin(a))
p.arrow(tail, tip, 1.8, "#c01c28ff")
p.label(tail[0] + 4, tail[1] - 6, "Pa", 10, "#c01c28ff", True)
p.label(tail[0] + 4, tail[1] + 8, "10°", 8.5, "#c01c28ff")
p.stroke([P(4.15, 0), P(4.15, h3)], 0.6, "#c01c28ff")
p.label(*P(4.2, h3 / 2 + 0.2), "H′/3", 8.5, "#c01c28ff")
# cotas
def dim_h(x1, x2, y, txt):
    a1, a2 = P(x1, y), P(x2, y)
    p.stroke([a1, a2], 0.6)
    for q in (a1, a2):
        p.stroke([(q[0], q[1] - 3), (q[0], q[1] + 3)], 0.6)
    p.label((a1[0] + a2[0]) / 2 - 8, a1[1] + 3, txt, 8)

def dim_v(x, y1, y2, txt, dx=-28):
    a1, a2 = P(x, y1), P(x, y2)
    p.stroke([a1, a2], 0.6)
    for q in (a1, a2):
        p.stroke([(q[0] - 3, q[1]), (q[0] + 3, q[1])], 0.6)
    p.label(a1[0] + dx, (a1[1] + a2[1]) / 2 - 6, txt, 8)

dim_h(0, 0.7, -0.6, "0.7")
dim_h(0.7, 0.9, -0.6, "0.2")
dim_h(0.9, 1.4, -0.6, "0.5")
dim_h(1.4, 4, -0.6, "2.6")
dim_h(0, 4, -0.95, "B = 4.0 m")
dim_v(-0.5, 0, 0.7, "0.7", -16)
dim_v(-0.5, 0.7, 6.7, "H2 = 6.0", -40)
dim_v(-1.0, 0, 1.5, "D = 1.5", -36)
dim_v(4.9, 6.7, 7.158, "H1", 6)
dim_v(5.25, 0, 7.158, "H′ = 7.158 m", 6)
dim_h(0.9, 1.4, 6.95, "0.5")
p.label(X0, 790, "Figura 1. Sección del muro (1 m = 55 pt). AB es el plano virtual donde actúa el empuje; C es el eje de volcamiento.", 9)

# ---------------- Página 2: empujes y fuerzas ----------------
p = Page()
p.text(0, X0, "2. Empuje activo de Rankine sobre el plano AB", calc=False, bold=True, color=BLUE)
p.text(1, X0, "Relleno inclinado α (Das ec. 7.21). La guía nota que Das usa 0.3532 (fila α = 11° de su tabla); la fórmula da el valor correcto.",
       calc=False, size=9)
p.text(2, X0, "K_a:=COS(α)*(COS(α)-RAIZ(COS(α)^2-COS(φ_1)^2))/(COS(α)+RAIZ(COS(α)^2-COS(φ_1)^2))=")
p.text(4, X0, "P_a:=1/2*γ_1*H′^2*K_a=")
p.text(5, X0, "P_h:=P_a*COS(α)=")
p.text(5, col(14), "P_v:=P_a*SENO(α)=")

p.text(6, X0, "3. Fuerzas verticales y momento resistente respecto a la punta C", calc=False, bold=True, color=BLUE)
p.text(7, X0, "Secciones de la figura 1: 1 y 2 vástago, 3 base, 4 y 5 suelo sobre el talón. Pv actúa sobre AB, a B de C.", calc=False, size=9)
p.text(8, X0, "Peso por metro de muro", calc=False, size=10)
p.text(8, col(14), "Brazo a C", calc=False, size=10)
p.text(8, col(24), "Momento", calc=False, size=10)
tab = [("W_1:=γ_c*b_1*H2=", "x_1:=t_p+b_2+b_1/2="), ("W_2:=γ_c*b_2*H2/2=", "x_2:=t_p+2*b_2/3="),
       ("W_3:=γ_c*B*H3=", "x_3:=B/2="), ("W_4:=γ_1*L_t*H2=", "x_4:=B-L_t/2="), ("W_5:=γ_1*L_t*H1/2=", "x_5:=B-L_t/3=")]
for i, (w, x) in enumerate(tab):
    p.text(9 + i, X0, w)
    p.text(9 + i, col(14), x)
    p.text(9 + i, col(24), f"M_{i+1}:=W_{i+1}*x_{i+1}=?kN*m/m")
p.text(14, X0, "ΣV:=W_1+W_2+W_3+W_4+W_5+P_v=")
p.text(15, X0, "M_R:=M_1+M_2+M_3+M_4+M_5+P_v*B=?kN*m/m")

p.text(17, X0, "4. Volcamiento (NSR-10 Tabla H.6.9-1: FS ≥ 3.0 estático)", calc=False, bold=True, color=BLUE)
p.text(18, X0, "M_o:=P_h*H′/3=?kN*m/m")
p.text(18, col(13), "FS_volc:=M_R/M_o=")
p.text(18, col(22), "FS_volc≥3=")
p.text(19, X0, "No cumple NSR-10: Das acepta FS ≥ 2.0, la NSR-10 pide 3.0. Hay que redimensionar (paso 7 de la guía).",
       calc=False, size=9, color="#c01c28ff")

p.text(20, X0, "5. Deslizamiento (δ = k1·φ2, adhesión k2·c2, con k1 = k2 = 2/3)", calc=False, bold=True, color=BLUE)
p.text(21, X0, "k_1:=2/3")
p.text(21, col(6), "k_2:=2/3")
p.text(21, col(12), "K_p:=TAN(45°+φ_2/2)^2=")
p.text(22, X0, "P_p:=1/2*K_p*γ_2*D^2+2*c_2*RAIZ(K_p)*D=")
p.text(23, X0, "Como Das, contando el pasivo frente al pie:", calc=False, size=10)
p.text(24, X0, "FS_Das:=(ΣV*TAN(k_1*φ_2)+B*k_2*c_2+P_p)/P_h=")
p.text(25, X0, "Como exige la NSR-10 H.6.7(b), sin el pasivo:", calc=False, size=10)
p.text(26, X0, "FS_desl:=(ΣV*TAN(k_1*φ_2)+B*k_2*c_2)/P_h=")
p.text(26, col(22), "FS_desl≥1.6=")
p.text(27, X0, "No cumple NSR-10 (mínimo 1.60 sin pasivo). Solución de la guía: dentellón bajo la base.",
       calc=False, size=9, color="#c01c28ff")

# ---------------- Página 3: excentricidad y capacidad portante ----------------
p = Page()
p.text(0, X0, "6. Excentricidad y presiones bajo la base", calc=False, bold=True, color=BLUE)
p.text(1, X0, "x_R:=(M_R-M_o)/ΣV=")
p.text(1, col(13), "e:=B/2-x_R=")
p.text(1, col(22), "e≤B/6=")
p.text(2, X0, "q_max:=ΣV/B*(1+6*e/B)=")
p.text(2, col(16), "q_min:=ΣV/B*(1-6*e/B)=")
p.text(3, X0, "La resultante cae en el tercio central: toda la base está comprimida (trapecio de presiones).", calc=False, size=9)

p.text(4, X0, "6a. Capacidad portante (Meyerhof con base reducida y carga inclinada, Das ec. 8.22)", calc=False, bold=True, color=BLUE)
p.text(5, X0, "Factores de capacidad para φ2:", calc=False, size=10)
p.text(6, X0, "N_q:=TAN(45°+φ_2/2)^2*EXP(π*TAN(φ_2))=")
p.text(6, col(19), "N_c:=(N_q-1)/TAN(φ_2)=")
p.text(7, X0, "N_γ:=2*(N_q+1)*TAN(φ_2)=")
p.text(7, col(13), "q:=γ_2*D=")
p.text(7, col(21), "B′:=B-2*e=")
p.text(8, X0, "Profundidad e inclinación:", calc=False, size=10)
p.text(9, X0, "F_qd:=1+2*TAN(φ_2)*(1-SENO(φ_2))^2*D/B′=")
p.text(9, col(19), "F_cd:=F_qd-(1-F_qd)/(N_c*TAN(φ_2))=")
p.text(10, X0, "ψ:=ATAN(P_h/ΣV)=?°")
p.text(10, col(11), "F_ci:=(1-ψ/90°)^2=")
p.text(10, col(21), "F_γi:=(1-ψ/φ_2)^2=")
p.text(11, X0, "q_u:=c_2*N_c*F_cd*F_ci+q*N_q*F_qd*F_ci+1/2*γ_2*B′*N_γ*F_γi=")
p.text(12, X0, "FS_cap:=q_u/q_max=")
p.text(12, col(12), "FS_cap≥3=")
p.text(13, X0, "Con el Ka de Das (0.3532) este FS da 2.98 y Das (p. 393) pide redimensionar; con el Ka correcto queda justo en 3.0.",
       calc=False, size=9, color="#c01c28ff")

p.text(15, X0, "Resumen del muro original", calc=False, bold=True, color=BLUE)
p.text(16, X0, "Verificación", calc=False, size=10)
p.text(16, col(11), "Calculado", calc=False, size=10)
p.text(16, col(18), "Mínimo NSR-10", calc=False, size=10)
p.text(16, col(24), "Estado", calc=False, size=10)
rows = [("Volcamiento", "FS_volc=", "3.0", "FS_volc≥3="),
        ("Deslizamiento sin pasivo", "FS_desl=", "1.6", "FS_desl≥1.6="),
        ("Excentricidad", "e=", "B/6 = 0.667 m", "e≤B/6="),
        ("Capacidad portante", "FS_cap=", "3.0", "FS_cap≥3=")]
for i, (name, val, mn, chk) in enumerate(rows):
    r = 17 + i
    p.text(r, X0, name, calc=False, size=10)
    p.text(r, col(11), val)
    p.text(r, col(18), mn, calc=False, size=10)
    p.text(r, col(24), chk)
p.text(22, X0, "Conclusión: el muro del ejemplo no cumple la NSR-10 en volcamiento ni en deslizamiento. La guía lo rediseña",
       calc=False, size=10)
p.text(23, X0, "con B = 5.20 m, puntera de 1.1 m y un dentellón de 0.40 × 0.80 m: páginas 4 a 6. Diseño estructural: páginas 6 a 12.", calc=False, size=10)

exec(open(__file__.replace('generar_muro.py', 'generar_muro_figuras.py'), encoding='utf-8').read())
exec(open(__file__.replace('generar_muro.py', 'generar_muro_diseno.py'), encoding='utf-8').read())
exec(open(__file__.replace('generar_muro.py', 'generar_muro_dibujo.py'), encoding='utf-8').read())

pages[2].gap(10)
pages[0].items = [it for it in pages[0].items]
doc = ('<?xml version="1.0" standalone="no"?>\n<xournal creator="xournalpp 1.3.8+dev" fileversion="5">\n'
       '<title>Xournal++ document - see https://xournalpp.github.io/</title>\n'
       + "\n".join(pg.xml() for pg in pages) + '\n</xournal>\n')
with gzip.open(sys.argv[1], "wt", encoding="utf-8") as f:
    f.write(doc)
print("ok", len(pages), "páginas")
