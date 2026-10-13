# make_music.py: three pieces of music for Juraic Field, written note by note here and sounded by calculation
# (no samples, no recordings, no music-making AI). Each file ends where it begins: every sound that rings past the end
# is added to the beginning, so the game can repeat the file without a break.
#   bgm/synth-versus.wav   120 beats a minute, E minor, 64 bars
#   bgm/synth-solo.wav     100 beats a minute, A minor, 40 bars
#   bgm/synth-menu.wav     90 beats a minute, C major, 32 bars
# It writes them into bgm/ beside tools/. The package keeps them as FLAC (lighten.py: the same samples), one
# folder further down, by the scene they are for: bgm/versus, bgm/toko, bgm/menu.
# Run with a Python that has numpy and scipy.
import os
import sys
import wave
import numpy as np
from scipy import signal

SR = 44100
here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(os.path.dirname(here), "bgm")
os.makedirs(out, exist_ok=True)
rng = np.random.default_rng(20261010)

SEMI = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def midi(name):
    n = SEMI[name[0]]
    rest = name[1:]
    while rest[0] in "#b":
        n += 1 if rest[0] == "#" else -1
        rest = rest[1:]
    return 12 * (int(rest) + 1) + n


def hz(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def line(text):
    """'D5:6 F5:2 -:4' -> [(midi or None, sixteenths)]; a bar is 16."""
    notes = []
    for bar in text.split("|"):
        got = [(None if n == "-" else midi(n), int(d)) for n, d in (w.split(":") for w in bar.split())]
        assert sum(d for _, d in got) == 16, bar
        notes += got
    return notes


def lowpass(x, cut):
    b, a = signal.butter(2, min(cut, SR * 0.45) / (SR / 2))
    return signal.lfilter(b, a, x)


def highpass(x, cut):
    b, a = signal.butter(2, cut / (SR / 2), "high")
    return signal.lfilter(b, a, x)


def adsr(n, a, d, s, r):
    e = np.full(n, s, dtype=np.float64)
    na, nd, nr = int(a * SR), int(d * SR), int(r * SR)
    na, nr = min(na, n // 2), min(nr, n // 2)
    e[:na] = np.linspace(0, 1, na, endpoint=False)
    nd = min(nd, n - na - nr)
    if nd > 0:
        e[na:na + nd] = np.linspace(1, s, nd)
    e[n - nr:] *= np.linspace(1, 0, nr)
    return e


def saws(f, n, cents, vib=0.0):
    t = np.arange(n) / SR
    x = np.zeros(n)
    for c in cents:
        ff = f * 2 ** (c / 1200)
        ph = ff * t + rng.uniform()
        if vib:
            ph = ph + vib * ff / 5.5 / (2 * np.pi) * np.sin(2 * np.pi * 5.5 * t) * np.minimum(1, t / 0.25)
        x += 2 * (ph % 1) - 1
    return x / len(cents)


def square(f, n, width=0.5):
    t = np.arange(n) / SR
    return np.where((f * t) % 1 < width, 1.0, -1.0)


# ---------------------------------------------------------------- the voices
def v_lead(m, sec):
    n = int((sec + 0.12) * SR)
    x = saws(hz(m), n, (-7, 0, 7), vib=0.004) + 0.35 * square(hz(m) / 2, n)
    return lowpass(x, 5200) * adsr(n, 0.008, 0.12, 0.7, 0.12)


def v_pluck(m, sec):
    n = int(max(sec, 0.25) * SR)
    t = np.arange(n) / SR
    return lowpass(square(hz(m), n, 0.3) + 0.5 * saws(hz(m), n, (5,)), 3600) * np.exp(-t / 0.11) * np.minimum(1, t / 0.003)


def v_bell(m, sec):
    n = int((sec + 1.2) * SR)
    t = np.arange(n) / SR
    f = hz(m)
    x = np.sin(2 * np.pi * f * t + 1.6 * np.exp(-t / 0.25) * np.sin(2 * np.pi * 2 * f * t))
    return x * np.exp(-t / 0.7) * np.minimum(1, t / 0.004)


def v_bass(m, sec):
    n = int((sec + 0.03) * SR)
    x = saws(hz(m), n, (0,)) + 0.6 * square(hz(m), n)
    return lowpass(x, 620) * adsr(n, 0.004, 0.08, 0.75, 0.03)


def v_soft_bass(m, sec):
    n = int((sec + 0.08) * SR)
    t = np.arange(n) / SR
    f = hz(m)
    x = np.sin(2 * np.pi * f * t) + 0.25 * np.sin(4 * np.pi * f * t) + 0.08 * np.sin(6 * np.pi * f * t)
    return x * adsr(n, 0.012, 0.2, 0.8, 0.08)


def v_pad(m, sec):
    n = int((sec + 0.7) * SR)
    return lowpass(saws(hz(m), n, (-11, -4, 3, 9)), 1700) * adsr(n, 0.35, 0.3, 0.85, 0.7)


def make_drums():
    t = np.arange(int(0.45 * SR)) / SR
    kick = np.sin(2 * np.pi * (45 * t + 110 * 0.03 * (1 - np.exp(-t / 0.03)))) * np.exp(-t / 0.16)
    kick[:60] += np.linspace(0.5, 0, 60) * rng.uniform(-1, 1, 60)
    t = np.arange(int(0.3 * SR)) / SR
    snare = highpass(lowpass(rng.uniform(-1, 1, t.size), 8000), 900) * np.exp(-t / 0.07) + 0.5 * np.sin(2 * np.pi * 190 * t) * np.exp(-t / 0.05)
    t = np.arange(int(0.08 * SR)) / SR
    hat = highpass(rng.uniform(-1, 1, t.size), 7500) * np.exp(-t / 0.018)
    t = np.arange(int(0.3 * SR)) / SR
    open_hat = highpass(rng.uniform(-1, 1, t.size), 7000) * np.exp(-t / 0.09)
    t = np.arange(int(0.12 * SR)) / SR
    rim = (np.sin(2 * np.pi * 820 * t) + 0.4 * highpass(rng.uniform(-1, 1, t.size), 3000)) * np.exp(-t / 0.02)
    return {"kick": kick, "snare": snare, "hat": hat, "open": open_hat, "rim": rim}


DRUM = make_drums()


# ---------------------------------------------------------------- the song as a ring
class Song:
    def __init__(self, bpm, bars):
        self.step = 60.0 / bpm / 4
        self.bars = bars
        self.n = int(round(bars * 16 * self.step * SR))
        self.bus = {}
        self.kicks = []

    def put(self, bus, x, at, gain=1.0, pan=0.0):
        buf = self.bus.setdefault(bus, np.zeros((self.n, 2)))
        start = int(round(at * self.step * SR)) % self.n
        a = (pan + 1) * np.pi / 4
        for ch, g in ((0, np.cos(a)), (1, np.sin(a))):
            pos, left = start, x
            while left.size:
                take = min(left.size, self.n - pos)
                buf[pos:pos + take, ch] += gain * g * left[:take]
                left, pos = left[take:], 0

    def play(self, bus, voice, notes, at, gain=1.0, pan=0.0, hold=1.0, shift=0):
        for m, d in notes:
            if m is not None:
                self.put(bus, voice(m + shift, d * self.step * hold), at, gain, pan)
            at += d

    def drum(self, name, at, gain=1.0, pan=0.0):
        self.put("drums", DRUM[name], at, gain, pan)
        if name == "kick":
            self.kicks.append(at)

    def duck(self, depth, sec):
        g = np.ones(self.n)
        shape = 1 - depth * np.exp(-np.arange(int(sec * 4 * SR)) / (sec * SR))
        for at in self.kicks:
            start = int(round(at * self.step * SR)) % self.n
            idx = (start + np.arange(shape.size)) % self.n
            g[idx] = np.minimum(g[idx], shape)
        return g[:, None]

    def echo(self, x, steps, feedback, taps=6):
        d = int(round(steps * self.step * SR))
        y = np.zeros_like(x)
        for k in range(1, taps + 1):
            r = np.roll(x, k * d, axis=0)
            y += feedback ** k * (r[:, ::-1] if k % 2 else r)
        return y

    def room(self, x, sec):
        t = np.arange(int(sec * 4 * SR)) / SR
        y = np.zeros_like(x)
        for ch in (0, 1):
            ir = np.zeros(self.n)
            tail = rng.normal(size=t.size) * np.exp(-t / sec)
            tail[:int(0.012 * SR)] = 0
            ir[:t.size] = lowpass(tail, 5000) / np.sqrt(np.sum(tail ** 2))
            y[:, ch] = np.fft.irfft(np.fft.rfft(x[:, ch]) * np.fft.rfft(ir), self.n)
        return y

    def save(self, name, mix):
        mix = mix / np.sqrt(np.mean(mix ** 2)) * 0.16
        mix = np.tanh(mix * 1.15) / 1.15
        mix = mix / max(1.0, np.abs(mix).max() / 0.92)
        pcm = np.clip(mix * 32767 + rng.uniform(-0.5, 0.5, mix.shape), -32768, 32767).astype("<i2")
        with wave.open(os.path.join(out, name), "wb") as f:
            f.setnchannels(2)
            f.setsampwidth(2)
            f.setframerate(SR)
            f.writeframes(pcm.tobytes())
        seam = np.abs(pcm[0].astype(int) - pcm[-1].astype(int)).max()
        inside = np.abs(np.diff(pcm.astype(int), axis=0)).max()
        print("%s  %.1f s  peak %.2f  rms %.3f  step at the seam %d (largest step inside %d)" % (name, self.n / SR, np.abs(mix).max(), np.sqrt(np.mean(mix ** 2)), seam, inside))


# chords: bass note, the three notes of the pad and the arpeggio
def chord(root, minor):
    r = midi(root)
    return (r, [r + 24, r + 24 + (3 if minor else 4), r + 31, r + 36])


CH = {"Dm": chord("D2", True), "Bb": chord("Bb1", False), "C": chord("C2", False), "A": chord("A1", False), "Gm": chord("G1", True),
      "F": chord("F1", False), "Am": chord("A1", True), "G": chord("G1", False), "Em": chord("E2", True), "D": chord("D2", False), "B": chord("B1", False)}


def pad_bar(song, name, bar, gain, bars=1):
    for i, m in enumerate(CH[name][1][:3]):
        song.put("pad", v_pad(m, bars * 16 * song.step), bar * 16, gain, (-0.5, 0.0, 0.5)[i])


def arp_bar(song, name, bar, order, every, voice, gain, hold=1.0):
    tones = CH[name][1]
    for k in range(16 // every):
        song.put("arp", voice(tones[order[k % len(order)]], every * song.step * hold), bar * 16 + k * every, gain, (-0.45, 0.45)[k % 2])


# ---------------------------------------------------------------- versus
# Written for 120 beats a minute: the drive comes from sixteenths in the bass and the tune, not from the speed.
def versus():
    s = Song(120, 64)
    A = ["Em", "Em", "C", "D", "Em", "Em", "C", "B"]
    B = ["Am", "Am", "Em", "Em", "C", "D", "B", "B"]
    lead_a = line("E5:3 G5:3 B5:2 A5:2 G5:2 E5:2 G5:2 | F#5:3 G5:3 E5:4 -:2 B4:2 E5:2 | G5:3 E5:3 C6:2 B5:2 G5:2 E5:2 G5:2 | A5:3 F#5:3 D6:4 C6:2 A5:2 F#5:2 |"
                  "B5:3 G5:3 E6:2 D6:2 B5:2 G5:2 B5:2 | A5:3 B5:3 G5:4 -:2 E5:2 G5:2 | C6:3 B5:3 G5:2 E5:4 G5:2 A5:2 | B5:4 F#5:4 D#5:4 F#5:2 B5:2")
    lead_a2 = lead_a[:-11] + line("E6:3 D6:3 C6:2 B5:4 A5:2 G5:2 | F#5:6 D#5:2 B4:8")
    lead_b = line("C6:4 A5:2 E5:2 A5:4 C6:2 E6:2 | D6:2 C6:2 B5:2 A5:2 E5:8 | B5:4 G5:2 E5:2 G5:4 B5:2 E6:2 | D6:2 B5:2 A5:2 G5:2 E5:8 |"
                  "G5:3 C6:3 E6:2 D6:4 C6:2 G5:2 | A5:3 D6:3 F#6:2 E6:4 D6:2 A5:2 | D#6:6 B5:2 F#5:4 B5:4 | B5:2 C#6:2 D#6:2 F#6:2 F#6:8")
    lead_b2 = lead_b[:-9] + line("D#6:4 F#6:4 B5:4 D#6:4 | F#6:4 D#6:2 B5:2 F#5:8")
    # (chords, lead, drums: 1 light, 2 full, 3 a break that builds up, arpeggio an octave up as well)
    plan = [(A, None, 1, False), (A, lead_a, 2, False), (A, lead_a2, 2, False), (B, lead_b, 2, False), (B, lead_b2, 2, True),
            (A, None, 3, False), (A, lead_a, 2, True), (A, lead_a2, 2, True)]
    # the bass in sixteenths: (step, semitones above the root, length)
    bass = ((0, 0, 2), (3, 0, 1), (4, 0, 2), (6, 12, 1), (8, 0, 2), (10, 0, 1), (11, 12, 1), (12, 0, 2), (14, 7, 1), (15, 12, 1))
    for sec, (chords, lead, drums, high) in enumerate(plan):
        for i, name in enumerate(chords):
            bar = sec * 8 + i
            root = CH[name][0] + (12 if CH[name][0] < midi("C2") else 0)
            pad_bar(s, name, bar, 0.17)
            arp_bar(s, name, bar, (0, 2, 1, 2, 3, 2, 1, 2), 1, v_pluck, 0.17 if lead else 0.26)
            if high:
                arp_bar(s, name, bar, (3, 2, 3, 1), 2, lambda m, d: v_pluck(m + 12, d), 0.09)
            if not (drums == 3 and i < 4):
                for at, off, d in bass:
                    s.put("bass", v_bass(root + off, d * s.step * 0.8), bar * 16 + at, 0.52)
            else:
                s.put("bass", v_soft_bass(root, 14 * s.step), bar * 16, 0.5)
            if drums == 1:
                for q in (0, 8) if i < 4 else (0, 4, 8, 12):
                    s.drum("kick", bar * 16 + q, 0.95)
                for q in (2, 6, 10, 14):
                    s.drum("hat", bar * 16 + q, 0.16, 0.3)
            if drums == 2 or (drums == 3 and i >= 4):
                for q in (0, 4, 8, 12) + ((7,) if i % 2 == 1 else ()) + ((15,) if i % 4 == 3 else ()):
                    s.drum("kick", bar * 16 + q, 0.95 if q % 4 == 0 else 0.6)
                for q in (4, 12):
                    s.drum("snare", bar * 16 + q, 0.5)
                for q in range(16):
                    s.drum("hat", bar * 16 + q, 0.07 if q % 4 != 2 else 0.0, 0.3)
                for q in (2, 6, 10, 14):
                    s.drum("open", bar * 16 + q, 0.12, 0.3)
            if drums == 3:
                if i < 4:
                    for q in (4, 12):
                        s.drum("rim", bar * 16 + q, 0.25, -0.2)
                    for q in (2, 6, 10, 14):
                        s.drum("hat", bar * 16 + q, 0.12, 0.3)
                if i == 7:
                    for q in range(8, 16):
                        s.drum("snare", bar * 16 + q, 0.2 + 0.04 * (q - 8))
        if lead:
            s.play("lead", v_lead, lead, sec * 8 * 16, 0.3, hold=0.9)
    duck = s.duck(0.55, 0.12)
    mix = s.bus["drums"] + s.bus["bass"] * duck + s.bus["pad"] * duck
    mix += s.bus["arp"] + 0.35 * s.echo(s.bus["arp"], 3, 0.5)
    mix += s.bus["lead"] + 0.3 * s.echo(s.bus["lead"], 6, 0.45)
    mix += 0.22 * s.room(s.bus["lead"] + s.bus["arp"] + 0.6 * s.bus["pad"] + 0.3 * s.bus["drums"], 0.5)
    s.save("synth-versus.wav", mix)


# ---------------------------------------------------------------- solo play
def solo():
    s = Song(100, 40)
    A = ["Am", "Am", "F", "F", "Dm", "Dm", "G", "Em"]
    B = ["F", "G", "Am", "Am", "F", "G", "Em", "Em"]
    tune_a = line("E5:8 -:4 C5:2 D5:2 | E5:4 G5:4 E5:8 | A5:8 -:4 G5:2 E5:2 | F5:4 E5:4 C5:8 |"
                  "D5:8 -:4 F5:2 A5:2 | G5:4 F5:4 D5:8 | B4:4 D5:4 G5:4 D5:4 | E5:12 -:4")
    tune_b = line("C5:4 F5:4 A5:8 | B4:4 D5:4 G5:8 | C5:4 E5:4 A5:6 G5:2 | E5:12 -:4 |"
                  "A5:4 F5:4 C5:8 | G5:4 D5:4 B4:8 | E5:4 G5:4 B5:8 | B5:4 A5:4 G5:4 E5:4")
    plan = [(A, None, False), (A, tune_a, True), (B, tune_b, True), (A, tune_a, True), (B, None, True)]
    for sec, (chords, tune, drums) in enumerate(plan):
        for i, name in enumerate(chords):
            bar = sec * 8 + i
            root = CH[name][0] + (12 if CH[name][0] < midi("D2") else 0)
            pad_bar(s, name, bar, 0.2)
            arp_bar(s, name, bar, (0, 1, 2, 3, 2, 1, 2, 1), 2, v_pluck, 0.14, hold=1.5)
            for at, off, d in ((0, 0, 6), (8, 0, 4), (12, 7, 4)):
                s.put("bass", v_soft_bass(root + off, d * s.step), bar * 16 + at, 0.5)
            if drums:
                for q in (0, 10):
                    s.drum("kick", bar * 16 + q, 0.7)
                s.drum("rim", bar * 16 + 4, 0.22, -0.2)
                s.drum("rim", bar * 16 + 12, 0.22, -0.2)
                for q in (2, 6, 10, 14):
                    s.drum("hat", bar * 16 + q, 0.09, 0.3)
        if tune:
            s.play("lead", v_bell, tune, sec * 8 * 16, 0.26)
    duck = s.duck(0.3, 0.12)
    mix = s.bus["drums"] + s.bus["bass"] * duck + s.bus["pad"] * duck
    mix += s.bus["arp"] + 0.45 * s.echo(s.bus["arp"], 3, 0.5)
    mix += s.bus["lead"] + 0.4 * s.echo(s.bus["lead"], 6, 0.5)
    mix += 0.4 * s.room(s.bus["lead"] + s.bus["arp"] + s.bus["pad"] + 0.3 * s.bus["drums"], 0.9)
    s.save("synth-solo.wav", mix)


# ---------------------------------------------------------------- menu
# Written for 90 beats a minute: long notes of a bell, chords with a seventh, soft bass, few drums.
def menu():
    s = Song(90, 32)
    A = ["F", "G", "Am", "Am", "F", "G", "C", "C"]
    B = ["Dm", "Em", "F", "G", "Dm", "Em", "F", "G"]
    seventh = {"F": 11, "C": 11, "Am": 10, "Dm": 10, "Em": 10, "G": 10}
    tune_a = line("A5:6 G5:2 E5:4 C5:4 | D5:6 E5:2 G5:8 | E5:4 A5:4 G5:2 E5:2 C5:4 | E5:12 -:4 |"
                  "A5:6 C6:2 A5:4 F5:4 | B5:6 A5:2 G5:8 | E5:4 G5:4 C6:6 B5:2 | G5:12 -:4")
    tune_b = line("F5:4 A5:4 D6:8 | G5:4 B5:4 E6:8 | A5:4 C6:4 F6:6 E6:2 | D6:8 B5:4 G5:4 |"
                  "D6:4 A5:4 F5:8 | E6:4 B5:4 G5:8 | C6:4 A5:4 F5:4 A5:4 | B5:8 D6:4 G5:4")
    plan = [(A, None), (A, tune_a), (B, tune_b), (A, tune_a)]
    for sec, (chords, tune) in enumerate(plan):
        for i, name in enumerate(chords):
            bar = sec * 8 + i
            root = CH[name][0] + (12 if CH[name][0] < midi("C2") else 0)
            pad_bar(s, name, bar, 0.19)
            s.put("pad", v_pad(CH[name][1][0] + seventh[name], 16 * s.step), bar * 16, 0.12, 0.25)
            arp_bar(s, name, bar, (0, 2, 1, 3, 2, 1, 2, 1), 2, v_pluck, 0.13, hold=1.5)
            for at, off, d in ((0, 0, 5), (6, 0, 2), (8, 7, 4), (12, 12, 2), (14, 7, 2)):
                s.put("bass", v_soft_bass(root + off, d * s.step * 0.9), bar * 16 + at, 0.5)
            for q in (0, 7, 10):
                s.drum("kick", bar * 16 + q, 0.75 if q != 7 else 0.5)
            for q in (4, 12):
                s.drum("rim", bar * 16 + q, 0.26, -0.2)
                s.drum("snare", bar * 16 + q, 0.12)
            for q in range(0, 16, 2):
                s.drum("hat", bar * 16 + q, 0.11 if q % 4 == 2 else 0.05, 0.3)
            if i % 4 == 3:
                s.drum("open", bar * 16 + 14, 0.08, 0.3)
        if tune:
            s.play("lead", v_bell, tune, sec * 8 * 16, 0.26)
    duck = s.duck(0.3, 0.12)
    mix = s.bus["drums"] + s.bus["bass"] * duck + s.bus["pad"] * duck
    mix += s.bus["arp"] + 0.45 * s.echo(s.bus["arp"], 3, 0.5)
    mix += s.bus["lead"] + 0.4 * s.echo(s.bus["lead"], 6, 0.5)
    mix += 0.38 * s.room(s.bus["lead"] + s.bus["arp"] + s.bus["pad"] + 0.3 * s.bus["drums"], 0.85)
    s.save("synth-menu.wav", mix)


for name, make in (("versus", versus), ("solo", solo), ("menu", menu)):
    if len(sys.argv) < 2 or name in sys.argv[1:]:
        make()
