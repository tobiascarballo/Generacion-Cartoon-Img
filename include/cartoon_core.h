/*
 * ============================================================================
 * Trabajo Practico Final Integrador - Computacion Avanzada (UADER FCyT)
 * Alumnos: Carballo Tobias, Dacuy Santino
 * Docente: Dra. Maria Fabiana Piccoli
 *
 * Archivo: include/cartoon_core.h
 * Descripcion:
 *   Nucleo computacional y matematico compartido por las cuatro implementaciones:
 *     1. Secuencial (src/cartoon_seq.c)
 *     2. Memoria Compartida - OpenMP (src/cartoon_omp.c)
 *     3. Memoria Distribuida - MPI (src/cartoon_mpi.c)
 *     4. Paralela Hibrida - MPI + OpenMP (src/cartoon_hybrid.c)
 *
 *   Implementa el pipeline completo del efecto Cartoon descripto en el apunte
 *   de catedra "Procesamiento de Imagenes" de la Dra. Piccoli:
 *     - RAMA 1: Deteccion de bordes en tres pasos obligatorios:
 *         * Paso 1: Filtrado / Borroneo (media aritmetica 3x3 o 5x5).
 *         * Paso 2: Resaltado con gradiente direccional de Sobel (3x3 o 5x5).
 *         * Paso 3: Deteccion mediante umbralado binario (Umbralize).
 *     - RAMA 2: Posterizado cuantizado por canal RGB independiente y fusion.
 *     - COMBINACION FINAL: Suma pixel a pixel con saturacion en 255.
 * ============================================================================
 */

#ifndef CARTOON_CORE_H
#define CARTOON_CORE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

/*
 * CANALES_RGB: Cada pixel digital en formato RGB se representa con 3 canales:
 *   Indice 0: Rojo (Red)
 *   Indice 1: Verde (Green)
 *   Indice 2: Azul (Blue)
 * La imagen se almacena en memoria contigua en orden por filas (row-major order):
 *   Tamanio total = ancho * alto * 3 bytes.
 */
#define CANALES_RGB 3

/*
 * UMBRAL_BORDE_DEF: Valor de corte por defecto para el Paso 3 (Umbralize).
 * Si la magnitud ponderada del gradiente de Sobel supera este valor, el pixel
 * se clasifica como borde (255 = blanco); en caso contrario, se descarta (0 = negro).
 */
#define UMBRAL_BORDE_DEF 90

/* ----------------------------------------------------------------------------
 * reservar_memoria:
 *   Encapsula la reserva dinamica de memoria usando calloc, que ademas de
 *   garantizar memoria contigua inicializa todos los bytes a cero.
 *   Esto asegura que los pixeles de borde auxiliar comiencen siendo negros (0,0,0),
 *   respetando estrictamente la consigna del trabajo practico.
 * ---------------------------------------------------------------------------- */
static inline void *reservar_memoria(size_t cantidad, size_t tam_elemento)
{
    /* Si se solicitan 0 elementos, se reserva 1 byte para evitar punteros NULL ambiguos */
    size_t n = (cantidad > 0) ? cantidad : 1;

    /* calloc limpia a 0 toda la memoria asignada */
    void *ptr = calloc(n, tam_elemento);

    /* Validacion de seguridad: si el sistema operativo rechaza la solicitud, se aborta */
    if (!ptr) {
        fprintf(stderr, "[ERROR CRITICO] Fallo de asignacion de memoria: %zu elementos de %zu bytes.\n",
                cantidad, tam_elemento);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

/* ----------------------------------------------------------------------------
 * saturar_u8:
 *   Garantiza que cualquier resultado aritmetico (sumas, magnitudes, convoluciones)
 *   permanezca confinado en el rango representable por un entero sin signo de 8 bits:
 *   [0, 255]. Si es menor a 0 se trunca a 0; si supera 255 se trunca a 255.
 * ---------------------------------------------------------------------------- */
static inline uint8_t saturar_u8(int valor)
{
    if (valor < 0) return 0;
    if (valor > 255) return 255;
    return (uint8_t)valor;
}

/* ----------------------------------------------------------------------------
 * obtener_pixel_seguro:
 *   Lectura segura de un canal de color en la coordenada (y, x, c).
 *   Si la coordenada cae fuera de las filas validas del buffer o fuera del ancho,
 *   retorna 0 (color neutro negro), satisfaciendo la condicion de contorno exigida.
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
    /* Verificacion de limites espaciales en 2D */
    if (y < fila_min_valida || y >= fila_max_valida || x < 0 || x >= ancho) {
        return 0; /* Color neutro negro (0, 0, 0) para bordes fuera de limites */
    }

    /* Mapeo 2D a 1D contiguo: ((y * ancho) + x) * CANALES_RGB + c */
    return img[((size_t)y * ancho + x) * CANALES_RGB + c];
}

/* ============================================================================
 * RAMA 1: DETECCION DE BORDES (Apunte de Catedra, Paginas 48 a 58)
 * ============================================================================
 *
 * Paso 1: Filtrado / Borroneo (Mean Blur)
 *   Aplica una convolucion espacial 2D pasa-bajo de media aritmetica (K x K):
 *     - K = 3: Mascara de 3x3 (radio = 1, area = 9 elementos con peso 1/9).
 *     - K = 5: Mascara de 5x5 (radio = 2, area = 25 elementos con peso 1/25).
 *   Objetivo: Atenuar el ruido de alta frecuencia para que los operadores
 *   gradientes posteriores no detecten falsos positivos en zonas texturadas.
 *
 *   Disenio para paralelismo:
 *     Trabaja en el intervalo de filas [fila_ini, fila_fin).
 *     El buffer de entrada contiene datos validos entre [fila_min_buf, fila_max_buf).
 *     En OpenMP: fila_min_buf = 0, fila_max_buf = alto total.
 *     En MPI: fila_min_buf = 0 (incluyendo halo superior), fila_max_buf = total_con_halos.
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
    /* Radio de la ventana de convolucion (1 para 3x3, 2 para 5x5) */
    const int radio = tam_filtro / 2;

    /* Cantidad total de coeficientes de la mascara (9 o 25) */
    const int area = tam_filtro * tam_filtro;

    /* Recorrido fila por fila del bloque asignado a este hilo o proceso */
    for (int y = fila_ini; y < fila_fin; y++) {
        for (int x = 0; x < ancho; x++) {
            /* Acumuladores enteros para evitar desbordes antes de la division */
            int suma_r = 0, suma_g = 0, suma_b = 0;

            /* Convolucion bidimensional sobre la vecindad [-radio, +radio] */
            for (int dy = -radio; dy <= radio; dy++) {
                int ny = y + dy; /* Coordenada vertical vecina */
                for (int dx = -radio; dx <= radio; dx++) {
                    int nx = x + dx; /* Coordenada horizontal vecina */

                    /* Si el vecino se encuentra dentro del rango de memoria accesible */
                    if (ny >= fila_min_buf && ny < fila_max_buf && nx >= 0 && nx < ancho) {
                        size_t idx = ((size_t)ny * ancho + nx) * CANALES_RGB;
                        suma_r += entrada[idx + 0];
                        suma_g += entrada[idx + 1];
                        suma_b += entrada[idx + 2];
                    }
                    /* Si cae fuera de los bordes, el valor neutro es negro (suma 0) */
                }
            }

            /* Se calcula el promedio aritmetico dividiendo por el area total */
            size_t out_idx = ((size_t)y * ancho + x) * CANALES_RGB;
            salida_blur[out_idx + 0] = (uint8_t)(suma_r / area);
            salida_blur[out_idx + 1] = (uint8_t)(suma_g / area);
            salida_blur[out_idx + 2] = (uint8_t)(suma_b / area);
        }
    }
}

/* ============================================================================
 * Pasos 2 y 3: Resaltado de Bordes y Deteccion por Umbralado
 * ============================================================================
 *
 * Paso 2: Resaltado (Sobel)
 *   Calcula las derivadas espaciales direccionales horizontales (Gx) y
 *   verticales (Gy) sobre la imagen suavizada.
 *   Luego calcula la magnitud euclidea del gradiente:
 *     |G| = sqrt(Gx^2 + Gy^2)
 *   Calcula la luminancia ponderada segun la percepcion visual humana (Pag. 5 y 12):
 *     Y = 0.30 * R + 0.59 * G + 0.11 * B
 *
 * Paso 3: Deteccion mediante Umbralado (Umbralize)
 *   Evalua si Y >= umbral:
 *     - Si es verdadero: 255 (linea de borde blanca).
 *     - Si es falso: 0 (fondo negro sin bordes).
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
    /* Mascaras direccionales de Sobel 3x3 estandar (Pagina 56 del Apunte) */
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

    /* Mascaras de Sobel extendidas a 5x5 para filtros de mayor dimension */
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

    /*
     * Factor de escala: la mascara 5x5 tiene coeficientes mayores; dividiendo por
     * 12.0f se mantiene la escala dinamica del gradiente equivalente a la de 3x3.
     */
    const float escala_grad = (tam_filtro == 5) ? (1.0f / 12.0f) : 1.0f;

    for (int y = fila_ini; y < fila_fin; y++) {
        for (int x = 0; x < ancho; x++) {
            /* Acumuladores del gradiente horizontal (Gx) y vertical (Gy) por canal */
            int gx_r = 0, gy_r = 0;
            int gx_g = 0, gy_g = 0;
            int gx_b = 0, gy_b = 0;

            if (tam_filtro == 3) {
                /* Convolucion con mascara Sobel 3x3 */
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
                /* Convolucion con mascara Sobel 5x5 */
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

            /* Paso 2: Calculo de la magnitud del gradiente |G| = sqrt(Gx^2 + Gy^2) */
            int mag_r = saturar_u8((int)(sqrtf((float)(gx_r * gx_r + gy_r * gy_r)) * escala_grad));
            int mag_g = saturar_u8((int)(sqrtf((float)(gx_g * gx_g + gy_g * gy_g)) * escala_grad));
            int mag_b = saturar_u8((int)(sqrtf((float)(gx_b * gx_b + gy_b * gy_b)) * escala_grad));

            /*
             * Conversion a escala de grises mediante luminosidad perceptiva estandar:
             *   Y = 0.30 * R + 0.59 * G + 0.11 * B  (Aritmetica entera con base 100)
             */
            int mag_gris = (30 * mag_r + 59 * mag_g + 11 * mag_b) / 100;

            /*
             * Paso 3: Umbralado binario (Umbralize):
             *   Si el cambio de intensidad es abrupto (>= umbral), es borde (255 = blanco).
             *   Si el cambio es suave (< umbral), es fondo liso (0 = negro).
             */
            uint8_t valor_borde = (mag_gris >= umbral) ? 255 : 0;

            /* Se asigna el valor binario a los tres canales de salida */
            size_t out_idx = ((size_t)y * ancho + x) * CANALES_RGB;
            salida_bordes[out_idx + 0] = valor_borde;
            salida_bordes[out_idx + 1] = valor_borde;
            salida_bordes[out_idx + 2] = valor_borde;
        }
    }
    (void)radio; /* Suprime advertencia de variable no utilizada si el compilador no la optimiza */
}

/* ============================================================================
 * RAMA 2: POSTERIZADO POR CANAL Y SUMA (Apunte de Catedra, Paginas 10 y 12)
 * ============================================================================
 *
 * "El posterizado divide el rango total de tonos continuos (0..255) en una
 *  cantidad finita de rangos discretos (ej. 3 o 9). En imagenes color, se realiza
 *  el posterizado en cada canal de forma independiente y luego se suman en una sola."
 *
 * Optimizacion computacional:
 *   Se precalcula una tabla Look-Up Table (LUT) de 256 bytes.
 *   Esto permite mapear cada canal en complejidad temporal O(1) directo de memoria,
 *   evitando divisiones costosas dentro del bucle de pixeles.
 * ============================================================================ */
static inline uint8_t cuantizar_nivel_posterizado(uint8_t valor, int num_rangos)
{
    if (num_rangos <= 1) return 0;

    /* Determina a que escalon discreto [0 .. num_rangos - 1] pertenece el valor */
    int escalon = ((int)valor * num_rangos) / 256;
    if (escalon >= num_rangos) escalon = num_rangos - 1;

    /* Re-proyecta el escalon de forma uniforme al rango visual [0 .. 255] */
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
    /* Precomputacion de la tabla LUT (256 entradas) */
    uint8_t lut[256];
    for (int z = 0; z < 256; z++) {
        lut[z] = cuantizar_nivel_posterizado((uint8_t)z, num_rangos);
    }

    /* Recorrido de los pixeles de las filas asignadas */
    for (int y = fila_ini; y < fila_fin; y++) {
        for (int x = 0; x < ancho; x++) {
            size_t idx = ((size_t)y * ancho + x) * CANALES_RGB;

            /* Posterizacion O(1) independiente por cada componente espectral */
            uint8_t r_post = lut[entrada_orig[idx + 0]];
            uint8_t g_post = lut[entrada_orig[idx + 1]];
            uint8_t b_post = lut[entrada_orig[idx + 2]];

            /* Almacenamiento en el buffer de salida posterizado */
            salida_post[idx + 0] = r_post;
            salida_post[idx + 1] = g_post;
            salida_post[idx + 2] = b_post;
        }
    }
}

/* ============================================================================
 * COMBINACION FINAL: FUSION DE BORDES + POSTERIZADO (Pagina 12 del Apunte)
 * ============================================================================
 *
 * Se realiza la suma pixel a pixel entre la imagen posterizada y la mascara
 * de bordes detectada. Para evitar desbordes aritmeticos por encima de 255,
 * se aplica suma saturada:
 *   Pixel_Final = min(255, Pixel_Posterizado + Pixel_Bordes)
 *
 * Como la deteccion de bordes vale 255 donde hay trazos fuertes, el resultado
 * final resalta los contornos estilo dibujo animado / historieta (Cartoon).
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

            /* Suma con saturacion para canal Rojo */
            salida_cartoon[idx + 0] = saturar_u8((int)posterizado[idx + 0] + (int)bordes[idx + 0]);

            /* Suma con saturacion para canal Verde */
            salida_cartoon[idx + 1] = saturar_u8((int)posterizado[idx + 1] + (int)bordes[idx + 1]);

            /* Suma con saturacion para canal Azul */
            salida_cartoon[idx + 2] = saturar_u8((int)posterizado[idx + 2] + (int)bordes[idx + 2]);
        }
    }
}

#endif /* CARTOON_CORE_H */
