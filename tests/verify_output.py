"""Independent cell-grid verifier of the generator's output (Python 3, stdlib).

Usage: python tests/verify_output.py generator.log [--allow-extra]

It consumes the generator's stdout, not its internal C++ state, and checks the
definition of a valid result directly, one cell at a time. The C++ search skips
most of these checks because they follow from the way it builds a board; this
program exists to catch a hole in that reasoning.

Two fields: 6 columns with 12 rows that clear and a hidden 13th row, pairs
appearing in column 3, the game over when column 3 reaches row 12; and (a
result preceded by "field=10x18") 10 columns with 18 rows that clear and a
hidden 19th row, pairs appearing in column 5, the game over when column 5 or 6
reaches row 18. Below, "row TOP" is row 12 or 18, the cells where the game is
over are the death cells.

A result is valid when
  1. no column has a gap and nothing sits above the hidden row;
  2. firing it clears exactly `chains` waves, each one group of exactly four,
     and the first wave is the printed trigger;
  3. after the chain the death cells are empty;
  4. at least one "last pair" is printed, and for every printed pair:
     a. it is the two topmost pieces of one column, or the topmost pieces of two
        neighbouring columns;
     b. without the pair nothing clears;
     c. without the pair the death cells are empty;
     d. the pair can travel to its place (rule below);
  5. the printed pairs are exactly all pairs that satisfy 4a-4d.
The board itself may cover the death cells: the last pair is allowed to choke
as long as the chain frees them again (rule 3).
"""
import argparse
import re

OFFSETS = [(0, 1), (1, 0), (0, -1), (-1, 0)]
MAX_HOP = 4  # lower columns the lifted axis piece can cross before it must land


class Field:
    def __init__(self, width, rows, spawn, death, capacity):
        self.width = width        # columns
        self.top = rows           # rows that connect and clear
        self.height = rows + 1    # rows a piece can rest in (with the hidden row)
        self.spawn = spawn        # column where pairs appear (0-based)
        self.death = death        # columns whose cell of row TOP must stay free
        self.capacity = capacity  # the most pieces of a valid board


SMALL = Field(6, 12, 2, (2,), 78)
# 8 full columns of 19 and the two death columns at 17 before the last pair, plus the pair.
WIDE = Field(10, 18, 4, (4, 5), 8 * 19 + 2 * 17 + 2)


def wave(board, f=SMALL):
    """One clearing wave. Returns (groups of four or more, board after gravity)."""
    visited, erase = set(), set()
    groups = []
    for cell, color in board.items():
        if cell[1] >= f.top or cell in visited:
            continue
        group, queue = set(), [cell]
        visited.add(cell)
        while queue:
            x, y = queue.pop()
            group.add((x, y))
            for dx, dy in OFFSETS:
                q = x + dx, y + dy
                if q[1] < f.top and board.get(q) == color and q not in visited:
                    visited.add(q)
                    queue.append(q)
        if len(group) >= 4:
            groups.append(group)
            erase.update(group)
    after = {}
    for x in range(f.width):
        colors = [v for (cx, y), v in sorted(board.items(), key=lambda kv: kv[0][1])
                  if cx == x and (cx, y) not in erase]
        after.update({(x, y): c for y, c in enumerate(colors)})
    return groups, after


def heights(board, f=SMALL):
    return [sum(1 for (x, _) in board if x == column) for column in range(f.width)]


def death_free(board, f=SMALL):
    return all((x, f.top - 1) not in board for x in f.death)


def column_reachable(h, target, f=SMALL):
    """Can the falling pair travel from the spawn column to `target`?

    TOP-1 or lower: always passable. TOP+1 (a full column): never. TOP: only
    while the axis piece is lifted into the hidden row (wall crossing). It is
    lifted by a quick turn at the spawn (both neighbours TOP or higher), by a
    floor kick on a column exactly TOP-1 high, or by standing on a TOP-high
    column; once lifted it may hop over up to MAX_HOP lower columns. The floor
    kick may be made on the other side of the spawn column; the columns crossed
    on the way back count as hopped. Nothing moves once a death column is TOP high.
    """
    top, kick, s = f.top, f.top - 1, f.spawn
    if any(h[x] >= top for x in f.death):
        return False
    step = -1 if target < s else 1
    fly = -1  # lower columns that may still be crossed in the air; -1 = not lifted
    if (h[s - 1] >= top and h[s + 1] >= top) or h[s] == kick:
        fly = MAX_HOP
    else:
        x = s - step
        for hopped in range(1, MAX_HOP + 1):
            if not 0 <= x < f.width:
                break
            if h[x] == kick:
                fly = MAX_HOP - hopped
            if h[x] >= kick:
                break
            x -= step
    x = s
    while x != target:
        x += step
        if h[x] >= f.height:
            return False
        if h[x] == top:
            if fly < 0:
                return False
            fly = MAX_HOP
        elif h[x] == kick:
            fly = MAX_HOP
        elif fly >= 0:
            fly -= 1
    return True


def pair_candidates(board, f=SMALL):
    """Every pair of topmost pieces: (kind, frozenset of two cells)."""
    h = heights(board, f)
    for x in range(f.width):
        if h[x] >= 2:
            yield "V", frozenset({(x, h[x] - 1), (x, h[x] - 2)})
    for x in range(f.width - 1):
        if h[x] and h[x + 1]:
            yield "H", frozenset({(x, h[x] - 1), (x + 1, h[x + 1] - 1)})


def pair_is_valid(board, kind, cells, f=SMALL):
    before = {cell: color for cell, color in board.items() if cell not in cells}
    if wave(before, f)[0]:
        return False
    if not death_free(before, f):
        return False
    h = heights(before, f)
    columns = sorted(x for x, _ in cells)
    if kind == "V":
        return h[columns[0]] <= f.height - 2 and column_reachable(h, columns[0], f)
    far = columns[1] if columns[0] >= f.spawn else columns[0]
    return h[columns[0]] <= f.height - 1 and h[columns[1]] <= f.height - 1 and column_reachable(h, far, f)


CELL = re.compile(r"\((\d+),(\d+)\)")
OPTION = re.compile(r"([VH])\((\d+),(\d+)\)\((\d+),(\d+)\)")


def verify(text, allow_extra=False):
    counts = {}
    sections = text.split("field (top row first; columns left to right):\n")
    assert len(sections) > 1, "no solutions in log"
    for index, chunk in enumerate(sections[1:], 1):
        header = sections[index - 1]
        chain_headers = re.findall(r"chains=(\d+)", header)
        target = int(chain_headers[-1])
        # "field=10x18" belongs to this result only if it follows its "chains=" line.
        f = WIDE if header.rfind("\nfield=10x18\n") > header.rfind("chains=") else SMALL
        lines = chunk.splitlines()
        rows = [line.split() for line in lines[:f.height]]
        assert all(len(row) == f.width for row in rows)
        board = {(x, f.height - 1 - y): c for y, row in enumerate(rows)
                 for x, c in enumerate(row) if c != "."}
        assert all(c in "RGBYP" for c in board.values())
        assert 4 * target <= len(board) <= f.capacity
        if not allow_extra:
            assert len(board) == 4 * target
        # 1. compact columns (nothing above the hidden row can be printed at all)
        h = heights(board, f)
        assert all((x, y) in board for x in range(f.width) for y in range(h[x])), "hole in a column"
        if f is WIDE:
            code_line = next(line for line in lines if line.startswith("board code: "))
            code = "".join(board.get((x, y), ".") for y in range(f.height - 1, -1, -1) for x in range(f.width))
            assert code_line == "board code: 10x18:" + code, "board code differs from the printed field"

        trigger_line = next(line for line in lines if line.startswith("trigger cells"))
        trigger = {(int(x) - 1, int(y) - 1) for x, y in CELL.findall(trigger_line)}
        assert len(trigger) == 4
        option_line = next(line for line in lines if line.startswith("last pair options"))
        printed = {(kind, frozenset({(int(a) - 1, int(b) - 1), (int(c) - 1, int(d) - 1)}))
                   for kind, a, b, c, d in OPTION.findall(option_line.split(":", 1)[1])}

        # 4 and 5: the printed last pairs are exactly the valid ones
        valid = {(kind, cells) for kind, cells in pair_candidates(board, f) if pair_is_valid(board, kind, cells, f)}
        assert printed, "no last pair"
        assert printed <= set(pair_candidates(board, f)), "a printed pair is not made of topmost pieces"
        assert printed <= valid, "a printed last pair is not valid"
        assert valid <= printed, "a valid last pair was not found"

        # 2 and 3: the chain
        built, waves = dict(board), 0
        while True:
            groups, after = wave(built, f)
            if not groups:
                break
            assert len(groups) == 1 and len(groups[0]) == 4, "a wave is not one group of four"
            if waves == 0:
                assert groups[0] == trigger, "first wave is not the trigger"
            built = after
            waves += 1
        assert waves == target
        assert len(built) == len(board) - 4 * target
        assert death_free(built, f), "choked after the chain"
        size = "" if f is SMALL else "10x18/"
        key = f"{size}{target}-chain/{len(board)}-cell"
        counts[key] = counts.get(key, 0) + 1
    return counts


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("log")
    parser.add_argument("--allow-extra", action="store_true")
    args = parser.parse_args()
    with open(args.log, encoding="utf-8") as source:
        print("Verified:", verify(source.read(), args.allow_extra))
