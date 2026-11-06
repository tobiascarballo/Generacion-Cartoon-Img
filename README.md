# Generación de Imágenes Estilo Cartoon con Computación Paralela

**Trabajo Práctico Final Integrador — Computación Avanzada**  
*Licenciatura en Sistemas de Información — Facultad de Ciencia y Tecnología (FCyT), UADER*  
* **Alumnos:** Tobías Carballo y Santino Dacuy  
* **Docente Titular:** Dra. María Fabiana Piccoli  

---

## 📌 Descripción del Proyecto

Este proyecto implementa el pipeline completo de procesamiento digital de imágenes para la transformación de fotografías RGB en imágenes con **efecto Cartoon** (estilo caricatura / historieta), conforme a los lineamientos teóricos del apunte de cátedra.

El sistema fue diseñado y evaluado bajo **cuatro paradigmas de ejecución** en lenguaje C (estándar C11):
1. **Secuencial (`cartoon_seq.c`)**: Línea base (*baseline*) de comparación de rendimiento.
2. **Memoria Compartida (`cartoon_omp.c`)**: Paralelismo multihilo mediante directivas **OpenMP** con partición estática de filas.
3. **Memoria Distribuida (`cartoon_mpi.c`)**: Paralelismo mediante paso de mensajes con **MPI** (`MPI_Scatterv`, `MPI_Gatherv`, intercambio bidireccional de filas fantasma / *halo rows* con `MPI_Sendrecv`).
4. **Computación Paralela Híbrida (`cartoon_hybrid.c`)**: Modelo multinivel **MPI + OpenMP** (`MPI_THREAD_FUNNELED`).

---

## ⚙️ Arquitectura del Algoritmo (Pipeline Cartoon)

El algoritmo se divide en dos ramas independientes que procesan la imagen de entrada y luego se combinan píxel a píxel:

```
                  ┌───────────────────────────────┐
                  │    Imagen Original (RGB)      │
                  └──────────────┬────────────────┘
                                 │
                 ┌───────────────┴───────────────┐
                 │                               │
                 ▼                               ▼
       [RAMA 1: BORDES]                 [RAMA 2: COLOR]
  1. Suavizado (Media Aritmética)      Cuantización por canal
     Filtro pasa-bajo 3x3 o 5x5        independiente (3 o 9 rangos)
                 │                     con tabla LUT O(1)
                 ▼                               │
  2. Resaltado (Gradiente Sobel)                 ▼
     Magnitud euclídea |G|             Suma de canales cuantizados
     Luminancia perceptiva                       │
                 │                               │
                 ▼                               │
  3. Detección (Umbralado)                       │
     Binarización (0 o 255)                      │
                 │                               │
                 └───────────────┬───────────────┘
                                 │
                                 ▼
                 ┌───────────────────────────────┐
                 │       COMBINACIÓN FINAL       │
                 │  Suma saturada píxel a píxel  │
                 └───────────────┬───────────────┘
                                 │
                                 ▼
                  ┌───────────────────────────────┐
                  │    Imagen Cartoon Resultante  │
                  └───────────────────────────────┘
```

---

## 📁 Estructura del Repositorio

```text
├── src/                          # Código fuente de las 4 versiones
│   ├── cartoon_seq.c             # Implementación Secuencial
│   ├── cartoon_omp.c             # Implementación OpenMP (Memoria Compartida)
│   ├── cartoon_mpi.c             # Implementación MPI (Memoria Distribuida)
│   └── cartoon_hybrid.c          # Implementación Híbrida (MPI + OpenMP)
├── include/                      # Cabeceras y bibliotecas
│   ├── cartoon_core.h            # Núcleo algorítmico y matemático común
│   ├── imagen_io.h               # Entrada/Salida de imágenes y temporizador HPC
│   ├── stb_image.h               # Decodificador de imágenes (Sean Barrett)
│   └── stb_image_write.h         # Codificador de imágenes (Sean Barrett)
├── bin/                          # Binarios ejecutables compilados
├── img/                          # Imágenes de prueba y salidas
│   ├── input/                    # Imágenes sintéticas y de prueba
│   └── output/                   # Carpeta de destino de resultados
├── results/                      # Mediciones y gráficos de desempeño
│   ├── benchmarks.csv            # Tiempos de ejecución, speedup y eficiencia
│   └── plots/                    # Gráficos comparativos generados
├── compilar_y_probar.ps1         # Script PowerShell de compilación y prueba en Windows
├── Makefile                      # Automatización de compilación (Linux / GCC / mpicc)
├── foto2.jpg                     # Fotografía real de prueba
├── foto3.jpg                     # Fotografía real de prueba
└── README.md                     # Documentación principal del proyecto
```

---

## 🚀 Compilación y Ejecución

### Opción 1: En Windows (PowerShell con MinGW y MS-MPI)

Para compilar las 4 versiones y ejecutar una prueba rápida de validación:
```powershell
.\compilar_y_probar.ps1
```

O compilar manualmente con GCC:
```powershell
# 1. Secuencial
gcc -std=c11 -O3 -Wall -Wextra -Iinclude src/cartoon_seq.c -lm -o bin/cartoon_seq.exe

# 2. OpenMP
gcc -std=c11 -O3 -Wall -Wextra -fopenmp -Iinclude src/cartoon_omp.c -lm -o bin/cartoon_omp.exe

# 3. MPI
gcc -std=c11 -O3 -Wall -Wextra -Iinclude -I"C:\tmp\toolchain\msmpi\sdk" src/cartoon_mpi.c "C:\tmp\toolchain\msmpi\runtime_1\msmpi.dll" -lm -o bin/cartoon_mpi.exe

# 4. Híbrida
gcc -std=c11 -O3 -Wall -Wextra -fopenmp -Iinclude -I"C:\tmp\toolchain\msmpi\sdk" src/cartoon_hybrid.c "C:\tmp\toolchain\msmpi\runtime_1\msmpi.dll" -lm -o bin/cartoon_hybrid.exe
```

### Opción 2: En Linux / Clúster (mediante `Makefile`)

```bash
# Compilar los cuatro objetivos
make all

# Limpiar binarios
make clean
```

---

## 💻 Parámetros de Ejecución

Todos los ejecutables reciben parámetros uniformes:

### Secuencial
```bash
./bin/cartoon_seq <imagen_entrada> <imagen_salida> <tam_filtro: 3|5> <rangos: 3|9>
# Ejemplo:
./bin/cartoon_seq foto2.jpg img/output/resultado_seq.png 3 9
```

### OpenMP (Memoria Compartida)
```bash
./bin/cartoon_omp <imagen_entrada> <imagen_salida> <tam_filtro> <rangos> [num_threads]
# Ejemplo con 8 hilos:
./bin/cartoon_omp foto2.jpg img/output/resultado_omp.png 3 9 8
```

### MPI (Memoria Distribuida)
```bash
mpiexec -n <num_procesos> ./bin/cartoon_mpi <imagen_entrada> <imagen_salida> <tam_filtro> <rangos>
# Ejemplo con 4 procesos:
mpiexec -n 4 ./bin/cartoon_mpi foto2.jpg img/output/resultado_mpi.png 3 9
```

### Híbrido (MPI + OpenMP)
```bash
mpiexec -n <num_procesos> ./bin/cartoon_hybrid <imagen_entrada> <imagen_salida> <tam_filtro> <rangos> [num_threads_por_proceso]
# Ejemplo con 2 procesos y 4 hilos por proceso (8 núcleos lógicos):
mpiexec -n 2 ./bin/cartoon_hybrid foto2.jpg img/output/resultado_hyb.png 3 9 4
```

---

## 📊 Verificación de Equivalencia Numérica

Las 4 implementaciones fueron sometidas a validación píxel a píxel contra la versión secuencial:
$$\text{MaxDiff} = \max_{x,y,c} |I_{\text{Paralela}}(x,y,c) - I_{\text{Secuencial}}(x,y,c)| = 0$$
Garantizando que las estrategias de partición por filas, intercambio de halos en MPI y barreras de sincronización en OpenMP producen resultados numéricamente exactos e idénticos.
