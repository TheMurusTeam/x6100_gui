# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
"""
Shared code of the WEFAX denoiser kit: pictures, alignment of a received fax
with its original, scores, the C tools.
"""
import os
import platform
import subprocess

import numpy as np
from PIL import Image

KIT = os.path.dirname(os.path.abspath(__file__))
WIDTH = 1809            # pixels of the fax line (decoder)
PIC_H = 1293            # rows of the Northwood pictures


def tool(name):
    """path of a C tool: bin/<name>.exe on Windows, else ../<name> built by make"""
    if platform.system() == "Windows":
        p = os.path.join(KIT, "bin", name + ".exe")
    else:
        p = os.path.join(KIT, "bin", name)
        if not os.path.exists(p):
            p = os.path.join(KIT, "..", name)
    if not os.path.exists(p):
        raise SystemExit(f"{p} not found: see README.md, 'The C tools'")
    return p


def load_original(path):
    """the original (JOMOC, 1720 x 1293, 1 bit) stretched to the fax line:
    uint8 0 black .. 255 white, WIDTH x PIC_H"""
    im = Image.open(path).convert("L")
    if im.size[0] < 1000:
        raise ValueError(f"{path}: {im.size[0]} pixels wide, a preview, not an original")
    return np.array(im.resize((WIDTH, im.size[1]), Image.BOX))


def load_gray(path):
    return np.array(Image.open(path).convert("L"))


def _blur(a, r=2):
    """box blur, separable (stands for a gaussian in the alignment)"""
    k = np.ones(2 * r + 1, np.float32) / (2 * r + 1)
    a = np.apply_along_axis(lambda v: np.convolve(v, k, "same"), 1, a)
    return np.apply_along_axis(lambda v: np.convolve(v, k, "same"), 0, a)


def align(original, received, max_dy=140):
    """(dx, dy, worst correlation, best correlation): the received fax row
    dy + r, column (c + dx) mod WIDTH, is original row r, column c"""
    ob = _blur((255 - original.astype(np.float32)) / 255)
    rb = _blur((255 - received.astype(np.float32)) / 255)
    res = []
    h = original.shape[0]
    for y0 in range(100, h - 150, 250):
        a = ob[y0:y0 + 120]
        A = a - a.mean()
        FA = np.conj(np.fft.rfft2(A, s=(120, 2048)))
        best = None
        for dy in range(0, min(max_dy, received.shape[0] - y0 - 120)):
            b = rb[y0 + dy:y0 + dy + 120]
            B = b - b.mean()
            c = np.fft.irfft2(np.fft.rfft2(B, s=(120, 2048)) * FA, s=(120, 2048))[0]
            k = int(np.argmax(c))
            v = c[k] / np.sqrt((A * A).sum() * (B * B).sum() + 1e-9)
            if best is None or v > best[0]:
                best = (float(v), k if k < 1024 else k - 2048, dy)
        if best is not None:
            res.append(best)
    if not res:
        raise ValueError("received fax too short")
    dy = int(np.median([x[2] for x in res]))
    dx = int(np.median([x[1] for x in res]))
    return dx, dy, min(x[0] for x in res), max(x[0] for x in res)


def cut(received, dx, dy, rows):
    """the part of the received fax that matches the original rows"""
    r = np.roll(received, -dx, axis=1)[dy:dy + rows]
    if r.shape[0] < rows:
        r = np.vstack([r, np.full((rows - r.shape[0], r.shape[1]), 255, r.dtype)])
    return r


def gray_error(a, b, margin=20):
    """mean absolute gray difference (0..255) away from the borders"""
    a = a.astype(np.float32)[margin:-margin, margin:-margin]
    b = b.astype(np.float32)[margin:-margin, margin:-margin]
    return float(np.abs(a - b).mean())


def run(args, **kw):
    return subprocess.run(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, **kw)
