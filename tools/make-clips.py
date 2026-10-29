#!/usr/bin/env python3
"""Synthesise a clip set.

The sets this makes are not recordings of anything; they are generated here,
which is the only way to have a piano note or a drum in the tree without
inheriting someone else's sample licence.  Run it to make or remake a set:

    tools/make-clips.py piano  clips/piano

The keys are laid out as main.c lays them out: keyloc[] is read straight out
of it, so that a set which varies with position varies the way the keyboard
is actually shaped, left to right.

Only press clips (XX-1) are written.  None of these sounds has a release
worth hearing, and a key going up stays silent unless --fallback-sound is
asked for.
"""

import argparse
import math
import pathlib
import re
import struct
import sys
import wave

import numpy as np

RATE = 44100


def keyboard_layout(main_c):
    """Scancode -> (row, column), from the table in main.c."""
    text = pathlib.Path(main_c).read_text()
    body = re.search(r"static int keyloc\[\]\[32\] = \{(.*?)\n\};", text, re.S)
    if body is None:
        sys.exit("could not find keyloc[] in %s" % main_c)

    layout = {}
    for row, line in enumerate(body.group(1).strip().splitlines()):
        codes = [int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})", line)]
        for col, code in enumerate(codes):
            if code and code not in layout:
                layout[code] = (row, col)
    return layout


def envelope(n, attack, decay, hold=0.0):
    """Linear attack, flat hold, exponential decay, all in seconds."""
    a = max(1, int(attack * RATE))
    h = int(hold * RATE)
    d = max(1, n - a - h)
    return np.concatenate([
        np.linspace(0.0, 1.0, a),
        np.ones(h),
        np.exp(-np.linspace(0.0, 1.0, d) * 5.0 / max(1e-9, decay / 0.2)),
    ])[:n]


def resonate(x, freq, q):
    """One biquad bandpass, for shaping noise into something with a throat."""
    w = 2.0 * math.pi * freq / RATE
    alpha = math.sin(w) / (2.0 * q)
    b = [alpha, 0.0, -alpha]
    a = [1.0 + alpha, -2.0 * math.cos(w), 1.0 - alpha]
    y = np.zeros_like(x)
    for i in range(len(x)):
        y[i] = (b[0] * x[i]
                + b[1] * (x[i - 1] if i else 0.0)
                + b[2] * (x[i - 2] if i > 1 else 0.0)
                - a[1] * (y[i - 1] if i else 0.0)
                - a[2] * (y[i - 2] if i > 1 else 0.0)) / a[0]
    return y


def lowpass(x, cutoff):
    """One pole, to take the glare off a square wave."""
    a = math.exp(-2.0 * math.pi * cutoff / RATE)
    y = np.zeros_like(x)
    prev = 0.0
    for i in range(len(x)):
        prev = (1.0 - a) * x[i] + a * prev
        y[i] = prev
    return y


def tone(f0, dur, partials, decay, inharmonic=0.0):
    t = np.arange(int(dur * RATE)) / RATE
    out = np.zeros_like(t)
    for k in range(1, partials + 1):
        f = f0 * k * (1.0 + inharmonic * k * k)
        if f > RATE / 2.2:
            break
        out += (np.sin(2.0 * math.pi * f * t) / k ** 1.3
                * np.exp(-t * decay * (1.0 + 0.35 * k)))
    return out


def sweep(f_from, f_to, dur, shape=1.0):
    t = np.arange(int(dur * RATE)) / RATE
    k = (t / max(1e-9, t[-1])) ** shape
    f = f_from * (f_to / f_from) ** k
    return np.sin(2.0 * math.pi * np.cumsum(f) / RATE)


def noise(dur, seed):
    return np.random.default_rng(seed).normal(0.0, 1.0, int(dur * RATE))


# ---- the sets -----------------------------------------------------------

PENTATONIC = [0, 2, 4, 7, 9]        # random typing stays consonant


def piano(code, pos, seed):
    step = int(pos * 17)            # left to right, low to high
    semi = PENTATONIC[step % 5] + 12 * (step // 5)
    f0 = 110.0 * 2.0 ** (semi / 12.0)
    n = int(0.55 * RATE)
    body = tone(f0, 0.55, 12, 3.0, inharmonic=0.0004)
    hammer = noise(0.012, seed) * np.exp(-np.linspace(0, 1, int(0.012 * RATE)) * 9)
    out = body[:n]
    out[:len(hammer)] += hammer * 0.25
    return out * envelope(n, 0.004, 0.45)


def drums(code, pos, seed):
    kind = code % 4
    if kind == 0:                                  # kick
        n = int(0.26 * RATE)
        out = sweep(125, 45, 0.26, 0.45) * envelope(n, 0.002, 0.2)
        click = noise(0.008, seed)
        click *= np.exp(-np.linspace(0.0, 1.0, len(click)) * 8.0)
        out[:len(click)] += click * 0.4
    elif kind == 1:                                # snare
        n = int(0.17 * RATE)
        out = resonate(noise(0.17, seed), 1900, 0.8) * 0.9
        out += np.sin(2 * math.pi * 185 * np.arange(n) / RATE) * 0.5
        out *= envelope(n, 0.001, 0.12)
    elif kind == 2:                                # hat
        n = int(0.07 * RATE)
        out = resonate(noise(0.07, seed), 8000, 1.2) * envelope(n, 0.001, 0.045)
    else:                                          # tom
        n = int(0.22 * RATE)
        out = sweep(240, 120, 0.22, 0.6) * envelope(n, 0.002, 0.18)
    return out[:n]


def slapstick(code, pos, seed):
    kind = code % 4
    if kind == 0:                                  # boing
        n = int(0.30 * RATE)
        t = np.arange(n) / RATE
        f = 320.0 * np.exp(-t * 6.0) + 90.0
        out = np.sin(2 * math.pi * np.cumsum(f * (1 + 0.5 * np.sin(2 * math.pi * 11 * t))) / RATE)
        out *= envelope(n, 0.002, 0.22)
    elif kind == 1:                                # slide whistle up
        n = int(0.22 * RATE)
        out = sweep(500, 1900, 0.22, 0.8) * envelope(n, 0.01, 0.18)
    elif kind == 2:                                # slide whistle down
        n = int(0.22 * RATE)
        out = sweep(1900, 500, 0.22, 0.8) * envelope(n, 0.01, 0.18)
    else:                                          # slap
        n = int(0.09 * RATE)
        out = resonate(noise(0.09, seed), 1200, 0.7) * envelope(n, 0.001, 0.05)
    return out[:n]


SETS = {
    "piano": piano,
    "drums": drums,
    "slapstick": slapstick,
}


def write_wav(path, samples):
    peak = float(np.max(np.abs(samples))) or 1.0
    pcm = np.clip(samples / peak * 0.72, -1.0, 1.0)
    data = (pcm * 32767.0).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(data.tobytes())


def main():
    ap = argparse.ArgumentParser(description=__doc__,
            formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("set", choices=sorted(SETS))
    ap.add_argument("outdir")
    ap.add_argument("--main", default="main.c",
            help="where to read the key layout from")
    args = ap.parse_args()

    layout = keyboard_layout(args.main)
    widest = max(col for _, col in layout.values()) or 1
    out = pathlib.Path(args.outdir)
    out.mkdir(parents=True, exist_ok=True)

    make = SETS[args.set]
    for code, (row, col) in sorted(layout.items()):
        pos = col / widest                      # 0 at the left, 1 at the right
        write_wav(out / ("%02x-1.wav" % code), make(code, pos, code))

    print("%s: %d clips in %s" % (args.set, len(layout), out))


if __name__ == "__main__":
    main()
