# ============================================================================
# Trabajo Practico Final Integrador - Computacion Avanzada (UADER FCyT)
# Alumnos: Carballo Tobias, Dacuy Santino
# Docente: Dra. Maria Fabiana Piccoli
#
# Script: compilar_y_probar.ps1 (PowerShell para Windows)
#
# Descripcion didactica:
#   Automatiza la compilacion de los 4 ejecutables en Windows bajo MinGW-w64 y MS-MPI,
#   y realiza una ejecucion de prueba sobre una escena para verificar que los cuatro
#   paradigmas funcionen de punta a punta sin errores:
#     1. bin\cartoon_seq.exe    (Secuencial)
#     2. bin\cartoon_omp.exe    (Memoria Compartida - OpenMP)
#     3. bin\cartoon_mpi.exe    (Memoria Distribuida - MPI)
#     4. bin\cartoon_hybrid.exe (Paralela Hibrida - MPI + OpenMP)
# ============================================================================

# Detiene la ejecucion del script si cualquier comando falla con un error
$ErrorActionPreference = "Stop"

# Ubica la ruta del directorio donde reside este script y se posiciona en el
$BaseDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $BaseDir

# Si existe la toolchain de MinGW y MS-MPI local, la agrega temporalmente al PATH del proceso
if (Test-Path "C:\tmp\toolchain\mingw64\bin") {
    $env:PATH = "C:\tmp\toolchain\mingw64\bin;C:\tmp\toolchain\msmpi\runtime_1;$env:PATH"
}

# Desactiva el ajuste dinamico de hilos de OpenMP para que siempre cree los hilos solicitados
$env:OMP_DYNAMIC = "FALSE"

# Crea las carpetas necesarias si todavia no existen
New-Item -ItemType Directory -Force -Path "bin", "img\input", "img\output", "results\plots" | Out-Null

# ----------------------------------------------------------------------------
# 1. Compilacion de la Version Secuencial
# Banderas GCC:
#   -std=c11 : Estandar ISO C11
#   -O3      : Maximo nivel de optimizacion del compilador (vectorizacion, inlining)
#   -Wall -Wextra : Emision de todas las advertencias de buenas practicas
#   -Iinclude : Incluye el directorio de cabeceras (cartoon_core.h, imagen_io.h)
#   -lm      : Vincula la libreria matematica estandar (math.h: sqrtf, etc.)
# ----------------------------------------------------------------------------
Write-Host "[1/4] Compilando version Secuencial (cartoon_seq.exe)..." -ForegroundColor Cyan
gcc.exe -std=c11 -O3 -Wall -Wextra -Iinclude src\cartoon_seq.c -lm -o bin\cartoon_seq.exe

# ----------------------------------------------------------------------------
# 2. Compilacion de la Version Memoria Compartida (OpenMP)
# Banderas adicionales:
#   -fopenmp : Habilita las directivas #pragma omp y vincula la biblioteca libgomp
# ----------------------------------------------------------------------------
Write-Host "[2/4] Compilando version Memoria Compartida OpenMP (cartoon_omp.exe)..." -ForegroundColor Cyan
gcc.exe -std=c11 -O3 -Wall -Wextra -fopenmp -Iinclude src\cartoon_omp.c -lm -o bin\cartoon_omp.exe

# ----------------------------------------------------------------------------
# 3 y 4. Compilacion de las Versiones con Memoria Distribuida (MPI) e Hibrida
# Vincula los encabezados mpi.h y la biblioteca dinamica msmpi.dll de Microsoft MPI
# ----------------------------------------------------------------------------
if (Test-Path "C:\tmp\toolchain\msmpi\sdk\mpi.h") {
    Write-Host "[3/4] Compilando version Memoria Distribuida MPI (cartoon_mpi.exe)..." -ForegroundColor Cyan
    gcc.exe -std=c11 -O3 -Wall -Wextra -Iinclude -I"C:\tmp\toolchain\msmpi\sdk" src\cartoon_mpi.c "C:\tmp\toolchain\msmpi\runtime_1\msmpi.dll" -lm -o bin\cartoon_mpi.exe

    Write-Host "[4/4] Compilando version Paralela Hibrida MPI+OpenMP (cartoon_hybrid.exe)..." -ForegroundColor Cyan
    gcc.exe -std=c11 -O3 -Wall -Wextra -fopenmp -Iinclude -I"C:\tmp\toolchain\msmpi\sdk" src\cartoon_hybrid.c "C:\tmp\toolchain\msmpi\runtime_1\msmpi.dll" -lm -o bin\cartoon_hybrid.exe
} else {
    # Alternativa si se compila con el wrapper mpicc en entornos que lo posean en el PATH
    Write-Host "[3/4] Compilando MPI con mpicc..." -ForegroundColor Cyan
    mpicc -std=c11 -O3 -Wall -Wextra -Iinclude src\cartoon_mpi.c -lm -o bin\cartoon_mpi.exe
    Write-Host "[4/4] Compilando Hibrido con mpicc -fopenmp..." -ForegroundColor Cyan
    mpicc -std=c11 -O3 -Wall -Wextra -fopenmp -Iinclude src\cartoon_hybrid.c -lm -o bin\cartoon_hybrid.exe
}

# ----------------------------------------------------------------------------
# Demostracion rapida de ejecucion y validacion funcional
# ----------------------------------------------------------------------------
Write-Host "`n[OK] Compilacion finalizada exitosamente. Ejecutando demostracion rapida (800x800)..." -ForegroundColor Green

# Ejecucion Secuencial
Write-Host "-> Ejecutando Secuencial..." -ForegroundColor Yellow
.\bin\cartoon_seq.exe img\input\escena_800x800.png img\output\demo_seq.png 3 9

# Ejecucion OpenMP con 8 hilos
Write-Host "-> Ejecutando OpenMP (8 hilos)..." -ForegroundColor Yellow
.\bin\cartoon_omp.exe img\input\escena_800x800.png img\output\demo_omp.png 3 9 8

# Ejecucion MPI con 4 procesos distribuidos
Write-Host "-> Ejecutando MPI (4 procesos)..." -ForegroundColor Yellow
mpiexec.exe -n 4 .\bin\cartoon_mpi.exe img\input\escena_800x800.png img\output\demo_mpi.png 3 9

# Ejecucion Hibrida con 2 procesos MPI y 4 hilos OpenMP por proceso (2 x 4 = 8 nucleos)
Write-Host "-> Ejecutando Hibrida (2 procesos x 4 hilos = 8 cores)..." -ForegroundColor Yellow
mpiexec.exe -n 2 .\bin\cartoon_hybrid.exe img\input\escena_800x800.png img\output\demo_hybrid.png 3 9 4

Write-Host "`n[COMPLETADO] Las 4 versiones generaron sus salidas en img\output\ correctamente." -ForegroundColor Green
