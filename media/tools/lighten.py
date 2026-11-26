"""lighten.py: the pictures and the music of the package made smaller without changing them.

  music     every piece as FLAC at the strongest compression. A .wav becomes a .flac of the same name; a .flac is
            packed again. The samples must come out bit for bit as they went in: each result is read back and
            compared with its source, sample by sample, and the run stops when one differs.
  pictures  every .png written again with the strongest compression of the format; the pixels are compared too.

usage: python lighten.py <folder that holds the pictures and the music, in any folders below it> <folder to write to>
Nothing in the first folder is changed. Run with a Python that has numpy, PyAV and Pillow.
"""
import os
import shutil
import sys
from pathlib import Path
from tempfile import TemporaryDirectory


def real(path):
    return os.path.normcase(os.path.realpath(path))


def inside(path, parent):
    try:
        return os.path.commonpath([path, parent]) == parent
    except ValueError:  # (on two drives)
        return False


def outputs(src, dst):
    """Plan all output names before any directory or file is written."""
    source_root = real(src)
    if inside(real(dst), source_root):
        raise ValueError(f"{dst}: the folder to write to is the source folder or in it")
    jobs = []
    for folder, _, names in os.walk(src):
        rel = Path(folder).relative_to(src)
        for name in sorted(names):
            path = Path(folder) / name
            ending = path.suffix.lower()
            if ending not in (".wav", ".flac", ".png"):
                continue
            out_name = name if ending == ".png" else path.stem + ".flac"
            jobs.append((path, dst / rel / out_name, ending))

    inputs = {real(path) for path, _, _ in jobs}
    destinations = {}
    for path, out, _ in jobs:
        parent = real(out.parent)
        # Resolve directory aliases. The leaf itself will be replaced, so its link target is safe.
        key = os.path.normcase(os.path.join(parent, out.name))
        if inside(parent, source_root) or key in inputs:
            raise ValueError(f"{out}: writing here would replace a source")
        if key in destinations:
            raise ValueError(f"{out}: both {destinations[key]} and {path} produce this file")
        destinations[key] = path
    return jobs


def publish(out, prepare):
    """Write and verify on the same filesystem, then replace the output directory entry."""
    out.parent.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(prefix=".lighten-", dir=out.parent) as temporary:
        candidate = Path(temporary) / out.name
        result = prepare(candidate)
        os.replace(candidate, out)
        return result


def flat(path):
    """The samples of a file, one row a frame of sound, as 64-bit integers, with its rate, format and channels."""
    import av
    import numpy as np

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
    import av

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


def prepare_asset(path, candidate, ending):
    import numpy as np

    if ending == ".png":
        from PIL import Image

        with Image.open(path) as im:
            im.load()
            im.save(candidate, optimize=True, compress_level=9)
            with Image.open(candidate) as again:
                same = again.mode == im.mode and again.size == im.size and np.array_equal(np.asarray(again), np.asarray(im))
            note = f"{im.mode} {im.size[0]}x{im.size[1]}"
    else:
        pack(path, candidate)
        a, fa, rate, layout = flat(path)
        b, fb, rate_b, layout_b = flat(candidate)
        # A WAV and a FLAC may name the same channel layout differently.
        same = a.shape == b.shape and rate == rate_b and np.array_equal(a, b)
        note = f"{fa} -> {fb}, {rate} Hz, {a.shape[1]} channels, {a.shape[0]} frames ({a.shape[0] / rate:.1f} s)"
    if not same:
        raise ValueError(f"{path}: the result differs from the source")
    before, after = path.stat().st_size, candidate.stat().st_size
    if after >= before and ending != ".wav":
        shutil.copyfile(path, candidate)
        after = before
        note += "; not smaller, kept as it is"
    return before, after, note


def main(src, dst):
    total_before = total_after = 0
    for path, out, ending in outputs(src, dst):
        before, after, note = publish(out, lambda candidate: prepare_asset(path, candidate, ending))
        total_before += before
        total_after += after
        print(f"{str(path.relative_to(src)):40s} {before:10d} -> {after:10d}  ({100 * after / before:5.1f} %)  same content: True  [{note}]")
    ratio = 100 * total_after / total_before if total_before else 0
    print(f"total {total_before} -> {total_after} bytes ({ratio:.1f} %)")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: python lighten.py SOURCE DESTINATION")
    try:
        main(Path(sys.argv[1]), Path(sys.argv[2]))
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from error
