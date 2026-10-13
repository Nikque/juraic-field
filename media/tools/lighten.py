"""lighten.py: the pictures and the music of the package made smaller without changing them.

  music     every piece as FLAC at the strongest compression. A .wav becomes a .flac of the same name; a .flac is
            packed again. The samples must come out bit for bit as they went in: each result is read back and
            compared with its source, sample by sample, and the run stops when one differs.
  pictures  every .png written again with the strongest compression of the format; the pixels are compared too.

usage: python lighten.py <folder that holds the pictures and the music, in any folders below it> <folder to write to>
Nothing in the first folder is changed. Run with a Python that has numpy, PyAV and Pillow.
"""
import os
import sys

import av
import numpy as np
from PIL import Image

src, dst = sys.argv[1], sys.argv[2]


def flat(path):
    """The samples of a file, one row a frame of sound, as 64-bit integers, with its rate, format and channels."""
    rows = []
    with av.open(path) as f:
        stream = f.streams.audio[0]
        fmt, rate, layout = stream.format.name, stream.rate, stream.layout.name
        for frame in f.decode(stream):
            a = frame.to_ndarray()
            rows.append(a.T if frame.format.is_planar else a.reshape(-1, frame.layout.nb_channels))
    data = np.concatenate(rows, axis=0)
    if data.dtype.kind == "f":
        raise SystemExit(f"{path}: the samples are floats ({fmt}); not packed")
    return data.astype(np.int64), fmt, rate, layout


def pack(path, out):
    """Writes the sound of `path` to `out` as FLAC, in whole-number samples as wide as those of the source."""
    with av.open(path) as f, av.open(out, "w", format="flac") as o:
        stream = f.streams.audio[0]
        s = o.add_stream("flac", rate=stream.rate)
        # (FLAC holds 16 or 24 bits a sample: a 16-bit source stays 16, a wider one goes in as 32-bit integers that hold 24)
        s.format = "s16" if stream.format.name in ("s16", "s16p") else "s32"
        s.layout = stream.layout.name
        s.options = {"compression_level": "12"}
        for frame in f.decode(stream):
            frame.pts = None
            for packet in s.encode(frame):
                o.mux(packet)
        for packet in s.encode(None):
            o.mux(packet)


total_before = total_after = 0
for folder, _, names in os.walk(src):
    rel = os.path.relpath(folder, src)
    for name in sorted(names):
        path = os.path.join(folder, name)
        stem, ending = os.path.splitext(name)
        ending = ending.lower()
        if ending not in (".wav", ".flac", ".png"):
            continue
        os.makedirs(os.path.join(dst, rel), exist_ok=True)
        if ending == ".png":
            out = os.path.join(dst, rel, name)
            with Image.open(path) as im:
                im.load()
                im.save(out, optimize=True, compress_level=9)
                with Image.open(out) as again:
                    same = again.mode == im.mode and again.size == im.size and np.array_equal(np.asarray(again), np.asarray(im))
                note = f"{im.mode} {im.size[0]}x{im.size[1]}"
        else:
            out = os.path.join(dst, rel, stem + ".flac")
            pack(path, out)
            a, fa, rate, layout = flat(path)
            b, fb, rate_b, layout_b = flat(out)
            # (the same number of frames and of channels, the same rate, every sample the same; the name of the layout
            #  is not compared: a .wav says "2 channels" where a .flac says "stereo")
            same = a.shape == b.shape and rate == rate_b and np.array_equal(a, b)
            note = f"{fa} -> {fb}, {rate} Hz, {a.shape[1]} channels, {a.shape[0]} frames ({a.shape[0] / rate:.1f} s)"
        if not same:
            raise SystemExit(f"{path}: the result differs from the source - stopped")
        before, after = os.path.getsize(path), os.path.getsize(out)
        if after >= before and ending != ".wav":
            # (not smaller: the file goes over as it is)
            with open(path, "rb") as f, open(out, "wb") as o:
                o.write(f.read())
            after = before
            note += "; not smaller, kept as it is"
        total_before += before
        total_after += after
        print(f"{os.path.join(rel, name):40s} {before:10d} -> {after:10d}  ({100 * after / before:5.1f} %)  same content: {same}  [{note}]")
print(f"total {total_before} -> {total_after} bytes ({100 * total_after / total_before:.1f} %)")
