# media — the pictures and the music that come with Juraic Field

The build copies `background/` and `bgm/` next to `JuraicField.exe`, where the game looks for them. Each place and
each scene has a folder of its own; a file put into one of them can be chosen on the settings screen
(表示と音). Without these folders the game runs with plain fields and without music.

The pictures in `background/` and the music in `bgm/` were made for this program. Everything in this folder is
under the same licence as the program (MIT, see `../LICENSE`).

## Pictures (`background/`)

| File | Place | How it was made |
| --- | --- | --- |
| `1p/field-1p-silhouette.png` | the field of the first player (and of the one player) | drawn by calculation: `tools/make_pictures.py` |
| `2p/field-2p-juraic.png` | the field of the second player | the author's character Juraic Vaid: the picture `source/juraic-vaid-original.webp`, cut out of its white ground, made dark and put on a dark ground by `tools/make_pictures.py` |
| `other/stage-grid.png` | the rest of the screen | drawn by calculation: `tools/make_pictures.py` |

`tools/make_pictures.py` draws by calculation and uses no picture-making AI. The one picture it starts from is
`source/juraic-vaid-original.webp` (1024 x 1536, the character on a white ground); the game and its build do not
use that file.

## Music (`bgm/`)

| File | Scene | How it was made |
| --- | --- | --- |
| `versus/synth-versus.flac` | a match (the piece the game begins with) | written note by note and sounded by calculation: `tools/make_music.py` (no samples, no recordings, no music-making AI) |
| `toko/synth-solo.flac` | one player (the piece the game begins with) | the same |
| `menu/synth-menu.flac` | the menu and the settings (the piece the game begins with) | the same |
| `versus/ace-versus-1.flac`, `versus/ace-versus-2.flac` | a match | generated with the music model ACE-Step 1.5 through ComfyUI (`tools/ace_run.py`), then cut and faded so that the end runs on into the beginning (`tools/loop_fix.py`) |
| `toko/ace-solo-1.flac`, `toko/ace-solo-2.flac` | one player | the same |
| `menu/ace-menu-1.flac`, `menu/ace-menu-2.flac` | the menu and the settings | the same |

Every piece ends where it begins, so the game can repeat it without a break.

The model card of ACE-Step 1.5 declares the MIT licence and says that the music it generates may be used
commercially (https://huggingface.co/ACE-Step/acestep-v15-base, read on 2026-10-13; the pieces were generated with
the "turbo" checkpoint as ComfyUI packages it).

## Size

`tools/make_music.py` writes `.wav` files. For the repository and the package every piece was packed as FLAC at the
strongest compression by `tools/lighten.py`, which reads each result back and compares it with its source sample by
sample (the pieces that were `.flac` already were packed again the same way). The pictures could not be made
smaller without changing them and are as their tool wrote them. `tools/look_sound.py` shows what a sound file
holds and where two files differ.

## Tools (`tools/`)

They were run with the Python of a ComfyUI installation (numpy, scipy, Pillow, PyAV); `ace_run.py` needs a running
ComfyUI with the ACE-Step 1.5 checkpoint. They are kept here as the record of how the files were made; the game
and its build do not need them. `make_pictures.py` and `make_music.py` write into `background/` and `bgm/` beside
`tools/`; the files of the package stand one folder further down, by the place or the scene they are for.
