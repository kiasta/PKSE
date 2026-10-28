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

namespace UIHost
{

    Recorder &Recorder::get()
    {
        static Recorder recorder;
        return recorder;
    }

    void Recorder::beginFrame()
    {
        currentFrame.draws.clear();
        currentFrame.frameIndex = flushedFrameCount;
        clipEnabled = false;
    }

    void Recorder::push(Draw draw)
    {
        draw.visible = clipContains(draw.drawX, draw.drawY, draw.drawWidth, draw.drawHeight);
        currentFrame.draws.push_back(std::move(draw));
    }

    void Recorder::endFrame()
    {
        finishedFrame = currentFrame;
        finishedFrame.frameIndex = flushedFrameCount++;
        currentFrame.draws.clear();
        clipEnabled = false;
    }

    void Recorder::setClip(int rectX, int rectY, int rectWidth, int rectHeight)
    {
        clipEnabled = true;
        clipX = rectX;
        clipY = rectY;
        clipWidth = rectWidth;
        clipHeight = rectHeight;
    }
    void Recorder::clearClip()
    {
        clipEnabled = false;
    }

    bool Recorder::clipContains(int drawX, int drawY, int drawWidth, int drawHeight) const
    {
        if (!clipEnabled)
        {
            return true;
        }
        // Any overlap counts as visible: a partially clipped label is still on screen.
        return drawX < clipX + clipWidth && drawX + std::max(drawWidth, 1) > clipX && drawY < clipY + clipHeight && drawY + std::max(drawHeight, 1) > clipY;
    }

    static bool contains(const std::string &haystack, const std::string &needle)
    {
        return haystack.find(needle) != std::string::npos;
    }

    std::vector<const Draw *> Frame::text(const std::string &needle, bool visibleOnly) const
    {
        std::vector<const Draw *> matches;
        for (const Draw &draw : draws)
        {
            if (draw.kind == Kind::Text && (!visibleOnly || draw.visible) && contains(draw.text, needle))
            {
                matches.push_back(&draw);
            }
        }
        return matches;
    }

    std::vector<const Draw *> Frame::textExact(const std::string &wholeText, bool visibleOnly) const
    {
        std::vector<const Draw *> matches;
        for (const Draw &draw : draws)
        {
            if (draw.kind == Kind::Text && (!visibleOnly || draw.visible) && draw.text == wholeText)
            {
                matches.push_back(&draw);
            }
        }
        return matches;
    }

    const Draw *Frame::first(const std::string &needle) const
    {
        auto matches = text(needle);
        return matches.empty() ? nullptr : matches.front();
    }

    std::vector<const Draw *> Frame::primitives(const char *primitive, bool visibleOnly) const
    {
        std::vector<const Draw *> matches;
        for (const Draw &draw : draws)
        {
            if ((!visibleOnly || draw.visible) && std::strcmp(draw.primitive, primitive) == 0)
            {
                matches.push_back(&draw);
            }
        }
        return matches;
    }

    size_t Frame::spriteCount() const
    {
        size_t visibleSpriteCount = 0;
        for (const Draw &draw : draws)
        {
            if (draw.kind == Kind::Sprite && draw.visible)
            {
                ++visibleSpriteCount;
            }
        }
        return visibleSpriteCount;
    }

    std::string Frame::dump() const
    {
        std::string dumpText;
        for (const Draw &draw : draws)
        {
            if (draw.kind != Kind::Text || !draw.visible)
            {
                continue;
            }
            dumpText += "    [" + std::to_string(draw.drawX) + "," + std::to_string(draw.drawY) + "] " + draw.text + "\n";
        }
        return dumpText;
    }
}

namespace
{
    // The real sizes from src/UI/PKSEFramebuffer.cpp. Kept in step by the harness's own check --
    // see uitest.cpp, which fails if these drift from the values that file uses.
    constexpr float FONT_SIZES[static_cast<int>(TextStyle::Count)] = {15.0f, 19.0f, 26.0f, 34.0f};

    /// Average advance as a fraction of em for Nunito, measured over the strings this UI actually
    /// draws (species names, numbers, labels). Good to a few pixels on a short string.
    constexpr float AVERAGE_GLYPH_ADVANCE = 0.50f;

    UIHost::Draw makeShape(const char *primitive, int shapeX, int shapeY, int shapeWidth, int shapeHeight, Color color)
    {
        UIHost::Draw draw;
        draw.kind = UIHost::Kind::Shape;
        draw.primitive = primitive;
        draw.drawX = shapeX;
        draw.drawY = shapeY;
        draw.drawWidth = shapeWidth;
        draw.drawHeight = shapeHeight;
        draw.color = color;
        return draw;
    }
    void record(UIHost::Draw draw)
    {
        UIHost::Recorder::get().push(std::move(draw));
    }
}

namespace UI
{

    PKSEFramebuffer::PKSEFramebuffer() : window(nullptr), glContext(nullptr), vg(nullptr), fontSans(0), fontSym(0), fontSym2(0), width(1280), height(720) {}

    PKSEFramebuffer::~PKSEFramebuffer() = default;

    void PKSEFramebuffer::clear(Color color)
    {
        UIHost::Recorder::get().beginFrame();
        UIHost::Draw draw = makeShape("clear", 0, 0, width, height, color);
        draw.kind = UIHost::Kind::Clear;
        record(std::move(draw));
    }

    void PKSEFramebuffer::flush()
    {
        UIHost::Recorder::get().endFrame();
    }

    void PKSEFramebuffer::drawPixel(int pixelX, int pixelY, Color color)
    {
        record(makeShape("pixel", pixelX, pixelY, 1, 1, color));
    }
    void PKSEFramebuffer::drawRect(int rectX, int rectY, int rectWidth, int rectHeight, Color color)
    {
        record(makeShape("rect", rectX, rectY, rectWidth, rectHeight, color));
    }
    void PKSEFramebuffer::drawFilledRect(int rectX, int rectY, int rectWidth, int rectHeight, Color color)
    {
        record(makeShape("filledRect", rectX, rectY, rectWidth, rectHeight, color));
    }
    void PKSEFramebuffer::drawFilledEllipse(int centerX, int centerY, int radiusX, int radiusY, Color color)
    {
        record(makeShape("ellipse", centerX - radiusX, centerY - radiusY, radiusX * 2, radiusY * 2, color));
    }
    void PKSEFramebuffer::drawFilledRoundedRect(int rectX, int rectY, int rectWidth, int rectHeight, int, Color color)
    {
        record(makeShape("filledRoundedRect", rectX, rectY, rectWidth, rectHeight, color));
    }
    void PKSEFramebuffer::drawRoundedRect(int rectX, int rectY, int rectWidth, int rectHeight, int, Color color, int)
    {
        record(makeShape("roundedRect", rectX, rectY, rectWidth, rectHeight, color));
    }
    void PKSEFramebuffer::drawFilledCircle(int centerX, int centerY, int radius, Color color)
    {
        record(makeShape("filledCircle", centerX - radius, centerY - radius, radius * 2, radius * 2, color));
    }
    void PKSEFramebuffer::drawCircle(int centerX, int centerY, int radius, Color color, int)
    {
        record(makeShape("circle", centerX - radius, centerY - radius, radius * 2, radius * 2, color));
    }
    void PKSEFramebuffer::drawSoftShadow(int shadowX, int shadowY, int shadowWidth, int shadowHeight, int)
    {
        if (shadowWidth > 0 && shadowHeight > 0)
        {
            record(makeShape("softShadow", shadowX, shadowY, shadowWidth, shadowHeight, Color(0, 0, 0, 110)));
        }
    }
    void PKSEFramebuffer::drawVerticalGradient(int gradientX, int gradientY, int gradientWidth, int gradientHeight, Color top, Color)
    {
        if (gradientWidth > 0 && gradientHeight > 0)
        {
            record(makeShape("gradient", gradientX, gradientY, gradientWidth, gradientHeight, top));
        }
    }
    void PKSEFramebuffer::drawShinyMark(int markX, int markY, int markSize, Color color)
    {
        if (markSize > 1)
        {
            record(makeShape("shiny", markX, markY, markSize, markSize, color));
        }
    }
    void PKSEFramebuffer::drawPointerCursor(int tipX, int tipY, int headHeight, Color color)
    {
        record(makeShape("cursor", tipX - headHeight / 2, tipY - headHeight, headHeight, headHeight, color));
    }
    // Recorded by KIND, so a test can tell a folder row from a file row -- which is the whole
    // reason the browser draws two different icons.
    void PKSEFramebuffer::drawFolderIcon(int iconX, int iconY, int iconSize, Color color, bool open)
    {
        if (iconSize > 3)
        {
            record(makeShape(open ? "folder-open" : "folder", iconX, iconY, iconSize, iconSize, color));
        }
    }
    void PKSEFramebuffer::drawFileIcon(int iconX, int iconY, int iconSize, Color color)
    {
        if (iconSize > 3)
        {
            record(makeShape("file", iconX, iconY, iconSize, iconSize, color));
        }
    }
    void PKSEFramebuffer::drawSearchIcon(int iconX, int iconY, int iconSize, Color color)
    {
        if (iconSize > 3)
        {
            record(makeShape("search", iconX, iconY, iconSize, iconSize, color));
        }
    }
    void PKSEFramebuffer::drawEvolveArrow(int iconX, int iconY, int iconSize, Color color)
    {
        if (iconSize > 3)
        {
            record(makeShape("evolve-arrow", iconX, iconY, iconSize, iconSize, color));
        }
    }
    void PKSEFramebuffer::drawBackspaceIcon(int iconX, int iconY, int iconSize, Color color)
    {
        if (iconSize > 3)
        {
            record(makeShape("backspace", iconX, iconY, iconSize, iconSize, color));
        }
    }
    // Recorded by STATE, so a test can see shift turn on without reading the key's fill colour.
    void PKSEFramebuffer::drawShiftIcon(int iconX, int iconY, int iconSize, Color color, bool filled)
    {
        if (iconSize > 3)
        {
            record(makeShape(filled ? "shift-on" : "shift", iconX, iconY, iconSize, iconSize, color));
        }
    }

    void PKSEFramebuffer::drawStatHexagon(int centerX, int centerY, int hexagonRadius, const float *, int, float, Color fillColor, Color, Color)
    {
        record(makeShape("hexagon", centerX - hexagonRadius, centerY - hexagonRadius, hexagonRadius * 2, hexagonRadius * 2, fillColor));
    }

    // These delegate to other PUBLIC primitives in src/UI/PKSEFramebuffer.cpp, and are mirrored
    // exactly here rather than recorded as a single op. A card really is a filled rounded rect plus
    // an outline; recording it as one "card" would make the primitive stream the harness sees
    // differ from the one the console produces, and any test that counted primitives would then be
    // testing the harness rather than the UI.
    void PKSEFramebuffer::drawCard(int cardX, int cardY, int cardWidth, int cardHeight)
    {
        drawFilledRoundedRect(cardX, cardY, cardWidth, cardHeight, 14, Colors::Panel);
        drawRoundedRect(cardX, cardY, cardWidth, cardHeight, 14, Colors::Border, 1);
    }

    void PKSEFramebuffer::drawSelectionHighlight(int highlightX, int highlightY, int highlightWidth, int highlightHeight)
    {
        drawFilledRoundedRect(highlightX, highlightY, highlightWidth, highlightHeight, 10, Colors::Selected);
        drawRoundedRect(highlightX, highlightY, highlightWidth, highlightHeight, 10, Colors::Accent, 2);
    }

    void PKSEFramebuffer::drawHDivider(int dividerX, int dividerY, int dividerWidth)
    {
        drawFilledRect(dividerX, dividerY, dividerWidth, 2, Colors::Border);
    }

    void PKSEFramebuffer::drawPill(int pillX, int pillY, int pillWidth, int pillHeight, Color color)
    {
        drawFilledRoundedRect(pillX, pillY, pillWidth, pillHeight, pillHeight / 2, color);
    }
    void PKSEFramebuffer::drawPillBorder(int pillX, int pillY, int pillWidth, int pillHeight, Color color, int thickness)
    {
        drawRoundedRect(pillX, pillY, pillWidth, pillHeight, pillHeight / 2, color, thickness);
    }

    void PKSEFramebuffer::drawEgg(int centerX, int centerY, int eggSize)
    {
        const int radiusX = std::max(4, static_cast<int>(eggSize * 0.30f));
        const int radiusY = std::max(5, static_cast<int>(eggSize * 0.40f));
        drawFilledEllipse(centerX, centerY, radiusX + 2, radiusY + 2, Color(206, 196, 168, 255));
        drawFilledEllipse(centerX, centerY, radiusX, radiusY, Color(238, 232, 210, 255));
        const Color spotColor(120, 198, 176, 255);
        drawFilledCircle(centerX - radiusX / 3, centerY - radiusY / 3, std::max(2, eggSize / 13), spotColor);
        drawFilledCircle(centerX + radiusX / 3, centerY, std::max(2, eggSize / 16), spotColor);
        drawFilledCircle(centerX - radiusX / 5, centerY + radiusY / 3, std::max(2, eggSize / 18), spotColor);
    }

    int PKSEFramebuffer::drawTypeBadge(int badgeX, int badgeY, const std::string &typeName, Color typeColor)
    {
        const int badgeHeight = 24;
        int labelWidth = 0;
        int labelHeight = 0;
        measureText(typeName, labelWidth, labelHeight, TextStyle::Caption);
        const int labelPaddingX = 12;
        const int badgeWidth = labelWidth + labelPaddingX * 2;
        drawFilledRoundedRect(badgeX, badgeY, badgeWidth, badgeHeight, badgeHeight / 2, typeColor);
        const int luminance = (typeColor.red * 299 + typeColor.green * 587 + typeColor.blue * 114) / 1000;
        const Color labelColor = luminance > 150 ? Color(30, 30, 36) : Color(245, 245, 250);
        drawText(badgeX + labelPaddingX, badgeY + (badgeHeight - labelHeight) / 2, typeName, labelColor, TextStyle::Caption);
        return badgeWidth;
    }

    void PKSEFramebuffer::drawText(int textX, int textY, const char *text, Color color, TextStyle style)
    {
        if (!text || !*text)
        {
            return;
        }
        drawText(textX, textY, std::string(text), color, style);
    }

    void PKSEFramebuffer::drawText(int textX, int textY, const std::string &text, Color color, TextStyle style)
    {
        if (text.empty())
        {
            return;                       // matches the real backend's early-out
        }
        int textWidth = 0;
        int textHeight = 0;
        measureText(text, textWidth, textHeight, style);
        UIHost::Draw draw;
        draw.kind = UIHost::Kind::Text;
        draw.primitive = "text";
        draw.drawX = textX;
        draw.drawY = textY;
        draw.drawWidth = textWidth;
        draw.drawHeight = textHeight;
        draw.text = text;
        draw.color = color;
        draw.style = style;
        record(std::move(draw));
    }

    void PKSEFramebuffer::drawSymbol(int symbolX, int symbolY, const std::string &symbol, Color color, TextStyle style)
    {
        if (symbol.empty())
        {
            return;
        }
        int symbolWidth = 0;
        int symbolHeight = 0;
        measureText(symbol, symbolWidth, symbolHeight, style);
        UIHost::Draw draw;
        draw.kind = UIHost::Kind::Text;
        draw.primitive = "symbol";
        draw.drawX = symbolX;
        draw.drawY = symbolY;
        draw.drawWidth = symbolWidth;
        draw.drawHeight = symbolHeight;
        draw.text = symbol;
        draw.color = color;
        draw.style = style;
        record(std::move(draw));
    }

    void PKSEFramebuffer::drawImage(int imageX, int imageY, int sourceWidth, int sourceHeight, const unsigned char *imageData, int)
    {
        if (!imageData)
        {
            return;
        }
        UIHost::Draw draw = makeShape("image", imageX, imageY, sourceWidth, sourceHeight, Colors::White);
        draw.kind = UIHost::Kind::Sprite;
        record(std::move(draw));
    }

    void PKSEFramebuffer::drawImageScaled(int imageX, int imageY, int, int, int destinationWidth, int destinationHeight, const unsigned char *imageData, int)
    {
        if (!imageData)
        {
            return;
        }
        UIHost::Draw draw = makeShape("imageScaled", imageX, imageY, destinationWidth, destinationHeight, Colors::White);
        draw.kind = UIHost::Kind::Sprite;
        record(std::move(draw));
    }

    void PKSEFramebuffer::drawSpriteIdle(int spriteX, int spriteY, int boxWidth, int boxHeight, int, int, const unsigned char *spriteData, int, float)
    {
        if (!spriteData)
        {
            return;
        }
        UIHost::Draw draw = makeShape("spriteIdle", spriteX, spriteY, boxWidth, boxHeight, Colors::White);
        draw.kind = UIHost::Kind::Sprite;
        record(std::move(draw));
    }

    void PKSEFramebuffer::setClipRect(int clipX, int clipY, int clipWidth, int clipHeight)
    {
        UIHost::Recorder::get().setClip(clipX, clipY, clipWidth, clipHeight);
    }
    void PKSEFramebuffer::clearClip()
    {
        UIHost::Recorder::get().clearClip();
    }

    void PKSEFramebuffer::measureText(const std::string &text, int &outWidth, int &outHeight, TextStyle style)
    {
        const float fontSize = FONT_SIZES[static_cast<int>(style)];
        outWidth = static_cast<int>(std::lround(fontSize * AVERAGE_GLYPH_ADVANCE * static_cast<float>(text.size())));
        outHeight = lineHeight(style);
    }

    // Mirrors the real query from Nunito's own metrics (hhea ascent 1011, descent -353; cap height
    // 705 per 1000 em), so text placed with it lands where the console puts it rather than where a
    // line-box guess would.
    int PKSEFramebuffer::textYCenteredOn(int centerY, TextStyle style) const
    {
        const float fontSize = FONT_SIZES[static_cast<int>(style)];
        const float ascender = fontSize * 1011.0f / (1011.0f + 353.0f);
        return static_cast<int>(std::lround(static_cast<float>(centerY) - ascender + fontSize * 0.705f / 2.0f));
    }

    int PKSEFramebuffer::lineHeight(TextStyle style) const
    {
        return static_cast<int>(std::lround(FONT_SIZES[static_cast<int>(style)] * 1.35f));
    }

    // Timing is DETERMINISTIC here, not wall-clock: a test that pressed the same buttons must record
    // the same frame every run, and an idle animation driven by a real clock would make sprite
    // positions differ between runs on the same input. The driver advances this per frame.
    void PKSEFramebuffer::startFade()
    {
        fadeStart = -100.0;
    }   // fades are skipped entirely
    void PKSEFramebuffer::drawFadeOverlay() {}

    void PKSEFramebuffer::invalidateImage(const unsigned char *) {}
    void PKSEFramebuffer::onSpriteEvicted(const unsigned char *) {}
}
