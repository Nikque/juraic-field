#pragma once
// A small drawing engine of its own for the systems other than Windows (play/gfx_soft.h): rectangles, rounded
// rectangles, ellipses and their outlines, smoothed at their edges, and pictures, painted into a buffer of pixels
// that the window shows (play/platform_sdl.h) or that is saved as a PNG file (the self test). The edges are
// smoothed by the distance of the centre of a pixel to the outline of the shape, as Direct2D draws them closely
// enough; the pixels are kept with their colours multiplied by their opacity.
#include "canvas.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace play {

struct Raster {
    int width = 0, height = 0;
    std::vector<uint32_t> px; // 0xAARRGGBB, the colours multiplied by the opacity
    float scale = 1, dx = 0, dy = 0; // canvas to pixels: x * scale + dx
    struct Box {
        int x0, y0, x1, y1;
    };
    std::vector<Box> clips;

    void resize(int w, int h) {
        width = std::max(1, w), height = std::max(1, h);
        px.assign(size_t(width) * size_t(height), 0xFF000000u);
        clips.clear();
    }
    void clear(ColorF c) {
        const uint32_t v = pack(c, 1);
        std::fill(px.begin(), px.end(), v);
    }
    // The canvas of CANVAS_W x CANVAS_H as large as fits, in the middle (as the window shows it on Windows).
    void fit() {
        scale = std::min(float(width) / CANVAS_W, float(height) / CANVAS_H);
        dx = (float(width) - CANVAS_W * scale) / 2, dy = (float(height) - CANVAS_H * scale) / 2;
    }
    // Pixels whose centre is inside r (of the canvas) are drawn, until unclip.
    void clip(RectF r) {
        Box b{int(std::ceil(r.left * scale + dx - 0.5f)), int(std::ceil(r.top * scale + dy - 0.5f)), int(std::ceil(r.right * scale + dx - 0.5f)),
              int(std::ceil(r.bottom * scale + dy - 0.5f))};
        const Box o = area();
        b.x0 = std::max(b.x0, o.x0), b.y0 = std::max(b.y0, o.y0), b.x1 = std::min(b.x1, o.x1), b.y1 = std::min(b.y1, o.y1);
        clips.push_back(b);
    }
    void unclip() {
        if (!clips.empty()) clips.pop_back();
    }

    void fillRect(RectF r, ColorF c) {
        const float l = r.left * scale + dx, t = r.top * scale + dy, rr = r.right * scale + dx, b = r.bottom * scale + dy;
        cover(l, t, rr, b, c, rectAt(l, t, rr, b));
    }
    void fillRound(RectF r, float radius, ColorF c) {
        roundShape(r, radius, c, -1);
    }
    void strokeRound(RectF r, float radius, ColorF c, float width) {
        roundShape(r, radius, c, width);
    }
    void fillEllipse(float x, float y, float rx, float ry, ColorF c) { ellipseShape(x, y, rx, ry, c, -1); }
    void strokeEllipse(float x, float y, float rx, float ry, ColorF c, float width) { ellipseShape(x, y, rx, ry, c, width); }

    // A picture of w x h pixels (0xAARRGGBB, multiplied) drawn over `to` from its part `from`, smoothly scaled.
    void image(const uint32_t* pic, int w, int h, RectF from, RectF to) {
        imagePixels(pic, w, h, from, to, area(), [&](int x, int y, uint32_t p) { over(px[size_t(y) * size_t(width) + size_t(x)], p); });
    }

    // The pixels of a picture drawn by `image` with `fillRect` of `to` in `shade` over it (the picture darkened), kept
    // as they come out of the two, so that while the picture, its place in pixels and the shade stay the same it is
    // only copied: the scaling of a large picture is the most of a frame drawn here. The owner of the picture empties
    // it (forget) when the picture is another.
    struct Stamp {
        const uint32_t* pic = nullptr;
        int w = 0, h = 0, width = 0, height = 0;
        float place[8]{}; // `from`, and `to` in pixels
        float shade[4]{};
        Box box{};
        // first: what is put over a pixel; then: put over it after that, where the two cannot be one (a pixel of the
        // picture that is not opaque, with the shade over it); empty when there is no such pixel
        std::vector<uint32_t> first, then;

        void forget() { *this = Stamp(); }
    };
    // The same pixels as image(pic, w, h, from, to) and then fillRect(to, shade).
    void picture(const uint32_t* pic, int w, int h, RectF from, RectF to, ColorF shade, Stamp& s) {
        const float l = to.left * scale + dx, t = to.top * scale + dy, r = to.right * scale + dx, b = to.bottom * scale + dy;
        const float place[8] = {from.left, from.top, from.right, from.bottom, l, t, r, b}, tone[4] = {shade.r, shade.g, shade.b, shade.a};
        if (s.pic != pic || s.w != w || s.h != h || s.width != width || s.height != height || !std::equal(place, place + 8, s.place) ||
            !std::equal(tone, tone + 4, s.shade)) {
            s.forget();
            s.pic = pic, s.w = w, s.h = h, s.width = width, s.height = height;
            std::copy(place, place + 8, s.place), std::copy(tone, tone + 4, s.shade);
            // (the box of the shade, which holds that of the picture, inside the pixels)
            s.box = {std::max(0, int(std::floor(l - 1))), std::max(0, int(std::floor(t - 1))), std::min(width, int(std::ceil(r + 1))),
                     std::min(height, int(std::ceil(b + 1)))};
            s.box.x1 = std::max(s.box.x1, s.box.x0), s.box.y1 = std::max(s.box.y1, s.box.y0);
            const size_t bw = size_t(s.box.x1 - s.box.x0), n = bw * size_t(s.box.y1 - s.box.y0);
            const auto at = [&](int x, int y) { return size_t(y - s.box.y0) * bw + size_t(x - s.box.x0); };
            std::vector<uint32_t> under(n, 0), shaded(n, 0);
            imagePixels(pic, w, h, from, to, s.box, [&](int x, int y, uint32_t p) { under[at(x, y)] = p; });
            coverPixels(l, t, r, b, s.box, shade, rectAt(l, t, r, b), [&](int x, int y, uint32_t q) { shaded[at(x, y)] = q; });
            s.first.resize(n);
            for (size_t i = 0; i < n; ++i) {
                const uint32_t p = under[i], q = shaded[i];
                if (!p || !q) {
                    s.first[i] = p | q; // (one of them is nothing: over() leaves the pixel as it is for 0)
                } else if (p >> 24 == 255) {
                    uint32_t d = p; // (an opaque pixel of the picture takes the place of what was there)
                    over(d, q);
                    s.first[i] = d;
                } else {
                    if (s.then.empty()) s.then.assign(n, 0);
                    s.first[i] = p, s.then[i] = q;
                }
            }
        }
        const Box o = area();
        const int x0 = std::max(o.x0, s.box.x0), x1 = std::min(o.x1, s.box.x1), y0 = std::max(o.y0, s.box.y0), y1 = std::min(o.y1, s.box.y1);
        const size_t bw = size_t(s.box.x1 - s.box.x0);
        for (int y = y0; y < y1; ++y) {
            uint32_t* d = &px[size_t(y) * size_t(width)];
            const size_t row = size_t(y - s.box.y0) * bw - size_t(s.box.x0);
            for (int x = x0; x < x1; ++x) over(d[x], s.first[row + size_t(x)]);
            if (!s.then.empty())
                for (int x = x0; x < x1; ++x) over(d[x], s.then[row + size_t(x)]);
        }
    }
    // A mask of w x h opacities (a glyph) with its top left at the pixel (x, y), in colour c.
    void mask(const uint8_t* m, int w, int h, int x, int y, ColorF c) {
        const Box o = area();
        for (int j = std::max(0, o.y0 - y); j < h && y + j < o.y1; ++j)
            for (int i = std::max(0, o.x0 - x); i < w && x + i < o.x1; ++i)
                if (const uint8_t a = m[size_t(j) * size_t(w) + size_t(i)]) over(px[size_t(y + j) * size_t(width) + size_t(x + i)], pack(c, float(a) / 255.f));
    }

    // The pixels as a PNG file of 8-bit RGBA (stored without compression: the format allows it, and every
    // reader of PNG files reads it). False when the file cannot be written.
    bool savePng(const std::string& file) const {
        std::FILE* f = std::fopen(file.c_str(), "wb");
        if (!f) return false;
        std::vector<uint8_t> raw;
        raw.reserve(size_t(height) * (size_t(width) * 4 + 1));
        for (int y = 0; y < height; ++y) {
            raw.push_back(0);
            for (int x = 0; x < width; ++x) {
                const uint32_t p = px[size_t(y) * size_t(width) + size_t(x)];
                const uint32_t a = p >> 24;
                const auto un = [a](uint32_t v) { return uint8_t(a ? std::min<uint32_t>(255, (v * 255 + a / 2) / a) : 0); };
                raw.push_back(un(p >> 16 & 255)), raw.push_back(un(p >> 8 & 255)), raw.push_back(un(p & 255)), raw.push_back(uint8_t(a));
            }
        }
        std::vector<uint8_t> z = {0x78, 0x01};
        for (size_t at = 0; at < raw.size() || at == 0;) {
            const size_t n = std::min<size_t>(65535, raw.size() - at);
            z.push_back(at + n == raw.size() ? 1 : 0);
            z.push_back(uint8_t(n)), z.push_back(uint8_t(n >> 8)), z.push_back(uint8_t(~n)), z.push_back(uint8_t(~n >> 8));
            z.insert(z.end(), raw.begin() + long(at), raw.begin() + long(at + n));
            at += n;
            if (n == 0) break;
        }
        uint32_t s1 = 1, s2 = 0;
        for (const uint8_t b : raw) s1 = (s1 + b) % 65521, s2 = (s2 + s1) % 65521;
        const uint32_t adler = s2 << 16 | s1;
        for (int k = 3; k >= 0; --k) z.push_back(uint8_t(adler >> (8 * k)));
        const auto chunk = [f](const char* type, const std::vector<uint8_t>& data) {
            uint8_t head[8] = {uint8_t(data.size() >> 24), uint8_t(data.size() >> 16), uint8_t(data.size() >> 8), uint8_t(data.size()),
                               uint8_t(type[0]), uint8_t(type[1]), uint8_t(type[2]), uint8_t(type[3])};
            std::fwrite(head, 1, 8, f);
            if (!data.empty()) std::fwrite(data.data(), 1, data.size(), f);
            uint32_t crc = 0xFFFFFFFFu;
            const auto add = [&crc](uint8_t b) {
                crc ^= b;
                for (int k = 0; k < 8; ++k) crc = crc & 1 ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
            };
            for (int k = 4; k < 8; ++k) add(head[k]);
            for (const uint8_t b : data) add(b);
            crc ^= 0xFFFFFFFFu;
            const uint8_t tail[4] = {uint8_t(crc >> 24), uint8_t(crc >> 16), uint8_t(crc >> 8), uint8_t(crc)};
            std::fwrite(tail, 1, 4, f);
        };
        static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
        std::fwrite(signature, 1, 8, f);
        chunk("IHDR", {uint8_t(width >> 24), uint8_t(width >> 16), uint8_t(width >> 8), uint8_t(width), uint8_t(height >> 24), uint8_t(height >> 16),
                       uint8_t(height >> 8), uint8_t(height), 8, 6, 0, 0, 0});
        chunk("IDAT", z);
        chunk("IEND", {});
        return std::fclose(f) == 0;
    }

    static uint32_t pack(ColorF c, float coverage) {
        const float a = std::clamp(c.a * coverage, 0.f, 1.f);
        const auto ch = [a](float v) { return uint32_t(std::lround(std::clamp(v, 0.f, 1.f) * a * 255.f)); };
        return uint32_t(std::lround(a * 255.f)) << 24 | ch(c.r) << 16 | ch(c.g) << 8 | ch(c.b);
    }

private:
    Box area() const { return clips.empty() ? Box{0, 0, width, height} : clips.back(); }
    static void over(uint32_t& d, uint32_t s) {
        const uint32_t sa = s >> 24;
        if (sa == 255) return void(d = s);
        if (!sa && !(s & 0xFFFFFFu)) return;
        const uint32_t k = 255 - sa;
        const auto ch = [&](int shift) { return std::min<uint32_t>(255, (s >> shift & 255) + ((d >> shift & 255) * k + 127) / 255) << shift; };
        d = ch(24) | ch(16) | ch(8) | ch(0);
    }
    static uint32_t mix(uint32_t a, uint32_t b, float f) {
        const auto ch = [&](int shift) { return uint32_t(std::lround(float(a >> shift & 255) * (1 - f) + float(b >> shift & 255) * f)) << shift; };
        return ch(24) | ch(16) | ch(8) | ch(0);
    }
    // Every pixel of the box (l, t)-(r, b), widened by one, painted with c at the coverage `at` gives its centre.
    template <class At> void cover(float l, float t, float r, float b, ColorF c, At&& at) {
        coverPixels(l, t, r, b, area(), c, at, [&](int x, int y, uint32_t s) { over(px[size_t(y) * size_t(width) + size_t(x)], s); });
    }
    // ... each pixel of it inside o handed to put(x, y, its colour at that coverage).
    template <class At, class Put> static void coverPixels(float l, float t, float r, float b, Box o, ColorF c, At&& at, Put&& put) {
        const int x0 = std::max(o.x0, int(std::floor(l - 1))), x1 = std::min(o.x1, int(std::ceil(r + 1)));
        const int y0 = std::max(o.y0, int(std::floor(t - 1))), y1 = std::min(o.y1, int(std::ceil(b + 1)));
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                const float k = at(float(x) + 0.5f, float(y) + 0.5f);
                if (k > 0) put(x, y, pack(c, std::min(k, 1.f)));
            }
    }
    // The coverage of the pixel centred at (x, y) by the box (l, t)-(r, b) of pixels.
    struct RectAt {
        float l, t, r, b;
        float operator()(float x, float y) const {
            const float cx = std::clamp(std::min(r, x + 0.5f) - std::max(l, x - 0.5f), 0.f, 1.f);
            const float cy = std::clamp(std::min(b, y + 0.5f) - std::max(t, y - 0.5f), 0.f, 1.f);
            return cx * cy;
        }
    };
    static RectAt rectAt(float l, float t, float r, float b) { return RectAt{l, t, r, b}; }
    // The pixels of image() inside o, each handed to put(x, y, its colour).
    template <class Put> void imagePixels(const uint32_t* pic, int w, int h, RectF from, RectF to, Box o, Put&& put) const {
        const float l = to.left * scale + dx, t = to.top * scale + dy, r = to.right * scale + dx, b = to.bottom * scale + dy;
        if (r <= l || b <= t || w <= 0 || h <= 0) return;
        const float sx = (from.right - from.left) / (r - l), sy = (from.bottom - from.top) / (b - t);
        const int x0 = std::max(o.x0, int(std::ceil(l - 0.5f))), x1 = std::min(o.x1, int(std::ceil(r - 0.5f)));
        const int y0 = std::max(o.y0, int(std::ceil(t - 0.5f))), y1 = std::min(o.y1, int(std::ceil(b - 0.5f)));
        for (int y = y0; y < y1; ++y) {
            const float v = from.top + (float(y) + 0.5f - t) * sy - 0.5f;
            const int v0 = std::clamp(int(std::floor(v)), 0, h - 1), v1 = std::min(v0 + 1, h - 1);
            const float fv = std::clamp(v - float(v0), 0.f, 1.f);
            for (int x = x0; x < x1; ++x) {
                const float u = from.left + (float(x) + 0.5f - l) * sx - 0.5f;
                const int u0 = std::clamp(int(std::floor(u)), 0, w - 1), u1 = std::min(u0 + 1, w - 1);
                const float fu = std::clamp(u - float(u0), 0.f, 1.f);
                put(x, y, mix(mix(pic[size_t(v0) * size_t(w) + size_t(u0)], pic[size_t(v0) * size_t(w) + size_t(u1)], fu),
                              mix(pic[size_t(v1) * size_t(w) + size_t(u0)], pic[size_t(v1) * size_t(w) + size_t(u1)], fu), fv));
            }
        }
    }
    // stroke < 0: filled; else the outline of that width, its middle on the edge.
    void roundShape(RectF r, float radius, ColorF c, float stroke) {
        const float l = r.left * scale + dx, t = r.top * scale + dy, rr = r.right * scale + dx, b = r.bottom * scale + dy;
        const float cx = (l + rr) / 2, cy = (t + b) / 2, hx = (rr - l) / 2, hy = (b - t) / 2;
        const float rad = std::min({radius * scale, hx, hy}), half = stroke < 0 ? 0 : stroke * scale / 2;
        cover(l - half, t - half, rr + half, b + half, c, [&](float x, float y) {
            const float qx = std::abs(x - cx) - (hx - rad), qy = std::abs(y - cy) - (hy - rad);
            const float outside = std::hypot(std::max(qx, 0.f), std::max(qy, 0.f)) + std::min(std::max(qx, qy), 0.f) - rad;
            return stroke < 0 ? 0.5f - outside : half + 0.5f - std::abs(outside);
        });
    }
    void ellipseShape(float x, float y, float rx, float ry, ColorF c, float stroke) {
        const float ex = x * scale + dx, ey = y * scale + dy, ax = std::max(rx * scale, 0.01f), ay = std::max(ry * scale, 0.01f);
        const float half = stroke < 0 ? 0 : stroke * scale / 2;
        cover(ex - ax - half, ey - ay - half, ex + ax + half, ey + ay + half, c, [&](float px_, float py_) {
            const float u = (px_ - ex) / ax, v = (py_ - ey) / ay;
            const float f = u * u + v * v - 1, g = 2 * std::sqrt(u * u / (ax * ax) + v * v / (ay * ay));
            const float outside = g > 1e-6f ? f / g : -std::min(ax, ay);
            return stroke < 0 ? 0.5f - outside : half + 0.5f - std::abs(outside);
        });
    }
};

} // namespace play
