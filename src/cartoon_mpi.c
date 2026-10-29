/*
 * ============================================================================
 * Trabajo Practico Final Integrador - Computacion Avanzada (UADER FCyT)
 * Alumnos: Carballo Tobias, Dacuy Santino
 *
 * Inciso 2: Programa en Memoria Distribuida con MPI (cartoon_mpi.c)
 * Descripcion:
 *   Version paralela en memoria distribuida utilizando pasaje de mensajes
 *   (MPI). Emplea particionado de dominio por bloques de filas equilibrados
 *   (MPI_Scatterv / MPI_Gatherv) e intercambio de filas fantasma (halo/ghost
 *   rows) mediante MPI_Sendrecv y MPI_PROC_NULL para las etapas de convolucion
 *   (Borroneo y Operador de Sobel).
 *
 * Compilacion (Linux / WSL):
 *   mpicc -std=c11 -O3 -Wall -Wextra -Iinclude src/cartoon_mpi.c -lm -o bin/cartoon_mpi
 *
 * Ejecucion:
 *   mpiexec -n 4 ./bin/cartoon_mpi <entrada> <salida> <filtro: 3|5> <rangos: 3|9> [umbral]
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <mpi.h>

#include "imagen_io.h"

/* ----------------------------------------------------------------------------
 * Intercambio de filas fantasma (Halo Exchange) entre procesos vecinos.
 *
 * Estructura del buffer local (m filas propias + 2 * radio filas fantasma):
 *   Filas [0 .. radio - 1]           : Halo superior (vecino 'arriba')
 *   Filas [radio .. radio + m - 1]   : 'm' filas propias del proceso
 *   Filas [radio + m .. 2*radio+m-1] : Halo inferior (vecino 'abajo')
 *
 * Al usar MPI_PROC_NULL en los extremos (rank 0 hacia arriba, rank P-1 hacia
 * abajo), MPI no modifica las filas fantasma exteriores, las cuales permanecen
 * en 0 (negro), cumpliendo la condicion de borde neutro negro.
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

    uint8_t *halo_sup      = buf_local;
    uint8_t *propias_sup   = buf_local + (size_t)radio * bytes_por_fila;
    uint8_t *propias_inf   = buf_local + (size_t)m * bytes_por_fila;
    uint8_t *halo_inf      = buf_local + (size_t)(radio + m) * bytes_por_fila;

    /* 1. Enviar primeras 'radio' filas propias hacia arriba y recibir halo inferior desde abajo */
    MPI_Sendrecv(
        propias_sup, bytes_halo, MPI_UNSIGNED_CHAR, arriba, 10,
        halo_inf,    bytes_halo, MPI_UNSIGNED_CHAR, abajo,  10,
        MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    /* 2. Enviar ultimas 'radio' filas propias hacia abajo y recibir halo superior desde arriba */
    MPI_Sendrecv(
        propias_inf, bytes_halo, MPI_UNSIGNED_CHAR, abajo,  20,
        halo_sup,    bytes_halo, MPI_UNSIGNED_CHAR, arriba, 20,
        MPI_COMM_WORLD, MPI_STATUS_IGNORE);
}

int main(int argc, char **argv)
{
    int rank = 0, p = 1;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &p);

    int valido = 1;
    int tam_filtro = 3;
    int rangos_post = 3;
    int umbral = UMBRAL_BORDE_DEF;
    int ancho = 0, alto = 0;
    uint8_t *img_orig = NULL;
    uint8_t *img_cartoon = NULL;

    if (rank == 0) {
        if (argc < 5) {
            fprintf(stderr,
                    "Uso: mpiexec -n <P> %s <entrada> <salida> <filtro: 3|5> <rangos: 3|9> [umbral]\n",
                    argv[0]);
            valido = 0;
        } else {
            tam_filtro  = atoi(argv[3]);
            rangos_post = atoi(argv[4]);
            if (argc >= 6) umbral = atoi(argv[5]);

            if (!validar_parametros_algoritmo(tam_filtro, rangos_post)) {
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

    /* Difusion de estado de validacion y parametros de configuracion */
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
    MPI_Bcast(&umbral,      1, MPI_INT, 0, MPI_COMM_WORLD);

    /* Calculo del reparto equilibrado de filas (igual que en TP3 Ej. 4 y 5) */
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

    /* Reserva de buffers locales con espacio para 2 * radio filas fantasma en 0 (negro) */
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

    /* Sincronizacion antes de iniciar el cronometraje paralelo */
    MPI_Barrier(MPI_COMM_WORLD);
    double t_inicio_total = MPI_Wtime();

    /* 1. Distribucion de bloques de filas desde la raiz hacia el interior local [radio .. radio+m-1] */
    double t_comm0 = MPI_Wtime();
    MPI_Scatterv(
        img_orig, cant, desp, MPI_UNSIGNED_CHAR,
        local_orig + (size_t)radio * bytes_por_fila, cant[rank], MPI_UNSIGNED_CHAR,
        0, MPI_COMM_WORLD);

    /* Intercambio de filas fantasma de la imagen original antes del Borroneo */
    intercambiar_filas_fantasma(local_orig, m, bytes_por_fila, radio, arriba, abajo);
    double t_comm1 = MPI_Wtime();

    /* 2. Etapa 1: Filtrado / Borroneo local sobre las filas [radio .. radio + m) */
    double t_comp0 = MPI_Wtime();
    paso1_filtrado_borroneo_rango(
        local_orig, local_blur, ancho,
        radio, radio + m, 0, filas_locales_totales, tam_filtro);
    double t_comp1 = MPI_Wtime();

    /* 3. Intercambio de filas fantasma de la imagen borroneada antes de Sobel */
    double t_comm2 = MPI_Wtime();
    intercambiar_filas_fantasma(local_blur, m, bytes_por_fila, radio, arriba, abajo);
    double t_comm3 = MPI_Wtime();

    /* 4. Etapa 2 y 3: Resaltado (Sobel) + Deteccion (Umbralado) + Posterizado + Suma */
    double t_comp2 = MPI_Wtime();
    paso2y3_resaltado_y_deteccion_rango(
        local_blur, local_bordes, ancho,
        radio, radio + m, 0, filas_locales_totales, tam_filtro, umbral);

    rama2_posterizado_rango(
        local_orig, local_post, ancho,
        radio, radio + m, rangos_post);

    combinar_bordes_y_posterizado_rango(
        local_bordes, local_post, local_cartoon,
        ancho, radio, radio + m);
    double t_comp3 = MPI_Wtime();

    /* 5. Recoleccion de las sub-imagenes Cartoon en el proceso raiz (rank 0) */
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
        printf(" [MPI] Generacion de Cartoon completada (%d procesos)\n", p);
        printf("================================================================\n");
        printf(" Imagen        : %s (%d x %d px)\n", argv[1], ancho, alto);
        printf(" Configuracion : Filtro %dx%d | Posterizado %d rangos | Procesos=%d\n",
               tam_filtro, tam_filtro, rangos_post, p);
        printf(" Tiempos (s)   : Computo=%.6f | Comunicacion=%.6f\n",
               t_comp_max, t_comm_max);
        printf(" Tiempo Total  : %.6f segundos\n", t_total_max);
        printf("CSV_METRIC,MPI,%d,1,%dx%d,%dx%d,%d,%.6f,%.6f\n",
               p, ancho, alto, tam_filtro, tam_filtro, rangos_post, t_total_max, t_comp_max);
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
