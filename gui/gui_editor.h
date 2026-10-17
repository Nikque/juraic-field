#pragma once
#include "gui_model.h"
#include <algorithm>

namespace juraic_gui {
// The editor allows empty cells, floating cells and kinds the generator never
// produces. Kinds 1..6 are the engine's codes; 7..10 exist only here.
enum Piece : Cell { Empty = 0, Red = 1, Green = 2, Blue = 3, Yellow = 4, Purple = 5,
                    Garbage = 6, PointPiece = 7, Hard = 8, Iron = 9, Wall = 10 };
inline constexpr wchar_t pieceLetters[] = L" RGBYP○$HI#";

// Gravity with fixed walls: each stretch between walls is compacted on its own.
inline bool settle(Board& b) {
    const Board before = b;
    for (int x = 0; x < b.width; ++x) {
        int dst = 0;
        for (int y = 0; y < b.height; ++y) {
            const Cell value = b.cells[x][y];
            if (value == Wall) {
                dst = y + 1;
                continue;
            }
            if (value) {
                b.cells[x][y] = Empty;
                b.cells[x][dst++] = value;
            }
        }
    }
    return b != before;
}

struct EditorWave {
    int score = 0, colored = 0;
};
// One clearing wave without gravity. Which coloured pieces clear (and the
// hidden-row rule, groups and colours for the score) comes from the engine:
// core/ for 6 x 13 boards (F = BitField), generator/grid_field.h for 10 x 19
// ones; the effect on the special kinds next to them is applied here.
template <class F, class WaveT, class Bits>
inline EditorWave editorClearOn(Board& b, int chain, const Bits& clearMask) {
    F colors;
    for (int x = 0; x < b.width; ++x)
        for (int y = 0; y < b.height; ++y)
            if (b.cells[x][y] >= Red && b.cells[x][y] <= Purple) colors.set(x, y, b.cells[x][y]);
    EditorWave wave;
    WaveT w;
    if (!findWave(colors, w)) return wave;
    wave.colored = w.pieces;
    // Cells touched by cleared pieces from one direction or more, and from two or more.
    const Bits a = w.cleared.up(), c = w.cleared.down(), d = w.cleared.left(), e = w.cleared.right();
    const Bits once = (a | c | d | e) & clearMask;
    const Bits twice = ((a & c) | (a & d) | (a & e) | (c & d) | (c & e) | (d & e)) & clearMask;
    int base = 10 * wave.colored, points = 0;
    for (int x = 0; x < b.width; ++x)
        for (int y = 0; y < b.clearRows(); ++y) {
            Cell& cell = b.cells[x][y];
            bool erase = w.cleared.get(x, y);
            if (once.get(x, y)) {
                if (cell == Garbage || cell == PointPiece) {
                    erase = true;
                    if (cell == PointPiece) points += 50;
                } else if (cell == Hard) {
                    if (twice.get(x, y)) {
                        erase = true;
                        base += 60;
                    } else {
                        cell = Garbage;
                        base += 10;
                    }
                }
            }
            if (erase) cell = Empty;
        }
    wave.score = base * std::max(1, CHAIN_BONUS[std::clamp(chain, 1, 19)] + COLOR_BONUS[w.colors] + w.groupBonus) + points;
    return wave;
}
inline EditorWave editorClear(Board& b, int chain) {
    if (b.wide()) return editorClearOn<GridField<G10>, GridFieldWave<G10>>(b, chain, GRID_CLEAR_MASK<G10>);
    return editorClearOn<BitField, Wave>(b, chain, MASK_12);
}
inline std::vector<Frame> editorTimeline(Board b) {
    std::vector<Frame> frames{{b, {}, {}, "編集盤面（開始前）"}};
    if (settle(b)) frames.push_back({b, {}, {}, "初期落下後"});
    int score = 0;
    // (a board of 6 x 13 cells has at most 19 waves; one of 10 x 19 at most 47 of four coloured pieces)
    const int maxWaves = b.wide() ? G10::W * G10::H / 4 : 19;
    for (int n = 1; n <= maxWaves; ++n) {
        const EditorWave wave = editorClear(b, n);
        if (!wave.colored) break;
        score += wave.score;
        frames.push_back({b, {}, {}, std::to_string(n) + "連鎖: 消去・変化後", n, score});
        settle(b);
        frames.push_back({b, {}, {}, std::to_string(n) + "連鎖: 落下後", n, score});
    }
    frames.back().label += " / シミュレーション終了";
    return frames;
}

struct Editor {
    Board board{};
    std::vector<Board> undo, redo;
    void replace(const Board& value) {
        if (board == value) return;
        undo.push_back(board);
        if (undo.size() > 256) undo.erase(undo.begin());
        redo.clear();
        board = value;
    }
    void back() {
        if (undo.empty()) return;
        redo.push_back(board);
        board = undo.back();
        undo.pop_back();
    }
    void forward() {
        if (redo.empty()) return;
        undo.push_back(board);
        board = redo.back();
        redo.pop_back();
    }
};
// Version 1: a 6 x 13 board, top row first. Version 2: the size (10 19) on its own line, then the same.
inline void saveBoard(const std::filesystem::path& path, const Board& b) {
    std::ofstream out(path, std::ios::binary);
    if (b.wide()) out << "JURAIC_FIELD_BOARD 2\n" << b.width << ' ' << b.height << '\n';
    else out << "JURAIC_FIELD_BOARD 1\n";
    for (int y = b.height - 1; y >= 0; --y)
        for (int x = 0; x < b.width; ++x) out << int(b.cells[x][y]) << (x == b.width - 1 ? '\n' : ' ');
    out.flush();
    if (!out) throw std::runtime_error("編集盤面を保存できません。");
}
inline Board loadBoard(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > 4096) throw std::runtime_error("盤面ファイルが大きすぎます。");
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    int version;
    if (!(in >> magic >> version) || magic != "JURAIC_FIELD_BOARD" || (version != 1 && version != 2))
        throw std::runtime_error("対応する編集盤面形式ではありません。");
    Board b;
    if (version == 2) {
        int width, height;
        if (!(in >> width >> height) || width != G10::W || height != G10::H)
            throw std::runtime_error("対応していない盤面の大きさです。");
        b = emptyBoard(true);
    }
    for (int y = b.height - 1; y >= 0; --y)
        for (int x = 0; x < b.width; ++x) {
            int v;
            if (!(in >> v) || v < 0 || v > 10) throw std::runtime_error("不正な種別です。");
            b.cells[x][y] = Cell(v);
        }
    std::string extra;
    if (in >> extra) throw std::runtime_error("盤面ファイルに余分なデータがあります。");
    return b;
}
} // namespace juraic_gui
