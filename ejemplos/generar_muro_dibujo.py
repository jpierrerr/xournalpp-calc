# ---------------- Página 8: despiece y dibujo del refuerzo ----------------
p = Page()
head(p, 0, "9. Detallado: marcas de refuerzo (módulo común s0 = 0.14 m)")
hdr = ["Marca", "Ubicación", "Barra", "Forma"]
cols = [0, 3, 13, 19]
for c, h_ in zip(cols, hdr):
    p.text(1, col(c), h_, calc=False, bold=True, size=10)
marks = [("V1", "Vástago, cara del relleno", "No.7 c/0.14", "L con gancho en la base"),
         ("V2", "Vástago, cara frontal", "No.4 c/0.28", "L, gancho hacia la puntera"),
         ("H1", "Vástago, horizontal", "No.4 c/0.18", "Recta, en ambas caras"),
         ("T1", "Talón, arriba", "No.8 c/0.14", "L: todo B + gancho"),
         ("P1", "Puntera, abajo", "No.5 c/0.14", "Recta, todo B"),
         ("L1", "Base, longitudinal", "No.5 c/0.30", "Recta, en ambas caras"),
         ("K1", "Dentellón", "No.6 c/0.28", "U con gancho"),
         ("K2", "Dentellón, longitudinal", "6 No.4", "Recta, una por esquina")]
for i, r_ in enumerate(marks):
    for c, v in zip(cols, r_):
        p.text(2 + i, col(c), v, calc=False, size=10)
note(p, 10, "Separaciones múltiplos de 0.14 m: las barras verticales (V1, V2, K1) nunca caen en el plano de T1 y P1.")
note(p, 11, "Anclaje de V1: ldh = 0.24·fy/√f′c·db = 423 mm ≤ 593 mm disponibles. T1 corre todo B (ld = 1.54 m ≤ 1.725 m).")
note(p, 12, "Drenaje (H.6.5): filtros y drenes detrás del muro; si el relleno se satura hay que sumar el empuje del agua.")
# dibujo del muro rediseñado con su refuerzo
S2 = 46.0
OX2, OY2 = 160.0, 722.0


def Q(x, y):
    return (OX2 + x * S2, OY2 - y * S2)


wall2 = [Q(0, 0), Q(1.7, 0), Q(1.7, -0.8), Q(2.1, -0.8), Q(2.1, 0), Q(5.2, 0), Q(5.2, 0.7), Q(1.8, 0.7),
         Q(1.8, 6.7), Q(1.3, 6.7), Q(1.1, 0.7), Q(0, 0.7), Q(0, 0)]
p.stroke(wall2, 1.6, "#3d3846ff", fill=55)
p.stroke([Q(1.8, 6.7), Q(5.2, 7.3)], 1.2, SOIL)
p.stroke([Q(-0.6, 1.5), Q(1.13, 1.5)], 1.2, SOIL)
for i in range(4):
    x = -0.5 + i * 0.4
    p.stroke([Q(x, 1.5), Q(x - 0.15, 1.35)], 0.6, SOIL)
REB = "#c01c28ff"
c5, c7 = 0.075, 0.05
# V1: cara del relleno, baja con gancho hacia el talón
p.stroke([Q(1.8 - c7, 6.65), Q(1.8 - c7, 0.11), Q(2.35, 0.11)], 1.4, REB)
# V2: cara frontal
p.stroke([Q(1.3 + c7 * 0.9, 6.65), Q(1.1 + c7, 0.75), Q(1.1 + c7, 0.11), Q(0.8, 0.11)], 1.0, REB)
# T1: arriba del talón, todo B con gancho
p.stroke([Q(0.075, 0.7 - c7), Q(5.2 - 0.075, 0.7 - c7), Q(5.2 - 0.075, 0.3)], 1.4, REB)
# P1: abajo, todo B
p.stroke([Q(0.075, c5), Q(5.2 - 0.075, c5)], 1.2, REB)
# K1 en U dentro del dentellón
p.stroke([Q(1.7 + c5, 0.45), Q(1.7 + c5, -0.8 + c5), Q(2.1 - c5, -0.8 + c5), Q(2.1 - c5, 0.3)], 1.1, REB)
p.stroke([Q(1.7 + c5, 0.45), Q(1.4, 0.45)], 1.1, REB)
# H1 y L1 (puntos)
for k in range(1, 12):
    y = 0.7 + k * 0.5
    if y > 6.5:
        break
    xb = 1.8 - c7 - 0.06
    xf = 1.1 + 0.2 * (y - 0.7) / 6 + c7 + 0.06
    for xx in (xb, xf):
        cx, cy = Q(xx, y)
        p.stroke([(cx - 1.2, cy), (cx + 1.2, cy)], 2.4, "#1a5fb4ff")
lbl = [((1.95, 4.2), "V1 No.7 c/0.14"), ((-0.15, 4.2), "V2 No.4 c/0.28"), ((-0.15, 3.6), "H1 No.4 c/0.18"),
       ((3.0, 0.95), "T1 No.8 c/0.14"), ((2.4, -0.35), "P1 No.5 c/0.14"), ((0.6, -0.7), "K1 No.6 c/0.28")]
for (x, y), s_ in lbl:
    cx, cy = Q(x, y)
    p.label(cx, cy, s_, 8.5, REB)
dimy = -1.15
for x1, x2, t in [(0, 1.1, "1.1"), (1.1, 1.8, "0.7"), (1.8, 5.2, "3.4"), (0, 5.2, "B = 5.2 m")]:
    a1, a2 = Q(x1, dimy if t != "B = 5.2 m" else dimy - 0.35), Q(x2, dimy if t != "B = 5.2 m" else dimy - 0.35)
    p.stroke([a1, a2], 0.6)
    for qq in (a1, a2):
        p.stroke([(qq[0], qq[1] - 3), (qq[0], qq[1] + 3)], 0.6)
    p.label((a1[0] + a2[0]) / 2 - 8, a1[1] + 2, t, 8)
p.label(*Q(2.2, -0.65), "Dentellón 0.40 × 0.80", 8)
p.label(X0, 818, "Figura 2. Muro rediseñado con su refuerzo (esquema; diámetros exagerados). Rojo: barras transversales; azul: horizontales.", 9)
