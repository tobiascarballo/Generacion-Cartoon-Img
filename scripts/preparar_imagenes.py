#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
============================================================================
Trabajo Practico Final Integrador - Computacion Avanzada (UADER FCyT)
Alumnos: Carballo Tobias, Dacuy Santino

Script: preparar_imagenes.py
Descripcion:
  Genera el banco de imagenes de prueba en las tres resoluciones exigidas
  por la catedra (800x800, 2000x2000 y 5000x5000) con riqueza cromatica,
  bordes definidos, gradientes suaves y figuras geometricas/organicas
  ideales para apreciar visualmente:
    1) El borroneo de ruido (3x3 y 5x5)
    2) La deteccion de bordes por gradiente de Sobel y umbralado
    3) El posterizado en 3 y 9 rangos por canal RGB
    4) La composicion Cartoon final
============================================================================
"""

import os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter


def generar_escena_paisaje_urbano(tam: int) -> Image.Image:
    """
    Construye una escena de alta definicion escalable a cualquier resolucion
    (800x800, 2000x2000, 5000x5000) con cielo en gradiente, sol, montanias,
    edificios coloridos, arboles y detalles finos.
    """
    # 1. Gradiente suave para cielo y suelo usando NumPy vectorizado
    y = np.linspace(0.0, 1.0, tam, dtype=np.float32)[:, None]
    x = np.linspace(0.0, 1.0, tam, dtype=np.float32)[None, :]

    img = np.zeros((tam, tam, 3), dtype=np.uint8)

    # Cielo crepuscular con transicion tonal rica (ideal para evaluar posterizado 3 vs 9)
    r_cielo = np.clip(40 + 180 * y + 35 * np.sin(3.0 * np.pi * x), 0, 255)
    g_cielo = np.clip(90 + 110 * (1.0 - np.abs(y - 0.45)) + 25 * np.cos(2.0 * np.pi * x), 0, 255)
    b_cielo = np.clip(220 - 140 * y, 0, 255)

    img[:, :, 0] = r_cielo.astype(np.uint8)
    img[:, :, 1] = g_cielo.astype(np.uint8)
    img[:, :, 2] = b_cielo.astype(np.uint8)

    pil_img = Image.fromarray(img, mode="RGB")
    draw = ImageDraw.Draw(pil_img)

    s = tam / 1000.0  # Factor de escala relativo a 1000x1000

    # 2. Sol con halos concentricos (prueba excelente de posterizado y bordes circulares)
    cx, cy = int(720 * s), int(240 * s)
    for rad, col in [
        (int(145 * s), (255, 140, 50)),
        (int(115 * s), (255, 185, 60)),
        (int(85 * s),  (255, 225, 90)),
        (int(55 * s),  (255, 250, 180)),
    ]:
        draw.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], fill=col)

    # 3. Cordillera de montanias en capas (bordes diagonales bien marcados)
    montania_lejana = [
        (0, int(620 * s)),
        (int(180 * s), int(370 * s)),
        (int(360 * s), int(510 * s)),
        (int(550 * s), int(310 * s)),
        (int(760 * s), int(490 * s)),
        (int(910 * s), int(360 * s)),
        (tam, int(540 * s)),
        (tam, tam),
        (0, tam),
    ]
    draw.polygon(montania_lejana, fill=(75, 95, 145))

    # Picos nevados
    nieve_1 = [
        (int(550 * s), int(310 * s)),
        (int(485 * s), int(395 * s)),
        (int(530 * s), int(380 * s)),
        (int(565 * s), int(400 * s)),
        (int(610 * s), int(385 * s)),
    ]
    draw.polygon(nieve_1, fill=(235, 245, 255))

    montania_cercana = [
        (0, int(680 * s)),
        (int(140 * s), int(490 * s)),
        (int(310 * s), int(610 * s)),
        (int(480 * s), int(440 * s)),
        (int(690 * s), int(600 * s)),
        (int(860 * s), int(470 * s)),
        (tam, int(630 * s)),
        (tam, tam),
        (0, tam),
    ]
    draw.polygon(montania_cercana, fill=(45, 115, 95))

    # 4. Pradera y lago en la mitad inferior
    draw.rectangle([0, int(660 * s), tam, tam], fill=(55, 150, 75))
    draw.ellipse([int(120 * s), int(710 * s), int(920 * s), int(960 * s)], fill=(40, 135, 195))
    draw.ellipse([int(160 * s), int(730 * s), int(880 * s), int(935 * s)], fill=(65, 170, 225))

    # 5. Casas / edificios coloridos con techos y ventanas (bordes verticales/horizontales)
    casas = [
        (int(80 * s),  int(560 * s), int(210 * s), int(700 * s), (215, 75, 65),  (120, 35, 30)),
        (int(230 * s), int(530 * s), int(370 * s), int(705 * s), (235, 175, 55), (140, 85, 25)),
        (int(390 * s), int(575 * s), int(510 * s), int(710 * s), (85, 165, 215), (40, 80, 125)),
    ]
    for x0, y0, x1, y1, col_pared, col_techo in casas:
        draw.rectangle([x0, y0, x1, y1], fill=col_pared, outline=(25, 25, 25), width=max(1, int(3 * s)))
        xm = (x0 + x1) // 2
        alto_t = int(65 * s)
        draw.polygon([(x0 - int(12 * s), y0), (xm, y0 - alto_t), (x1 + int(12 * s), y0)],
                     fill=col_techo, outline=(25, 25, 25))
        # Ventanas iluminadas
        paso_x = (x1 - x0) // 3
        paso_y = (y1 - y0) // 3
        for vx in range(x0 + paso_x // 2, x1 - paso_x // 2, paso_x):
            for vy in range(y0 + paso_y // 2, y1 - paso_y // 2, paso_y):
                draw.rectangle(
                    [vx, vy, vx + paso_x // 2, vy + paso_y // 2],
                    fill=(255, 240, 130),
                    outline=(30, 30, 30),
                    width=max(1, int(2 * s)),
                )

    # 6. Arboles en primer plano
    for tx, ty, tr in [
        (int(640 * s), int(650 * s), int(55 * s)),
        (int(750 * s), int(670 * s), int(65 * s)),
        (int(870 * s), int(645 * s), int(50 * s)),
    ]:
        draw.rectangle([tx - int(12 * s), ty, tx + int(12 * s), ty + int(95 * s)], fill=(105, 65, 35))
        draw.ellipse([tx - tr, ty - tr, tx + tr, ty + tr], fill=(35, 125, 55), outline=(20, 70, 30))
        draw.ellipse([tx - int(tr * 0.7), ty - int(tr * 1.3), tx + int(tr * 0.7), ty + int(tr * 0.2)],
                     fill=(55, 165, 70))

    # 7. Agregamos un leve ruido sintetico controlado en un 1.5% de pixeles para que el Paso 1
    #    (Filtrado / Borroneo 3x3 vs 5x5) demuestre su funcion de eliminacion de ruido
    arr = np.array(pil_img, dtype=np.int16)
    rng = np.random.default_rng(seed=42 + tam)
    ruido = rng.normal(0, 6.0, size=arr.shape).astype(np.int16)
    arr = np.clip(arr + ruido, 0, 255).astype(np.uint8)

    return Image.fromarray(arr, mode="RGB")


def main():
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    in_dir = os.path.join(base_dir, "img", "input")
    out_dir = os.path.join(base_dir, "img", "output")
    os.makedirs(in_dir, exist_ok=True)
    os.makedirs(out_dir, exist_ok=True)

    resoluciones = [800, 2000, 5000]
    for res in resoluciones:
        ruta = os.path.join(in_dir, f"escena_{res}x{res}.png")
        print(f"[*] Generando imagen de prueba {res}x{res} -> {ruta} ...")
        img = generar_escena_paisaje_urbano(res)
        # Usamos compress_level=1 para escritura rapida sin perdida en 5000x5000
        img.save(ruta, format="PNG", compress_level=1)
        tam_mb = os.path.getsize(ruta) / (1024 * 1024)
        print(f"    [OK] Guardada ({tam_mb:.2f} MB)")


if __name__ == "__main__":
    main()
