#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
============================================================================
Trabajo Practico Final Integrador - Computacion Avanzada (UADER FCyT)
Alumnos: Carballo Tobias, Dacuy Santino

Script: graficar_resultados.py
Descripcion:
  Lee los resultados de 'results/benchmarks.csv' y genera todos los graficos
  de Tiempo de Ejecucion, Speedup (S = Ts / Tp), Eficiencia (E = S / p) y
  paneles visuales de las etapas del algoritmo Cartoon para el informe
  y la presentacion oral.
============================================================================
"""

import os
import csv
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from PIL import Image


def cargar_csv(ruta_csv):
    filas = []
    with open(ruta_csv, mode="r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            row["procesos"] = int(row["procesos"])
            row["threads"] = int(row["threads"])
            row["workers"] = int(row["workers"])
            row["rangos"] = int(row["rangos"])
            row["tiempo_total"] = float(row["tiempo_total"])
            row["tiempo_computo"] = float(row["tiempo_computo"])
            row["speedup"] = float(row["speedup"])
            row["eficiencia"] = float(row["eficiencia"])
            filas.append(row)
    return filas


def graficar_speedup_eficiencia_openmp(filas, plots_dir):
    """Genera curvas de Speedup y Eficiencia para OpenMP variando threads (1, 2, 4, 8, 16)."""
    fig, axes = plt.subplots(1, 2, figsize=(13, 5.2), dpi=160)
    resoluciones = ["800x800", "2000x2000", "5000x5000"]
    colores = {"800x800": "#1f77b4", "2000x2000": "#ff7f0e", "5000x5000": "#2ca02c"}
    estilos = {"3x3": "-o", "5x5": "--s"}

    # Referencia ideal
    threads_ref = [1, 2, 4, 8, 16]
    axes[0].plot(threads_ref, threads_ref, ":", color="gray", label="Speedup Ideal Lineal")
    axes[1].axhline(1.0, linestyle=":", color="gray", label="Eficiencia Ideal (100%)")
    axes[1].axvline(8, linestyle="-.", color="#bbbbbb", linewidth=1, label="Limite Nucleos Fisicos (8C)")

    for res in resoluciones:
        for filtro in ["3x3", "5x5"]:
            sub = [
                r for r in filas
                if r["paradigma"] in ("SEQ", "OMP")
                and r["resolucion"] == res
                and r["filtro"] == filtro
                and r["rangos"] == 9
            ]
            sub.sort(key=lambda x: x["workers"])
            if not sub:
                continue
            xs = [r["workers"] for r in sub]
            sp = [r["speedup"] for r in sub]
            ef = [r["eficiencia"] for r in sub]
            etiqueta = f"{res} (Filtro {filtro})"
            axes[0].plot(xs, sp, estilos[filtro], color=colores[res], linewidth=2, markersize=6, label=etiqueta)
            axes[1].plot(xs, ef, estilos[filtro], color=colores[res], linewidth=2, markersize=6, label=etiqueta)

    axes[0].set_title("Speedup en Memoria Compartida (OpenMP)", fontsize=12, fontweight="bold")
    axes[0].set_xlabel("Numero de Threads (OpenMP)")
    axes[0].set_ylabel("Speedup (S = Ts / Tp)")
    axes[0].set_xticks(threads_ref)
    axes[0].grid(True, alpha=0.3)
    axes[0].legend(fontsize=8.5)

    axes[1].set_title("Eficiencia en Memoria Compartida (OpenMP)", fontsize=12, fontweight="bold")
    axes[1].set_xlabel("Numero de Threads (OpenMP)")
    axes[1].set_ylabel("Eficiencia (E = S / T)")
    axes[1].set_xticks(threads_ref)
    axes[1].set_ylim(0, 1.15)
    axes[1].grid(True, alpha=0.3)
    axes[1].legend(fontsize=8.5)

    plt.tight_layout()
    plt.savefig(os.path.join(plots_dir, "openmp_speedup_eficiencia.png"), bbox_inches="tight", pad_inches=0.15)
    plt.close(fig)


def graficar_speedup_eficiencia_mpi(filas, plots_dir):
    """Genera curvas de Speedup y Eficiencia para MPI variando procesos (1, 2, 4, 8)."""
    fig, axes = plt.subplots(1, 2, figsize=(13, 5.2), dpi=160)
    resoluciones = ["800x800", "2000x2000", "5000x5000"]
    colores = {"800x800": "#1f77b4", "2000x2000": "#ff7f0e", "5000x5000": "#2ca02c"}
    estilos = {"3x3": "-o", "5x5": "--s"}

    procs_ref = [1, 2, 4, 8]
    axes[0].plot(procs_ref, procs_ref, ":", color="gray", label="Speedup Ideal Lineal")
    axes[1].axhline(1.0, linestyle=":", color="gray", label="Eficiencia Ideal (100%)")

    for res in resoluciones:
        for filtro in ["3x3", "5x5"]:
            sub = [
                r for r in filas
                if r["paradigma"] in ("SEQ", "MPI")
                and r["resolucion"] == res
                and r["filtro"] == filtro
                and r["rangos"] == 9
            ]
            sub.sort(key=lambda x: x["workers"])
            if not sub:
                continue
            xs = [r["workers"] for r in sub]
            sp = [r["speedup"] for r in sub]
            ef = [r["eficiencia"] for r in sub]
            etiqueta = f"{res} (Filtro {filtro})"
            axes[0].plot(xs, sp, estilos[filtro], color=colores[res], linewidth=2, markersize=6, label=etiqueta)
            axes[1].plot(xs, ef, estilos[filtro], color=colores[res], linewidth=2, markersize=6, label=etiqueta)

    axes[0].set_title("Speedup en Memoria Distribuida (MPI)", fontsize=12, fontweight="bold")
    axes[0].set_xlabel("Numero de Procesos MPI (P)")
    axes[0].set_ylabel("Speedup (S = Ts / Tp)")
    axes[0].set_xticks(procs_ref)
    axes[0].grid(True, alpha=0.3)
    axes[0].legend(fontsize=8.5)

    axes[1].set_title("Eficiencia en Memoria Distribuida (MPI)", fontsize=12, fontweight="bold")
    axes[1].set_xlabel("Numero de Procesos MPI (P)")
    axes[1].set_ylabel("Eficiencia (E = S / P)")
    axes[1].set_xticks(procs_ref)
    axes[1].set_ylim(0, 1.15)
    axes[1].grid(True, alpha=0.3)
    axes[1].legend(fontsize=8.5)

    plt.tight_layout()
    plt.savefig(os.path.join(plots_dir, "mpi_speedup_eficiencia.png"), bbox_inches="tight", pad_inches=0.15)
    plt.close(fig)


def graficar_comparativa_paradigmas(filas, plots_dir):
    """Compara Secuencial, OpenMP, MPI e Hibrido para 5000x5000 y 2000x2000."""
    fig, axes = plt.subplots(1, 2, figsize=(13.5, 5.4), dpi=160)

    # Seleccionamos configuraciones de 4, 8 y 16 workers totales en 5000x5000 (Filtro 5x5, Rangos 9)
    sub_5000 = [
        r for r in filas
        if r["resolucion"] == "5000x5000" and r["filtro"] == "5x5" and r["rangos"] == 9
    ]

    etiquetas = []
    tiempos = []
    speedups = []
    eficiencias = []
    colores_bar = []

    mapa_color = {
        "SEQ": "#555555",
        "OMP": "#1f77b4",
        "MPI": "#ff7f0e",
        "HYBRID": "#2ca02c",
    }

    orden_preferido = [
        ("SEQ", 1, 1),
        ("OMP", 1, 4),
        ("MPI", 4, 1),
        ("HYBRID", 2, 2),
        ("OMP", 1, 8),
        ("MPI", 8, 1),
        ("HYBRID", 2, 4),
        ("HYBRID", 4, 2),
        ("OMP", 1, 16),
        ("HYBRID", 4, 4),
    ]

    for par, p, t in orden_preferido:
        match = [r for r in sub_5000 if r["paradigma"] == par and r["procesos"] == p and r["threads"] == t]
        if not match:
            continue
        m = match[0]
        if par == "SEQ":
            lbl = "SEQ\n(1x1)"
        elif par == "OMP":
            lbl = f"OMP\n({t}T)"
        elif par == "MPI":
            lbl = f"MPI\n({p}P)"
        else:
            lbl = f"HIBRIDO\n({p}Px{t}T)"
        etiquetas.append(lbl)
        tiempos.append(m["tiempo_total"])
        speedups.append(m["speedup"])
        eficiencias.append(m["eficiencia"])
        colores_bar.append(mapa_color[par])

    x = np.arange(len(etiquetas))
    bars1 = axes[0].bar(x, speedups, color=colores_bar, edgecolor="black", alpha=0.88)
    axes[0].set_title("Speedup Comparativo (Imagen 5000x5000, Filtro 5x5)", fontsize=11.5, fontweight="bold")
    axes[0].set_xticks(x)
    axes[0].set_xticklabels(etiquetas, fontsize=8.5)
    axes[0].set_ylabel("Speedup (S = Ts / Tp)")
    axes[0].grid(True, axis="y", alpha=0.3)
    for b, v in zip(bars1, speedups):
        axes[0].text(b.get_x() + b.get_width() / 2, v + 0.12, f"{v:.2f}x", ha="center", va="bottom", fontsize=8)

    bars2 = axes[1].bar(x, eficiencias, color=colores_bar, edgecolor="black", alpha=0.88)
    axes[1].set_title("Eficiencia Comparativa (Imagen 5000x5000, Filtro 5x5)", fontsize=11.5, fontweight="bold")
    axes[1].set_xticks(x)
    axes[1].set_xticklabels(etiquetas, fontsize=8.5)
    axes[1].set_ylabel("Eficiencia (E = S / (P * T))")
    axes[1].set_ylim(0, 1.15)
    axes[1].axhline(1.0, linestyle=":", color="gray")
    axes[1].grid(True, axis="y", alpha=0.3)
    for b, v in zip(bars2, eficiencias):
        axes[1].text(b.get_x() + b.get_width() / 2, v + 0.02, f"{v*100:.0f}%", ha="center", va="bottom", fontsize=8)

    plt.tight_layout()
    plt.savefig(os.path.join(plots_dir, "comparativa_paradigmas_5000.png"), bbox_inches="tight", pad_inches=0.15)
    plt.close(fig)


def graficar_impacto_filtro_y_posterizado(filas, plots_dir):
    """Muestra el impacto del tamanio de filtro (3x3 vs 5x5) y rangos de posterizado (3 vs 9)."""
    fig, axes = plt.subplots(1, 2, figsize=(13, 5.0), dpi=160)
    resoluciones = ["800x800", "2000x2000", "5000x5000"]

    combos = [
        ("3x3", 3, "#4c72b0", "Filtro 3x3 | Post 3 rangos"),
        ("3x3", 9, "#64b5cd", "Filtro 3x3 | Post 9 rangos"),
        ("5x5", 3, "#dd8452", "Filtro 5x5 | Post 3 rangos"),
        ("5x5", 9, "#c44e52", "Filtro 5x5 | Post 9 rangos"),
    ]

    x = np.arange(len(resoluciones))
    width = 0.19

    # Panel izquierdo: Tiempo Secuencial
    for idx_c, (filtro, rng, col, lbl) in enumerate(combos):
        vals = []
        for res in resoluciones:
            m = [
                r["tiempo_total"] for r in filas
                if r["paradigma"] == "SEQ" and r["resolucion"] == res and r["filtro"] == filtro and r["rangos"] == rng
            ]
            vals.append(m[0] if m else 0.0)
        axes[0].bar(x + (idx_c - 1.5) * width, vals, width, label=lbl, color=col, edgecolor="black")

    axes[0].set_title("Impacto de Filtro (3x3 vs 5x5) y Posterizado (3 vs 9) - Secuencial", fontsize=11, fontweight="bold")
    axes[0].set_xticks(x)
    axes[0].set_xticklabels(resoluciones)
    axes[0].set_ylabel("Tiempo de Ejecucion (segundos - escala log)")
    axes[0].set_yscale("log")
    axes[0].grid(True, axis="y", alpha=0.3)
    axes[0].legend(fontsize=8.5)

    # Panel derecho: Desglose computo vs comunicacion en MPI e Hibrido (5000x5000, 5x5, 9 rangos)
    sub_comm = [
        r for r in filas
        if r["resolucion"] == "5000x5000" and r["filtro"] == "5x5" and r["rangos"] == 9
        and r["paradigma"] in ("MPI", "HYBRID")
    ]
    lbls_c = []
    t_comp = []
    t_comm = []
    for r in sub_comm:
        if r["paradigma"] == "MPI":
            lbls_c.append(f"MPI {r['procesos']}P")
        else:
            lbls_c.append(f"HIB {r['procesos']}Px{r['threads']}T")
        tc = r["tiempo_computo"]
        tt = r["tiempo_total"]
        t_comp.append(tc)
        t_comm.append(max(0.0, tt - tc))

    xc = np.arange(len(lbls_c))
    axes[1].bar(xc, t_comp, label="Tiempo de Computo Local", color="#2ca02c", edgecolor="black")
    axes[1].bar(xc, t_comm, bottom=t_comp, label="Tiempo de Comunicacion MPI (Scatterv/Halos/Gatherv)", color="#d62728", edgecolor="black")
    axes[1].set_title("Desglose Computo vs Comunicacion (5000x5000, Filtro 5x5)", fontsize=11, fontweight="bold")
    axes[1].set_xticks(xc)
    axes[1].set_xticklabels(lbls_c, fontsize=8.5)
    axes[1].set_ylabel("Tiempo Total (segundos)")
    axes[1].grid(True, axis="y", alpha=0.3)
    axes[1].legend(fontsize=8.5)

    plt.tight_layout()
    plt.savefig(os.path.join(plots_dir, "impacto_filtros_y_comunicacion.png"), bbox_inches="tight", pad_inches=0.15)
    plt.close(fig)


def generar_panel_etapas_visuales(base_dir, plots_dir):
    """Genera una lamina comparativa con las etapas intermedias del proceso Cartoon."""
    in_path = os.path.join(base_dir, "img", "input", "escena_800x800.png")
    out_base = os.path.join(base_dir, "img", "output", "etapas_800.png")
    b1 = out_base + "_1_borroneado.png"
    b2 = out_base + "_2_bordes.png"
    b3 = out_base + "_3_posterizado.png"

    if not (os.path.exists(in_path) and os.path.exists(out_base) and os.path.exists(b1)):
        return

    fig, axes = plt.subplots(1, 5, figsize=(16, 3.6), dpi=160)
    items = [
        (in_path,  "1. Imagen Original\n(RGB 800x800)"),
        (b1,       "2. Paso 1: Filtrado\n(Borroneo Media)"),
        (b2,       "3. Pasos 2 y 3: Bordes\n(Sobel + Umbralado)"),
        (b3,       "4. Rama 2: Posterizado\n(Reduccion Tonal RGB)"),
        (out_base, "5. Resultado Cartoon\n(Suma Bordes + Post)"),
    ]
    for ax, (p, titulo) in zip(axes, items):
        im = Image.open(p)
        ax.imshow(im)
        ax.set_title(titulo, fontsize=9.5, fontweight="bold")
        ax.axis("off")

    plt.tight_layout()
    plt.savefig(os.path.join(plots_dir, "panel_etapas_cartoon.png"), bbox_inches="tight", pad_inches=0.18)
    plt.close(fig)

    # Panel 2x2 de las 4 combinaciones (Filtro 3x3 y 5x5 x Posterizado 3 y 9)
    combos_paths = [
        (os.path.join(base_dir, "img", "output", "cartoon_800_f3_r3.png"), "Filtro 3x3 | Posterizado 3 Rangos"),
        (os.path.join(base_dir, "img", "output", "cartoon_800_f3_r9.png"), "Filtro 3x3 | Posterizado 9 Rangos"),
        (os.path.join(base_dir, "img", "output", "cartoon_800_f5_r3.png"), "Filtro 5x5 | Posterizado 3 Rangos"),
        (os.path.join(base_dir, "img", "output", "cartoon_800_f5_r9.png"), "Filtro 5x5 | Posterizado 9 Rangos"),
    ]
    if all(os.path.exists(p) for p, _ in combos_paths):
        fig2, axes2 = plt.subplots(2, 2, figsize=(9.5, 9.2), dpi=160)
        for ax, (p, tit) in zip(axes2.flat, combos_paths):
            ax.imshow(Image.open(p))
            ax.set_title(tit, fontsize=10.5, fontweight="bold")
            ax.axis("off")
        plt.tight_layout()
        plt.savefig(os.path.join(plots_dir, "comparativa_parametros_cartoon.png"), bbox_inches="tight", pad_inches=0.18)
        plt.close(fig2)


def main():
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    csv_path = os.path.join(base_dir, "results", "benchmarks.csv")
    plots_dir = os.path.join(base_dir, "results", "plots")
    os.makedirs(plots_dir, exist_ok=True)

    if not os.path.exists(csv_path):
        print(f"[ERROR] No existe {csv_path}")
        return

    filas = cargar_csv(csv_path)
    graficar_speedup_eficiencia_openmp(filas, plots_dir)
    graficar_speedup_eficiencia_mpi(filas, plots_dir)
    graficar_comparativa_paradigmas(filas, plots_dir)
    graficar_impacto_filtro_y_posterizado(filas, plots_dir)
    generar_panel_etapas_visuales(base_dir, plots_dir)
    print(f"[OK] Graficos generados exitosamente en: {plots_dir}")


if __name__ == "__main__":
    main()
