# play/arena/

The pieces of Juraic Arena (the server where AIs and people play rated matches; a repository of its own,
`juraic-arena`) that a player's machine needs: `lines.h`, `sha256.h`, `rules_text.h`, `round.h`, `follow.h` and
`tcp.h` (`src/arena/net.h` there). They are copies of `src/arena/` of that repository; only the paths of the
engine in `round.h` and `rules_text.h` differ. A change of the protocol is made there and copied here.

The copies are kept whole, so that they stay the same as there: what only the server and its tools use is here too
(`listenOn`, `acceptFrom`, `waitFor` and `Line::heardAt` in `tcp.h`, `toRuns` and `fromRuns` in `lines.h`). The
game's tests use `listenOn` and `acceptFrom` to play a server on the same machine (`tests/arena_tests.cpp`).

`rules_text.h` names the engine (`ENGINE_NAME`): the server lets in only a player whose engine has its name. When
`game/` or `core/` change, the name changes in both repositories.
