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

**Herramientas → Malla de alineación** (Ctrl+Alt+M, o el botón de cuadrícula) muestra una malla de puntos como la de Mathcad. Sigue el fondo de la página:

- Cuadriculado o puntos: un punto en cada esquina de los cuadros.
- Renglones: una fila de puntos sobre cada renglón (y columnas cada media distancia entre renglones, empezando en el margen).
- Otros fondos: puntos cada 5 mm.

Con la malla activa, cada cuadro de texto o región de cálculo nuevo empieza en el punto más cercano al clic, con la línea base sobre una fila de puntos, y la distancia entre sus líneas es la de la malla (o un múltiplo si la letra no cabe). Así cada línea queda sobre un renglón. Esta distancia se guarda en el archivo (`lineHeight`). La malla solo se ve en pantalla: no sale al imprimir ni al exportar.
