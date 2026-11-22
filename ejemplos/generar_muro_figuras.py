# Ilustraciones del diseño estructural (pasos 8.1 a 8.5). Se ejecuta dentro de generar_muro.py.
CONC = "#3d3846ff"
PRESS = "#1a5fb4ff"
LOADC = "#c01c28ff"
BAR = "#c01c28ff"
SECT = "#e66100ff"


def ytop(r):
    return Y0 + r * LH


def poly(pg, pts, w=1.0, c=CONC, fill=None, style=None):
    pg.stroke(pts + [pts[0]] if fill is not None else pts, w, c, style, fill)


def fig_vastago(pg, r0):
    """Vástago como voladizo: empujes sobre su cara y sección crítica."""
    top = ytop(r0) + 8
    hpx = 130.0            # 6 m
    k = hpx / 6.0
    xb = X0 + 110          # cara del relleno
    base_y = top + hpx
    stem = [(xb - 0.7 * k, base_y), (xb, base_y), (xb, top), (xb - 0.5 * k, top)]
    poly(pg, stem, 1.4, CONC, fill=60)
    pg.stroke([(xb - 0.7 * k - 25, base_y), (xb + 95, base_y)], 1.2, CONC)          # base (corte)
    pg.stroke([(xb - 0.7 * k - 25, base_y + 0.7 * k), (xb + 95, base_y + 0.7 * k)], 0.6, CONC, "dash")
    # triángulo de empuje estático
    tri = [(xb, top), (xb + 55, base_y), (xb, base_y)]
    poly(pg, tri, 0.8, PRESS, fill=40)
    # rectángulo de sobrecarga
    rect = [(xb + 55, top), (xb + 67, top), (xb + 67, base_y), (xb + 55, base_y)]
    poly(pg, rect, 0.8, "#26a269ff", fill=40)
    pg.stroke([(xb, top), (xb + 55, top)], 0.5, PRESS, "dot")
    # flechas de las resultantes
    pg.arrow((xb + 85, base_y - hpx / 3), (xb + 2, base_y - hpx / 3), 1.4, PRESS)
    pg.label(xb + 88, base_y - hpx / 3 - 6, "Phs a h/3", 8.5, PRESS)
    pg.arrow((xb + 85, base_y - hpx / 2), (xb + 2, base_y - hpx / 2), 1.2, "#26a269ff")
    pg.label(xb + 88, base_y - hpx / 2 - 6, "Pqs a h/2", 8.5, "#26a269ff")
    pg.arrow((xb + 85, base_y - 0.6 * hpx), (xb + 2, base_y - 0.6 * hpx), 1.2, LOADC)
    pg.label(xb + 88, base_y - 0.6 * hpx - 6, "ΔPs a 0.6h (sismo)", 8.5, LOADC)
    pg.label(xb + 18, base_y - 12, "Ka·γ1·h", 8, PRESS)
    # sección crítica y barra V1
    pg.stroke([(xb - 0.7 * k - 12, base_y), (xb + 12, base_y)], 1.6, SECT, "dash")
    pg.label(xb - 0.7 * k - 92, base_y - 12, "sección crítica", 8.5, SECT)
    pg.stroke([(xb - 4, top + 3), (xb - 4, base_y + 0.55 * k), (xb + 25, base_y + 0.55 * k)], 1.4, BAR)
    pg.label(xb - 0.7 * k - 70, top + 30, "V1 (cara del relleno,", 8, BAR)
    pg.label(xb - 0.7 * k - 70, top + 41, "lado traccionado)", 8, BAR)
    pg.label(xb - 0.7 * k - 48, top - 2, "h = 6.0 m", 8)
    # texto explicativo a la derecha
    tx = X0 + 330
    pg.label(tx, top + 10, "Figura 12. El vástago es un voladizo", 9, bold=True)
    pg.label(tx, top + 24, "empotrado en la base. El empuje de su", 9)
    pg.label(tx, top + 37, "propia altura tracciona la cara del", 9)
    pg.label(tx, top + 50, "relleno: ahí va el acero principal V1.", 9)
    pg.label(tx, top + 70, "Azul: empuje estático (triángulo).", 9, PRESS)
    pg.label(tx, top + 83, "Verde: sobrecarga Ka·q (rectángulo).", 9, "#26a269ff")
    pg.label(tx, top + 96, "Rojo: incremento sísmico ΔPs.", 9, LOADC)
    pg.label(tx, top + 109, "Naranja: sección de diseño (Mu, Vu a d).", 9, SECT)


def fig_base(pg, r0):
    """Presiones mayoradas bajo la base para U1 y U2."""
    top = ytop(r0) + 48
    k = 75.0                       # pt por metro
    x0 = X0 + 20
    B_, tp_, ts_ = 5.2, 1.1, 0.7
    sc = 0.32                      # pt por kPa
    cases = [("U1 = 1.2D + 1.6(L+H)", 209.96, 147.39), ("U2 = 0.9D + 1.0E + 1.6H", 280.29, 38.12)]
    y = top
    for name, qa, qb in cases:
        # base
        poly(pg, [(x0, y), (x0 + B_ * k, y), (x0 + B_ * k, y + 0.7 * 20), (x0, y + 0.7 * 20)], 1.2, CONC, fill=60)
        poly(pg, [(x0 + tp_ * k, y), (x0 + (tp_ + ts_) * k, y), (x0 + (tp_ + ts_) * k, y - 30), (x0 + (tp_ + 0.2) * k, y - 30)],
             1.2, CONC, fill=60)
        yb = y + 14
        pg.label(x0 - 12, yb - 10, "C", 9, LOADC, True)
        # trapecio de presiones hacia arriba (dibujado bajo la base)
        trap = [(x0, yb), (x0 + B_ * k, yb), (x0 + B_ * k, yb + qb * sc), (x0, yb + qa * sc)]
        poly(pg, trap, 0.9, PRESS, fill=45)
        for i in range(9):
            xx = x0 + (i + 0.5) * B_ * k / 9
            qq = qa - (qa - qb) * (i + 0.5) / 9
            pg.arrow((xx, yb + qq * sc - 2), (xx, yb + 2), 0.7, PRESS, 4)
        pg.label(x0 + 4, yb + qa * sc + 2, f"qa = {qa:.1f} kPa", 8.5, PRESS)
        pg.label(x0 + B_ * k + 5, yb + qb * sc - 4, f"qb = {qb:.1f}", 8.5, PRESS)
        # caras del vástago
        for xf in (tp_, tp_ + ts_):
            pg.stroke([(x0 + xf * k, y - 34), (x0 + xf * k, yb + 100)], 0.8, SECT, "dash")
        pg.label(x0 + 3.0 * k, y - 32, name, 9, bold=True)
        y += 150
    pg.label(x0 + 2, top - 16, "puntera", 8.5, SECT)
    pg.label(x0 + 3.0 * k, top - 14, "talón (3.4 m)", 8.5, SECT)
    pg.label(X0, top + 2 * 150 - 22, "Figura 13. Presiones mayoradas bajo la base. Las líneas naranjas son las caras del vástago,", 9)
    pg.label(X0, top + 2 * 150 - 10, "donde se cortan la puntera y el talón como voladizos. En los dos casos e ≤ B/6 (trapecio).", 9)


def fig_puntera(pg, r0):
    top = ytop(r0) + 30
    k = 150.0
    x0 = X0 + 60
    L_ = 1.1
    th = 0.7 * 60
    poly(pg, [(x0, top), (x0 + L_ * k, top), (x0 + L_ * k, top + th), (x0, top + th)], 1.2, CONC, fill=60)
    pg.stroke([(x0 + L_ * k, top - 40), (x0 + L_ * k, top + th + 90)], 1.6, SECT, "dash")
    pg.label(x0 + L_ * k + 40, top + 8, "cara frontal del vástago", 8.5, SECT)
    pg.label(x0 + L_ * k + 40, top + 20, "(sección crítica)", 8.5, SECT)
    poly(pg, [(x0 + L_ * k, top - 40), (x0 + L_ * k + 30, top - 40), (x0 + L_ * k + 30, top), (x0 + L_ * k, top)], 0.6, CONC,
         style="dot")
    # presión hacia arriba U2
    qa, qt = 280.29, 229.06
    sc = 0.3
    yb = top + th
    poly(pg, [(x0, yb), (x0 + L_ * k, yb), (x0 + L_ * k, yb + qt * sc), (x0, yb + qa * sc)], 0.9, PRESS, fill=45)
    for i in range(6):
        xx = x0 + (i + 0.5) * L_ * k / 6
        qq = qa - (qa - qt) * (i + 0.5) / 6
        pg.arrow((xx, yb + qq * sc - 2), (xx, yb + 2), 0.8, PRESS, 5)
    pg.label(x0 - 50, yb + qa * sc - 10, "qa = 280.3", 8.5, PRESS)
    pg.label(x0 + L_ * k + 8, yb + qt * sc - 6, "qt = 229.1 kPa", 8.5, PRESS)
    # peso propio
    pg.arrow((x0 + L_ * k / 2, top - 30), (x0 + L_ * k / 2, top - 2), 1.3, LOADC)
    pg.label(x0 + 5, top - 44, "0.9·γc·H3 (peso propio)", 8.5, LOADC)
    # barra P1 abajo
    pg.stroke([(x0 + 4, top + th - 6), (x0 + L_ * k + 60, top + th - 6)], 1.6, BAR)
    pg.label(x0 + 10, top + th - 20, "P1 No.5 c/0.14 (tracción abajo)", 8.5, BAR)
    pg.label(x0, top + th / 2 - 5, "", 8)
    tx = X0 + 300
    pg.label(tx, top + 60, "Figura 14. La puntera es un voladizo", 9, bold=True)
    pg.label(tx, top + 74, "que el suelo empuja hacia arriba.", 9)
    pg.label(tx, top + 87, "Momento en la cara del vástago:", 9)
    pg.label(tx, top + 100, "Mu = L²(2qa + qt)/6 − peso propio.", 9)
    pg.label(tx, top + 113, "Tracción abajo: el acero va abajo.", 9)


def fig_talon(pg, r0):
    top = ytop(r0) + 88
    k = 95.0
    x0 = X0 + 40
    L_ = 3.4
    th = 0.7 * 40
    # vástago (trozo) y talón
    poly(pg, [(x0 - 0.7 * k * 0.5, top - 55), (x0, top - 55), (x0, top), (x0 - 0.7 * k * 0.5, top)], 0.6, CONC, style="dot")
    poly(pg, [(x0, top), (x0 + L_ * k, top), (x0 + L_ * k, top + th), (x0, top + th)], 1.2, CONC, fill=60)
    pg.stroke([(x0, top - 70), (x0, top + th + 62)], 1.6, SECT, "dash")
    pg.label(x0 + 6, top - 80, "cara posterior del vástago (sección crítica)", 8.5, SECT)
    # suelo sobre el talón (cargas hacia abajo)
    for i in range(7):
        xx = x0 + (i + 0.5) * L_ * k / 7
        pg.arrow((xx, top - 40), (xx, top - 2), 0.8, "#986a44ff", 5)
    pg.stroke([(x0, top - 40), (x0 + L_ * k, top - 40)], 0.8, "#986a44ff")
    pg.label(x0 + 10, top - 54, "1.6·(W4 + W5): suelo sobre el talón (+ sobrecarga en U1)", 8.5, "#986a44ff")
    # Pv en el extremo
    pg.arrow((x0 + L_ * k, top - 60), (x0 + L_ * k, top - 2), 1.5, LOADC)
    pg.label(x0 + L_ * k + 5, top - 64, "1.6·Pv", 9, LOADC, True)
    # reacción del suelo U2: q0 = 196.5 en la cara, qb = 38.1 en el extremo
    q0, qb = 196.46, 38.12
    sc = 0.30
    yb = top + th
    poly(pg, [(x0, yb), (x0 + L_ * k, yb), (x0 + L_ * k, yb + qb * sc), (x0, yb + q0 * sc)], 0.9, PRESS, fill=45)
    for i in range(8):
        xx = x0 + (i + 0.5) * L_ * k / 8
        qq = q0 - (q0 - qb) * (i + 0.5) / 8
        pg.arrow((xx, yb + qq * sc - 2), (xx, yb + 2), 0.8, PRESS, 5)
    pg.label(x0 + 6, yb + q0 * sc + 3, "q0 = 196.5 kPa", 8.5, PRESS)
    pg.label(x0 + L_ * k + 6, yb + qb * sc - 6, "qb = 38.1", 8.5, PRESS)
    # barra T1 arriba con gancho
    pg.stroke([(x0 - 60, top + 5), (x0 + L_ * k - 5, top + 5), (x0 + L_ * k - 5, top + th - 5)], 1.6, BAR)
    pg.label(x0 + 150, top + 9, "T1 No.8 c/0.14 (tracción arriba)", 8.5, BAR)
    pg.label(X0, yb + 92, "Figura 15. El talón es un voladizo cargado hacia abajo por el suelo, su peso y Pv en el extremo;", 9)
    pg.label(X0, yb + 104, "la reacción del suelo sube pero es menor. Por eso la tracción queda arriba y el acero va arriba.", 9)


def fig_dentellon(pg, r0):
    top = ytop(r0) + 40
    k = 150.0
    x0 = X0 + 205
    bk, hk = 0.4, 0.8
    # base (trozo) y dentellón
    poly(pg, [(x0 - 90, top - 40), (x0 + bk * k + 90, top - 40), (x0 + bk * k + 90, top), (x0 - 90, top)], 0.8, CONC, fill=40)
    poly(pg, [(x0, top), (x0 + bk * k, top), (x0 + bk * k, top + hk * k), (x0, top + hk * k)], 1.3, CONC, fill=60)
    pg.stroke([(x0 - 100, top), (x0 + bk * k + 100, top)], 1.6, SECT, "dash")
    pg.label(x0 + bk * k + 30, top + 4, "sección crítica (unión con la base)", 8.5, SECT)
    # pasivo: rectángulo de cohesión + triángulo de fricción, empujando hacia la derecha (desde la puntera)
    wc = 45.0
    wt = 14.0
    poly(pg, [(x0, top), (x0 - wc, top), (x0 - wc - wt, top + hk * k), (x0, top + hk * k)], 0.9, PRESS, fill=45)
    pg.stroke([(x0 - wc, top), (x0 - wc, top + hk * k)], 0.6, PRESS, "dot")
    for i in range(5):
        yy = top + (i + 0.5) * hk * k / 5
        pg.arrow((x0 - wc - wt * (i + 0.5) / 5 + 2, yy), (x0 - 2, yy), 0.8, PRESS, 5)
    pg.label(x0 - wc - 115, top + 15, "2·c2·√Kp (cohesión)", 8.5, PRESS)
    pg.label(x0 - wc - 115, top + hk * k - 10, "+ Kp·γ2·z (triángulo)", 8.5, PRESS)
    # barra K1 en U con gancho
    c = 9
    pg.stroke([(x0 + c - 22, top - 30), (x0 + c, top - 30), (x0 + c, top + hk * k - c), (x0 + bk * k - c, top + hk * k - c),
               (x0 + bk * k - c, top - 25)], 1.6, BAR)
    pg.label(x0 + bk * k + 10, top + hk * k / 2, "K1 No.6 c/0.28 en U", 8.5, BAR)
    pg.label(x0 + bk * k + 10, top + hk * k / 2 + 12, "(cara frontal traccionada)", 8.5, BAR)
    pg.label(x0 + 10, top + hk * k + 6, "0.40 m", 8)
    pg.label(x0 + bk * k + 4, top + hk * k - 22, "hk = 0.80 m", 8)
    pg.label(X0, top + hk * k + 30, "Figura 16. El dentellón es un voladizo corto colgado de la base. El pasivo del suelo frente a él", 9)
    pg.label(X0, top + hk * k + 42, "lo empuja hacia el talón: la cara frontal queda traccionada y ahí trabaja la pata de K1.", 9)
