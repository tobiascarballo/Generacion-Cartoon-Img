# ============================================================================
# Trabajo Practico Final Integrador - Computacion Avanzada (UADER FCyT)
# Alumnos: Carballo Tobias, Dacuy Santino
#
# Makefile para compilar las 4 versiones del generador de Cartoons:
#   1. bin/cartoon_seq    : Secuencial
#   2. bin/cartoon_mpi    : Memoria Distribuida (MPI)
#   3. bin/cartoon_omp    : Memoria Compartida (OpenMP)
#   4. bin/cartoon_hybrid : Paralela Hibrida (MPI + OpenMP)
# ============================================================================

CC       ?= gcc
MPICC    ?= mpicc
CFLAGS   ?= -std=c11 -O3 -Wall -Wextra -Iinclude
OMPFLAGS ?= -fopenmp
LDLIBS   ?= -lm

BIN_DIR  := bin

TARGETS  := $(BIN_DIR)/cartoon_seq \
            $(BIN_DIR)/cartoon_omp \
            $(BIN_DIR)/cartoon_mpi \
            $(BIN_DIR)/cartoon_hybrid

.PHONY: all clean dirs

all: dirs $(TARGETS)

dirs:
	mkdir -p $(BIN_DIR) img/input img/output results/plots

# 1. Version Secuencial
$(BIN_DIR)/cartoon_seq: src/cartoon_seq.c include/cartoon_core.h include/imagen_io.h
	$(CC) $(CFLAGS) src/cartoon_seq.c $(LDLIBS) -o $@

# 2. Version Memoria Distribuida (MPI)
$(BIN_DIR)/cartoon_mpi: src/cartoon_mpi.c include/cartoon_core.h include/imagen_io.h
	$(MPICC) $(CFLAGS) src/cartoon_mpi.c $(LDLIBS) -o $@

# 3. Version Memoria Compartida (OpenMP)
$(BIN_DIR)/cartoon_omp: src/cartoon_omp.c include/cartoon_core.h include/imagen_io.h
	$(CC) $(CFLAGS) $(OMPFLAGS) src/cartoon_omp.c $(LDLIBS) -o $@

# 4. Version Paralela Hibrida (MPI + OpenMP)
$(BIN_DIR)/cartoon_hybrid: src/cartoon_hybrid.c include/cartoon_core.h include/imagen_io.h
	$(MPICC) $(CFLAGS) $(OMPFLAGS) src/cartoon_hybrid.c $(LDLIBS) -o $@

clean:
	rm -rf $(BIN_DIR)/*
