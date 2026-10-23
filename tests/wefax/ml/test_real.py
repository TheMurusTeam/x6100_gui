#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
"""
The WEFAX denoiser on real faxes: the FAX SETs (a folder each, with the
recording REC_*.wav and the original *.bmp from JOMOC).

    test_real.py SETS_FOLDER [--model models/wefax_unet.pt] [--out results]

Each recording is decoded again by the decoder of the firmware (Denoise
ON), aligned with the original and passed through the network. Printed:
the error (mean gray difference from the original, 0..255) of the received
fax and of the denoised one. Written in --out, for each set: the denoised
fax (PNG) and a side by side of original / received / denoised.
"""
import argparse
import glob
import os
import tempfile

import numpy as np
import torch
from PIL import Image

import wfml
from model import WefaxUNet, run_full


def rows(path):
    """rows of a decoded fax, 0 if there is none (no start tone: 0 rows)"""
    try:
        return wfml.load_gray(path).shape[0]
    except Exception:
        return 0


def decode(wav, out):
    """Auto RX as on the radio; Manual RX if the start tone is not in the recording"""
    wfml.run([wfml.tool("wefax_wav"), wav, out, "--denoise"])
    if rows(out) > 200:
        return "Auto RX"
    wfml.run([wfml.tool("wefax_wav"), wav, out, "--denoise", "--cont"])
    return "Manual RX" if rows(out) > 200 else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sets")
    ap.add_argument("--model", default=os.path.join(wfml.KIT, "models", "wefax_unet.pt"))
    ap.add_argument("--out", default=os.path.join(wfml.KIT, "results"))
    a = ap.parse_args()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    ck = torch.load(a.model, map_location=device)
    model = WefaxUNet(**ck["cfg"]).to(device)
    model.load_state_dict(ck["model"])
    os.makedirs(a.out, exist_ok=True)

    rows = []
    for d in sorted(glob.glob(os.path.join(a.sets, "**", ""), recursive=True)):
        wavs = glob.glob(os.path.join(d, "*.wav"))
        origs = [f for f in glob.glob(os.path.join(d, "*.bmp"))]
        if len(wavs) != 1 or len(origs) != 1:
            continue
        name = os.path.basename(os.path.normpath(d))
        try:
            orig = wfml.load_original(origs[0])
        except ValueError as e:
            print(f"{name}: {e}")
            continue
        with tempfile.TemporaryDirectory() as td:
            pgm = os.path.join(td, "rx.pgm")
            mode = decode(wavs[0], pgm)
            if mode is None:
                print(f"{name}: no fax decoded from {os.path.basename(wavs[0])}")
                continue
            rx = wfml.load_gray(pgm)
        try:
            dx, dy, cmin, cmax = wfml.align(orig, rx)
        except ValueError as e:
            print(f"{name}: {e}")
            continue
        rx = wfml.cut(rx, dx, dy, orig.shape[0])
        p = run_full(model, rx, device)
        ai = (255 * (1 - p)).astype(np.uint8)
        e_rx, e_ai = wfml.gray_error(rx, orig), wfml.gray_error(ai, orig)
        rows.append((name, e_rx, e_ai))
        print(f"{name:24s} {mode:9s} corr {cmax:.2f}  received {e_rx:5.1f}  denoised {e_ai:5.1f}  "
              f"({100 * (e_ai - e_rx) / e_rx:+.0f} %)", flush=True)
        Image.fromarray(ai).save(os.path.join(a.out, name + "_denoised.png"))
        side = np.concatenate([orig, np.full((orig.shape[0], 8), 128, np.uint8), rx,
                               np.full((orig.shape[0], 8), 128, np.uint8), ai], 1)
        Image.fromarray(side).save(os.path.join(a.out, name + "_compare.png"))
    if rows:
        print(f"{'mean':24s} {'':9s} {'':9s} received {np.mean([r[1] for r in rows]):5.1f}  "
              f"denoised {np.mean([r[2] for r in rows]):5.1f}")


if __name__ == "__main__":
    main()
