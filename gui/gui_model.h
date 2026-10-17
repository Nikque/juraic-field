#pragma once
// Data shared by the GUI, its tests and the result files: settings text,
// playback frames and the versioned .jfchain archive. All game logic comes from
// the engine (core/ and generator/); nothing is re-implemented for display.
#include "../generator/generator.h"
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace juraic_gui {
using namespace juraic;
using Point = std::pair<int, int>; // x from the left, y from the bottom (0-based)
using G10 = Grid10x19;

// A plain grid for drawing, editing and files. Unlike the engine's bit planes
// it can also hold the editor-only piece kinds (see gui_editor.h). It is 6 x 13
// or 10 x 19 (the rows with the hidden row); cells outside the size stay empty.
struct Board {
    int width = 6, height = 13;
    std::array<std::array<Cell, 19>, 10> cells{}; // [x][bottom-up y]
    bool operator==(const Board&) const = default;
    bool wide() const { return width == G10::W; }
    int clearRows() const { return height - 1; } // rows that connect and clear
};
inline Board emptyBoard(bool wide) {
    Board b;
    if (wide) b.width = G10::W, b.height = G10::H;
    return b;
}
inline Board boardFromBits(const BitField& f) {
    Board b;
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < H; ++y) b.cells[x][y] = f.get(x, y);
    return b;
}
inline Board boardFromGrid(const GridField<G10>& f) {
    Board b = emptyBoard(true);
    for (int x = 0; x < G10::W; ++x)
        for (int y = 0; y < G10::H; ++y) b.cells[x][y] = f.get(x, y);
    return b;
}
// Only the kinds the engine knows (colours and garbage); the rest stay empty.
inline BitField bitsFromBoard(const Board& b) {
    BitField f;
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < H; ++y)
            if (b.cells[x][y] >= 1 && b.cells[x][y] <= GARBAGE) f.set(x, y, b.cells[x][y]);
    return f;
}
inline GridField<G10> gridFromBoard(const Board& b) {
    GridField<G10> f;
    for (int x = 0; x < G10::W; ++x)
        for (int y = 0; y < G10::H; ++y)
            if (b.cells[x][y] >= 1 && b.cells[x][y] <= GARBAGE) f.set(x, y, b.cells[x][y]);
    return f;
}
inline std::vector<Point> pointsOf(FieldBits bits) {
    std::vector<Point> points;
    forEachCell(bits, [&](int x, int y) { points.emplace_back(x, y); });
    return points;
}
inline std::vector<Point> pointsOf(const GridBits<G10>& bits) {
    std::vector<Point> points;
    forEachCell(bits, [&](int x, int y) { points.emplace_back(x, y); });
    return points;
}

inline GeneratorConfig defaults() {
    GeneratorConfig c;
    c.initialSeed = 20261004;
    c.colorCount = 4;
    c.beamWidth = 48;
    c.restarts = 100000;
    c.candidatesPerParent = 2;
    c.targetSuccessCount = 100;
    c.targetChain = 19;
    return c;
}
inline std::string configText(const GeneratorConfig& c) {
    std::ostringstream o;
    o << "[Generator]\n";
    if (c.wide()) o << "field_columns = " << c.fieldColumns << '\n' << "field_rows = " << c.fieldRows << '\n';
    o << "target_chain = " << c.targetChain << '\n'
      << "target_success_count = " << c.targetSuccessCount << '\n'
      << "initial_seed = " << c.initialSeed << '\n'
      << "color_count = " << c.colorCount << '\n'
      << "beam_width = " << c.beamWidth << '\n'
      << "candidates_per_parent = " << c.candidatesPerParent << '\n'
      << "restarts = " << c.restarts << '\n'
      << "min_extra_pieces = " << c.minExtraPieces << '\n'
      << "max_extra_pieces = " << c.maxExtraPieces << '\n'
      << "threads = " << c.threads << '\n';
    return o.str();
}
inline void writeConfig(const std::filesystem::path& path, const GeneratorConfig& c) {
    std::ofstream f(path, std::ios::binary);
    f << configText(c);
    f.flush();
    if (!f) throw std::runtime_error("設定を保存できません: " + pathToUtf8(path));
}
constexpr int INPUT_COUNT = 10;
// No floating point conversion, including values above 2^53. `wide`: the 10 x 18 field.
inline GeneratorConfig parseInputs(const std::array<std::string, INPUT_COUNT>& t, bool wide = false) {
    for (const auto& s : t)
        if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("設定は半角の10進整数で入力してください。");
    const auto orZero = [](const std::string& s, const char* key) { return s == "0" ? 0 : parsePositiveInt(s, key); };
    GeneratorConfig c;
    if (wide) c.fieldColumns = G10::W, c.fieldRows = G10::CLEAR_H;
    c.targetChain = parsePositiveInt(t[0], "target_chain");
    c.targetSuccessCount = parsePositiveInt(t[1], "target_success_count");
    c.initialSeed = parseSeed(t[2]);
    c.colorCount = parsePositiveInt(t[3], "color_count");
    c.minExtraPieces = orZero(t[4], "min_extra_pieces");
    c.maxExtraPieces = orZero(t[5], "max_extra_pieces");
    c.beamWidth = parsePositiveInt(t[6], "beam_width");
    c.candidatesPerParent = parsePositiveInt(t[7], "candidates_per_parent");
    c.restarts = parsePositiveInt(t[8], "restarts");
    c.threads = orZero(t[9], "threads");
    if (c.targetChain < 1 || c.targetChain > c.maxChain())
        throw std::runtime_error("連鎖数は1〜" + std::to_string(c.maxChain()) + "です。");
    if (c.colorCount < 3 || c.colorCount > 5) throw std::runtime_error("色数は3〜5です。");
    if (c.minExtraPieces > c.maxExtraPieces || c.maxExtraPieces > c.maxPieces() - 4 * c.targetChain)
        throw std::runtime_error("余剰は 0 ≤ 下限 ≤ 上限 ≤ " + std::to_string(c.maxPieces()) + "−4×連鎖数 です。");
    if (c.threads > 256) throw std::runtime_error("スレッド数は0（自動）〜256です。");
    return c;
}

struct Frame {
    Board board;
    std::vector<Point> marked; // white rings: the pieces that clear first
    std::vector<Point> pair;   // yellow rings: a last pair that fires the chain
    std::string label;
    int chain = 0;
    int score = 0;
};
// Frame 0 is the board right before it fires; then, for every wave, the board
// with the cleared pieces removed (survivors keep their height) and the board
// after gravity.
inline std::vector<Frame> timeline(const Solution& s) {
    const std::string problem = verifySolution(s);
    if (!problem.empty()) throw std::runtime_error("盤面の検証に失敗しました: " + problem);
    std::vector<Frame> frames;
    Frame first{boardFromBits(s.field), pointsOf(s.trigger), {}, "発火直前: " + std::to_string(s.targetChain) + "連鎖"};
    for (int id = 0; id < PAIR_POSITIONS; ++id)
        if (s.firePairs >> id & 1) {
            first.pair = pointsOf(pairCells(s.field, id));
            break;
        }
    frames.push_back(std::move(first));
    BitField f = s.field;
    int score = 0;
    Wave w;
    for (int n = 1; findWave(f, w); ++n) {
        score += waveScore(w, n);
        f.erase(w.cleared | w.garbage);
        frames.push_back({boardFromBits(f), {}, {}, std::to_string(n) + "連鎖目: 消去後・落下前", n, score});
        f.drop();
        frames.push_back({boardFromBits(f), {}, {}, std::to_string(n) + "連鎖目: 落下後", n, score});
    }
    frames.back().label = f.empty() ? "連鎖終了: 全消し" : "連鎖終了: 余剰が残る";
    return frames;
}
// The same for a 10 x 18 result.
inline std::vector<Frame> timeline(const WideSolution& s) {
    const std::string problem = gridVerifySolution(s);
    if (!problem.empty()) throw std::runtime_error("盤面の検証に失敗しました: " + problem);
    std::vector<Frame> frames;
    Frame first{boardFromGrid(s.field), pointsOf(s.trigger), {}, "発火直前: " + std::to_string(s.targetChain) + "連鎖"};
    for (int id = 0; id < gridPairPositions<G10>(); ++id)
        if (s.firePairs >> id & 1) {
            first.pair = pointsOf(gridPairCells(s.field, id));
            break;
        }
    frames.push_back(std::move(first));
    GridField<G10> f = s.field;
    int score = 0;
    GridFieldWave<G10> w;
    for (int n = 1; findWave(f, w); ++n) {
        score += gridWaveScore(w, n);
        f.erase(w.cleared | w.garbage);
        frames.push_back({boardFromGrid(f), {}, {}, std::to_string(n) + "連鎖目: 消去後・落下前", n, score});
        f.drop();
        frames.push_back({boardFromGrid(f), {}, {}, std::to_string(n) + "連鎖目: 落下後", n, score});
    }
    frames.back().label = f.empty() ? "連鎖終了: 全消し" : "連鎖終了: 余剰が残る";
    return frames;
}
// The frame shown when a result is selected ("発火へ").
inline size_t ignitionFrame(const Solution&) { return 0; }

// A saved result: a 6 x 13 board (`solution`) or, when `wide`, a 10 x 18 one (`wideSolution`).
struct Record {
    Solution solution;
    uint64_t seed = 0;
    std::filesystem::path path;
    bool wide = false;
    WideSolution wideSolution;

    int targetChain() const { return wide ? wideSolution.targetChain : solution.targetChain; }
    int pieces() const { return wide ? wideSolution.field.count() : solution.field.count(); }
    Board board() const { return wide ? boardFromGrid(wideSolution.field) : boardFromBits(solution.field); }
    std::vector<Point> trigger() const { return wide ? pointsOf(wideSolution.trigger) : pointsOf(solution.trigger); }
    // Every last pair that fires the chain: (vertical, its two cells).
    std::vector<std::pair<bool, std::vector<Point>>> lastPairs() const {
        std::vector<std::pair<bool, std::vector<Point>>> pairs;
        if (wide) {
            for (int id = 0; id < gridPairPositions<G10>(); ++id)
                if (wideSolution.firePairs >> id & 1) pairs.push_back({id < G10::W, pointsOf(gridPairCells(wideSolution.field, id))});
        } else {
            for (int id = 0; id < PAIR_POSITIONS; ++id)
                if (solution.firePairs >> id & 1) pairs.push_back({id < W, pointsOf(pairCells(solution.field, id))});
        }
        return pairs;
    }
};
inline std::vector<Frame> timeline(const Record& r) { return r.wide ? timeline(r.wideSolution) : timeline(r.solution); }

// Versioned, bounded archive. Version 2: a 6 x 13 board, trigger and last pairs; version 3: the same for a
// 10 x 18 board, with the size on its own line. (Files of v0.4.0 and before - another first word, and in
// v0.3.0 version 1 with a build sequence - are not read any more.)
inline void saveRecord(const std::filesystem::path& path, const Solution& s, uint64_t seed) {
    std::ofstream o(path, std::ios::binary);
    o << "JURAIC_FIELD_CHAIN 2\n" << seed << ' ' << s.targetChain << '\n';
    int h[W];
    s.field.heights(h);
    for (int x = 0; x < W; ++x) {
        o << h[x];
        for (int y = 0; y < h[x]; ++y) o << ' ' << int(s.field.get(x, y));
        o << '\n';
    }
    for (Point p : pointsOf(s.trigger)) o << p.first << ' ' << p.second << '\n';
    o << s.firePairs << '\n';
    o.flush();
    if (!o) throw std::runtime_error("結果を保存できません: " + pathToUtf8(path));
}
inline void saveRecord(const std::filesystem::path& path, const WideSolution& s, uint64_t seed) {
    std::ofstream o(path, std::ios::binary);
    o << "JURAIC_FIELD_CHAIN 3\n" << G10::W << ' ' << G10::CLEAR_H << '\n' << seed << ' ' << s.targetChain << '\n';
    int h[G10::W];
    s.field.heights(h);
    for (int x = 0; x < G10::W; ++x) {
        o << h[x];
        for (int y = 0; y < h[x]; ++y) o << ' ' << int(s.field.get(x, y));
        o << '\n';
    }
    for (Point p : pointsOf(s.trigger)) o << p.first << ' ' << p.second << '\n';
    o << s.firePairs << '\n';
    o.flush();
    if (!o) throw std::runtime_error("結果を保存できません: " + pathToUtf8(path));
}
inline Record loadRecord(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > 65536) throw std::runtime_error("結果ファイルが大きすぎます。");
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    int version;
    Record r;
    r.path = path;
    if (!(in >> magic >> version) || magic != "JURAIC_FIELD_CHAIN" || (version != 2 && version != 3))
        throw std::runtime_error("対応する盤面ファイルの形式ではありません。");
    int columns = W, rows = H, maxChain = MAX_TARGET_CHAIN;
    if (version == 3) {
        int width, clear;
        if (!(in >> width >> clear) || width != G10::W || clear != G10::CLEAR_H)
            throw std::runtime_error("対応していない盤面の大きさです。");
        r.wide = true;
        columns = G10::W, rows = G10::H, maxChain = juraic::maxChain<G10>();
    }
    std::string seed;
    int chain = 0;
    in >> seed >> chain;
    r.seed = parseSeed(seed);
    if (chain < 1 || chain > maxChain) throw std::runtime_error("不正な連鎖数です。");
    r.solution.targetChain = r.wideSolution.targetChain = chain;
    for (int x = 0; x < columns; ++x) {
        int n;
        if (!(in >> n) || n < 0 || n > rows) throw std::runtime_error("不正な盤面です。");
        for (int y = 0; y < n; ++y) {
            int v;
            if (!(in >> v) || v < 1 || v > 5) throw std::runtime_error("不正な色です。");
            if (r.wide) r.wideSolution.field.set(x, y, Cell(v));
            else r.solution.field.set(x, y, Cell(v));
        }
    }
    const auto point = [&](Point& p) {
        if (!(in >> p.first >> p.second) || p.first < 0 || p.first >= columns || p.second < 0 || p.second >= rows)
            throw std::runtime_error("不正な座標です。");
    };
    for (int i = 0; i < 4; ++i) {
        Point p;
        point(p);
        if (r.wide) r.wideSolution.trigger |= GridBits<G10>::cell(p.first, p.second);
        else r.solution.trigger |= FieldBits::cell(p.first, p.second);
    }
    const int positions = r.wide ? gridPairPositions<G10>() : PAIR_POSITIONS;
    uint64_t fire = 0;
    if (!(in >> fire) || !fire || fire >> positions) throw std::runtime_error("不正な最後の1組です。");
    r.solution.firePairs = unsigned(fire);
    r.wideSolution.firePairs = uint32_t(fire);
    std::string tail;
    if (in >> tail) throw std::runtime_error("結果ファイルに余分なデータがあります。");
    const std::string problem = r.wide ? gridVerifySolution(r.wideSolution) : verifySolution(r.solution);
    if (!problem.empty()) throw std::runtime_error("結果ファイルのエンジン検証に失敗しました: " + problem);
    if (r.wide) r.solution = {};
    return r;
}
} // namespace juraic_gui
