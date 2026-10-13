#pragma once
// Drawing with Direct2D: pieces, a player's field with its animations, text.
//
// The game state holds only the result of each phase (see game/player.h); the
// View remembers what the field looked like a frame earlier and draws the
// pops, the falls and the garbage from the phase and its remaining frames.
// Everything is drawn on a 1280 x 720 canvas that is scaled to the window.
#include <d2d1.h>
#include <dwrite.h>
#include "../game/match.h"
#include <functional>
#include <string>

namespace play {
using namespace juraic;

constexpr float CANVAS_W = 1280, CANVAS_H = 720;
constexpr float CELL = 46;

inline D2D1_COLOR_F rgb(int r, int g, int b, float a = 1) { return D2D1::ColorF(r / 255.f, g / 255.f, b / 255.f, a); }
inline D2D1_COLOR_F pieceColor(Cell c, float a = 1) {
    switch (c) {
    case 1: return rgb(232, 64, 70, a);
    case 2: return rgb(72, 190, 84, a);
    case 3: return rgb(64, 120, 238, a);
    case 4: return rgb(244, 200, 48, a);
    case 5: return rgb(164, 84, 214, a);
    default: return rgb(172, 178, 190, a);
    }
}

struct Gfx {
    ID2D1RenderTarget* rt = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    IDWriteTextFormat* font[4]{}; // 0 small, 1 normal, 2 large, 3 huge
    bool shadow = false;          // text is drawn with a dark edge (a picture is behind it)

    void fill(D2D1_RECT_F r, D2D1_COLOR_F c) {
        brush->SetColor(c);
        rt->FillRectangle(r, brush);
    }
    void round(D2D1_RECT_F r, float radius, D2D1_COLOR_F c) {
        brush->SetColor(c);
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush);
    }
    void frame(D2D1_RECT_F r, float radius, D2D1_COLOR_F c, float width) {
        brush->SetColor(c);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush, width);
    }
    void circle(float x, float y, float rx, float ry, D2D1_COLOR_F c) {
        brush->SetColor(c);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), rx, ry), brush);
    }
    // align: -1 left, 0 centre, 1 right
    void text(const std::wstring& s, float x, float y, float w, float h, int size, D2D1_COLOR_F c, int align = -1) {
        IDWriteTextFormat* f = font[size];
        f->SetTextAlignment(align < 0 ? DWRITE_TEXT_ALIGNMENT_LEADING : align == 0 ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_TRAILING);
        if (shadow) {
            // (a picture is behind the game: every text gets a dark edge, so that it can be read on any picture)
            brush->SetColor(D2D1::ColorF(0, 0, 0, 0.7f * c.a));
            for (int k = 0; k < 4; ++k) {
                const float dx = k & 1 ? 1.5f : -1.5f, dy = k & 2 ? 1.5f : -1.5f;
                rt->DrawText(s.c_str(), UINT32(s.size()), f, D2D1::RectF(x + dx, y + dy, x + w + dx, y + h + dy), brush);
            }
        }
        brush->SetColor(c);
        rt->DrawText(s.c_str(), UINT32(s.size()), f, D2D1::RectF(x, y, x + w, y + h), brush);
    }

    // One piece centred at (cx, cy). scale shrinks it. sx and sy change its
    // shape (the bounce after landing); its bottom stays where it is.
    void piece(Cell c, float cx, float cy, float alpha = 1, float scale = 1, float size = CELL, float sx = 1, float sy = 1) {
        const float r = size * 0.47f * scale, rx = r * sx, ry = r * sy;
        cy += r * 0.94f * (1 - sy);
        const D2D1_COLOR_F base = pieceColor(c, alpha);
        circle(cx, cy + ry * 0.04f, rx, ry * 0.94f, rgb(20, 24, 40, alpha * 0.55f));
        circle(cx, cy, rx * 0.93f, ry * 0.88f, base);
        circle(cx - rx * 0.28f, cy - ry * 0.42f, rx * 0.34f, ry * 0.2f, rgb(255, 255, 255, alpha * 0.35f));
        if (c == GARBAGE) {
            circle(cx, cy + ry * 0.1f, rx * 0.42f, ry * 0.36f, rgb(120, 126, 140, alpha));
            return;
        }
        for (int side = -1; side <= 1; side += 2) {
            circle(cx + side * rx * 0.32f, cy + ry * 0.02f, rx * 0.2f, ry * 0.24f, rgb(255, 255, 255, alpha));
            circle(cx + side * rx * 0.32f, cy + ry * 0.07f, rx * 0.1f, ry * 0.13f, rgb(24, 24, 36, alpha));
        }
    }
    // A piece in one of the shapes of the bounce (Timing::bounceLock, bounceFall).
    void bouncing(Cell c, float cx, float cy, int shape) {
        static const float sx[4] = {1, 1.12f, 0.82f, 1}, sy[4] = {1, 0.85f, 0.98f, 0.95f};
        piece(c, cx, cy, 1, 1, CELL, sx[shape], sy[shape]);
    }
};

// What the renderer keeps for one player between frames.
struct View {
    GameField last;        // the field at the end of the previous frame
    GameField from;        // the field the running animation starts from
    FieldBits popping;     // cells that disappear in the running wave
    int total = 0;         // length of the running phase
    float px = 2, py = 11.5f, angle = 0; // where the pair is drawn (smoothed)
    int chainShown = 0, chainTimer = 0;  // "n chain" banner
    // The pair that locked last, for its bounce: frames since the lock, the
    // cells of the two pieces (axis, child), and for the piece of a split the
    // frame (counted from the lock) in which it comes to rest; -1 for the other.
    int sinceLock = 1 << 20;
    // The bounce begins in the frame the pair lands - in the real game a pair that comes down by itself bounces
    // while it can still be moved (counted on video) -, not in the frame it locks: the frames since
    // the last landing of the pair, and how many of them had passed when it locked.
    int sinceLanding = 1 << 20, landedFor = 0;
    int lockX[2]{}, lockY[2]{}, restAt[2]{-1, -1};
    Cell lockColor[2]{};

    void reset(const Player& p) {
        *this = View{};
        last = from = p.field;
    }
    // Call once per game frame, after the frame has been played.
    void update(const Player& p, const PlayerEvents& e) {
        if (e.spawned) px = float(p.pair.x), py = float(p.pair.y) + 0.5f, angle = 0;
        if (e.locked || e.waveStarted || e.garbageFell) from = last, total = p.wait;
        ++sinceLock;
        if (sinceLanding < (1 << 20)) ++sinceLanding;
        if (e.spawned) sinceLanding = 1 << 20;
        if (e.control.landed) sinceLanding = 0;
        if (e.locked) {
            const Timing& t = DEFAULT_TIMING;
            sinceLock = 0;
            // (a lock in the frame of the landing, as with the down key: 0, the bounce begins here as before)
            landedFor = sinceLanding >= (1 << 20) ? 0 : sinceLanding < Timing::BOUNCE_LOCK ? sinceLanding : Timing::BOUNCE_LOCK;
            lockX[0] = p.pair.x, lockY[0] = p.placed.axisY, lockColor[0] = p.colors.axis;
            lockX[1] = p.pair.childX(), lockY[1] = p.placed.childY, lockColor[1] = p.colors.child;
            restAt[0] = restAt[1] = -1;
            if (p.phase == Phase::Split) // a split near the top starts later (see Timing::splitFrames)
                restAt[p.placed.axisY < p.placed.lockY ? 0 : 1] = p.wait - t.splitFrames(p.placed.split, 0, 0) + t.splitFall(p.placed.split);
        }
        if (e.waveStarted) {
            popping = p.wave.wave.cleared | p.wave.wave.garbage;
            chainShown = p.chain.chains, chainTimer = 70;
        }
        if (p.phase == Phase::Land && p.wait == DEFAULT_TIMING.landFrames) total = p.wait;
        if (chainTimer) --chainTimer;
        if (p.phase == Phase::Control) {
            const float tx = float(p.pair.x), ty = float(p.pair.y) + (p.pair.upper ? 0.5f : 0.f);
            px += (tx - px) * 0.55f;
            py += (ty - py) * 0.6f;
            float target = float(p.pair.dir) * 90.f, d = target - angle;
            while (d > 180) d -= 360;
            while (d < -180) d += 360;
            angle += d * 0.5f;
        }
        last = p.field;
    }
};

// Draws the settled pieces of g, with links between equal neighbours.
// skip: cells not to draw. Rows above the 12th are not shown.
inline void drawCells(Gfx& gfx, const GameField& g, float x0, float y0, FieldBits skip = FieldBits(), float alpha = 1) {
    const auto centre = [&](int x, float y) { return D2D1::Point2F(x0 + (x + 0.5f) * CELL, y0 + (11.5f - y) * CELL); };
    for (int pass = 0; pass < 2; ++pass)
        for (int x = 0; x < W; ++x)
            for (int y = 0; y < CLEAR_H; ++y) {
                const Cell c = g.f.get(x, y);
                if (!c || skip.get(x, y)) continue;
                const D2D1_POINT_2F p = centre(x, float(y));
                if (pass == 1) {
                    gfx.piece(c, p.x, p.y, alpha);
                    continue;
                }
                if (c == GARBAGE) continue;
                if (x + 1 < W && g.f.get(x + 1, y) == c && !skip.get(x + 1, y))
                    gfx.round(D2D1::RectF(p.x, p.y - CELL * 0.26f, p.x + CELL, p.y + CELL * 0.26f), 8, pieceColor(c, alpha));
                if (y + 1 < CLEAR_H && g.f.get(x, y + 1) == c && !skip.get(x, y + 1))
                    gfx.round(D2D1::RectF(p.x - CELL * 0.26f, p.y - CELL, p.x + CELL * 0.26f, p.y), 8, pieceColor(c, alpha));
            }
}

// The field of one player with whatever is moving in it. (x0, y0) is the
// top-left corner of the twelve visible rows. coming: the pair that appears
// next, when it is to be drawn in the frame before it does (null = not).
// picture: draws the picture the settings give this field over the area it is handed and says whether it
// did; without one (or when it says no) the field has its plain ground of two shades. Over a picture the
// two shades stay as a faint pattern of lighter and darker cells: the cells can be told, and with them
// where a pair is.
inline void drawField(Gfx& gfx, const Player& p, const View& v, float x0, float y0, bool ghost, const Pair* coming = nullptr,
                      const std::function<bool(const D2D1_RECT_F&)>& picture = {}) {
    const D2D1_RECT_F area = D2D1::RectF(x0, y0, x0 + W * CELL, y0 + CLEAR_H * CELL);
    gfx.round(D2D1::RectF(area.left - 8, area.top - 8, area.right + 8, area.bottom + 8), 10, rgb(58, 66, 96));
    if (!picture || !picture(area)) {
        gfx.fill(area, rgb(22, 26, 44));
        for (int x = 0; x < W; ++x)
            for (int y = 0; y < CLEAR_H; ++y)
                if ((x + y) % 2) gfx.fill(D2D1::RectF(x0 + x * CELL, y0 + y * CELL, x0 + (x + 1) * CELL, y0 + (y + 1) * CELL), rgb(26, 31, 52));
    } else {
        for (int x = 0; x < W; ++x)
            for (int y = 0; y < CLEAR_H; ++y)
                gfx.fill(D2D1::RectF(x0 + x * CELL, y0 + y * CELL, x0 + (x + 1) * CELL, y0 + (y + 1) * CELL), (x + y) % 2 ? rgb(255, 255, 255, 0.02f) : rgb(0, 0, 0, 0.045f)); // (faint: the picture of the second field must show through it)
    }
    // the cell that must stay free
    gfx.text(L"×", x0 + DEATH_X * CELL, y0 + 2, CELL, CELL, 2, rgb(200, 60, 70, 0.75f), 0);
    gfx.rt->PushAxisAlignedClip(area, D2D1_ANTIALIAS_MODE_ALIASED);
    const auto cx = [&](float x) { return x0 + (x + 0.5f) * CELL; };
    const auto cy = [&](float y) { return y0 + (11.5f - y) * CELL; };
    const int elapsed = v.total - p.wait;

    // The pieces of the pair that has just locked bounce; they are drawn alone, after the rest.
    FieldBits landed;
    int shape[2] = {-1, -1};
    if (p.phase == Phase::Split || p.phase == Phase::Land)
        for (int i = 0; i < 2; ++i) {
            const Timing& t = DEFAULT_TIMING;
            const int frame = v.restAt[i] < 0 ? v.sinceLock + v.landedFor : v.sinceLock - v.restAt[i] - 1; // (the bounce of the landing goes on)
            if (frame < 0 || frame >= (v.restAt[i] < 0 ? Timing::BOUNCE_LOCK : Timing::BOUNCE_FALL) || v.lockY[i] >= CLEAR_H) continue;
            shape[i] = v.restAt[i] < 0 ? t.bounceLock[frame] : t.bounceFall[frame];
            landed |= FieldBits::cell(v.lockX[i], v.lockY[i]);
        }

    if (p.phase == Phase::Wave) {
        // The steps of a wave as measured on the real game (see Timing).
        const Timing& t = DEFAULT_TIMING;
        const int falling = elapsed - t.waveQuick; // frames since the pieces above started to fall
        if (elapsed < t.popBlink) {
            drawCells(gfx, v.from, x0, y0);
        } else if (elapsed < t.popFace) {
            // blinking: hidden for the first half of each period
            const bool hidden = (elapsed - t.popBlink) % t.popBlinkPeriod < t.popBlinkPeriod / 2;
            drawCells(gfx, v.from, x0, y0, hidden ? v.popping : FieldBits());
        } else if (elapsed < t.popGone) {
            // the popping faces: the pieces turn pale
            drawCells(gfx, v.from, x0, y0, v.popping);
            forEachCell(v.popping, [&](int x, int y) {
                gfx.piece(v.from.f.get(x, y), cx(float(x)), cy(float(y)));
                gfx.circle(cx(float(x)), cy(float(y)), CELL * 0.44f, CELL * 0.41f, rgb(255, 255, 255, 0.6f));
            });
        } else if (falling < 0) {
            // gone; what is above still hangs in the air
            drawCells(gfx, v.from, x0, y0, v.popping);
        } else if (falling * t.waveFallPerFrame >= p.wave.longestFall * 256 + Timing::BOUNCE_FALL * t.waveFallPerFrame) {
            drawCells(gfx, p.field, x0, y0); // everything has landed and come to rest
        } else {
            // The pieces that stay where they are keep their links; a falling
            // piece is drawn alone until it has come to rest.
            FieldBits moving = v.popping;
            for (int x = 0; x < W; ++x) {
                bool gap = false;
                for (int y = 0; y < H; ++y) {
                    if (v.popping.get(x, y)) gap = true;
                    else if (gap && v.from.f.get(x, y)) moving |= FieldBits::cell(x, y);
                }
            }
            drawCells(gfx, v.from, x0, y0, moving);
            const float fallen = float((falling + 1) * t.waveFallPerFrame) / 256.f;
            for (int x = 0; x < W; ++x) {
                int target = 0;
                for (int y = 0; y < H; ++y) {
                    const Cell c = v.from.f.get(x, y);
                    if (!c || v.popping.get(x, y)) continue;
                    if (moving.get(x, y)) {
                        // frames since this piece came down
                        const int rest = falling - (y - target) * 256 / t.waveFallPerFrame;
                        if (rest < 0) gfx.piece(c, cx(float(x)), cy(std::max(float(target), float(y) - fallen)));
                        else gfx.bouncing(c, cx(float(x)), cy(float(target)), rest < Timing::BOUNCE_FALL ? t.bounceFall[rest] : 0);
                    }
                    ++target;
                }
            }
        }
    } else if (p.phase == Phase::Split && v.total > 0) {
        // One piece of the pair is still falling to its place.
        const bool axisFalls = p.placed.axisY < p.placed.lockY;
        const int fx = axisFalls ? p.pair.x : p.pair.childX(), fy = axisFalls ? p.placed.axisY : p.placed.childY;
        drawCells(gfx, p.field, x0, y0, FieldBits::cell(fx, fy < H ? fy : H - 1) | landed);
        // It has fallen k cells when splitFall(k) frames have passed (see Timing);
        // in between it moves evenly. A split near the top starts later.
        const Timing& t = DEFAULT_TIMING;
        const int distance = p.placed.split;
        const int time = elapsed - (v.total - t.splitFrames(distance, 0, 0));
        float fallen = 0;
        for (int k = 0; k < distance && time > t.splitFall(k); ++k)
            fallen = float(k) + std::min(1.f, float(time - t.splitFall(k)) / float(t.splitFall(k + 1) - t.splitFall(k)));
        if (time <= t.splitFall(distance))
            gfx.piece(axisFalls ? p.colors.axis : p.colors.child, cx(float(fx)), cy(std::max(float(fy), float(p.placed.lockY) - fallen)));
        else if (!landed.get(fx, fy < H ? fy : H - 1))
            gfx.piece(axisFalls ? p.colors.axis : p.colors.child, cx(float(fx)), cy(float(fy)));
    } else if (p.phase == Phase::Garbage && v.total > 0) {
        // The new garbage comes down from above the field.
        FieldBits fresh;
        int h[W];
        p.field.heights(h);
        for (int x = 0; x < W; ++x)
            for (int i = 0; i < p.dropped[x]; ++i) fresh |= FieldBits::cell(x, h[x] - 1 - i);
        drawCells(gfx, p.field, x0, y0, fresh);
        // The garbage of a column comes down as one block. Its lowest piece is
        // in row r in the frame Timing::garbageCell[r][x], faster and faster,
        // until it reaches the top of the column. (Checked on the video: two
        // drops of 30, on an empty field and on a field five high, passed the
        // rows and landed in the frames of that table, give or take one frame;
        // no bounce was seen.)
        const Timing& t = DEFAULT_TIMING;
        forEachCell(fresh, [&](int x, int y) {
            const int floor = std::min(h[x] - p.dropped[x], 13);
            float lowest = float(floor);
            if (elapsed < t.garbageCell[13][x]) {
                lowest = 14;
            } else {
                for (int r = floor; r < 13 && elapsed < t.garbageCell[r][x]; ++r)
                    lowest = float(r) + float(t.garbageCell[r][x] - elapsed) / float(t.garbageCell[r][x] - t.garbageCell[r + 1][x]);
            }
            gfx.piece(GARBAGE, cx(float(x)), cy(float(y) + lowest - float(floor)));
        });
    } else {
        drawCells(gfx, p.field, x0, y0, landed);
    }
    for (int i = 0; i < 2; ++i)
        if (shape[i] >= 0) gfx.bouncing(v.lockColor[i], cx(float(v.lockX[i])), cy(float(v.lockY[i])), shape[i]);

    if (p.phase == Phase::Control) {
        if (ghost) {
            // Small dots where the two pieces would come to rest.
            GameField g = p.field;
            const PlaceResult r = place(g, p.pair.placement(), p.colors);
            gfx.circle(cx(float(p.pair.x)), cy(float(r.axisY)), CELL * 0.16f, CELL * 0.16f, pieceColor(p.colors.axis, 0.8f));
            gfx.circle(cx(float(p.pair.childX())), cy(float(r.childY)), CELL * 0.16f, CELL * 0.16f, pieceColor(p.colors.child, 0.8f));
        }
        const float rad = v.angle * 3.14159265f / 180.f;
        const float ax = cx(v.px), ay = cy(v.py);
        // (a pair lying on the ground bounces from the frame it landed in, while it can still be moved)
        const int bounce = p.pair.grounded && v.sinceLanding < Timing::BOUNCE_LOCK ? DEFAULT_TIMING.bounceLock[v.sinceLanding] : -1;
        if (bounce >= 0) {
            gfx.bouncing(p.colors.child, ax + std::sin(rad) * CELL, ay - std::cos(rad) * CELL, bounce);
            gfx.bouncing(p.colors.axis, ax, ay, bounce);
        } else {
            gfx.piece(p.colors.child, ax + std::sin(rad) * CELL, ay - std::cos(rad) * CELL);
            gfx.piece(p.colors.axis, ax, ay);
        }
        gfx.brush->SetColor(rgb(255, 255, 255, 0.55f));
        gfx.rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(ax, ay), CELL * 0.45f, CELL * 0.42f), gfx.brush, 2);
    } else if (coming && p.phase == Phase::Next && p.wait <= 1) {
        // The pair is seen where it appears one frame before it can be moved (Timing::nextShown, [M]).
        const float ax = cx(float(SPAWN_X)), ay = cy(float(SPAWN_Y) + 0.5f);
        gfx.piece(coming->child, ax, ay - CELL);
        gfx.piece(coming->axis, ax, ay);
        gfx.brush->SetColor(rgb(255, 255, 255, 0.55f));
        gfx.rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(ax, ay), CELL * 0.45f, CELL * 0.42f), gfx.brush, 2);
    }
    gfx.rt->PopAxisAlignedClip();
    if (v.chainTimer > 0 && v.chainShown > 0) {
        const float a = std::min(1.f, float(v.chainTimer) / 20.f);
        gfx.text(std::to_wstring(v.chainShown) + L" 連鎖", x0, y0 + 4 * CELL - float(70 - v.chainTimer) * 0.4f, W * CELL, 70, 3, rgb(255, 236, 120, a), 0);
    }
    if (p.phase == Phase::Dead) {
        gfx.fill(area, rgb(10, 12, 24, 0.6f));
        gfx.text(L"ゲームオーバー", x0, y0 + 4.5f * CELL, W * CELL, 60, 2, rgb(255, 255, 255), 0);
    }
}

// Next and next-next pairs in a small box. Shortly before a pair appears the window moves on, as
// measured on the real game (Timing::nextSlide, nextShown): the next pair leaves it upwards, the one
// after it moves up into its place, and the pair after that comes in from below. The window ends
// where the lower pair stands, so that the upper piece of the new pair is in sight five frames before
// the pair on the field can be moved and its lower piece four frames before: from then on the AI
// knows the pair (ai/observe.h).
inline void drawNext(Gfx& gfx, const Round& r, const Player& p, float x, float y) {
    gfx.round(D2D1::RectF(x, y, x + 78, y + 250), 10, rgb(40, 46, 70));
    gfx.text(L"NEXT", x, y + 6, 78, 24, 0, rgb(200, 206, 226), 0);
    const Timing& t = r.timing;
    // 0 = the window stands; 1 = it stands again, every pair one place further on.
    float moved = 0;
    if (t.nextSlide > 1 && p.phase == Phase::Next && p.wait <= t.nextSlide)
        moved = std::min(1.f, float(t.nextSlide + 1 - p.wait) / float(t.nextSlide - 1));
    const float lesser = CELL * 0.8f, cx = x + 39, first = y + 68, step = 112; // (the lower pair is drawn smaller)
    const auto draw = [&](int index, float top, float size) {
        const Pair pair = r.tsumo.pair(p.nextPair + index);
        gfx.piece(pair.child, cx, top, 1, 1, size);
        gfx.piece(pair.axis, cx, top + size, 1, 1, size);
    };
    gfx.rt->PushAxisAlignedClip(D2D1::RectF(x, y + 30, x + 78, y + 244), D2D1_ANTIALIAS_MODE_ALIASED);
    if (moved < 0.5f) draw(0, first - 2 * step * moved, CELL); // (it is gone twice as fast as the others move)
    draw(1, first + step * (1 - moved), lesser + (CELL - lesser) * moved);
    if (moved > 0) draw(2, first + step * (2 - moved), lesser);
    gfx.rt->PopAxisAlignedClip();
}

} // namespace play
