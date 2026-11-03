#!/usr/bin/env python3
"""Builds the factory impulse responses in Resources/ir.

  python3 tools/make_irs.py /path/to/HybridReverb2-impulse-response-database

Three families:
- PLATE: a modal simulation of a 2 m x 1 m x 0.5 mm steel plate (the EMT 140 format), about 20 000 modes.
  Written for this project.
- DIGITAL: a small 8-line feedback delay network run at a low sample rate with coarse arithmetic, in the spirit of
  late-1980s rack reverbs. Written for this project. Not a capture of any unit.
- ROOM: rooms chosen from the HybridReverb2 database (Institute of Communication Acoustics, Ruhr-Universitaet Bochum,
  CC BY-SA 4.0). They are simulated with tinyAVE, not microphone recordings.

Every file is normalised to the same energy, so changing IR does not change the level much.
"""
import sys
from pathlib import Path

import numpy as np
from scipy import signal

sys.path.insert(0, str(Path(__file__).parent))
import flacio

SR = 48000
OUT = Path(__file__).resolve().parent.parent / "Resources" / "ir"


def normalise(x):
    x = x - np.mean(x, axis=0, keepdims=True)
    return x / np.sqrt(np.sum(x ** 2) / x.shape[1])


def fade_tail(x, ms=60):
    n = int(SR * ms / 1000)
    x[-n:] *= np.cos(np.linspace(0, np.pi / 2, n))[:, None] ** 2
    return x


def trim(x, floor_db=-85.0):
    """Cuts the file where the running energy falls below the floor, relative to the peak energy."""
    e = np.sum(x ** 2, axis=1)
    w = 512
    env = np.convolve(e, np.ones(w) / w, mode="same")
    above = np.nonzero(env > env.max() * 10 ** (floor_db / 10))[0]
    return x[: min(len(x), int(above[-1]) + SR // 20)]


def plate(t_mid, hf=1.0, seed=0, a=2.0, b=1.0, h=0.0005):
    """Kirchhoff plate, simply supported: w_mn = kappa * pi^2 * ((m/a)^2 + (n/b)^2).
    t_mid is the RT60 near 2.5 kHz, where the damper of an EMT 140 is read. Lows ring longer; 10 kHz stays under 1 s."""
    E, rho, nu = 2.0e11, 7850.0, 0.3
    kappa = h * np.sqrt(E / (12 * rho * (1 - nu ** 2)))
    f_max = 15000.0
    m_max = int(a * np.sqrt(2 * np.pi * f_max / kappa) / np.pi)
    n_max = int(b * np.sqrt(2 * np.pi * f_max / kappa) / np.pi)
    m, n = np.meshgrid(np.arange(1, m_max + 1), np.arange(1, n_max + 1), indexing="ij")
    m, n = m.ravel().astype(np.float64), n.ravel().astype(np.float64)
    w = kappa * np.pi ** 2 * ((m / a) ** 2 + (n / b) ** 2)
    keep = w < 2 * np.pi * f_max
    m, n, w = m[keep], n[keep], w[keep]
    f = w / (2 * np.pi)

    def shape(x, y):
        return np.sin(m * np.pi * x / a) * np.sin(n * np.pi * y / b)

    drive = shape(0.72 + 0.03 * seed, 0.41)
    pick_l, pick_r = shape(0.41, 0.63), shape(1.61, 0.29)
    rt_inv = (0.42 + 0.58 * np.sqrt(f / 2500.0)) / t_mid + hf * 1.2 * np.maximum(0.0, (f / 10000.0) ** 2 - 0.0625)
    sigma = 6.9078 * rt_inv
    length = int(SR * min(1.15 * t_mid * 2.3, 9.0))
    p = np.exp((-sigma + 1j * w) / SR).astype(np.complex64)
    z = np.ones_like(p)
    gain = (drive * np.where(f < 12000, 1.0, np.cos((f - 12000) / 3000 * np.pi / 2) ** 2)).astype(np.float32)
    al, ar = (gain * pick_l).astype(np.float32), (gain * pick_r).astype(np.float32)
    out = np.zeros((length, 2), dtype=np.float32)
    print(f"  plate RT {t_mid}: {len(p)} modes, {length / SR:.1f} s", flush=True)
    block = 512
    table = (p[:, None] ** np.arange(block)[None, :]).astype(np.complex64)   # modes x block
    step = (p ** block).astype(np.complex64)
    for i in range(0, length, block):
        r = (z[:, None] * table).real
        k = min(block, length - i)
        out[i:i + k, 0] = (al @ r)[:k]
        out[i:i + k, 1] = (ar @ r)[:k]
        z = z * step
    sos = signal.butter(2, 90.0, "hp", fs=SR, output="sos")     # the suspension and the pickups have no real bass below this
    return fade_tail(normalise(signal.sosfilt(sos, out.astype(np.float64), axis=0)))


def digital(fs, t60, size, bits, damp=0.35, pre_ms=0.0, gate=None, reverse=False, seed=1):
    """8-line FDN with a Householder matrix, run at fs with truncated arithmetic. The state quantiser is what puts the
    grain in the tail, as on the 16-bit fixed-point machines of the period; a low fs leaves the top of the band empty."""
    rng = np.random.default_rng(seed)
    primes = np.array([743, 907, 1097, 1279, 1433, 1609, 1801, 1993], dtype=float)
    lens = (primes * size * fs / 20000.0).astype(int)
    lines = [np.zeros(n) for n in lens]
    pos = np.zeros(8, dtype=int)
    g = 10 ** (-3.0 * lens / fs / t60)
    lp = np.zeros(8)
    ap_len = (np.array([113, 337, 89, 251]) * fs / 20000.0).astype(int)
    ap = [np.zeros(n) for n in ap_len]
    ap_pos = [0, 0, 0, 0]
    q = 2.0 ** (bits - 1)
    n_out = int(fs * (t60 * 1.25 + 0.05))
    out = np.zeros((n_out, 2))
    tap_l = rng.choice([-1.0, 1.0], 8)
    tap_r = rng.choice([-1.0, 1.0], 8)
    for t in range(n_out):
        x = 0.5 if t == 0 else 0.0
        for i in range(4):
            d = ap[i][ap_pos[i]]
            w = x + 0.62 * d
            ap[i][ap_pos[i]] = w
            ap_pos[i] = (ap_pos[i] + 1) % ap_len[i]
            x = d - 0.62 * w
        s = np.array([lines[i][pos[i]] for i in range(8)])
        lp = lp + (1.0 - damp) * (s - lp)
        fb = (lp - 2.0 / 8.0 * lp.sum()) * g
        fb = np.floor((fb + x) * q + 0.5) / q
        for i in range(8):
            lines[i][pos[i]] = fb[i]
            pos[i] = (pos[i] + 1) % lens[i]
        out[t, 0] = tap_l @ s
        out[t, 1] = tap_r @ s
    out = signal.resample_poly(out, SR, fs, axis=0)
    if gate is not None:
        n = int(SR * gate)
        out = out[:n] * (0.45 + 0.55 * rng.random(1))[0]
        out[-int(SR * 0.012):] *= np.linspace(1, 0, int(SR * 0.012))[:, None]
    if reverse:
        # the swell of a "reverse" program: a diffuse tail under a rising envelope, ending abruptly
        n = int(SR * 0.45)
        out = out[:n] * np.linspace(0.02, 1, n)[:, None] ** 1.3
        out[-int(SR * 0.004):] *= np.linspace(1, 0, int(SR * 0.004))[:, None]
    if pre_ms > 0:
        out = np.concatenate([np.zeros((int(SR * pre_ms / 1000), 2)), out], axis=0)
    return fade_tail(normalise(out))


ROOMS = [
    ("Tile Room", "bathroom/music/1m", "4.65 x 3.43 x 2.45 m"),
    ("Living Room", "livingroom/music/2m", "6.65 x 4.90 x 3.50 m"),
    ("Studio", "studio/music/2m", "9.31 x 6.86 x 4.90 m"),
    ("Small Hall", "small_concert_hall/music/4m", "13.30 x 9.80 x 7.00 m"),
    ("Large Hall", "large_concert_hall/music/8m", "26.60 x 19.60 x 14.00 m"),
    ("Huge Hall", "huge_concert_hall/music/16m", "53.20 x 39.20 x 28.00 m"),
    ("Church", "church/speech/8m", "52.36 x 17.56 x 32.20 m"),
    ("Cathedral", "church/music/8m", "52.36 x 17.56 x 32.20 m"),
]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    made = []

    def put(index, name, x):
        fn = f"{index:02d}_{name.lower().replace(' ', '_')}.flac"
        flacio.write(OUT / fn, np.clip(x / max(1.0, np.abs(x).max() * 1.02), -1, 1), SR)
        made.append((name, fn, len(x) / SR))
        print(f"{fn}: {len(x) / SR:.2f} s, {(OUT / fn).stat().st_size / 1024:.0f} KB", flush=True)

    put(0, "Plate Short", plate(1.2))
    put(1, "Plate Medium", plate(2.4, seed=1))
    put(2, "Plate Long", plate(4.5, seed=2))
    put(3, "Plate Bright", plate(3.0, hf=0.25, seed=3))
    put(4, "Digital Hall", digital(31250, 3.2, 1.25, 14, damp=0.30))
    put(5, "Digital Room", digital(31250, 0.9, 0.55, 14, damp=0.25, seed=2))
    put(6, "Digital Long", digital(25000, 9.0, 1.6, 12, damp=0.38, seed=3))
    put(7, "Digital Gate", digital(31250, 2.0, 0.5, 14, damp=0.15, gate=0.24, seed=4))
    put(8, "Digital Reverse", digital(31250, 1.6, 0.6, 14, damp=0.15, reverse=True, seed=5))

    if len(sys.argv) > 1:
        db = Path(sys.argv[1]) / "RIR_Database"
        for i, (name, path, _) in enumerate(ROOMS):
            left = flacio.read(db / path / "left_fl.flac")[:, 0]
            right = flacio.read(db / path / "right_fr.flac")[:, 0]
            n = min(len(left), len(right))
            x = np.stack([left[:n], right[:n]], axis=1).astype(np.float64)
            put(9 + i, name, fade_tail(normalise(trim(x)[: 10 * SR])))

    total = sum((OUT / f).stat().st_size for _, f, _ in made)
    print(f"total {total / 1048576:.1f} MB")


if __name__ == "__main__":
    main()
