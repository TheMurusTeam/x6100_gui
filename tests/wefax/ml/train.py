#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
"""
Trains the WEFAX denoiser on the pairs of prepare_pairs.py.

    train.py PAIRS_FOLDER [--out models/wefax_unet.pt] [--steps 30000]

About 1 original in 10 (by name) is kept out of the training to check the
network on pictures it never saw; its error is printed every --check steps
and the best network is saved. Uses the Nvidia card when there is one.
"""
import argparse
import glob
import os
import random
import time
import zlib

import numpy as np
import torch
import torch.nn.functional as F

import wfml
from model import WefaxUNet, run_full


def is_check(path):
    """pairs of about 1 original in 10 are kept for the check"""
    orig = os.path.basename(path).rsplit("_", 1)[0]
    return zlib.crc32(orig.encode()) % 10 == 0


def load(paths, limit):
    rx, orig = [], []
    for p in paths[:limit]:
        with np.load(p) as z:
            rx.append(z["rx"])
            orig.append(z["orig"])
    return rx, orig


def batch(rx, orig, n, size, rng):
    xs, ys = [], []
    for _ in range(n):
        i = rng.randrange(len(rx))
        h, w = rx[i].shape
        y0, x0 = rng.randrange(h - size), rng.randrange(w - size)
        a = rx[i][y0:y0 + size, x0:x0 + size]
        b = orig[i][y0:y0 + size, x0:x0 + size]
        if rng.random() < 0.5:          # pictures are not mirror-symmetric, but
            a, b = a[:, ::-1], b[:, ::-1]  # noise is: more variety for free
        xs.append(a)
        ys.append(b)
    x = torch.from_numpy((255 - np.stack(xs).astype(np.float32)) / 255)[:, None]
    y = torch.from_numpy((np.stack(ys) < 128).astype(np.float32))[:, None]
    return x, y


def loss_fn(logit, y):
    # black pixels and their surroundings (lines, text) count 4 times more
    near = F.max_pool2d(y, 5, stride=1, padding=2)
    w = 1 + 3 * near
    return F.binary_cross_entropy_with_logits(logit, y, weight=w)


def check(model, rx, orig, device):
    errs = []
    for a, b in zip(rx, orig):
        p = run_full(model, a, device)
        errs.append(wfml.gray_error(255 * (1 - p), b))
    return float(np.mean(errs)), float(np.mean([wfml.gray_error(a, b) for a, b in zip(rx, orig)]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pairs")
    ap.add_argument("--out", default=os.path.join(wfml.KIT, "models", "wefax_unet.pt"))
    ap.add_argument("--steps", type=int, default=30000)
    ap.add_argument("--batch", type=int, default=32)
    ap.add_argument("--size", type=int, default=256)
    ap.add_argument("--lr", type=float, default=2e-3)
    ap.add_argument("--base", type=int, default=16, help="channels of the first level (16)")
    ap.add_argument("--check", type=int, default=1000)
    ap.add_argument("--max-pairs", type=int, default=4000, help="pairs kept in memory (2.3 MB each, twice)")
    ap.add_argument("--resume", action="store_true")
    a = ap.parse_args()

    files = sorted(glob.glob(os.path.join(a.pairs, "*.npz")))
    train_f = [f for f in files if not is_check(f)]
    check_f = [f for f in files if is_check(f)]
    if not train_f or not check_f:
        raise SystemExit(f"{len(files)} pairs: too few (some originals are needed for the check too)")
    random.Random(1).shuffle(train_f)
    rx, orig = load(train_f, a.max_pairs)
    crx, corig = load(check_f, 24)
    print(f"{len(rx)} training pairs, {len(crx)} check pairs")

    device = torch.device("cuda" if torch.cuda.is_available() else
                          "mps" if torch.backends.mps.is_available() else "cpu")
    print("device:", device, torch.cuda.get_device_name(0) if device.type == "cuda" else "")
    model = WefaxUNet(a.base).to(device)
    print(f"{sum(p.numel() for p in model.parameters()):,} parameters")
    opt = torch.optim.AdamW(model.parameters(), lr=a.lr, weight_decay=1e-4)
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=a.lr, total_steps=a.steps, pct_start=0.05)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    start, best = 0, 1e9
    if a.resume and os.path.exists(a.out + ".last"):
        ck = torch.load(a.out + ".last", map_location=device)
        model.load_state_dict(ck["model"]); opt.load_state_dict(ck["opt"]); sched.load_state_dict(ck["sched"])
        start, best = ck["step"], ck["best"]
        print(f"resumed at step {start}, best check error {best:.2f}")

    use_amp = device.type == "cuda"
    rng = random.Random(start)
    t0, run_loss = time.time(), 0.0
    for step in range(start, a.steps):
        model.train()
        x, y = batch(rx, orig, a.batch, a.size, rng)
        x, y = x.to(device, non_blocking=True), y.to(device, non_blocking=True)
        with torch.autocast(device.type, dtype=torch.bfloat16, enabled=use_amp):
            logit = model(x)
        loss = loss_fn(logit.float(), y)
        opt.zero_grad(set_to_none=True)
        loss.backward()
        opt.step()
        sched.step()
        run_loss += loss.item()
        if (step + 1) % a.check == 0 or step + 1 == a.steps:
            err, base = check(model, crx, corig, device)
            tag = ""
            if err < best:
                best = err
                torch.save({"model": model.state_dict(), "cfg": model.cfg, "step": step + 1,
                            "check_error": err}, a.out)
                tag = "  saved"
            torch.save({"model": model.state_dict(), "opt": opt.state_dict(), "sched": sched.state_dict(),
                        "step": step + 1, "best": best}, a.out + ".last")
            print(f"step {step + 1:6d}  loss {run_loss / a.check:.4f}  check error {err:5.2f} "
                  f"(received {base:5.2f})  {time.time() - t0:5.0f} s{tag}", flush=True)
            run_loss = 0.0
    print(f"best check error {best:.2f}: {a.out}")


if __name__ == "__main__":
    main()
