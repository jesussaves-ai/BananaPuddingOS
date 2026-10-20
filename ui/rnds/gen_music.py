#!/usr/bin/env python3
# Generates Banana's default menu background music, ui/assets/rnds/sounds/music.wav: a slow four-chord pad in A
# minor, quiet and seamless when looped. banana-ui's Sound mixes it under the menu's chimes (sound.cpp); Settings >
# Display & sound > Background music turns it on and off, and a user theme can point sounds.music at a track of its
# own.
#
#   python3 rnds/gen_music.py ui/assets/rnds/sounds/music.wav
#
# Every frequency is a whole number of cycles over the loop (a multiple of 1/LOOP seconds) and every amplitude
# envelope is periodic, so the last sample meets the first with no click. It is written at 22050 Hz mono (half the
# chimes' 44100, so the file stays small; Sound resamples it once when it loads) as 16-bit PCM, the only kind
# sound.cpp reads.

import math
import struct
import sys
import wave

RATE = 22050
LOOP = 12.0                      # seconds; the pad breathes over one full cycle
N = int(RATE * LOOP)

# A minor, F, C, G: four 3-second chords, each its own voice; sums of sines, low harmonics only so it stays soft
CHORDS = [
    [110.00, 164.81, 220.00, 261.63],   # Am
    [87.31, 130.81, 174.61, 220.00],    # F
    [130.81, 196.00, 261.63, 329.63],   # C
    [98.00, 146.83, 196.00, 246.94],    # G
]
SEG = 1.0 / len(CHORDS)          # each chord's share of the loop (0..1)
DETUNE = 2.0 / LOOP              # two cycles more over the loop: a slow, periodic chorus


def quantize(f):
    # the nearest whole number of cycles over the loop, so the note joins itself exactly at the seam
    return round(f * LOOP) / LOOP


def note(t, f, env):
    # sine with two quiet harmonics; the detuned twin gives the chord movement without breaking the loop
    s = math.sin(2 * math.pi * f * t) + 0.30 * math.sin(2 * math.pi * 2 * f * t) + 0.12 * math.sin(2 * math.pi * 3 * f * t)
    s += 0.85 * math.sin(2 * math.pi * (f + DETUNE) * t)
    return s * env


def main(path):
    buf = [0.0] * N
    for i in range(N):
        t = i / RATE
        u = (t / LOOP) % 1.0
        seg = min(len(CHORDS) - 1, int(u / SEG))  # which chord
        # the chord's envelope: silent at the seam between chords, at its fullest in the middle (a raised cosine)
        x = (u - seg * SEG) / SEG
        env = math.sin(math.pi * x) ** 2
        # a slower breath over the whole loop, periodic too
        env *= 0.80 + 0.20 * math.sin(2 * math.pi * (t / LOOP))
        v = 0.0
        for f in CHORDS[seg]:
            v += note(t, quantize(f), 1.0)
        buf[i] = v * env / len(CHORDS[seg])

    peak = max(1e-9, max(abs(x) for x in buf))
    gain = 0.24 / peak                            # quiet under the chimes
    frames = b"".join(struct.pack("<h", max(-32768, min(32767, int(x * gain * 32767 + (0.5 if x >= 0 else -0.5))))) for x in buf)

    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(frames)
    print("wrote %s: %.1f s, %d Hz, mono, %d bytes" % (path, LOOP, RATE, len(frames)))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "music.wav")
