# Optimización del renderizado CPU de Vortex

Se conserva una sola mejora. El muestreador de planos usa un resto calculado con FMA cuando la aritmética binaria de doble precisión y el rango del cociente permiten reproducir exactamente el resultado de `std::fmod`. El cambio acelera el muestreo de texturas de dimensiones arbitrarias y conserva la rama existente de coordenadas fijas para 128×128.

## Método

- Referencia en `3cb4712` con el mismo código de diagnóstico que la versión final. MacBook Air Apple M5, macOS 26.6.2, AppleClang 21.0, Release con `-O3 -DNDEBUG -std=c++17`. Benchmark monohilo sin SDL3. El tiempo total abarca `first_person::render`; la preparación de superficies por el consumidor se mide aparte.
- Siete escenas sintéticas y tres poses repetidas. `dense` combina 259 segmentos estáticos, 1 dinámico y paredes altas que cubren toda la imagen. `open` deja visibles suelo y techo con fisheye de intensidad cero. `short` combina paredes bajas y cobertura parcial. `scalar` usa planos 37×65 y 8×9 con periodo 193,5. `sprites` superpone 48 instancias y dos efectos en una segunda capa. `mixed` combina rejilla, cobertura parcial, dos capas y deformación positiva. `warp` combina un espacio abierto y deformación más intensa.
- Seis tamaños de 320×200 a 3840×2160. El total usa 124 muestras calientes por celda y versión, tomadas en cuatro procesos por versión en orden ABBA ABBA. El perfil por fase usa 42 muestras por celda y versión en orden ABBA. Se descartan seis calentamientos por celda. El primer calentamiento queda registrado como muestra fría `-6`. P95 es el valor en posición `ceil(0.95 × n)` de las muestras ordenadas.
- El binario de total usa el motor habitual sin instrumentación. El binario de perfil recompila el mismo orquestador con cronómetros exclusivos para preparación de búferes, rayos, proyección de paredes, callback de material, raster de paredes, planos, profundidad base, sprites y fisheye. La escritura de profundidad por sprite se incluye en sprites. Las proyecciones de filas y sprites se incluyen en sus respectivas fases. La suma de medianas de fases puede diferir del total mediano. En particular, los tramos por columna incluyen el coste de lectura del reloj.
- La memoria se mide con las peticiones al asignador ordinario de C++ dentro del render. Se registra memoria retenida por el pipeline, pico transitorio por encima de la memoria retenida y asignaciones por frame. El tamaño de la rejilla, incluida su memoria de trabajo, se registra aparte. Geometría, texturas y demás recursos de entrada se construyen fuera de la medición. Las cifras corresponden a bytes solicitados por los contenedores.

## Auditoría del pipeline

| Fase | Comportamiento y costes observados en el código |
| --- | --- |
| Preparación | El consumidor calcula deltas, aristas, determinantes y orientación para reutilizarlos durante todo el frame. La rejilla estática se puede reutilizar al mover la cámara. El benchmark reserva capacidad y vuelve a preparar superficies por pose. El motor no exige preparar de nuevo la geometría estática de la rejilla. |
| Raycasting | La ruta corta recorre superficies preparadas contiguas. La rejilla recorre celdas con DDA, fusiona máscaras de candidatos, considera cada segmento una vez y reproduce los impactos en orden original para conservar empates. Borra dos juegos de máscaras por rayo y examina las superficies dinámicas por separado. El coste depende del ancho, la densidad y la distribución espacial. |
| Proyección | Hay un rayo por columna y una proyección de pared por impacto. Los planos vuelven a calcular la dirección de cámara al proyectar cada fila. Los sprites recalculan la base de cámara por instancia. El trabajo de proyección de pared es pequeño en estas escenas. |
| Paredes | Se sombrea por distancia. El bucle recorre el framebuffer verticalmente con stride de una fila. Para columnas altas, W01 alterna dirección para reutilizar líneas entre columnas. Existe una caché de sombreado para columnas de textura de 74 filas. La ruta de otras alturas sigue calculando la coordenada vertical y empaquetando el texel en cada píxel. |
| Planos | P01 clasifica la cobertura en bloques de 32 columnas y omite tramos ocultos. La ruta 128×128 usa acumuladores 32.32 con comprobación de bordes. La ruta escalar calcula dos coordenadas por píxel visible y aplica dos restos. La lectura de RGB depende del texel y el empaquetado RGBA se repite por píxel. Convertir a RGBA por frame añadiría un recorrido y un temporal. Persistirlo requeriría definir invalidación para texturas mutables. |
| Profundidad | Con capas de sprites se amplía la primera fila de distancias a una superficie pública de 8 bytes por píxel, incluso si las capas están vacías. Se llena con distancia de pared para toda la altura, incluidas las extensiones de sprites fuera de la pared visible. Sin capas se omite el trabajo. El buffer completo y sus valores forman parte del resultado que recibe el consumidor. |
| Sprites | Se respeta el orden entregado por el consumidor y el de las capas. El motor recorta a la pantalla, rechaza columnas detrás de las paredes y precalcula tramos horizontales de texel. Cada instancia normal reserva una lista de tramos. Los texeles transparentes se saltan por tramo. El solapamiento entre sprites sigue exigiendo pruebas por píxel de texeles opacos. La ruta de impactos verifica profundidad y pendiente de pared. |
| Fisheye | X01 mantiene el intercambio de framebuffer con una copia contigua a intensidad cero. Con intensidad positiva se calcula una tabla por cada cambio de tamaño. En caliente se hace una lectura secuencial de índices, una lectura remapeada del color y una escritura secuencial en el framebuffer alternativo. |
| Framebuffers y temporales | El color se redimensiona y limpia cada frame. Profundidad y cobertura reutilizan capacidad en `RenderBuffers`. Fisheye intercambia dos vectores de color. Los temporales recurrentes son los bloques de cobertura y los tramos de sprites. La tabla de fisheye y la profundidad son las reservas de memoria más grandes. |

## Ranking de oportunidades

| Prioridad | Cuello de botella | Frecuencia y escala | Ganancia posible, riesgo y complejidad |
| ---: | --- | --- | --- |
| 1 | Muestreo escalar de planos | Dos coordenadas por píxel visible. Domina `scalar` a todas las resoluciones. | Ganancia alta medida. La sustitución local del resto tiene riesgo numérico acotado por la prueba de equivalencia y los casos de borde. |
| 2 | Oclusión entre sprites | Por píxel opaco de cada instancia visible. Aumenta con resolución, tamaño y superposición. | Ganancia potencial alta. El coste de ocultación temprana debe medirse junto con orden de capas, empate, transparencias e impactos. Riesgo y complejidad medios a altos. |
| 3 | Localidad de paredes altas | Por píxel de pared. Domina el recinto denso. | Ganancia potencial alta. Mejorar localidad adicional sin diferir escrituras observables exige cuidado con los callbacks. Comparar primero texturas de diversas alturas y agrupaciones locales. |
| 4 | Fisheye positivo | Por píxel con el efecto activo. El mapa se reconstruye al cambiar resolución. | Una tabla estrecha ahorra memoria de forma directa. El beneficio de tiempo medido no justificó el cambio. El riesgo numérico de estrechar índices es bajo con un formato de respaldo. |
| 5 | Profundidad base y limpieza de color | Tráfico lineal. Profundidad solo con capas. | El coste caliente observado es pequeño frente a sprites y paredes. Omitir el buffer público cambiaría la semántica. Probar cambios locales de copia es sencillo. |
| 6 | Rayos y preparación | Escalan sobre todo con columnas y superficies. | Coste bajo en este conjunto. Interesa medir escenas con muchas superficies dinámicas, rejillas con muchos bloques e intersecciones cercanas en empate antes de cambiar el índice. |

## Cambio seleccionado

Para periodos finitos positivos, la ruta rápida devuelve la coordenada directamente cuando su magnitud ya cabe en una tesela. Si la magnitud requiere envolver y el cociente cabe en el rango acotado, obtiene un entero con `floor` y calcula el resto con una multiplicación y suma fusionadas. La división puede redondear hasta el entero de la siguiente tesela. Se detecta por el resto negativo y se vuelve a calcular con el entero anterior. Para magnitudes al menos iguales al periodo, ambos operandos son múltiplos exactos de la unidad binaria del periodo y el resto de magnitud menor al periodo es representable. Se restituye el signo antes de aplicar la envoltura, tolerancia de costura y selección de texel originales.

El uso de FMA depende de la macro estándar `FP_FAST_FMA` y de la representación IEEE 754 binaria de 53 bits. En otros entornos y cocientes fuera de rango se conserva `std::fmod`. No se añade estado, tabla, reserva ni conversión de recursos. La API pública sigue igual.

## Evidencia y reproducción

```sh
cmake -S . -B build/render-bench -DCMAKE_BUILD_TYPE=Release \
  -DVORTEX_ENABLE_SDL3=OFF -DVORTEX_BUILD_RENDER_BENCHMARK=ON
cmake --build build/render-bench
build/render-bench/tests/vortex/vortex_render_benchmark time 31 > total.csv
build/render-bench/tests/vortex/vortex_render_profile time 21 > fases.csv
```

Para reproducir la referencia, preparar un árbol temporal de `3cb4712` y aplicar solo los archivos del diagnóstico (`scene_renderer.cpp`, `tests/vortex/CMakeLists.txt`, `render_benchmark.cpp` y `render_profile.h`). Ejecutar `vortex_render_benchmark write referencia.bin` con ese binario y `vortex_render_benchmark verify referencia.bin` con el binario final. La referencia binaria ocupa aproximadamente 1,9 GiB y contiene 147 cuadros correspondientes a siete escenas, siete resoluciones y tres poses.

## Resultados

### Desglose de la referencia a 3840×2160

Medianas en milisegundos. El total y su P95 proceden del binario sin instrumentación. En preparación, 0,000 indica un valor inferior a 0,0005 ms.

| Escena | Prep. escena | Búferes | Rayos | Proyección | Material | Paredes | Planos | Profundidad | Sprites | Fisheye | Total | P95 total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| dense | 0,001 | 0,246 | 0,502 | 0,049 | 0,049 | 14,882 | 0,208 | 0,000 | 0,000 | 0,000 | 15,712 | 16,177 |
| open | 0,000 | 0,394 | 0,078 | 0,050 | 0,050 | 0,987 | 6,785 | 0,000 | 0,000 | 0,759 | 9,079 | 9,247 |
| short | 0,001 | 0,303 | 0,350 | 0,049 | 0,049 | 1,623 | 6,426 | 0,000 | 0,000 | 0,000 | 8,901 | 9,295 |
| scalar | 0,000 | 0,632 | 0,071 | 0,053 | 0,051 | 0,735 | 39,653 | 0,000 | 0,000 | 0,000 | 41,903 | 42,557 |
| sprites | 0,001 | 0,397 | 0,348 | 0,049 | 0,049 | 3,145 | 5,537 | 0,725 | 19,508 | 0,000 | 29,753 | 30,200 |
| mixed | 0,001 | 0,387 | 0,560 | 0,049 | 0,049 | 6,818 | 4,089 | 0,853 | 18,190 | 4,072 | 35,076 | 35,618 |
| warp | 0,000 | 0,333 | 0,079 | 0,051 | 0,050 | 0,978 | 6,787 | 0,000 | 0,000 | 3,168 | 11,422 | 11,632 |

### Antes y después por resolución

Escena `scalar`. Tiempos en milisegundos. La reducción se calcula sobre el total mediano.

| Resolución | Total antes | P95 antes | Total después | P95 después | Reducción | Planos antes → después |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 320×200 | 0,333 | 0,511 | 0,202 | 0,205 | 39,4 % | 0,442 → 0,279 |
| 640×400 | 1,296 | 1,333 | 0,769 | 0,808 | 40,7 % | 1,232 → 0,725 |
| 1280×720 | 4,678 | 4,740 | 2,877 | 2,981 | 38,5 % | 4,460 → 2,679 |
| 1920×1080 | 10,432 | 10,654 | 6,394 | 6,627 | 38,7 % | 10,041 → 6,040 |
| 2560×1440 | 18,655 | 19,066 | 11,504 | 11,904 | 38,3 % | 17,767 → 10,746 |
| 3840×2160 | 41,903 | 42,557 | 25,948 | 26,437 | 38,1 % | 39,653 → 24,470 |

La variabilidad de los procesos cortos se aprecia a 320×200. En esa resolución, la mayor subida de mediana de los controles fue de 0,003 ms (0,85 %, en `mixed`). En el conjunto de tamaños, la mayor subida de mediana de un control fue de 1,28 % (`sprites`, 2560×1440). La escena mixta de texturas 128×128 registra 35,076 → 35,352 ms a 4K. Las máquinas sin FMA rápida usan la ruta anterior.

### Memoria y asignaciones

Medianas a 3840×2160. Los valores calientes coinciden entre versiones. La asignación recurrente común es el resumen de cobertura. La escena `scalar` conserva exactamente 33.208.320 bytes entre frames, con 1.920 bytes de pico temporal y una asignación por frame caliente.

| Escena | Retenidos, bytes | Temporales, bytes | Rejilla, bytes | Asignaciones calientes | Asignaciones primer frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| dense | 33.208.320 | 1.920 | 7.136 | 1 | 3 |
| open | 66.385.920 | 1.920 | 144 | 1 | 4 |
| short | 33.208.320 | 1.920 | 5.264 | 1 | 3 |
| scalar | 33.208.320 | 1.920 | 144 | 1 | 3 |
| sprites | 99.594.240 | 1.920 | 5.680 | 49 | 54 |
| mixed | 199.127.040 | 1.920 | 6.640 | 49 | 56 |
| warp | 132.741.120 | 1.920 | 144 | 1 | 5 |

La escena mixta incluye, entre otros, 66.355.200 bytes de profundidad y 66.355.200 bytes de índices de fisheye. Los 144 bytes en escenas sin índice activo corresponden al objeto de rejilla vacío del benchmark.

### Compatibilidad con P01, X01 y W01

Se restauró por separado el archivo de implementación anterior a cada mejora, manteniendo activas las otras dos. Cada comparación se repitió en orden ABBA. Los tres binarios de control reprodujeron los 147 cuadros de la referencia.

| Mejora activa frente a desactivada | Escena 4K | Mediana de fase desactivada → activa | Total instrumentado desactivada → activa |
| --- | --- | ---: | ---: |
| P01 | dense | Planos 2,771 → 0,151 ms | 18,633 → 16,046 ms |
| X01 | open | Fisheye 1,618 → 0,732 ms | 10,175 → 9,376 ms |
| W01 | dense | Paredes 16,618 → 14,800 ms | 17,831 → 16,032 ms |

En `open`, X01 redujo además el primer fisheye de 16,733 a 2,416 ms y evitó 66.355.200 bytes de tabla. El binario final conserva las tres mejoras y produce los mismos cuadros que la referencia combinada.

### Alternativas experimentadas

Las tres variantes pasaron la comparación binaria. Las siguientes medianas 4K proceden de ejecuciones alternadas del perfil.

| Variante | Resultado frente a su referencia | Decisión |
| --- | --- | --- |
| Tabla de fisheye de 32 bits con respaldo ancho | En `warp`, ahorró 33.177.600 bytes. La fase pasó de 3,176 a 3,145 ms y el total de 11,744 a 11,751 ms. | Descartada para esta fase de rendimiento. |
| Expansión de profundidad por copias de bloques duplicados | En `sprites`, la fase subió de 0,721 a 1,298 ms y el total de 30,001 a 30,704 ms. | Descartada por regresión. |
| Un vector de tramos por capa de sprites | En `sprites`, las asignaciones calientes bajaron de 49 a 3. El total pasó de 30,012 a 30,099 ms. | Descartada por falta de reducción del total. |

### Verificación y límites

- Los 147 cuadros coincidieron byte a byte en color, profundidad por píxel, intervalos de cobertura y profundidad por columna, en compilaciones Release con SDL3 y sin SDL3. Las firmas de las secuencias de callbacks de materiales y observadores de capas coincidieron. Las poses y resoluciones incluyen clipping vertical, resoluciones impares, texturas de distintos tamaños, sombreado, materiales dinámicos, transparencias y oclusión total y parcial.
- `vortex_cpu_scene` comprueba cobertura, sombreado y raster de paredes, intercambio de búferes, distorsión y redondeo en bordes de texel. La comprobación nueva compara el muestreador contra `std::fmod` con periodos subnormales, fronteras de tesela, cocientes fuera de rango y 20.000 pares pseudoaleatorios de magnitudes.
- CTest Release sin SDL3, 2/2. CTest Release con SDL3, 3/3.
- Los CSV de `build/cpu-phase` incluyen las 42 combinaciones de escena y resolución, las distribuciones por fase, memoria, primer frame y datos brutos de las ejecuciones. Los perfiles son sintéticos y proceden de un único Apple M5. No se mide la presentación ni se extrapola la ganancia a un juego concreto.

## Siguientes candidatos

1. Medir qué fracción de las pruebas de profundidad de sprites se rechaza por paredes, por sprites anteriores y por transparencias. Priorizar el rechazo temprano con la secuencia de capas y las reglas de empate actuales.
2. Estudiar el coste de las escrituras con stride en paredes altas a distintas anchuras y alturas de textura. Mantener el orden de selección de materiales y el momento de publicación del color.
3. Evaluar el empaquetado genérico de texeles y la reducción de trabajo en filas del plano 128×128 con escenas de suelo abierto. Medir por separado coste de construcción, invalidación y memoria.
4. Reevaluar las tablas de fisheye si el siguiente objetivo incluye memoria retenida o latencia al cambiar resolución.

## S01 — Oclusión y rasterización de sprites

La referencia usa el rasterizador de sprites previo a S01 y el mismo árbol de trabajo para el resto del motor, incluida la FMA que estaba pendiente de commit. La comparación mantiene P01, X01 y W01 activos. El benchmark usa la API de Vortex y genera geometría, imágenes y máscaras sintéticas.

### Auditoría del pipeline

| Caso | Trabajo que se hace y dónde se detiene |
| --- | --- |
| A. Oculto por pared | Se transforma, proyecta y recorta. El límite de profundidad por columna rechaza todas las columnas antes del recorrido por filas. La ruta ordinaria aún reserva el vector temporal y recorre la anchura recortada. |
| B. Parcialmente oculto por pared | El mismo límite elimina las columnas ocultas y conserva los huecos entre tramos. En las columnas admitidas, la profundidad por píxel sigue siendo necesaria para detectar otros sprites. El límite de pared en Vortex cubre la columna entera para sprites, aunque el intervalo de color de la pared sea más corto. |
| C. Oculto por sprites anteriores | Vortex itera las capas y las instancias en orden de entrada, sin ordenación interna. Los texeles opacos pasan por identificación de tramo, máscara y empaquetado de color antes de comprobar profundidad. Un píxel rechazado consume esas etapas y evita toda escritura. Una escritura aceptada puede quedar cubierta por una instancia posterior y puede ser visible para un observador en el límite de capa. |
| D. Transparencia | La ruta ordinaria comprueba el índice una vez por tramo horizontal y fila. El índice cero evita leer el RGB y recorrer en profundidad todos los píxeles del tramo. Los impactos en pared comprueban la superficie antes de muestrear la máscara. |
| E. Fuera de pantalla | La cámara rechaza profundidades no finitas o inferiores a 0,01. Una proyección válida fuera del viewport se recorta a un rectángulo vacío, antes de reservar tramos. |
| F. Grande y cercano | El recorte limita el trabajo al viewport. El muestreo horizontal se reutiliza entre filas. Persisten el acceso repetido de texeles en filas que proyectan el mismo V y la prueba de profundidad por píxel opaco. El color se empaqueta en RGBA sin aplicar un sombreado adicional al sprite. |
| G. Pequeños y lejanos | Ganan peso las validaciones de imagen, los senos y cosenos de cámara calculados por instancia, el recorte y la reserva del vector. Si la proyección abarca pocos píxeles, el raster aporta poco tiempo. |

El vector de tramos es temporal por instancia. La profundidad por píxel, el límite de pared por columna y la cobertura de paredes pertenecen al renderer de escena. La cobertura de color de la pared sirve al dibujo de planos. Usarla como una máscara alternativa de sprites cambiaría la visibilidad. Los impactos pueden ajustar su prueba a la pendiente de una pared. El overlay de empate tiene una tolerancia de profundidad y conserva la profundidad anterior al escribir color. Ambas rutas requieren sus predicados actuales.

### Experimentos

| Variante | Decisión |
| --- | --- |
| Buscar la primera columna visible antes de reservar | Evita la reserva de una instancia completamente oculta. Las comparaciones adicionales y la diferencia pequeña en el total de las escenas complejas no justificaron el cambio. |
| Reutilizar un vector de tramos por capa y reservar tras la primera columna visible | Reduce las asignaciones, especialmente en `small`, pero el raster domina en las escenas mixtas y el beneficio en el total no se mantiene al subir resolución. |
| Encontrar intervalos continuos que superan la prueba y rellenarlos por separado | Las dos búsquedas escalares y los rangos cortos encarecieron el total en las escenas mixtas. |
| Rechazar bloques de cuatro u ocho antes del bucle escalar | Reduce ramas en el solapamiento. Duplica algunos accesos al volver al bucle escalar y penaliza al sprite cercano opaco. Probar el grupo tras un primer fallo también produjo regresiones en solapamientos a alta resolución. |
| Saltar únicamente el prefijo de bloques completamente rechazados | Acelera el solapamiento extremo. La ganancia cae para sprites opacos visibles y se observaron retrocesos en escenas pequeñas. |
| Máscara de dos decisiones uniformes para bloques de ocho | Presenta mejoras a alta resolución. La máscara de cuatro es más compacta y abarca más tramos cortos, con mejor resultado en el piloto a 640×400. |
| Cachear por franja la profundidad y la coordenada U de impactos | Conserva las fórmulas y los accesos contiguos dentro de cada franja. Los impactos tienen poco peso en las escenas mixtas del piloto y la ganancia total quedó cerca del ruido. |
| Reutilizar la máscara y el color en filas con el mismo V | El recorrido por tramo antes que por fila penalizó `mixed` cerca del 9 % a 4K y el sprite opaco entre 15 % y 16 %. Limitar la agrupación a ocho filas redujo la pérdida del opaco a 3–4 %, con ganancias menores y variables en `mixed`. |

S01 conserva una sola optimización. Para un tramo horizontal de un texel opaco, el camino ordinario de profundidad `NoNaN` calcula una máscara de escritura cada cuatro píxeles contiguos. Una máscara vacía pasa al bloque siguiente, una completa rellena ambos buffers y las mixtas aplican el resultado ya calculado a cada píxel. La cola usa el bucle escalar. Se mantiene el predicado original `!(z >= profundidad)`, el orden de instancias y capas y la regla de primer sprite en empate. El camino de impactos, el overlay de empate y el modo público que admite NaN siguen usando sus pruebas escalares. La máscara vive en una variable local y no requiere memoria dinámica ni almacenamiento por frame.

### Metodología y fidelidad

- Release en Apple M5 con AppleClang 21 y la opción de benchmarks activada. Cuatro repeticiones de 24 muestras por combinación y versión, en orden antes/después y después/antes alternado. Los percentiles excluyen seis cuadros de calentamiento, incluido el primero. P95 usa rango más próximo. El tiempo total abarca `first_person::render`. La preparación de rayos por el consumidor figura aparte. Los observadores antes y después de las capas miden el tiempo de sprites en el mismo ejecutable usado para el total.
- `vortex_render_profile time` mide proyección, clipping, generación de tramos y raster, y cuenta comparaciones, rechazos, píxeles transparentes y escrituras. La ordenación tiene coste cero porque Vortex recibe las instancias en el orden de dibujo. `vortex_render_profile probe` muestrea una de cada 257 operaciones de profundidad, lectura/empaquetado y escritura. Registra intervalos de reloj vacíos para estimar el coste de las operaciones por separado. Las estimaciones incluyen perturbación del muestreo y no se suman para obtener el tiempo real de raster.
- `vortex_render_benchmark write referencia.bin` guarda color, profundidad por píxel y columna, cobertura, argumentos de cada callback de pared y el framebuffer completo de cada callback de capa. `verify referencia.bin` compara los datos por bytes y comprueba el fin del flujo. Se probaron 294 cuadros (14 escenas, siete resoluciones y tres poses) y dos casos por la API directa de sprites. La resolución impar de escena es 641×481, y la prueba directa usa 319×177. Incluyen empates exactos y con tolerancia, flips, clipping, varias dimensiones de imagen, máscara de índice cero, profundidad con NaN, un fondo opaco y oclusión fragmentada.
- Una escena adicional con reloj determinista comparó 2.246 eventos intercalados de materiales, capas, reloj y etapas de `Timing`. Las trazas coincidieron con y sin SDL3.
- Coincidieron los 12.755.271.044 bytes en las dos compilaciones Release. Los archivos que implementan P01, X01, W01 y FMA quedaron intactos durante S01. La matriz sin sprites incluye escenas de pared alta, cobertura de planos, fisheye cero y plano escalar con textura irregular.
- CTest Release sin SDL3 pasó 2/2. CTest Release con SDL3 pasó 3/3.

### Resultados

En `mixed` a 1920×1080, la mediana registra 9,17 millones de pruebas por píxel y 7,18 millones de fallos de la comparación inicial de profundidad (78,3 %), con otros 7,23 millones de píxeles evitados por máscara. La proyección consume 0,9 µs, el clipping 0,7 µs y los tramos 44,9 µs en el perfil. La rasterización domina. En `hidden`, las 128 instancias llegan al límite de columna y se descartan antes de evaluar un solo píxel. En `offscreen` las 128 se descartan por rectángulo vacío.

Los contadores coinciden en las 84 combinaciones. S01 mantiene las comparaciones individuales y reduce las decisiones del bucle y las escrituras escalares en bloques uniformes. Las sondas por operación tienen poca resolución en operaciones baratas. Una estimación de cero indica que la diferencia frente al reloj vacío quedó por debajo del ruido. El tiempo independiente por operación se incluye como diagnóstico, y la selección se basa en el tiempo por capa y el tiempo total medidos sin esas sondas.

**Reducción del P50 total, en porcentaje.** Positivo indica una reducción de tiempo.

| Resolución | `sprites` | `mixed` | `overlap` | `partial` | `large-opaque` | `large-transparent` | `small` | `hidden` | `offscreen` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 320×200 | +5,1 | +4,6 | +3,1 | -0,1 | +5,9 | +2,4 | +0,3 | +0,4 | +0,9 |
| 640×400 | +14,6 | +12,0 | +18,2 | +4,3 | +6,9 | +0,6 | +1,8 | +0,4 | 0,0 |
| 1280×720 | +20,2 | +16,8 | +30,3 | +8,4 | +6,6 | +0,7 | +0,9 | -0,3 | +0,5 |
| 1920×1080 | +22,8 | +18,2 | +33,4 | +10,6 | +7,5 | +0,2 | +0,3 | +0,1 | 0,0 |
| 2560×1440 | +19,7 | +11,7 | +31,9 | +8,0 | +6,8 | +0,1 | -0,3 | -0,4 | -0,3 |
| 3840×2160 | +16,6 | +10,7 | +27,5 | +8,4 | +9,8 | -0,1 | +0,2 | +0,1 | 0,0 |

| Resolución | `dense` | `open` | `short` | `scalar` | `warp` |
| --- | ---: | ---: | ---: | ---: | ---: |
| 320×200 | 0,0 | -0,8 | +1,2 | +2,4 | -0,9 |
| 640×400 | +0,6 | -0,3 | +1,1 | -0,5 | -0,4 |
| 1280×720 | -1,0 | -0,4 | +0,5 | -0,1 | 0,0 |
| 1920×1080 | +0,4 | +0,1 | +0,4 | -0,1 | +0,5 |
| 2560×1440 | -0,2 | +0,2 | 0,0 | 0,0 | +0,2 |
| 3840×2160 | -0,1 | -0,1 | -0,2 | -0,7 | -0,1 |

**Comparaciones representativas, en milisegundos.**

| Escena | Resolución | Sprites P50 | Sprites P95 | Total P50 | Total P95 |
| --- | --- | ---: | ---: | ---: | ---: |
| `partial` | 320×200 | 0,066 → 0,067 | 0,077 → 0,088 | 0,180 → 0,180 | 0,196 → 0,202 |
| `mixed` | 320×200 | 0,188 → 0,173 | 0,230 → 0,219 | 0,335 → 0,319 | 0,390 → 0,377 |
| `sprites` | 1920×1080 | 4,144 → 2,618 | 4,248 → 2,750 | 6,697 → 5,172 | 6,824 → 5,350 |
| `mixed` | 1920×1080 | 3,750 → 2,496 | 4,157 → 2,883 | 7,240 → 5,922 | 7,835 → 6,481 |
| `overlap` | 1920×1080 | 12,114 → 7,233 | 12,268 → 7,334 | 14,676 → 9,780 | 14,834 → 9,886 |
| `large-opaque` | 1920×1080 | 0,671 → 0,453 | 0,713 → 0,470 | 3,257 → 3,014 | 3,341 → 3,058 |
| `sprites` | 3840×2160 | 19,598 → 14,608 | 20,047 → 16,102 | 29,873 → 24,909 | 30,462 → 26,343 |
| `mixed` | 3840×2160 | 18,269 → 14,377 | 18,868 → 14,824 | 35,149 → 31,380 | 35,874 → 31,950 |
| `overlap` | 3840×2160 | 49,391 → 33,032 | 50,188 → 34,132 | 59,691 → 43,287 | 60,451 → 44,478 |
| `large-opaque` | 3840×2160 | 3,144 → 1,779 | 3,184 → 1,870 | 13,340 → 12,028 | 13,443 → 12,158 |

La mayor variación adversa de mediana es +1,01 % en el control sin sprites `dense` a 1280×720. Entre escenarios con sprites, `partial` a 320×200 registra +0,000126 ms en P50 y +0,006041 ms (+3,1 %) en P95. El control `open` sin sprites registra +0,004083 ms (+5,2 %) en P95 a 320×200. Las ganancias de `mixed` y `sprites` de 640×400 en adelante se repiten en las cuatro rondas. Las mediciones proceden de una sola máquina.

**Memoria a 3840×2160.** Medianas calientes y asignaciones en el primer frame. Las 84 combinaciones coinciden antes y después, incluidos P95 de memoria temporal y asignaciones. Los bytes corresponden a las solicitudes interceptadas por el benchmark.

| Escena | Retenidos | Pico temporal | Asignaciones calientes | Asignaciones primer frame |
| --- | ---: | ---: | ---: | ---: |
| `sprites` | 99.594.240 | 1.920 | 49 | 54 |
| `mixed` | 199.127.040 | 1.920 | 49 | 56 |
| `overlap` | 99.594.240 | 1.920 | 97 | 101 |
| `partial` | 99.594.240 | 1.920 | 25 | 29 |
| `large-opaque` | 99.594.240 | 1.920 | 2 | 6 |
| `large-transparent` | 99.594.240 | 1.920 | 2 | 6 |
| `small` | 99.594.240 | 1.920 | 742 | 753 |
| `hidden` | 99.594.240 | 1.920 | 129 | 133 |
| `offscreen` | 99.594.240 | 1.920 | 1 | 5 |

Los resultados de las 84 combinaciones están en los CSV de [P50/P95 por escena](../build/s01-results/timing-summary.csv), [fases y contadores](../build/s01-results/phase-summary.csv), [sondas por operación](../build/s01-results/probe-summary.csv) y [memoria y asignaciones](../build/s01-results/memory-summary.csv). Los CSV brutos y el agregador quedaron en `build/s01-results`.
