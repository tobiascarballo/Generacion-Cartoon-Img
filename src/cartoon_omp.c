/*
 * ============================================================================
 * Trabajo Practico Final Integrador - Computacion Avanzada (UADER FCyT)
 * Alumnos: Carballo Tobias, Dacuy Santino
 *
 * Archivo: src/cartoon_omp.c
 * Inciso 3: Programa en Memoria Compartida con OpenMP
 *
 * ¿Cual es su objetivo?
 *   Paraleliza el algoritmo Cartoon utilizando multiples hilos (threads) que
 *   comparten el mismo espacio de memoria RAM del proceso. Permite evaluar el
 *   escalamiento al variar la cantidad de hilos (1, 2, 4, 8 y 16 threads).
 *
 * Conceptos clave de OpenMP implementados y fundamentados:
 *   - Descomposicion de datos por bloques de filas: El bucle exterior de filas
 *     'for (int y = 0; y < alto; y++)' se divide estaticamente entre los hilos.
 *   - 'schedule(static)': Como cada fila de la imagen cuesta exactamente lo mismo
 *     en operaciones aritmeticas, la asignacion estatica reparte bloques contiguos
 *     de H/T filas con cero sobrecarga en tiempo de ejecucion y maxima localidad
 *     en las memorias cache L1/L2.
 *   - 'default(none)': Buena practica de concurrencia que obliga a declarar
 *     explicitamente el ambito de cada variable (shared o private), evitando
 *     condiciones de carrera accidentales.
 *   - Ausencia de data races (sin locks ni critical): Cada hilo lee de buffers
 *     compartidos de solo lectura y escribe unicamente en sus filas asignadas.
 *   - Barrera de sincronizacion implicita: Al cerrar el primer '#pragma omp for',
 *     todos los hilos se esperan antes de que cualquiera inicie Sobel, resolviendo
 *     la dependencia Read-After-Write (Sobel necesita filas vecinas ya borroneadas).
 *
 * Compilacion manual:
 *   gcc -std=c11 -O3 -Wall -Wextra -fopenmp -Iinclude src/cartoon_omp.c -lm -o bin/cartoon_omp.exe
 * ============================================================================
 */

#include <stdio.h>    /* Entrada y salida estandar: printf, fprintf, stderr */
#include <stdlib.h>   /* Utilidades estandar: atoi, exit, EXIT_SUCCESS, EXIT_FAILURE */
#include <stdint.h>   /* Tipos enteros con tamaño exacto: uint8_t */
#include <string.h>   /* Manejo de cadenas */
#include <omp.h>      /* API de OpenMP: omp_set_num_threads, omp_set_dynamic, etc. */

#include "imagen_io.h" /* Carga/guardado de imagenes y temporizador de alta precision */

/* Funcion interna que ejecuta las 4 etapas del pipeline Cartoon en paralelo */
static void ejecutar_pipeline_openmp(
    const uint8_t *img_orig,     /* [IN]  Puntero a los bytes de la imagen original (solo lectura) */
    uint8_t *img_blur,           /* [OUT] Buffer intermedio para la imagen suavizada */
    uint8_t *img_bordes,         /* [OUT] Buffer intermedio para la mascara binaria de bordes */
    uint8_t *img_post,           /* [OUT] Buffer intermedio para la imagen posterizada */
    uint8_t *img_cartoon,        /* [OUT] Buffer final con la suma Cartoon */
    int ancho,                   /* Ancho de la imagen en pixeles (W) */
    int alto,                    /* Alto de la imagen en pixeles (H) */
    int tam_filtro,              /* Tamaño de la mascara (3 o 5) */
    int rangos_post,             /* Cantidad de niveles tonales de posterizado (3 o 9) */
    int umbral,                  /* Valor de corte para binarizar los bordes */
    int num_threads,             /* Cantidad de hilos OpenMP solicitados */
    double *t_filtrado,          /* [OUT] Tiempo medido en segundos para la etapa 1 */
    double *t_bordes,            /* [OUT] Tiempo medido en segundos para la etapa 2 */
    double *t_post,              /* [OUT] Tiempo medido en segundos para la etapa 3 */
    double *t_suma)              /* [OUT] Tiempo medido en segundos para la etapa 4 */
{
    /* Desactiva el ajuste dinamico de hilos del sistema operativo:
     * garantiza que el runtime use exactamente la cantidad pedida (2, 4, 8, 16) */
    omp_set_dynamic(0);

    /* Fija el tamaño del equipo de hilos para las siguientes regiones paralelas */
    omp_set_num_threads(num_threads);

    /* Precalentamiento del equipo de threads (Thread Pool Warm-up):
     * La primera vez que OpenMP crea hilos en Windows, el sistema operativo
     * tarda unos milisegundos en asignar pilas de memoria. Hacemos una region
     * vacia previa para que ese costo no ensucie las mediciones de tiempo */
    #pragma omp parallel
    {
        (void)omp_get_thread_num(); /* Lee el ID de hilo para forzar el despliegue del pool */
    }

    double t0 = obtener_tiempo_seg(); /* Inicio del cronometraje del pipeline paralelo */

    /* ------------------------------------------------------------------------
     * ETAPA 1: FILTRADO / BORRONEO (Convolucion de media aritmetica KxK)
     *
     * Directiva OpenMP explicada clausula por clausula:
     *   - 'parallel for': Crea el equipo de 'num_threads' hilos y divide las iteraciones del for.
     *   - 'default(none)': Obliga a clasificar explicitamente todas las variables.
     *   - 'shared(...)': Todos los hilos leen el mismo arreglo 'img_orig' y escriben en 'img_blur'.
     *   - 'schedule(static)': Asigna a cada hilo un bloque fijo contiguo de filas (~H/T).
     *   - La variable del bucle 'y' es privada automaticamente a cada hilo.
     *   - BARRERA IMPLICITA AL FINAL: Al salir del bloque, ningun hilo avanza a la
     *     Etapa 2 hasta que TODOS hayan terminado de borronear sus filas.
     * ------------------------------------------------------------------------ */
    #pragma omp parallel for default(none) \
        shared(img_orig, img_blur, ancho, alto, tam_filtro) \
        schedule(static)
    for (int y = 0; y < alto; y++) {
        /* Cada hilo procesa su fila 'y' individual llamando al nucleo matematico */
        paso1_filtrado_borroneo_rango(
            img_orig, img_blur, ancho,
            y, y + 1, 0, alto, tam_filtro);
    }
    double t1 = obtener_tiempo_seg(); /* Fin de Etapa 1 */

    /* ------------------------------------------------------------------------
     * ETAPA 2: RESALTADO (Sobel KxK) + DETECCION (Umbralado)
     *
     * Lee la imagen ya borroneada (img_blur) y escribe en img_bordes.
     * Como cada hilo escribe en filas disjuntas, no hay condiciones de carrera.
     * ------------------------------------------------------------------------ */
    #pragma omp parallel for default(none) \
        shared(img_blur, img_bordes, ancho, alto, tam_filtro, umbral) \
        schedule(static)
    for (int y = 0; y < alto; y++) {
        paso2y3_resaltado_y_deteccion_rango(
            img_blur, img_bordes, ancho,
            y, y + 1, 0, alto, tam_filtro, umbral);
    }
    double t2 = obtener_tiempo_seg(); /* Fin de Etapa 2 */

    /* ------------------------------------------------------------------------
     * ETAPA 3: POSTERIZADO POR CANAL RGB
     *
     * Precalculamos la Look-Up Table (LUT) de 256 bytes en el hilo principal.
     * Los 256 bytes caben holgadamente en la cache L1 de cada nucleo.
     * Luego paralelizamos el recorrido de filas por hilos.
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
            /* Acceso directo 1D: indice = (y * ancho + x) * 3 */
            size_t idx = ((size_t)y * ancho + x) * CANALES_RGB;
            img_post[idx + 0] = lut[img_orig[idx + 0]]; /* Canal Rojo */
            img_post[idx + 1] = lut[img_orig[idx + 1]]; /* Canal Verde */
            img_post[idx + 2] = lut[img_orig[idx + 2]]; /* Canal Azul */
        }
    }
    double t3 = obtener_tiempo_seg(); /* Fin de Etapa 3 */

    /* ------------------------------------------------------------------------
     * ETAPA 4: COMBINACION FINAL (Suma saturada de Bordes + Posterizado)
     *
     * Cada hilo toma sus filas y suma componente a componente con tope en 255.
     * ------------------------------------------------------------------------ */
    #pragma omp parallel for default(none) \
        shared(img_bordes, img_post, img_cartoon, ancho, alto) \
        schedule(static)
    for (int y = 0; y < alto; y++) {
        combinar_bordes_y_posterizado_rango(
            img_bordes, img_post, img_cartoon,
            ancho, y, y + 1);
    }
    double t4 = obtener_tiempo_seg(); /* Fin del pipeline completo */

    /* Almacenamos los tiempos individuales de cada fase */
    *t_filtrado = t1 - t0;
    *t_bordes   = t2 - t1;
    *t_post     = t3 - t2;
    *t_suma     = t4 - t3;
}

int main(int argc, char **argv)
{
    /* ------------------------------------------------------------------------
     * 1. VALIDACION DE PARAMETROS DE ENTRADA
     * ------------------------------------------------------------------------ */
    if (argc < 6) {
        fprintf(stderr,
                "Uso: %s <imagen_entrada> <imagen_salida> <filtro: 3|5> <rangos: 3|9> <num_threads> [umbral]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    const char *ruta_entrada = argv[1];          /* Ruta del archivo de imagen de origen */
    const char *ruta_salida  = argv[2];          /* Ruta donde se escribira el resultado cartoon */
    int tam_filtro           = atoi(argv[3]);    /* Tamaño del filtro de vecindad (3 o 5) */
    int rangos_post          = atoi(argv[4]);    /* Niveles de cuantizacion del posterizado (3 o 9) */
    int num_threads          = atoi(argv[5]);    /* Numero de threads OpenMP a utilizar */
    int umbral               = (argc >= 7) ? atoi(argv[6]) : UMBRAL_BORDE_DEF; /* Umbral de borde opcional */

    /* Validacion de parametros algoritmicos y de concurrencia */
    if (!validar_parametros_algoritmo(tam_filtro, rangos_post) || num_threads < 1) {
        fprintf(stderr, "[ERROR] Parametros invalidos (num_threads debe ser >= 1).\n");
        return EXIT_FAILURE;
    }

    /* ------------------------------------------------------------------------
     * 2. CARGA DE LA IMAGEN Y RESERVA DE BUFFERS
     * ------------------------------------------------------------------------ */
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

    /* ------------------------------------------------------------------------
     * 3. LLAMADA AL PIPELINE PARALELO CON OPENMP
     * ------------------------------------------------------------------------ */
    double t_filtrado = 0.0, t_bordes = 0.0, t_post = 0.0, t_suma = 0.0;

    ejecutar_pipeline_openmp(
        img_orig, img_blur, img_bordes, img_post, img_cartoon,
        ancho, alto, tam_filtro, rangos_post, umbral, num_threads,
        &t_filtrado, &t_bordes, &t_post, &t_suma);

    double t_total = t_filtrado + t_bordes + t_post + t_suma;

    /* ------------------------------------------------------------------------
     * 4. GUARDADO DE LA IMAGEN FINAL Y SALIDA DE RESULTADOS
     * ------------------------------------------------------------------------ */
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

    /* ------------------------------------------------------------------------
     * 5. LIBERACION DE RECURSOS
     * ------------------------------------------------------------------------ */
    stbi_image_free(img_orig);
    free(img_blur);
    free(img_bordes);
    free(img_post);
    free(img_cartoon);

    return EXIT_SUCCESS;
}
