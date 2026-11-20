# W02 Paredes y profundidad

**Decisión final: descartar W02.** Se restauró el raster de paredes de `c5e938e` tras repetir las mediciones de 2560×1440. La revisión final de este informe recoge el diagnóstico y las correcciones probadas. El estudio inicial se conserva como referencia histórica.

## Método del estudio inicial

Referencia `c5e938e` con los mismos escenarios y puntos de instrumentación que la variante final. Se usaron escenas sintéticas de Vortex, Release C++17, Apple M5 de 16 GB, macOS 26.6.2 y AppleClang 21.0. El tiempo total se tomó con el benchmark ordinario, que mide la llamada a `first_person::render` y registra por separado la preparación de geometría. Se alternó el orden por pareja ABBA ABBA con 31 muestras calientes por proceso y celda, para 124 por versión. Se ejecutaron ambos binarios antes de iniciar la serie publicada. La primera serie después de compilar mostró picos concentrados en el arranque de un binario y quedó registrada en los CSV originales. El perfil se tomó en orden ABBA con 21 muestras calientes por proceso y celda. Cada proceso calentó seis frames por celda y conservó el primero para medir asignaciones y arranque. P95 usa el rango `ceil(0,95 × n)`.

| Caso | Escena | Texturas de pared, filas |
| --- | --- | --- |
| A | Solo paredes | 74, 57 |
| B | Pocos sprites visibles | 128, 57 |
| C | 96 sprites superpuestos | 74, 57 |
| D | Pared próxima y recortada, sin sprites | 128, 57 |
| E | Paredes lejanas | 57 |
| F | Escena mixta con planos, sprites y fisheye positivo | 73, 57 |
| G | 128 sprites detrás de paredes | 74, 57 |
| H | Ocho capas ordenadas, con sprites cada vez más cercanos, transparencias y empates | 128, 57 |

El perfil incluye contadores de pruebas Z por píxel, descartes S01 y escrituras. Se muestreó una operación de cada 257 para aproximar el coste del acceso y comparación de Z por columna y por píxel y de las escrituras de sprite. Los resultados por operación incluyen el ruido de calibración y los accesos de color en la sonda de escritura. Sirven para interpretar los contadores y el tiempo del raster de sprites.

## Auditoría

- Con capas, `resize` inicializa los elementos nuevos de un vector público de `double`. En caliente, con dimensión estable, se vuelve a iniciar solo la primera fila con infinito para representar rayos sin impacto. Cada impacto escribe en esa fila su distancia. Con cero capas se omite la preparación de profundidad.
- La expansión copia la fila base a las demás usando escrituras contiguas. A 4K son 66.324.480 bytes de destino por frame, con una fuente de 30.720 bytes que se reutiliza. El resultado incluye las zonas de pantalla por encima y debajo de la pared rasterizada. La distancia ahí evita que se vean sprites detrás de la pared.
- S01 consulta `wallDepthColumns` para descartar columnas antes del raster de sprites. Ese vector sigue representando las paredes después de que los sprites modifiquen el búfer por píxel. La fila cero puede cambiar con los sprites y el consumidor puede leer ambos vectores. Una representación diferida o por columnas exigiría materializar el estado público y considerar las lecturas entre capas. Quitar el relleno inicial cambiaría además el estado disponible cuando se llama al callback de material.
- La rasterización de paredes escribe color con stride de una fila. W01 alterna la dirección de columnas altas para reutilizar líneas recientes. Las paredes magnificadas también repiten divisiones, lecturas y sombreado de una misma fila de textura. La rama existente de 74 filas precalcula el sombreado.
- Las medidas de memoria corresponden a bytes solicitados al asignador C++. Excluyen texturas y geometría preparadas y registran aparte la rejilla. Reflejan capacidad retenida y pico transitorio medido por encima de los bytes vivos al terminar el frame.

## Evidencia y reproducción

```sh
cmake -S . -B build/w02 -DCMAKE_BUILD_TYPE=Release \
  -DVORTEX_ENABLE_SDL3=OFF -DVORTEX_BUILD_RENDER_BENCHMARK=ON
cmake --build build/w02
build/w02/tests/vortex/vortex_render_benchmark time 31 w02- > total.csv
build/w02/tests/vortex/vortex_render_profile time 21 w02- > fases.csv
build/w02/tests/vortex/vortex_render_profile probe 7 w02- > accesos.csv
ctest --test-dir build/w02 --output-on-failure
```

Para construir la referencia, usar el raster de paredes de `c5e938e` con el resto de archivos de este cambio. Ejecutar `vortex_render_benchmark write referencia.bin` con la referencia y `vortex_render_benchmark verify referencia.bin` con la variante final. El archivo completo de referencia ocupa unos 21 GiB. La verificación cubre 22 escenas, siete resoluciones, tres poses por escena y casos directos de sprites. El filtro opcional `w02-` limita ambos comandos a las ocho escenas W02 y conserva los casos directos.

## Experimentos

El porcentaje es la media geométrica del cambio de P50 total sobre las 48 combinaciones de escena y resolución. Los barridos de selección usaron 84 muestras calientes por celda para copia por bloques, caché y reutilización de color. El primer barrido de tramos usó 42 muestras por celda. El barrido del umbral más conservador usó 124.

| Variante | Δ P50 total | Decisión |
| --- | ---: | --- |
| Expandir Z por duplicación de bloques ya escritos | +1,55 % | Descartada. Leyó regiones crecientes para escribir el mismo volumen que la referencia. |
| Cachear el rango visible de filas sombreadas para texturas de 74 y 128 filas | −3,82 % | Descartada. Ganó en paredes altas de 128 filas, con subidas en 2560×1440 y sin cubrir tamaños arbitrarios. |
| Reutilizar el color anterior cuando coincide la fila de texel | −4,73 % | Seleccionada para la confirmación. Funciona con alturas arbitrarias y mantiene la fórmula de muestreo por fila. |
| Reutilizar tramos de ocho filas cuando coinciden ambos extremos, umbral de cuatro muestras por texel visible | +1,95 % | Descartada. La comprobación adicional costó tiempo cuando hubo muchos cambios de fila de texel. |
| Tramos de ocho con umbral de doce muestras proyectadas por texel | −0,18 % | Descartada. La mejora en algunas paredes altas apenas se reflejó en el conjunto. |

En el estudio inicial se eligió una sola optimización para la confirmación. La revisión posterior la descartó. Cuando una pared texturizada cubre más filas de pantalla que filas de textura, el raster general conserva el índice y el color sombreado del último texel. Si el índice vuelve a coincidir, escribe el color ya calculado. El cálculo del índice, el orden de las escrituras, el recorte y la dirección de recorrido se conservan. La rama de 74 filas sigue usando su caché. La variante añade estado escalar local y no requiere reservas de memoria. La precisión, el orden y los empates de profundidad se conservan.

## Resultados

### Tiempo total CPU

Tiempos en milisegundos, medidos sin instrumentación.

La media geométrica del P50 total bajó un 5,0 %. En 1920×1080, el caso de paredes altas mejoró un 28,3 %. En 2560×1440, esa escena subió un 12,1 % y la de pocos sprites un 7,8 %. En 320×200 la mayor subida de P50 fue de 0,0025 ms y la mayor subida de P95 fue de 0,0098 ms. Los CSV de ambos barridos se conservan en `build/w02-results`.

| Escena | Resolución | P50 base → final | P95 base → final | Δ P50 |
| --- | --- | ---: | ---: | ---: |
| walls | 320×200 | 0.085 → 0.085 | 0.092 → 0.087 | +0.0 % |
| few | 320×200 | 0.113 → 0.116 | 0.123 → 0.120 | +2.2 % |
| overlap | 320×200 | 0.727 → 0.709 | 0.765 → 0.762 | -2.5 % |
| tall | 320×200 | 0.130 → 0.086 | 0.138 → 0.091 | -34.1 % |
| far | 320×200 | 0.021 → 0.021 | 0.022 → 0.021 | +0.0 % |
| mixed | 320×200 | 0.345 → 0.326 | 0.373 → 0.383 | -5.5 % |
| hidden | 320×200 | 0.101 → 0.100 | 0.103 → 0.103 | -0.4 % |
| layers | 320×200 | 0.314 → 0.311 | 0.339 → 0.338 | -0.9 % |
| walls | 640×400 | 0.316 → 0.319 | 0.323 → 0.328 | +0.8 % |
| few | 640×400 | 0.356 → 0.309 | 0.369 → 0.328 | -13.1 % |
| overlap | 640×400 | 1.664 → 1.665 | 1.693 → 1.696 | +0.0 % |
| tall | 640×400 | 0.443 → 0.330 | 0.458 → 0.374 | -25.4 % |
| far | 640×400 | 0.065 → 0.066 | 0.066 → 0.075 | +0.4 % |
| mixed | 640×400 | 0.937 → 0.896 | 0.978 → 0.950 | -4.3 % |
| hidden | 640×400 | 0.391 → 0.392 | 0.404 → 0.401 | +0.2 % |
| layers | 640×400 | 1.009 → 1.018 | 1.064 → 1.061 | +0.9 % |
| walls | 1280×720 | 1.231 → 1.224 | 1.294 → 1.357 | -0.5 % |
| few | 1280×720 | 1.251 → 1.034 | 1.292 → 1.077 | -17.3 % |
| overlap | 1280×720 | 4.539 → 4.541 | 4.583 → 4.584 | +0.1 % |
| tall | 1280×720 | 1.491 → 1.171 | 1.513 → 1.190 | -21.5 % |
| far | 1280×720 | 0.258 → 0.231 | 0.274 → 0.243 | -10.8 % |
| mixed | 1280×720 | 2.770 → 2.659 | 2.811 → 2.714 | -4.0 % |
| hidden | 1280×720 | 1.450 → 1.435 | 1.520 → 1.481 | -1.0 % |
| layers | 1280×720 | 3.482 → 3.383 | 3.681 → 3.567 | -2.9 % |
| walls | 1920×1080 | 2.171 → 2.165 | 2.363 → 2.199 | -0.3 % |
| few | 1920×1080 | 2.691 → 2.029 | 2.739 → 2.107 | -24.6 % |
| overlap | 1920×1080 | 8.820 → 8.830 | 8.934 → 9.082 | +0.1 % |
| tall | 1920×1080 | 3.237 → 2.319 | 3.272 → 2.353 | -28.3 % |
| far | 1920×1080 | 0.508 → 0.422 | 0.522 → 0.435 | -16.9 % |
| mixed | 1920×1080 | 6.289 → 5.961 | 6.648 → 6.321 | -5.2 % |
| hidden | 1920×1080 | 2.533 → 2.530 | 2.569 → 2.569 | -0.1 % |
| layers | 1920×1080 | 7.903 → 7.620 | 8.380 → 7.987 | -3.6 % |
| walls | 2560×1440 | 9.585 → 9.595 | 9.772 → 9.699 | +0.1 % |
| few | 2560×1440 | 6.763 → 7.292 | 7.090 → 7.370 | +7.8 % |
| overlap | 2560×1440 | 16.763 → 16.630 | 17.032 → 16.866 | -0.8 % |
| tall | 2560×1440 | 8.946 → 10.029 | 8.984 → 10.088 | +12.1 % |
| far | 2560×1440 | 1.332 → 1.409 | 1.369 → 1.450 | +5.8 % |
| mixed | 2560×1440 | 14.276 → 14.611 | 14.774 → 15.396 | +2.3 % |
| hidden | 2560×1440 | 10.595 → 10.596 | 10.658 → 10.656 | +0.0 % |
| layers | 2560×1440 | 15.348 → 15.465 | 16.162 → 16.068 | +0.8 % |
| walls | 3840×2160 | 14.766 → 14.814 | 15.036 → 15.114 | +0.3 % |
| few | 3840×2160 | 12.443 → 11.959 | 12.618 → 12.230 | -3.9 % |
| overlap | 3840×2160 | 40.723 → 40.110 | 42.833 → 41.349 | -1.5 % |
| tall | 3840×2160 | 15.584 → 15.399 | 15.744 → 15.549 | -1.2 % |
| far | 3840×2160 | 2.212 → 1.864 | 2.277 → 1.915 | -15.7 % |
| mixed | 3840×2160 | 31.420 → 31.063 | 31.977 → 31.658 | -1.1 % |
| hidden | 3840×2160 | 16.663 → 16.651 | 17.220 → 17.236 | -0.1 % |
| layers | 3840×2160 | 34.868 → 34.118 | 36.557 → 34.954 | -2.2 % |

### Fases y profundidad a 3840×2160

Medianas en milisegundos, obtenidas con el binario de perfil. Cada celda muestra referencia → variante. Los tiempos de raster de sprites incluyen pruebas de profundidad y transparencias. El perfil por fase mantiene su propia sobrecarga y se interpreta por separado del total de arriba.

| Escena | Paredes | Inicio Z | Expansión Z | Copia columna Z | Raster sprites | Sprites total | Planos | Fisheye |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| walls | 14.090 → 14.106 | 0.000 → 0.000 | 0.000 → 0.000 | 0.000 → 0.000 | 0.000 → 0.000 | 0.000 → 0.000 | 0.000 → 0.000 | 0.000 → 0.000 |
| few | 9.958 → 9.395 | 0.000 → 0.000 | 0.760 → 0.745 | 0.000 → 0.000 | 1.086 → 1.065 | 1.090 → 1.068 | 0.000 → 0.000 | 0.000 → 0.000 |
| overlap | 4.056 → 4.096 | 0.001 → 0.001 | 0.663 → 0.663 | 0.000 → 0.000 | 44.783 → 44.549 | 45.043 → 44.808 | 0.000 → 0.000 | 0.000 → 0.000 |
| tall | 14.894 → 14.683 | 0.000 → 0.000 | 0.000 → 0.000 | 0.000 → 0.000 | 0.000 → 0.000 | 0.000 → 0.000 | 0.000 → 0.000 | 0.000 → 0.000 |
| far | 1.802 → 1.363 | 0.000 → 0.000 | 0.000 → 0.000 | 0.000 → 0.000 | 0.000 → 0.000 | 0.000 → 0.000 | 0.000 → 0.000 | 0.000 → 0.000 |
| mixed | 6.802 → 6.480 | 0.001 → 0.001 | 0.796 → 0.796 | 0.001 → 0.001 | 16.691 → 16.707 | 16.789 → 16.802 | 4.087 → 4.091 | 4.148 → 4.153 |
| hidden | 15.001 → 15.113 | 0.001 → 0.001 | 0.796 → 0.796 | 0.000 → 0.000 | 0.000 → 0.000 | 0.083 → 0.083 | 0.000 → 0.000 | 0.000 → 0.000 |
| layers | 4.067 → 3.324 | 0.001 → 0.001 | 0.745 → 0.744 | 0.001 → 0.001 | 28.199 → 28.220 | 28.267 → 28.290 | 5.216 → 5.175 | 0.824 → 0.826 |

### Memoria y trabajo de sprites a 3840×2160

Los P50 y P95 de memoria y asignaciones coinciden entre versiones en las 48 celdas, en frío y en caliente. La tabla muestra las medianas y el recuento de pruebas de profundidad por píxel.

| Escena | Bytes retenidos | Pico temporal, bytes | Asignaciones frío / caliente | Init Z fría, ms | Pruebas Z por píxel | Columnas descartadas |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| walls | 33177600 | 0 | 1 / 0 | 0.000 | 0 | 0 |
| few | 99563520 | 348 | 5 / 2 | 2.946 | 1452968 | 0 |
| overlap | 99563520 | 732 | 99 / 96 | 0.370 | 142527255 | 0 |
| tall | 33177600 | 0 | 1 / 0 | 0.000 | 0 | 0 |
| far | 33177600 | 0 | 1 / 0 | 0.000 | 0 | 0 |
| mixed | 199127040 | 1920 | 56 / 49 | 0.463 | 36695720 | 20551 |
| hidden | 99563520 | 732 | 131 / 128 | 0.449 | 0 | 148312 |
| layers | 132771840 | 1920 | 22 / 17 | 0.458 | 53379357 | 0 |

En la referencia 4K, la expansión caliente de Z cuesta entre 0,663 y 0,796 ms en las cinco escenas con capas. Eso equivale a 83–100 GB/s de bytes de destino en la copia, una medida de trabajo lógico del búfer. El inicio caliente y la copia por columnas están en el orden de un microsegundo. El inicio frío incluye la creación de la superficie y la primera escritura de sus páginas. El coste frío varía con la disponibilidad de páginas reutilizables. Los P50 y P95 de fases, memoria y asignaciones para las 48 celdas se encuentran en `build/w02-results/fases-memoria.csv`.

La oclusión de paredes del caso G descarta unas 148.000 columnas de sprite por frame 4K. El raster de sprites ejecuta cero pruebas por píxel en G. En C, el caso superpuesto, ejecuta unos 143 millones de pruebas por píxel. La sonda de comparación por píxel estima alrededor de 16 ms en C, con una resolución limitada por la calibración. En H, la sonda estima unos 8 ms de comparaciones y 17 ms de escrituras donde también se escribe color. Una llamada de sonda puede abarcar un bloque S01 de cuatro píxeles. Las estimaciones fluctúan y no se suman al total. Las lecturas próximas al coste del reloj producen estimaciones de signo variable, especialmente para las comparaciones por columna. Se guardaron llamadas, muestras, P50, P95 y extrapolación por frame en `build/w02-results/accesos-sprites.csv`.

### Fidelidad

Release pasó 2/2 pruebas CTest sin SDL3 y 3/3 con SDL3. Cada configuración comparó byte a byte 22.128.096.596 bytes con la referencia. Incluyen el color final, el búfer completo de profundidad, las distancias de pared por columna, la cobertura, los argumentos observados por el callback de material y el framebuffer antes y después de cada capa activa. Los casos cubren paredes recortadas, texturas regulares e irregulares, resoluciones impares, escenas sin sprites y múltiples capas con solapamiento, transparencia y empates. La API directa de sprites se verifica también con y sin NaN de entrada.

Los casos de paredes altas comprueban cada fila contra el muestreo escalar y comparan ambas direcciones de W01. CTest cubre P01 y FMA mediante las pruebas de cobertura y bordes de muestreo de planos. Las pruebas de fisheye comprueban X01 a intensidad cero y las transiciones de intensidad. La comparación de los cuadros con capas y las pruebas directas de sprites cubren S01. `git diff --check` quedó limpio.

### Siguiente cuello de botella

Priorizar las pruebas y escrituras de sprites con mucho solapamiento. C ofrece un caso de rechazo repetido después de dibujar los primeros sprites, y H uno de actualizaciones repetidas con orden de capa y empates. En el perfil 4K, el raster de sprites ocupa unos 45 ms en C y 28 ms en H. En la escena de pared próxima sin sprites, el raster de paredes sigue ocupando unos 15 ms.


## Revisión de la regresión a 2560×1440

Se enlazó el mismo benchmark de escena con cada raster compilado en Release C++17 con AppleClang 21.0 y `-O3`. Las 48 combinaciones se volvieron a medir con cuatro rondas en orden alternado y 76 muestras calientes por celda y variante. Se hizo un lanzamiento de calentamiento por binario. Los tiempos siguientes corresponden a la llamada completa al renderizador CPU y se expresan en milisegundos.

| Resolución | Escena | P50 referencia → W02 | P95 referencia → W02 |
| --- | --- | ---: | ---: |
| 1920×1080 | tall | 3.232 → 2.313 | 3.258 → 2.345 |
| 1920×1080 | few | 2.695 → 2.019 | 2.754 → 2.096 |
| 2560×1440 | tall | 8.969 → 9.999 | 9.130 → 10.095 |
| 2560×1440 | few | 6.753 → 7.291 | 6.976 → 7.420 |
| 3840×2160 | tall | 15.648 → 15.307 | 15.925 → 15.440 |
| 3840×2160 | few | 12.513 → 11.938 | 12.758 → 12.468 |

El P50 total de W02 bajó un 5,27 % en media geométrica. A 2560×1440, W02 perdió en las cuatro parejas de ejecuciones. Las pérdidas por pareja fueron del 9,67 al 11,78 % en paredes altas y del 6,72 al 8,13 % con pocos sprites.

### Reutilización y paso vertical del framebuffer

En paredes altas de 128 filas, el porcentaje de muestras que repetían texel fue 97,43 % a 1080p, 98,08 % a 1440p y 98,72 % a 4K. Las relaciones medias de altura proyectada sin recortar a altura de textura fueron 40,7×, 54,2× y 81,3×. Con pocos sprites a 1440p, la pared de 128 filas tuvo un 87,73 % de repeticiones y una relación de 8,15×. En esa resolución, la pared lejana de 57 filas tuvo un 66,87 % de repeticiones y la pared principal de 73 filas en la escena mixta un 95,94 %. Las paredes de 74 filas siguieron usando su caché previa.

El raster aislado fijó la geometría, la textura de 128 filas, la altura de framebuffer de 1440 y la dirección alterna de W01. Al cambiar de 2559 a 2560 o 2561 píxeles de ancho, cambió el paso vertical de escritura de 10.236 a 10.240 o 10.244 bytes. El porcentaje de reutilización fue idéntico para cada grupo.

| Altura proyectada / altura de textura | Reutilización | Ancho | P50 de raster referencia → W02, ms |
| ---: | ---: | ---: | ---: |
| 2,5× | 60,00 % | 2559 | 1,135 → 0,774 |
| 2,5× | 60,00 % | 2560 | 1,663 → 1,787 |
| 2,5× | 60,00 % | 2561 | 1,140 → 0,776 |
| 38× | 97,29 % | 2559 | 5,137 → 2,442 |
| 38× | 97,29 % | 2560 | 10,174 → 10,162 |
| 38× | 97,29 % | 2561 | 5,138 → 2,429 |

Con una relación de 2,5×, el mismo ancho de 2560 también penalizó la reutilización con texturas de 57 (+6,5 %) y 193 filas (+8,7 %). El control de 74 filas quedó cerca de cero (+0,2 %). El barrido identifica una sensibilidad al paso de escritura. Las pruebas de tiempo no permiten atribuirla a un nivel concreto de caché o al TLB. En el raster aislado más magnificado, el paso de 10.240 bytes anula casi toda la ganancia. La escena completa añade variación de proyección entre columnas y muestra una pérdida reproducible.

### Dirección y código generado

En la prueba de paredes altas a 1440p, el raster anterior pasó de 8,987 a 11,240 ms al sustituir el recorrido alterno por avance en todas las columnas. Para W02, el mismo cambio elevó el total de 10,026 a 11,507 ms. W01 beneficia a ambas versiones y beneficia más a la referencia en esa resolución. Con ambos rasterizadores en avance, W02 siguió por detrás un 2,38 %. La dirección y el paso afectan al coste, y la dirección por sí sola no explica el resultado.

El ensamblador del raster general de referencia es escalar en ambos sentidos. W02 conserva la división, la conversión a entero, el recorte y la escritura por fila, y añade una comparación y un salto que controlan una dependencia con la fila y el color anteriores. Los diagnósticos de AppleClang detectan esa dependencia, pero el bucle general de referencia ya carecía de vectorización. La caché de 74 filas siguió vectorizando su precálculo. Estas observaciones identifican el coste adicional por muestra y descartan una pérdida de vectorización SIMD del bucle general como explicación.

### Correcciones locales

Los porcentajes muestran el cambio del P50 total frente a la referencia de la misma serie.

| Variante | Media geométrica de 48 celdas | Paredes altas a 1440p | Pocos sprites a 1440p |
| --- | ---: | ---: | ---: |
| W02 original | −5,27 % | +11,48 % | +7,97 % |
| Reutilizar solo en columnas que avanzan | −4,06 % | +5,37 % | +4,13 % |
| Exigir cuatro filas proyectadas por texel | −3,84 % | +11,50 % | +8,71 % |
| Leer dimensiones y puntero de textura una vez por columna | −4,50 % | +11,86 % | +9,13 % |

Las tres correcciones conservaron parte de la ganancia global y mantuvieron la penalización en 1440p. Se restauró únicamente `wall_raster.cpp` a `c5e938e`. P01, X01, W01, FMA y S01 siguen vigentes. Los contadores temporales de esta revisión viven en `build/w02-review`. Las sondas del estudio inicial siguen compilándose solo en el binario de perfil. El CSV `build/w02-review/totales.csv` recoge P50 y P95 de las 48 combinaciones y de las pruebas de dirección. En el mismo directorio quedaron los datos por ronda, los contadores de reutilización, el barrido de stride y el ensamblador.

### Validación de la restauración

El archivo de raster coincide con el de `c5e938e`. CTest en Release pasó 2/2 sin SDL3 y 3/3 con SDL3. El benchmark de W02 y los dos builds restaurados compararon cada uno 22.128.096.596 bytes con la referencia de color, profundidad, cobertura y datos observados por callbacks. `git diff --check` quedó limpio.
