# S02 · Oclusión de sprites superpuestos

## Qué se midió

El benchmark usa texturas, paredes y sprites sintéticos de Vortex. El caso `opaque-overlap` dibuja 96 billboards opacos de cerca a lejos. Los casos `overlap` y `w02-overlap` intercalan opacos y máscaras. `partial` cruza un borde de pared, `large-opaque` presenta un sprite próximo visible, `large-transparent` somete el raster a máscara vacía y `small` proyecta 768 sprites pequeños. Las escenas mixtas y de capas añaden paredes, planos, efectos y empates.

El binario ordinario mide la llamada completa a `first_person::render` con reloj monotónico de pared y el coste de preparación de geometría por separado. El CSV de percentiles incluye también la suma por muestra. El observador de capas en modo de tiempos solo toma dos lecturas del reloj por capa. Un binario de control desactiva el observador y enlaza la misma biblioteca CPU. El binario de perfil añade contadores y relojes al código del renderizador. Sus tiempos sirven para localizar el trabajo. Los tiempos de mejora proceden del binario ordinario, sin sondas ni contadores.

Las comparaciones usan Release C++17, AppleClang 21.0, macOS arm64, 23 escenas y ocho resoluciones, incluidas 641×481 y 1279×721. Cada proceso calienta seis frames por celda, descarta las muestras frías y rota tres poses. Las ejecuciones alternan referencia (R), descarte completo (A) y recorte (B) en orden RABBAR BRAARB, con 13 muestras calientes por celda en cada proceso, 52 por variante. P50 es la mediana y P95 es el percentil por rango más próximo.

## Coste por operación

- La inicialización de escena genera la profundidad de las paredes en la primera fila y la replica. Hay 66.324.480 bytes de profundidad por frame a 4K. La cota de pared por columna permite descartar sprites tras la pared, incluidas sus zonas sin color rasterizado. La cota sigue siendo válida cuando otros sprites reducen la profundidad por píxel.
- La ruta normal calcula un índice horizontal por columna y lo agrupa en tramos por texel. Calcula el índice vertical una vez por fila. La proyección, el recorte, la creación de tramos y el raster se miden en fases separadas. La fase de raster agrupa el recorrido de filas, tramos y píxeles.
- Cada tramo consulta la máscara una vez. El índice cero termina el tramo. Un tramo opaco lee RGB y empaqueta el color una vez. Los sprites de esta ruta carecen de sombreado dinámico.
- Cada píxel opaco de un tramo superviviente consulta profundidad. S01 lo hace en grupos de cuatro cuando procede. Un rechazo consume lectura de profundidad y control de flujo. Un éxito escribe 4 bytes de color y 8 de profundidad, salvo las reglas específicas de empate o profundidad desordenada. Las sondas de comparación, máscara/empaquetado y escritura miden operaciones agrupadas con una muestra cada 257 llamadas. El coste al restar dos intervalos tan cortos tiene incertidumbre y las estimaciones no son aditivas.
- El color y la profundidad se recorren por filas. Los sprites grandes visitan repetidamente las mismas líneas de profundidad. Se cuantificó el tráfico lógico por comparaciones y escrituras. Los temporizadores y contadores utilizados no identifican a qué nivel de caché llega cada acceso.
- Cada sprite rasterizable reserva un vector de tramos. La preparación de tramos y las asignaciones se registraron por separado del bucle que recorre los píxeles.

### Perfil a 4K

En `w02-overlap`, el tiempo mediano de raster medido dentro del binario instrumentado pasa de 45,495 a 24,605 ms. La proyección y el recorte suman alrededor de 0,004 ms en la referencia y la construcción de tramos pasa de 0,240 a 0,156 ms. Expandir el búfer de profundidad ocupa entre 0,66 y 0,68 ms en esta escena. Activar las sondas finas eleva el raster de referencia a 60,410 ms. La calibración de la sonda queda cerca del tiempo de una operación y produce estimaciones de signo variable para accesos y escrituras. Los recuentos y el control sin observador dan una comparación más estable.

| Escena | Pruebas Z | Rechazos Z | Escrituras de píxel | Píxeles bajo máscara transparente | Asignaciones calientes |
| --- | ---: | ---: | ---: | ---: | ---: |
| w02-overlap | 142.527.255 → 82.715.272 | 137.493.498 → 77.719.676 | 4.995.596 → 4.995.596 | 79.036.433 → 61.809.250 | 96 → 34 |
| opaque-overlap | 248.328.367 → 5.119.200 | 243.209.167 → 0 | 5.119.200 → 5.119.200 | 0 → 0 | 96 → 1 |
| partial | 12.367.476 → 7.502.668 | 10.501.623 → 5.636.815 | 1.865.853 → 1.865.853 | 8.194.253 → 6.363.136 | 25 → 11 |
| large-opaque | 8.294.400 → 8.294.400 | 0 → 0 | 8.294.400 → 8.294.400 | 0 → 0 | 2 → 2 |
| large-transparent | 0 → 0 | 0 → 0 | 0 → 0 | 8.294.400 → 8.294.400 | 2 → 2 |

Cada columna presenta la mediana independiente de su contador. En `w02-overlap` se evitan unos 478 MB de lecturas lógicas de Z por frame. Las escrituras conservan su volumen. El raster de `large-transparent` tarda unos 0,028 ms y el de `large-opaque` 2,511 ms en la referencia. En el primer caso basta recorrer las máscaras. En el segundo los 8,29 millones de píxeles aceptados requieren comparar Z y escribir color y Z.

El estado nuevo ocupa 32 bytes en la pila en esta plataforma. En `w02-overlap` se mantienen 99.563.520 bytes retenidos y un pico transitorio de 732 bytes en el asignador del perfil. En `opaque-overlap` el pico transitorio sigue en 348 bytes. En frío, las asignaciones bajan de 99 a 37 y de 99 a 4 respectivamente. Las asignaciones que desaparecen corresponden a vectores de tramos de los sprites descartados.

## Variantes

A conserva una región opaca anterior por llamada a `sprites::render`. Si un sprite ordinario queda totalmente dentro y tiene profundidad mayor o igual, termina antes de reservar tramos o acceder a textura y Z por píxel. B prueba además a recortar un borde cubierto en X o Y si la otra dimensión está completamente cubierta. Ambas variantes excluyen las rutas de NaN, superposición con empate permitido y cálculo de superficie para impactos.

Una región candidata debe tener más píxeles visibles que texeles para amortizar la búsqueda de índices cero. Una vez certificada se conserva hasta que aparece otra región al menos igual de grande y cercana. Para un sprite opaco, cada píxel de la región queda con profundidad menor o igual a la del sprite. Si la comparación normal falla, esa condición ya se cumplía. Si el filtro de pared elimina una columna, la cota demuestra la misma condición. Los dibujos posteriores solo pueden reducir la profundidad. La región se reinicia por llamada y nunca se conserva a través de callbacks de capa. El criterio opera sobre los límites de píxel recortados con las fórmulas existentes.

## Resultado

Se conserva A. El P50 total baja un 13,72 % en la media geométrica de las 184 combinaciones de escena y resolución. B baja un 13,58 %. Los tres escenarios superpuestos, considerados juntos en las ocho resoluciones, reducen el P50 cerca del 65 % con ambas variantes. El recorte parcial aporta prácticamente la misma ganancia en el objetivo y amplía el cambio al recorrido de sprites parcialmente visibles.

Tiempos en milisegundos. La suma de preparación y render se calcula para cada muestra antes de obtener el percentil.

| Escena | Resolución | Render P50 | Render P95 | Δ P50 render | Preparación + render P50 | Preparación + render P95 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| mixed | 1920×1080 | 5.979 → 5.941 | 6.300 → 6.252 | -0.6 % | 5.980 → 5.942 | 6.301 → 6.253 |
| mixed | 2560×1440 | 14.835 → 14.752 | 15.304 → 15.257 | -0.6 % | 14.836 → 14.753 | 15.305 → 15.257 |
| mixed | 3840×2160 | 31.520 → 31.501 | 32.051 → 32.246 | -0.1 % | 31.521 → 31.502 | 32.053 → 32.247 |
| opaque-overlap | 1920×1080 | 12.726 → 1.443 | 13.427 → 1.642 | -88.7 % | 12.727 → 1.443 | 13.428 → 1.643 |
| opaque-overlap | 2560×1440 | 23.375 → 3.415 | 24.801 → 3.520 | -85.4 % | 23.375 → 3.416 | 24.801 → 3.521 |
| opaque-overlap | 3840×2160 | 57.093 → 6.305 | 62.807 → 6.726 | -89.0 % | 57.094 → 6.305 | 62.808 → 6.727 |
| overlap | 1920×1080 | 9.802 → 6.608 | 9.903 → 6.724 | -32.6 % | 9.802 → 6.608 | 9.904 → 6.725 |
| overlap | 2560×1440 | 17.734 → 12.552 | 17.939 → 12.756 | -29.2 % | 17.735 → 12.553 | 17.940 → 12.756 |
| overlap | 3840×2160 | 43.384 → 28.368 | 44.947 → 29.073 | -34.6 % | 43.385 → 28.369 | 44.948 → 29.074 |
| partial | 1920×1080 | 3.340 → 3.030 | 3.430 → 3.072 | -9.3 % | 3.341 → 3.030 | 3.430 → 3.073 |
| partial | 2560×1440 | 8.199 → 7.680 | 8.603 → 8.136 | -6.3 % | 8.199 → 7.681 | 8.603 → 8.137 |
| partial | 3840×2160 | 16.312 → 15.230 | 16.974 → 15.642 | -6.6 % | 16.312 → 15.231 | 16.975 → 15.643 |
| w02-layers | 1920×1080 | 7.867 → 7.798 | 8.097 → 7.981 | -0.9 % | 7.867 → 7.799 | 8.098 → 7.982 |
| w02-layers | 2560×1440 | 15.118 → 15.073 | 15.581 → 15.814 | -0.3 % | 15.119 → 15.074 | 15.582 → 15.815 |
| w02-layers | 3840×2160 | 34.604 → 34.592 | 35.467 → 35.407 | -0.0 % | 34.605 → 34.593 | 35.468 → 35.408 |
| w02-overlap | 1920×1080 | 8.875 → 5.547 | 9.112 → 5.630 | -37.5 % | 8.876 → 5.547 | 9.112 → 5.630 |
| w02-overlap | 2560×1440 | 16.650 → 11.249 | 16.930 → 11.463 | -32.4 % | 16.651 → 11.250 | 16.931 → 11.464 |
| w02-overlap | 3840×2160 | 40.093 → 24.389 | 41.297 → 25.034 | -39.2 % | 40.094 → 24.389 | 41.298 → 25.035 |

### Control de instrumentación y regresiones

El control sin observador repitió referencia y A en orden RAAR, con 42 muestras por variante y celda. Además de medir latencia, tomó tiempo de proceso con `std::clock`. El caso original bajó de 40,239 a 24,422 ms de CPU a 4K. La latencia del mismo control fue 40,264 → 24,431 ms. Los resultados ordinarios con observador, 40,093 → 24,389 ms, muestran que el coste observado se mantiene al quitar las lecturas del reloj por capa.

| Resolución | CPU P50 w02-overlap | CPU P95 w02-overlap | Reducción CPU P50 |
| --- | ---: | ---: | ---: |
| 1920×1080 | 8,801 → 5,545 ms | 8,905 → 5,612 ms | 37,0 % |
| 2560×1440 | 16,614 → 11,232 ms | 16,944 → 11,478 ms | 32,4 % |
| 3840×2160 | 40,239 → 24,422 ms | 42,783 → 25,016 ms | 39,3 % |

El control `w02-far` a 1440p subió de 1,297 a 1,333 ms de P50 en el barrido completo. En la repetición sin observador pasó de 1,373 a 1,332 ms. Los mayores aumentos absolutos de P95 de A fueron 0,626 ms en `w02-mixed` a 1440p y 0,620 ms en `sprites` a 4K. Se repitieron con órdenes opuestos. En la última serie, `w02-mixed` pasó de 14,587 a 14,485 ms de P95, y `sprites` de 24,910 a 25,082 ms (+0,7 %), con P50 de 24,612 a 24,592 ms. B mostró una subida puntual de 12,9 % de P95 en `open` a 1440p. El control alternado pasó de 4,402 a 4,360 ms. En las cuatro primeras parejas, la mejora de P50 de A en `w02-overlap` fue del 36,9 al 37,8 % a 1080p, del 32,2 al 32,8 % a 1440p y del 38,9 al 39,4 % a 4K.

## Fidelidad

Cada variante y el build final con SDL3 compararon 24.827.781.876 bytes con la referencia. Se incluyeron el color final, profundidad completa, cotas de pared, cobertura de paredes, argumentos y orden del callback de material, y los framebuffers antes y después de cada capa. Las comprobaciones directas cubren profundidad con NaN, paredes, sprites opacos, máscaras, volteos y empates. El umbral de certificación depende del área visible frente al tamaño de la textura en todas las resoluciones.

CTest Release pasó 2/2 sin SDL3 y 3/3 con SDL3. La cobertura y el muestreo de planos verifican P01 y FMA, las pruebas de fisheye verifican X01, el raster de paredes altas verifica W01 y la comparación de capas y sprites verifica S01. Los diagnósticos de W02 siguen en el árbol de trabajo.

## Reproducción

```sh
cmake -S . -B build/s02 -DCMAKE_BUILD_TYPE=Release \
  -DVORTEX_ENABLE_SDL3=OFF -DVORTEX_BUILD_RENDER_BENCHMARK=ON
cmake --build build/s02
build/s02/tests/vortex/vortex_render_benchmark time 13 w02-overlap
build/s02/tests/vortex/vortex_render_profile time 7 w02-overlap
build/s02/tests/vortex/vortex_render_profile probe 7 w02-overlap
ctest --test-dir build/s02 --output-on-failure
```

Para reproducir la comparación, compilar la referencia con los diagnósticos de W02 y el mismo benchmark S02. Ejecutar `vortex_render_benchmark write referencia.bin` con la referencia y `vortex_render_benchmark verify referencia.bin` con cada variante. El archivo de referencia ocupa unos 23,1 GiB. Los CSV, las dos versiones experimentales del raster y los scripts de medición quedan en `build/s02-results`.
