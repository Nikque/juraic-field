// A saved picture of the real game's window read with a table (play/realgame_read.h): for making the table from
// pictures of the real game and checking it.
//
//   realgame_read <picture.bmp> [--layout FILE] [--points OUT.bmp]
//   realgame_read --write-layout FILE
//
// Prints what the picture shows on each side: every point of the field (from the top: a space above what the screen
// shows, . nothing, 1 to 5 the colours, x garbage; two lines a row, the upper point first), the pieces at rest, the
// pair in play, the window of the next pairs and the garbage notice. Without --layout the table built in is used,
// which is not taken from the real game.
//   --points OUT.bmp     the picture with every point read marked on it, in the colour read there (black where
//                        nothing is read), to see whether the table fits the picture
//   --write-layout FILE  writes the table built in as text, to begin a table from
// Pictures are BMP files of 24 or 32 bits a pixel (those the game saves in the mode of the real game).
#include "../play/realgame_read.h"
#include <iostream>

using namespace play::realgame;

namespace {

char symbol(juraic::Cell c) {
    return c == HIDDEN ? ' ' : c == juraic::EMPTY ? '.' : c == juraic::GARBAGE ? 'x' : char('0' + c);
}

// A small square around a point, in the colour of the table read there (black: none), with a white rim.
void markPoint(std::vector<uint8_t>& pixels, int width, int height, const Layout& l, float x, float y, juraic::Cell cell) {
    const Scale s({nullptr, width, height, width * 4}, l);
    Rgb inside{};
    for (const ColorRef& c : l.colors)
        if (c.cell == cell && cell != juraic::EMPTY) {
            inside = c.rgb;
            break;
        }
    const int cx = int(x * s.x), cy = int(y * s.y), r = std::max(3, int(l.patch * s.x) + 2);
    for (int j = cy - r; j <= cy + r; ++j)
        for (int i = cx - r; i <= cx + r; ++i) {
            if (i < 0 || j < 0 || i >= width || j >= height) continue;
            const bool rim = i == cx - r || i == cx + r || j == cy - r || j == cy + r;
            uint8_t* p = &pixels[(size_t(j) * size_t(width) + size_t(i)) * 4];
            const Rgb c = rim ? Rgb{255, 255, 255} : inside;
            p[0] = uint8_t(c.b), p[1] = uint8_t(c.g), p[2] = uint8_t(c.r);
        }
}

} // namespace

int main(int argc, char** argv) {
    std::string picturePath, layoutPath, pointsPath;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--layout" && more) layoutPath = argv[++i];
        else if (a == "--points" && more) pointsPath = argv[++i];
        else if (a == "--write-layout" && more) {
            std::ofstream out(argv[++i], std::ios::binary);
            out << "# The table built in: not taken from the real game (play/realgame_read.h formatLayout).\n" << formatLayout(sampleLayout());
            if (!out) return std::cerr << "cannot write " << argv[i] << '\n', 1;
            return 0;
        } else if (!a.empty() && a[0] != '-' && picturePath.empty()) picturePath = a;
        else return std::cerr << "usage: realgame_read <picture.bmp> [--layout FILE] [--points OUT.bmp] | --write-layout FILE\n", 2;
    }
    if (picturePath.empty()) return std::cerr << "usage: realgame_read <picture.bmp> [--layout FILE] [--points OUT.bmp] | --write-layout FILE\n", 2;
    Layout l = sampleLayout();
    if (!layoutPath.empty()) {
        std::string error;
        if (!loadLayout(layoutPath, l, &error)) return std::cerr << layoutPath << ": " << error << '\n', 1;
    }
    std::vector<uint8_t> pixels;
    int width = 0, height = 0;
    if (!loadBmp(picturePath, pixels, width, height)) return std::cerr << picturePath << ": not a BMP of 24 or 32 bits a pixel\n", 1;
    const Picture picture{pixels.data(), width, height, width * 4};
    const Reading r = read(picture, l);
    std::cout << picturePath << ": " << width << " x " << height << " (the table is for " << l.baseWidth << " x " << l.baseHeight << ")\n";
    for (int s = 0; s < 2; ++s) {
        const SideRead& d = r.side[s];
        std::cout << "\n" << (s ? "2P" : "1P") << " side: the points of the field\n";
        for (int k = POINTS - 1; k >= 0; --k) {
            std::string line = "  ";
            for (int x = 0; x < juraic::W; ++x) line += symbol(d.point[x][k]);
            if (k % 2 == 0) line += "   row " + std::to_string(k / 2 + 1);
            std::cout << line << '\n';
        }
        const juraic::GameField resting = restingField(d);
        int h[juraic::W];
        resting.heights(h);
        std::cout << "  pieces at rest, by column:";
        for (int x = 0; x < juraic::W; ++x) std::cout << ' ' << h[x];
        std::cout << '\n';
        const PairSeen p = readPair(d, resting);
        if (p.seen) {
            static const char* turns[] = {"up", "right", "down", "left"};
            std::cout << "  pair above them: axis in column " << p.at.x + 1 << ", row " << p.at.y + 1 << (p.at.upper ? " (half a cell up)" : "") << ", child " << turns[p.at.dir]
                      << ", colours " << symbol(p.colors.axis) << ' ' << (p.childHidden ? '?' : symbol(p.colors.child)) << '\n';
        }
        for (int n = 0; n < 2; ++n)
            std::cout << "  " << (n ? "next-next" : "next") << ": " << (d.nextRead[n] ? std::string{symbol(d.next[n].axis), ' ', symbol(d.next[n].child)} : std::string("not read"))
                      << " (axis, child)\n";
        std::cout << "  garbage notice: " << d.notice << '\n';
    }
    if (!pointsPath.empty()) {
        std::vector<uint8_t> marked = pixels;
        for (int s = 0; s < 2; ++s) {
            const SideLayout& d = l.side[s];
            for (int x = 0; x < juraic::W; ++x)
                for (int k = 0; k < POINTS; ++k)
                    if (r.side[s].point[x][k] != HIDDEN) {
                        const Spot at = pointSpot(d, x, k);
                        markPoint(marked, width, height, l, at.x, at.y, r.side[s].point[x][k]);
                    }
            for (int n = 0; n < 2; ++n)
                for (int k = 0; k < 2; ++k) markPoint(marked, width, height, l, d.next[n][k].x, d.next[n][k].y, k ? r.side[s].next[n].child : r.side[s].next[n].axis);
            for (const Spot& at : d.notice) markPoint(marked, width, height, l, at.x, at.y, juraic::EMPTY);
        }
        if (!saveBmp(pointsPath, {marked.data(), width, height, width * 4})) return std::cerr << "cannot write " << pointsPath << '\n', 1;
    }
    return 0;
}
