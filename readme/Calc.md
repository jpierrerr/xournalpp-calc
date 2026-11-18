# xournalpp-calc: regiones de cálculo

Extensión de Xournal++ para hacer memorias de cálculo al estilo Mathcad sobre la hoja.

## Uso

1. Activa **Herramientas → Región de cálculo** (Ctrl+Shift+M, o el botón de calculadora). Se selecciona la herramienta de texto.
2. Haz clic en la hoja y escribe una fórmula por línea. Al salir del cuadro (Esc o clic afuera) se muestran los resultados.
3. Las variables definidas quedan disponibles para todas las regiones que están **más abajo** en la hoja (y en las páginas siguientes). Si dos regiones están a la misma altura, se evalúa primero la de la izquierda.

| Escribes | Significa | Se muestra |
| --- | --- | --- |
| `L := 6 m` | define `L` | `L := 6 m` |
| `M := w*L^2/8 =` | define y muestra el valor | `M := w·L²/8 = 112.5 kN·m` |
| `M = ? kN*m` | muestra en las unidades pedidas | `M = 112.5 kN·m` |
| `A_s := 5 cm^2` | subíndice con `_` | A<sub>s</sub> := 5 cm² |
| `V := 2 m3` | potencia de una unidad sin `^` (m3, cm2, mm4, s2…) | V := 2 m³ |
| `P := 3 kn`, `q := 2 kpa` | unidades de 2 o más letras sin importar mayúsculas, si no hay otra igual (kn = kN, kpa = kPa); una sola letra (n, a, v) sigue siendo variable | P := 3 kN |
| `z := 1      m2`, `x := 2m` | la unidad queda siempre a un espacio del número | z := 1 m² |
| `s ≤ fy =` | comparación (`<`, `>`, `≤`, `≥`, `≠`, también en cadena `0 < s ≤ fy`): ✓ cumple (verde) o ✗ no cumple (rojo) | s ≤ fy = ✓ cumple |
| `x := (-b ± √(b²-4·a·c))/(2·a) =` | `±` da los dos resultados; `x` guarda el de +, y `x_1`, `x_2` los dos | x = 3 ; -2 |
| `Cálculo de la viga` | línea sin `=` ni `:=`: texto normal | igual |
| `# nota` | comentario | igual |

- Operadores: `+ - * / ^`, paréntesis, multiplicación implícita (`2.5 m`, `1 kN/1 mm^2`). También `·`, `×`, `²`, `³`, `√`.
- Funciones: `sqrt`, `cbrt`, `abs`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `ln`, `log`, `exp`, `min`, `max`, `round`, `floor`, `ceil`.
- Ángulos: `sin(30 deg)`; los resultados de `asin`, `acos`, `atan` están en radianes (`θ = ? deg` para verlos en grados).
- Errores de unidades (sumar `MPa + m`) y variables no definidas se marcan en rojo junto a la línea.
- Las variables del usuario tienen prioridad sobre las unidades (como en Mathcad): si defines `h := 50 cm`, `h` deja de ser "hora".

## Unidades disponibles

- Longitud: `m km cm mm μm um in ft yd mi`; área/volumen: `ha L mL`
- Masa: `kg gm` (gramo) `mg t ton lb`; `g` es la gravedad (9.80665 m/s²), como en Mathcad
- Tiempo: `s ms min h hr day yr`
- Fuerza: `N kN MN kgf tf tonf lbf kip`
- Presión: `Pa kPa MPa GPa bar atm psi ksi`
- Energía y potencia: `J kJ MJ cal kcal kWh W kW MW hp`
- Otras: `Hz kHz rpm rad deg ° A mA V kV Ω ohm C K mol cd`, constantes `π pi %`

Los resultados sin unidad pedida se muestran en SI con prefijo: N, Pa, N·m, W y N/m usan k, M o G para que el número quede por debajo de 1000.

## Letras griegas

- **Herramientas → Letras griegas y símbolos** (Ctrl+Alt+G) abre una paleta flotante; un clic inserta el símbolo en el texto que estás editando (si no hay texto en edición, lo copia al portapapeles).
- Atajo estilo Mathcad: escribe la letra latina y pulsa **Ctrl+G** (`a`→α, `b`→β, `q`→θ, `f`→φ, `s`→σ, `W`→Ω, ...).

## Archivo

Las regiones se guardan en el `.xopp` como textos con el atributo `calc="true"`. Xournal++ original los abre como texto normal (sin resultados). Los resultados se recalculan al abrir y al exportar a PDF o PNG.

## Malla de alineación

Dos botones (también en el menú Herramientas):

- **Alinear a la malla** (Ctrl+Alt+M): cada cuadro de texto o región de cálculo nuevo empieza en el punto de la malla más cercano al clic, con la línea base sobre un renglón, y sus líneas quedan separadas un renglón (o varios si la letra no cabe).
  La letra queda 1 mm por encima del renglón, para que los puntos no la corten.
- **Malla visible** (Ctrl+Alt+V): muestra u oculta los puntos. La alineación funciona aunque la malla esté oculta. Los puntos solo se ven en pantalla: no salen al imprimir ni al exportar.

La malla es la misma en todos los formatos, para que los apuntes no se desordenen al cambiar el fondo de la hoja:

- Cuadriculado: puntos en las esquinas de los cuadros de 5 mm.
- Cuadriculado con línea vertical: igual, con la línea roja de margen a 5 cuadros del borde izquierdo (como la hoja de renglones). En el archivo es `format=graph` con `vm=70.85`, así que el Xournal++ original lo abre como cuadriculado normal.
- Hoja de parcial: encabezado arriba (asignatura, fecha, nombre, código, profesor, grupo y casilla de nota) y cuadrícula con línea vertical dentro de un marco de esquinas redondeadas, a 2 cuadros de los lados y 1 del borde inferior. Config `vm=70.85,ex=1`; la cuadrícula empieza a 8 cuadros del borde superior.
- Renglones: los renglones están cada **2 cuadros** (10 mm), el primero a 6 cuadros del borde superior y el margen vertical a 5 cuadros del borde izquierdo, así que caen sobre la malla.
- Hoja en blanco: la misma malla de 5 mm.
- Los renglones de escritura están cada 2 filas de puntos en todos los formatos.

La separación entre líneas de cada cuadro de texto se guarda en el archivo (`lineHeight`).

### Moverse con el teclado

Con **Alinear a la malla** activado, mientras escribes en un cuadro:

| Tecla | Qué hace |
| --- | --- |
| Enter | Cierra el cuadro y abre otro inmediatamente delante (a la derecha), en el mismo renglón, dejando un cuadro vacío de la malla entre los dos |
| Shift+Enter | Salto de línea dentro del mismo cuadro |
| Tab | Cierra el cuadro y abre otro en el renglón de abajo, en la misma columna |
| Flechas | Con el cuadro vacío, lo mueven un punto de la malla (arriba/abajo, un renglón). Con texto, mueven el cursor y, al llegar al borde del cuadro, saltan a un cuadro nuevo al lado |
| Ctrl+RePág (Page Up) | Lo que escribas a continuación sale como exponente / superíndice (`1 m` + Ctrl+RePág + `2` → 1 m²). En cálculos escribe `^`. Pulsarlo otra vez lo cancela |
| Ctrl+AvPág (Page Down) | Lo que escribas a continuación sale como subíndice (`A` + Ctrl+AvPág + `s` → Aₛ). En cálculos escribe `_`; después `As` también encuentra a `A_s` |
| `==` | Símbolo de definición ≔ (escribir `:=` también se muestra como ≔). También Ctrl+0 |
| `+-` / `-+`, `-=` / `=-`, `+=` / `=+`, `++`, `--` | Se convierten en ±, ≤, ≥, >, < (`:=-` no se toca) |

- Al llegar al borde derecho se sigue en el renglón de abajo; al llegar al final de la hoja se sigue en la página siguiente (si no existe, se crea una con el mismo fondo).
- Si el punto de destino cae sobre un texto que ya existe, se abre ese texto para editarlo.
- Un cuadro que se deja vacío no se guarda.

## Hoja con membrete propio

Si existe la carpeta `membrete` dentro de la carpeta de configuración (en Windows `%LOCALAPPDATA%\xournal-jackmath\membrete`), con un archivo `membrete.ini`, aparece un formato de página más con tus logos y textos: el mismo marco de la hoja de parcial, con el logo izquierdo, el título en el centro, el logo derecho y una fila de campos para llenar.

```ini
[membrete]
nombre=Formato Unimag
logo_izquierdo=escudo.png
logo_derecho=sellos.png
titulo=UNIVERSIDAD DEL MAGDALENA;Facultad de Ingeniería
campos=Programa;Hoja de cálculo
```

Los logos (PNG o JPG) van en la misma carpeta; el programa recorta el fondo blanco o transparente. Los logos y textos son tuyos y no forman parte del programa. En otro equipo sin esa carpeta, la página se ve como hoja de parcial.

## Carpeta de configuración

Xournal JackMath guarda sus ajustes, barras de herramientas y membrete en su propia carpeta (`xournal-jackmath`; en Windows `%LOCALAPPDATA%\xournal-jackmath`), para no compartirlos con un Xournal++ oficial instalado en el mismo equipo. La primera vez copia automáticamente la carpeta `xournalpp` si existe. El instalador tampoco toca las claves de registro del Xournal++ oficial.
