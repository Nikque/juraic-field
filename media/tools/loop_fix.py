"""loop_fix.py <in.flac> <out.flac> <bpm> [fade bars=2]: makes a generated piece repeat without a break.
The piece is cut to a whole number of bars (4 beats each) and the bars after the cut are faded into the beginning,
so the end runs on into the start. It leans on the piece keeping the tempo it was asked for.
Run with a Python that has numpy and PyAV."""
import sys
import av
import numpy as np

src, dst, bpm = sys.argv[1], sys.argv[2], float(sys.argv[3])
fade_bars = int(sys.argv[4]) if len(sys.argv) > 4 else 2

inp = av.open(src)
stream = inp.streams.audio[0]
rate = stream.rate
x = np.concatenate([f.to_ndarray().reshape(-1, f.samples) if f.format.is_planar else f.to_ndarray().reshape(f.samples, -1).T
                    for f in inp.decode(stream)], axis=1).astype(np.float64)
if x.dtype != np.float64 or np.abs(x).max() > 2:
    x = x / 32768.0
inp.close()

# a quiet tail (the piece fading out) is not part of the loop
level = np.sqrt(np.mean(x ** 2))
frame = rate // 2
rms = np.array([np.sqrt(np.mean(x[:, i:i + frame] ** 2)) for i in range(0, x.shape[1] - frame, frame)])
loud = np.where(rms > 0.5 * level)[0]
usable = (loud[-1] + 1) * frame
bar = 4 * 60.0 / bpm * rate
bars = int(usable / bar) - fade_bars
length, fade = int(round(bars * bar)), int(round(fade_bars * bar))
t = np.linspace(0, np.pi / 2, fade)
y = x[:, :length].copy()
y[:, :fade] = x[:, :fade] * np.sin(t) + x[:, length:length + fade] * np.cos(t)
y = y / max(1.0, np.abs(y).max() / 0.95)

out = av.open(dst, "w")
o = out.add_stream("flac", rate=rate)
o.layout = "stereo" if y.shape[0] == 2 else "mono"
o.format = "s16"
pcm = np.clip(y * 32767, -32768, 32767).astype(np.int16)
for i in range(0, pcm.shape[1], 4096):
    f = av.AudioFrame.from_ndarray(np.ascontiguousarray(pcm[:, i:i + 4096].T.reshape(1, -1)), format="s16", layout=o.layout.name)
    f.sample_rate = rate
    for p in o.encode(f):
        out.mux(p)
for p in o.encode(None):
    out.mux(p)
out.close()
print("%s: %d Hz, %.1f s read, %.1f s loud, %d bars kept = %.1f s, peak %.2f, rms %.3f" % (dst, rate, x.shape[1] / rate, usable / rate, bars, length / rate, np.abs(y).max(), np.sqrt(np.mean(y ** 2))))
