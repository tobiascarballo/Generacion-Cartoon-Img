/*
 * ============================================================================
 * Trabajo Practico Final Integrador - Computacion Avanzada (UADER FCyT)
 * Alumnos: Carballo Tobias, Dacuy Santino
 *
 * Archivo: src/cartoon_hybrid.c
 * Inciso 4: Programa Paralelo Hibrido (MPI + OpenMP)
 *
 * ¿Cual es su objetivo?
 *   Combina dos niveles jerarquicos de paralelismo en una misma aplicacion:
 *     1. Nivel inter-proceso (grano grueso): MPI distribuye bloques de filas
 *        entre procesos independientes y gestiona el intercambio de fronteras
 *        (filas fantasma) mediante paso de mensajes.
 *     2. Nivel intra-proceso (grano fino): OpenMP despliega un equipo de 'T'
 *        hilos dentro de cada proceso MPI para paralelizar el calculo sobre
 *        las filas locales asignadas.
 *
 * Conceptos tecnicos y de arquitectura implementados:
 *   - Modelo MPI_THREAD_FUNNELED:
 *     Se inicializa con 'MPI_Init_thread(..., MPI_THREAD_FUNNELED, ...)'.
 *     Indica que la aplicacion es multihilo, pero SOLO el hilo maestro de cada
 *     proceso realiza las llamadas a la API de MPI, siempre fuera de las
 *     regiones paralelas de OpenMP. Esto evita sobrecargas de contencion y
 *     bloqueos internos en el driver de comunicaciones de red.
 *   - Fusion de bucles (Loop Fusion) para localidad de cache:
 *     Despues de intercambiar los halos de la imagen borroneada, un unico
 *     bloque '#pragma omp parallel for schedule(static)' ejecuta para cada
 *     fila 'y' el calculo de Sobel+Umbral, el Posterizado RGB y la Suma final
 *     en una sola pasada continua. La fila se mantiene caliente en las caches
 *     L1 y L2 de cada nucleo, reduciendo accesos costosos a la memoria RAM.
 *
 * Compilacion manual:
 *   gcc -std=c11 -O3 -Wall -Wextra -fopenmp -Iinclude src/cartoon_hybrid.c -lmsmpi -o bin/cartoon_hybrid.exe
 *
 * Ejecucion:
 *   mpiexec -n 2 ./bin/cartoon_hybrid.exe foto2.jpg img/output/out_hyb.png 5 9 4 75
 * ============================================================================
 */

#include <stdio.h>    /* Entrada y salida estandar: printf, fprintf, stderr */
#include <stdlib.h>   /* Utilidades de memoria y conversion (atoi, free) */
#include <stdint.h>   /* Enteros de ancho fijo: uint8_t */
#include <string.h>   /* Manejo de cadenas */
#include <mpi.h>      /* API de Memoria Distribuida (MPI) */
#include <omp.h>      /* API de Memoria Compartida (OpenMP) */

#include "imagen_io.h" /* Funciones compartidas de entrada/salida de imagenes */

/* ----------------------------------------------------------------------------
 * FUNCION: intercambiar_filas_fantasma
 * Realiza el intercambio de bordes entre procesos MPI vecinos usando MPI_Sendrecv
 * ---------------------------------------------------------------------------- */
static void intercambiar_filas_fantasma(
    uint8_t *buf_local,
    int m,
    int bytes_por_fila,
    int radio,
    int arriba,
    int abajo)
{
    int bytes_halo = radio * bytes_por_fila;

    uint8_t *halo_sup    = buf_local;
    uint8_t *propias_sup = buf_local + (size_t)radio * bytes_por_fila;
    uint8_t *propias_inf = buf_local + (size_t)m * bytes_por_fila;
    uint8_t *halo_inf    = buf_local + (size_t)(radio + m) * bytes_por_fila;

    /* Envio hacia arriba y recepcion desde abajo */
    MPI_Sendrecv(
        propias_sup, bytes_halo, MPI_UNSIGNED_CHAR, arriba, 10,
        halo_inf,    bytes_halo, MPI_UNSIGNED_CHAR, abajo,  10,
        MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    /* Envio hacia abajo y recepcion desde arriba */
    MPI_Sendrecv(
        propias_inf, bytes_halo, MPI_UNSIGNED_CHAR, abajo,  20,
        halo_sup,    bytes_halo, MPI_UNSIGNED_CHAR, arriba, 20,
        MPI_COMM_WORLD, MPI_STATUS_IGNORE);
}

int main(int argc, char **argv)
{
    /* ------------------------------------------------------------------------
     * 1. INICIALIZACION HIBRIDA: MPI CON SOPORTE DE HILOS (MPI_THREAD_FUNNELED)
     * ------------------------------------------------------------------------ */
    int rank = 0;              /* ID de este proceso MPI */
    int p = 1;                 /* Cantidad total de procesos MPI */
    int nivel_provisto = 0;    /* Nivel de soporte de threads que confirmo el runtime MPI */

    /* Inicializa MPI solicitando soporte para programas multihilo */
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &nivel_provisto);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &p);

    int valido = 1;
    int tam_filtro  = 3;
    int rangos_post = 3;
    int num_threads = 2;       /* Cantidad de hilos OpenMP por cada proceso MPI */
    int umbral      = UMBRAL_BORDE_DEF;
    int ancho = 0, alto = 0;
    uint8_t *img_orig = NULL;
    uint8_t *img_cartoon = NULL;

    /* ------------------------------------------------------------------------
     * 2. LECTURA Y VALIDACION EN EL PROCESO RAIZ (RANK 0)
     * ------------------------------------------------------------------------ */
    if (rank == 0) {
        if (argc < 6) {
            fprintf(stderr,
                    "Uso: mpiexec -n <P> %s <entrada> <salida> <filtro: 3|5> <rangos: 3|9> <num_threads> [umbral]\n",
                    argv[0]);
            valido = 0;
        } else {
            tam_filtro  = atoi(argv[3]);
            rangos_post = atoi(argv[4]);
            num_threads = atoi(argv[5]);
            if (argc >= 7) umbral = atoi(argv[6]);

            if (!validar_parametros_algoritmo(tam_filtro, rangos_post) || num_threads < 1) {
                valido = 0;
            } else {
                img_orig = cargar_imagen_rgb(argv[1], &ancho, &alto);
                if (!img_orig) {
                    valido = 0;
                } else if (p > alto) {
                    fprintf(stderr, "[ERROR] Procesos (%d) superan alto de imagen (%d).\n", p, alto);
                    valido = 0;
                }
            }
        }
    }

    /* ------------------------------------------------------------------------
     * 3. DIFUSION COLECTIVA DE PARAMETROS A TODOS LOS PROCESOS (MPI_Bcast)
     * ------------------------------------------------------------------------ */
    MPI_Bcast(&valido,      1, MPI_INT, 0, MPI_COMM_WORLD);
    if (!valido) {
        if (img_orig) stbi_image_free(img_orig);
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    MPI_Bcast(&ancho,       1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&alto,        1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&tam_filtro,  1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&rangos_post, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&num_threads, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&umbral,      1, MPI_INT, 0, MPI_COMM_WORLD);

    /* ------------------------------------------------------------------------
     * 4. CONFIGURACION DEL ENTORNO OPENMP DENTRO DE CADA PROCESO
     * ------------------------------------------------------------------------ */
    omp_set_dynamic(0);                 /* Fija la cantidad exacta de threads */
    omp_set_num_threads(num_threads);   /* Asigna T hilos por proceso (Workers = P * T) */

    int bytes_por_fila = ancho * CANALES_RGB;
    int radio = tam_filtro / 2;

    /* Arreglos de conteos y desplazamientos para Scatterv y Gatherv */
    int *filas  = (int *)reservar_memoria(p, sizeof(int));
    int *inicio = (int *)reservar_memoria(p, sizeof(int));
    int *cant   = (int *)reservar_memoria(p, sizeof(int));
    int *desp   = (int *)reservar_memoria(p, sizeof(int));

    int offset_filas = 0;
    for (int r = 0; r < p; r++) {
        filas[r]  = (alto / p) + (r < (alto % p));
        inicio[r] = offset_filas;
        cant[r]   = filas[r] * bytes_por_fila;
        desp[r]   = offset_filas * bytes_por_fila;
        offset_filas += filas[r];
    }

    int m = filas[rank];                                 /* Filas propias de este rango */
    int filas_locales_totales = m + 2 * radio;           /* Incluye 2 halos */
    size_t bytes_buffer_local = (size_t)filas_locales_totales * (size_t)bytes_por_fila;

    /* Buffers locales en memoria propia de cada proceso MPI */
    uint8_t *local_orig    = (uint8_t *)reservar_memoria(bytes_buffer_local, sizeof(uint8_t));
    uint8_t *local_blur    = (uint8_t *)reservar_memoria(bytes_buffer_local, sizeof(uint8_t));
    uint8_t *local_bordes  = (uint8_t *)reservar_memoria(bytes_buffer_local, sizeof(uint8_t));
    uint8_t *local_post    = (uint8_t *)reservar_memoria(bytes_buffer_local, sizeof(uint8_t));
    uint8_t *local_cartoon = (uint8_t *)reservar_memoria(bytes_buffer_local, sizeof(uint8_t));

    if (rank == 0) {
        img_cartoon = (uint8_t *)reservar_memoria((size_t)alto * bytes_por_fila, sizeof(uint8_t));
    }

    int arriba = (rank > 0)     ? (rank - 1) : MPI_PROC_NULL;
    int abajo  = (rank < p - 1) ? (rank + 1) : MPI_PROC_NULL;

    /* Precalculo de la tabla LUT de posterizado (de solo lectura para los hilos) */
    uint8_t lut[256];
    for (int z = 0; z < 256; z++) {
        lut[z] = cuantizar_nivel_posterizado((uint8_t)z, rangos_post);
    }

    /* Precalentamiento del equipo de hilos OpenMP en cada proceso MPI */
    #pragma omp parallel
    {
        (void)omp_get_thread_num();
    }

    MPI_Barrier(MPI_COMM_WORLD);
    double t_inicio_total = MPI_Wtime();

    /* ------------------------------------------------------------------------
     * 5. DISTRIBUCION MPI DE FILAS E INTERCAMBIO INICIAL DE HALOS
     * ------------------------------------------------------------------------ */
    double t_comm0 = MPI_Wtime();
    MPI_Scatterv(
        img_orig, cant, desp, MPI_UNSIGNED_CHAR,
        local_orig + (size_t)radio * bytes_por_fila, cant[rank], MPI_UNSIGNED_CHAR,
        0, MPI_COMM_WORLD);

    /* 1er Intercambio MPI de filas fantasma (sobre imagen original) */
    intercambiar_filas_fantasma(local_orig, m, bytes_por_fila, radio, arriba, abajo);
    double t_comm1 = MPI_Wtime();

    /* ------------------------------------------------------------------------
     * 6. ETAPA 1: FILTRADO / BORRONEO EN PARALELO CON OPENMP
     * ------------------------------------------------------------------------
     * Los 'num_threads' hilos del proceso se reparten las 'm' filas locales.
     */
    double t_comp0 = MPI_Wtime();
    #pragma omp parallel for default(none) \
        shared(local_orig, local_blur, ancho, radio, m, filas_locales_totales, tam_filtro) \
        schedule(static)
    for (int y = radio; y < radio + m; y++) {
        paso1_filtrado_borroneo_rango(
            local_orig, local_blur, ancho,
            y, y + 1, 0, filas_locales_totales, tam_filtro);
    }
    double t_comp1 = MPI_Wtime();

    /* ------------------------------------------------------------------------
     * 7. 2do INTERCAMBIO MPI DE FILAS FANTASMA (Sobre imagen borroneada)
     * ------------------------------------------------------------------------
     * Solo el hilo maestro llama a MPI mientras los demas hilos esperan.
     */
    double t_comm2 = MPI_Wtime();
    intercambiar_filas_fantasma(local_blur, m, bytes_por_fila, radio, arriba, abajo);
    double t_comm3 = MPI_Wtime();

    /* ------------------------------------------------------------------------
     * 8. OPTIMIZACION DE FUSION DE BUCLES (Loop Fusion con OpenMP)
     * ------------------------------------------------------------------------
     * En lugar de hacer 3 bucles separados (Sobel, luego Posterizado, luego Suma),
     * fusionamos las 3 operaciones en una sola pasada para cada fila 'y':
     *   1. Calcula Sobel + Umbral de la fila 'y'.
     *   2. Aplica posterizado LUT sobre la fila 'y'.
     *   3. Suma ambos resultados en la fila 'y' del buffer Cartoon.
     * Resultado: la fila permanece en la cache L1/L2 del nucleo y no viaja a RAM.
     * ------------------------------------------------------------------------ */
    double t_comp2 = MPI_Wtime();
    #pragma omp parallel for default(none) \
        shared(local_orig, local_blur, local_bordes, local_post, local_cartoon, \
               lut, ancho, radio, m, filas_locales_totales, tam_filtro, umbral) \
        schedule(static)
    for (int y = radio; y < radio + m; y++) {
        /* Paso 2 y 3: Resaltado con Sobel y Umbralado */
        paso2y3_resaltado_y_deteccion_rango(
            local_blur, local_bordes, ancho,
            y, y + 1, 0, filas_locales_totales, tam_filtro, umbral);

        /* Rama 2: Posterizado de la fila 'y' */
        for (int x = 0; x < ancho; x++) {
            size_t idx = ((size_t)y * ancho + x) * CANALES_RGB;
            local_post[idx + 0] = lut[local_orig[idx + 0]];
            local_post[idx + 1] = lut[local_orig[idx + 1]];
            local_post[idx + 2] = lut[local_orig[idx + 2]];
        }

        /* Combinacion Final: Suma saturada de la fila 'y' */
        combinar_bordes_y_posterizado_rango(
            local_bordes, local_post, local_cartoon,
            ancho, y, y + 1);
    }
    double t_comp3 = MPI_Wtime();

    /* ------------------------------------------------------------------------
     * 9. RECOLECCION MPI DE RESULTADOS EN EL PROCESO RAIZ (MPI_Gatherv)
     * ------------------------------------------------------------------------ */
    double t_comm4 = MPI_Wtime();
    MPI_Gatherv(
        local_cartoon + (size_t)radio * bytes_por_fila, cant[rank], MPI_UNSIGNED_CHAR,
        img_cartoon, cant, desp, MPI_UNSIGNED_CHAR,
        0, MPI_COMM_WORLD);
    double t_fin_total = MPI_Wtime();

    double t_comm_local  = (t_comm1 - t_comm0) + (t_comm3 - t_comm2) + (t_fin_total - t_comm4);
    double t_comp_local  = (t_comp1 - t_comp0) + (t_comp3 - t_comp2);
    double t_total_local = t_fin_total - t_inicio_total;

    /* Reduccion del tiempo maximo entre todos los procesos */
    double t_comm_max = 0.0, t_comp_max = 0.0, t_total_max = 0.0;
    MPI_Reduce(&t_comm_local,  &t_comm_max,  1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&t_comp_local,  &t_comp_max,  1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&t_total_local, &t_total_max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    /* ------------------------------------------------------------------------
     * 10. ESCRITURA DE LA IMAGEN EN DISCO E INFORME DE METRICAS
     * ------------------------------------------------------------------------ */
    if (rank == 0) {
        guardar_imagen_rgb(argv[2], img_cartoon, ancho, alto);

        printf("================================================================\n");
        printf(" [HIBRIDO MPI+OpenMP] Cartoon completado (%d proc x %d threads = %d)\n",
               p, num_threads, p * num_threads);
        printf("================================================================\n");
        printf(" Imagen        : %s (%d x %d px)\n", argv[1], ancho, alto);
        printf(" Configuracion : Filtro %dx%d | Posterizado %d rangos | P=%d x T=%d\n",
               tam_filtro, tam_filtro, rangos_post, p, num_threads);
        printf(" Tiempos (s)   : Computo=%.6f | Comunicacion=%.6f\n",
               t_comp_max, t_comm_max);
        printf(" Tiempo Total  : %.6f segundos\n", t_total_max);
        printf("CSV_METRIC,HYBRID,%d,%d,%dx%d,%dx%d,%d,%.6f,%.6f\n",
               p, num_threads, ancho, alto, tam_filtro, tam_filtro, rangos_post,
               t_total_max, t_comp_max);
    }

    /* ------------------------------------------------------------------------
     * 11. LIBERACION DE MEMORIA Y CIERRE DE MPI
     * ------------------------------------------------------------------------ */
    if (img_orig) stbi_image_free(img_orig);
    free(img_cartoon);
    free(local_orig);
    free(local_blur);
    free(local_bordes);
    free(local_post);
    free(local_cartoon);
    free(filas);
    free(inicio);
    free(cant);
    free(desp);

    MPI_Finalize();
    return EXIT_SUCCESS;
}
