"""Generate and independently verify the 10 x 18 field for every chain length.

Chain lengths 1..47 with four and five colours, and for each no extra pieces,
one extra piece, a range up to the largest number (188 - 4 * chain) and the
largest number itself. Three colours: chain lengths 1..47 without the largest
number of extra pieces (with it some chain lengths are not found). The
independent verifier (verify_output.py) checks the full definition of a valid
board.
"""
import configparser
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from verify_output import verify

executable = str(Path(sys.argv[1]).resolve())
config = configparser.ConfigParser()
config.read(sys.argv[2], encoding="utf-8-sig")
defaults = {key: int(value) for key, value in config["Generator"].items()}
results = []
with tempfile.TemporaryDirectory(prefix="juraic-all-wide-") as temp:
    root = Path(temp)
    for chain in range(1, 48):
        capacity = 188 - 4 * chain
        for colors in (3, 4, 5):
            ranges = {(0, 0), (min(1, capacity), min(1, capacity)), (0, capacity)}
            if colors != 3:
                ranges.add((capacity, capacity))
            for low, high in sorted(ranges):
                path = root / f"c{chain}-colors{colors}-extra{low}-{high}"
                path.mkdir()
                settings = dict(defaults, initial_seed=42, target_chain=chain, color_count=colors,
                                min_extra_pieces=low, max_extra_pieces=high, target_success_count=1,
                                restarts=20000, field_columns=10, field_rows=18)
                source = path / "config.ini"
                source.write_text("".join(f"{key}={value}\n" for key, value in settings.items()))
                run = subprocess.run([executable, str(source)], cwd=path, capture_output=True,
                                     encoding="utf-8", timeout=180)
                assert run.returncode == 0, f"{settings}: {run.stdout[-500:]} {run.stderr}"
                verified = verify(run.stdout, True)
                assert sum(verified.values()) == 1, (settings, verified)
                for key in verified:
                    cells = int(key.split("/")[2].split("-")[0])
                    assert key.startswith(f"10x18/{chain}-chain/") and 4 * chain + low <= cells <= 4 * chain + high, (settings, verified)
                assert len((path / "boards_10x18.txt").read_text().splitlines()) == 1
                results.append(dict(chain=chain, colors=colors, min_extra=low, max_extra=high))
                print(f"Verified 10 x 18, chain={chain}, colors={colors}, extra={low}..{high}: {verified}", flush=True)
print(json.dumps(dict(cases=len(results))))
