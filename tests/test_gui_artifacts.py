"""Independently replay every saved GUI construction, including partial runs."""
import pathlib
import sys
import re
from verify_output import verify, wave, WIDE

def decode_url(url):
    if url.startswith("https://www.pndsng.com/puyo/index.html?"):
        code = url.split("?", 1)[1]
        tokens = re.findall(r"([a-f])(\d*)", code)
        assert "".join(a+b for a,b in tokens) == code
        cells = ["abecdf".index(a) for a,b in tokens for _ in range(int(b or 1))]
    else:
        prefixes = ("https://ishikawapuyo.net/simu/pe.html?", "https://www.puyop.com/s/")
        prefix = next(p for p in prefixes if url.startswith(p))
        alphabet = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-"
        cells = [v for c in url[len(prefix):] for v in divmod(alphabet.index(c), 8)]
        cells = [0]*(78-len(cells)) + cells
    assert len(cells) == 78 and all(0 <= c <= 5 for c in cells)
    return cells

def archive_cells(path):
    columns = [list(map(int, line.split()))[1:] for line in path.read_text(encoding="utf-8").splitlines()[2:8]]
    return [columns[x][y] if y < len(columns[x]) else 0 for y in range(12, -1, -1) for x in range(6)]

def wide_archive_code(path):
    """The board code (as boards_10x18.txt) of an archive of version 3."""
    lines = path.read_text(encoding="utf-8").splitlines()
    assert lines[0] == "JURAIC_FIELD_CHAIN 3" and lines[1] == "10 18", path
    columns = [list(map(int, line.split()))[1:] for line in lines[3:13]]
    return "10x18:" + "".join(".RGBYP"[columns[x][y]] if y < len(columns[x]) else "." for y in range(18, -1, -1) for x in range(10))

root = pathlib.Path(sys.argv[1])
total = 0
wide_total = 0
formats = set()
for log in root.rglob("generator.log"):
    text = log.read_text(encoding="utf-8")
    # (results are board-N.jfchain; a run retained from an older version has them under another ending)
    archives = list(log.parent.glob("board-*.*"))
    if not archives:
        assert "field (top row first" not in text, log
        continue
    counts = verify(text, True)
    n = sum(counts.values())
    if (log.parent / "boards_10x18.txt").exists():
        # A 10 x 18 run: board codes instead of URLs, archives of version 3.
        codes = (log.parent / "boards_10x18.txt").read_text(encoding="utf-8").splitlines()
        assert n == len(archives) == len(codes) == len(set(codes)) and all(k.startswith("10x18/") for k in counts), (log, counts)
        assert not (log.parent / "simulator_urls.txt").exists() and not (log.parent / "url-format.txt").exists()
        for i, code in enumerate(codes, 1):
            assert wide_archive_code(log.parent / f"board-{i}.jfchain") == code, (log, i)
        wide_total += n
        continue
    urls = (log.parent / "19chain_urls.txt").read_text(encoding="utf-8").splitlines()
    assert n == len(archives) == len(urls) == len(set(urls)), (log, counts)
    # Old retained runs predate the selector; their canonical URLs still verify.
    if (log.parent / "url-format.txt").exists():
        exported = (log.parent / "simulator_urls.txt").read_text(encoding="utf-8").splitlines()
        assert len(exported) == n
        format_id = (log.parent / "url-format.txt").read_text().strip()
        if format_id not in ("ishikawa", "mattulwan"):
            format_id = "park"  # (this site had another id in runs retained from an older version)
        formats.add(format_id)
        prefixes = {"ishikawa":"https://ishikawapuyo.net/simu/pe.html?", "park":"https://www.puyop.com/s/", "mattulwan":"https://www.pndsng.com/puyo/index.html?"}
        for i, url in enumerate(exported, 1):
            assert url.startswith(prefixes[format_id])
            assert decode_url(url) == archive_cells(next(log.parent.glob(f"board-{i}.*"))), (log, i)
            assert decode_url(urls[i-1]) == decode_url(url)
    total += n
assert total >= 100, total
assert formats == {"ishikawa", "park", "mattulwan"}, formats
model_count = 0
for format_id in ("ishikawa", "park", "mattulwan"):  # (not the files an earlier build left under another id)
    for exported in pathlib.Path(sys.argv[2]).rglob(f"urls-{format_id}.txt"):
        assert decode_url(exported.read_text().strip()) == archive_cells(exported.parent / "盤面.jfchain")
        model_count += 1
assert model_count == 54, model_count  # (1 and 19 chains, three to five colours, 0 to 2 extra cells: 18 settings, 3 sites)

# Verify displayed pre-gravity holes with the independent grid matcher, rather
# than C++ helper results. Retained original heights must survive the clear.
def transition(before, cleared, dropped):
    groups, after = wave(before)
    assert groups
    erased = set().union(*groups)
    expected = {pos: color for pos, color in before.items() if pos not in erased}
    assert cleared == expected, "post-clear frame must preserve survivor heights"
    assert dropped == after, "following frame must apply gravity"
    return len(erased)

timeline_count = 0
for path in pathlib.Path(sys.argv[2]).glob("*/timeline.txt"):
    lines = path.read_text().splitlines()
    count, ignition = map(int, lines[0].split())
    frames = []
    for line in lines[1:]:
        pair, chain, *cells = map(int, line.split())
        assert len(cells) == 78
        board = {(i % 6, 12 - i // 6): color for i, color in enumerate(cells) if color}
        frames.append((pair, chain, board))
    assert len(frames) == count
    target = frames[-1][1]
    assert len(frames) - ignition - 1 == target * 2
    for n in range(1, target + 1):
        i = ignition + 2*n - 1
        assert frames[i][1] == frames[i+1][1] == n
        transition(frames[i-1][2], frames[i][2], frames[i+1][2])
    for i in range(1, ignition):
        if frames[i][0] == frames[i-1][0] and frames[i+1][0] == frames[i][0]:
            assert frames[i][1] == frames[i+1][1] == 0
            assert transition(frames[i-1][2], frames[i][2], frames[i+1][2]) == 5
    timeline_count += 1
assert timeline_count == 18, timeline_count

# The 10 x 18 timelines of the model test: one frame a line, the chain and the 190 cells from the hidden row down.
wide_timelines = 0
for path in pathlib.Path(sys.argv[2]).glob("wide/*/timeline-10x19.txt"):
    lines = path.read_text().splitlines()
    frames = []
    for line in lines[1:]:
        chain, *cells = map(int, line.split())
        assert len(cells) == 190
        frames.append((chain, {(i % 10, 18 - i // 10): color for i, color in enumerate(cells) if color}))
    assert len(frames) == int(lines[0])
    target = frames[-1][0]
    assert len(frames) == 1 + 2 * target and frames[0][0] == 0
    for n in range(1, target + 1):
        before, cleared, dropped = frames[2*n - 2][1], frames[2*n - 1][1], frames[2*n][1]
        groups, after = wave(before, WIDE)
        assert len(groups) == 1 and len(groups[0]) == 4, (path, n)
        assert cleared == {pos: color for pos, color in before.items() if pos not in groups[0]} and dropped == after, (path, n)
    wide_timelines += 1
assert wide_timelines == 6, wide_timelines
assert wide_total >= 3, wide_total
print(f"Independently verified {total} GUI results (completed and cancelled runs) and {wide_total} of 10 x 18")
print(f"Verified the 10 x 18 playback of all {wide_timelines} model timelines (1, 20 and 47 chains)")
print(f"Also verified all {model_count} URL exports for 1/19 chains, 3/4/5 colors and 0/1/2 extra cells")
print(f"Verified post-clear holes and following gravity in all {timeline_count} displayed timelines, including preparation clears")
