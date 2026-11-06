/*
 * ============================================================================
 * Trabajo Practico Final Integrador - Computacion Avanzada (UADER FCyT)
 * Alumnos: Carballo Tobias, Dacuy Santino
 * Docente: Dra. Maria Fabiana Piccoli
 *
 * Archivo: include/imagen_io.h
 * Descripcion:
 *   Modulo de Entrada/Salida de imagenes y temporizacion de alta resolucion.
 *   - Utiliza las bibliotecas libres de cabecera unica stb_image.h y
 *     stb_image_write.h (Sean Barrett) para decodificar y codificar imagenes
 *     en formatos populares (PNG, JPG, BMP).
 *   - Normaliza siempre los datos en memoria como un arreglo unidimensional
 *     contiguo de bytes RGB (3 bytes por pixel, row-major order: W * H * 3).
 *   - Implementa un temporizador monotono de alta resolucion multiplataforma
 *     (QueryPerformanceCounter en Windows / clock_gettime en Linux POSIX).
 * ============================================================================
 */

#ifndef IMAGEN_IO_H
#define IMAGEN_IO_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/*
 * Se define STB_IMAGE_IMPLEMENTATION en un unico archivo para que el preprocesador
 * genere el codigo de las funciones de decodificacion de stb_image.
 */
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

/*
 * Se define STB_IMAGE_WRITE_IMPLEMENTATION para generar el codigo de codificacion
 * y escritura a disco (PNG, JPG, BMP).
 */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "cartoon_core.h"

/* ============================================================================
 * TEMPORIZADOR DE ALTA PRECISION MULTIPLATAFORMA
 * ============================================================================
 * Permite medir el tiempo de computo puro (excluyendo E/S de disco) con una
 * precision de microsegundos o nanosegundos segun el sistema operativo.
 * ============================================================================ */
#if defined(_WIN32)
#include <windows.h>

/*
 * obtener_tiempo_seg (Windows):
 *   Usa QueryPerformanceCounter (QPC) y QueryPerformanceFrequency.
 *   Es el reloj de mayor precision disponible en Windows para benchmarks de HPC.
 */
static inline double obtener_tiempo_seg(void)
{
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
}
#else
#include <time.h>

/*
 * obtener_tiempo_seg (Linux / POSIX):
 *   Usa clock_gettime con CLOCK_MONOTONIC, que no se ve afectado por saltos
 *   de hora del sistema NTP.
 */
static inline double obtener_tiempo_seg(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#endif

/* ----------------------------------------------------------------------------
 * cargar_imagen_rgb:
 *   Lee un archivo de imagen desde disco (JPG, PNG, BMP, etc.).
 *   Fuerza la conversion automatica a 3 canales (RGB: CANALES_RGB), ignorando
 *   canales alfa (transparencia) o expandiendo imagenes en escala de grises.
 *   Retorna un puntero al bloque contiguo de memoria con los bytes de la imagen.
 * ---------------------------------------------------------------------------- */
static inline uint8_t *cargar_imagen_rgb(const char *ruta, int *ancho, int *alto)
{
    int canales_archivo = 0; /* Almacena los canales originales del archivo */

    /*
     * stbi_load: decodifica el archivo.
     * Pasando CANALES_RGB (3) como ultimo argumento, stbi_load entrega exactamente
     * 3 bytes por pixel en formato RGB continuo (ancho * alto * 3).
     */
    unsigned char *datos = stbi_load(ruta, ancho, alto, &canales_archivo, CANALES_RGB);

    if (!datos) {
        fprintf(stderr, "[ERROR] No se pudo cargar la imagen '%s'. Causa: %s\n",
                ruta, stbi_failure_reason());
        return NULL;
    }
    return (uint8_t *)datos;
}

/* ----------------------------------------------------------------------------
 * guardar_imagen_rgb:
 *   Escribe el arreglo contiguo de pixeles RGB en disco.
 *   Detecta la extension (.png, .bmp, .jpg) para invocar al escritor correspondiente.
 *   Si la ruta es "NONE", no escribe nada (util para mediciones de rendimiento masivo).
 * ---------------------------------------------------------------------------- */
static inline int guardar_imagen_rgb(const char *ruta, const uint8_t *img, int ancho, int alto)
{
    /* Si se especifica "NONE", se omite la escritura a disco */
    if (!ruta || strcmp(ruta, "NONE") == 0) {
        return 1;
    }

    /* Extrae la extension del archivo a partir del ultimo punto */
    const char *ext = strrchr(ruta, '.');
    int ok = 0;

    if (ext && (strcmp(ext, ".bmp") == 0 || strcmp(ext, ".BMP") == 0)) {
        /* Escritura en formato mapa de bits sin compresion */
        ok = stbi_write_bmp(ruta, ancho, alto, CANALES_RGB, img);
    } else if (ext && (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".JPG") == 0 ||
                       strcmp(ext, ".jpeg") == 0 || strcmp(ext, ".JPEG") == 0)) {
        /* Escritura en formato JPEG con calidad visual del 95% */
        ok = stbi_write_jpg(ruta, ancho, alto, CANALES_RGB, img, 95);
    } else {
        /* Por defecto: PNG sin perdida (stride_bytes = ancho * 3 bytes) */
        ok = stbi_write_png(ruta, ancho, alto, CANALES_RGB, img, ancho * CANALES_RGB);
    }

    if (!ok) {
        fprintf(stderr, "[ERROR] Fallo al escribir la imagen de salida en '%s'\n", ruta);
        return 0;
    }
    return 1;
}

/* ----------------------------------------------------------------------------
 * validar_parametros_algoritmo:
 *   Valida los argumentos numericos pasados por linea de comandos:
 *     - tam_filtro: debe ser 3 (3x3) o 5 (5x5), conforme al apunte de catedra.
 *     - rangos: cantidad de niveles de posterizado (debe estar entre 2 y 256).
 * ---------------------------------------------------------------------------- */
static inline int validar_parametros_algoritmo(int tam_filtro, int rangos)
{
    if (tam_filtro != 3 && tam_filtro != 5) {
        fprintf(stderr, "[ERROR] Tamanio de mascara invalido: %d. Solo se permite 3 (3x3) o 5 (5x5).\n",
                tam_filtro);
        return 0;
    }
    if (rangos < 2 || rangos > 256) {
        fprintf(stderr, "[ERROR] Cantidad de rangos de posterizado invalida: %d (debe estar en [2, 256]).\n",
                rangos);
        return 0;
    }
    return 1;
}

#endif /* IMAGEN_IO_H */
