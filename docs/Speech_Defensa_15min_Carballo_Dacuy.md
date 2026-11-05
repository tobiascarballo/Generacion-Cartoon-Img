# Guion Completo de Exposición Oral (15 Minutos) — Defensa Trabajo Práctico Final

**Asignatura:** Computación Avanzada (4° Año — Lic. en Sistemas de Información, UADER FCyT)  
**Docente:** Dra. María Fabiana Piccoli  
**Expositores:** Tobías Carballo y Santino Dacuy  
**Duración Total:** 15 minutos exactos (divididos en 6 bloques equitativos: ~7 min 30 s cada uno) + ronda de preguntas.

---

## Estructura General de Tiempos (15 Minutos)

| Minuto | Bloque | Orador | Contenido Principal | Apoyo Visual en Pantalla |
| :--- | :--- | :--- | :--- | :--- |
| **00:00 – 02:30** | **Bloque 1** | **Tobías Carballo** | Presentación del problema, representación de la imagen en memoria y arquitectura del algoritmo Cartoon (Rama 1: Bordes en 3 pasos; Rama 2: Posterizado; Suma final). | Informe (`Figura 1`, `Figura 2` y `Figura 2b` con `foto1.jpg`, `foto2.jpg`, `foto3.jpg`). |
| **02:30 – 05:00** | **Bloque 2** | **Santino Dacuy** | Marco teórico de Computación Paralela Híbrida (características, ventajas y desventajas) y explicación detallada del núcleo matemático en `include/cartoon_core.h` y versión Secuencial (`src/cartoon_seq.c`). | Código abierto en `include/cartoon_core.h` y `src/cartoon_seq.c`. |
| **05:00 – 07:30** | **Bloque 3** | **Tobías Carballo** | Paralelización en Memoria Compartida con OpenMP (`src/cartoon_omp.c`): estrategia de división por filas, directivas `#pragma omp parallel for schedule(static)`, ausencia de condiciones de carrera y sincronización entre etapas. | Código abierto en `src/cartoon_omp.c` + Gráfico `openmp_speedup_eficiencia.png`. |
| **07:30 – 10:30** | **Bloque 4** | **Santino Dacuy** | Paralelización en Memoria Distribuida con MPI (`src/cartoon_mpi.c`): descomposición de dominio 1D con `MPI_Scatterv`/`MPI_Gatherv`, problema de dependencias espaciales y solución mediante **Filas Fantasma (Halo Rows)** con `MPI_Sendrecv` y `MPI_PROC_NULL`. | Código abierto en `src/cartoon_mpi.c` + Gráfico `mpi_speedup_eficiencia.png`. |
| **10:30 – 13:00** | **Bloque 5** | **Tobías Carballo** | Arquitectura Híbrida MPI + OpenMP (`src/cartoon_hybrid.c`), inicialización `MPI_THREAD_FUNNELED`, fusión de bucles para localidad de caché y **Demostración en Vivo** en terminal sobre `foto1.jpg`, `foto2.jpg` y `foto3.jpg`. | Código `src/cartoon_hybrid.c` + Terminal ejecutando en vivo. |
| **13:00 – 15:00** | **Bloque 6** | **Santino Dacuy** | Análisis experimental completo (Inciso 5): tablas y curvas en `800x800`, `2000x2000` y `5000x5000`, impacto de filtros `3x3` vs `5x5` y posterizado `3` vs `9`, límite físico de 8 núcleos vs 16 hilos SMT y conclusiones finales. | Gráficos `comparativa_paradigmas_5000.png` e `impacto_filtros_y_comunicacion.png`. |

---

## Guion Palabra por Palabra (Minuto a Minuto)

---

### BLOQUE 1 (00:00 a 02:30) — Orador: TOBÍAS CARBALLO
**Tema:** Introducción, Representación de la Imagen y Algoritmo de Generación de Cartoons  
**En pantalla:** Abrir el Informe Word (`Informe_TP_Final_Carballo_Dacuy.docx`) en las páginas 1 y 2 mostrando el diagrama de etapas (`panel_etapas_cartoon.png` y `panel_fotos_reales.png`).

> **[Tobías — 00:00 a 01:00]**  
> *"Buenas tardes profesora. Con mi compañero Santino Dacuy vamos a presentar nuestro Trabajo Práctico Final Integrador de Computación Avanzada, que consiste en el diseño, paralelización y análisis de desempeño de un generador de imágenes estilo Cartoon desarrollado íntegramente en lenguaje C (estándar C11) bajo cuatro paradigmas: Secuencial, Memoria Compartida con OpenMP, Memoria Distribuida con MPI y Computación Paralela Híbrida combinando MPI más OpenMP.*  
> *En primer lugar, respecto a la representación de datos: toda imagen digital RGB de ancho $W$ por alto $H$ se carga en memoria como un arreglo unidimensional contiguo ordenado por filas (row-major order) de $W \times H \times 3$ bytes sin estructuras anidadas ni punteros dobles. Esto es fundamental tanto para maximizar la localidad espacial en la caché del procesador como para poder enviar bloques contiguos de filas directamente a través de MPI. Además, tal como solicita la consigna, para todos los píxeles de borde donde la máscara de convolución sobresale de las coordenadas válidas de la imagen, adoptamos el color neutro negro $(0, 0, 0)$."*

> **[Tobías — 01:00 a 02:30]**  
> *"¿Cómo funciona el algoritmo para transformar una fotografía real en un Cartoon? Como vemos en la Figura 1 y en las pruebas con nuestras propias fotografías (`foto1.jpg`, `foto2.jpg` y `foto3.jpg`), el algoritmo se divide en dos ramas independientes que luego se combinan:*  
> *La **Primera Rama es la Detección de Bordes**, que implementamos respetando estrictamente los 3 pasos del apunte de cátedra:*  
> *1. **Paso 1 — Filtrado o Borroneo:** Aplicamos un filtro pasa-bajo de media aritmética con máscara de $3\times 3$ (dividiendo por 9) o de $5\times 5$ (dividiendo por 25). Su objetivo es suavizar la imagen y eliminar el ruido de alta frecuencia para que las derivadas no disparen falsos bordes.*  
> *2. **Paso 2 — Resaltado de Bordes:** Sobre la imagen ya borroneada, aproximamos el gradiente bidimensional aplicando las máscaras direccionales de **Sobel** horizontal ($G_x$) y vertical ($G_y$), tanto en su formulación de $3\times 3$ como en la extendida de $5\times 5$. Obtenemos la magnitud del gradiente $|\nabla I| = \sqrt{G_x^2 + G_y^2}$ por canal y calculamos la luminancia ponderada ($0.30 R + 0.59 G + 0.11 B$).*  
> *3. **Paso 3 — Detección mediante Umbralado (`Umbralize`):** Comparamos la magnitud del gradiente contra un umbral; si lo supera, asignamos blanco ($255$) marcando el borde, y si no, negro ($0$).*  
> *En paralelo, la **Segunda Rama es el Posterizado**: tomamos la imagen original y cuantizamos cada canal de color (R, G y B por separado) reduciendo los 256 niveles continuos a un conjunto discreto de solo **3 rangos** o **9 rangos** mediante una tabla de consulta (LUT) precalculada en $O(1)$, y sumamos los tres canales cuantizados en una única imagen posterizada.*  
> *Finalmente, en el **Paso de Combinación**, sumamos píxel a píxel con saturación en 255 la imagen de detección de bordes y la imagen posterizada, obteniendo el Cartoon final."*

---

### BLOQUE 2 (02:30 a 05:00) — Orador: SANTINO DACUY
**Tema:** Computación Paralela Híbrida y Diseño del Núcleo Secuencial (`cartoon_core.h` y `cartoon_seq.c`)  
**En pantalla:** Mostrar `include/cartoon_core.h` y `src/cartoon_seq.c` en el editor.

> **[Santino — 02:30 a 03:45]**  
> *"Gracias Tobías. Antes de entrar en cada versión paralela, quiero introducir el concepto central que motiva el trabajo: la **Computación Paralela Híbrida**. Los sistemas actuales de alto desempeño (clusters modernos) son jerárquicos: están formados por múltiples nodos interconectados por red (memoria distribuida), pero cada nodo contiene internamente procesadores multi-núcleo que comparten memoria RAM y niveles de caché (memoria compartida).*  
> *El modelo híbrido combina **MPI** para el paralelismo de grano grueso entre nodos o procesos pesados, y **OpenMP** para el paralelismo de grano fino dentro de cada proceso.*  
> *¿Cuáles son sus **ventajas**? Primero, reduce drásticamente el overhead de comunicación y el consumo de memoria: en lugar de lanzar 16 procesos MPI independientes en una misma máquina —lo que implica 16 espacios de memoria separados y 15 fronteras de intercambio de datos—, podemos lanzar por ejemplo 2 o 4 procesos MPI y que cada uno abra 4 hilos OpenMP que comparten la misma memoria caché L3 y las mismas tablas de lectura. Como **desventaja o desafío**, exige gestionar dos niveles de sincronización y configurar cuidadosamente el soporte de hilos en MPI (`MPI_Init_thread`) para evitar contención."*

> **[Santino — 03:45 a 05:00]**  
> *"Para garantizar que la comparativa de Speedup y Eficiencia entre los 4 programas sea 100% justa y científicamente válida, diseñamos el módulo `include/cartoon_core.h`. Allí encapsulamos exactamente las mismas funciones matemáticas por rango de filas (`[fila_ini, fila_fin)`) que invocan las cuatro versiones:*  
> *— `paso1_filtrado_borroneo_rango()` aplica la convolución de media $3\times 3$ o $5\times 5$.*  
> *— `paso2y3_resaltado_y_deteccion_rango()` aplica las máscaras de Sobel y el umbralado.*  
> *— `rama2_posterizado_rango()` construye la LUT de 3 o 9 rangos y cuantiza cada canal RGB.*  
> *— Y `combinar_bordes_y_posterizado_rango()` realiza la suma saturada.*  
> *En el **Inciso 1 (`src/cartoon_seq.c`)**, invocamos estas cuatro etapas de forma secuencial sobre el rango completo `[0, alto)`, midiendo con cronómetros de alta resolución (`QueryPerformanceCounter` / `clock_gettime`) el tiempo puro de procesamiento $T_s$, es decir, excluyendo la lectura y escritura en disco para no distorsionar la métrica."*

---

### BLOQUE 3 (05:00 a 07:30) — Orador: TOBÍAS CARBALLO
**Tema:** Memoria Compartida con OpenMP (`src/cartoon_omp.c` — Inciso 3)  
**En pantalla:** Mostrar `src/cartoon_omp.c` (líneas 75 a 135) y el gráfico `openmp_speedup_eficiencia.png`.

> **[Tobías — 05:00 a 06:30]**  
> *"Pasando al **Inciso 3 (`src/cartoon_omp.c`)**, implementamos la versión en Memoria Compartida utilizando **OpenMP** en C. Aquí toda la matriz de la imagen reside en el espacio de direcciones compartido del proceso.*  
> *¿Qué estrategia de división de trabajo adoptamos? Aplicamos **descomposición de datos por filas** sobre el bucle externo $y \in [0, H)$ utilizando la directiva `#pragma omp parallel for default(none) schedule(static)`.*  
> *Elegimos planificación **`schedule(static)`** por dos razones técnicas clave: en primer lugar, porque tanto la convolución de borroneo como Sobel y el posterizado realizan exactamente la misma cantidad de operaciones aritméticas en cada fila de la imagen; al ser una carga perfectamente uniforme, el reparto estático divide las $H$ filas en bloques contiguos de tamaño $H/T$ con **cero overhead de planificación en tiempo de ejecución** y máxima localidad espacial en caché.*  
> *En segundo lugar, ¿cómo evitamos condiciones de carrera (data races) y resolvemos las dependencias de datos? Dentro de cada etapa, la imagen de entrada es estrictamente de **solo lectura (`const uint8_t *`)** y cada hilo $t$ escribe únicamente en sus propias filas asignadas del buffer de salida (`img_blur`, `img_bordes`, `img_post` o `img_cartoon`), por lo que los conjuntos de escritura son disjuntos y no se requieren regiones críticas ni locks."*

> **[Tobías — 06:30 a 07:30]**  
> *"Sin embargo, sí existe una **dependencia de datos entre el Paso 1 y el Paso 2**: para que el operador de Sobel en el Paso 2 pueda calcular el gradiente de la fila $y$, necesita que las filas vecinas $y-R \dots y+R$ de `img_blur` ya hayan terminado de borronearse en el Paso 1. Esto queda resuelto mediante la **barrera implícita** al cierre del primer `#pragma omp parallel for`: ningún hilo comienza el Paso 2 de Sobel hasta que todos los hilos finalizaron el Paso 1 de borroneo. En cambio, una vez que `img_blur` está lista, las filas de Sobel, Posterizado y Suma final son totalmente independientes entre sí."*

---

### BLOQUE 4 (07:30 a 10:30) — Orador: SANTINO DACUY
**Tema:** Memoria Distribuida con MPI y Filas Fantasma (`src/cartoon_mpi.c` — Inciso 2)  
**En pantalla:** Mostrar `src/cartoon_mpi.c` (función `intercambiar_halos` y `main`) y el gráfico `mpi_speedup_eficiencia.png`.

> **[Santino — 07:30 a 09:00]**  
> *"En el **Inciso 2 (`src/cartoon_mpi.c`)**, abordamos el modelo de **Memoria Distribuida con MPI**, donde cada uno de los $P$ procesos posee su propio espacio de memoria privado.*  
> *Utilizamos una **descomposición de dominio 1D por bloques de filas**. Como el alto $H$ de una imagen no siempre es múltiplo exacto de la cantidad de procesos $P$ —por ejemplo, en nuestra `foto2.jpg` que mide $860\times 573$ píxeles—, calculamos arreglos `sendcounts` y `displs` repartiendo el resto $H \bmod P$ de forma balanceada entre los primeros procesos, y distribuimos la imagen desde el proceso raíz (`rank 0`) mediante la operación colectiva **`MPI_Scatterv`**.*  
> *Ahora bien, aquí aparece el desafío central de cualquier operación de vecindad en memoria distribuida: cuando el proceso $k$ debe aplicar una máscara de $3\times 3$ (radio $R = 1$) o de $5\times 5$ (radio $R = 2$) sobre su primera fila local (fila $0$) o su última fila local (fila $m_k - 1$), necesita leer hasta $R$ filas que pertenecen físicamente al proceso vecino superior (`rank - 1`) y $R$ filas del proceso vecino inferior (`rank + 1`)."*

> **[Santino — 09:00 a 10:30]**  
> *"Para resolver esto sin comunicar toda la imagen, implementamos la técnica de **Filas Fantasma (Halo / Ghost Rows)**. Cada proceso aloja un buffer extendido de $(m_k + 2R)$ filas: las primeras $R$ filas son el halo superior, las $m_k$ filas centrales son las filas propias, y las últimas $R$ filas son el halo inferior.*  
> *Creamos la función `intercambiar_halos()`, que utiliza **`MPI_Sendrecv`** para emparejar el envío y la recepción simultáneamente con `rank - 1` y `rank + 1`, garantizando **ausencia total de interbloqueos (deadlocks)**. Además, para los procesos extremos (`rank 0` en el borde superior de la imagen y `rank P - 1` en el borde inferior), asignamos como vecino a la constante **`MPI_PROC_NULL`**: así MPI ignora esa comunicación sin necesidad de `if/else` complejos y las filas fantasma externas conservan su inicialización en `0` (`calloc`), cumpliendo automáticamente con el borde auxiliar neutro en color negro $(0, 0, 0)$ pedido por la cátedra.*  
> *Realizamos exactamente **dos intercambios de halos**: el primero sobre `local_orig` antes del Paso 1 (Borroneo), y el segundo sobre `local_blur` antes del Paso 2 (Sobel), ya que Sobel necesita las filas vecinas ya borroneadas. El Posterizado y la Suma son operaciones punto a punto que no requieren comunicación, y al finalizar recolectamos las sub-imágenes en `rank 0` con **`MPI_Gatherv`**."*

---

### BLOQUE 5 (10:30 a 13:00) — Orador: TOBÍAS CARBALLO
**Tema:** Versión Híbrida MPI + OpenMP (`src/cartoon_hybrid.c` — Inciso 4) y Demostración en Vivo  
**En pantalla:** Mostrar `src/cartoon_hybrid.c` y luego cambiar a la consola PowerShell para ejecutar en vivo.

> **[Tobías — 10:30 a 11:45]**  
> *"En el **Inciso 4 (`src/cartoon_hybrid.c`)**, integramos ambos paradigmas en nuestra solución de **Computación Paralela Híbrida (MPI + OpenMP)**.*  
> *El programa se inicializa mediante `MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provisto)`. Bajo el nivel `MPI_THREAD_FUNNELED`, todas las llamadas a MPI (`MPI_Scatterv`, los dos `MPI_Sendrecv` de filas fantasma y `MPI_Gatherv`) son ejecutadas exclusivamente por el hilo maestro de cada proceso MPI fuera de las regiones paralelas, mientras que los $T$ hilos de OpenMP se despliegan dentro de cada proceso para paralelizar los bucles de filas locales $m_k$.*  
> *Además, en la versión híbrida aplicamos una optimización clave de **fusión de bucles (loop fusion)**: una vez que se intercambian los halos de `local_blur`, un único bloque `#pragma omp parallel for schedule(static)` ejecuta para cada fila $y$ el cálculo de Sobel+Umbral, el Posterizado RGB y la Suma saturada en la misma pasada, manteniendo la fila caliente en la caché L1/L2 de cada núcleo."*

> **[Tobías — 11:45 a 13:00 — DEMOSTRACIÓN EN VIVO]**  
> *"Vamos a ejecutarlo ahora mismo en vivo sobre nuestras fotografías (`foto1.jpg`, `foto2.jpg` y `foto3.jpg`) para ver cómo trabajan las 4 versiones y cómo producen exactamente el mismo resultado píxel a píxel:"*  
> *(Ejecutar en la terminal los comandos del recuadro inferior y abrir la imagen resultante).*

#### Comandos listos para copiar y pegar en la Demo en Vivo:
```powershell
$env:PATH = "C:\tmp\toolchain\mingw64\bin;C:\tmp\toolchain\msmpi\runtime_1;" + $env:PATH
cd "c:\Users\tobic\OneDrive\Documentos\Facu\4to Año Sistemas\Computación Avanzada\TPs\TP-Final"

# 1. Ejecutar version Secuencial con exportacion de las 3 etapas intermedias (--etapas)
.\bin\cartoon_seq.exe foto2.jpg img\output\demo_seq.png 5 9 75 --etapas

# 2. Ejecutar version OpenMP con 8 hilos
.\bin\cartoon_omp.exe foto2.jpg img\output\demo_omp.png 5 9 8 75

# 3. Ejecutar version Hibrida (2 procesos MPI x 4 hilos OpenMP = 8 workers)
mpiexec -n 2 .\bin\cartoon_hybrid.exe foto2.jpg img\output\demo_hyb.png 5 9 4 75
```

---

### BLOQUE 6 (13:00 a 15:00) — Orador: SANTINO DACUY
**Tema:** Análisis de Resultados Experimentales (Inciso 5 y 6): Speedup, Eficiencia y Conclusiones  
**En pantalla:** Mostrar los gráficos `comparativa_paradigmas_5000.png` e `impacto_filtros_y_comunicacion.png` del Informe.

> **[Santino — 13:00 a 14:15]**  
> *"Para cerrar la exposición, presentamos el **Análisis de Desempeño del Inciso 5**. Evaluamos sistemáticamente las tres resoluciones exigidas por la consigna —**`800x800`** (640 mil píxeles), **`2000x2000`** (4 millones de píxeles) y **`5000x5000`** (25 millones de píxeles)— combinadas con máscaras de **$3\times 3$ y $5\times 5$** y posterizados de **3 y 9 rangos**, sobre un procesador AMD Ryzen 7 5700U de **8 núcleos físicos y 16 hilos lógicos (SMT)**.*  
> *Previo a medir tiempos, verificamos mediante un script automatizado que las 4 versiones producen **Diferencia Máxima = 0 píxel a píxel** en todas las imágenes y configuraciones.*  
> *De las curvas y tablas obtenemos cuatro conclusiones fundamentales:*  
> *1. **Efecto del tamaño del problema (Ley de Amdahl / Gustafson):** En la imagen chica de `800x800` con filtro $3\times 3$, el cálculo secuencial tarda apenas $28\text{ ms}$; allí el costo de crear hilos o comunicar procesos limita el Speedup a $5.25\times$ en OpenMP y $3.35\times$ en MPI. Pero al escalar a **`5000x5000`**, el trabajo aritmético domina ampliamente sobre la sobrecarga de sincronización, logrando un **Speedup de $8.07\times$ en OpenMP** (con eficiencia de hasta **94%** en 4 hilos) y un **Speedup de $7.71\times$ en la versión Híbrida $4\text{P}\times 4\text{T}$** con filtro $5\times 5$, reduciendo el tiempo de ejecución de **7.20 segundos a solo 0.93 segundos**.*  
> *2. **Impacto del tamaño de filtro ($3\times 3$ vs $5\times 5$) y del posterizado ($3$ vs $9$):** Pasar de una máscara de $3\times 3$ (9 celdas) a una de $5\times 5$ (25 celdas) incrementa la carga teórica de convolución en $25/9 = 2.78\times$, y en nuestras mediciones el tiempo sube exactamente un promedio de $2.57\times$, elevando la eficiencia paralela porque cada worker realiza más cómputo útil por cada byte comunicado. En cambio, variar el posterizado entre **3 y 9 rangos no altera el tiempo de ejecución**, ya que resolvimos la cuantización con una tabla LUT en $O(1)$."*

> **[Santino — 14:15 a 15:00]**  
> *3. **Núcleos Físicos vs Hilos Lógicos (SMT):** Hasta $p = 8$ workers (uno por núcleo físico), el Speedup crece de forma cuasi-lineal y la eficiencia se mantiene entre el 73% y el 94%. Al pasar de 8 a 16 workers activando HyperThreading/SMT, el Speedup sigue aumentando pero la eficiencia cae al rango del 45%–50%, porque cada par de hilos lógicos compite por las mismas unidades aritméticas y caché L1/L2 del núcleo físico.*  
> *4. **Superioridad del Modelo Híbrido en Alta Carga:** Como muestra la Figura 6 de desglose entre cómputo y comunicación, en la imagen de `5000x5000` con filtro $5\times 5$, las comunicaciones MPI representan menos del 7% del tiempo total, y la versión **Híbrida ($4\text{P}\times 4\text{T}$ y $2\text{P}\times 4\text{T}$)** logra superar tanto a MPI puro como a OpenMP puro al reducir la cantidad de mensajes colectivos y aprovechar la fusión de bucles en caché.*  
> *Muchas gracias profesora, quedamos a su entera disposición para cualquier pregunta sobre el código o el informe."*

---

## Banco de Preguntas Frecuentes de Examen (Defensa Oral)

1. **¿Por qué hay DOS llamadas a `intercambiar_halos()` en MPI e Híbrido y no una sola al principio?**  
   - **Respuesta:** Porque existen dos operadores de vecindad encadenados: el Paso 1 (Borroneo) necesita las $R$ filas vecinas de la imagen original (`local_orig`), pero el Paso 2 (Sobel) se calcula sobre la **imagen ya borroneada** (`local_blur`). Como cada proceso solo borronea sus propias $m_k$ filas interiores, las filas fantasma de `local_blur` quedan vacías hasta que realizamos el segundo `intercambiar_halos(local_blur)` antes de ejecutar Sobel.
2. **¿Cómo resolvieron qué pasa en los bordes de la imagen cuando la máscara de $3\times 3$ o $5\times 5$ se sale fuera de rango?**  
   - **Respuesta:** La consigna indica tomar el valor neutro negro $(0, 0, 0)$. En Secuencial y OpenMP (`obtener_pixel_con_borde_negro`), si la coordenada $(y, x)$ cae fuera de $[0, H) \times [0, W)$, devolvemos `0`. En MPI e Híbrido, en los bordes verticales (`x < 0` o `x >= W`) aplica la misma condición, y en los bordes horizontales globales (`rank 0` hacia arriba y `rank P-1` hacia abajo) usamos `MPI_PROC_NULL` en `MPI_Sendrecv` sobre un buffer inicializado con `calloc` (ceros), por lo que las filas fantasma exteriores quedan automáticamente en negro $(0,0,0)$.
3. **¿Por qué usaron `MPI_Scatterv` y `MPI_Gatherv` en lugar de `MPI_Scatter` y `MPI_Gather` comunes?**  
   - **Respuesta:** Porque `MPI_Scatter` exige que la cantidad de filas $H$ sea divisible exactamente por la cantidad de procesos $P$. Con `MPI_Scatterv` y `MPI_Gatherv`, si $H$ no es múltiplo de $P$ (como en `foto1.jpg` de 906 filas o `foto2.jpg` de 573 filas con 4 u 8 procesos), repartimos el resto $H \bmod P$ dando 1 fila extra a los primeros procesos, logrando balance de carga óptimo y soportando cualquier dimensión de imagen.
4. **¿Qué nivel de soporte de hilos inicializaron en el programa Híbrido y qué significa?**  
   - **Respuesta:** Usamos `MPI_Init_thread(..., MPI_THREAD_FUNNELED, ...)`. Significa que el programa es multihilo (tiene regiones OpenMP), pero garantizamos que **solo el hilo principal (master thread)** realiza llamadas a funciones de MPI, siempre fuera de las regiones `#pragma omp parallel`.
