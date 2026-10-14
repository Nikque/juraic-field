#pragma once
// Drawing a player's field with its animations, and the next pairs (play/gfx.h draws the marks).
//
// The game state holds only the result of each phase (see game/player.h); the
// View remembers what the field looked like a frame earlier and draws the
// pops, the falls and the garbage from the phase and its remaining frames.
// Everything is drawn on a 1280 x 720 canvas that is scaled to the window.
#include "gfx.h"
#include "view_state.h"
#include <functional>
#include <string>

namespace play {
using namespace juraic;

// Draws the settled pieces of g, with links between equal neighbours.
// skip: cells not to draw. Rows above the 12th are not shown.
inline void drawCells(Gfx& gfx, const GameField& g, float x0, float y0, FieldBits skip = FieldBits(), float alpha = 1) {
    const auto centre = [&](int x, float y) { return pointF(x0 + (x + 0.5f) * CELL, y0 + (11.5f - y) * CELL); };
    for (int pass = 0; pass < 2; ++pass)
        for (int x = 0; x < W; ++x)
            for (int y = 0; y < CLEAR_H; ++y) {
                const Cell c = g.f.get(x, y);
                if (!c || skip.get(x, y)) continue;
                const PointF p = centre(x, float(y));
                if (pass == 1) {
                    gfx.piece(c, p.x, p.y, alpha);
                    continue;
                }
                if (c == GARBAGE) continue;
                if (x + 1 < W && g.f.get(x + 1, y) == c && !skip.get(x + 1, y))
                    gfx.round(rectF(p.x, p.y - CELL * 0.26f, p.x + CELL, p.y + CELL * 0.26f), 8, pieceColor(c, alpha));
                if (y + 1 < CLEAR_H && g.f.get(x, y + 1) == c && !skip.get(x, y + 1))
                    gfx.round(rectF(p.x - CELL * 0.26f, p.y - CELL, p.x + CELL * 0.26f, p.y), 8, pieceColor(c, alpha));
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
                      const std::function<bool(const RectF&)>& picture = {}) {
    const RectF area = rectF(x0, y0, x0 + W * CELL, y0 + CLEAR_H * CELL);
    gfx.round(rectF(area.left - 8, area.top - 8, area.right + 8, area.bottom + 8), 10, rgb(58, 66, 96));
    if (!picture || !picture(area)) {
        gfx.fill(area, rgb(22, 26, 44));
        for (int x = 0; x < W; ++x)
            for (int y = 0; y < CLEAR_H; ++y)
                if ((x + y) % 2) gfx.fill(rectF(x0 + x * CELL, y0 + y * CELL, x0 + (x + 1) * CELL, y0 + (y + 1) * CELL), rgb(26, 31, 52));
    } else {
        for (int x = 0; x < W; ++x)
            for (int y = 0; y < CLEAR_H; ++y)
                gfx.fill(rectF(x0 + x * CELL, y0 + y * CELL, x0 + (x + 1) * CELL, y0 + (y + 1) * CELL), (x + y) % 2 ? rgb(255, 255, 255, 0.02f) : rgb(0, 0, 0, 0.045f)); // (faint: the picture of the second field must show through it)
    }
    // the cell that must stay free
    gfx.text(L"×", x0 + DEATH_X * CELL, y0 + 2, CELL, CELL, 2, rgb(200, 60, 70, 0.75f), 0);
    gfx.clip(area);
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
        gfx.ring(ax, ay, CELL * 0.45f, CELL * 0.42f, rgb(255, 255, 255, 0.55f), 2);
    } else if (coming && p.phase == Phase::Next && p.wait <= 1) {
        // The pair is seen where it appears one frame before it can be moved (Timing::nextShown, [M]).
        const float ax = cx(float(SPAWN_X)), ay = cy(float(SPAWN_Y) + 0.5f);
        gfx.piece(coming->child, ax, ay - CELL);
        gfx.piece(coming->axis, ax, ay);
        gfx.ring(ax, ay, CELL * 0.45f, CELL * 0.42f, rgb(255, 255, 255, 0.55f), 2);
    }
    gfx.unclip();
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
    gfx.round(rectF(x, y, x + 78, y + 250), 10, rgb(40, 46, 70));
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
    gfx.clip(rectF(x, y + 30, x + 78, y + 244));
    if (moved < 0.5f) draw(0, first - 2 * step * moved, CELL); // (it is gone twice as fast as the others move)
    draw(1, first + step * (1 - moved), lesser + (CELL - lesser) * moved);
    if (moved > 0) draw(2, first + step * (2 - moved), lesser);
    gfx.unclip();
}

} // namespace play
