/**
 * PKSEFramebuffer, implemented by RECORDING instead of rasterizing.
 *
 * This file replaces src/UI/PKSEFramebuffer.cpp in the host UI harness and is the whole reason the
 * harness needs no GPU, no window, no SDL and no NanoVG: every SDL/GL/NanoVG symbol in the entire UI
 * layer lives in the file this one stands in for. Screens, panels, dialogs and modals are compiled
 * from the SAME sources the .nro uses and are not aware of the substitution.
 *
 * FIDELITY -- the one honest caveat. Text advance widths here are computed from a per-style average,
 * not from Nunito's real glyph metrics, so anything whose layout is driven by measureText (centring,
 * right-alignment, ellipsis truncation) lands within a few pixels of where the console puts it
 * rather than exactly on it. Positional assertions should therefore be written with a tolerance, and
 * a test that needs pixel-exact text placement belongs in the on-console pass. Everything else --
 * which primitives were drawn, in what order, with what content, at what grid position, in what
 * colour, inside which clip -- is exact, because it is recorded at the call rather than measured
 * off a rendering.
 */
#include "FrameRecorder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace UI;

namespace UIHost {

    Recorder& Recorder::get() { static Recorder r; return r; }

    void Recorder::beginFrame() { cur.draws.clear(); cur.index = frames; clipping = false; }

    void Recorder::push(Draw d) {
        d.visible = clipContains(d.drawX, d.drawY, d.drawWidth, d.drawHeight);
        cur.draws.push_back(std::move(d));
    }

    void Recorder::endFrame() { done = cur; done.index = frames++; cur.draws.clear(); clipping = false; }

    void Recorder::setClip(int x, int y, int w, int h) { clipping = true; cx = x; cy = y; cw = w; ch = h; }
    void Recorder::clearClip() { clipping = false; }

    bool Recorder::clipContains(int x, int y, int w, int h) const {
        if (!clipping) return true;
        // Any overlap counts as visible: a partially clipped label is still on screen.
        return x < cx + cw && x + std::max(w, 1) > cx && y < cy + ch && y + std::max(h, 1) > cy;
    }

    static bool contains(const std::string& hay, const std::string& needle) {
        return hay.find(needle) != std::string::npos;
    }

    std::vector<const Draw*> Frame::text(const std::string& needle, bool visibleOnly) const {
        std::vector<const Draw*> out;
        for (const Draw& d : draws)
            if (d.kind == Kind::Text && (!visibleOnly || d.visible) && contains(d.text, needle))
                out.push_back(&d);
        return out;
    }

    std::vector<const Draw*> Frame::textExact(const std::string& s, bool visibleOnly) const {
        std::vector<const Draw*> out;
        for (const Draw& d : draws)
            if (d.kind == Kind::Text && (!visibleOnly || d.visible) && d.text == s)
                out.push_back(&d);
        return out;
    }

    const Draw* Frame::first(const std::string& needle) const {
        auto v = text(needle);
        return v.empty() ? nullptr : v.front();
    }

    std::vector<const Draw*> Frame::ops(const char* op, bool visibleOnly) const {
        std::vector<const Draw*> out;
        for (const Draw& d : draws)
            if ((!visibleOnly || d.visible) && std::strcmp(d.op, op) == 0) out.push_back(&d);
        return out;
    }

    size_t Frame::sprites() const {
        size_t n = 0;
        for (const Draw& d : draws) if (d.kind == Kind::Sprite && d.visible) ++n;
        return n;
    }

    std::string Frame::dump() const {
        std::string s;
        for (const Draw& d : draws) {
            if (d.kind != Kind::Text || !d.visible) continue;
            s += "    [" + std::to_string(d.drawX) + "," + std::to_string(d.drawY) + "] " + d.text + "\n";
        }
        return s;
    }
}

namespace {
    // The real sizes from src/UI/PKSEFramebuffer.cpp. Kept in step by the harness's own check --
    // see uitest.cpp, which fails if these drift from the values that file uses.
    constexpr float FONT_SIZES[static_cast<int>(TextStyle::Count)] = { 15.0f, 19.0f, 26.0f, 34.0f };

    /// Average advance as a fraction of em for Nunito, measured over the strings this UI actually
    /// draws (species names, numbers, labels). Good to a few pixels on a short string.
    constexpr float AVERAGE_GLYPH_ADVANCE = 0.50f;

    UIHost::Draw shape(const char* op, int x, int y, int w, int h, Color c) {
        UIHost::Draw d;
        d.kind = UIHost::Kind::Shape; d.op = op;
        d.drawX = x; d.drawY = y; d.drawWidth = w; d.drawHeight = h; d.color = c;
        return d;
    }
    void rec(UIHost::Draw d) { UIHost::Recorder::get().push(std::move(d)); }
}

namespace UI {

    PKSEFramebuffer::PKSEFramebuffer()
        : window(nullptr), glContext(nullptr), vg(nullptr),
          fontSans(0), fontSym(0), fontSym2(0), width(1280), height(720) {}

    PKSEFramebuffer::~PKSEFramebuffer() = default;

    void PKSEFramebuffer::clear(Color color) {
        UIHost::Recorder::get().beginFrame();
        UIHost::Draw d = shape("clear", 0, 0, width, height, color);
        d.kind = UIHost::Kind::Clear;
        rec(std::move(d));
    }

    void PKSEFramebuffer::flush() { UIHost::Recorder::get().endFrame(); }

    void PKSEFramebuffer::drawPixel(int x, int y, Color c)                    { rec(shape("pixel", x, y, 1, 1, c)); }
    void PKSEFramebuffer::drawRect(int x, int y, int w, int h, Color c)       { rec(shape("rect", x, y, w, h, c)); }
    void PKSEFramebuffer::drawFilledRect(int x, int y, int w, int h, Color c) {
        rec(shape("filledRect", x, y, w, h, c));
    }
    void PKSEFramebuffer::drawFilledEllipse(int cx, int cy, int rx, int ry, Color c) {
        rec(shape("ellipse", cx - rx, cy - ry, rx * 2, ry * 2, c));
    }
    void PKSEFramebuffer::drawFilledRoundedRect(int x, int y, int w, int h, int, Color c) {
        rec(shape("filledRoundedRect", x, y, w, h, c));
    }
    void PKSEFramebuffer::drawRoundedRect(int x, int y, int w, int h, int, Color c, int) {
        rec(shape("roundedRect", x, y, w, h, c));
    }
    void PKSEFramebuffer::drawFilledCircle(int cx, int cy, int r, Color c) {
        rec(shape("filledCircle", cx - r, cy - r, r * 2, r * 2, c));
    }
    void PKSEFramebuffer::drawCircle(int cx, int cy, int r, Color c, int) {
        rec(shape("circle", cx - r, cy - r, r * 2, r * 2, c));
    }
    void PKSEFramebuffer::drawSoftShadow(int x, int y, int w, int h, int) {
        if (w > 0 && h > 0) rec(shape("softShadow", x, y, w, h, Color(0, 0, 0, 110)));
    }
    void PKSEFramebuffer::drawVerticalGradient(int x, int y, int w, int h, Color top, Color) {
        if (w > 0 && h > 0) rec(shape("gradient", x, y, w, h, top));
    }
    void PKSEFramebuffer::drawShinyMark(int x, int y, int size, Color c) {
        if (size > 1) rec(shape("shiny", x, y, size, size, c));
    }
    void PKSEFramebuffer::drawPointerCursor(int tipX, int tipY, int hh, Color c) {
        rec(shape("cursor", tipX - hh / 2, tipY - hh, hh, hh, c));
    }
    // Recorded by KIND, so a test can tell a folder row from a file row -- which is the whole
    // reason the browser draws two different icons.
    void PKSEFramebuffer::drawFolderIcon(int x, int y, int size, Color c, bool open) {
        if (size > 3) rec(shape(open ? "folder-open" : "folder", x, y, size, size, c));
    }
    void PKSEFramebuffer::drawFileIcon(int x, int y, int size, Color c) {
        if (size > 3) rec(shape("file", x, y, size, size, c));
    }
    void PKSEFramebuffer::drawSearchIcon(int x, int y, int size, Color c) {
        if (size > 3) rec(shape("search", x, y, size, size, c));
    }
    void PKSEFramebuffer::drawEvolveArrow(int x, int y, int size, Color c) {
        if (size > 3) rec(shape("evolve-arrow", x, y, size, size, c));
    }
    void PKSEFramebuffer::drawBackspaceIcon(int x, int y, int size, Color c) {
        if (size > 3) rec(shape("backspace", x, y, size, size, c));
    }
    // Recorded by STATE, so a test can see shift turn on without reading the key's fill colour.
    void PKSEFramebuffer::drawShiftIcon(int x, int y, int size, Color c, bool filled) {
        if (size > 3) rec(shape(filled ? "shift-on" : "shift", x, y, size, size, c));
    }

    void PKSEFramebuffer::drawStatHexagon(int cx, int cy, int R, const float*, int, float, Color f, Color, Color) {
        rec(shape("hexagon", cx - R, cy - R, R * 2, R * 2, f));
    }

    // These delegate to other PUBLIC primitives in src/UI/PKSEFramebuffer.cpp, and are mirrored
    // exactly here rather than recorded as a single op. A card really is a filled rounded rect plus
    // an outline; recording it as one "card" would make the primitive stream the harness sees
    // differ from the one the console produces, and any test that counted primitives would then be
    // testing the harness rather than the UI.
    void PKSEFramebuffer::drawCard(int x, int y, int w, int h) {
        drawFilledRoundedRect(x, y, w, h, 14, Colors::Panel);
        drawRoundedRect(x, y, w, h, 14, Colors::Border, 1);
    }

    void PKSEFramebuffer::drawSelectionHighlight(int x, int y, int w, int h) {
        drawFilledRoundedRect(x, y, w, h, 10, Colors::Selected);
        drawRoundedRect(x, y, w, h, 10, Colors::Accent, 2);
    }

    void PKSEFramebuffer::drawHDivider(int x, int y, int w) { drawFilledRect(x, y, w, 2, Colors::Border); }

    void PKSEFramebuffer::drawPill(int x, int y, int w, int h, Color c) { drawFilledRoundedRect(x, y, w, h, h / 2, c); }
    void PKSEFramebuffer::drawPillBorder(int x, int y, int w, int h, Color c, int t) {
        drawRoundedRect(x, y, w, h, h / 2, c, t);
    }

    void PKSEFramebuffer::drawEgg(int cx, int cy, int size) {
        const int rx = std::max(4, static_cast<int>(size * 0.30f));
        const int ry = std::max(5, static_cast<int>(size * 0.40f));
        drawFilledEllipse(cx, cy, rx + 2, ry + 2, Color(206, 196, 168, 255));
        drawFilledEllipse(cx, cy, rx, ry, Color(238, 232, 210, 255));
        const Color spot(120, 198, 176, 255);
        drawFilledCircle(cx - rx / 3, cy - ry / 3, std::max(2, size / 13), spot);
        drawFilledCircle(cx + rx / 3, cy,          std::max(2, size / 16), spot);
        drawFilledCircle(cx - rx / 5, cy + ry / 3, std::max(2, size / 18), spot);
    }

    int PKSEFramebuffer::drawTypeBadge(int x, int y, const std::string& typeName, Color typeColor) {
        const int h = 24;
        int tw = 0, th = 0; measureText(typeName, tw, th, TextStyle::Caption);
        const int padX = 12, total = tw + padX * 2;
        drawFilledRoundedRect(x, y, total, h, h / 2, typeColor);
        const int lum = (typeColor.red * 299 + typeColor.green * 587 + typeColor.blue * 114) / 1000;
        const Color txt = lum > 150 ? Color(30, 30, 36) : Color(245, 245, 250);
        drawText(x + padX, y + (h - th) / 2, typeName, txt, TextStyle::Caption);
        return total;
    }

    void PKSEFramebuffer::drawText(int x, int y, const char* text, Color c, TextStyle s) {
        if (!text || !*text) return;
        drawText(x, y, std::string(text), c, s);
    }

    void PKSEFramebuffer::drawText(int x, int y, const std::string& text, Color c, TextStyle s) {
        if (text.empty()) return;                       // matches the real backend's early-out
        int w = 0, h = 0;
        measureText(text, w, h, s);
        UIHost::Draw d;
        d.kind = UIHost::Kind::Text; d.op = "text";
        d.drawX = x; d.drawY = y; d.drawWidth = w; d.drawHeight = h;
        d.text = text; d.color = c; d.style = s;
        rec(std::move(d));
    }

    void PKSEFramebuffer::drawSymbol(int x, int y, const std::string& symbol, Color c, TextStyle s) {
        if (symbol.empty()) return;
        int w = 0, h = 0;
        measureText(symbol, w, h, s);
        UIHost::Draw d;
        d.kind = UIHost::Kind::Text; d.op = "symbol";
        d.drawX = x; d.drawY = y; d.drawWidth = w; d.drawHeight = h;
        d.text = symbol; d.color = c; d.style = s;
        rec(std::move(d));
    }

    void PKSEFramebuffer::drawImage(int x, int y, int iw, int ih, const unsigned char* data, int) {
        if (!data) return;
        UIHost::Draw d = shape("image", x, y, iw, ih, Colors::White);
        d.kind = UIHost::Kind::Sprite;
        rec(std::move(d));
    }

    void PKSEFramebuffer::drawImageScaled(int x, int y, int, int, int dw, int dh, const unsigned char* data, int) {
        if (!data) return;
        UIHost::Draw d = shape("imageScaled", x, y, dw, dh, Colors::White);
        d.kind = UIHost::Kind::Sprite;
        rec(std::move(d));
    }

    void PKSEFramebuffer::drawSpriteIdle(int x, int y, int boxW, int boxH, int, int,
                                         const unsigned char* data, int, float) {
        if (!data) return;
        UIHost::Draw d = shape("spriteIdle", x, y, boxW, boxH, Colors::White);
        d.kind = UIHost::Kind::Sprite;
        rec(std::move(d));
    }

    void PKSEFramebuffer::setClipRect(int x, int y, int w, int h) { UIHost::Recorder::get().setClip(x, y, w, h); }
    void PKSEFramebuffer::clearClip()                            { UIHost::Recorder::get().clearClip(); }

    void PKSEFramebuffer::measureText(const std::string& text, int& outW, int& outH, TextStyle s) {
        const float size = FONT_SIZES[static_cast<int>(s)];
        outW = static_cast<int>(std::lround(size * AVERAGE_GLYPH_ADVANCE * static_cast<float>(text.size())));
        outH = lineHeight(s);
    }

    // Mirrors the real query from Nunito's own metrics (hhea ascent 1011, descent -353; cap height
    // 705 per 1000 em), so text placed with it lands where the console puts it rather than where a
    // line-box guess would.
    int PKSEFramebuffer::textYCenteredOn(int centerY, TextStyle s) const {
        const float size = FONT_SIZES[static_cast<int>(s)];
        const float ascender = size * 1011.0f / (1011.0f + 353.0f);
        return static_cast<int>(std::lround(static_cast<float>(centerY) - ascender + size * 0.705f / 2.0f));
    }

    int PKSEFramebuffer::lineHeight(TextStyle s) const {
        return static_cast<int>(std::lround(FONT_SIZES[static_cast<int>(s)] * 1.35f));
    }

    // Timing is DETERMINISTIC here, not wall-clock: a test that pressed the same buttons must record
    // the same frame every run, and an idle animation driven by a real clock would make sprite
    // positions differ between runs on the same input. The driver advances this per frame.
    void PKSEFramebuffer::startFade()        { fadeStart = -100.0; }   // fades are skipped entirely
    void PKSEFramebuffer::drawFadeOverlay()  {}

    void PKSEFramebuffer::invalidateImage(const unsigned char*) {}
    void PKSEFramebuffer::onSpriteEvicted(const unsigned char*) {}
}
