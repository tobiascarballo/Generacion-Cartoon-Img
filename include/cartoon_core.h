/*
 * ============================================================================
 * Trabajo Practico Final Integrador - Computacion Avanzada (UADER FCyT)
 * Alumnos: Carballo Tobias, Dacuy Santino
 *
 * Archivo: cartoon_core.h
 * Descripcion:
 *   Nucleo matematico y utilidades de E/S para la generacion de imagenes
 *   Cartoon a partir de imagenes RGB. Compartido por las 4 versiones:
 *     1. Secuencial (cartoon_seq.c)
 *     2. Memoria Distribuida - MPI (cartoon_mpi.c)
 *     3. Memoria Compartida - OpenMP (cartoon_omp.c)
 *     4. Paralela Hibrida - MPI + OpenMP (cartoon_hybrid.c)
 * ============================================================================
 */

#ifndef CARTOON_CORE_H
#define CARTOON_CORE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

/* Numero de canales fijos para representacion RGB (3 bytes por pixel) */
#define CANALES_RGB 3

/* Umbral por defecto para el Paso 3 de Deteccion de Bordes (Pag. 58 Apunte) */
#define UMBRAL_BORDE_DEF 90

/* ----------------------------------------------------------------------------
 * Funcion auxiliar para reserva segura de memoria contigua inicializada en 0
 * (Cumple con la regla: "el valor neutro para bordes auxiliares es negro = 0")
 * ---------------------------------------------------------------------------- */
static inline void *reservar_memoria(size_t cantidad, size_t tam_elemento)
{
    size_t n = (cantidad > 0) ? cantidad : 1;
    void *ptr = calloc(n, tam_elemento);
    if (!ptr) {
        fprintf(stderr, "[ERROR] Fallo al reservar memoria (%zu elementos de %zu bytes).\n",
                cantidad, tam_elemento);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

/* ----------------------------------------------------------------------------
 * Saturacion de valores enteros al rango de 8 bits [0, 255]
 * ---------------------------------------------------------------------------- */
static inline uint8_t saturar_u8(int valor)
{
    if (valor < 0) return 0;
    if (valor > 255) return 255;
    return (uint8_t)valor;
}

/* ----------------------------------------------------------------------------
 * Lectura segura de un pixel (y, x, c) con condicion de borde auxiliar NEGRO
 *
 * Parametros:
 *   img       : buffer contiguo de tamanio alto_buf * ancho * 3
 *   ancho     : ancho de la imagen en pixeles
 *   alto_buf  : cantidad de filas presentes en el buffer 'img'
 *   y, x, c   : fila, columna y canal (0=R, 1=G, 2=B)
 *   fila_min_valida, fila_max_valida : limites de filas validas en el buffer
 * ---------------------------------------------------------------------------- */
static inline uint8_t obtener_pixel_seguro(
    const uint8_t *img,
    int ancho,
    int fila_min_valida,
    int fila_max_valida,
    int y,
    int x,
    int c)
{
    if (y < fila_min_valida || y >= fila_max_valida || x < 0 || x >= ancho) {
        return 0; /* Valor neutro negro (0,0,0) para bordes auxiliares */
    }
    return img[((size_t)y * ancho + x) * CANALES_RGB + c];
}

/* ============================================================================
 * RAMA 1: DETECCION DE BORDES (3 PASOS OBLIGATORIOS - Pag. 57 del Apunte)
 * ============================================================================
 *
 * Paso 1: Filtrado (Eliminacion de ruido / Borroneo de imagen - Pag. 48 y 51)
 *   Aplica un filtro pasa-bajo de media aritmetica de tamanio K x K
 *   (K = 3 -> 3x3 con peso 1/9; K = 5 -> 5x5 con peso 1/25).
 *
 *   Soporta procesamiento de un subconjunto de filas [fila_ini, fila_fin)
 *   dentro de un buffer que tiene filas validas en [fila_min_buf, fila_max_buf),
 *   lo que permite reutilizar exactamente la misma funcion tanto en memoria
 *   compartida como en cada proceso MPI con filas fantasma (halo rows).
 * ============================================================================ */
static inline void paso1_filtrado_borroneo_rango(
    const uint8_t *entrada,
    uint8_t *salida_blur,
    int ancho,
    int fila_ini,
    int fila_fin,
    int fila_min_buf,
    int fila_max_buf,
    int tam_filtro)
{
    const int radio = tam_filtro / 2; /* 1 para 3x3, 2 para 5x5 */
    const int area = tam_filtro * tam_filtro; /* 9 para 3x3, 25 para 5x5 */

    for (int y = fila_ini; y < fila_fin; y++) {
        for (int x = 0; x < ancho; x++) {
            int suma_r = 0, suma_g = 0, suma_b = 0;

            for (int dy = -radio; dy <= radio; dy++) {
                int ny = y + dy;
                for (int dx = -radio; dx <= radio; dx++) {
                    int nx = x + dx;
                    if (ny >= fila_min_buf && ny < fila_max_buf && nx >= 0 && nx < ancho) {
                        size_t idx = ((size_t)ny * ancho + nx) * CANALES_RGB;
                        suma_r += entrada[idx + 0];
                        suma_g += entrada[idx + 1];
                        suma_b += entrada[idx + 2];
                    }
                    /* Si cae fuera de limites, el borde auxiliar es negro (suma 0) */
                }
            }

            size_t out_idx = ((size_t)y * ancho + x) * CANALES_RGB;
            salida_blur[out_idx + 0] = (uint8_t)(suma_r / area);
            salida_blur[out_idx + 1] = (uint8_t)(suma_g / area);
            salida_blur[out_idx + 2] = (uint8_t)(suma_b / area);
        }
    }
}

/* ============================================================================
 * Paso 2 y Paso 3 de Deteccion de Bordes:
 *   - Paso 2 (Resaltado - Pag. 52, 55, 56): Amplifica la diferencia entre
 *     bordes y no-bordes aplicando la aproximacion del gradiente mediante
 *     operadores de Sobel (3x3 o 5x5) y calculando la magnitud del gradiente:
 *       |G| = sqrt(Gx^2 + Gy^2)
 *   - Paso 3 (Deteccion / Umbralado - Pag. 10 y 58): Aplica la operacion
 *     Umbralize(A, u) para detectar los bordes que superan el umbral.
 * ============================================================================ */
static inline void paso2y3_resaltado_y_deteccion_rango(
    const uint8_t *entrada_blur,
    uint8_t *salida_bordes,
    int ancho,
    int fila_ini,
    int fila_fin,
    int fila_min_buf,
    int fila_max_buf,
    int tam_filtro,
    int umbral)
{
    /* Mascaras de Sobel 3x3 (Pag. 56 del Apunte) */
    static const int sobel3_x[3][3] = {
        { -1,  0,  1 },
        { -2,  0,  2 },
        { -1,  0,  1 }
    };
    static const int sobel3_y[3][3] = {
        {  1,  2,  1 },
        {  0,  0,  0 },
        { -1, -2, -1 }
    };

    /* Mascaras de Sobel extendidas a 5x5 (para filtro 5x5) */
    static const int sobel5_x[5][5] = {
        { -1,  -2,  0,   2,  1 },
        { -4,  -8,  0,   8,  4 },
        { -6, -12,  0,  12,  6 },
        { -4,  -8,  0,   8,  4 },
        { -1,  -2,  0,   2,  1 }
    };
    static const int sobel5_y[5][5] = {
        {  1,   4,   6,   4,  1 },
        {  2,   8,  12,   8,  2 },
        {  0,   0,   0,   0,  0 },
        { -2,  -8, -12,  -8, -2 },
        { -1,  -4,  -6,  -4, -1 }
    };

    const int radio = tam_filtro / 2;
    /* Factor de escala para mantener la magnitud de 5x5 comparable con 3x3 */
    const float escala_grad = (tam_filtro == 5) ? (1.0f / 12.0f) : 1.0f;

    for (int y = fila_ini; y < fila_fin; y++) {
        for (int x = 0; x < ancho; x++) {
            int gx_r = 0, gy_r = 0;
            int gx_g = 0, gy_g = 0;
            int gx_b = 0, gy_b = 0;

            if (tam_filtro == 3) {
                for (int dy = -1; dy <= 1; dy++) {
                    int ny = y + dy;
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = x + dx;
                        if (ny >= fila_min_buf && ny < fila_max_buf && nx >= 0 && nx < ancho) {
                            size_t idx = ((size_t)ny * ancho + nx) * CANALES_RGB;
                            int wx = sobel3_x[dy + 1][dx + 1];
                            int wy = sobel3_y[dy + 1][dx + 1];

                            gx_r += entrada_blur[idx + 0] * wx;
                            gy_r += entrada_blur[idx + 0] * wy;

                            gx_g += entrada_blur[idx + 1] * wx;
                            gy_g += entrada_blur[idx + 1] * wy;

                            gx_b += entrada_blur[idx + 2] * wx;
                            gy_b += entrada_blur[idx + 2] * wy;
                        }
                    }
                }
            } else {
                for (int dy = -2; dy <= 2; dy++) {
                    int ny = y + dy;
                    for (int dx = -2; dx <= 2; dx++) {
                        int nx = x + dx;
                        if (ny >= fila_min_buf && ny < fila_max_buf && nx >= 0 && nx < ancho) {
                            size_t idx = ((size_t)ny * ancho + nx) * CANALES_RGB;
                            int wx = sobel5_x[dy + 2][dx + 2];
                            int wy = sobel5_y[dy + 2][dx + 2];

                            gx_r += entrada_blur[idx + 0] * wx;
                            gy_r += entrada_blur[idx + 0] * wy;

                            gx_g += entrada_blur[idx + 1] * wx;
                            gy_g += entrada_blur[idx + 1] * wy;

                            gx_b += entrada_blur[idx + 2] * wx;
                            gy_b += entrada_blur[idx + 2] * wy;
                        }
                    }
                }
            }

            /* Magnitud del gradiente por canal (Paso 2: Resaltado) */
            int mag_r = saturar_u8((int)(sqrtf((float)(gx_r * gx_r + gy_r * gy_r)) * escala_grad));
            int mag_g = saturar_u8((int)(sqrtf((float)(gx_g * gx_g + gy_g * gy_g)) * escala_grad));
            int mag_b = saturar_u8((int)(sqrtf((float)(gx_b * gx_b + gy_b * gy_b)) * escala_grad));

            /* Luminancia del gradiente (Pag. 5 y 12: Y = 0.30*R + 0.59*G + 0.11*B)
               para evaluar el umbral de borde de manera uniforme */
            int mag_gris = (30 * mag_r + 59 * mag_g + 11 * mag_b) / 100;

            /* Paso 3: Deteccion mediante Umbralado (Pag. 10 y 58)
               Rango negro/blanco: si supera el umbral es borde (255), sino 0 (negro) */
            uint8_t valor_borde = (mag_gris >= umbral) ? 255 : 0;

            size_t out_idx = ((size_t)y * ancho + x) * CANALES_RGB;
            salida_bordes[out_idx + 0] = valor_borde;
            salida_bordes[out_idx + 1] = valor_borde;
            salida_bordes[out_idx + 2] = valor_borde;
        }
    }
    (void)radio;
}

/* ============================================================================
 * RAMA 2: POSTERIZADO POR CANAL Y SUMA (Pag. 10 y Pag. 12 del Apunte)
 * ============================================================================
 *   "Posterizado: Divide el rango total de niveles de color en un rango mas
 *    pequenio. Cada pixel de la imagen original pasa a tener un valor de ese
 *    rango mas pequenio (Pag. 10 y ejemplo imagen color en Pag. 12).
 *    En imagenes color se debe realizar el posterizado de cada color y sumar
 *    las imagenes resultantes en una sola."
 * ============================================================================ */
static inline uint8_t cuantizar_nivel_posterizado(uint8_t valor, int num_rangos)
{
    if (num_rangos <= 1) return 0;
    /* Indice de escalon en [0, num_rangos - 1] */
    int escalon = ((int)valor * num_rangos) / 256;
    if (escalon >= num_rangos) escalon = num_rangos - 1;
    /* Mapeo uniforme del escalon al rango [0, 255] */
    return (uint8_t)((escalon * 255) / (num_rangos - 1));
}

static inline void rama2_posterizado_rango(
    const uint8_t *entrada_orig,
    uint8_t *salida_post,
    int ancho,
    int fila_ini,
    int fila_fin,
    int num_rangos)
{
    /* Precalculamos la tabla de consulta (LUT) de 256 entradas para el
       posterizado (funcion evaluada z' = T(z) de Pag. 9 y 10 del Apunte) */
    uint8_t lut[256];
    for (int z = 0; z < 256; z++) {
        lut[z] = cuantizar_nivel_posterizado((uint8_t)z, num_rangos);
    }

    for (int y = fila_ini; y < fila_fin; y++) {
        for (int x = 0; x < ancho; x++) {
            size_t idx = ((size_t)y * ancho + x) * CANALES_RGB;

            /* Posterizado independiente de cada canal (R, G, B) */
            uint8_t r_post = lut[entrada_orig[idx + 0]];
            uint8_t g_post = lut[entrada_orig[idx + 1]];
            uint8_t b_post = lut[entrada_orig[idx + 2]];

            /* Suma de las 3 imagenes monocromaticas posterizadas:
               Img_R(r_post, 0, 0) + Img_G(0, g_post, 0) + Img_B(0, 0, b_post) */
            salida_post[idx + 0] = r_post;
            salida_post[idx + 1] = g_post;
            salida_post[idx + 2] = b_post;
        }
    }
}

/* ============================================================================
 * COMBINACION FINAL: SUMA DE DETECCION DE BORDES + POSTERIZADO
 * ============================================================================
 *   "Finalmente se suman las imagenes resultantes de la deteccion de bordes
 *    y de la posterizacion."
 * ============================================================================ */
static inline void combinar_bordes_y_posterizado_rango(
    const uint8_t *bordes,
    const uint8_t *posterizado,
    uint8_t *salida_cartoon,
    int ancho,
    int fila_ini,
    int fila_fin)
{
    for (int y = fila_ini; y < fila_fin; y++) {
        for (int x = 0; x < ancho; x++) {
            size_t idx = ((size_t)y * ancho + x) * CANALES_RGB;
            salida_cartoon[idx + 0] = saturar_u8((int)posterizado[idx + 0] + (int)bordes[idx + 0]);
            salida_cartoon[idx + 1] = saturar_u8((int)posterizado[idx + 1] + (int)bordes[idx + 1]);
            salida_cartoon[idx + 2] = saturar_u8((int)posterizado[idx + 2] + (int)bordes[idx + 2]);
        }
    }
}

#endif /* CARTOON_CORE_H */
