/*
 * ============================================================================
 * Trabajo Practico Final Integrador - Computacion Avanzada (UADER FCyT)
 * Alumnos: Carballo Tobias, Dacuy Santino
 *
 * Inciso 3: Programa en Memoria Compartida con OpenMP (cartoon_omp.c)
 * Descripcion:
 *   Version paralela en memoria compartida utilizando Threads (OpenMP).
 *   Permite configurar el numero de threads y el esquema de planificacion.
 *
 *   Analisis de dependencias de datos (segun TP4):
 *     - Paso 1 (Filtrado): Lectura compartida sobre img_orig (sin carrera) y
 *       escritura disjunta por fila en img_blur. Requiere barrera al finalizar
 *       porque el Paso 2 lee vecindades [y-R, y+R] de img_blur (dependencia
 *       verdadera Read-After-Write entre etapas).
 *     - Paso 2 y 3 (Resaltado Sobel + Umbralado): Lectura compartida de
 *       img_blur y escritura disjunta en img_bordes.
 *     - Rama 2 (Posterizado): Independiente de la Rama 1; transforma cada
 *       pixel aisladamente (operacion raster punto a punto).
 *     - Combinacion Final: Suma pixel a pixel de img_bordes + img_post.
 *
 * Compilacion:
 *   gcc -std=c11 -O3 -Wall -Wextra -fopenmp -Iinclude src/cartoon_omp.c -lm -o bin/cartoon_omp
 *
 * Uso:
 *   ./bin/cartoon_omp <entrada> <salida> <filtro: 3|5> <rangos: 3|9> <num_threads> [umbral]
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <omp.h>

#include "imagen_io.h"

static void ejecutar_pipeline_openmp(
    const uint8_t *img_orig,
    uint8_t *img_blur,
    uint8_t *img_bordes,
    uint8_t *img_post,
    uint8_t *img_cartoon,
    int ancho,
    int alto,
    int tam_filtro,
    int rangos_post,
    int umbral,
    int num_threads,
    double *t_filtrado,
    double *t_bordes,
    double *t_post,
    double *t_suma)
{
    omp_set_dynamic(0);
    omp_set_num_threads(num_threads);

    /* Precalentamiento del equipo de threads OpenMP para no medir el costo de
       creacion inicial del thread pool del sistema operativo */
    #pragma omp parallel
    {
        (void)omp_get_thread_num();
    }

    double t0 = obtener_tiempo_seg();

    /* ------------------------------------------------------------------------
     * Etapa 1: Filtrado / Borroneo (Convolucion KxK)
     * Se reparte el bucle externo de filas entre el equipo de threads.
     * La barrera implicita al final de '#pragma omp for' garantiza que todo
     * img_blur este calculado antes de que el Paso 2 lea filas vecinas.
     * ------------------------------------------------------------------------ */
    #pragma omp parallel for default(none) \
        shared(img_orig, img_blur, ancho, alto, tam_filtro) \
        schedule(static)
    for (int y = 0; y < alto; y++) {
        paso1_filtrado_borroneo_rango(
            img_orig, img_blur, ancho,
            y, y + 1, 0, alto, tam_filtro);
    }
    double t1 = obtener_tiempo_seg();

    /* ------------------------------------------------------------------------
     * Etapa 2: Resaltado (Sobel KxK) + Deteccion (Umbralado)
     * ------------------------------------------------------------------------ */
    #pragma omp parallel for default(none) \
        shared(img_blur, img_bordes, ancho, alto, tam_filtro, umbral) \
        schedule(static)
    for (int y = 0; y < alto; y++) {
        paso2y3_resaltado_y_deteccion_rango(
            img_blur, img_bordes, ancho,
            y, y + 1, 0, alto, tam_filtro, umbral);
    }
    double t2 = obtener_tiempo_seg();

    /* ------------------------------------------------------------------------
     * Etapa 3: Posterizado por canal RGB y suma en una sola imagen
     * Precalculamos la LUT de cuantizacion una sola vez y paralelizamos las filas
     * ------------------------------------------------------------------------ */
    uint8_t lut[256];
    for (int z = 0; z < 256; z++) {
        lut[z] = cuantizar_nivel_posterizado((uint8_t)z, rangos_post);
    }

    #pragma omp parallel for default(none) \
        shared(img_orig, img_post, lut, ancho, alto) \
        schedule(static)
    for (int y = 0; y < alto; y++) {
        for (int x = 0; x < ancho; x++) {
            size_t idx = ((size_t)y * ancho + x) * CANALES_RGB;
            img_post[idx + 0] = lut[img_orig[idx + 0]];
            img_post[idx + 1] = lut[img_orig[idx + 1]];
            img_post[idx + 2] = lut[img_orig[idx + 2]];
        }
    }
    double t3 = obtener_tiempo_seg();

    /* ------------------------------------------------------------------------
     * Etapa 4: Combinacion Final (Suma saturada de Bordes + Posterizado)
     * ------------------------------------------------------------------------ */
    #pragma omp parallel for default(none) \
        shared(img_bordes, img_post, img_cartoon, ancho, alto) \
        schedule(static)
    for (int y = 0; y < alto; y++) {
        combinar_bordes_y_posterizado_rango(
            img_bordes, img_post, img_cartoon,
            ancho, y, y + 1);
    }
    double t4 = obtener_tiempo_seg();

    *t_filtrado = t1 - t0;
    *t_bordes   = t2 - t1;
    *t_post     = t3 - t2;
    *t_suma     = t4 - t3;
}

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr,
                "Uso: %s <imagen_entrada> <imagen_salida> <filtro: 3|5> <rangos: 3|9> <num_threads> [umbral]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    const char *ruta_entrada = argv[1];
    const char *ruta_salida  = argv[2];
    int tam_filtro           = atoi(argv[3]);
    int rangos_post          = atoi(argv[4]);
    int num_threads          = atoi(argv[5]);
    int umbral               = (argc >= 7) ? atoi(argv[6]) : UMBRAL_BORDE_DEF;

    if (!validar_parametros_algoritmo(tam_filtro, rangos_post) || num_threads < 1) {
        fprintf(stderr, "[ERROR] Parametros invalidos (num_threads debe ser >= 1).\n");
        return EXIT_FAILURE;
    }

    int ancho = 0, alto = 0;
    uint8_t *img_orig = cargar_imagen_rgb(ruta_entrada, &ancho, &alto);
    if (!img_orig) {
        return EXIT_FAILURE;
    }

    size_t total_bytes = (size_t)ancho * (size_t)alto * CANALES_RGB;
    uint8_t *img_blur    = (uint8_t *)reservar_memoria(total_bytes, sizeof(uint8_t));
    uint8_t *img_bordes  = (uint8_t *)reservar_memoria(total_bytes, sizeof(uint8_t));
    uint8_t *img_post    = (uint8_t *)reservar_memoria(total_bytes, sizeof(uint8_t));
    uint8_t *img_cartoon = (uint8_t *)reservar_memoria(total_bytes, sizeof(uint8_t));

    double t_filtrado = 0.0, t_bordes = 0.0, t_post = 0.0, t_suma = 0.0;

    ejecutar_pipeline_openmp(
        img_orig, img_blur, img_bordes, img_post, img_cartoon,
        ancho, alto, tam_filtro, rangos_post, umbral, num_threads,
        &t_filtrado, &t_bordes, &t_post, &t_suma);

    double t_total = t_filtrado + t_bordes + t_post + t_suma;

    if (!guardar_imagen_rgb(ruta_salida, img_cartoon, ancho, alto)) {
        stbi_image_free(img_orig);
        free(img_blur);
        free(img_bordes);
        free(img_post);
        free(img_cartoon);
        return EXIT_FAILURE;
    }

    printf("================================================================\n");
    printf(" [OPENMP] Generacion de Cartoon completada (%d threads)\n", num_threads);
    printf("================================================================\n");
    printf(" Imagen        : %s (%d x %d px)\n", ruta_entrada, ancho, alto);
    printf(" Configuracion : Filtro %dx%d | Posterizado %d rangos | Threads=%d\n",
           tam_filtro, tam_filtro, rangos_post, num_threads);
    printf(" Tiempos (s)   : Filtrado=%.6f | Bordes=%.6f | Post=%.6f | Suma=%.6f\n",
           t_filtrado, t_bordes, t_post, t_suma);
    printf(" Tiempo Total  : %.6f segundos\n", t_total);
    printf("CSV_METRIC,OMP,1,%d,%dx%d,%dx%d,%d,%.6f,%.6f\n",
           num_threads, ancho, alto, tam_filtro, tam_filtro, rangos_post, t_total, t_total);

    stbi_image_free(img_orig);
    free(img_blur);
    free(img_bordes);
    free(img_post);
    free(img_cartoon);

    return EXIT_SUCCESS;
}
