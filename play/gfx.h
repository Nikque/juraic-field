#pragma once
// The drawing of the game: the marks of the system (Direct2D on Windows, play/gfx_d2d.h; SDL elsewhere,
// play/gfx_sdl.h) and the pieces built of them.
#ifdef _WIN32
#include "gfx_d2d.h"
#else
#include "gfx_sdl.h"
#endif

namespace play {

struct Gfx : GfxBase {
    // One piece centred at (cx, cy). scale shrinks it. sx and sy change its
    // shape (the bounce after landing); its bottom stays where it is.
    void piece(Cell c, float cx, float cy, float alpha = 1, float scale = 1, float size = CELL, float sx = 1, float sy = 1) {
        const float r = size * 0.47f * scale, rx = r * sx, ry = r * sy;
        cy += r * 0.94f * (1 - sy);
        const ColorF base = pieceColor(c, alpha);
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

} // namespace play
