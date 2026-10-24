# Chain board generator

A C++20 program that randomly generates boards (three to five colours) which fire an exact
chain of 1 to 19 waves when one last pair is placed. The URL of every board found is appended to
`19chain_urls.txt`. The shipped settings generate 100 new 19-chain boards of 76 pieces in four colours.
With the field set to 10 columns × 18 rows it generates chains of 1 to 47 waves ("10 × 18 fields" below).

All board work is done on bitboards: one 16-bit lane per column, six columns in one SSE register,
three registers for the colour code (48 bytes per board). The same core (`core/`) is used by solo play,
versus play and the AI.

## Windows GUI

`JuraicFieldGenerator.exe` offers settings, asynchronous generation with cancellation, a result list, a
6×13 or 10×19 board and chain playback. The CLI (`random_19_chain.exe` / `run.cmd`) remains available. See
[GUI_README.md](GUI_README.md) (Japanese) for usage and [URL_FORMATS.md](URL_FORMATS.md) for the simulator URLs.
The board editor supports five colours, garbage, point, hard and iron pieces and fixed walls; these
special kinds are never generated.

## What a generated board satisfies

The output is the board **right after the last pair has been placed**.

1. 6 columns × 13 rows, no gaps inside a column, nothing in the 14th row.
2. Firing it gives exactly the requested number of waves, and every wave clears **one group of exactly four**.
3. The 13th row never connects or clears, but falls when the pieces below it disappear.
4. After the chain, the cell where pairs appear (third column, twelfth row) is empty.
5. The board holds `4 * target_chain + extra` pieces; `extra` pieces remain after the chain.
6. There is at least one **last pair**: the two topmost pieces of one column, or the topmost pieces of two
   neighbouring columns, such that without them
   - nothing clears,
   - the cell where pairs appear is empty, and
   - the pair can travel to its place (reach rule below).

The board itself may cover the cell where pairs appear: the last pair may be placed there as long as the
chain frees it again (item 4). For each board the program prints the four pieces that clear first and
**every** last pair that satisfies item 6.

### Reach rule

The pair appears in the third column (axis in row 12, child in row 13). Whether a position can be reached
is decided from the column heights alone:

- a column of height 11 or less can always be passed;
- a column of height 13 can never be passed (the axis piece cannot enter the 14th row);
- a column of height 12 can be passed only while the axis piece is lifted to the 13th row (wall crossing).
  It is lifted right after the spawn when both neighbours of the third column are 12 or higher (quick
  turn), on a column exactly 11 high (floor kick), or on top of a 12-high column it has already climbed;
- once lifted it may hop over **up to two** lower columns before landing on the 12-high column. The floor
  kick may be made on the other side of the spawn column; the columns crossed on the way back count;
- a vertical pair needs a column of height 11 or less (nothing is left in the 14th row).

Beside the quick turn, the floor kick and a one-column hop from a 12-high column, hops after a floor kick
and over two to four columns are allowed: they are ordinary wall crossings of the real game (`MAX_HOP` in
`core/reach.h`). Every placement this rule allows is also allowed by the movement model of v0.3.0 (kicks
and quick turns searched without gravity); this was checked for all 6.45 million height combinations with
`tools/compare_reach.cpp`.

### Not guaranteed

- **No build sequence from an empty board is produced** (v0.3.0 did). The guarantee is only that placing
  the last pair fires the chain.
- Frame timing, lock delay and per-platform differences are not modelled. Garbage pieces are not generated.

### Every board is generated independently

Each attempt uses its own random stream and starts from the board that remains after the chain; one
attempt saves at most one board. Found boards are never varied to mass-produce similar ones. URLs already
in `19chain_urls.txt` are skipped.

## How the search works

The chain is grown backwards. Starting from the residual board (only the extra pieces, nothing clears),
each step inserts a group of four cells into the columns. A step is accepted only if, on the new board,
(i) no group of four or more exists among the old pieces and (ii) no old piece next to the inserted cells
has their colour. Then the inserted group is the only thing that clears, and gravity gives back the
previous board exactly. By induction a board made by n steps clears in exactly n waves of one group of
four, starting with the group inserted last and ending as the residual board - for every chain length,
extra count and colour count. So no chain simulation runs inside the search:

| Condition | Checked during the search |
| --- | --- |
| number of waves, single groups of four, trigger | follows from the construction: not checked |
| spawn cell empty after the chain | the final board is the residual board, which is built with at most 11 pieces in the third column |
| nothing in the 14th row | insertions keep every column at 13 or lower |
| nothing clears without the last pair | equivalent to "the pair holds a trigger piece": one mask test |
| the last pair can appear and reach its place | from the heights, per candidate pair |

Insertion is a per-column multiplication, "is there a group of four or more" is derived from four
same-colour-neighbour masks without flood fill, and candidate positions come from precomputed bit sets.
Each step keeps `candidates_per_parent` uniformly chosen children per board and at most `beam_width`
boards; the children are found by testing random (position, colour) pairs instead of listing them all.
The step before the last keeps every child.

On a nearly full board the last pair has very little room: with 76 pieces a trigger piece of a valid last
pair must sit at row 10 or higher in the third column or at row 12 in the second or fourth, and nowhere
else. So for boards of **72 pieces or more**, the six steps before the last use only insertions that leave
at most one old piece above the new group in one of its columns. This raises the share of successful
attempts for 19 chains / 76 pieces from 24% to 52%. Smaller boards, where the rules leave the trigger almost
free, are searched without this guidance so that their variety is not narrowed.

**Every saved board is checked against the full definition directly** (`verifySolution`), and an
independent Python cell simulator (`tests/verify_output.py`) checks the same conditions, including that
the printed last pairs are exactly all valid ones.

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

The first six keys are required.
Exit codes: 0 target reached, 1 attempt limit reached (URLs found so far are kept), 2 configuration or I/O error.

## Speed

Windows 11 x64, Ryzen 9 9950X, Visual Studio 2022, Release, 2026-10-04, seed 20261004, beam 48 / 2.
Other work was running on the machine, so the numbers are indicative.

| Setting | v0.3.0 (20 boards) | new, 1 thread (2000 boards) | new, 16 threads (2000 boards) |
| --- | ---: | ---: | ---: |
| 19 chains, 76 pieces, 4 colours | 3.81 s (5.2/s) | 1.49 s (1340/s) | 0.17 s |
| 19 chains, 77 pieces | 8.65 s (2.3/s) | 1.83 s (1090/s) | 0.20 s |
| 19 chains, 78 pieces | 17.54 s (1.1/s) | 2.80 s (714/s) | 0.28 s |
| 18 chains, 72-78 pieces | 2.87 s (7.0/s) | 1.29 s (1550/s) | 0.15 s |
| 19 chains, 76 pieces, 5 colours | 5.51 s (3.6/s) | 1.39 s (1440/s) | 0.18 s |

v0.3.0 also searched for a build sequence from an empty board, so this is not a like-for-like comparison.
See [BENCHMARK.md](BENCHMARK.md) (Japanese).

## 10 × 18 fields

With `field_columns = 10` and `field_rows = 18` in `config.ini` (in the GUI: "10列×18段" under "盤面") the
generator works on a field of 10 columns:

- the lower 18 rows connect and clear; the 19th row is hidden: it never connects or clears but falls when the
  pieces below it disappear; nothing is put into a 20th row;
- pairs appear in the fifth column, and the game is over when the fifth or the sixth column reaches the 18th
  row: those two cells are empty after the chain and before the last pair;
- the reach rule is the one above with the rows moved (17 or lower: passable; 19: never; 18: only while the
  axis piece is lifted into the 19th row);
- 1 to 47 chains: before the last pair the fifth and sixth columns hold at most 17 pieces, so a board holds at
  most `8 × 19 + 2 × 17 + 2 = 188` pieces, `188 / 4 = 47` waves; extra pieces up to `188 - 4 * target_chain`.

No simulator site shows a field of this size, so no URL is written; instead a **board code** is appended to
`boards_10x18.txt` (and used to skip duplicates): `10x18:` and the 190 cells from the 19th row down, each
row left to right, as `.` (empty) and `R G B Y P`. The output lists `field=10x18` before the board and a
`board code:` line instead of the URL. The GUI shows, plays and edits these boards.

The search is the same backward insertion, on a field type with one 32-bit word per column
(`generator/grid_field.h`) and the same steps written for any field (`generator/grid_generator.h`); on
6 × 13 that search finds, attempt by attempt, exactly the boards of the 6 × 13 search
(`tests/grid_tests.cpp`). Boards with at most 20 cells to spare follow a plan: before the last 12 steps the
columns more than two away from the fifth and sixth are filled and room for the last steps is kept next to
them; in the last 12 steps the new group goes near the top there; at every step the boards with the least
room, weighted by the distance from the fifth and sixth columns, are kept. For 47 chains in four colours,
attempts without the plan found 0 boards of 300, with it 50 of 200 (`tools/grid_bench.cpp`; the numbers
behind every value are in `generator/grid_field.h`).

Speed (2026-10-17, same machine and settings as above, 100 boards each, all checked by the independent verifier):
47 chains of 188 pieces in four colours 10.98 s on one thread (9.1/s) and 1.13 s on 16 threads (88/s); in
five colours 4.50 s and 0.58 s; 40 chains in four colours 0.56 s on 16 threads.

Three colours leave few insertions whose colour none of their neighbours has, so for three colours the plan
is changed (`Grid10x19ThreeColors`): no room is kept next to the fifth and sixth columns (the whole board
counts as next to them), boards on which the next step can insert nothing are dropped, steps in height between
neighbouring columns weigh heavily when boards are kept (a deep narrow gap takes upright groups of four only),
and four times as many boards are kept in the last six steps. 46 chains in three colours: 0 of 300 attempts
with the plan of four colours, 119 of 300 with this one; 47 chains: 100 boards in 323 s on one thread, 33 s
on 16 threads. In four and five colours this plan finds more boards an attempt but fewer a second, so it is
used for three colours only.

Of the 555 settings of 1 to 47 chains × three to five colours × extra pieces (none, one, the largest number,
a range up to it), one board each within 20000 attempts or 60 seconds (2026-10-17, four threads; three colours
measured again with the plan of v0.9.4): every setting in four and five colours was found, and in three
colours every setting but those with the largest number of extra pieces for 8, 9, 19, 20, 22 and 24-27 chains.
