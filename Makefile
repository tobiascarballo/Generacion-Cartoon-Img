# ============================================================================
# Trabajo Practico Final Integrador - Computacion Avanzada (UADER FCyT)
# Alumnos: Carballo Tobias, Dacuy Santino
# Docente: Dra. Maria Fabiana Piccoli
#
# Archivo: Makefile
# Descripcion didactica:
#   Automatiza la construccion estandar para entornos Linux / POSIX o MinGW
#   mediante la herramienta 'make'. Define las reglas de compilacion y dependencias
#   para los 4 binarios del proyecto:
#     1. bin/cartoon_seq    : Version Secuencial de referencia
#     2. bin/cartoon_omp    : Memoria Compartida con OpenMP
#     3. bin/cartoon_mpi    : Memoria Distribuida con MPI
#     4. bin/cartoon_hybrid : Computacion Paralela Hibrida (MPI + OpenMP)
# ============================================================================

# Compilador de C estandar (por defecto gcc si no se especifica otro)
CC       ?= gcc

# Wrapper del compilador MPI para resolver rutas de cabeceras y librerias MPI
MPICC    ?= mpicc

# Banderas de compilacion comunes:
#   -std=c11 : Estandar moderno de lenguaje C11
#   -O3      : Optimizaciones agresivas de bucles, vectorizacion SIMD y funciones inline
#   -Wall    : Activa advertencias principales sobre construcciones sospechosas
#   -Wextra  : Activa advertencias adicionales de buenas practicas
#   -Iinclude: Indica al preprocesador buscar cabeceras en el directorio include/
CFLAGS   ?= -std=c11 -O3 -Wall -Wextra -Iinclude

# Bandera para habilitar las directivas multihilo OpenMP y enlazar libgomp
OMPFLAGS ?= -fopenmp

# Librerias a enlazar (enlace con la biblioteca matematica estandar libm para sqrtf, etc.)
LDLIBS   ?= -lm

# Directorio de destino para los ejecutables generados
BIN_DIR  := bin

# Lista completa de ejecutables objetivo
TARGETS  := $(BIN_DIR)/cartoon_seq \
            $(BIN_DIR)/cartoon_omp \
            $(BIN_DIR)/cartoon_mpi \
            $(BIN_DIR)/cartoon_hybrid

# Reglas fonicas que no corresponden a nombres de archivos reales
.PHONY: all clean dirs

# Regla principal por defecto: crea los directorios y compila todos los objetivos
all: dirs $(TARGETS)

# Creacion de la jerarquia de carpetas del proyecto
dirs:
	mkdir -p $(BIN_DIR) img/input img/output results/plots

# ----------------------------------------------------------------------------
# 1. Regla para la Version Secuencial
# Depende del codigo fuente y de las cabeceras comunes
# ----------------------------------------------------------------------------
$(BIN_DIR)/cartoon_seq: src/cartoon_seq.c include/cartoon_core.h include/imagen_io.h
	$(CC) $(CFLAGS) src/cartoon_seq.c $(LDLIBS) -o $@

# ----------------------------------------------------------------------------
# 2. Regla para la Version Memoria Compartida (OpenMP)
# Se compila con GCC anadiendo la bandera de paralelismo -fopenmp
# ----------------------------------------------------------------------------
$(BIN_DIR)/cartoon_omp: src/cartoon_omp.c include/cartoon_core.h include/imagen_io.h
	$(CC) $(CFLAGS) $(OMPFLAGS) src/cartoon_omp.c $(LDLIBS) -o $@

# ----------------------------------------------------------------------------
# 3. Regla para la Version Memoria Distribuida (MPI)
# Se compila mediante el wrapper mpicc que enlaza el runtime de paso de mensajes
# ----------------------------------------------------------------------------
$(BIN_DIR)/cartoon_mpi: src/cartoon_mpi.c include/cartoon_core.h include/imagen_io.h
	$(MPICC) $(CFLAGS) src/cartoon_mpi.c $(LDLIBS) -o $@

# ----------------------------------------------------------------------------
# 4. Regla para la Version Paralela Hibrida (MPI + OpenMP)
# Combina mpicc con la bandera de multihilo -fopenmp
# ----------------------------------------------------------------------------
$(BIN_DIR)/cartoon_hybrid: src/cartoon_hybrid.c include/cartoon_core.h include/imagen_io.h
	$(MPICC) $(CFLAGS) $(OMPFLAGS) src/cartoon_hybrid.c $(LDLIBS) -o $@

# ----------------------------------------------------------------------------
# Limpieza de binarios generados
# ----------------------------------------------------------------------------
clean:
	rm -rf $(BIN_DIR)/*
