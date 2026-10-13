"""look_sound.py <file> [<file to compare with>]: what a sound file holds (format, rate, channels, frames), and where
the samples of a second file differ from it. Run with a Python that has numpy and PyAV."""
import sys

import av
import numpy as np


def flat(path):
    rows = []
    with av.open(path) as f:
        stream = f.streams.audio[0]
        info = (stream.codec_context.name, stream.format.name, stream.rate, stream.layout.name, stream.codec_context.bits_per_raw_sample)
        for frame in f.decode(stream):
            a = frame.to_ndarray()
            rows.append(a.T if frame.format.is_planar else a.reshape(-1, frame.layout.nb_channels))
    return np.concatenate(rows, axis=0), info


a, ia = flat(sys.argv[1])
print(sys.argv[1], ia, a.shape, a.dtype, "min", a.min(), "max", a.max())
if len(sys.argv) > 2:
    b, ib = flat(sys.argv[2])
    print(sys.argv[2], ib, b.shape, b.dtype, "min", b.min(), "max", b.max())
    n = min(len(a), len(b))
    diff = np.nonzero((a[:n].astype(np.int64) != b[:n].astype(np.int64)).any(axis=1))[0]
    print("frames:", len(a), len(b), "differing among the first", n, ":", len(diff), "first at", diff[:5])
    if len(diff):
        i = diff[0]
        print("a", a[i:i + 3].tolist(), "b", b[i:i + 3].tolist())
