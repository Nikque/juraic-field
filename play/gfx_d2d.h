#pragma once
// The drawing of the canvas with Direct2D and DirectWrite (Windows): the few kinds of marks the game is made of
// (play/gfx.h builds the pieces of them).
#include "canvas.h"
#include <dwrite.h>
#include <string>

namespace play {

struct GfxBase {
    ID2D1RenderTarget* rt = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    IDWriteTextFormat* font[4]{}; // 0 small, 1 normal, 2 large, 3 huge
    bool shadow = false;          // text is drawn with a dark edge (a picture is behind it)

    void fill(RectF r, ColorF c) {
        brush->SetColor(c);
        rt->FillRectangle(r, brush);
    }
    void round(RectF r, float radius, ColorF c) {
        brush->SetColor(c);
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush);
    }
    void frame(RectF r, float radius, ColorF c, float width) {
        brush->SetColor(c);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush, width);
    }
    void circle(float x, float y, float rx, float ry, ColorF c) {
        brush->SetColor(c);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), rx, ry), brush);
    }
    // The outline of an ellipse.
    void ring(float x, float y, float rx, float ry, ColorF c, float width) {
        brush->SetColor(c);
        rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), rx, ry), brush, width);
    }
    // Nothing is drawn outside r until unclip.
    void clip(RectF r) { rt->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED); }
    void unclip() { rt->PopAxisAlignedClip(); }
    // align: -1 left, 0 centre, 1 right
    void text(const std::wstring& s, float x, float y, float w, float h, int size, ColorF c, int align = -1) {
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
};

} // namespace play
