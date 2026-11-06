/*
 * ============================================================================
 * Trabajo Practico Final Integrador - Computacion Avanzada (UADER FCyT)
 * Alumnos: Carballo Tobias, Dacuy Santino
 *
 * Archivo: src/cartoon_mpi.c
 * Inciso 2: Programa en Memoria Distribuida con MPI
 *
 * ¿Cual es su objetivo?
 *   Paraleliza el algoritmo Cartoon distribuyendo la imagen entre multiples
 *   procesos independientes mediante paso de mensajes (MPI). Cada proceso
 *   posee su propio espacio de memoria aislado (modelo de memoria distribuida).
 *
 * Conceptos clave de MPI implementados y fundamentados:
 *   - Descomposicion de dominio 1D por bloques de filas:
 *     Cada proceso recibe un bloque continuo de 'm' filas.
 *     Si 'alto' no es divisible por P (como en fotos de 573 o 906 filas), el
 *     resto (alto % P) se reparte entre los primeros procesos:
 *       filas[r] = (alto / P) + (r < (alto % P))
 *     garantizando una diferencia maxima de a lo sumo 1 fila (balance optimo).
 *   - Comunicacion Colectiva con 'MPI_Scatterv' y 'MPI_Gatherv':
 *     Permiten enviar y recibir bloques de distinto tamaño (cantidades y
 *     desplazamientos variables), a diferencia de Scatter/Gather simples.
 *   - Filas Fantasma (Halo / Ghost Rows):
 *     Para calcular convoluciones (Borroneo y Sobel), cada proceso necesita
 *     leer 'radio' filas vecinas que estan en la memoria del proceso superior
 *     (rank-1) o inferior (rank+1). Cada proceso aloja (m + 2*radio) filas:
 *       [0 .. radio-1]           : Halo superior (copia del vecino de arriba)
 *       [radio .. radio + m - 1] : 'm' filas propias
 *       [radio+m .. 2*radio+m-1] : Halo inferior (copia del vecino de abajo)
 *   - 'MPI_Sendrecv' libre de deadlocks:
 *     Empareja el envio de la frontera propia con la recepcion del halo del
 *     vecino en una sola operacion atomica sin riesgo de interbloqueo.
 *   - 'MPI_PROC_NULL' y borde neutro negro (0, 0, 0):
 *     En los extremos (arriba de rank 0 y abajo de rank P-1), el destino es
 *     MPI_PROC_NULL. MPI ignora esa llamada y el buffer (creado con ceros)
 *     mantiene su color negro, cumpliendo la regla de borde neutro de la consigna.
 *   - Dos intercambios de halos obligatorios:
 *     1° Sobre 'local_orig' antes del Borroneo.
 *     2° Sobre 'local_blur' antes de Sobel (Sobel opera sobre filas borroneadas).
 *
 * Compilacion manual:
 *   gcc -std=c11 -O3 -Wall -Wextra -Iinclude src/cartoon_mpi.c -lmsmpi -o bin/cartoon_mpi.exe
 *
 * Ejecucion:
 *   mpiexec -n 4 ./bin/cartoon_mpi.exe foto2.jpg img/output/out_mpi.png 5 9 75
 * ============================================================================
 */

#include <stdio.h>    /* Entrada y salida estandar */
#include <stdlib.h>   /* Funciones de sistema y conversion (atoi, malloc, free) */
#include <stdint.h>   /* Tipos con ancho fijo: uint8_t */
#include <string.h>   /* Manejo de cadenas de caracteres */
#include <mpi.h>      /* API estandar de paso de mensajes (MPI) */

#include "imagen_io.h" /* Funciones compartidas de carga/guardado de imagenes */

/* ----------------------------------------------------------------------------
 * FUNCION: intercambiar_filas_fantasma
 *
 * Intercambia las filas de frontera con los procesos vecino superior ('arriba')
 * e inferior ('abajo') utilizando la primitiva combinada MPI_Sendrecv.
 *
 * Parametros:
 *   buf_local      : Buffer con espacio para (m + 2*radio) filas
 *   m              : Cantidad de filas propias del proceso
 *   bytes_por_fila : Ancho de la imagen * 3 bytes (R, G, B)
 *   radio          : Radio de la convolucion (1 para 3x3; 2 para 5x5)
 *   arriba         : Rango del vecino superior (rank - 1 o MPI_PROC_NULL)
 *   abajo          : Rango del vecino inferior (rank + 1 o MPI_PROC_NULL)
 * ---------------------------------------------------------------------------- */
static void intercambiar_filas_fantasma(
    uint8_t *buf_local,
    int m,
    int bytes_por_fila,
    int radio,
    int arriba,
    int abajo)
{
    /* Cantidad de bytes a transferir por cada halo (radio filas completas) */
    int bytes_halo = radio * bytes_por_fila;

    /* Punteros a las 4 regiones de memoria dentro del buffer local */
    uint8_t *halo_sup      = buf_local;                                          /* Filas [0 .. radio-1] */
    uint8_t *propias_sup   = buf_local + (size_t)radio * bytes_por_fila;         /* Primeras 'radio' filas propias */
    uint8_t *propias_inf   = buf_local + (size_t)m * bytes_por_fila;             /* Ultimas 'radio' filas propias */
    uint8_t *halo_inf      = buf_local + (size_t)(radio + m) * bytes_por_fila;   /* Filas [radio+m .. 2*radio+m-1] */

    /* 1. Paso hacia arriba: Envía las filas propias superiores hacia 'arriba'
     *    y simultaneamente recibe en su halo inferior desde 'abajo'.
     *    Si arriba o abajo es MPI_PROC_NULL, MPI completa la llamada como no-op */
    MPI_Sendrecv(
        propias_sup, bytes_halo, MPI_UNSIGNED_CHAR, arriba, 10,
        halo_inf,    bytes_halo, MPI_UNSIGNED_CHAR, abajo,  10,
        MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    /* 2. Paso hacia abajo: Envía las filas propias inferiores hacia 'abajo'
     *    y simultaneamente recibe en su halo superior desde 'arriba'. */
    MPI_Sendrecv(
        propias_inf, bytes_halo, MPI_UNSIGNED_CHAR, abajo,  20,
        halo_sup,    bytes_halo, MPI_UNSIGNED_CHAR, arriba, 20,
        MPI_COMM_WORLD, MPI_STATUS_IGNORE);
}

int main(int argc, char **argv)
{
    /* ------------------------------------------------------------------------
     * 1. INICIALIZACION DEL ENTORNO MPI
     * ------------------------------------------------------------------------ */
    int rank = 0; /* Identificador unico del proceso actual (0, 1, ..., p-1) */
    int p = 1;    /* Cantidad total de procesos en ejecucion */

    MPI_Init(&argc, &argv);                 /* Inicializa el sistema de paso de mensajes */
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);  /* Obtiene el rango (ID) de este proceso */
    MPI_Comm_size(MPI_COMM_WORLD, &p);     /* Obtiene el numero total de procesos (P) */

    int valido = 1;                         /* Bandera de validacion de argumentos */
    int tam_filtro = 3;                     /* Tamaño de la mascara (3 o 5) */
    int rangos_post = 3;                    /* Niveles de posterizado (3 o 9) */
    int umbral = UMBRAL_BORDE_DEF;          /* Umbral para clasificar bordes (75) */
    int ancho = 0, alto = 0;                /* Dimensiones de la imagen */
    uint8_t *img_orig = NULL;               /* Buffer con la imagen completa (solo en rank 0) */
    uint8_t *img_cartoon = NULL;            /* Buffer con la imagen final (solo en rank 0) */

    /* ------------------------------------------------------------------------
     * 2. PROCESO RAIZ (RANK 0): LECTURA Y VALIDACION DE ENTRADA
     * ------------------------------------------------------------------------ */
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

    /* ------------------------------------------------------------------------
     * 3. DIFUSION DE PARAMETROS (MPI_Bcast)
     * ------------------------------------------------------------------------
     * El proceso raiz envia las dimensiones y opciones a todos los demas rangos
     */
    MPI_Bcast(&valido,      1, MPI_INT, 0, MPI_COMM_WORLD);
    if (!valido) {
        if (img_orig) stbi_image_free(img_orig);
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    MPI_Bcast(&ancho,       1, MPI_INT, 0, MPI_COMM_WORLD); /* Difunde W */
    MPI_Bcast(&alto,        1, MPI_INT, 0, MPI_COMM_WORLD); /* Difunde H */
    MPI_Bcast(&tam_filtro,  1, MPI_INT, 0, MPI_COMM_WORLD); /* Difunde tamaño de filtro */
    MPI_Bcast(&rangos_post, 1, MPI_INT, 0, MPI_COMM_WORLD); /* Difunde niveles posterizado */
    MPI_Bcast(&umbral,      1, MPI_INT, 0, MPI_COMM_WORLD); /* Difunde umbral de bordes */

    /* ------------------------------------------------------------------------
     * 4. CALCULO DEL PARTICIONADO EQUILIBRADO DE FILAS
     * ------------------------------------------------------------------------
     * Se preparan los arreglos 'cant' (bytes por proceso) y 'desp' (offsets)
     * para las operaciones vectoriales MPI_Scatterv y MPI_Gatherv.
     */
    int bytes_por_fila = ancho * CANALES_RGB;
    int radio = tam_filtro / 2;             /* Radio: 1 para filtro 3x3, 2 para 5x5 */

    int *filas  = (int *)reservar_memoria(p, sizeof(int)); /* Filas que recibe cada proceso */
    int *inicio = (int *)reservar_memoria(p, sizeof(int)); /* Fila inicial de cada proceso */
    int *cant   = (int *)reservar_memoria(p, sizeof(int)); /* Total de bytes por proceso */
    int *desp   = (int *)reservar_memoria(p, sizeof(int)); /* Desplazamiento en bytes desde inicio */

    int offset_filas = 0;
    for (int r = 0; r < p; r++) {
        /* Reparto equitativo: si alto no es multiplo de p, los primeros procesos reciben 1 fila extra */
        filas[r]  = (alto / p) + (r < (alto % p));
        inicio[r] = offset_filas;
        cant[r]   = filas[r] * bytes_por_fila;
        desp[r]   = offset_filas * bytes_por_fila;
        offset_filas += filas[r];
    }

    /* Filas asignadas a este proceso en particular */
    int m = filas[rank];
    int filas_locales_totales = m + 2 * radio; /* Filas propias + 2 halos */
    size_t bytes_buffer_local = (size_t)filas_locales_totales * (size_t)bytes_por_fila;

    /* Reserva de memoria para los buffers locales inicializados en 0 (calloc) */
    uint8_t *local_orig    = (uint8_t *)reservar_memoria(bytes_buffer_local, sizeof(uint8_t));
    uint8_t *local_blur    = (uint8_t *)reservar_memoria(bytes_buffer_local, sizeof(uint8_t));
    uint8_t *local_bordes  = (uint8_t *)reservar_memoria(bytes_buffer_local, sizeof(uint8_t));
    uint8_t *local_post    = (uint8_t *)reservar_memoria(bytes_buffer_local, sizeof(uint8_t));
    uint8_t *local_cartoon = (uint8_t *)reservar_memoria(bytes_buffer_local, sizeof(uint8_t));

    /* El proceso raiz reserva el buffer para recibir la imagen final ensamblada */
    if (rank == 0) {
        img_cartoon = (uint8_t *)reservar_memoria((size_t)alto * bytes_por_fila, sizeof(uint8_t));
    }

    /* Determinacion de vecinos logicos en la topologia lineal de procesos */
    int arriba = (rank > 0)     ? (rank - 1) : MPI_PROC_NULL;
    int abajo  = (rank < p - 1) ? (rank + 1) : MPI_PROC_NULL;

    /* Sincronizacion de procesos antes de iniciar el cronometraje */
    MPI_Barrier(MPI_COMM_WORLD);
    double t_inicio_total = MPI_Wtime();

    /* ------------------------------------------------------------------------
     * 5. DISPERSION DE DATOS (MPI_Scatterv)
     * ------------------------------------------------------------------------
     * La raiz envia a cada proceso su bloque de 'm' filas.
     * En el buffer local se almacena dejando un margen de 'radio' filas arriba
     * para el halo superior: 'local_orig + radio * bytes_por_fila'.
     */
    double t_comm0 = MPI_Wtime();
    MPI_Scatterv(
        img_orig, cant, desp, MPI_UNSIGNED_CHAR,
        local_orig + (size_t)radio * bytes_por_fila, cant[rank], MPI_UNSIGNED_CHAR,
        0, MPI_COMM_WORLD);

    /* PRIMER INTERCAMBIO DE HALOS: Imagen original (antes del borroneo) */
    intercambiar_filas_fantasma(local_orig, m, bytes_por_fila, radio, arriba, abajo);
    double t_comm1 = MPI_Wtime();

    /* ------------------------------------------------------------------------
     * 6. ETAPA 1 LOCAL: FILTRADO / BORRONEO
     * ------------------------------------------------------------------------
     * Procesa las filas locales [radio .. radio + m) leyendo los halos adyacentes.
     */
    double t_comp0 = MPI_Wtime();
    paso1_filtrado_borroneo_rango(
        local_orig, local_blur, ancho,
        radio, radio + m, 0, filas_locales_totales, tam_filtro);
    double t_comp1 = MPI_Wtime();

    /* SEGUNDO INTERCAMBIO DE HALOS: Imagen borroneada (antes de Sobel)
     * Sobel necesita leer las filas vecinas ya suavizadas para estimar el gradiente */
    double t_comm2 = MPI_Wtime();
    intercambiar_filas_fantasma(local_blur, m, bytes_por_fila, radio, arriba, abajo);
    double t_comm3 = MPI_Wtime();

    /* ------------------------------------------------------------------------
     * 7. ETAPAS 2, 3 Y COMBINACION LOCAL (Sin comunicacion adicional)
     * ------------------------------------------------------------------------ */
    double t_comp2 = MPI_Wtime();
    /* Resaltado con Sobel y binarizacion por umbral */
    paso2y3_resaltado_y_deteccion_rango(
        local_blur, local_bordes, ancho,
        radio, radio + m, 0, filas_locales_totales, tam_filtro, umbral);

    /* Posterizado de la imagen original por canal RGB */
    rama2_posterizado_rango(
        local_orig, local_post, ancho,
        radio, radio + m, rangos_post);

    /* Suma saturada de Bordes + Posterizado */
    combinar_bordes_y_posterizado_rango(
        local_bordes, local_post, local_cartoon,
        ancho, radio, radio + m);
    double t_comp3 = MPI_Wtime();

    /* ------------------------------------------------------------------------
     * 8. RECOLECCION DE RESULTADOS (MPI_Gatherv)
     * ------------------------------------------------------------------------
     * Cada proceso envia sus 'm' filas calculadas (sin los halos) hacia la raiz.
     */
    double t_comm4 = MPI_Wtime();
    MPI_Gatherv(
        local_cartoon + (size_t)radio * bytes_por_fila, cant[rank], MPI_UNSIGNED_CHAR,
        img_cartoon, cant, desp, MPI_UNSIGNED_CHAR,
        0, MPI_COMM_WORLD);
    double t_fin_total = MPI_Wtime();

    /* Calculo de tiempos locales de computo y comunicacion */
    double t_comm_local  = (t_comm1 - t_comm0) + (t_comm3 - t_comm2) + (t_fin_total - t_comm4);
    double t_comp_local  = (t_comp1 - t_comp0) + (t_comp3 - t_comp2);
    double t_total_local = t_fin_total - t_inicio_total;

    /* Reduccion colectiva: obtenemos el tiempo maximo (cuello de botella de la aplicacion) */
    double t_comm_max = 0.0, t_comp_max = 0.0, t_total_max = 0.0;
    MPI_Reduce(&t_comm_local,  &t_comm_max,  1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&t_comp_local,  &t_comp_max,  1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&t_total_local, &t_total_max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    /* ------------------------------------------------------------------------
     * 9. ESCRITURA DE SALIDA (Solo proceso raiz rank 0)
     * ------------------------------------------------------------------------ */
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

    /* ------------------------------------------------------------------------
     * 10. LIBERACION DE MEMORIA Y FINALIZACION DE MPI
     * ------------------------------------------------------------------------ */
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

    MPI_Finalize();                         /* Cierra el entorno MPI de forma ordenada */
    return EXIT_SUCCESS;
}
