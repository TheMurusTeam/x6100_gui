#!/usr/bin/env python3
"""Independent SSTV test encoder (12 kHz mono 16-bit WAV): PD50, PD90.
Phase-continuous FM, VIS 8-bit (7 data + even parity), as the published
specs (N7CXI / JL Barber "Proposal for SSTV mode specifications")."""
import sys, wave, numpy as np
from PIL import Image
SR = 12000
MODES = {  # vis, pixel ms
    'pd50': (93, 0.286), 'pd90': (99, 0.532),
}
class FM:
    def __init__(s): s.ph = 0.0; s.out = []; s.t = 0.0; s.n = 0
    def tone(s, hz, ms):
        s.t += ms / 1000.0 * SR
        k = int(round(s.t)) - s.n
        if k <= 0: return
        f = np.full(k, float(hz)) if np.isscalar(hz) else hz
        ph = s.ph + np.cumsum(2 * np.pi * f / SR)
        s.out.append(np.sin(ph)); s.ph = ph[-1] % (2*np.pi); s.n += k
    def pixels(s, vals, px_ms):
        # each pixel its own duration on the continuous time line
        for v in vals: s.tone(1500 + 800 * v / 255.0, px_ms)
def vis(fm, code):
    fm.tone(1900, 300); fm.tone(1200, 10); fm.tone(1900, 300); fm.tone(1200, 30)
    bits = [(code >> i) & 1 for i in range(7)]
    for b in bits + [sum(bits) & 1]: fm.tone(1100 if b else 1300, 30)
    fm.tone(1200, 30)
def main(mode, png, wav, snr=None):
    code, px = MODES[mode]
    im = np.asarray(Image.open(png).convert('RGB').resize((320, 256)), dtype=float)
    R, G, B = im[..., 0], im[..., 1], im[..., 2]
    Y = 0.299*R + 0.587*G + 0.114*B
    RY = np.clip(128 + 0.5*(R - Y)/0.701*1.0, 0, 255)   # (R-Y)/1.402 + 128
    BY = np.clip(128 + 0.5*(B - Y)/0.886*1.0, 0, 255)   # (B-Y)/1.772 + 128
    fm = FM(); fm.tone(1900, 500); vis(fm, code)
    for l in range(128):
        r0, r1 = 2*l, 2*l + 1
        fm.tone(1200, 20.0); fm.tone(1500, 2.080)
        fm.pixels(Y[r0], px)
        fm.pixels((RY[r0] + RY[r1]) / 2, px)
        fm.pixels((BY[r0] + BY[r1]) / 2, px)
        fm.pixels(Y[r1], px)
    fm.tone(1500, 300)
    x = np.concatenate(fm.out) * 0.5
    if snr is not None:   # white noise in 3 kHz, SNR dB
        p = np.mean(x**2); x = x + np.random.default_rng(1).normal(0, np.sqrt(p / 10**(snr/10) * (SR/2) / 3000), len(x))
    x = np.concatenate([np.zeros(SR), x, np.zeros(SR)])
    with wave.open(wav, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((np.clip(x, -1, 1) * 32000).astype('<i2').tobytes())
    print(f'{wav}: {len(x)/SR:.1f} s')
if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]) if len(sys.argv) > 4 else None)
