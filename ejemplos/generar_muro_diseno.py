# Rediseño (paso 7) y diseño estructural (pasos 8 y 9). Se ejecuta dentro de gen.py.
RED = "#c01c28ff"
C2 = col(17)   # segunda columna


def head(pg, r, s):
    pg.text(r, X0, s, calc=False, bold=True, color=BLUE)


def note(pg, r, s, color="#000000ff"):
    pg.text(r, X0, s, calc=False, size=9, color=color)


def row(pg, r, *items):
    """items: (columna, texto) o texto (columna 0). Texto que empieza con '#' va como nota normal."""
    for it in items:
        c, t = it if isinstance(it, tuple) else (0, it)
        x = col(c)
        if t.startswith("#"):
            pg.text(r, x, t[1:], calc=False, size=10)
        else:
            pg.text(r, x, t)


# ---------------- Página 4: rediseño, caso estático ----------------
p = Page()
head(p, 0, "7. Rediseño para cumplir la NSR-10 (paso 7 de la guía)")
note(p, 1, "Se alarga la base y la puntera y se agrega un dentellón bajo el vástago. Se redefinen solo los datos que cambian.")
row(p, 2, "B:=5.2m", (6, "t_p:=1.1m"), (12, "γ_c:=23.5kN/m^3"), (21, "h_k:=0.8m"), (26, "b_k:=0.4m"))
row(p, 3, "L_t:=B-t_p-b_2-b_1=", (11, "H1:=L_t*TAN(α)="), (22, "H′:=H1+H2+H3="))
head(p, 4, "7a. Caso estático")
row(p, 5, "P_a:=1/2*γ_1*H′^2*K_a=", (C2 and 17, "P_h:=P_a*COS(α)="))
row(p, 6, "P_v:=P_a*SENO(α)=")
row(p, 7, "#Peso", (11, "#Brazo a C"), (22, "#Altura del centroide"))
tab = [("W_1:=γ_c*b_1*H2=", "x_1:=t_p+b_2+b_1/2=", "y_1:=H3+H2/2="),
       ("W_2:=γ_c*b_2*H2/2=", "x_2:=t_p+2*b_2/3=", "y_2:=H3+H2/3="),
       ("W_3:=γ_c*B*H3=", "x_3:=B/2=", "y_3:=H3/2="),
       ("W_4:=γ_1*L_t*H2=", "x_4:=B-L_t/2=", "y_4:=H3+H2/2="),
       ("W_5:=γ_1*L_t*H1/2=", "x_5:=B-L_t/3=", "y_5:=H3+H2+H1/3=")]
for i, (w, x, y) in enumerate(tab):
    row(p, 8 + i, w, (11, x), (22, y))
row(p, 13, "ΣV:=W_1+W_2+W_3+W_4+W_5+P_v=", (19, "M_o:=P_h*H′/3=?kN*m/m"))
row(p, 14, "M_R:=W_1*x_1+W_2*x_2+W_3*x_3+W_4*x_4+W_5*x_5+P_v*B=?kN*m/m")
row(p, 15, "FS_volc:=M_R/M_o=", (12, "FS_volc≥3="))
row(p, 16, "#Deslizamiento sin pasivo frente al pie; el pasivo del dentellón sí cuenta (solo el suelo bajo la base):")
row(p, 17, "R_b:=ΣV*TAN(k_1*φ_2)+B*k_2*c_2=")
row(p, 18, "P_pk:=1/2*K_p*γ_2*h_k^2+2*c_2*RAIZ(K_p)*h_k=")
row(p, 19, "FS_sin:=R_b/P_h=", (10, "FS_desl:=(R_b+P_pk)/P_h="), (22, "FS_desl≥1.6="))
row(p, 20, "e:=B/2-(M_R-M_o)/ΣV=", (13, "e≤B/6="), (20, "B′:=B-2*e="))
row(p, 21, "q_max:=ΣV/B*(1+6*e/B)=", (C2 and 17, "q_min:=ΣV/B*(1-6*e/B)="))
row(p, 22, "F_qd:=1+2*TAN(φ_2)*(1-SENO(φ_2))^2*D/B′=", (19, "F_cd:=F_qd-(1-F_qd)/(N_c*TAN(φ_2))="))
row(p, 23, "ψ:=ATAN(P_h/ΣV)=?°", (10, "F_ci:=(1-ψ/90°)^2="), (20, "F_γi:=(1-ψ/φ_2)^2="))
row(p, 24, "q_u:=c_2*N_c*F_cd*F_ci+q*N_q*F_qd*F_ci+1/2*γ_2*B′*N_γ*F_γi=")
row(p, 25, "FS_cap:=q_u/q_max=", (12, "FS_cap≥3="))
note(p, 26, "Nc, Nq, Nγ y q = γ2·D son los de la página 3.")

# ---------------- Página 5: sobrecarga ----------------
p = Page()
head(p, 0, "7b. Caso con sobrecarga q = 10 kPa (NSR-10 H.6.3)")
note(p, 1, "Empuja como un rectángulo Ka·q a H′/2. Su peso sobre el talón no estabiliza, pero sí se suma para las presiones.")
row(p, 2, "q_s:=10kPa", (8, "P_q:=K_a*q_s*H′="))
row(p, 3, "P_qh:=P_q*COS(α)=", (17, "P_qv:=P_q*SENO(α)="))
row(p, 4, "ΣV_q:=ΣV+P_qv=", (17, "M_Rq:=M_R+P_qv*B=?kN*m/m"))
row(p, 5, "M_oq:=M_o+P_qh*H′/2=?kN*m/m", (17, "FS_vq:=M_Rq/M_oq="), (25, "FS_vq≥3="))
row(p, 6, "FS_dq:=(ΣV_q*TAN(k_1*φ_2)+B*k_2*c_2+P_pk)/(P_h+P_qh)=", (23, "FS_dq≥1.6="))
note(p, 7, "Sin el dentellón este FS sería 1.46 < 1.6: por eso se necesita.")
row(p, 8, "e_q:=B/2-(M_Rq+q_s*L_t*x_4-M_oq)/(ΣV_q+q_s*L_t)=", (22, "e_q≤B/6="))
row(p, 9, "q_maxq:=(ΣV_q+q_s*L_t)/B*(1+6*e_q/B)=", (19, "B′_q:=B-2*e_q="))
row(p, 10, "ψ_q:=ATAN((P_h+P_qh)/(ΣV_q+q_s*L_t))=?°")
row(p, 11, "F_qdq:=1+2*TAN(φ_2)*(1-SENO(φ_2))^2*D/B′_q=", (19, "F_cdq:=F_qdq-(1-F_qdq)/(N_c*TAN(φ_2))="))
row(p, 12, "q_uq:=c_2*N_c*F_cdq*(1-ψ_q/90°)^2+q*N_q*F_qdq*(1-ψ_q/90°)^2+1/2*γ_2*B′_q*N_γ*(1-ψ_q/φ_2)^2=")
row(p, 13, "FS_cq:=q_uq/q_maxq=", (12, "FS_cq≥3="))

head(p, 15, "7c. Caso sísmico (Mononobe-Okabe, NSR-10 H.6.8 y H.5.2.5)")
note(p, 16, "Santa Marta: Aa = 0.15, perfil C con Fa = 1.2. Sobre AB: β = 90°, δ = α. El incremento ΔPAE actúa a 0.6H′.")
row(p, 17, "a_max:=0.15*1.2=", (8, "k_h:=0.8*a_max="), (16, "θ:=ATAN(k_h)=?°"))
row(p, 18, "K_AE:=COS(φ_1-θ)^2/(COS(θ)*COS(α+θ)*(1+RAIZ(SENO(φ_1+α)*SENO(φ_1-θ-α)/(COS(α+θ)*COS(α))))^2)=")
row(p, 20, "P_AE:=1/2*γ_1*H′^2*K_AE=", (17, "ΔP:=P_AE-P_a="))
row(p, 21, "F_i:=k_h*(W_1+W_2+W_3+W_4+W_5)=")
row(p, 22, "M_i:=k_h*(W_1*y_1+W_2*y_2+W_3*y_3+W_4*y_4+W_5*y_5)=?kN*m/m")
row(p, 23, "ΣV_E:=ΣV+ΔP*SENO(α)=", (17, "M_RE:=M_R+ΔP*SENO(α)*B=?kN*m/m"))
row(p, 24, "M_oE:=M_o+ΔP*COS(α)*0.6*H′+M_i=?kN*m/m")
row(p, 25, "FS_vE:=M_RE/M_oE=", (12, "FS_vE≥2="))
note(p, 26, "Continúa en la página siguiente.")

# ---------------- Página 6: sismo (cont.) y resumen ----------------
p = Page()
head(p, 0, "7c. Caso sísmico (continuación)")
row(p, 1, "F_dE:=P_h+ΔP*COS(α)+F_i=")
row(p, 2, "FS_dE:=(ΣV_E*TAN(k_1*φ_2)+B*k_2*c_2+P_pk)/F_dE=", (21, "FS_dE≥1.05="))
row(p, 3, "e_E:=B/2-(M_RE-M_oE)/ΣV_E=", (15, "e_E≤B/4="))
row(p, 4, "L_E:=3*(B/2-e_E)=", (12, "#Como e > B/6 la base se despega en parte: presión triangular de longitud L."))
row(p, 5, "q_maxE:=2*ΣV_E/L_E=", (15, "B′_E:=B-2*e_E="))
row(p, 6, "ψ_E:=ATAN(F_dE/ΣV_E)=?°", (15, "#Como ψ > φ2, el término de Nγ se anula (Fγi = 0)."))
row(p, 7, "F_qdE:=1+2*TAN(φ_2)*(1-SENO(φ_2))^2*D/B′_E=", (19, "F_cdE:=F_qdE-(1-F_qdE)/(N_c*TAN(φ_2))="))
row(p, 8, "q_uE:=c_2*N_c*F_cdE*(1-ψ_E/90°)^2+q*N_q*F_qdE*(1-ψ_E/90°)^2=")
row(p, 9, "FS_cE:=q_uE/q_maxE=", (12, "FS_cE≥1.5="))

head(p, 11, "Resumen de estabilidad del muro rediseñado (B = 5.2 m)")
row(p, 12, "#Verificación", (9, "#Estático"), (16, "#Con sobrecarga"), (23, "#Sismo"))
summ = [("#Volcamiento (≥3.0 / ≥2.0)", "FS_volc=", "FS_vq=", "FS_vE="),
        ("#Deslizamiento (≥1.6 / ≥1.05)", "FS_desl=", "FS_dq=", "FS_dE="),
        ("#Excentricidad (B/6 / B/4)", "e=", "e_q=", "e_E="),
        ("#Capacidad (≥3.0 / ≥1.5)", "FS_cap=", "FS_cq=", "FS_cE=")]
for i, (a_, b_, c_, d_) in enumerate(summ):
    row(p, 13 + i, a_, (9, b_), (16, c_), (23, d_))
note(p, 17, "Todo cumple. El sismo gobierna: el volcamiento queda en 2.03, al límite.")
note(p, 18, "La estabilidad general (FS ≥ 1.5 estático) se revisa aparte con un análisis de taludes.")

head(p, 19, "8. Diseño estructural (NSR-10 Título C, cargas mayoradas del Título B)")
note(p, 20, "U1 = 1.2D + 1.6(L+H) con sobrecarga · U2 = 0.9D + 1.0E + 1.6H con sismo · φ = 0.90 a flexión y 0.75 a cortante.")
row(p, 21, "f_c:=28MPa", (6, "f_y:=420MPa"), (12, "φ_f:=0.9"), (17, "φ_v:=0.75"), (22, "b:=1m"))
note(p, 22, "Áreas de barra: No.4 = 129 mm², No.5 = 199 mm², No.6 = 284 mm², No.7 = 387 mm², No.8 = 510 mm².")
row(p, 23, "#Cuantía para un momento Mu por metro (sección rectangular, b = 1 m):")
row(p, 24, "#ρ = 0.85·f′c/fy·(1 − √(1 − 2·Mu/(0.85·φ·f′c·b·d²)))   ·   φVc = φ·0.17·√f′c·b·d  (C.11.2.1.1)")

# ---------------- Página 7: vástago ----------------
p = Page()
head(p, 0, "8.1 Vástago: sección crítica en la unión con la base (h = H2)")
note(p, 1, "Sobre su cara actúa el empuje de su propia altura, no el de H′. Recubrimiento 50 mm, barras No.7.")
row(p, 2, "h:=H2", (5, "P_hs:=1/2*γ_1*h^2*K_a*COS(α)="))
row(p, 3, "P_qs:=K_a*q_s*h*COS(α)=", (17, "ΔP_s:=1/2*γ_1*h^2*(K_AE-K_a)*COS(α)="))
row(p, 4, "M_is:=k_h*(W_1*H2/2+W_2*H2/3)=?kN*m/m")
row(p, 5, "M_u1:=1.6*(P_hs*h/3+P_qs*h/2)=?kN*m/m")
row(p, 6, "M_u2:=1.6*P_hs*h/3+ΔP_s*0.6*h+M_is=?kN*m/m")
row(p, 7, "M_us:=MAX(M_u1;M_u2)=?kN*m/m", (17, "d_s:=700mm-50mm-22.2mm/2="))
row(p, 8, "ρ_s:=0.85*f_c/f_y*(1-RAIZ(1-2*M_us*b/(0.85*φ_f*f_c*b*d_s^2)))=")
row(p, 9, "A_ss:=ρ_s*b*d_s=?mm^2")
row(p, 10, "A_smin:=MAX(0.25*RAIZ(f_c/MPa)*MPa/f_y;1.4MPa/f_y)*b*d_s=?mm^2")
row(p, 11, "#V1: No.7 c/0.14 en la cara del relleno")
row(p, 12, "A_V1:=387mm^2/0.14m*b=?mm^2", (17, "A_V1≥A_ss="))
row(p, 13, "#Cortante a una distancia d de la base (gobierna el sismo): parte estática y parte sísmica")
row(p, 14, "V_Hs:=1.6*1/2*γ_1*(h-d_s)^2*K_a*COS(α)=")
row(p, 15, "V_Es:=1/2*γ_1*(h-d_s)^2*(K_AE-K_a)*COS(α)+k_h*(W_1+W_2)*(h-d_s)/h=")
row(p, 16, "V_us:=V_Hs+V_Es=")
row(p, 17, "φV_cs:=φ_v*0.17*RAIZ(f_c/MPa)*MPa*d_s=?kN/m", (19, "φV_cs≥V_us="))
row(p, 18, "#Refuerzo horizontal (C.14.3.3), en dos capas:")
row(p, 19, "A_sh:=0.0020*b*700mm=?mm^2", (17, "#H1: No.4 c/0.18 por cada cara"))
row(p, 20, "#Corte opcional del 50 % de V1 a 2.57 m sobre la base (V1b), según la guía (paso 9.5).")

# ---------------- Página 8: base y puntera ----------------
p = Page()
head(p, 0, "8.2 Base: presiones con cargas mayoradas")
note(p, 1, "D = concreto, H = suelo y empuje, L = sobrecarga, E = sismo. Momentos respecto a la punta C.")
row(p, 2, "W_D:=W_1+W_2+W_3=", (17, "M_D:=W_1*x_1+W_2*x_2+W_3*x_3=?kN*m/m"))
row(p, 3, "W_H:=W_4+W_5+P_v=", (17, "M_H:=W_4*x_4+W_5*x_5+P_v*B=?kN*m/m"))
row(p, 4, "#U1 = 1.2D + 1.6(L+H), con sobrecarga:")
row(p, 5, "V_1:=1.2*W_D+1.6*(W_H+q_s*L_t+P_qv)=")
row(p, 6, "N_1:=1.2*M_D+1.6*(M_H+q_s*L_t*x_4+P_qv*B)=?kN*m/m")
row(p, 7, "O_1:=1.6*(M_o+P_qh*H′/2)=?kN*m/m", (19, "e_1:=B/2-(N_1-O_1)/V_1="))
row(p, 8, "q_a1:=V_1/B*(1+6*e_1/B)=", (17, "q_b1:=V_1/B*(1-6*e_1/B)="))
row(p, 9, "#U2 = 0.9D + 1.0E + 1.6H, con sismo:")
row(p, 10, "V_2:=0.9*W_D+1.6*W_H+ΔP*SENO(α)=")
row(p, 11, "N_2:=0.9*M_D+1.6*M_H+ΔP*SENO(α)*B=?kN*m/m")
row(p, 12, "O_2:=1.6*M_o+ΔP*COS(α)*0.6*H′+M_i=?kN*m/m", (21, "e_2:=B/2-(N_2-O_2)/V_2="))
row(p, 13, "q_a2:=V_2/B*(1+6*e_2/B)=", (17, "q_b2:=V_2/B*(1-6*e_2/B)="))
note(p, 14, "En ambos casos e ≤ B/6: la presión es un trapecio, de qa bajo la punta a qb bajo el talón.")

head(p, 15, "8.3 Puntera (voladizo desde la cara frontal del vástago, tracción abajo)")
row(p, 16, "q_t1:=q_a1-(q_a1-q_b1)*t_p/B=", (17, "q_t2:=q_a2-(q_a2-q_b2)*t_p/B="))
row(p, 17, "M_p1:=t_p^2*(2*q_a1+q_t1)/6-1.2*γ_c*H3*t_p^2/2=?kN*m/m")
row(p, 18, "M_p2:=t_p^2*(2*q_a2+q_t2)/6-0.9*γ_c*H3*t_p^2/2=?kN*m/m")
row(p, 19, "M_up:=MAX(M_p1;M_p2)=?kN*m/m", (17, "d_p:=700mm-75mm-15.9mm/2="))
row(p, 20, "A_sp:=0.85*f_c/f_y*(1-RAIZ(1-2*M_up*b/(0.85*φ_f*f_c*b*d_p^2)))*b*d_p=?mm^2")
row(p, 21, "#Gobierna el mínimo de retracción y temperatura sobre el área bruta (C.7.12.2.1):")
row(p, 22, "A_spmin:=0.0018*b*H3=?mm^2", (17, "#P1: No.5 c/0.14"))
row(p, 23, "A_P1:=199mm^2/0.14m*b=?mm^2", (17, "A_P1≥MAX(A_sp;A_spmin)="))
row(p, 24, "V_up:=t_p*(q_a2+q_t2)/2-0.9*γ_c*H3*t_p=")
row(p, 25, "φV_cp:=φ_v*0.17*RAIZ(f_c/MPa)*MPa*d_p=?kN/m", (19, "φV_cp≥V_up="))

# ---------------- Página 9: talón y dentellón ----------------
p = Page()
head(p, 0, "8.4 Talón (voladizo desde la cara posterior del vástago, tracción arriba)")
note(p, 1, "Cargas hacia abajo: suelo (1.6H), peso propio, sobrecarga y Pv en el extremo. La reacción del suelo sube.")
row(p, 2, "x_0:=t_p+b_2+b_1=", (10, "M_s:=1.6*(W_4*L_t/2+W_5*2*L_t/3+P_v*L_t)=?kN*m/m"))
row(p, 3, "q_01:=q_a1-(q_a1-q_b1)*x_0/B=", (17, "q_02:=q_a2-(q_a2-q_b2)*x_0/B="))
row(p, 4, "M_t1:=M_s+1.2*γ_c*H3*L_t^2/2+1.6*(q_s*L_t^2/2+P_qv*L_t)-L_t^2*(q_01+2*q_b1)/6=?kN*m/m")
row(p, 5, "M_t2:=M_s+0.9*γ_c*H3*L_t^2/2+ΔP*SENO(α)*L_t-L_t^2*(q_02+2*q_b2)/6=?kN*m/m")
row(p, 6, "M_ut:=MAX(M_t1;M_t2)=?kN*m/m", (17, "d_t:=700mm-50mm-25.4mm/2="))
row(p, 7, "A_st:=0.85*f_c/f_y*(1-RAIZ(1-2*M_ut*b/(0.85*φ_f*f_c*b*d_t^2)))*b*d_t=?mm^2")
row(p, 8, "#T1: No.8 c/0.14 arriba")
row(p, 9, "A_T1:=510mm^2/0.14m*b=?mm^2", (17, "A_T1≥A_st="))
row(p, 10, "V_ut:=1.6*(W_4+W_5+P_v)+0.9*γ_c*H3*L_t+ΔP*SENO(α)-L_t*(q_02+q_b2)/2=")
row(p, 11, "φV_ct:=φ_v*0.17*RAIZ(f_c/MPa)*MPa*d_t=?kN/m", (19, "φV_ct≥V_ut="))

head(p, 13, "8.5 Dentellón (voladizo corto empotrado en la base, empujado por el pasivo)")
note(p, 14, "Se diseña para el pasivo completo con el factor de H. La parte triangular actúa a 2hk/3 de la base; la de cohesión, a hk/2.")
row(p, 15, "P_kt:=1/2*K_p*γ_2*h_k^2=", (13, "P_kc:=2*c_2*RAIZ(K_p)*h_k="))
row(p, 16, "V_uk:=1.6*(P_kt+P_kc)=", (13, "M_uk:=1.6*(P_kt*2*h_k/3+P_kc*h_k/2)=?kN*m/m"))
row(p, 17, "d_k:=400mm-75mm-19.1mm/2=")
row(p, 18, "A_sk:=0.85*f_c/f_y*(1-RAIZ(1-2*M_uk*b/(0.85*φ_f*f_c*b*d_k^2)))*b*d_k=?mm^2")
row(p, 19, "#K1: No.6 c/0.28 en U")
row(p, 20, "A_K1:=284mm^2/0.28m*b=?mm^2", (17, "A_K1≥A_sk="))
row(p, 21, "φV_ck:=φ_v*0.17*RAIZ(f_c/MPa)*MPa*d_k=?kN/m", (19, "φV_ck≥V_uk="))
row(p, 22, "#Acero longitudinal de la base (C.7.12.2.1):")
row(p, 23, "A_L:=0.0018*b*H3=?mm^2", (17, "#L1: No.5 c/0.30 por cada cara"))
