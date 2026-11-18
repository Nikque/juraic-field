# Chain board generator

A C++20 program that randomly generates boards (three to five colours) which fire an exact
chain of 1 to 19 waves when one last pair is placed. The URL of every board found is appended to
`19chain_urls.txt`. The shipped settings generate 100 new 19-chain boards of 76 pieces in four colours.
With the field set to 10 columns × 18 rows it generates chains of 1 to 47 waves ([10 × 18 fields](#10--18-fields)).

All board work is done on bitboards: one 16-bit lane per column, six columns in one SSE register, and
three registers for the colour code (48 bytes per board). The same core (`core/`) is also used by solo play,
versus play and the AI.

## Windows GUI

`JuraicFieldGenerator.exe` lets you edit the settings, generate and cancel, choose results, view 6×13 and
10×19 boards and play back chains. The CLI (`random_19_chain.exe` / `run.cmd`) remains available. For usage,
output folders, building and what is supported, see [GUI_README.md](GUI_README.md) (Japanese).

- The GUI writes board URLs in one of the formats of three external chain simulators. On the screen they are
  called 石川, 対戦パーク and mattulwan系（pndsngミラー） ([URL_FORMATS.md](URL_FORMATS.md)).
- It can edit boards, undo and redo, and play back, save and load the chains of edited boards.
- The board editor handles five normal colours plus garbage, point, hard and iron pieces and fixed walls.
  These special kinds are never generated.

About the CLI:

- Started directly from its executable, the CLI still shows Japanese text. When a console is attached, it
  switches the output code page to UTF-8 while it runs and restores it on exit.
- Logs redirected to a file are UTF-8 as well.
- Settings paths on Windows may contain Japanese characters and emoji.
- When the last line of the URL file has no line break, the next URL is still appended on a line of its own.

## What a generated board satisfies

The output is the board **right after the last pair has been placed**. It satisfies all of the following:

1. 6 columns × 13 rows, no gaps inside a column, nothing in the 14th row.
2. Firing it gives exactly the requested number of waves, and every wave clears **one group of exactly four**.
3. The 13th row never connects or clears, but falls when the pieces below it disappear.
4. After the chain, the cell where pairs appear (third column from the left, twelfth row from the bottom) is empty.
5. The board holds `4 * target_chain + extra` pieces. `extra` is chosen between `min_extra_pieces` and
   `max_extra_pieces`, and that many pieces remain after the chain.
6. There is at least one **last pair**: the two topmost pieces of one column, or the topmost pieces of two
   neighbouring columns, such that
   - nothing clears on the board without them,
   - the cell where pairs appear is empty on the board without them (the next pair can appear), and
   - the pair can travel to its place ([Reach rule](#reach-rule)).

The board itself may cover the cell where pairs appear right after the last pair is placed, as long as the
two placed pieces clear in the chain and item 4 holds.

For each board the program prints the four pieces that clear first (the trigger) and **every** last pair
that satisfies item 6.

### Reach rule

The pair appears in the third column (axis in row 12, child in row 13). Whether a position can be reached
is decided from the column heights alone:

- a column of height 11 or less can always be passed;
- a column of height 13 (filled up to the 13th row) can never be passed, because the axis piece cannot enter the 14th row;
- a column of height 12 can be passed only while the axis piece is lifted to the 13th row (wall crossing).
  It is lifted in one of three ways:
  - right after the spawn, when the second and the fourth columns are both 12 or higher (quick turn);
  - on a column exactly 11 high (floor kick);
  - on top of a 12-high column it has already climbed;
- once lifted, it may hop over **up to four** lower columns (on a six-column field, any number) before
  landing on a 12-high column. The floor kick may be made on the other side of the spawn column; then the
  columns crossed on the way back (including the third column) count as hopped;
- a vertical pair is placed only on a column of height 11 or less (nothing is left in the 14th row).

Beside the quick turn, the floor kick and a one-column hop from a 12-high column, hops after a floor kick
and over two to four columns are allowed: they are wall crossings possible in the real game (the number of
columns is `MAX_HOP` in `core/reach.h`).

This rule allows exactly the placements allowed by the movement model of v0.3.0 and earlier, which searched
wall kicks, floor kicks and quick turns breadth-first, without gravity.

- This was checked for all 6.45 million height combinations with `tools/compare_reach.cpp`. In v0.4.0, when
  `MAX_HOP` was 2, the only difference was moves hopping over three columns or more.
- The rule also agrees with the placements actually reachable by the frame-by-frame controls of the game
  (`game/control.h`; `tests/frame_tests.cpp`).

### Not guaranteed

- **No build sequence from an empty board is produced** (v0.3.0 and earlier did). The guarantee is only that
  placing the last pair fires the chain; how to stack up to that board is left to the player.
- Frame-by-frame gravity, lock delay, input timing and per-platform differences are not modelled. The boards
  are not matched against the piece sequence of any particular game. Garbage pieces are not generated.

### Every board is generated independently

Each attempt uses its own random stream, chosen by its attempt number, and starts from the board that remains
after the chain. One attempt saves at most one board. Found boards are never varied to mass-produce similar
ones. URLs already in `19chain_urls.txt`, and URLs already found in the same run, are skipped.

## How the search works

The chain is grown backwards. Starting from the residual board (only the extra pieces, nothing clears),
each step inserts a group of four cells into the middle of the columns. A step is accepted only if the new
board meets both of these:

- (i) apart from the inserted four, there is no group of four or more;
- (ii) the inserted four do not connect with neighbouring pieces of their colour.

Then the inserted group is the only thing that clears, and gravity gives back the previous board exactly.
So a board made by n steps always clears in exactly n waves of one group of four. The group inserted last
clears first, and the board after the chain is the starting board. This holds for every chain length, extra
count and colour count.

So no chain simulation runs inside the search. Each condition is handled as follows:

| Condition | Checked during the search |
| --- | --- |
| number of waves, single groups of four, trigger | follows from the construction: not checked |
| spawn cell empty after the chain | the final board is the residual board, which is built with at most 11 pieces in the third column |
| nothing in the 14th row | insertions keep every column at 13 or lower |
| nothing clears without the last pair | equivalent to "the pair holds a trigger piece": one mask test |
| the last pair can appear and reach its place | from the heights, per candidate pair |

With 11 extra pieces or fewer, the limit of 11 pieces in the third column holds by itself.

The insertions are done with bit operations:

- insertion into a column is a per-column multiplication (shift);
- whether a group of four or more exists is derived from four "has a same-colour neighbour" masks, without
  flood fill;
- candidate positions are found directly from bit sets of the positions that move a trigger piece and the
  positions that fit the column heights.

Each step keeps `candidates_per_parent` uniformly chosen children per board and at most `beam_width`
boards (beam search). The children are found by drawing random (position, colour) pairs and testing them,
stopping once enough are found, instead of listing all candidates. Only when 96 draws are not enough are all
candidates listed and the rest chosen from them, so the choice stays uniform. The step before the last keeps
every child.

### Guidance on nearly full boards

On a nearly full board the last pair has very little room. At least one trigger piece must be in a position
that belongs to the last pair (within the top two pieces of a column the pair can reach). From the column
heights alone, the positions where that trigger piece can be are these (`—`: impossible):

| Pieces on the board | Column 1 | Column 2 | Column 3 | Column 4 | Column 5 | Column 6 |
| ---: | --- | --- | --- | --- | --- | --- |
| 78 | — | — | row 12 | — | — | — |
| 77 | — | — | row 11 or higher | — | — | — |
| 76 (19 chains) | — | row 12 | row 10 or higher | row 12 | — | — |
| 75 | row 12 | row 11 or higher | row 9 or higher | row 11 or higher | row 12 | — |
| 72 (18 chains) | row 9 or higher | row 8 or higher | row 6 or higher | row 8 or higher | row 9 or higher | row 10 or higher |
| 68 (17 chains) | row 5 or higher | row 4 or higher | row 2 or higher | row 4 or higher | row 5 or higher | row 6 or higher |

Moreover, the last insertion has to move the trigger piece of the step before it, so the earlier triggers
must also be near the surface. So for boards of **72 pieces or more**, the six steps before the last use only
insertions that leave at most one old piece above the new group in one of its columns. This raises the share
of successful attempts for 19 chains / 76 pieces from 24% to 52%.

The guidance narrows the variety of the boards, so it is not used for boards of fewer than 72 pieces (17
chains or fewer without extra pieces), where the theoretical limits are loose. Whether a board has 72 pieces
or more depends on the number of pieces, not on the chain length, so a chain of 17 or fewer is guided too when
it has many extra pieces. With or without guidance, every saved board is checked against all conditions.

**Every saved board is checked against the full definition directly, without relying on the reasoning
above** (`verifySolution`). In addition, an independent Python cell simulator (`tests/verify_output.py`)
checks the same conditions, including that the printed last pairs are exactly all valid ones.

Attempts run on several threads. Attempt k always uses random stream k and results are committed in
order, so **the same settings give the same boards in the same order for any thread count and compiler**
(checked: MSVC and GCC produce the same 2000 URLs).

## Generator settings

| Key | Meaning | Shipped |
| --- | --- | --- |
| `field_columns`, `field_rows` | columns and rows that clear (one hidden row above them): 6 and 12, or 10 and 18 | 6, 12 |
| `target_chain` | number of waves, 1-19 (1-47 on 10 × 18) | 19 |
| `target_success_count` | new unique boards to save in this run | 100 |
| `initial_seed` | random seed (unsigned 64-bit) | 20261004 |
| `color_count` | 3, 4 or 5 | 4 |
| `beam_width` | boards kept per step | 48 |
| `restarts` | maximum number of independent attempts | 100000 |
| `candidates_per_parent` | children kept per board | 2 |
| `min_extra_pieces` / `max_extra_pieces` | range of extra pieces, at most `78 - 4 * target_chain` (`188 - 4 * target_chain` on 10 × 18) | 0 / 0 |
| `threads` | search threads, 0 = automatic (half of the logical processors) | 0 |

`config.ini` may be saved as UTF-8 with or without a BOM. It uses `key = value` lines; blank lines and
comments after `;` or `#` are ignored, and section headers such as `[Generator]` are skipped. Unknown keys,
duplicates, missing required keys and invalid numbers are errors.

- Six keys are required: `initial_seed`, `color_count`, `beam_width`, `restarts`, `candidates_per_parent`,
  `target_success_count`. The defaults of the others are `field_columns=6`, `field_rows=12`,
  `target_chain=19`, `min_extra_pieces=0`, `max_extra_pieces=0`, `threads=0`.
- `initial_seed` is a decimal integer from 0 to 18446744073709551615. It is not the seed that reproduces the
  pieces of a game.
- A larger `beam_width` raises the success rate of one attempt and makes each attempt take longer by as much;
  the number of boards per second hardly changes.
- `color_count` 4 is the one of normal matches.
- With `candidates_per_parent` at 1 the candidates do not spread, and almost no attempt succeeds.
- Extra pieces: `0 <= min <= max <= 78 - 4 * target_chain`. Equal values fix the number of pieces; different
  values choose a number in the range for each attempt. Boards with an
  odd number of pieces are generated as they are.
- Positive integer keys other than `initial_seed` go up to 2147483647; `threads` goes up to 256.

Examples: only 19 chains of 77 pieces: `target_chain=19`, `min_extra_pieces=1`, `max_extra_pieces=1`.
18 chains of 72 to 78 pieces: `target_chain=18`, `min_extra_pieces=0`, `max_extra_pieces=6`.

The output goes to the **working folder at run time**. The settings file can be given as an argument:

```powershell
.\random_19_chain.exe C:\path\to\config.ini
```

Exit codes: 0 target reached, 1 attempt limit reached (URLs found so far are kept), 2 configuration or I/O error.

### Reading the output

```
trigger cells (x from left, y from bottom, 1-based): (3,9) (3,10) (3,11) (3,12)
last pair options (V vertical, H horizontal): V(3,12)(3,13)
```

`trigger cells` are the four pieces that clear first, and `last pair options` are the candidates for the
last pair (V vertical, H horizontal; placing any one of them last fires the chain). Coordinates are the column
from the left and the row from the bottom, starting at 1.

## Speed

Windows 11 x64, Ryzen 9 9950X, Visual Studio 2022, Release, 2026-10-04, seed 20261004, beam 48 / 2,
starting from an empty URL history. The times include start-up and failed attempts.
Other work was running on the machine, so the numbers are indicative.

| Setting | v0.3.0 (20 boards) | new, 1 thread (2000 boards) | new, 16 threads (2000 boards) |
| --- | ---: | ---: | ---: |
| 19 chains, 76 pieces, 4 colours | 3.81 s (5.2/s) | 1.49 s (1340/s) | 0.17 s |
| 19 chains, 77 pieces | 8.65 s (2.3/s) | 1.83 s (1090/s) | 0.20 s |
| 19 chains, 78 pieces | 17.54 s (1.1/s) | 2.80 s (714/s) | 0.28 s |
| 18 chains, 72-78 pieces | 2.87 s (7.0/s) | 1.29 s (1550/s) | 0.15 s |
| 10 chains, 40 pieces | 0.33 s | 0.55 s (3600/s) | 0.20 s |
| 19 chains, 76 pieces, 5 colours | 5.51 s (3.6/s) | 1.39 s (1440/s) | 0.18 s |

Generating 20000 boards of 19 chains / 76 pieces on 16 threads took 1.28 s (about 15,600 per second).
v0.3.0 also searched for a build sequence from an empty board, so this is not a like-for-like comparison.
See [BENCHMARK.md](BENCHMARK.md) (Japanese) for details.

```powershell
python tools/benchmark.py build/Release/random_19_chain.exe --target 2000 --restarts 100000 --threads 1 --save-log run.log
python tests/verify_output.py run.log --allow-extra
```

## 10 × 18 fields

With `field_columns = 10` and `field_rows = 18` in `config.ini` (in the GUI: "10列×18段" under "盤面") the
generator works on a field of 10 columns × 18 rows:

- The lower 18 rows connect and clear. The 19th row above them is hidden: it never connects or clears, but
  falls when the pieces below it disappear. Nothing is put into a 20th row.
- Pairs appear in the fifth column from the left. The game is over when the 18th row of the fifth or the sixth
  column is filled, so those two cells are empty after the chain and on the board before the last pair. They
  may be filled right after the last pair is placed.
- The reach rule is the six-column rule with the rows moved: a column of 17 or lower can be passed, a column
  of 19 never, and a column of 18 only while the axis piece is lifted into the 19th row. When the fifth and
  sixth columns are filled up to 17, a floor kick lifts the pair.
- 1 to 47 chains: before the last pair the fifth and sixth columns hold at most 17 pieces, so a board holds at
  most `8 × 19 + 2 × 17 + 2 = 188` pieces, `188 / 4 = 47` waves. Extra pieces: `0 <= min <= max <= 188 - 4 * target_chain`.
- Everything else in [What a generated board satisfies](#what-a-generated-board-satisfies) is the same as on
  six columns, such as every wave clearing one group of exactly four and the last pair firing the chain.

No external chain simulator shows a field of this size, so no URL is written.

- Instead, a **board code**, the board written on one line, is appended to `boards_10x18.txt`. The board codes
  are also used to skip duplicates.
- A board code is `10x18:` followed by the 190 cells, from the 19th row down to the first, each row left to
  right, as `.` (empty) and `R G B Y P`.
- The CLI output has the same form as for six columns, with a `field=10x18` line before the board and a
  `board code:` line instead of the `URL:` line.
- The GUI shows these results, plays back their chains and edits the boards ([GUI_README.md](GUI_README.md)).

### Search on 10 × 18

The search is the same backward insertion as on six columns.

- The field type is a separate one with one 32-bit word per column (`generator/grid_field.h`).
- The search is the same steps written for a field of any size (`generator/grid_generator.h`).
- A test confirms that on 6 × 13 this search finds, attempt by attempt, exactly the boards of the six-column
  search (`tests/grid_tests.cpp`).

On boards with almost no room left (at most 20 unused cells), the plain search does not reach a shape that
the last pair fires. So these rules are added:

- Before the last 12 steps, the columns more than two away from the fifth and sixth are filled first, and room
  for the last 12 steps is kept in the fifth and sixth columns and the two columns on each side of them.
- In the last 12 steps, only insertions that leave at most one old piece above the new group in one of the
  fifth and sixth columns and the two columns on each side are used.
- At each step, the boards kept are those with the least room, weighted by the distance from the fifth and
  sixth columns (at random among equal ones).
- With 78 extra pieces or more, the extra pieces are placed starting from the columns farthest from the fifth
  and sixth.
- In three colours, the four inserted pieces must differ in colour from every piece around them, so there are
  few places to insert. For three colours only, the rules change as follows (`Grid10x19ThreeColors`):
  - no room is kept near the fifth and sixth columns (the whole board counts as near them);
  - boards on which the next step can insert nothing are not kept;
  - differences in height between neighbouring columns weigh heavily in the ranking of the boards kept,
    because a deep narrow gap can be filled only by upright groups of four;
  - in the last six steps, four times as many boards are kept.
- For 46 chains in three colours, the rules of four colours found 0 boards in 300 attempts, and the rules of
  three colours found 119 in 300.
- With these rules, four and five colours find more boards per attempt, but each attempt is 5 to 7 times
  slower and fewer boards are found per second. So they are not used for four and five colours.

The values of the rules were chosen by measurements with `tools/grid_bench.cpp` (the numbers are in the notes
of `generator/grid_field.h`). For example, for 47 chains in four colours, attempts without the rules found 0
boards of 300, and with them 50 of 200.

### Speed on 10 × 18

2026-10-17, Ryzen 9 9950X, Visual Studio 2022, Release. Time to generate 100 boards from an empty history with
seed 20261004, beam 48 / 2, including start-up and failed attempts. Other work was running on the machine, so
the numbers are indicative. All 100 boards passed the independent verifier (`tests/verify_output.py`).

| Setting | Attempts | 1 thread | 16 threads |
| --- | ---: | ---: | ---: |
| 47 chains, 188 pieces, 4 colours | 406 | 10.98 s (9.1/s) | 1.13 s (88/s) |
| 47 chains, 188 pieces, 5 colours | 184 | 4.50 s (22/s) | 0.58 s (173/s) |
| 44 chains, 176 pieces, 4 colours | 164 | | 0.53 s (189/s) |
| 40 chains, 160 pieces, 4 colours | 182 | | 0.56 s (177/s) |
| 30 chains, 120 pieces, 4 colours | 102 | | 0.23 s (444/s) |
| 19 chains, 76 pieces, 4 colours | 100 | | 0.23 s (441/s) |
| 47 chains, 188 pieces, 3 colours | 1742 | 323.38 s (0.3/s) | 33.19 s (3.0/s) |
| 44 chains, 176 pieces, 3 colours | 117 | | 2.40 s (42/s) |
| 40 chains, 160 pieces, 3 colours | 338 | | 1.12 s (90/s) |

The three-colour rows were measured on the same day with the three-colour rules of v0.9.4.

### Not guaranteed and weak points

The 555 settings of 1 to 47 chains × three to five colours × extra pieces (none, one, the largest number,
a range up to it) were each generated once, within 20000 attempts or 60 seconds (2026-10-17, four threads).
Three colours were measured again with the three-colour rules of v0.9.4.

- In four and five colours, every setting was found.
- In three colours, every setting with no, one, or a range of extra pieces was found (47 chains included).
- In three colours with the largest number of extra pieces (the board after the chain is nearly full),
  nothing was found for 8, 9, 19, 20, 22 and 24-27 chains.
- The likely reason is that the colours of the extra pieces are chosen at random, so the three colours mix
  around the remaining room and no colour can be inserted. This has not been confirmed.
