/*
 * ============================================================================
 * Trabajo Practico Final Integrador - Computacion Avanzada (UADER FCyT)
 * Alumnos: Carballo Tobias, Dacuy Santino
 *
 * Archivo: src/cartoon_seq.c
 * Inciso 1: Programa Secuencial de referencia
 *
 * ¿Cual es su objetivo?
 *   Transforma una imagen real RGB en un dibujo estilo Cartoon ejecutando el
 *   algoritmo en un unico hilo (sin paralelismo). Este tiempo se toma como
 *   tiempo base de referencia (Ts) para calcular el Speedup de las versiones
 *   OpenMP, MPI e Hibrida.
 *
 * Pipeline del algoritmo (segun Apuntes de catedra):
 *   1. Rama 1 - Paso 1: Suavizado / Borroneo (filtro de media 3x3 o 5x5)
 *   2. Rama 1 - Paso 2 y 3: Resaltado de bordes (Sobel) + Deteccion (Umbralado)
 *   3. Rama 2: Posterizado de canales RGB (reduccion a 3 o 9 niveles tonales)
 *   4. Combinacion Final: Suma saturada de Bordes + Posterizado
 *
 * Compilacion manual:
 *   gcc -std=c11 -O3 -Wall -Wextra -Iinclude src/cartoon_seq.c -lm -o bin/cartoon_seq.exe
 * ============================================================================
 */

#include <stdio.h>    /* Entrada y salida estandar: printf, fprintf, stderr */
#include <stdlib.h>   /* Utilidades estandar: atoi, exit, EXIT_SUCCESS, EXIT_FAILURE */
#include <stdint.h>   /* Tipos con ancho fijo: uint8_t (enteros sin signo de 8 bits: 0 a 255) */
#include <string.h>   /* Manejo de cadenas: strcmp, snprintf */

#include "imagen_io.h" /* Funciones compartidas de carga/guardado de imagenes y temporizador */

int main(int argc, char **argv)
{
    /* ------------------------------------------------------------------------
     * 1. VALIDACION DE ARGUMENTOS DE LINEA DE COMANDOS
     * ------------------------------------------------------------------------ */
    /* Se requieren al menos 4 argumentos obligatorios: entrada, salida, filtro y rangos */
    if (argc < 5) {
        fprintf(stderr,
                "Uso: %s <imagen_entrada> <imagen_salida> <filtro: 3|5> <rangos: 3|9> [umbral] [--etapas]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    /* Asignacion de los parametros pasados por terminal */
    const char *ruta_entrada = argv[1];          /* Ruta del archivo de imagen a procesar */
    const char *ruta_salida  = argv[2];          /* Ruta donde se guardara la imagen cartoon resultante */
    int tam_filtro           = atoi(argv[3]);    /* Tamaño del filtro de convolucion (3 para 3x3 o 5 para 5x5) */
    int rangos_post          = atoi(argv[4]);    /* Niveles de posterizado solicitados (3 o 9 rangos) */
    int umbral               = UMBRAL_BORDE_DEF; /* Valor de umbral para clasificar bordes (por defecto 75 o 90) */
    int guardar_etapas       = 0;                /* Bandera: 1 si se pasa --etapas para guardar las fotos intermedias */

    /* Lectura de argumentos opcionales (el orden puede variar) */
    for (int i = 5; i < argc; i++) {
        if (strcmp(argv[i], "--etapas") == 0) {
            guardar_etapas = 1;                  /* Activa la generacion de las 3 imagenes intermedias */
        } else {
            umbral = atoi(argv[i]);              /* Si es un numero, se interpreta como nuevo umbral */
        }
    }

    /* Validacion de parametros algoritmicos segun la consigna oficial */
    if (!validar_parametros_algoritmo(tam_filtro, rangos_post)) {
        return EXIT_FAILURE;                     /* Retorna error si el filtro no es 3 o 5, o los rangos no son 3 o 9 */
    }

    /* ------------------------------------------------------------------------
     * 2. CARGA DE LA IMAGEN EN MEMORIA RAM
     * ------------------------------------------------------------------------ */
    int ancho = 0, alto = 0;
    /* cargar_imagen_rgb utiliza stb_image.h para descomprimir JPG/PNG a un arreglo 1D contiguo */
    uint8_t *img_orig = cargar_imagen_rgb(ruta_entrada, &ancho, &alto);
    if (!img_orig) {
        return EXIT_FAILURE;                     /* Falla si el archivo no existe o esta corrupto */
    }

    /* Calculo de la cantidad total de bytes de la imagen: W * H * 3 canales (R, G, B) */
    size_t total_bytes = (size_t)ancho * (size_t)alto * CANALES_RGB;

    /* Reserva de memoria dinamica para los buffers intermedios y final */
    uint8_t *img_blur    = (uint8_t *)reservar_memoria(total_bytes, sizeof(uint8_t)); /* Buffer para Paso 1: Suavizado */
    uint8_t *img_bordes  = (uint8_t *)reservar_memoria(total_bytes, sizeof(uint8_t)); /* Buffer para Pasos 2-3: Bordes Sobel */
    uint8_t *img_post    = (uint8_t *)reservar_memoria(total_bytes, sizeof(uint8_t)); /* Buffer para Rama 2: Posterizado */
    uint8_t *img_cartoon = (uint8_t *)reservar_memoria(total_bytes, sizeof(uint8_t)); /* Buffer para la imagen Cartoon final */

    /* ------------------------------------------------------------------------
     * 3. EJECUCION SECUENCIAL DEL PIPELINE Y MEDICION DE TIEMPOS
     * ------------------------------------------------------------------------
     * Se mide exclusivamente el tiempo de computo (excluyendo carga y guardado de disco)
     */
    double t0 = obtener_tiempo_seg();            /* Marca de tiempo inicial (cronometro de alta precision) */

    /* RAMA 1 - PASO 1: Filtrado Pasa-Bajo (Borroneo / Suavizado)
     * Aplica la convolucion de media aritmetica sobre el rango completo de filas [0, alto).
     * Su objetivo es eliminar el ruido para que las derivadas no disparen falsos bordes. */
    paso1_filtrado_borroneo_rango(
        img_orig, img_blur, ancho,
        0, alto, 0, alto, tam_filtro);
    double t1 = obtener_tiempo_seg();            /* Fin del Paso 1 */

    /* RAMA 1 - PASO 2 y 3: Resaltado (Gradiente de Sobel) + Deteccion (Umbralado)
     * Lee la imagen ya borroneada (img_blur) y calcula |G| = sqrt(Gx^2 + Gy^2).
     * Si el gradiente supera el umbral, asigna blanco (255); si no, negro (0). */
    paso2y3_resaltado_y_deteccion_rango(
        img_blur, img_bordes, ancho,
        0, alto, 0, alto, tam_filtro, umbral);
    double t2 = obtener_tiempo_seg();            /* Fin de Pasos 2 y 3 */

    /* RAMA 2: Posterizado por Canal de Color (R, G, B)
     * Reduce los 256 niveles tonales a 3 o 9 rangos usando una tabla LUT precalculada O(1).
     * Suma los 3 canales posterizados en una sola imagen RGB. */
    rama2_posterizado_rango(
        img_orig, img_post, ancho,
        0, alto, rangos_post);
    double t3 = obtener_tiempo_seg();            /* Fin de Rama 2 */

    /* COMBINACION FINAL: Suma saturada de Bordes + Posterizado
     * Para cada pixel y canal: Cartoon = min(255, Posterizado + Bordes). */
    combinar_bordes_y_posterizado_rango(
        img_bordes, img_post, img_cartoon,
        ancho, 0, alto);
    double t4 = obtener_tiempo_seg();            /* Fin del pipeline completo */

    /* Calculo de duracion por cada etapa individual y tiempo total de calculo */
    double t_filtrado  = t1 - t0;
    double t_bordes    = t2 - t1;
    double t_post      = t3 - t2;
    double t_suma      = t4 - t3;
    double t_total_alg = t4 - t0;                /* Tiempo secuencial puro Ts */

    /* ------------------------------------------------------------------------
     * 4. GUARDADO DE LA IMAGEN RESULTANTE EN DISCO
     * ------------------------------------------------------------------------ */
    if (!guardar_imagen_rgb(ruta_salida, img_cartoon, ancho, alto)) {
        stbi_image_free(img_orig);
        free(img_blur);
        free(img_bordes);
        free(img_post);
        free(img_cartoon);
        return EXIT_FAILURE;
    }

    /* Si el usuario pidio --etapas, guardamos las fotos intermedias para el informe */
    if (guardar_etapas) {
        char ruta_aux[512];
        /* 1. Imagen suavizada */
        snprintf(ruta_aux, sizeof(ruta_aux), "%s_1_borroneado.png", ruta_salida);
        guardar_imagen_rgb(ruta_aux, img_blur, ancho, alto);

        /* 2. Imagen de bordes blancos sobre fondo negro */
        snprintf(ruta_aux, sizeof(ruta_aux), "%s_2_bordes.png", ruta_salida);
        guardar_imagen_rgb(ruta_aux, img_bordes, ancho, alto);

        /* 3. Imagen posterizada con colores planos */
        snprintf(ruta_aux, sizeof(ruta_aux), "%s_3_posterizado.png", ruta_salida);
        guardar_imagen_rgb(ruta_aux, img_post, ancho, alto);
    }

    /* ------------------------------------------------------------------------
     * 5. IMPRESION DE METRICAS Y RESULTADOS POR CONSOLA
     * ------------------------------------------------------------------------ */
    printf("================================================================\n");
    printf(" [SECUENCIAL] Generacion de Cartoon completada\n");
    printf("================================================================\n");
    printf(" Imagen        : %s (%d x %d px)\n", ruta_entrada, ancho, alto);
    printf(" Configuracion : Filtro %dx%d | Posterizado %d rangos | Umbral %d\n",
           tam_filtro, tam_filtro, rangos_post, umbral);
    printf(" Tiempos (s)   : Filtrado=%.6f | Bordes=%.6f | Post=%.6f | Suma=%.6f\n",
           t_filtrado, t_bordes, t_post, t_suma);
    printf(" Tiempo Total  : %.6f segundos\n", t_total_alg);
    /* Salida en formato CSV para analisis automatizado de benchmarks */
    printf("CSV_METRIC,SEQ,1,1,%dx%d,%dx%d,%d,%.6f,%.6f\n",
           ancho, alto, tam_filtro, tam_filtro, rangos_post, t_total_alg, t_total_alg);

    /* ------------------------------------------------------------------------
     * 6. LIBERACION DE MEMORIA DINAMICA (Evita fugas de memoria / memory leaks)
     * ------------------------------------------------------------------------ */
    stbi_image_free(img_orig);                   /* Libera el buffer creado por stb_image */
    free(img_blur);                              /* Libera buffer de borroneo */
    free(img_bordes);                            /* Libera buffer de bordes */
    free(img_post);                              /* Libera buffer de posterizado */
    free(img_cartoon);                           /* Libera buffer de cartoon final */

    return EXIT_SUCCESS;                         /* Fin exitoso del programa secuencial */
}
