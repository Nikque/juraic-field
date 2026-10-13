#!/usr/bin/env python3
"""Independent check of the move-level game rules (game/turn.h).

Replays the log written by `game_tests <log>` with a cell-by-cell simulator
written from the rules, not from the C++ code, and compares every number and
every cell after every move.

Usage: test_game.py <log file>
"""
import sys

W, ROWS, CLEAR_ROWS = 6, 13, 12  # rows a piece can rest in / rows that connect
GARBAGE = 6
COLOR_BONUS = [0, 0, 3, 6, 12, 24]
# Extra frames of a split near the top, by (row, column) of the axis piece.
TOP_SPLIT = {(11, 0): 2, (11, 1): 4, (11, 2): 6, (11, 3): 4, (11, 4): 2,
             (10, 1): 2, (10, 2): 4, (10, 3): 2, (9, 2): 2}


def chain_bonus(chain):
    if chain <= 1:
        return 0
    if chain <= 3:
        return 8 * (chain - 1)
    return 32 * (chain - 3)


def group_bonus(size, clear_count):
    if clear_count < 4:
        size += 4 - clear_count
    if size >= 11:
        return 10
    return size - 3 if size >= 5 else 0


def decode_placement(pid):
    """-> (axis column, child dx, child dy)"""
    if pid < 6:
        return pid, 0, 1
    if pid < 11:
        return pid - 6, 1, 0
    if pid < 17:
        return pid - 11, 0, -1
    return pid - 16, -1, 0


class Player:
    def __init__(self):
        self.grid = [[0] * (ROWS + 1) for _ in range(W)]  # [x][y], y == 13 is the 14th row
        self.score = self.pool = self.pending = self.confirmed = 0
        self.all_clear = False

    def height(self, x):
        h = sum(1 for y in range(ROWS) if self.grid[x][y])
        assert all(self.grid[x][y] for y in range(h)), "field has a hole"
        return h

    def cells(self):
        return "".join(str(self.grid[x][y]) for x in range(W) for y in range(ROWS)) + \
               "".join(str(self.grid[x][ROWS]) for x in range(W))


class Game:
    def __init__(self, v, timing):
        it = iter(v)
        self.colors, self.clear_count, self.target, self.margin, self.step = (next(it) for _ in range(5))
        steps = next(it)
        self.rates = [next(it) for _ in range(steps)]
        self.requirement, self.all_clear_bonus, self.soft_bonus, self.max_drop = (next(it) for _ in range(4))
        self.players = [Player(), Player()]
        self.t = timing

    def rate(self, frame):
        seconds = frame // 60
        if not self.rates or seconds < self.margin:
            return self.target
        return self.rates[min((seconds - self.margin) // self.step, len(self.rates) - 1)]

    def find_wave(self, grid):
        seen = set()
        cleared, colors, bonus = set(), set(), 0
        for x in range(W):
            for y in range(CLEAR_ROWS):
                c = grid[x][y]
                if (x, y) in seen or not 1 <= c <= 5:
                    continue
                group, stack = {(x, y)}, [(x, y)]
                while stack:
                    cx, cy = stack.pop()
                    for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                        if 0 <= nx < W and 0 <= ny < CLEAR_ROWS and (nx, ny) not in group and grid[nx][ny] == c:
                            group.add((nx, ny))
                            stack.append((nx, ny))
                seen |= group
                if len(group) >= self.clear_count:
                    cleared |= group
                    colors.add(c)
                    bonus += group_bonus(len(group), self.clear_count)
        garbage = set()
        for x, y in cleared:
            for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if 0 <= nx < W and 0 <= ny < CLEAR_ROWS and grid[nx][ny] == GARBAGE:
                    garbage.add((nx, ny))
        return cleared, garbage, len(colors), bonus

    def turn(self, f):
        t = self.t
        who, frame, pid, axis, child, soft = f[:6]
        me, opp = self.players[who], self.players[1 - who]
        ax, dx, dy = decode_placement(pid)
        cx = ax + dx
        assert 0 <= cx < W
        if dy == 1:
            ay = me.height(ax)
            cy = ay + 1
        elif dy == -1:
            cy = me.height(ax)
            ay = cy + 1
        else:
            ay, cy = me.height(ax), me.height(cx)
        assert ay < ROWS, "axis piece in the 14th row"
        assert cy <= ROWS and not me.grid[cx][cy] and not me.grid[ax][ay], "cell is taken"
        me.grid[ax][ay], me.grid[cx][cy] = axis, child
        split = abs(ay - cy) if dy == 0 else 0
        lock_y = max(ay, cy) if dy == 0 else ay  # row of the axis piece when the pair stopped
        frames = t["place"][0] + t["place"][1] * abs(ax - 2) + t["place"][2] * (11 - lock_y)
        if split:
            frames += (t["split"][split] if split <= 11 else t["split"][11] + 2 * (split - 11)) + TOP_SPLIT.get((lock_y, ax), 0)
        if self.soft_bonus:
            me.score += soft
            me.pool += soft

        chains = points_total = sent = offsetted = held = 0
        while True:
            cleared, garbage, colors, bonus = self.find_wave(me.grid)
            if not cleared:
                break
            chains += 1
            rate = self.rate(frame + frames - t["next"])  # the wave starts after the landing animation
            longest = 0
            for x in range(W):
                gone = 0
                for y in range(ROWS):
                    if (x, y) in cleared or (x, y) in garbage:
                        gone += 1
                    elif me.grid[x][y]:
                        longest = max(longest, gone)
            frames += t["wave"][0] if longest == 0 else t["wave"][1] + t["wave"][2] * (longest - 1)
            for x, y in cleared | garbage:
                me.grid[x][y] = 0
            for x in range(W):  # gravity; the 14th row stays where it is
                column = [c for c in me.grid[x][:ROWS] if c]
                me.grid[x][:ROWS] = column + [0] * (ROWS - len(column))
            multiplier = min(999, max(1, chain_bonus(chains) + COLOR_BONUS[colors] + bonus))
            points = 10 * len(cleared) * multiplier
            if me.all_clear:
                points += self.all_clear_bonus
                me.all_clear = False
            points_total += points
            me.score += points
            if chains < self.requirement:
                held += points  # worth garbage only if the chain reaches the requirement
                continue
            me.pool += points + held
            held = 0
            made, me.pool = divmod(me.pool, rate)
            cancel = min(made, me.pending)
            me.pending -= cancel
            cancel2 = min(made - cancel, me.confirmed)
            me.confirmed -= cancel2
            offsetted += cancel + cancel2
            rest = made - cancel - cancel2
            sent += rest
            opp.pending += rest
        all_clear = False
        if chains:
            opp.confirmed += opp.pending
            opp.pending = 0
            if not any(me.grid[x][y] for x in range(W) for y in range(ROWS)):
                me.all_clear = all_clear = True
        chain_end = frames

        # Garbage: the log holds the field after it fell; the columns chosen for
        # the remainder are random, so check that they are a possible choice.
        want_cells = f[15]
        drop = min(me.confirmed, self.max_drop)
        me.confirmed -= drop
        rows, extra = divmod(drop, W)
        heights = [me.height(x) for x in range(W)]
        placed, must, may = [0] * W, 0, 0
        for x in range(W):
            logged = [int(ch) for ch in want_cells[x * ROWS:(x + 1) * ROWS]]
            h = heights[x]
            assert logged[:h] == me.grid[x][:h], f"column {x} changed below the garbage"
            k = sum(1 for c in logged[h:] if c)
            assert logged[h:h + k] == [GARBAGE] * k and not any(logged[h + k:]), f"column {x}: not garbage on top"
            room = ROWS - h
            low, high = min(rows, room), min(rows + 1, room)
            assert k in (low, high), f"column {x}: {k} garbage, expected {low} or {high}"
            if low != high:
                must += k == high
                may += k == high
            else:
                may += 1  # a full column hides whether it got the extra one
            placed[x] = k
            me.grid[x][h:h + k] = [GARBAGE] * k
        assert must <= extra <= may, f"remainder {extra} does not fit the columns ({must}..{may})"
        if drop:
            total = sum(placed)
            if total:
                slowest = max(t["cell"][min(heights[x], 13)][x] for x in range(W) if placed[x])
                frames += slowest + t["shake"][min(total, 30)]
        dead = bool(me.grid[2][11])

        got = dict(chains=chains, points=points_total, sent=sent, offsetted=offsetted, dropped=drop,
                   all_clear=int(all_clear), dead=int(dead), frames=frames, chain_end=chain_end,
                   cells=me.cells(), score=me.score, pool=me.pool, pending=me.pending, confirmed=me.confirmed,
                   opp_pending=opp.pending, opp_confirmed=opp.confirmed)
        names = list(got)
        for name, value in zip(names, f[6:]):
            assert got[name] == value, f"{name}: simulator {got[name]}, log {value}"


def main():
    path = sys.argv[1]
    game, timing, games, turns = None, None, 0, 0
    with open(path, encoding="ascii") as log:
        for number, line in enumerate(log, 1):
            parts = line.split()
            try:
                if parts[0] == "TIMING":
                    next_frames, *v = [int(p) for p in parts[1:]]
                    cell = v[18:18 + 14 * W]
                    timing = {"next": next_frames, "place": v[0:3], "wave": v[3:6], "split": v[6:18],
                              "cell": [cell[y * W:(y + 1) * W] for y in range(14)], "shake": v[18 + 14 * W:]}
                    assert len(timing["shake"]) == 31
                elif parts[0] == "GAME":
                    game = Game([int(p) for p in parts[1:]], timing)
                    games += 1
                else:
                    fields = [p if i == 15 else int(p) for i, p in enumerate(parts[1:])]
                    game.turn(fields)
                    turns += 1
            except AssertionError as error:
                print(f"{path}:{number}: {error}")
                return 1
    if games < 100 or turns < 10000:
        print(f"log too small: {games} games, {turns} turns")
        return 1
    print(f"game log verified: {games} games, {turns} turns")
    return 0


if __name__ == "__main__":
    sys.exit(main())
