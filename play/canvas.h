#pragma once
// The canvas the game is drawn on: 1280 x 720, scaled to the window, and its types - a rectangle, a colour, a
// point. On Windows they are those of Direct2D, which draws them as they are; elsewhere structures of the same
// fields.
#ifdef _WIN32
#include <d2d1.h>
#endif
#include "../game/match.h"

namespace play {
using namespace juraic;

#ifdef _WIN32
using RectF = D2D1_RECT_F;
using ColorF = D2D1_COLOR_F;
using PointF = D2D1_POINT_2F;
inline RectF rectF(float left, float top, float right, float bottom) { return D2D1::RectF(left, top, right, bottom); }
inline ColorF colorF(float r, float g, float b, float a = 1) { return D2D1::ColorF(r, g, b, a); }
inline PointF pointF(float x, float y) { return D2D1::Point2F(x, y); }
#else
struct RectF {
    float left, top, right, bottom;
};
struct ColorF {
    float r, g, b, a;
};
struct PointF {
    float x, y;
};
inline RectF rectF(float left, float top, float right, float bottom) { return RectF{left, top, right, bottom}; }
inline ColorF colorF(float r, float g, float b, float a = 1) { return ColorF{r, g, b, a}; }
inline PointF pointF(float x, float y) { return PointF{x, y}; }
#endif

constexpr float CANVAS_W = 1280, CANVAS_H = 720;
constexpr float CELL = 46;

inline ColorF rgb(int r, int g, int b, float a = 1) { return colorF(r / 255.f, g / 255.f, b / 255.f, a); }
inline ColorF pieceColor(Cell c, float a = 1) {
    switch (c) {
    case 1: return rgb(232, 64, 70, a);
    case 2: return rgb(72, 190, 84, a);
    case 3: return rgb(64, 120, 238, a);
    case 4: return rgb(244, 200, 48, a);
    case 5: return rgb(164, 84, 214, a);
    default: return rgb(172, 178, 190, a);
    }
}

} // namespace play
