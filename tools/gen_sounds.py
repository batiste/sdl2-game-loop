#!/usr/bin/env python3
"""
Generate the sound effects in the 8-bit spirit of the art: demon deaths are
"pops", a falling squeal mixed with a burst of noise; the hero's hurt sound
is a low thud.

  assets/demon-death-1.wav .. 3   three pitches for the common demons
  assets/demon-death-boss.wav     longer and deeper, for the evil lord
  assets/hero-hurt.wav            a low thud when the hero takes a hit
  assets/hero-death.wav           a wobbling squeal sliding down, then the thud of the fall
  assets/level-up.wav             a rising arpeggio with sparkles
  assets/select.wav               two quick notes up, for buttons and choices
  assets/tick.wav                 a tiny blip when the selection moves
  assets/explosion.wav            a bomb: a crack of noise over a falling rumble
  assets/coin.wav                 the classic two notes when gold is picked up
  assets/potion.wav               bubbles going up, when a potion is drunk
  assets/denied.wav               a low buzz: too poor, or nothing to use
  assets/splat.wav                a jar of molasses breaking on the ground
  assets/repel.wav                a soft low whoosh, the beat of the repel amulets
  assets/block.wav                a wooden knock, a blob hitting the shield
  assets/ricochet.wav             a bright ping bending up, a blob bounced off the shield
  assets/heartbeat.wav            lub-dub, when the hero is about to die

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


def dying(seed):
    """A wobbling squeal sliding down, then the thud of the body hitting the
    ground 0.9 s in, when the hero has fallen over (see main.c)."""
    n = int(RATE * 0.85)
    out = []
    phase = 0.0
    for i in range(n):
        t = i / n
        wobble = 1 + 0.06 * math.sin(i / RATE * 2 * math.pi * 11)
        phase += 700 * (90 / 700) ** t * wobble / RATE
        pulse = 1.0 if phase % 1.0 < 0.3 else -1.0
        envelope = min(1.0, i / (RATE * 0.004)) * (1 - t) ** 0.8
        out.append(pulse * envelope * 0.45)
    out += [0.0] * int(RATE * 0.05)
    return out + thud(0.3, seed)


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


def chime(notes, note_seconds, tail_seconds, sparkles, seed):
    """Pulse wave arpeggio going up, the last note ringing with a vibrato,
    then a sprinkle of high blips."""
    rnd = random.Random(seed)
    starts = [k * note_seconds for k in range(len(notes))]
    total = starts[-1] + tail_seconds + 0.05 * sparkles
    out = [0.0] * int(RATE * total)
    for k, (start, freq) in enumerate(zip(starts, notes)):
        last = k == len(notes) - 1
        length = tail_seconds if last else note_seconds * 1.8
        phase = 0.0
        for i in range(int(RATE * length)):
            t = i / RATE
            vibrato = 1 + (0.006 * math.sin(t * 2 * math.pi * 7) if last else 0)
            phase += freq * vibrato / RATE
            pulse = 1.0 if phase % 1.0 < 0.25 else -1.0
            triangle = 4 * abs((phase * 2) % 1.0 - 0.5) - 1  # an octave up
            envelope = min(1.0, t / 0.003) * math.exp(-t / (length * 0.35))
            out[int(RATE * start) + i] += (pulse * 0.6 + triangle * 0.3) * envelope
    for k in range(sparkles):
        start = int(RATE * (starts[-1] + 0.04 + k * 0.05 + rnd.uniform(0, 0.02)))
        freq = rnd.uniform(2000, 3600)
        for i in range(int(RATE * 0.04)):
            if start + i < len(out):
                t = i / RATE
                out[start + i] += math.sin(t * freq * 2 * math.pi) * math.exp(-t / 0.01) * 0.35
    peak = max(abs(s) for s in out)
    return [s / peak * 0.7 for s in out]


def boom(seconds, seed):
    """Noise that gets darker as it fades, over a low sine falling away."""
    rnd = random.Random(seed)
    n = int(RATE * seconds)
    out = []
    smooth = 0.0
    phase = 0.0
    for i in range(n):
        t = i / n
        # a one-pole low-pass closing over time: crack, then rumble
        cutoff = 0.9 * (1 - t) ** 2 + 0.03
        smooth += (rnd.uniform(-1, 1) - smooth) * cutoff
        phase += (90 * (30 / 90) ** t) / RATE
        rumble = math.sin(phase * 2 * math.pi)
        envelope = min(1.0, i / (RATE * 0.002)) * (1 - t) ** 1.6
        out.append(max(-1.0, min(1.0, (smooth * 1.6 + rumble * 0.7) * envelope * 0.8)))
    return out


def bubbles(count, seed):
    """Short sine bloops sliding up, one after the other."""
    rnd = random.Random(seed)
    out = []
    for k in range(count):
        length = int(RATE * rnd.uniform(0.045, 0.07))
        start, phase = 300 + k * 90 + rnd.uniform(-30, 30), 0.0
        for i in range(length):
            t = i / length
            phase += start * (1 + 1.5 * t) / RATE
            out.append(math.sin(phase * 2 * math.pi) * math.sin(t * math.pi) * 0.6)
    return out


def splat(seed):
    """A short muffled burst of noise over a low blub sliding down."""
    rnd = random.Random(seed)
    n = int(RATE * 0.28)
    out = []
    smooth = phase = 0.0
    for i in range(n):
        t = i / n
        smooth += (rnd.uniform(-1, 1) - smooth) * (0.35 * (1 - t) + 0.04)
        phase += 240 * (70 / 240) ** t / RATE
        envelope = min(1.0, i / (RATE * 0.002)) * (1 - t) ** 2
        out.append(max(-1.0, min(1.0, (smooth * 1.4 + math.sin(phase * 2 * math.pi) * 0.6) * envelope * 0.7)))
    return out


def whoosh(seed):
    """A sine dropping an octave under a breath of noise, swelling and fading."""
    rnd = random.Random(seed)
    n = int(RATE * 0.22)
    out = []
    smooth = phase = 0.0
    for i in range(n):
        t = i / n
        smooth += (rnd.uniform(-1, 1) - smooth) * 0.08
        phase += 160 * (80 / 160) ** t / RATE
        envelope = math.sin(t * math.pi) ** 1.5
        out.append((math.sin(phase * 2 * math.pi) * 0.6 + smooth * 1.2) * envelope * 0.6)
    return out


def knock(seed):
    """Two quick hollow knocks: resonant noise and a short low tone."""
    rnd = random.Random(seed)
    out = []
    for hit in range(2):
        n = int(RATE * 0.06)
        smooth = phase = 0.0
        for i in range(n):
            t = i / n
            smooth += (rnd.uniform(-1, 1) - smooth) * 0.3
            phase += (320 - 60 * hit) / RATE
            envelope = (1 - t) ** 3
            out.append((math.sin(phase * 2 * math.pi) * 0.7 + smooth * 0.8) * envelope * (0.8 - 0.3 * hit))
        out += [0.0] * int(RATE * 0.02)
    return out


def ping(seed):
    """A short knock, then a bright tone bending up and ringing out."""
    out = knock(seed)[: int(RATE * 0.03)]
    n = int(RATE * 0.25)
    phase = 0.0
    for i in range(n):
        t = i / n
        phase += (1400 + 900 * min(1.0, t * 4)) / RATE
        tone = math.sin(phase * 2 * math.pi) * 0.7 + math.sin(phase * 4 * math.pi) * 0.2
        out.append(tone * math.exp(-t * 6) * 0.6)
    return out


def heartbeat():
    """Two muffled low thumps, the second softer: lub-dub."""
    out = []
    for beat, (freq, gain) in enumerate(((70, 1.0), (58, 0.7))):
        n = int(RATE * 0.12)
        phase = 0.0
        for i in range(n):
            t = i / n
            phase += freq * (1 - 0.3 * t) / RATE
            envelope = min(1.0, i / (RATE * 0.004)) * (1 - t) ** 2
            out.append(math.sin(phase * 2 * math.pi) * envelope * gain * 0.9)
        if beat == 0:
            out += [0.0] * int(RATE * 0.08)
    return out


def buzz(seconds):
    n = int(RATE * seconds)
    return [(1.0 if (i * 110 / RATE) % 1.0 < 0.5 else -1.0) * 0.35 * (1 - i / n) for i in range(n)]


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
    save(dying(11), "hero-death.wav")
    # C E G C, then G D
    save(chime((523.25, 659.25, 783.99, 1046.5), 0.07, 0.5, 8, 5), "level-up.wav")
    save(chime((783.99, 1174.66), 0.045, 0.14, 0, 6), "select.wav")
    save([s * 0.6 for s in chime((1567.98,), 0.02, 0.03, 0, 7)], "tick.wav")
    save(boom(0.9, 8), "explosion.wav")
    save([s * 0.7 for s in chime((987.77, 1318.51), 0.05, 0.16, 0, 9)], "coin.wav")  # B5 E6
    save(bubbles(5, 10), "potion.wav")
    save(buzz(0.16), "denied.wav")
    save(splat(12), "splat.wav")
    save(whoosh(13), "repel.wav")
    save(knock(14), "block.wav")
    save(ping(15), "ricochet.wav")
    save(heartbeat(), "heartbeat.wav")


if __name__ == "__main__":
    main()
