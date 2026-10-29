/*
 * ============================================================================
 * Trabajo Practico Final Integrador - Computacion Avanzada (UADER FCyT)
 * Alumnos: Carballo Tobias, Dacuy Santino
 *
 * Inciso 1: Programa Secuencial (cartoon_seq.c)
 * Descripcion:
 *   Genera una imagen tipo Cartoon a partir de una imagen original RGB
 *   aplicando dos procesos independientes y sumando sus resultados:
 *     A) Deteccion de bordes:
 *        1. Filtrado (Borroneo 3x3 o 5x5 con borde neutro negro)
 *        2. Resaltado (Magnitud del gradiente con operador de Sobel 3x3 o 5x5)
 *        3. Deteccion (Umbralado)
 *     B) Posterizado por cada canal de color (R, G, B) en r rangos (3 o 9)
 *        y suma en una sola imagen.
 *     C) Suma final de la deteccion de bordes y el posterizado.
 *
 * Compilacion:
 *   gcc -std=c11 -O3 -Wall -Wextra -Iinclude src/cartoon_seq.c -lm -o bin/cartoon_seq
 *
 * Uso:
 *   ./bin/cartoon_seq <entrada> <salida> <filtro: 3|5> <rangos: 3|9> [umbral] [--etapas]
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "imagen_io.h"

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr,
                "Uso: %s <imagen_entrada> <imagen_salida> <filtro: 3|5> <rangos: 3|9> [umbral] [--etapas]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    const char *ruta_entrada = argv[1];
    const char *ruta_salida  = argv[2];
    int tam_filtro           = atoi(argv[3]);
    int rangos_post          = atoi(argv[4]);
    int umbral               = UMBRAL_BORDE_DEF;
    int guardar_etapas       = 0;

    for (int i = 5; i < argc; i++) {
        if (strcmp(argv[i], "--etapas") == 0) {
            guardar_etapas = 1;
        } else {
            umbral = atoi(argv[i]);
        }
    }

    if (!validar_parametros_algoritmo(tam_filtro, rangos_post)) {
        return EXIT_FAILURE;
    }

    /* 1. Carga de la imagen original a una matriz de pixeles RGB */
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

    /* 2. Ejecucion del pipeline Secuencial y medicion de tiempos */
    double t0 = obtener_tiempo_seg();

    /* Rama 1 - Paso 1: Filtrado (Eliminacion de ruido / Borroneo) */
    paso1_filtrado_borroneo_rango(
        img_orig, img_blur, ancho,
        0, alto, 0, alto, tam_filtro);
    double t1 = obtener_tiempo_seg();

    /* Rama 1 - Paso 2 y 3: Resaltado (Sobel) + Deteccion (Umbralado) */
    paso2y3_resaltado_y_deteccion_rango(
        img_blur, img_bordes, ancho,
        0, alto, 0, alto, tam_filtro, umbral);
    double t2 = obtener_tiempo_seg();

    /* Rama 2: Posterizado por canal RGB y suma en una sola imagen */
    rama2_posterizado_rango(
        img_orig, img_post, ancho,
        0, alto, rangos_post);
    double t3 = obtener_tiempo_seg();

    /* Combinacion Final: Suma de Deteccion de Bordes + Posterizado */
    combinar_bordes_y_posterizado_rango(
        img_bordes, img_post, img_cartoon,
        ancho, 0, alto);
    double t4 = obtener_tiempo_seg();

    double t_filtrado  = t1 - t0;
    double t_bordes    = t2 - t1;
    double t_post      = t3 - t2;
    double t_suma      = t4 - t3;
    double t_total_alg = t4 - t0;

    /* 3. Almacenamiento de la imagen Cartoon resultante */
    if (!guardar_imagen_rgb(ruta_salida, img_cartoon, ancho, alto)) {
        stbi_image_free(img_orig);
        free(img_blur);
        free(img_bordes);
        free(img_post);
        free(img_cartoon);
        return EXIT_FAILURE;
    }

    /* Si se solicita --etapas, guarda las imagenes intermedias para el informe */
    if (guardar_etapas) {
        char ruta_aux[512];
        snprintf(ruta_aux, sizeof(ruta_aux), "%s_1_borroneado.png", ruta_salida);
        guardar_imagen_rgb(ruta_aux, img_blur, ancho, alto);

        snprintf(ruta_aux, sizeof(ruta_aux), "%s_2_bordes.png", ruta_salida);
        guardar_imagen_rgb(ruta_aux, img_bordes, ancho, alto);

        snprintf(ruta_aux, sizeof(ruta_aux), "%s_3_posterizado.png", ruta_salida);
        guardar_imagen_rgb(ruta_aux, img_post, ancho, alto);
    }

    printf("================================================================\n");
    printf(" [SECUENCIAL] Generacion de Cartoon completada\n");
    printf("================================================================\n");
    printf(" Imagen        : %s (%d x %d px)\n", ruta_entrada, ancho, alto);
    printf(" Configuracion : Filtro %dx%d | Posterizado %d rangos | Umbral %d\n",
           tam_filtro, tam_filtro, rangos_post, umbral);
    printf(" Tiempos (s)   : Filtrado=%.6f | Bordes=%.6f | Post=%.6f | Suma=%.6f\n",
           t_filtrado, t_bordes, t_post, t_suma);
    printf(" Tiempo Total  : %.6f segundos\n", t_total_alg);
    printf("CSV_METRIC,SEQ,1,1,%dx%d,%dx%d,%d,%.6f,%.6f\n",
           ancho, alto, tam_filtro, tam_filtro, rangos_post, t_total_alg, t_total_alg);

    stbi_image_free(img_orig);
    free(img_blur);
    free(img_bordes);
    free(img_post);
    free(img_cartoon);

    return EXIT_SUCCESS;
}
