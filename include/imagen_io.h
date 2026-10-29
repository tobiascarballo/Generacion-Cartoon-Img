/*
 * ============================================================================
 * Trabajo Practico Final Integrador - Computacion Avanzada (UADER FCyT)
 * Alumnos: Carballo Tobias, Dacuy Santino
 *
 * Archivo: imagen_io.h
 * Descripcion:
 *   Carga y almacenamiento de imagenes (PNG, JPEG, BMP, TGA, PGM, PPM)
 *   convirtiendolas a una matriz contigua de pixeles RGB (3 bytes por pixel),
 *   y temporizador de alta precision multiplataforma (Windows / Linux).
 * ============================================================================
 */

#ifndef IMAGEN_IO_H
#define IMAGEN_IO_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "cartoon_core.h"

/* Temporizador de alta resolucion para medicion de desempenio */
#if defined(_WIN32)
#include <windows.h>
static inline double obtener_tiempo_seg(void)
{
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
}
#else
#include <time.h>
static inline double obtener_tiempo_seg(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#endif

/* ----------------------------------------------------------------------------
 * Carga una imagen desde disco (PNG, JPG, BMP, PGM, etc.) y la convierte
 * siempre a un arreglo contiguo RGB de 3 bytes por pixel (ancho * alto * 3).
 * ---------------------------------------------------------------------------- */
static inline uint8_t *cargar_imagen_rgb(const char *ruta, int *ancho, int *alto)
{
    int canales_archivo = 0;
    unsigned char *datos = stbi_load(ruta, ancho, alto, &canales_archivo, CANALES_RGB);
    if (!datos) {
        fprintf(stderr, "[ERROR] No se pudo cargar la imagen '%s': %s\n",
                ruta, stbi_failure_reason());
        return NULL;
    }
    return (uint8_t *)datos;
}

/* ----------------------------------------------------------------------------
 * Guarda una matriz de pixeles RGB (ancho * alto * 3) en disco segun extension
 * (.png, .bmp, .jpg). Por defecto utiliza PNG sin perdida.
 * ---------------------------------------------------------------------------- */
static inline int guardar_imagen_rgb(const char *ruta, const uint8_t *img, int ancho, int alto)
{
    if (!ruta || strcmp(ruta, "NONE") == 0) {
        return 1;
    }
    const char *ext = strrchr(ruta, '.');
    int ok = 0;

    if (ext && (strcmp(ext, ".bmp") == 0 || strcmp(ext, ".BMP") == 0)) {
        ok = stbi_write_bmp(ruta, ancho, alto, CANALES_RGB, img);
    } else if (ext && (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".JPG") == 0 ||
                       strcmp(ext, ".jpeg") == 0 || strcmp(ext, ".JPEG") == 0)) {
        ok = stbi_write_jpg(ruta, ancho, alto, CANALES_RGB, img, 95);
    } else {
        ok = stbi_write_png(ruta, ancho, alto, CANALES_RGB, img, ancho * CANALES_RGB);
    }

    if (!ok) {
        fprintf(stderr, "[ERROR] No se pudo guardar la imagen en '%s'\n", ruta);
        return 0;
    }
    return 1;
}

/* ----------------------------------------------------------------------------
 * Validacion de parametros comunes de linea de comandos:
 *   tam_filtro : debe ser 3 (3x3) o 5 (5x5)
 *   rangos     : cantidad de rangos de posterizado (ej. 3 o 9, >= 2)
 * ---------------------------------------------------------------------------- */
static inline int validar_parametros_algoritmo(int tam_filtro, int rangos)
{
    if (tam_filtro != 3 && tam_filtro != 5) {
        fprintf(stderr, "[ERROR] El tamanio de filtro debe ser 3 (3x3) o 5 (5x5). Recibido: %d\n",
                tam_filtro);
        return 0;
    }
    if (rangos < 2 || rangos > 256) {
        fprintf(stderr, "[ERROR] El rango de posterizado debe estar entre 2 y 256. Recibido: %d\n",
                rangos);
        return 0;
    }
    return 1;
}

#endif /* IMAGEN_IO_H */
