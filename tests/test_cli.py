"""Integration checks for flexible counts, odd preparation and config compatibility."""
import pathlib
import subprocess
import sys
import tempfile
from verify_output import verify

executable = str(pathlib.Path(sys.argv[1]).resolve())
base = dict(initial_seed=42, color_count=4, beam_width=16,
            candidates_per_parent=8, restarts=5000, target_success_count=1)


def run(settings, directory):
    config = directory / "config.ini"
    config.write_text("".join(f"{k}={v}\n" for k, v in settings.items()), encoding="utf-8")
    return subprocess.run([executable, str(config)], cwd=directory,
                          capture_output=True, encoding="utf-8", timeout=30)


with tempfile.TemporaryDirectory(prefix="juraic-cli-test-") as temp:
    root = pathlib.Path(temp)
    for index, (target, extra) in enumerate([(1, 0), (1, 1), (1, 2), (5, 3), (10, 2)]):
        directory = root / str(index)
        directory.mkdir()
        settings = dict(base, target_chain=target, min_extra_pieces=extra, max_extra_pieces=extra)
        result = run(settings, directory)
        assert result.returncode == 0, result.stderr
        counts = verify(result.stdout, True)
        assert counts == {f"{target}-chain/{4 * target + extra}-cell": 1}, counts
    # The boards and their order must not depend on the number of threads.
    outputs = []
    for threads in (1, 2, 7):
        directory = root / f"threads-{threads}"
        directory.mkdir()
        settings = dict(base, beam_width=48, candidates_per_parent=2, target_chain=19,
                        target_success_count=30, restarts=100000, threads=threads)
        result = run(settings, directory)
        assert result.returncode == 0, result.stderr
        assert f"threads={threads}" in result.stdout
        assert verify(result.stdout, True) == {"19-chain/76-cell": 30}
        outputs.append((directory / "19chain_urls.txt").read_text())
    assert outputs[0] == outputs[1] == outputs[2], "thread count changed the result"
    directory = root / "threads-invalid"
    directory.mkdir()
    assert run(dict(base, threads=1000), directory).returncode == 2
    # Old six-key files retain target 19, zero extras. One narrow attempt may
    # fail to find a field; it must still parse and execute without code 2.
    directory = root / "legacy"
    directory.mkdir()
    result = run(dict(base, beam_width=1, candidates_per_parent=1, restarts=1), directory)
    assert result.returncode in (0, 1)
    assert "target_chain=19" in result.stdout and "max_extra_pieces=0" in result.stdout
    # Extra pieces: a chain of five with three pieces left over.
    directory = root / "extra"
    directory.mkdir()
    result = run(dict(base, target_chain=5, min_extra_pieces=3, max_extra_pieces=3), directory)
    assert result.returncode == 0, result.stderr
    assert verify(result.stdout, True) == {"5-chain/23-cell": 1}
    for index, overrides in enumerate([
        dict(target_chain=0), dict(target_chain=20), dict(max_extra_pieces=-1),
        dict(target_chain=19, max_extra_pieces=3),
        dict(target_chain=18, max_extra_pieces=7),
        dict(min_extra_pieces=2, max_extra_pieces=1),
    ]):
        directory = root / f"invalid-{index}"
        directory.mkdir()
        result = run(dict(base, **overrides), directory)
        assert result.returncode == 2
        assert not (directory / "19chain_urls.txt").exists()
    directory = root / "日本語🟢"
    directory.mkdir()
    settings = dict(base, target_chain=1)
    config = directory / "設定🟢.ini"
    config.write_text("".join(f"{k}={v}\n" for k, v in settings.items()), encoding="utf-8-sig")
    result = subprocess.run([executable, str(config)], cwd=directory,
                            capture_output=True, encoding="utf-8", timeout=30)
    assert result.returncode == 0, result.stderr
    assert verify(result.stdout, True) == {"1-chain/4-cell": 1}
    missing = directory / "存在しない🟢.ini"
    result = subprocess.run([executable, str(missing)], cwd=directory,
                            capture_output=True, encoding="utf-8", timeout=30)
    assert result.returncode == 2 and str(missing) in result.stderr

    for index, ending in enumerate(["", "\r", "\n", "\r\n"]):
        directory = root / f"history-{index}"
        directory.mkdir()
        history = directory / "19chain_urls.txt"
        old_url = "https://ishikawapuyo.net/simu/pe.html?abc"
        history.write_bytes((old_url + ending).encode("ascii"))
        result = run(settings, directory)
        assert result.returncode == 0, result.stderr
        assert len(history.read_text().splitlines()) == 2
        assert history.read_text().splitlines()[0] == old_url
        first_urls = set(history.read_text().splitlines())
        result = run(settings, directory)
        assert result.returncode == 0, result.stderr
        assert "重複したため保存をスキップ" in result.stdout
        assert len(set(history.read_text().splitlines())) == 3
        assert first_urls <= set(history.read_text().splitlines())

    directory = root / "unwritable-output"
    directory.mkdir()
    (directory / "19chain_urls.txt").mkdir()
    result = run(settings, directory)
    assert result.returncode == 2
    assert "output file" in result.stderr

    # 10 x 18 fields: 47 chains, the board code file instead of URLs, the thread count.
    wide = dict(base, beam_width=48, candidates_per_parent=2, restarts=20000, field_columns=10, field_rows=18)
    outputs = []
    for threads in (1, 3):
        directory = root / f"wide-threads-{threads}"
        directory.mkdir()
        result = run(dict(wide, target_chain=47, target_success_count=4, threads=threads), directory)
        assert result.returncode == 0, result.stderr
        assert result.stdout.splitlines()[0].endswith(f"threads={threads}, field=10x18")
        assert verify(result.stdout) == {"10x18/47-chain/188-cell": 4}
        assert not (directory / "19chain_urls.txt").exists()
        outputs.append((directory / "boards_10x18.txt").read_text())
        wide_log = result.stdout
    assert outputs[0] == outputs[1], "thread count changed the 10 x 18 result"
    assert all(line.startswith("10x18:") and len(line) == 6 + 190 for line in outputs[0].splitlines())
    # The verifier must reject a board changed consistently in the field and the board code:
    # one piece of the first trigger gets another colour.
    lines = wide_log.splitlines()
    start = lines.index("field (top row first; columns left to right):") + 1
    trigger = next(line for line in lines[start:] if line.startswith("trigger cells"))
    x, y = (int(v) - 1 for v in trigger.split(":")[1].split()[0].strip("()").split(","))
    row = lines[start + 18 - y].split()
    row[x] = "R" if row[x] != "R" else "G"
    lines[start + 18 - y] = " ".join(row) + " "
    code = next(i for i in range(start, len(lines)) if lines[i].startswith("board code: "))
    cells = list(lines[code][len("board code: 10x18:"):])
    cells[(18 - y) * 10 + x] = row[x]
    lines[code] = "board code: 10x18:" + "".join(cells)
    try:
        verify("\n".join(lines) + "\n")
    except AssertionError:
        pass
    else:
        raise AssertionError("the verifier accepted a changed 10 x 18 board")
    # Extra pieces and the history file.
    directory = root / "wide-extra"
    directory.mkdir()
    result = run(dict(wide, target_chain=30, min_extra_pieces=3, max_extra_pieces=5, target_success_count=3), directory)
    assert result.returncode == 0, result.stderr
    assert sum(verify(result.stdout, True).values()) == 3
    assert all(k.startswith("10x18/30-chain/") and 123 <= int(k.split("/")[2].split("-")[0]) <= 125
               for k in verify(result.stdout, True))
    directory = root / "wide-history"
    directory.mkdir()
    once = dict(wide, target_chain=5, target_success_count=1)
    assert run(once, directory).returncode == 0
    result = run(once, directory)
    assert result.returncode == 0 and "重複したため保存をスキップ" in result.stdout
    assert len((directory / "boards_10x18.txt").read_text().splitlines()) == 2
    for index, overrides in enumerate([
        dict(field_columns=10, field_rows=12), dict(field_columns=8, field_rows=18), dict(field_rows=18, field_columns=6),
        dict(target_chain=48), dict(target_chain=40, max_extra_pieces=29), dict(target_chain=0),
    ]):
        directory = root / f"wide-invalid-{index}"
        directory.mkdir()
        result = run(dict(wide, **overrides), directory)
        assert result.returncode == 2, overrides
        assert not (directory / "boards_10x18.txt").exists() and not (directory / "19chain_urls.txt").exists()
print("CLI integration tests passed (generation, Unicode/BOM paths, invalid config, URL append/deduplication, I/O errors, 10 x 18 fields).")
