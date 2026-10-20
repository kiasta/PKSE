#include "UI/PKSEFramebuffer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <switch.h> // pl: the console's shared CJK fonts, loaded as NanoVG fallbacks below
#include <glad/glad.h>
#include <SDL2/SDL.h>

#include "nanovg.h"
#define NANOVG_GL3
#include "nanovg_gl.h"

#include "UI/SpriteManager.h" // eviction hook: textures are keyed on sprite buffer addresses
#include "Utils/Logger.h"

using namespace Utils;

namespace UI
{

    static constexpr const char *UI_FONT_PATH = "romfs:/fonts/Nunito.ttf";
    static constexpr const char *SYMBOLS_FONT_PATH = "romfs:/fonts/NotoSansSymbols.ttf";
    static constexpr const char *SYMBOLS2_FONT_PATH = "romfs:/fonts/NotoSansSymbols2.ttf";

    // Nunito point size per TextStyle.
    static constexpr float FONT_SIZES[static_cast<int>(TextStyle::Count)] = {
        15.0f, // Caption
        19.0f, // Body
        26.0f, // Heading
        34.0f, // Title
    };

    /// Nunito's capital height as a fraction of its em -- OS/2 sCapHeight 705 over unitsPerEm 1000,
    /// read from romfs/fonts/Nunito.ttf. A property of the bundled font file, not a tuning value.
    static constexpr float NUNITO_CAP_HEIGHT_PER_EM = 0.705f;

    static inline NVGcolor toNVG(Color color) { return nvgRGBA(color.red, color.green, color.blue, color.alpha); }

    // The framebuffer SpriteManager's eviction callback should reach. Only one exists at a time
    // (UIManager owns it), and it is cleared on destruction so a late eviction is a no-op.
    static PKSEFramebuffer *s_activeFramebuffer = nullptr;

    PKSEFramebuffer::PKSEFramebuffer() : window(nullptr), glContext(nullptr), vg(nullptr), fontSans(-1), fontSym(-1), fontSym2(-1), width(1280), height(720)
    {

        // Request an OpenGL 4.3 core context with a stencil buffer — NanoVG needs stencil for its
        // fills / stencil strokes. This is the proven Switch (mesa/nouveau) config.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        window = SDL_CreateWindow("PKSE", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_OPENGL);
        if (!window)
        {
            logErrorToFile("SDL_CreateWindow(OPENGL) failed");
            logErrorToFile(SDL_GetError());
            return;
        }

        glContext = SDL_GL_CreateContext(window);
        if (!glContext)
        {
            logErrorToFile("SDL_GL_CreateContext failed");
            logErrorToFile(SDL_GetError());
            return;
        }
        SDL_GL_MakeCurrent(window, glContext);
        SDL_GL_SetSwapInterval(1); // vsync

        if (!gladLoadGL())
        {
            logErrorToFile("gladLoadGL failed");
            return;
        }
        glGetError(); // clear any benign startup GL error

        const char *glver = reinterpret_cast<const char *>(glGetString(GL_VERSION));
        // should be "OpenGL 4.x", not "OpenGL ES"
        if (glver) logInfoToFile("GL_VERSION:", glver);

        vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
        if (!vg)
        {
            logErrorToFile("nvgCreateGL3 failed");
            return;
        }

        // Fonts from romfs (fontstash reads via fopen; romfs is mounted). Add the two symbol fonts
        // as fallbacks to Nunito so nvgText() automatically covers glyphs Nunito lacks (♂ ♀ ★ ♥ ◀ ▶).
        fontSans = nvgCreateFont(vg, "sans", UI_FONT_PATH);
        if (fontSans < 0)
            logErrorToFile("nvgCreateFont Nunito failed");
        fontSym = nvgCreateFont(vg, "sym", SYMBOLS_FONT_PATH);
        fontSym2 = nvgCreateFont(vg, "sym2", SYMBOLS2_FONT_PATH);
        if (fontSans >= 0 && fontSym >= 0)
            nvgAddFallbackFontId(vg, fontSans, fontSym);
        if (fontSans >= 0 && fontSym2 >= 0)
            nvgAddFallbackFontId(vg, fontSans, fontSym2);

        /**
         * Then the console's own fonts, so Japanese, Korean and Chinese render at all.
         *
         * Nunito and the two Noto Symbols faces carry Latin-1 completely -- accented French,
         * German, Italian and Spanish names have always drawn correctly -- and between them not one
         * CJK glyph. A Japanese Pokemon's nickname was decoded perfectly by the save layer and then
         * drew as nothing, which is what "non-English names do not render" actually meant.
         *
         * These come from `pl` shared memory rather than romfs because they are THE SAME FACES the
         * console and the games render Pokemon names with -- a Japanese nickname then looks in
         * PKSE exactly as it does in the game it came from, which a separately-sourced font cannot
         * promise -- and because there is no fetch step to go stale. `freeData = 0` because the
         * buffers belong to the service, not to fontstash; main.cpp holds `pl` open for the life
         * of the process for exactly that reason. Size is not the argument: PKSE runs under title
         * override, so the .nro's size is not a constraint either way.
         *
         * ORDER IS THE PRIORITY ORDER. fontstash walks the fallback chain until a face has the
         * glyph, so Nunito keeps every Latin character it already drew and these only answer for
         * what it lacks. Standard comes first because it covers kana and the common kanji; the
         * Chinese faces follow for Han the Japanese font does not have, then Hangul, then
         * Nintendo's private-use glyphs.
         */
        static constexpr struct { PlSharedFontType type; const char *name; } SHARED_FONTS[] = {
            {PlSharedFontType_Standard, "nxstd"},
            {PlSharedFontType_ChineseSimplified, "nxscn"},
            {PlSharedFontType_ExtChineseSimplified, "nxscnext"},
            {PlSharedFontType_ChineseTraditional, "nxtcn"},
            {PlSharedFontType_KO, "nxkor"},
            {PlSharedFontType_NintendoExt, "nxext"},
        };
        for (const auto &sharedFont : SHARED_FONTS)
        {
            PlFontData fontData;
            if (R_FAILED(plGetSharedFontByType(&fontData, sharedFont.type)))
                continue;
            const int fontId = nvgCreateFontMem(vg, sharedFont.name, static_cast<unsigned char *>(fontData.address), static_cast<int>(fontData.size), /*freeData=*/0);
            if (fontId >= 0 && fontSans >= 0)
                nvgAddFallbackFontId(vg, fontSans, fontId);
        }

        s_activeFramebuffer = this;
        SpriteManager::setEvictCallback(&PKSEFramebuffer::onSpriteEvicted);
    }

    void PKSEFramebuffer::onSpriteEvicted(const unsigned char *data)
    {
        if (s_activeFramebuffer)
            s_activeFramebuffer->invalidateImage(data);
    }

    void PKSEFramebuffer::invalidateImage(const unsigned char *data)
    {
        if (!vg || !data)
            return;
        auto iterator = imageCache.find(data);
        if (iterator == imageCache.end())
            return;
        if (iterator->second >= 0)
            nvgDeleteImage(vg, iterator->second);
        imageCache.erase(iterator);
    }

    PKSEFramebuffer::~PKSEFramebuffer()
    {
        // Stop eviction reaching a half-destroyed object; the loop below frees every texture anyway.
        SpriteManager::setEvictCallback(nullptr);
        if (s_activeFramebuffer == this)
            s_activeFramebuffer = nullptr;
        if (vg)
        {
            for (auto &kv : imageCache)
                if (kv.second >= 0)
                    nvgDeleteImage(vg, kv.second);
            imageCache.clear();
            nvgDeleteGL3(vg);
        }
        if (glContext)
            SDL_GL_DeleteContext(glContext);
        if (window)
            SDL_DestroyWindow(window);
    }

    // Begin a NanoVG frame if one isn't active yet (safety for draws before clear()).
    // Returns true if vg is valid and a frame is ready.
    bool PKSEFramebuffer::ensureFrame()
    {
        if (!vg)
            return false;
        if (!frameActive)
        {
            nvgBeginFrame(vg, static_cast<float>(width), static_cast<float>(height), 1.0f);
            frameActive = true;
        }
        return true;
    }

    void PKSEFramebuffer::clear(Color color)
    {
        if (!vg)
            return;
        if (frameActive)
        {
            nvgEndFrame(vg);
            frameActive = false;
        } // close a stray previous frame
        glViewport(0, 0, width, height);
        glClearColor(color.red / 255.0f, color.green / 255.0f, color.blue / 255.0f, color.alpha / 255.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        nvgBeginFrame(vg, static_cast<float>(width), static_cast<float>(height), 1.0f);
        frameActive = true;
    }

    void PKSEFramebuffer::flush()
    {
        if (vg && frameActive)
        {
            nvgEndFrame(vg);
            frameActive = false;
        }
        if (window)
            SDL_GL_SwapWindow(window);

        uint64_t nowSeconds = SDL_GetPerformanceCounter();
        if (lastCounter != 0)
        {
            double freq = static_cast<double>(SDL_GetPerformanceFrequency());
            deltaSeconds = static_cast<float>(static_cast<double>(nowSeconds - lastCounter) / freq);
            // guard against huge dt after a pause
            if (deltaSeconds > 0.1f) deltaSeconds = 0.1f;
        }
        lastCounter = nowSeconds;
        totalSeconds += deltaSeconds;
    }

    void PKSEFramebuffer::drawPixel(int pixelX, int pixelY, Color color)
    {
        if (!ensureFrame())
            return;
        nvgBeginPath(vg);
        nvgRect(vg, (float)pixelX, (float)pixelY, 1.0f, 1.0f);
        nvgFillColor(vg, toNVG(color));
        nvgFill(vg);
    }

    void PKSEFramebuffer::drawFilledRect(int rectX, int rectY, int rectWidth, int rectHeight, Color color)
    {
        if (!ensureFrame())
            return;
        nvgBeginPath(vg);
        nvgRect(vg, (float)rectX, (float)rectY, (float)rectWidth, (float)rectHeight);
        nvgFillColor(vg, toNVG(color));
        nvgFill(vg);
    }

    // Clip subsequent drawing to a rect (nvgScissor) so a scrolled region's rows don't spill over
    // neighbouring chrome. clearClip() lifts the restriction; both are no-ops without an active frame.
    void PKSEFramebuffer::setClipRect(int clipX, int clipY, int clipWidth, int clipHeight)
    {
        if (!ensureFrame())
            return;
        nvgScissor(vg, (float)clipX, (float)clipY, (float)clipWidth, (float)clipHeight);
    }
    void PKSEFramebuffer::clearClip()
    {
        if (!ensureFrame())
            return;
        nvgResetScissor(vg);
    }

    void PKSEFramebuffer::drawRect(int rectX, int rectY, int rectWidth, int rectHeight, Color color)
    {
        if (!ensureFrame())
            return;
        nvgBeginPath(vg);
        nvgRect(vg, rectX + 0.5f, rectY + 0.5f, rectWidth - 1.0f, rectHeight - 1.0f);
        nvgStrokeColor(vg, toNVG(color));
        nvgStrokeWidth(vg, 1.0f);
        nvgStroke(vg);
    }

    void PKSEFramebuffer::drawFilledRoundedRect(int rectX, int rectY, int rectWidth, int rectHeight, int cornerRadius, Color color)
    {
        if (rectWidth <= 0 || rectHeight <= 0 || !ensureFrame())
            return;
        int rmax = std::min(rectWidth, rectHeight) / 2;
        if (cornerRadius > rmax)
            cornerRadius = rmax;
        if (cornerRadius < 0)
            cornerRadius = 0;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, (float)rectX, (float)rectY, (float)rectWidth, (float)rectHeight, (float)cornerRadius);
        nvgFillColor(vg, toNVG(color));
        nvgFill(vg);
    }

    void PKSEFramebuffer::drawRoundedRect(int rectX, int rectY, int rectWidth, int rectHeight, int cornerRadius, Color color, int thickness)
    {
        if (rectWidth <= 0 || rectHeight <= 0 || thickness <= 0 || !ensureFrame())
            return;
        int rmax = std::min(rectWidth, rectHeight) / 2;
        if (cornerRadius > rmax)
            cornerRadius = rmax;
        if (cornerRadius < 0)
            cornerRadius = 0;
        float strokeWidth = (float)thickness;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, rectX + strokeWidth * 0.5f, rectY + strokeWidth * 0.5f, rectWidth - strokeWidth, rectHeight - strokeWidth, std::max(0.0f, cornerRadius - strokeWidth * 0.5f));
        nvgStrokeColor(vg, toNVG(color));
        nvgStrokeWidth(vg, strokeWidth);
        nvgStroke(vg);
    }

    void PKSEFramebuffer::drawFilledCircle(int centerX, int centerY, int radius, Color color)
    {
        if (radius <= 0 || !ensureFrame())
            return;
        nvgBeginPath(vg);
        nvgCircle(vg, (float)centerX, (float)centerY, (float)radius);
        nvgFillColor(vg, toNVG(color));
        nvgFill(vg);
    }

    void PKSEFramebuffer::drawCircle(int centerX, int centerY, int radius, Color color, int thickness)
    {
        if (radius <= 0 || !ensureFrame())
            return;
        float strokeWidth = (float)thickness;
        nvgBeginPath(vg);
        nvgCircle(vg, (float)centerX, (float)centerY, radius - strokeWidth * 0.5f);
        nvgStrokeColor(vg, toNVG(color));
        nvgStrokeWidth(vg, strokeWidth);
        nvgStroke(vg);
    }

    void PKSEFramebuffer::drawEgg(int centerX, int centerY, int size)
    {
        // Classic Pokemon egg: a cream oval (taller than wide) with a few teal spots.
        const int shellRadiusX = std::max(4, static_cast<int>(size * 0.30f));
        const int rectY = std::max(5, static_cast<int>(size * 0.40f));
        drawFilledEllipse(centerX, centerY, shellRadiusX + 2, rectY + 2, Color(206, 196, 168, 255)); // subtle edge ring
        drawFilledEllipse(centerX, centerY, shellRadiusX, rectY, Color(238, 232, 210, 255));         // cream shell
        const Color spot(120, 198, 176, 255);                                 // teal spots
        drawFilledCircle(centerX - shellRadiusX / 3, centerY - rectY / 3, std::max(2, size / 13), spot);
        drawFilledCircle(centerX + shellRadiusX / 3, centerY, std::max(2, size / 16), spot);
        drawFilledCircle(centerX - shellRadiusX / 5, centerY + rectY / 3, std::max(2, size / 18), spot);
    }

    void PKSEFramebuffer::drawShinyMark(int markX, int markY, int size, Color color)
    {
        // HOME's shiny mark: a four-pointed sparkle. The four tips sit on the cardinal directions; the
        // edges between them curve INWARD (a quad curve pulled toward a control point near the center), so
        // it reads as a pinched "twinkle" rather than a plain star. Anchored top-left in a size×size box,
        // like every other drawn icon.
        if (size <= 1 || !ensureFrame())
            return;
        const float tipRadius = size * 0.5f;      // tip radius (half the box)
        const float controlRadius = tipRadius * 0.34f;        // control radius -> depth of the concave pinch
        const float diagonalControl = controlRadius * 0.70710678f; // c * cos45, the diagonal control offset
        const float centerX = markX + tipRadius, cy = markY + tipRadius;
        nvgBeginPath(vg);
        nvgMoveTo(vg, centerX, cy - tipRadius);                   // top tip
        nvgQuadTo(vg, centerX + diagonalControl, cy - diagonalControl, centerX + tipRadius, cy); // -> right tip
        nvgQuadTo(vg, centerX + diagonalControl, cy + diagonalControl, centerX, cy + tipRadius); // -> bottom tip
        nvgQuadTo(vg, centerX - diagonalControl, cy + diagonalControl, centerX - tipRadius, cy); // -> left tip
        nvgQuadTo(vg, centerX - diagonalControl, cy - diagonalControl, centerX, cy - tipRadius); // -> back to top tip
        nvgClosePath(vg);
        nvgFillColor(vg, toNVG(color));
        nvgFill(vg);
    }

    void PKSEFramebuffer::drawFolderIcon(int iconX, int iconY, int size, Color color, bool open)
    {
        // A manila folder: a back panel carrying the tab, with the front panel over it. Two shapes
        // rather than one outline, because the seam between them is what makes it read as a folder
        // rather than as a rounded rectangle with a bite taken out.
        //
        // Proportions are a 16x16 art box scaled to `size`. The folder is WIDER than it is tall
        // (real ones are), so it is centred vertically in the square box the caller reserved --
        // that is what keeps it on the same baseline as the file icon beside it.
        if (size <= 3 || !ensureFrame())
            return;
        const float unitScale = size / 16.0f;
        const float iconWidth = 16.0f * unitScale;
        const float iconHeight = 13.0f * unitScale;               // shorter than the box
        const float top = iconY + (size - iconHeight) * 0.5f; // centred, so both icons share a baseline
        const float cornerRadius = std::max(1.0f, 1.6f * unitScale);
        const float tabW = 6.5f * unitScale; // the tab covers a little under half the width
        const float tabH = 2.6f * unitScale;
        const float bodyTop = top + tabH;

        auto shade = [&](float floatValue) { // f < 1 darkens, f > 1 lightens toward white
            auto shadeChannel = [&](int value)
            {
                const float offset = (floatValue <= 1.0f) ? value * floatValue : value + (255.0f - value) * (floatValue - 1.0f);
                return (unsigned char)std::min(255.0f, std::max(0.0f, offset));
            };
            return nvgRGBA(shadeChannel(color.red), shadeChannel(color.green), shadeChannel(color.blue), color.alpha);
        };

        // Back panel + tab, as one path: up the left edge, across the tab, down its right shoulder,
        // then along the back's top edge to the right corner.
        nvgBeginPath(vg);
        nvgMoveTo(vg, iconX, top + iconHeight);
        nvgLineTo(vg, iconX, top + cornerRadius);
        nvgQuadTo(vg, iconX, top, iconX + cornerRadius, top); // top-left corner
        nvgLineTo(vg, iconX + tabW - cornerRadius, top);
        nvgQuadTo(vg, iconX + tabW, top, iconX + tabW + cornerRadius, bodyTop); // the tab's sloped shoulder
        nvgLineTo(vg, iconX + iconWidth - cornerRadius, bodyTop);
        nvgQuadTo(vg, iconX + iconWidth, bodyTop, iconX + iconWidth, bodyTop + cornerRadius); // top-right corner
        nvgLineTo(vg, iconX + iconWidth, top + iconHeight);
        nvgClosePath(vg);
        nvgFillColor(vg, shade(0.72f)); // the back sits in shadow
        nvgFill(vg);

        // Front panel. Closed: square to the back, leaving the tab and a sliver of back showing.
        //
        // Open: a TRAPEZOID, splayed wider at the top and shifted right, which is how an open
        // folder is drawn everywhere. A rectangle merely nudged sideways was tried first and is
        // not legible at a 24px row -- at that size the shift is two pixels and reads as a
        // rendering slip rather than as a different icon.
        const float frontTop = bodyTop + 1.2f * unitScale;
        const float splayT = open ? 3.0f * unitScale : 0.0f; // top edge, shifted right
        const float splayB = open ? 1.4f * unitScale : 0.0f; // bottom edge, pulled in on both sides
        nvgBeginPath(vg);
        nvgMoveTo(vg, iconX + splayT, frontTop + cornerRadius);
        nvgQuadTo(vg, iconX + splayT, frontTop, iconX + splayT + cornerRadius, frontTop);
        nvgLineTo(vg, iconX + iconWidth - cornerRadius, frontTop);
        nvgQuadTo(vg, iconX + iconWidth, frontTop, iconX + iconWidth, frontTop + cornerRadius);
        nvgLineTo(vg, iconX + iconWidth - splayB, top + iconHeight - cornerRadius);
        nvgQuadTo(vg, iconX + iconWidth - splayB, top + iconHeight, iconX + iconWidth - splayB - cornerRadius, top + iconHeight);
        nvgLineTo(vg, iconX + splayB + cornerRadius, top + iconHeight);
        nvgQuadTo(vg, iconX + splayB, top + iconHeight, iconX + splayB, top + iconHeight - cornerRadius);
        nvgClosePath(vg);
        // Lit from the top: a flat fill at this size reads as a solid block.
        nvgFillPaint(vg, nvgLinearGradient(vg, iconX, frontTop, iconX, top + iconHeight, shade(1.18f), toNVG(color)));
        nvgFill(vg);
    }

    void PKSEFramebuffer::drawFileIcon(int iconX, int iconY, int size, Color color)
    {
        // A sheet of paper with the top-right corner turned down. The fold is the whole point --
        // without it a page is a rounded rectangle, which is also what a button looks like.
        if (size <= 3 || !ensureFrame())
            return;
        const float unitScale = size / 16.0f;
        const float iconWidth = 12.5f * unitScale; // portrait, and narrower than the folder
        const float iconHeight = 15.0f * unitScale;
        const float left = iconX + (size - iconWidth) * 0.5f; // centred in the box, like the folder
        const float top = iconY + (size - iconHeight) * 0.5f;
        const float cornerRadius = std::max(1.0f, 1.4f * unitScale);
        const float fold = 5.4f * unitScale;

        auto shade = [&](float floatValue)
        {
            auto shadeChannel = [&](int value)
            {
                const float offset = (floatValue <= 1.0f) ? value * floatValue : value + (255.0f - value) * (floatValue - 1.0f);
                return (unsigned char)std::min(255.0f, std::max(0.0f, offset));
            };
            return nvgRGBA(shadeChannel(color.red), shadeChannel(color.green), shadeChannel(color.blue), color.alpha);
        };

        // The page, with the top-right corner cut off along the fold line.
        nvgBeginPath(vg);
        nvgMoveTo(vg, left + cornerRadius, top);
        nvgLineTo(vg, left + iconWidth - fold, top);
        nvgLineTo(vg, left + iconWidth, top + fold); // the diagonal
        nvgLineTo(vg, left + iconWidth, top + iconHeight - cornerRadius);
        nvgQuadTo(vg, left + iconWidth, top + iconHeight, left + iconWidth - cornerRadius, top + iconHeight);
        nvgLineTo(vg, left + cornerRadius, top + iconHeight);
        nvgQuadTo(vg, left, top + iconHeight, left, top + iconHeight - cornerRadius);
        nvgLineTo(vg, left, top + cornerRadius);
        nvgQuadTo(vg, left, top, left + cornerRadius, top);
        nvgClosePath(vg);
        nvgFillPaint(vg, nvgLinearGradient(vg, left, top, left, top + iconHeight, shade(1.18f), toNVG(color)));
        nvgFill(vg);

        // The turned corner, darker so the fold reads as folded rather than as a notch.
        nvgBeginPath(vg);
        nvgMoveTo(vg, left + iconWidth - fold, top);
        nvgLineTo(vg, left + iconWidth, top + fold);
        nvgLineTo(vg, left + iconWidth - fold, top + fold);
        nvgClosePath(vg);
        nvgFillColor(vg, shade(0.58f));
        nvgFill(vg);
    }

    void PKSEFramebuffer::drawEvolveArrow(int iconX, int iconY, int size, Color color)
    {
        // A 16x16 art box scaled to `size`, with the extents symmetric in it: the head spans
        // x 2.5..13.5 about the centreline at 8, and the whole arrow runs y 2..14. One filled path
        // rather than a triangle plus a rectangle -- two primitives meeting at an edge leave a seam
        // the antialiasing shows as a lighter line straight across the arrow.
        if (size <= 3 || !ensureFrame())
            return;
        const float unitScale = size / 16.0f;
        const float centerX = iconX + 8.0f * unitScale;
        const float headBaseY = iconY + 8.5f * unitScale;
        const float halfShaft = 2.6f * unitScale;

        nvgBeginPath(vg);
        nvgMoveTo(vg, centerX, iconY + 2.0f * unitScale);            // apex
        nvgLineTo(vg, iconX + 13.5f * unitScale, headBaseY);         // head, right
        nvgLineTo(vg, centerX + halfShaft, headBaseY);               // step in to the shaft
        nvgLineTo(vg, centerX + halfShaft, iconY + 14.0f * unitScale);
        nvgLineTo(vg, centerX - halfShaft, iconY + 14.0f * unitScale);
        nvgLineTo(vg, centerX - halfShaft, headBaseY);
        nvgLineTo(vg, iconX + 2.5f * unitScale, headBaseY);          // head, left
        nvgClosePath(vg);
        nvgFillColor(vg, nvgRGBA(color.red, color.green, color.blue, color.alpha));
        nvgFill(vg);
    }

    void PKSEFramebuffer::drawBackspaceIcon(int iconX, int iconY, int size, Color color)
    {
        // A 16x16 art box scaled to `size`. The tag spans x 1.2..14.8 and y 3.2..12.8, symmetric
        // about the box's centre; the cross sits in the middle of the tag's square body, not of the
        // box, or it crowds the point.
        if (size <= 3 || !ensureFrame())
            return;
        const float unitScale = size / 16.0f;
        const float strokeWidth = std::max(1.5f, 1.6f * unitScale);
        const float bodyLeft = iconX + 5.4f * unitScale;
        const float bodyRight = iconX + 14.8f * unitScale;
        const float bodyTop = iconY + 3.2f * unitScale;
        const float bodyBottom = iconY + 12.8f * unitScale;
        const float centerY = iconY + 8.0f * unitScale;

        nvgLineJoin(vg, NVG_ROUND);
        nvgBeginPath(vg);
        nvgMoveTo(vg, iconX + 1.2f * unitScale, centerY); // the point
        nvgLineTo(vg, bodyLeft, bodyTop);
        nvgLineTo(vg, bodyRight, bodyTop);
        nvgLineTo(vg, bodyRight, bodyBottom);
        nvgLineTo(vg, bodyLeft, bodyBottom);
        nvgClosePath(vg);
        nvgStrokeColor(vg, toNVG(color));
        nvgStrokeWidth(vg, strokeWidth);
        nvgStroke(vg);

        const float crossCenterX = (bodyLeft + bodyRight) * 0.5f;
        const float crossReach = 2.1f * unitScale;
        nvgLineCap(vg, NVG_ROUND);
        nvgBeginPath(vg);
        nvgMoveTo(vg, crossCenterX - crossReach, centerY - crossReach);
        nvgLineTo(vg, crossCenterX + crossReach, centerY + crossReach);
        nvgMoveTo(vg, crossCenterX + crossReach, centerY - crossReach);
        nvgLineTo(vg, crossCenterX - crossReach, centerY + crossReach);
        nvgStroke(vg);
        // Nothing in this file wraps its state in nvgSave/nvgRestore, so the next stroke drawn this
        // frame would inherit both.
        nvgLineCap(vg, NVG_BUTT);
        nvgLineJoin(vg, NVG_MITER);
    }

    void PKSEFramebuffer::drawShiftIcon(int iconX, int iconY, int size, Color color, bool filled)
    {
        // A 16x16 art box scaled to `size`: the arrow runs x 1.8..14.2 and y 1.8..14.2. One path, so
        // the outline has no seam where the head meets the shaft.
        if (size <= 3 || !ensureFrame())
            return;
        const float unitScale = size / 16.0f;
        const float centerX = iconX + 8.0f * unitScale;
        const float headBaseY = iconY + 8.4f * unitScale;
        const float halfShaft = 2.8f * unitScale;
        const float shaftBottomY = iconY + 14.2f * unitScale;

        nvgLineJoin(vg, NVG_ROUND);
        nvgBeginPath(vg);
        nvgMoveTo(vg, centerX, iconY + 1.8f * unitScale); // apex
        nvgLineTo(vg, iconX + 14.2f * unitScale, headBaseY);
        nvgLineTo(vg, centerX + halfShaft, headBaseY);
        nvgLineTo(vg, centerX + halfShaft, shaftBottomY);
        nvgLineTo(vg, centerX - halfShaft, shaftBottomY);
        nvgLineTo(vg, centerX - halfShaft, headBaseY);
        nvgLineTo(vg, iconX + 1.8f * unitScale, headBaseY);
        nvgClosePath(vg);
        if (filled)
        {
            nvgFillColor(vg, toNVG(color));
            nvgFill(vg);
        }
        // Stroked in both states: the fill alone would sit half a stroke inside the outline's
        // extents, and the arrow would visibly shrink when shift came on.
        nvgStrokeColor(vg, toNVG(color));
        nvgStrokeWidth(vg, std::max(1.5f, 1.6f * unitScale));
        nvgStroke(vg);
        nvgLineJoin(vg, NVG_MITER);
    }

    void PKSEFramebuffer::drawSearchIcon(int iconX, int iconY, int size, Color color)
    {
        // Proportions are a 16x16 art box scaled to `size`, chosen so the glyph's extents are
        // SYMMETRIC in it: the lens's outer edge and the handle's cap each land 0.8 units in from
        // their corner. That is what lets the caller centre the icon by centring its box, the same
        // contract the folder and file icons keep.
        if (size <= 3 || !ensureFrame())
            return;
        const float unitScale = size / 16.0f;
        const float rimWidth = std::max(1.5f, 2.0f * unitScale);
        const float handleWidth = std::max(1.6f, 2.4f * unitScale); // a grip is thicker than a rim
        const float lensRadius = 4.7f * unitScale;                  // the rim's centreline
        const float lensCenterX = iconX + 6.5f * unitScale;
        const float lensCenterY = iconY + 6.5f * unitScale;

        // Glass first, under the rim: a wash of the caller's own colour rather than a second
        // outline. Without it the lens is a hole, and a hole with a stick on it only reads as a
        // magnifier to someone who already knows that is what they are looking at.
        nvgBeginPath(vg);
        nvgCircle(vg, lensCenterX, lensCenterY, lensRadius);
        nvgFillColor(vg, nvgRGBA(color.red, color.green, color.blue, (unsigned char)(color.alpha * 0.16f)));
        nvgFill(vg);

        // The handle, running out along the diagonal from under the rim. Its start is placed so
        // the ROUND CAP's back edge lands exactly on the rim's inner edge: the handle is thicker
        // than the rim, so starting it any further in leaves a rounded nub intruding into the
        // glass, which at a glance reads as a chip out of the lens.
        constexpr float diagonal = 0.70710678f; // cos 45
        const float handleStart = lensRadius - rimWidth * 0.5f + handleWidth * 0.5f;
        nvgLineCap(vg, NVG_ROUND);
        nvgBeginPath(vg);
        nvgMoveTo(vg, lensCenterX + handleStart * diagonal, lensCenterY + handleStart * diagonal);
        nvgLineTo(vg, iconX + 14.0f * unitScale, iconY + 14.0f * unitScale);
        nvgStrokeColor(vg, toNVG(color));
        nvgStrokeWidth(vg, handleWidth);
        nvgStroke(vg);
        // Nothing in this file wraps its state in nvgSave/nvgRestore -- every routine sets what it
        // needs -- so the cap has to go back, or the next stroke drawn this frame inherits it.
        nvgLineCap(vg, NVG_BUTT);

        // The rim last, over the handle's inner end.
        nvgBeginPath(vg);
        nvgCircle(vg, lensCenterX, lensCenterY, lensRadius);
        nvgStrokeColor(vg, toNVG(color));
        nvgStrokeWidth(vg, rimWidth);
        nvgStroke(vg);
    }

    void PKSEFramebuffer::drawPointerCursor(int tipX, int tipY, int headHeight, Color color)
    {
        // The storage grid's cursor: a slim arrowhead, pointing straight down at the slot, with no
        // shaft. Four corners, in art units 18 wide by 26 tall with the point at (0, 0).
        //
        // This is drawn rather than copied. Tracing an existing 3DS-era pointer sprite was tried first
        // and abandoned: it is 19x28 pixels, so at the size PKSE needs it every wobble in
        // the original outline shows. The proportions below were instead chosen against a real 78px
        // slot disc at the sizes the app actually uses.
        //
        // Two of them matter more than they look:
        //   * The notch depth is tied to the width. Arm thickness is w*n/hypot(w,26), so a narrow
        //     head with a deep notch thins the arms until the outline is most of what is left and
        //     the colour barely shows -- hence the shallower -20 that goes with these narrow wings.
        //   * The corners are mitred. Rounding them swallows the notch and the point, and what
        //     survives reads as a circle.
        // The body is bevelled rather than flat-filled, which is what stops a solid silhouette from
        // reading as a flat cut-out.
        static const signed char arrowOutline[][2] = {
            {0, 0},    // the point
            {-9, -26}, // left wing
            {0, -20},  // the notch in the back edge
            {9, -26},  // right wing
        };
        constexpr int count = (int)(sizeof(arrowOutline) / sizeof(arrowOutline[0]));
        if (headHeight <= 6 || !ensureFrame())
            return;

        // The outline is drawn UNDER the body, not over it. Strokes are centred on the path, so at a
        // ~60-degree apex they cover the fill for several pixels and the point reads as a hollow V.
        // Stroking wide first and filling on top leaves the outline entirely outside the body, and
        // the body itself runs all the way into the point.
        //
        // `headHeight` is the head itself -- the part you actually see as the arrow. Below it the
        // mitred outline runs on to a point, overshooting the apex by outer/sin(halfAngle). That is
        // measured off the art rather than hardcoded, because it depends strongly on how slim the
        // head is (2.0x the outline thickness at the widest head tried, 3.1x at this one); pin it to
        // a constant and a reshape silently pokes the point lower than the caller reserved room for.
        float halfW = 1.0f, artH = 1.0f;
        for (int index = 0; index < count; ++index)
        {
            halfW = std::max(halfW, (float)std::abs(arrowOutline[index][0]));
            artH = std::max(artH, (float)-arrowOutline[index][1]);
        }
        // outer is 7% of the WHOLE cursor, and the whole cursor is head + over -- so solve for over
        // rather than measuring it off a total we do not have yet.
        const float floatValue = 0.07f * std::sqrt(halfW * halfW + artH * artH) / halfW;
        const float over = headHeight * floatValue / (1.0f - floatValue);
        const float outer = (headHeight + over) * 0.07f; // outline thickness outside the body
        const float artScale = headHeight / artH;
        const float baseY = tipY - over; // the body's apex; the outline's point is at tipY
        auto trace = [&](float offsetX, float offsetY)
        {
            nvgBeginPath(vg);
            for (int index = 0; index < count; ++index)
            {
                const float pointX = tipX + offsetX + arrowOutline[index][0] * artScale;
                const float pointY = baseY + offsetY + arrowOutline[index][1] * artScale;
                if (index == 0)
                    nvgMoveTo(vg, pointX, pointY);
                else
                    nvgLineTo(vg, pointX, pointY);
            }
            nvgClosePath(vg);
        };
        auto shade = [&](float floatValue) { // f < 1 darkens, f > 1 lightens toward white
            auto shadeChannel = [&](int value)
            {
                const float shadedChannel = (floatValue <= 1.0f) ? value * floatValue : value + (255.0f - value) * (floatValue - 1.0f);
                return (unsigned char)std::min(255.0f, std::max(0.0f, shadedChannel));
            };
            return nvgRGBA(shadeChannel(color.red), shadeChannel(color.green), shadeChannel(color.blue), color.alpha);
        };

        // Drop shadow: the whole outer silhouette (stroke + fill), offset.
        trace(headHeight * 0.04f, headHeight * 0.07f);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 105));
        nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 105));
        nvgStrokeWidth(vg, outer * 2.0f);
        nvgStroke(vg);
        nvgFill(vg);

        // Widest stroke first, then a narrower dark one over its inner half, then the body over
        // that: what is left outside the body is a dark rim with a white outline beyond it.
        trace(0.0f, 0.0f);
        nvgStrokeColor(vg, nvgRGBA(242, 242, 242, 240));
        nvgStrokeWidth(vg, outer * 2.0f);
        nvgStroke(vg);
        nvgStrokeColor(vg, shade(0.42f));
        nvgStrokeWidth(vg, outer * 0.9f);
        nvgStroke(vg);
        // Lit from the upper-left, falling off across the head.
        NVGpaint body = nvgLinearGradient(vg, tipX - halfW * artScale, baseY - artH * artScale, tipX + halfW * artScale, baseY, shade(1.5f), toNVG(color));
        nvgFillPaint(vg, body);
        nvgFill(vg);
    }

    void PKSEFramebuffer::drawPill(int pillX, int pillY, int pillWidth, int pillHeight, Color color)
    {
        drawFilledRoundedRect(pillX, pillY, pillWidth, pillHeight, pillHeight / 2, color);
    }
    void PKSEFramebuffer::drawPillBorder(int pillX, int pillY, int pillWidth, int pillHeight, Color color, int thickness)
    {
        drawRoundedRect(pillX, pillY, pillWidth, pillHeight, pillHeight / 2, color, thickness);
    }

    void PKSEFramebuffer::drawFilledEllipse(int centerX, int centerY, int radiusX, int rectY, Color color)
    {
        if (radiusX <= 0 || rectY <= 0 || !ensureFrame())
            return;
        nvgBeginPath(vg);
        nvgEllipse(vg, (float)centerX, (float)centerY, (float)radiusX, (float)rectY);
        nvgFillColor(vg, toNVG(color));
        nvgFill(vg);
    }

    void PKSEFramebuffer::drawSoftShadow(int shadowX, int shadowY, int shadowWidth, int shadowHeight, int cornerRadius)
    {
        if (shadowWidth <= 0 || shadowHeight <= 0 || !ensureFrame())
            return;
        // Real feathered drop shadow: box gradient in the ring around the element (hole in the middle).
        NVGpaint shadowPaint = nvgBoxGradient(vg, (float)shadowX, shadowY + 3.0f, (float)shadowWidth, (float)shadowHeight, cornerRadius + 2.0f, 12.0f, nvgRGBA(0, 0, 0, 110), nvgRGBA(0, 0, 0, 0));
        nvgBeginPath(vg);
        nvgRect(vg, shadowX - 14.0f, shadowY - 14.0f, shadowWidth + 28.0f, shadowHeight + 28.0f);
        nvgRoundedRect(vg, (float)shadowX, (float)shadowY, (float)shadowWidth, (float)shadowHeight, (float)cornerRadius);
        nvgPathWinding(vg, NVG_HOLE);
        nvgFillPaint(vg, shadowPaint);
        nvgFill(vg);
    }

    void PKSEFramebuffer::drawVerticalGradient(int gradientX, int gradientY, int gradientWidth, int gradientHeight, Color top, Color bottom)
    {
        if (gradientWidth <= 0 || gradientHeight <= 0 || !ensureFrame())
            return;
        NVGpaint gradientPaint = nvgLinearGradient(vg, (float)gradientX, (float)gradientY, (float)gradientX, (float)(gradientY + gradientHeight), toNVG(top), toNVG(bottom));
        nvgBeginPath(vg);
        nvgRect(vg, (float)gradientX, (float)gradientY, (float)gradientWidth, (float)gradientHeight);
        nvgFillPaint(vg, gradientPaint);
        nvgFill(vg);
    }

    void PKSEFramebuffer::drawStatHexagon(int centerX, int centerY, int hexRadius, const float *values, int count, float maxValue, Color fill, Color webColor, Color outline)
    {
        if (hexRadius <= 0 || count <= 0 || !values || !ensureFrame())
            return;
        static const double axisAngles[6] = {90.0, 30.0, -30.0, 270.0, 210.0, 150.0};
        const double piRadians = 3.14159265358979323846;
        int axisCount = count < 6 ? count : 6;
        auto axisX = [&](double angleDegrees, double ringRadius)
        { return centerX + (float)(std::cos(angleDegrees * piRadians / 180.0) * ringRadius); };
        auto axisY = [&](double angleDegrees, double ringRadius)
        { return centerY - (float)(std::sin(angleDegrees * piRadians / 180.0) * ringRadius); };

        // Web rings.
        for (int ring = 1; ring <= 4; ++ring)
        {
            double webRingRadius = hexRadius * (ring / 4.0);
            nvgBeginPath(vg);
            for (int index = 0; index < axisCount; ++index)
            {
                float panelX = axisX(axisAngles[index], webRingRadius), py = axisY(axisAngles[index], webRingRadius);
                if (index == 0)
                    nvgMoveTo(vg, panelX, py);
                else
                    nvgLineTo(vg, panelX, py);
            }
            nvgClosePath(vg);
            nvgStrokeColor(vg, toNVG(webColor));
            nvgStrokeWidth(vg, 1.0f);
            nvgStroke(vg);
        }
        // Spokes.
        nvgBeginPath(vg);
        for (int index = 0; index < axisCount; ++index)
        {
            nvgMoveTo(vg, (float)centerX, (float)centerY);
            nvgLineTo(vg, axisX(axisAngles[index], hexRadius), axisY(axisAngles[index], hexRadius));
        }
        nvgStrokeColor(vg, toNVG(webColor));
        nvgStrokeWidth(vg, 1.0f);
        nvgStroke(vg);
        // Data polygon.
        nvgBeginPath(vg);
        for (int index = 0; index < axisCount; ++index)
        {
            double value = maxValue > 0.0f ? values[index] / maxValue : 0.0;
            value = std::clamp(value, 0.06, 1.0);
            float panelX = axisX(axisAngles[index], hexRadius * value), py = axisY(axisAngles[index], hexRadius * value);
            if (index == 0)
                nvgMoveTo(vg, panelX, py);
            else
                nvgLineTo(vg, panelX, py);
        }
        nvgClosePath(vg);
        nvgFillColor(vg, toNVG(fill));
        nvgFill(vg);
        nvgStrokeColor(vg, toNVG(outline));
        nvgStrokeWidth(vg, 2.0f);
        nvgStroke(vg);
    }

    void PKSEFramebuffer::applyTextStyle(TextStyle style) const
    {
        nvgFontFaceId(vg, fontSans);
        nvgFontSize(vg, FONT_SIZES[static_cast<int>(style)]);
    }

    void PKSEFramebuffer::drawText(int textX, int textY, const char *text, Color color, TextStyle style)
    {
        if (!text || !*text)
            return;
        drawText(textX, textY, std::string(text), color, style);
    }

    void PKSEFramebuffer::drawText(int textX, int textY, const std::string &text, Color color, TextStyle style)
    {
        if (text.empty() || !ensureFrame())
            return;
        applyTextStyle(style);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgFillColor(vg, toNVG(color));
        nvgText(vg, (float)textX, (float)textY, text.c_str(), nullptr);
        // Faux-bold for the display styles (Nunito is loaded regular; re-stroke slightly offset).
        if (style == TextStyle::Heading || style == TextStyle::Title)
            nvgText(vg, textX + 0.6f, (float)textY, text.c_str(), nullptr);
    }

    void PKSEFramebuffer::drawSymbol(int symbolX, int symbolY, const std::string &symbol, Color color, TextStyle style)
    {
        // Symbol fonts are fallbacks on "sans", so nvgText resolves ♂/♀/★/♥/◀/▶ automatically.
        if (symbol.empty() || !ensureFrame())
            return;
        applyTextStyle(style);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgFillColor(vg, toNVG(color));
        nvgText(vg, (float)symbolX, (float)symbolY, symbol.c_str(), nullptr);
    }

    void PKSEFramebuffer::measureText(const std::string &text, int &outWidth, int &outHeight, TextStyle style)
    {
        outWidth = 0;
        outHeight = 0;
        if (!vg)
            return;
        applyTextStyle(style);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        float advanceWidth = nvgTextBounds(vg, 0, 0, text.c_str(), nullptr, nullptr);
        outWidth = (int)std::ceil(advanceWidth);
        outHeight = lineHeight(style);
    }

    int PKSEFramebuffer::textYCenteredOn(int centerY, TextStyle style) const
    {
        if (!vg)
            return centerY;
        applyTextStyle(style);
        // TWO NORMALISATIONS, AND BOTH MATTER. drawText aligns TOP, and fontstash places the baseline
        // `ascender` below that y, where ascender is the font's ascent over (ascent - descent) times
        // the size (fontstash.h: font->ascender). The glyphs themselves are scaled per EM instead
        // (stbtt_ScaleForMappingEmToPixels), so a capital stands size * capHeight/em above that
        // baseline. Measured on a console screenshot, this puts a 26px heading's capitals within half
        // a pixel of where it predicts; the hand-picked offsets it replaces left them 5px high.
        float ascender = 0, descender = 0, lineHeightPixels = 0;
        nvgTextMetrics(vg, &ascender, &descender, &lineHeightPixels);
        const float capitalHeight = FONT_SIZES[static_cast<int>(style)] * NUNITO_CAP_HEIGHT_PER_EM;
        return static_cast<int>(std::lround(static_cast<float>(centerY) - ascender + capitalHeight / 2.0f));
    }

    int PKSEFramebuffer::lineHeight(TextStyle style) const
    {
        if (!vg)
            return 0;
        applyTextStyle(style);
        float ascender = 0, desc = 0, lh = 0;
        nvgTextMetrics(vg, &ascender, &desc, &lh);
        return (int)std::ceil(lh);
    }

    int PKSEFramebuffer::nvgImageFor(const unsigned char *data, int imageWidth, int imageHeight, int channels)
    {
        if (!vg || !data || imageWidth <= 0 || imageHeight <= 0)
            return -1;
        auto iterator = imageCache.find(data);
        if (iterator != imageCache.end())
            return iterator->second;

        // Mipmaps so downscaled sprites (box grid / party / menu) stay crisp instead of shimmering.
        const int flags = NVG_IMAGE_GENERATE_MIPMAPS;
        int image = -1;
        if (channels == 4)
        {
            image = nvgCreateImageRGBA(vg, imageWidth, imageHeight, flags, data);
        }
        else if (channels == 3)
        {
            std::vector<unsigned char> rgba(static_cast<size_t>(imageWidth) * imageHeight * 4);
            for (int index = 0; index < imageWidth * imageHeight; ++index)
            {
                rgba[index * 4 + 0] = data[index * 3 + 0];
                rgba[index * 4 + 1] = data[index * 3 + 1];
                rgba[index * 4 + 2] = data[index * 3 + 2];
                rgba[index * 4 + 3] = 255;
            }
            image = nvgCreateImageRGBA(vg, imageWidth, imageHeight, flags, rgba.data());
        }
        else
        {
            return -1;
        }
        imageCache[data] = image;
        return image;
    }

    void PKSEFramebuffer::drawImage(int imageX, int imageY, int imageWidth, int imageHeight, const unsigned char *data, int channels)
    {
        if (!ensureFrame())
            return;
        int image = nvgImageFor(data, imageWidth, imageHeight, channels);
        if (image < 0)
            return;
        NVGpaint imagePaint = nvgImagePattern(vg, (float)imageX, (float)imageY, (float)imageWidth, (float)imageHeight, 0.0f, image, 1.0f);
        nvgBeginPath(vg);
        nvgRect(vg, (float)imageX, (float)imageY, (float)imageWidth, (float)imageHeight);
        nvgFillPaint(vg, imagePaint);
        nvgFill(vg);
    }

    void PKSEFramebuffer::drawImageScaled(int imageX, int imageY, int imageWidth, int imageHeight, int destWidth, int destHeight, const unsigned char *data, int channels)
    {
        if (!ensureFrame())
            return;
        int image = nvgImageFor(data, imageWidth, imageHeight, channels);
        if (image < 0)
            return;
        NVGpaint imagePaint = nvgImagePattern(vg, (float)imageX, (float)imageY, (float)destWidth, (float)destHeight, 0.0f, image, 1.0f);
        nvgBeginPath(vg);
        nvgRect(vg, (float)imageX, (float)imageY, (float)destWidth, (float)destHeight);
        nvgFillPaint(vg, imagePaint);
        nvgFill(vg);
    }

    void PKSEFramebuffer::drawSpriteIdle(int spriteX, int spriteY, int boxWidth, int boxHeight, int sourceWidth, int sourceHeight, const unsigned char *data, int channels, float phase)
    {
        if (!data || boxWidth <= 0 || boxHeight <= 0)
            return;
        double bobPhase = std::sin(totalSeconds * 2.7 + phase);
        int bobOffset = (int)std::lround(bobPhase * 3.0);
        int drawWidth = (int)std::lround(boxWidth * (1.0 - bobPhase * 0.03));
        int drawHeight = (int)std::lround(boxHeight * (1.0 + bobPhase * 0.03));
        int baseY = spriteY + boxHeight;
        int drawX = spriteX + (boxWidth - drawWidth) / 2;
        int drawY = baseY - drawHeight - bobOffset;
        int shadowRadiusX = (int)std::lround(boxWidth * 0.30 - bobPhase * 2.0);
        int shadowRadiusY = (int)std::lround(5.0 - bobPhase * 1.0);
        drawFilledEllipse(spriteX + boxWidth / 2, baseY - 1, shadowRadiusX, shadowRadiusY, Color(0, 0, 0, 80));
        drawImageScaled(drawX, drawY, sourceWidth, sourceHeight, drawWidth, drawHeight, data, channels);
    }

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

    int PKSEFramebuffer::drawTypeBadge(int badgeX, int badgeY, const std::string &typeName, Color typeColor)
    {
        const int badgeHeight = 24;
        int textWidth, th;
        measureText(typeName, textWidth, th, TextStyle::Caption);
        const int padX = 12, total = textWidth + padX * 2;
        drawFilledRoundedRect(badgeX, badgeY, total, badgeHeight, badgeHeight / 2, typeColor);
        int luminance = (typeColor.red * 299 + typeColor.green * 587 + typeColor.blue * 114) / 1000;
        Color labelColor = luminance > 150 ? Color(30, 30, 36) : Color(245, 245, 250);
        drawText(badgeX + padX, badgeY + (badgeHeight - th) / 2, typeName, labelColor, TextStyle::Caption);
        return total;
    }

    void PKSEFramebuffer::startFade() { fadeStart = totalSeconds; }

    void PKSEFramebuffer::drawFadeOverlay()
    {
        constexpr double fadeDurationSeconds = 0.22;
        double fadeProgress = (totalSeconds - fadeStart) / fadeDurationSeconds;
        if (fadeProgress < 0.0 || fadeProgress >= 1.0)
            return;
        Uint8 fadeAlpha = static_cast<Uint8>((1.0 - fadeProgress) * 255.0);
        drawFilledRect(0, 0, width, height, Color(Colors::Background.red, Colors::Background.green, Colors::Background.blue, fadeAlpha));
    }
}
