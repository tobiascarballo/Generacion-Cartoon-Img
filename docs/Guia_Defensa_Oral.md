# Guía de Estudio y Defensa Oral — Trabajo Práctico Final Integrador
**Materia:** Computación Avanzada (4° Año — Lic. en Sistemas de Información, UADER FCyT)  
**Docente:** Dra. María Fabiana Piccoli  
**Alumnos:** Carballo Tobías y Dacuy Santino  
**Fechas:** Entrega 06/11/2026 — Defensa 06/11 o 13/11/2026

---

## 1. Resumen Ejecutivo: ¿Qué hicimos y por qué está diseñado así?

El objetivo del trabajo práctico es transformar una imagen digital RGB de entrada en una imagen con apariencia de dibujo animado (**Cartoon**) e implementar, comparar y analizar **cuatro versiones**:
1. **Secuencial (`src/cartoon_seq.c`)**: Línea base de referencia ($T_s$).
2. **Memoria Distribuida con MPI (`src/cartoon_mpi.c`)**: Paralelismo por paso de mensajes entre procesos independientes.
3. **Memoria Compartida con OpenMP (`src/cartoon_omp.c`)**: Paralelismo multihilo dentro de un mismo espacio de direcciones.
4. **Paralela Híbrida MPI + OpenMP (`src/cartoon_hybrid.c`)**: Combina particionado de datos entre procesos MPI (grano grueso) con paralelismo de bucles mediante threads OpenMP dentro de cada proceso (grano fino).

### ¿Por qué hicimos las 4 versiones en lenguaje C (`C11`)?
- Aunque la profesora permitió usar otro lenguaje para MPI si se deseaba, desarrollar las 4 versiones en **C (`-std=c11`)** compartiendo el mismo archivo de cabecera matemático (`include/cartoon_core.h`) tiene dos ventajas decisivas que deben mencionar en la defensa:
  1. **Validez científica del Speedup y la Eficiencia (Inciso 5):** El Speedup se define como $S_p = \frac{T_s}{T_p}$. Si el programa secuencial u OpenMP estuviera en C y el de MPI en Python, la diferencia de tiempos mediría el overhead del intérprete de Python y no el desempeño real del paradigma de memoria distribuida.
  2. **Integración directa en el modelo Híbrido (Inciso 4):** El código híbrido (`cartoon_hybrid.c`) une directamente el particionado `MPI_Scatterv` / `MPI_Sendrecv` / `MPI_Gatherv` de `cartoon_mpi.c` con las directivas `#pragma omp parallel for` de `cartoon_omp.c`.

---

## 2. Explicación Paso a Paso del Algoritmo "Cartoon" (Lo que dice el Apunte)

Una imagen RGB de ancho $W$ y alto $H$ se almacena en memoria como un **arreglo 1D contiguo** de tamaño $W \times H \times 3$ bytes (`uint8_t`), ordenado por filas (*row-major order*). Cada píxel $(y, x)$ ocupa 3 bytes consecutivos: `[R, G, B]` en el índice `(y * W + x) * 3`.

Para producir el efecto **Cartoon**, el enunciado indica ejecutar **dos ramas independientes** sobre la imagen original y **sumar** ambos resultados al final:

### Rama 1: Detección de Bordes (3 pasos obligatorios — Pág. 57 del Apunte)
1. **Paso 1 — Filtrado / Borroneado (`paso1_filtrado_borroneo_rango`, Pág. 48 y 51):**
   - **Objetivo:** Eliminar el ruido de alta frecuencia antes de derivar (porque los operadores de gradiente son muy sensibles al ruido).
   - **Cómo funciona:** Es una **convolución espacial** con una máscara de media aritmética de tamaño $K \times K$ ($K = 3$ o $K = 5$). Para cada píxel, promedia los $K^2$ píxeles vecinos (divide por $9$ en `3x3` o por $25$ en `5x5`).
   - **Condición de borde auxiliar:** Si el vecino cae fuera de la imagen, el enunciado establece que *"el valor neutro es negro"*, por lo que vale `(0, 0, 0)` (*Zero-Padding*).
2. **Paso 2 — Resaltado (`paso2y3_resaltado_y_deteccion_rango`, Pág. 55 y 56):**
   - **Objetivo:** Amplificar los cambios bruscos de intensidad (bordes) estimando el **Gradiente 2D** $\nabla f = \left(\frac{\partial f}{\partial x}, \frac{\partial f}{\partial y}\right)$.
   - **Cómo funciona:** Aplicamos las máscaras de **Sobel** horizontal ($G_x$) y vertical ($G_y$) de tamaño $3 \times 3$ o $5 \times 5$ sobre la imagen ya borroneada, y calculamos la magnitud del gradiente:
     $$|G| = \sqrt{G_x^2 + G_y^2}$$
     Luego ponderamos la luminancia del gradiente ($Y = 0.30 R + 0.59 G + 0.11 B$, Pág. 5 y 12 del apunte) para tener una medida unificada de borde.
3. **Paso 3 — Detección / Umbralado (`Umbralize`, Pág. 10 y 58):**
   - **Objetivo:** Clasificar qué píxeles son efectivamente un borde.
   - **Cómo funciona:** Si la magnitud del gradiente supera el umbral $T$ (`umbral = 90`), el píxel de borde toma el valor máximo (`255`); de lo contrario toma `0` (negro).

### Rama 2: Posterizado (`rama2_posterizado_rango`, Pág. 10 y 12 del Apunte)
- **Objetivo:** Reducir las variaciones tonales continuas ($256$ niveles por canal) a un conjunto pequeño de tonos planos ($r = 3$ o $r = 9$ rangos), imitando el pintado plano de una caricatura.
- **Cómo funciona:**
  - Es una **operación funcional evaluada píxel a píxel** $z' = T(z)$ (no depende de los píxeles vecinos).
  - Para cada valor $z \in [0, 255]$, determinamos a cuál de los $r$ intervalos pertenece: $\text{escalón} = \lfloor \frac{z \cdot r}{256} \rfloor \in \{0, \dots, r-1\}$, y lo mapeamos al valor representativo $\frac{\text{escalón} \cdot 255}{r - 1}$.
  - Como el cálculo depende sólo de $z \in [0, 255]$, precalculamos una tabla de consulta (**LUT** de 256 bytes) al inicio, transformando divisiones en accesos directos $O(1)$ a caché L1.
  - Se posteriza el canal R, el canal G y el canal B y se reúnen en una única imagen RGB posterizada.

### Combinación Final (`combinar_bordes_y_posterizado_rango`)
- Se suman componente a componente la imagen de detección de bordes y la imagen posterizada con saturación en $255$:
  $$\text{Cartoon}(y, x, c) = \min\big(255,\ \text{Posterizado}(y, x, c) + \text{Bordes}(y, x, c)\big)$$

---

## 3. Cómo explicar las 3 Estrategias Paralelas en el Pizarrón / Diapositivas

### A) Memoria Compartida — OpenMP (`cartoon_omp.c`)
- **Estrategia de división:** Descomposición de datos por filas mediante directivas `#pragma omp parallel for schedule(static)`.
- **¿Por qué `schedule(static)`?** Porque la cantidad de operaciones aritméticas por cada fila de la imagen es exactamente constante y predecible (para filtro $3 \times 3$ son 9 lecturas por píxel; para $5 \times 5$ son 25 lecturas por píxel). `static` divide las filas en bloques contiguos iguales sin overhead de planificación en tiempo de ejecución y maximiza la localidad espacial de caché.
- **Análisis de Dependencia de Datos (conexión con TP4):**
  - Dentro de una misma etapa (ej. Borroneo), cada iteración escribe en una fila distinta de `img_blur` y solo lee de `img_orig`. **No hay condiciones de carrera (Race Conditions)** ni hace falta usar `#pragma omp critical` ni `atomic`.
  - **Entre** el Paso 1 (Borroneo) y el Paso 2 (Sobel) sí existe una **dependencia de datos verdadera (Read-After-Write)**: para calcular Sobel en la fila $y$, un thread necesita que las filas vecinas $[y - R, y + R]$ de `img_blur` ya estén calculadas. La barrera implícita al cierre del `#pragma omp for` del Paso 1 garantiza esta sincronización sin código extra.

### B) Memoria Distribuida — MPI (`cartoon_mpi.c`)
- **Estrategia de división:** Descomposición de dominio 1D por bloques de filas equilibrados (igual que en el Ejercicio 4 y 5 del TP3):
  - Si $H$ no es múltiplo exacto de $P$, los primeros `H % P` procesos reciben $\lfloor H/P \rfloor + 1$ filas y los demás reciben $\lfloor H/P \rfloor$ filas (desbalance máximo de 1 fila).
  - Se utiliza `MPI_Scatterv` para distribuir las sub-matrices desde `rank = 0` y `MPI_Gatherv` para recolectar la imagen Cartoon terminada.
- **El problema de los bordes entre procesos y las Filas Fantasma (*Halo / Ghost Rows*):**
  - Para aplicar un filtro de tamaño $K \times K$ (radio $R = \lfloor K/2 \rfloor$, donde $R = 1$ para $3 \times 3$ y $R = 2$ para $5 \times 5$) sobre la primera fila de su bloque, el proceso `rank` necesita leer $R$ filas que pertenecen al proceso `rank - 1`. De igual modo, para su última fila necesita $R$ filas de `rank + 1`.
  - **Solución:** Cada proceso reserva memoria para $m + 2R$ filas:
    - Filas `0 .. R-1`: Halo superior (recibe de `arriba = rank - 1`).
    - Filas `R .. R+m-1`: Las $m$ filas propias del proceso.
    - Filas `R+m .. m+2R-1`: Halo inferior (recibe de `abajo = rank + 1`).
  - Antes del Paso 1 (Borroneo) y antes del Paso 2 (Sobel), ejecutamos `intercambiar_filas_fantasma()` usando `MPI_Sendrecv` y `MPI_PROC_NULL` en los extremos (`rank = 0` hacia arriba y `rank = P-1` hacia abajo).
  - **¿Por qué hay 2 intercambios de halos?**
    1. El primer intercambio envía los bordes de la imagen original antes de calcular el Borroneo.
    2. Como cada proceso sólo calcula el Borroneo de sus $m$ filas propias, antes de aplicar Sobel sobre la imagen borroneada necesita recibir las $R$ filas ya borroneadas de sus vecinos.

### C) Paralela Híbrida — MPI + OpenMP (`cartoon_hybrid.c`)
- **Motivación:** En clusters modernos (nodos interconectados por red donde cada nodo posee múltiples núcleos en memoria compartida), usar **MPI entre nodos/procesos** y **OpenMP dentro de cada proceso** reduce:
  1. La cantidad de mensajes que viajan por la red y la cantidad de filas fantasma duplicadas.
  2. La huella de memoria de buffers MPI.
- **Implementación:**
  - Inicializamos con `MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provisto)`.
  - **Nivel superior (MPI):** Divide la imagen en $P$ particiones mediante `MPI_Scatterv` e intercambia filas fantasma con `MPI_Sendrecv`.
  - **Nivel inferior (OpenMP):** Dentro de cada uno de los $P$ procesos MPI, se lanza un equipo de $T$ threads (`#pragma omp parallel for schedule(static)`) que reparte las $m$ filas locales, logrando $P \times T$ flujos de ejecución concurrentes.
  - Además, fusionamos los bucles de Sobel + Umbralado + Posterizado + Suma dentro de la segunda región paralela para reutilizar la caché L1/L2 por cada fila local.

---

## 4. Propuesta de División de la Exposición Oral (Tobías y Santino)

- **Parte 1 (Tobías — 7 a 8 min):**
  1. Introducción al problema: Representación RGB como matriz contigua 1D y explicación de las dos ramas del algoritmo Cartoon (Detección de bordes en 3 pasos: Borroneo, Resaltado por Sobel y Umbralado; y Posterizado por canal RGB + Suma final).
  2. Demostración visual de las etapas intermedias (`panel_etapas_cartoon.png`) y diferencia visual entre filtros `3x3` vs `5x5` y posterizado `3` vs `9` (`comparativa_parametros_cartoon.png`).
  3. Implementación Secuencial (`cartoon_seq.c`) y en Memoria Compartida con OpenMP (`cartoon_omp.c`): análisis de dependencias entre etapas, ausencia de condiciones de carrera y elección de `schedule(static)`.
- **Parte 2 (Santino — 7 a 8 min):**
  1. Implementación en Memoria Distribuida con MPI (`cartoon_mpi.c`): particionado por bloques de filas con `MPI_Scatterv`/`MPI_Gatherv` y explicación detallada del intercambio de **filas fantasma (Halo Rows)** con `MPI_Sendrecv` y `MPI_PROC_NULL`.
  2. Implementación Paralela Híbrida MPI + OpenMP (`cartoon_hybrid.c`): motivación de la computación híbrida, ventajas/desventajas, uso de `MPI_THREAD_FUNNELED` y cómo colaboran $P$ procesos $\times$ $T$ threads.
  3. Análisis de resultados experimentales (`Speedup` y `Eficiencia` en `800x800`, `2000x2000` y `5000x5000`) y conclusiones finales.

---

## 5. Preguntas Frecuentes de Examen / Defensa (con Respuestas Exactas)

1. **¿Por qué eligieron particionar la imagen por bloques de filas y no por columnas o por bloques 2D (cuadrículas)?**
   - *Respuesta:* Porque en C las matrices se almacenan por filas (*row-major order*). Al particionar por filas, cada sub-imagen y cada fila fantasma es un **bloque contiguo de bytes en memoria**, lo que permite enviarla con una sola llamada a `MPI_Scatterv` y `MPI_Sendrecv` sin tener que empaquetar columnas dispersas (`MPI_Type_vector`) y aprovecha al máximo las líneas de caché del procesador.
2. **¿Por qué para un filtro `3x3` se envía 1 fila fantasma por lado y para un filtro `5x5` se envían 2 filas fantasma por lado?**
   - *Respuesta:* Porque el radio de vecindad de una máscara de convolución cuadrada de lado $K$ es $R = \lfloor K/2 \rfloor$. Para centrar una máscara de $3 \times 3$ en la primera fila local se necesita leer $1$ fila superior; para una máscara de $5 \times 5$ se necesitan leer $2$ filas superiores y $2$ inferiores.
3. **¿Cómo resolvieron la condición de borde ("el valor neutro es negro") en los extremos de la imagen en MPI?**
   - *Respuesta:* Los buffers locales se reservan con `calloc` (inicializados en `0`, que en RGB es negro `(0,0,0)`). Al definir `arriba = (rank > 0) ? rank - 1 : MPI_PROC_NULL` y `abajo = (rank < p - 1) ? rank + 1 : MPI_PROC_NULL`, el proceso `rank 0` no recibe nada en su halo superior y el proceso `rank P-1` no recibe nada en su halo inferior, conservando los ceros iniciales automáticamente.
4. **¿Qué pasa con el Speedup y la Eficiencia cuando pasamos de 8 a 16 threads en este procesador (AMD Ryzen 7 5700U)?**
   - *Respuesta:* El procesador tiene **8 núcleos físicos y 16 hilos lógicos (SMT / HyperThreading)**. Hasta 8 workers cada hilo corre en su propio núcleo físico con sus unidades aritméticas dedicadas. Al pasar de 8 a 16 hilos, pares de hilos comparten el mismo núcleo físico y el ancho de banda a memoria RAM, por lo que el Speedup sigue subiendo levemente pero la **Eficiencia ($E = S / p$)** cae a cerca de la mitad, tal como predice la teoría.
5. **¿Por qué el Speedup mejora cuando aumentamos el tamaño de imagen de `800x800` a `5000x5000` o el filtro de `3x3` a `5x5`?**
   - *Respuesta:* Por la **Ley de Gustafson (Escalabilidad Débil)** y la relación cómputo/comunicación: en `800x800` el tiempo total es de apenas milisegundos, por lo que la latencia fija de crear procesos/hilos y sincronizar pesa proporcionalmente más. En `5000x5000` con filtro `5x5` (25 millones de píxeles $\times$ 25 operaciones por máscara), la carga aritmética domina ampliamente sobre el costo de comunicación, logrando los mayores Speedups y Eficiencias.
