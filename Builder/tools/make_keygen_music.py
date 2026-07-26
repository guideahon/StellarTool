"""Genera un chiptune estilo keygen (square/pulse + bass + arp), loopable, CC0.

Sintetizado 100% proceduralmente -> libre de copyright. Salida: keygen.wav
(16-bit PCM, 44100 Hz, ~38.4s, loop perfecto sobre 16 compases).

Uso: python make_keygen_music.py <out.wav>
"""
import struct
import sys
import wave
from math import sin, pi

SR = 44100
BPM = 132
BEAT = 60.0 / BPM
STEP = BEAT / 4.0          # semicorchea
STEPS_PER_BAR = 16
BARS = 16
N_STEPS = STEPS_PER_BAR * BARS

# Notas (Hz) - escala menor natural en La.
NOTE = {
    "A2": 110.00, "C3": 130.81, "E3": 164.81, "G3": 196.00, "A3": 220.00,
    "B3": 246.94, "C4": 261.63, "D4": 293.66, "E4": 329.63, "F4": 349.23,
    "G4": 392.00, "A4": 440.00, "B4": 493.88, "C5": 523.25, "E5": 659.25,
    "0": 0.0,
}

# Progresion (4 acordes x 4 compases) -> raiz del bajo + arpegio.
CHORDS = ["A", "F", "C", "G"]
ARP = {
    "A": ["A3", "C4", "E4", "A4"],
    "F": ["F4", "A4", "C5", "A4"],
    "C": ["C4", "E4", "G4", "C5"],
    "G": ["G3", "B3", "D4", "G4"],
}
BASSROOT = {"A": "A2", "F": "F4", "C": "C3", "G": "G3"}
# Melodia lead (una nota por semicorchea, patron pegajoso repetido por seccion).
LEAD_PAT = ["A4", "0", "E5", "A4", "C5", "0", "B4", "G4",
            "A4", "0", "E4", "G4", "A4", "0", "C5", "E5"]


def pulse(freq, t, duty=0.5):
    if freq <= 0:
        return 0.0
    ph = (t * freq) % 1.0
    return 1.0 if ph < duty else -1.0


def env(pos, length, a=0.005, r=0.04):
    """Envolvente simple ataque/decay por nota."""
    if pos < a:
        return pos / a
    if pos > length - r:
        return max(0.0, (length - pos) / r)
    return 1.0


def render():
    total = int(N_STEPS * STEP * SR)
    buf = [0.0] * total
    for s in range(N_STEPS):
        bar = s // STEPS_PER_BAR
        chord = CHORDS[(bar // 4) % len(CHORDS)]
        start = s * STEP
        n0 = int(start * SR)
        length = STEP
        # lead
        lead = NOTE[LEAD_PAT[s % 16]]
        # arpegio
        arp = NOTE[ARP[chord][s % 4]]
        # bajo: nota en cada corchea
        bass = NOTE[BASSROOT[chord]] if (s % 2 == 0) else 0.0
        for i in range(int(length * SR)):
            t = start + i / SR
            pos = i / SR
            e = env(pos, length)
            v = 0.0
            v += 0.32 * pulse(lead, t, 0.5) * e
            v += 0.18 * pulse(arp, t, 0.25) * env(pos, length, r=0.06)
            if bass > 0:
                v += 0.30 * pulse(bass, t, 0.5) * env(pos, STEP * 2)
            idx = n0 + i
            if idx < total:
                buf[idx] += v
    # normalizar suave + clip
    peak = max(1e-6, max(abs(x) for x in buf))
    g = 0.85 / peak
    return [max(-1.0, min(1.0, x * g)) for x in buf]


def main(out):
    data = render()
    with wave.open(out, "w") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<h", int(x * 32767)) for x in data))
    print(f"wrote {out}  ({len(data)/SR:.1f}s, {len(data)*2} bytes)")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "keygen.wav")
