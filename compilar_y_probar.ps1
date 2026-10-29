# ============================================================================
# Trabajo Practico Final Integrador - Computacion Avanzada (UADER FCyT)
# Alumnos: Carballo Tobias, Dacuy Santino
#
# Script PowerShell: compilar_y_probar.ps1
# Compila las 4 versiones (Secuencial, OpenMP, MPI e Hibrida) en Windows
# y ejecuta una prueba rapida sobre la imagen de 800x800.
# ============================================================================

$ErrorActionPreference = "Stop"
$BaseDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $BaseDir

if (Test-Path "C:\tmp\toolchain\mingw64\bin") {
    $env:PATH = "C:\tmp\toolchain\mingw64\bin;C:\tmp\toolchain\msmpi\runtime_1;$env:PATH"
}
$env:OMP_DYNAMIC = "FALSE"

New-Item -ItemType Directory -Force -Path "bin", "img\input", "img\output", "results\plots" | Out-Null

Write-Host "[1/4] Compilando version Secuencial (cartoon_seq.exe)..." -ForegroundColor Cyan
gcc.exe -std=c11 -O3 -Wall -Wextra -Iinclude src\cartoon_seq.c -lm -o bin\cartoon_seq.exe

Write-Host "[2/4] Compilando version Memoria Compartida OpenMP (cartoon_omp.exe)..." -ForegroundColor Cyan
gcc.exe -std=c11 -O3 -Wall -Wextra -fopenmp -Iinclude src\cartoon_omp.c -lm -o bin\cartoon_omp.exe

if (Test-Path "C:\tmp\toolchain\msmpi\sdk\mpi.h") {
    Write-Host "[3/4] Compilando version Memoria Distribuida MPI (cartoon_mpi.exe)..." -ForegroundColor Cyan
    gcc.exe -std=c11 -O3 -Wall -Wextra -Iinclude -I"C:\tmp\toolchain\msmpi\sdk" src\cartoon_mpi.c "C:\tmp\toolchain\msmpi\runtime_1\msmpi.dll" -lm -o bin\cartoon_mpi.exe

    Write-Host "[4/4] Compilando version Paralela Hibrida MPI+OpenMP (cartoon_hybrid.exe)..." -ForegroundColor Cyan
    gcc.exe -std=c11 -O3 -Wall -Wextra -fopenmp -Iinclude -I"C:\tmp\toolchain\msmpi\sdk" src\cartoon_hybrid.c "C:\tmp\toolchain\msmpi\runtime_1\msmpi.dll" -lm -o bin\cartoon_hybrid.exe
} else {
    Write-Host "[3/4] Compilando MPI con mpicc..." -ForegroundColor Cyan
    mpicc -std=c11 -O3 -Wall -Wextra -Iinclude src\cartoon_mpi.c -lm -o bin\cartoon_mpi.exe
    Write-Host "[4/4] Compilando Hibrido con mpicc -fopenmp..." -ForegroundColor Cyan
    mpicc -std=c11 -O3 -Wall -Wextra -fopenmp -Iinclude src\cartoon_hybrid.c -lm -o bin\cartoon_hybrid.exe
}

Write-Host "`n[OK] Compilacion finalizada exitosamente. Ejecutando demostracion rapida (800x800)..." -ForegroundColor Green
.\bin\cartoon_seq.exe img\input\escena_800x800.png img\output\demo_seq.png 3 9
.\bin\cartoon_omp.exe img\input\escena_800x800.png img\output\demo_omp.png 3 9 8
mpiexec.exe -n 4 .\bin\cartoon_mpi.exe img\input\escena_800x800.png img\output\demo_mpi.png 3 9
mpiexec.exe -n 2 .\bin\cartoon_hybrid.exe img\input\escena_800x800.png img\output\demo_hybrid.png 3 9 4
