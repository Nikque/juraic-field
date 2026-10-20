#pragma once
// The drawing of the canvas outside Windows: the marks of play/raster.h, and text with stb_truetype from the font
// files of the folder "fonts" next to the program (play/gfx.h builds the pieces of them). Text is laid out as
// DirectWrite lays it out on Windows closely enough: one line, in a box, from its top, left, centred or right.
#include "raster.h"
#include "../third_party/stb/stb_truetype.h"
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace play {

// A font file read whole, and the glyphs drawn of it so far (by size in pixels and character).
struct Face {
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
    bool ready = false;
    struct Glyph {
        std::vector<uint8_t> mask;
        int w = 0, h = 0, x = 0, y = 0; // the mask, and where its top left is from the pen on the baseline
        float advance = 0;
    };
    std::map<std::pair<int, char32_t>, Glyph> glyphs; // (the size in quarter pixels, the character)

    bool load(const std::filesystem::path& file, int index = 0) {
        std::ifstream in(file, std::ios::binary);
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (data.empty()) return false;
        const int offset = stbtt_GetFontOffsetForIndex(data.data(), index);
        ready = offset >= 0 && stbtt_InitFont(&info, data.data(), offset) != 0;
        return ready;
    }
    // The scale of the font for a size in pixels (the size of the em, as DirectWrite takes it).
    float scaleFor(float size) const { return stbtt_ScaleForMappingEmToPixels(&info, size); }
    float ascent(float size) const {
        int a = 0, d = 0, gap = 0;
        stbtt_GetFontVMetrics(&info, &a, &d, &gap);
        return float(a) * scaleFor(size);
    }
    const Glyph& glyph(float size, char32_t c) {
        const int key = int(size * 4 + 0.5f);
        const auto found = glyphs.find({key, c});
        if (found != glyphs.end()) return found->second;
        Glyph g;
        const float s = scaleFor(float(key) / 4);
        int advance = 0, bearing = 0;
        stbtt_GetCodepointHMetrics(&info, int(c), &advance, &bearing);
        g.advance = float(advance) * s;
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBox(&info, int(c), s, s, &x0, &y0, &x1, &y1);
        g.w = x1 - x0, g.h = y1 - y0, g.x = x0, g.y = y0;
        if (g.w > 0 && g.h > 0) {
            g.mask.assign(size_t(g.w) * size_t(g.h), 0);
            stbtt_MakeCodepointBitmap(&info, g.mask.data(), g.w, g.h, g.w, s, s, int(c));
        }
        return glyphs.emplace(std::make_pair(key, c), std::move(g)).first->second;
    }
};

struct GfxBase {
    Raster raster;
    // The normal and the bold weight. Text is drawn bold at every size, the nearest to the weights of Windows (semi-bold
    // for the two smaller sizes, bold for the larger); without a bold file, normal.
    Face face[2];
    bool shadow = false;    // text is drawn with a dark edge (a picture is behind it)
    static constexpr float SIZES[4] = {18, 24, 36, 60}; // 0 small, 1 normal, 2 large, 3 huge

    // The fonts of the game: "regular" and "bold" in `folder` (a .ttf, .otf with TrueType outlines, or .ttc).
    void loadFonts(const std::filesystem::path& folder) {
        for (int k = 0; k < 2; ++k) {
            std::error_code ignored;
            for (std::filesystem::directory_iterator it(folder, ignored), end; !ignored && it != end && !face[k].ready; it.increment(ignored)) {
                const std::string name = it->path().filename().string();
                std::string lower;
                for (const char c : name) lower += char(std::tolower(static_cast<unsigned char>(c)));
                const bool bold = lower.find("bold") != std::string::npos;
                const bool font = lower.size() > 4 && (lower.ends_with(".ttf") || lower.ends_with(".otf") || lower.ends_with(".ttc"));
                if (font && bold == (k == 1)) face[k].load(it->path());
            }
        }
    }
    // A frame of width x height pixels: cleared, the canvas fitted into its middle.
    void begin(int width, int height, ColorF background) {
        if (width != raster.width || height != raster.height) raster.resize(width, height);
        raster.clips.clear();
        raster.clear(background);
        raster.fit();
    }

    void fill(RectF r, ColorF c) { raster.fillRect(r, c); }
    void round(RectF r, float radius, ColorF c) { raster.fillRound(r, radius, c); }
    void frame(RectF r, float radius, ColorF c, float width) { raster.strokeRound(r, radius, c, width); }
    void circle(float x, float y, float rx, float ry, ColorF c) { raster.fillEllipse(x, y, rx, ry, c); }
    void ring(float x, float y, float rx, float ry, ColorF c, float width) { raster.strokeEllipse(x, y, rx, ry, c, width); }
    void clip(RectF r) { raster.clip(r); }
    void unclip() { raster.unclip(); }
    // align: -1 left, 0 centre, 1 right
    void text(const std::wstring& s, float x, float y, float w, float, int size, ColorF c, int align = -1) {
        Face& f = face[face[1].ready ? 1 : 0];
        if (!f.ready) return;
        // (the characters and the room each takes, once for the line and its edge)
        Line l;
        l.px = SIZES[size] * raster.scale; // (the size in pixels of the window)
        l.text = toChars(s);
        l.advance.resize(l.text.size()), l.kern.resize(l.text.size());
        for (size_t i = 0; i < l.text.size(); ++i) {
            l.advance[i] = f.glyph(l.px, l.text[i]).advance;
            l.kern[i] = i + 1 < l.text.size() ? float(stbtt_GetCodepointKernAdvance(&f.info, int(l.text[i]), int(l.text[i + 1]))) * f.scaleFor(l.px) : 0;
            l.width += l.advance[i];
            if (i + 1 < l.text.size()) l.width += l.kern[i];
        }
        if (shadow) {
            // (a picture is behind the game: every text gets a dark edge, so that it can be read on any picture)
            for (int k = 0; k < 4; ++k) line(f, l, x + (k & 1 ? 1.5f : -1.5f), y + (k & 2 ? 1.5f : -1.5f), w, colorF(0, 0, 0, 0.7f * c.a), align);
        }
        line(f, l, x, y, w, c, align);
    }

private:
    struct Line {
        float px = 0, width = 0;
        std::u32string text;
        std::vector<float> advance, kern; // by character: its advance, and the kerning to the next one
    };
    void line(Face& f, const Line& l, float x, float y, float w, ColorF c, int align) {
        const float left = x * raster.scale + raster.dx, boxW = w * raster.scale;
        float pen = align < 0 ? left : align == 0 ? left + (boxW - l.width) / 2 : left + boxW - l.width;
        // (the line lower by 0.22 of the size: the middle of the letters then stands where DirectWrite puts that of the
        //  font of Windows, measured on the self-test pictures of both at sizes 18, 24 and 36)
        const float baseline = y * raster.scale + raster.dy + f.ascent(l.px) + 0.22f * l.px;
        for (size_t i = 0; i < l.text.size(); ++i) {
            const Face::Glyph& g = f.glyph(l.px, l.text[i]);
            if (!g.mask.empty()) raster.mask(g.mask.data(), g.w, g.h, int(std::lround(pen)) + g.x, int(std::lround(baseline)) + g.y, c);
            pen += l.advance[i];
            if (i + 1 < l.text.size()) pen += l.kern[i];
        }
    }
    // The characters of a text (wchar_t is UTF-32 here; a pair of UTF-16 halves is joined all the same).
    static std::u32string toChars(const std::wstring& s) {
        std::u32string out;
        for (size_t i = 0; i < s.size(); ++i) {
            char32_t c = char32_t(s[i]);
            if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size() && char32_t(s[i + 1]) >= 0xDC00 && char32_t(s[i + 1]) < 0xE000)
                c = 0x10000 + ((c - 0xD800) << 10) + (char32_t(s[i + 1]) - 0xDC00), ++i;
            out += c;
        }
        return out;
    }
};

} // namespace play
