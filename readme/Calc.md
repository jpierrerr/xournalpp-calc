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
- Funciones (como en Excel en español, `;` entre argumentos, sin distinguir mayúsculas): `RAIZ`, `RAIZ3`, `ABS`, `SENO`, `COS`, `TAN`, `ASENO`, `ACOS`, `ATAN`, `ATAN2(x; y)`, `LN`, `LOG10`, `LOG(x; base)`, `EXP`, `MIN`, `MAX`, `REDONDEAR(x; decimales)`, `REDONDEAR.MENOS`, `REDONDEAR.MAS`. Fuera de edición se ven en notación matemática: `sen`, `√x` con barra, `|x|`, `log₂`. Los nombres en inglés de versiones anteriores (`sqrt`, `sin`, `atan2(y, x)`, `round`, `floor`, `ceil`...) siguen funcionando.
- Al escribir un nombre aparece un desplegable con las funciones que empiezan así (flechas para elegir, Tab/Enter/clic para escribirla, Esc para cerrarlo).
- Ángulos: `SENO(30 deg)`; los resultados de `ASENO`, `ACOS`, `ATAN` están en radianes (`θ = ? deg` para verlos en grados).
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
| Ctrl+↑ | Lo que escribas a continuación sale como exponente / superíndice (`1 m` + Ctrl+↑ + `2` → 1 m²). En cálculos escribe `^`. Pulsarlo otra vez lo cancela |
| Ctrl+↓ | Lo que escribas a continuación sale como subíndice (`A` + Ctrl+↓ + `s` → Aₛ). En cálculos escribe `_`; después `As` también encuentra a `A_s` |
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

## Seleccionar con doble clic (herramienta de texto)

Con la herramienta de texto (también en modo cálculo), haz doble clic en un sitio vacío: desde ese punto se dibuja un rectángulo de selección. Puedes arrastrar sin soltar el segundo clic, o soltar y mover el puntero (por ejemplo con el touchpad): el rectángulo lo sigue hasta el siguiente clic. Con la selección puedes mover el grupo (arrastrándolo) o borrarlo (Supr). Si "Alinear a la malla" está activo, al mover el grupo el punto donde hiciste el doble clic queda sobre un punto de la malla. El doble clic sobre un texto existente sigue seleccionando la palabra.

## Tecla Esc

Esc suelta la herramienta en uso, por pasos:

1. Primero cancela lo que esté a medias: el texto o la ecuación que se escribe, la selección, el rectángulo de
   selección que se está trazando (también el que sigue al puntero tras un doble clic), la curva (spline) o el texto
   de PDF seleccionado. Mientras se mantiene el botón pulsado dibujando un trazo o una forma, Esc no hace nada.
2. Si no hay nada a medias, quita la regla o el compás, apaga la región de cálculo y vuelve a la herramienta
   **Seleccionar objeto** (la flecha).

Las opciones de fondo (alinear a la malla, acople de giro, malla visible...) no cambian.

## Personalizar la barra de herramientas

*Editar → Barras de herramientas → Personalizar* abre una ventana con una lista por cada barra (Arriba fila 1,
Arriba fila 2, Izquierda, Derecha, Abajo):

- Arriba están las herramientas visibles, en su orden, agrupadas como en la barra (cada grupo va entre separadores).
  Cada grupo tiene su casilla: desmarcarla quita el grupo completo, que queda en "Grupos quitados" para volver a
  ponerlo; seleccionado, el grupo se sube, se baja, se arrastra o se mueve a otra barra completo. Las herramientas se
  siguen pudiendo manejar una por una. Desmarcar una herramienta la oculta. Se cambian de orden arrastrándolas o
  con **Subir**/**Bajar**; **Mover a** la pasa a otra barra.
- Abajo están las que no están en esa barra, por grupos. Marcar una la pone al final de la barra; si estaba en otra
  barra, se mueve.
- Los cambios se aplican de inmediato y se guardan en `toolbar.ini`. Una barra predefinida se copia la primera vez
  (queda como "... (personalizada)"); **Restaurar original** vuelve a la predefinida y borra la copia.
- **Arrastrar a las barras...** abre el editor anterior de Xournal++ (arrastrar desde una ventana hasta las barras).

## Color y grosor como listas

Por defecto, junto al lápiz hay dos botones: el color actual (abre la lista de colores de la paleta y "Otro color…")
y el grosor actual (abre muy fino … muy grueso). Son `COLOR_LIST` y `SIZE_LIST` en `toolbar.ini`; los botones sueltos
de cada color (`COLOR(n)`) y de cada grosor siguen disponibles en *Personalizar*.

## Formato de texto como en Word

Con la herramienta de texto, el bloque de formato de la barra (`TEXT_FORMAT`) actúa sobre el texto que se edita:
fuente y tamaño, agrandar y achicar (A▲ A▼), mayúsculas y minúsculas (Aa), borrar formato, negrita (Ctrl+B),
cursiva (Ctrl+I), subrayado (Ctrl+U) con tipo simple, doble u ondulado, tachado, subíndice, superíndice, color de
resaltado, color de fuente y alinear a la izquierda, centrar, a la derecha o justificar.

Con texto seleccionado, el formato se aplica a la selección. Sin selección, se aplica a lo que se escriba después en
el cursor (la fuente y el tamaño, sin selección, cambian todo el cuadro). Los botones muestran el formato del
cursor. El formato se guarda en el archivo `.xopp` (atributo `jmFormat` del texto).

Clic derecho dentro del texto que se edita: menú con una barra de formato corta y Cortar, Copiar, Pegar y
Seleccionar todo.

## Interfaz minimalista y pantalla de inicio

Los iconos predeterminados son de línea fina y un solo color (tema "Lucide", se puede volver a "Color" en
Preferencias). Los botones de JackMath, la navegación de páginas, el zoom y la alineación de texto tienen iconos
propios en ese estilo (`xopp-calc-*`, `xopp-go-*`, `xopp-zoom-*`, `xopp-format-*`).

La barra predeterminada ("Portrait") va en dos filas que llenan el ancho de la ventana:

- Fila 1 (documento): nuevo, abrir, guardar | PDF, imprimir | deshacer, rehacer | cortar, copiar, pegar | páginas |
  insertar y borrar hoja … formato de texto | región de cálculo, alinear a la malla, malla visible, griegas, Excel.
- Fila 2 (herramientas): lápiz, color, grosor | borrador, resaltador | texto, LaTeX, imagen, enlace | formas,
  relleno, escuadra, compás | selección, espacio vertical, mano | puntero láser, texto del PDF … imán de giro |
  herramienta predeterminada, pantalla completa.

Hay un solo imán de malla: *Alinear a la malla* de JackMath es también el "Ajustar a la cuadrícula" de Xournal++ (los
dos quedan siempre iguales; al abrir toma el valor guardado). El imán de giro pega los ángulos a múltiplos de 15° al
girar una selección o dibujar líneas; Alt lo invierte mientras se mantiene.

Las opciones del lápiz (formas, reconocedor, relleno, grosor y color) están siempre activas: elegidas con otra
herramienta (borrador, mano, selección…) toman el lápiz junto con la opción. Con *Seleccionar objeto* (la herramienta
tras Esc), un doble clic en un espacio vacío abre el rectángulo de selección.

La grabación de audio ya no está en la barra predeterminada (se puede añadir con *Personalizar*). Al actualizar, una
sola vez, una instalación anterior pasa a los iconos de línea y a esta barra (`jackmathUiVersion` en settings.xml);
una copia personalizada de la barra se conserva y se puede volver a elegir en el menú.

Al abrir el programa sin un archivo aparece la pantalla de inicio: formatos para una hoja nueva con vista previa
(en blanco, cuadriculado, con línea vertical, renglones, hoja de parcial y el membrete propio; *Más formatos…*
muestra los demás), archivos recientes con buscador y la opción de fijarlos arriba (`pinned-files.txt` en la carpeta
de configuración), Abrir, Anotar un PDF, Guía de uso (abre `Guia-Xournal-JackMath.pdf` de la carpeta de
configuración si existe) y Preferencias. Esc o *Nuevo* dejan la hoja en blanco. *Mostrar al iniciar* la desactiva
(`showStartScreen`).

*Archivo* (en la barra de título) no abre una lista: vuelve a esta pantalla, como en Word. Ahí se suman *Volver*
(o Esc), Guardar, Guardar como, Exportar a PDF, Imprimir y Salir; elegir un formato crea un documento nuevo (pregunta
antes si se guarda el actual) y *Nuevo* crea uno en blanco.

## Corrector de ortografía

Mientras se escribe un texto, las palabras mal escritas se subrayan con una línea roja ondulada (no la palabra que se
está escribiendo, ni las regiones de cálculo, ni símbolos y unidades como `kN`, `MPa`, `A_s` o `x2`). Clic derecho
sobre una de ellas ofrece correcciones, *Agregar al diccionario* (queda en `diccionario-personal.txt` de la carpeta
de configuración) e *Ignorar* (hasta cerrar el programa). *Revisar ortografía*, al final de ese menú, lo apaga o
enciende (`spellCheck` en settings.xml).

Usa Hunspell (opción de CMake `ENABLE_SPELLCHECK`, se apaga sola si no está; en MSYS2 el paquete
`mingw-w64-ucrt-x86_64-hunspell`) y el diccionario de español de `ui/dictionaries` (RLA-ES, ver su README).
