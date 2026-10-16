#pragma once
// The mode in which Juraic plays 2P of the real game (play/realgame_win.h): the real game's menus before an offline match
// of two players, read from its screen so that the pad of 2P is picked there without the person. On the screen of the
// players a pad takes the row of 2P by a press of its own; on the screen of the characters 2P picks its character and
// its way of play by presses of its own. Each screen is told by the colours of a few points of its fixed parts (a table
// of points, as fractions of the picture, measured on pictures of the real game saved at 2272 x 1278); the press is A.
// Plain C++, tested on every system (tests/realgame_tests.cpp).
#include "realgame_read.h"
#include <vector>

namespace play::realgame {

struct MenuPoint {
    float x, y; // fractions of the width and height of the picture
    Rgb rgb;
};

struct MenuLayout {
    int distance = 40;               // how far (in colour) a point may be from its colour and still be it
    std::vector<MenuPoint> players;  // the screen of the players (both of its states)
    std::vector<MenuPoint> waiting;  // ... with the row of 2P chosen (its yellow arrows)
    std::vector<MenuPoint> padded;   // ... and the picture of a pad in the row of 2P (taken by a pad; otherwise the keyboard)
    std::vector<MenuPoint> characters; // the screen of the characters
    std::vector<MenuPoint> decided;  // ... with 2P decided (its mark over its picture)
};

// The table measured on the real game (pictures of 2026-10-16, 2272 x 1278).
inline MenuLayout measuredMenus() {
    MenuLayout m;
    m.players = {{0.30f, 0.08f, {255, 86, 128}}, {0.50f, 0.89f, {220, 122, 160}}, {0.414f, 0.243f, {53, 170, 221}}, {0.50f, 0.12f, {192, 138, 155}}};
    m.waiting = {{0.4643f, 0.3873f, {255, 252, 0}}, {0.5885f, 0.3873f, {255, 253, 0}}};
    m.padded = {{0.5238f, 0.3756f, {255, 253, 255}}, {0.5238f, 0.3905f, {255, 253, 255}}, {0.5106f, 0.3905f, {255, 253, 255}}, {0.5304f, 0.3905f, {255, 253, 255}}};
    m.characters = {{0.30f, 0.08f, {255, 97, 35}}, {0.50f, 0.89f, {0, 113, 188}}, {0.10f, 0.08f, {255, 47, 12}}, {0.50f, 0.12f, {228, 187, 186}}};
    m.decided = {{0.60f, 0.665f, {237, 70, 13}}};
    return m;
}

enum class Menu {
    None,          // not a menu read here
    Players,       // the screen of the players, 2P taken (or the cursor elsewhere)
    PlayersWaiting, // ... the row of 2P chosen and not taken by a pad
    Characters,    // the screen of the characters, 2P not decided
    Decided,       // ... 2P decided
};

// The mean colour of 7 x 7 pixels around a point of the picture (given as fractions).
inline Rgb meanAround(const Picture& p, float fx, float fy) {
    const int cx = int(fx * float(p.width)), cy = int(fy * float(p.height));
    int r = 0, g = 0, b = 0, n = 0;
    for (int y = cy - 3; y <= cy + 3; ++y)
        for (int x = cx - 3; x <= cx + 3; ++x) {
            if (x < 0 || y < 0 || x >= p.width || y >= p.height) continue;
            const uint8_t* px = p.pixels + size_t(y) * size_t(p.stride) + size_t(x) * 4; // (BGRA)
            b += px[0], g += px[1], r += px[2], ++n;
        }
    if (!n) return {0, 0, 0};
    return {uint8_t(r / n), uint8_t(g / n), uint8_t(b / n)};
}
inline bool allNear(const Picture& p, const std::vector<MenuPoint>& points, int distance) {
    if (points.empty()) return false;
    for (const MenuPoint& q : points)
        if (distance2(meanAround(p, q.x, q.y), q.rgb) > distance * distance) return false;
    return true;
}
inline Menu readMenu(const Picture& p, const MenuLayout& m) {
    if (allNear(p, m.players, m.distance)) return allNear(p, m.waiting, m.distance) && !allNear(p, m.padded, m.distance) ? Menu::PlayersWaiting : Menu::Players;
    if (allNear(p, m.characters, m.distance)) return allNear(p, m.decided, m.distance) ? Menu::Decided : Menu::Characters;
    return Menu::None;
}

// When the pad of 2P presses A in the menus, picture after picture: on the screen of the players while its row is to be
// taken, on the screen of the characters until 2P is decided (a character, then a way of play); after a press, GAP
// pictures pass before the next (the screen takes the press and moves on).
class MenuPresser {
public:
    static constexpr int GAP = 40;
    int presses = 0;
    bool step(Menu m) {
        if (wait > 0) {
            --wait;
            return false;
        }
        if (m != Menu::PlayersWaiting && m != Menu::Characters) return false;
        wait = GAP, ++presses;
        return true;
    }

private:
    int wait = 0;
};

inline const char* menuName(Menu m) {
    switch (m) {
    case Menu::Players: return "the screen of the players";
    case Menu::PlayersWaiting: return "the screen of the players, 2P to be taken";
    case Menu::Characters: return "the screen of the characters, 2P to decide";
    case Menu::Decided: return "the screen of the characters, 2P decided";
    default: return "no menu";
    }
}

} // namespace play::realgame
