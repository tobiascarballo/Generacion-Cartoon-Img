/*
 * ============================================================================
 * Trabajo Practico Final Integrador - Computacion Avanzada (UADER FCyT)
 * Alumnos: Carballo Tobias, Dacuy Santino
 *
 * Inciso 4: Programa Paralelo Hibrido MPI + OpenMP (cartoon_hybrid.c)
 * Descripcion:
 *   Implementacion paralela hibrida combinando pasaje de mensajes (MPI) para
 *   la descomposicion de dominio de grano grueso entre procesos (bloques de
 *   filas + intercambio de filas fantasma) y memoria compartida (OpenMP) para
 *   la paralelizacion de grano fino de los bucles de calculo dentro de cada
 *   proceso MPI.
 *
 *   Modelo de soporte de hilos MPI: MPI_THREAD_FUNNELED (las llamadas a MPI
 *   las realiza el hilo principal de cada proceso fuera de las regiones
 *   paralelas de OpenMP).
 *
 * Compilacion (Linux / WSL):
 *   mpicc -std=c11 -O3 -Wall -Wextra -fopenmp -Iinclude src/cartoon_hybrid.c -lm -o bin/cartoon_hybrid
 *
 * Ejecucion:
 *   mpiexec -n <P> ./bin/cartoon_hybrid <entrada> <salida> <filtro: 3|5> <rangos: 3|9> <num_threads> [umbral]
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <mpi.h>
#include <omp.h>

#include "imagen_io.h"

/* ----------------------------------------------------------------------------
 * Intercambio de filas fantasma (Halo Exchange) entre procesos vecinos MPI
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

    MPI_Sendrecv(
        propias_sup, bytes_halo, MPI_UNSIGNED_CHAR, arriba, 10,
        halo_inf,    bytes_halo, MPI_UNSIGNED_CHAR, abajo,  10,
        MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    MPI_Sendrecv(
        propias_inf, bytes_halo, MPI_UNSIGNED_CHAR, abajo,  20,
        halo_sup,    bytes_halo, MPI_UNSIGNED_CHAR, arriba, 20,
        MPI_COMM_WORLD, MPI_STATUS_IGNORE);
}

int main(int argc, char **argv)
{
    int rank = 0, p = 1, nivel_provisto = 0;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &nivel_provisto);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &p);

    int valido = 1;
    int tam_filtro  = 3;
    int rangos_post = 3;
    int num_threads = 2;
    int umbral      = UMBRAL_BORDE_DEF;
    int ancho = 0, alto = 0;
    uint8_t *img_orig = NULL;
    uint8_t *img_cartoon = NULL;

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
                    fprintf(stderr, "[ERROR] La cantidad de procesos (%d) supera el alto de la imagen (%d).\n",
                            p, alto);
                    valido = 0;
                }
            }
        }
    }

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

    /* Configuracion del equipo de threads OpenMP dentro de cada proceso MPI */
    omp_set_dynamic(0);
    omp_set_num_threads(num_threads);

    int bytes_por_fila = ancho * CANALES_RGB;
    int radio = tam_filtro / 2;

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

    int m = filas[rank];
    int filas_locales_totales = m + 2 * radio;
    size_t bytes_buffer_local = (size_t)filas_locales_totales * (size_t)bytes_por_fila;

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

    /* Precalculo de LUT de posterizado */
    uint8_t lut[256];
    for (int z = 0; z < 256; z++) {
        lut[z] = cuantizar_nivel_posterizado((uint8_t)z, rangos_post);
    }

    /* Precalentamiento del equipo de threads OpenMP en cada proceso MPI */
    #pragma omp parallel
    {
        (void)omp_get_thread_num();
    }

    MPI_Barrier(MPI_COMM_WORLD);
    double t_inicio_total = MPI_Wtime();

    /* 1. Distribucion MPI de sub-matrices e intercambio de filas fantasma iniciales */
    double t_comm0 = MPI_Wtime();
    MPI_Scatterv(
        img_orig, cant, desp, MPI_UNSIGNED_CHAR,
        local_orig + (size_t)radio * bytes_por_fila, cant[rank], MPI_UNSIGNED_CHAR,
        0, MPI_COMM_WORLD);

    intercambiar_filas_fantasma(local_orig, m, bytes_por_fila, radio, arriba, abajo);
    double t_comm1 = MPI_Wtime();

    /* 2. Etapa 1 (OpenMP dentro de cada proceso MPI): Filtrado / Borroneo */
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

    /* 3. Intercambio MPI de filas fantasma de la imagen borroneada */
    double t_comm2 = MPI_Wtime();
    intercambiar_filas_fantasma(local_blur, m, bytes_por_fila, radio, arriba, abajo);
    double t_comm3 = MPI_Wtime();

    /* 4. Etapas 2, 3 y 4 (OpenMP dentro de cada proceso MPI):
          Resaltado Sobel + Umbralado + Posterizado + Suma en una sola pasada por fila */
    double t_comp2 = MPI_Wtime();
    #pragma omp parallel for default(none) \
        shared(local_orig, local_blur, local_bordes, local_post, local_cartoon, \
               lut, ancho, radio, m, filas_locales_totales, tam_filtro, umbral) \
        schedule(static)
    for (int y = radio; y < radio + m; y++) {
        /* Rama 1 (Pasos 2 y 3): Resaltado Sobel + Deteccion por Umbralado */
        paso2y3_resaltado_y_deteccion_rango(
            local_blur, local_bordes, ancho,
            y, y + 1, 0, filas_locales_totales, tam_filtro, umbral);

        /* Rama 2: Posterizado por canal RGB */
        for (int x = 0; x < ancho; x++) {
            size_t idx = ((size_t)y * ancho + x) * CANALES_RGB;
            local_post[idx + 0] = lut[local_orig[idx + 0]];
            local_post[idx + 1] = lut[local_orig[idx + 1]];
            local_post[idx + 2] = lut[local_orig[idx + 2]];
        }

        /* Combinacion Final: Suma de Bordes + Posterizado */
        combinar_bordes_y_posterizado_rango(
            local_bordes, local_post, local_cartoon,
            ancho, y, y + 1);
    }
    double t_comp3 = MPI_Wtime();

    /* 5. Recoleccion MPI de la imagen Cartoon final en rank 0 */
    double t_comm4 = MPI_Wtime();
    MPI_Gatherv(
        local_cartoon + (size_t)radio * bytes_por_fila, cant[rank], MPI_UNSIGNED_CHAR,
        img_cartoon, cant, desp, MPI_UNSIGNED_CHAR,
        0, MPI_COMM_WORLD);
    double t_fin_total = MPI_Wtime();

    double t_comm_local  = (t_comm1 - t_comm0) + (t_comm3 - t_comm2) + (t_fin_total - t_comm4);
    double t_comp_local  = (t_comp1 - t_comp0) + (t_comp3 - t_comp2);
    double t_total_local = t_fin_total - t_inicio_total;

    double t_comm_max = 0.0, t_comp_max = 0.0, t_total_max = 0.0;
    MPI_Reduce(&t_comm_local,  &t_comm_max,  1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&t_comp_local,  &t_comp_max,  1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&t_total_local, &t_total_max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

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
