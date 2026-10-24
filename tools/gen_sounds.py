#!/usr/bin/env python3
"""
Generate the sound effects in the 8-bit spirit of the art: demon deaths are
"pops", a falling squeal mixed with a burst of noise; the hero's hurt sound
is a low thud.

  assets/demon-death-1.wav .. 3   three pitches for the common demons
  assets/demon-death-boss.wav     longer and deeper, for the evil lord
  assets/hero-hurt.wav            a low thud when the hero takes a hit

Usage: python3 tools/gen_sounds.py   (standard library only)
"""

import math
import random
import struct
import wave
from pathlib import Path

RATE = 22050
ROOT = Path(__file__).resolve().parent.parent


def thud(seconds, seed):
    """Low triangle wave bending down, with a short crunch at the start."""
    rnd = random.Random(seed)
    n = int(RATE * seconds)
    out = []
    phase = 0.0
    for i in range(n):
        t = i / n
        freq = 300 * (120 / 300) ** t
        phase += freq / RATE
        triangle = 4 * abs(phase % 1.0 - 0.5) - 1
        crunch = rnd.uniform(-1, 1) * max(0.0, 1 - t * 6)
        envelope = min(1.0, i / (RATE * 0.002)) * (1 - t) ** 1.5
        out.append(max(-1.0, min(1.0, (triangle * 0.8 + crunch * 0.5) * envelope * 0.8)))
    return out


def pop(start_hz, end_hz, seconds, noise, seed):
    rnd = random.Random(seed)
    n = int(RATE * seconds)
    out = []
    phase = 0.0
    held = 0.0
    for i in range(n):
        t = i / n
        # exponential slide down, square wave
        freq = start_hz * (end_hz / start_hz) ** t
        phase += freq / RATE
        square = 1.0 if phase % 1.0 < 0.5 else -1.0
        # noise held for a few samples sounds crunchier, like old consoles
        if i % 3 == 0:
            held = rnd.uniform(-1, 1)
        envelope = min(1.0, i / (RATE * 0.004)) * (1 - t) ** 2.2
        sample = (square * (1 - noise) + held * noise) * envelope
        out.append(max(-1.0, min(1.0, sample * 0.55)))
    return out


def save(samples, name):
    path = ROOT / "assets" / name
    with wave.open(str(path), "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes(b"".join(struct.pack("<h", int(s * 32767)) for s in samples))
    print(f"wrote {path} ({len(samples) / RATE:.2f} s)")


def main():
    for i, (start, end) in enumerate(((620, 140), (540, 120), (700, 170))):
        save(pop(start, end, 0.22, 0.35, i), f"demon-death-{i + 1}.wav")
    boss = pop(300, 40, 1.1, 0.5, 9)
    save(boss, "demon-death-boss.wav")
    save(thud(0.2, 4), "hero-hurt.wav")


if __name__ == "__main__":
    main()
