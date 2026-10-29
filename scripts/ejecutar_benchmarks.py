#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
============================================================================
Trabajo Practico Final Integrador - Computacion Avanzada (UADER FCyT)
Alumnos: Carballo Tobias, Dacuy Santino

Script: ejecutar_benchmarks.py
Descripcion:
  1. Compila las 4 versiones (Secuencial, OpenMP, MPI e Hibrida).
  2. Verifica la equivalencia matematica exacta (pixel a pixel) entre las
     4 implementaciones para filtros 3x3 y 5x5 y posterizados 3 y 9.
  3. Ejecuta la matriz completa de pruebas del Inciso 5:
       - Imagenes: 800x800, 2000x2000, 5000x5000
       - Filtros: 3x3 y 5x5
       - Posterizado: 3 y 9 valores
       - Configuraciones:
           * Secuencial (1 proceso, 1 thread)
           * OpenMP (2, 4, 8, 16 threads)
           * MPI (2, 4, 8 procesos)
           * Hibrido MPI+OpenMP (2x2, 2x4, 4x2, 4x4)
  4. Calcula Speedup (S = Ts / Tp) y Eficiencia (E = S / p) y exporta
     'results/benchmarks.csv'.
============================================================================
"""

import os
import sys
import csv
import subprocess
import numpy as np
from PIL import Image


def configurar_entorno():
    env = os.environ.copy()
    mingw_bin = r"C:\tmp\toolchain\mingw64\bin"
    msmpi_bin = r"C:\tmp\toolchain\msmpi\runtime_1"
    rutas_extra = [p for p in (mingw_bin, msmpi_bin) if os.path.exists(p)]
    if rutas_extra:
        env["PATH"] = ";".join(rutas_extra) + ";" + env.get("PATH", "")
        os.environ["PATH"] = env["PATH"]
    env["OMP_DYNAMIC"] = "FALSE"
    os.environ["OMP_DYNAMIC"] = "FALSE"
    return env


def compilar_binarios(base_dir, env):
    bin_dir = os.path.join(base_dir, "bin")
    inc_dir = os.path.join(base_dir, "include")
    src_dir = os.path.join(base_dir, "src")
    os.makedirs(bin_dir, exist_ok=True)

    es_win = sys.platform.startswith("win")
    ext = ".exe" if es_win else ""

    gcc_portable = r"C:\tmp\toolchain\mingw64\bin\gcc.exe"
    gcc = gcc_portable if (es_win and os.path.exists(gcc_portable)) else ("gcc.exe" if es_win else "gcc")
    sdk_dir = r"C:\tmp\toolchain\msmpi\sdk"
    msmpi_dll = r"C:\tmp\toolchain\msmpi\runtime_1\msmpi.dll"

    comandos = [
        (
            "cartoon_seq",
            [gcc, "-std=c11", "-O3", "-Wall", "-Wextra", f"-I{inc_dir}",
             os.path.join(src_dir, "cartoon_seq.c"), "-lm",
             "-o", os.path.join(bin_dir, f"cartoon_seq{ext}")]
        ),
        (
            "cartoon_omp",
            [gcc, "-std=c11", "-O3", "-Wall", "-Wextra", "-fopenmp", f"-I{inc_dir}",
             os.path.join(src_dir, "cartoon_omp.c"), "-lm",
             "-o", os.path.join(bin_dir, f"cartoon_omp{ext}")]
        ),
    ]

    if es_win and os.path.exists(sdk_dir) and os.path.exists(msmpi_dll):
        comandos.extend([
            (
                "cartoon_mpi",
                [gcc, "-std=c11", "-O3", "-Wall", "-Wextra", f"-I{inc_dir}", f"-I{sdk_dir}",
                 os.path.join(src_dir, "cartoon_mpi.c"), msmpi_dll, "-lm",
                 "-o", os.path.join(bin_dir, f"cartoon_mpi{ext}")]
            ),
            (
                "cartoon_hybrid",
                [gcc, "-std=c11", "-O3", "-Wall", "-Wextra", "-fopenmp", f"-I{inc_dir}", f"-I{sdk_dir}",
                 os.path.join(src_dir, "cartoon_hybrid.c"), msmpi_dll, "-lm",
                 "-o", os.path.join(bin_dir, f"cartoon_hybrid{ext}")]
            ),
        ])
    else:
        comandos.extend([
            (
                "cartoon_mpi",
                ["mpicc", "-std=c11", "-O3", "-Wall", "-Wextra", f"-I{inc_dir}",
                 os.path.join(src_dir, "cartoon_mpi.c"), "-lm",
                 "-o", os.path.join(bin_dir, f"cartoon_mpi{ext}")]
            ),
            (
                "cartoon_hybrid",
                ["mpicc", "-std=c11", "-O3", "-Wall", "-Wextra", "-fopenmp", f"-I{inc_dir}",
                 os.path.join(src_dir, "cartoon_hybrid.c"), "-lm",
                 "-o", os.path.join(bin_dir, f"cartoon_hybrid{ext}")]
            ),
        ])

    for nombre, cmd in comandos:
        print(f"[*] Compilando {nombre} ...")
        subprocess.run(cmd, env=env, check=True)
    print("[OK] Los 4 ejecutables fueron compilados sin errores ni warnings.\n")


def extraer_metrica_csv(salida_texto):
    for linea in salida_texto.splitlines():
        if linea.startswith("CSV_METRIC,"):
            partes = linea.strip().split(",")
            # CSV_METRIC,PARADIGMA,P,T,RES,FILTRO,RANGOS,T_TOTAL,T_COMP
            return float(partes[7]), float(partes[8])
    raise RuntimeError(f"No se encontro CSV_METRIC en la salida:\n{salida_texto}")


def correr_comando_medido(cmd, env, repeticiones=2):
    tiempos_tot = []
    tiempos_comp = []
    for _ in range(repeticiones):
        res = subprocess.run(cmd, env=env, capture_output=True, text=True, check=True)
        tt, tc = extraer_metrica_csv(res.stdout)
        tiempos_tot.append(tt)
        tiempos_comp.append(tc)
    idx_min = int(np.argmin(tiempos_tot))
    return tiempos_tot[idx_min], tiempos_comp[idx_min]


def verificar_equivalencia_exacta(base_dir, env):
    """Comprueba que SEQ, OMP, MPI e HIBRIDO produzcan exactamente los mismos pixeles."""
    print("[*] Verificando equivalencia pixel a pixel entre las 4 versiones y muestras visuales...")
    es_win = sys.platform.startswith("win")
    ext = ".exe" if es_win else ""
    mpiexec_portable = r"C:\tmp\toolchain\msmpi\runtime_1\mpiexec.exe"
    mpiexec = mpiexec_portable if (es_win and os.path.exists(mpiexec_portable)) else ("mpiexec.exe" if es_win else "mpiexec")

    bin_seq = os.path.join(base_dir, "bin", f"cartoon_seq{ext}")
    bin_omp = os.path.join(base_dir, "bin", f"cartoon_omp{ext}")
    bin_mpi = os.path.join(base_dir, "bin", f"cartoon_mpi{ext}")
    bin_hyb = os.path.join(base_dir, "bin", f"cartoon_hybrid{ext}")

    in_800 = os.path.join(base_dir, "img", "input", "escena_800x800.png")
    out_dir = os.path.join(base_dir, "img", "output")
    os.makedirs(out_dir, exist_ok=True)

    # Generar etapas intermedias para el informe
    etapas_out = os.path.join(out_dir, "etapas_800.png")
    subprocess.run([bin_seq, in_800, etapas_out, "3", "9", "90", "--etapas"], env=env, check=True, capture_output=True)

    for f in (3, 5):
        for r in (3, 9):
            p_seq = os.path.join(out_dir, f"cartoon_800_f{f}_r{r}.png")
            p_omp = os.path.join(out_dir, f"verif_omp_f{f}_r{r}.png")
            p_mpi = os.path.join(out_dir, f"verif_mpi_f{f}_r{r}.png")
            p_hyb = os.path.join(out_dir, f"verif_hyb_f{f}_r{r}.png")

            subprocess.run([bin_seq, in_800, p_seq, str(f), str(r), "90"], env=env, check=True, capture_output=True)
            subprocess.run([bin_omp, in_800, p_omp, str(f), str(r), "4", "90"], env=env, check=True, capture_output=True)
            subprocess.run([mpiexec, "-n", "4", bin_mpi, in_800, p_mpi, str(f), str(r), "90"], env=env, check=True, capture_output=True)
            subprocess.run([mpiexec, "-n", "2", bin_hyb, in_800, p_hyb, str(f), str(r), "2", "90"], env=env, check=True, capture_output=True)

            arr_seq = np.array(Image.open(p_seq))
            arr_omp = np.array(Image.open(p_omp))
            arr_mpi = np.array(Image.open(p_mpi))
            arr_hyb = np.array(Image.open(p_hyb))

            diff_omp = int(np.max(np.abs(arr_seq.astype(int) - arr_omp.astype(int))))
            diff_mpi = int(np.max(np.abs(arr_seq.astype(int) - arr_mpi.astype(int))))
            diff_hyb = int(np.max(np.abs(arr_seq.astype(int) - arr_hyb.astype(int))))

            print(f"    - Config (Filtro {f}x{f}, Posterizado {r} rangos): "
                  f"MaxDiff(OMP)={diff_omp}, MaxDiff(MPI)={diff_mpi}, MaxDiff(HYBRID)={diff_hyb}")
            if diff_omp != 0 or diff_mpi != 0 or diff_hyb != 0:
                raise AssertionError("Diferencia detectada entre implementaciones!")

            # Limpiar archivos temporales de verificacion dejando los cartoon_800_f*_r*.png
            for tmp_p in (p_omp, p_mpi, p_hyb):
                if os.path.exists(tmp_p):
                    os.remove(tmp_p)

    print("[OK] Verificacion exitosa: 0 diferencias de pixel en todas las configuraciones!\n")


def main():
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    env = configurar_entorno()
    compilar_binarios(base_dir, env)
    verificar_equivalencia_exacta(base_dir, env)

    es_win = sys.platform.startswith("win")
    ext = ".exe" if es_win else ""
    mpiexec_portable = r"C:\tmp\toolchain\msmpi\runtime_1\mpiexec.exe"
    mpiexec = mpiexec_portable if (es_win and os.path.exists(mpiexec_portable)) else ("mpiexec.exe" if es_win else "mpiexec")

    bin_seq = os.path.join(base_dir, "bin", f"cartoon_seq{ext}")
    bin_omp = os.path.join(base_dir, "bin", f"cartoon_omp{ext}")
    bin_mpi = os.path.join(base_dir, "bin", f"cartoon_mpi{ext}")
    bin_hyb = os.path.join(base_dir, "bin", f"cartoon_hybrid{ext}")

    resoluciones = [800, 2000, 5000]
    filtros = [3, 5]
    rangos_lista = [3, 9]

    omp_threads_lista = [2, 4, 8, 16]
    mpi_procs_lista = [2, 4, 8]
    hybrid_configs = [(2, 2), (2, 4), (4, 2), (4, 4)]

    resultados = []

    for res in resoluciones:
        ruta_in = os.path.join(base_dir, "img", "input", f"escena_{res}x{res}.png")
        res_str = f"{res}x{res}"
        reps = 3 if res == 800 else 2

        for f in filtros:
            f_str = f"{f}x{f}"
            for r in rangos_lista:
                print(f"=== Benchmarking Imagen {res_str} | Filtro {f_str} | Posterizado {r} rangos ===")

                # 1. Secuencial (Ts de referencia)
                t_seq, tc_seq = correr_comando_medido(
                    [bin_seq, ruta_in, "NONE", str(f), str(r), "90"],
                    env, repeticiones=reps
                )
                print(f"  [SEQ 1x1]          T={t_seq:.6f}s | Speedup=1.00x | Efic=100.0%")
                resultados.append({
                    "paradigma": "SEQ",
                    "procesos": 1,
                    "threads": 1,
                    "workers": 1,
                    "resolucion": res_str,
                    "filtro": f_str,
                    "rangos": r,
                    "tiempo_total": t_seq,
                    "tiempo_computo": tc_seq,
                    "speedup": 1.0,
                    "eficiencia": 1.0,
                })

                # 2. OpenMP (Memoria Compartida)
                for th in omp_threads_lista:
                    tt, tc = correr_comando_medido(
                        [bin_omp, ruta_in, "NONE", str(f), str(r), str(th), "90"],
                        env, repeticiones=reps
                    )
                    sp = t_seq / tt
                    ef = sp / th
                    print(f"  [OMP {th:2d}T]          T={tt:.6f}s | Speedup={sp:.2f}x | Efic={ef*100:.1f}%")
                    resultados.append({
                        "paradigma": "OMP",
                        "procesos": 1,
                        "threads": th,
                        "workers": th,
                        "resolucion": res_str,
                        "filtro": f_str,
                        "rangos": r,
                        "tiempo_total": tt,
                        "tiempo_computo": tc,
                        "speedup": sp,
                        "eficiencia": ef,
                    })

                # 3. MPI (Memoria Distribuida)
                for pr in mpi_procs_lista:
                    tt, tc = correr_comando_medido(
                        [mpiexec, "-n", str(pr), bin_mpi, ruta_in, "NONE", str(f), str(r), "90"],
                        env, repeticiones=reps
                    )
                    sp = t_seq / tt
                    ef = sp / pr
                    print(f"  [MPI {pr:2d}P]          T={tt:.6f}s (Comp={tc:.6f}s) | Speedup={sp:.2f}x | Efic={ef*100:.1f}%")
                    resultados.append({
                        "paradigma": "MPI",
                        "procesos": pr,
                        "threads": 1,
                        "workers": pr,
                        "resolucion": res_str,
                        "filtro": f_str,
                        "rangos": r,
                        "tiempo_total": tt,
                        "tiempo_computo": tc,
                        "speedup": sp,
                        "eficiencia": ef,
                    })

                # 4. Hibrido (MPI + OpenMP)
                for pr, th in hybrid_configs:
                    workers = pr * th
                    tt, tc = correr_comando_medido(
                        [mpiexec, "-n", str(pr), bin_hyb, ruta_in, "NONE", str(f), str(r), str(th), "90"],
                        env, repeticiones=reps
                    )
                    sp = t_seq / tt
                    ef = sp / workers
                    print(f"  [HYB {pr}Px{th}T={workers:2d}W]    T={tt:.6f}s (Comp={tc:.6f}s) | Speedup={sp:.2f}x | Efic={ef*100:.1f}%")
                    resultados.append({
                        "paradigma": "HYBRID",
                        "procesos": pr,
                        "threads": th,
                        "workers": workers,
                        "resolucion": res_str,
                        "filtro": f_str,
                        "rangos": r,
                        "tiempo_total": tt,
                        "tiempo_computo": tc,
                        "speedup": sp,
                        "eficiencia": ef,
                    })

    # Guardar CSV
    results_dir = os.path.join(base_dir, "results")
    os.makedirs(results_dir, exist_ok=True)
    csv_path = os.path.join(results_dir, "benchmarks.csv")
    campos = [
        "paradigma", "procesos", "threads", "workers",
        "resolucion", "filtro", "rangos",
        "tiempo_total", "tiempo_computo", "speedup", "eficiencia"
    ]
    with open(csv_path, mode="w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=campos)
        writer.writeheader()
        for row in resultados:
            writer.writerow({
                "paradigma": row["paradigma"],
                "procesos": row["procesos"],
                "threads": row["threads"],
                "workers": row["workers"],
                "resolucion": row["resolucion"],
                "filtro": row["filtro"],
                "rangos": row["rangos"],
                "tiempo_total": f"{row['tiempo_total']:.6f}",
                "tiempo_computo": f"{row['tiempo_computo']:.6f}",
                "speedup": f"{row['speedup']:.4f}",
                "eficiencia": f"{row['eficiencia']:.4f}",
            })
    print(f"\n[OK] Tabla completa de benchmarks guardada en: {csv_path}")

    # Invocar generacion de graficos
    script_plot = os.path.join(base_dir, "scripts", "graficar_resultados.py")
    subprocess.run([sys.executable, script_plot], check=True)


if __name__ == "__main__":
    main()
