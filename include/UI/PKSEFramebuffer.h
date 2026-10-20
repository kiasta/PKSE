#ifndef UI_PKSE_FRAMEBUFFER_H
#define UI_PKSE_FRAMEBUFFER_H

#include <cstdint>
#include <map>
#include <string>

#include "UI/Common.h"

// Forward-declare types so we don't pull heavy SDL/GL/NanoVG headers into the 15+ UI files that
// include this header. The concrete includes live only in PKSEFramebuffer.cpp.
struct SDL_Window;
typedef void *SDL_GLContext;
struct NVGcontext;

namespace UI
{
    // Typographic scale (Nunito). Body is the default; Heading and Title are faux-bold.
    enum class TextStyle
    {
        Caption,
        Body,
        Heading,
        Title,
        Count
    };

    // NanoVG (OpenGL) 2D surface — GPU anti-aliased vector graphics + text. The public API is
    // deliberately identical to the earlier SDL_Renderer version so every screen/panel/dialog
    // keeps working unchanged; SDL provides only the window, the GL context, and input.
    class PKSEFramebuffer
    {
    public:
        PKSEFramebuffer();
        ~PKSEFramebuffer();

        void clear(Color color);
        void drawPixel(int pixelX, int pixelY, Color color);
        void drawRect(int rectX, int rectY, int rectWidth, int rectHeight, Color color);
        void drawFilledRect(int rectX, int rectY, int rectWidth, int rectHeight, Color color);
        void drawText(int textX, int textY, const char *text, Color color, TextStyle style = TextStyle::Body);
        void drawText(int textX, int textY, const std::string &text, Color color, TextStyle style = TextStyle::Body);

        // Draw a symbol (gender ♂/♀, star ★, heart ♥, ◀ ▶, …) via the fallback symbol font,
        // since the primary UI font (Nunito) doesn't include these glyphs. `style` picks the size,
        // so a symbol placed inside a small badge can be measured and drawn at the same (Caption) size.
        void drawSymbol(int symbolX, int symbolY, const std::string &symbol, Color color, TextStyle style = TextStyle::Body);
        void drawImage(int imageX, int imageY, int sourceWidth, int sourceHeight, const unsigned char *imageData, int channels);
        void drawImageScaled(int imageX, int imageY, int sourceWidth, int sourceHeight, int destWidth, int destHeight, const unsigned char *imageData, int channels);
        void flush();

        // Textures are cached under the ADDRESS of the pixel buffer they were built from, so when
        // the sprite cache frees a buffer its texture has to go too -- otherwise a later sprite
        // allocated at that same address would be drawn with the freed one's image. Hooked up to
        // SpriteManager's eviction callback; onSpriteEvicted forwards to the live framebuffer.
        void invalidateImage(const unsigned char *data);
        static void onSpriteEvicted(const unsigned char *data);

        // Clip subsequent drawing to a rectangle (for a scrollable region); clearClip() removes it.
        // Both need an active frame; primitives outside the clip rect are not rendered.
        void setClipRect(int clipX, int clipY, int clipWidth, int clipHeight);
        void clearClip();

        // Encapsulate the current look so panels stay simple and stay consistent when the
        // theme changes. All pull from the active Colors palette.
        void drawCard(int cardX, int cardY, int cardWidth, int cardHeight); // surface fill + border outline
        // selection fill + accent left edge
        void drawSelectionHighlight(int highlightX, int highlightY, int highlightWidth, int highlightHeight);
        void drawHDivider(int dividerX, int dividerY, int dividerWidth); // subtle horizontal rule
        void drawFilledEllipse(int centerX, int centerY, int radiusX, int rectY, Color color); // e.g. soft shadows

        // Anti-aliased on the horizontal edges of rounded corners (fractional coverage);
        // straight edges are exact. `r` is the corner radius (clamped to min(w,h)/2).
        void drawFilledRoundedRect(int rectX, int rectY, int rectWidth, int rectHeight, int cornerRadius, Color color);
        void drawRoundedRect(int rectX, int rectY, int rectWidth, int rectHeight, int cornerRadius, Color color, int thickness = 1);
        void drawFilledCircle(int centerX, int centerY, int radius, Color color);
        void drawCircle(int centerX, int centerY, int radius, Color color, int thickness = 1);
        // Draws a Pokemon egg (cream oval + teal spots) centered at (cx,cy), ~size px tall.
        // Used in the box/storage grids in place of the species sprite when a pokemon is an egg.
        void drawEgg(int centerX, int centerY, int size);
        // Draws HOME's shiny mark -- a four-pointed sparkle (concave-sided star) -- filling a size×size box
        // whose top-left is (x,y), so it drops into a marker row like any other icon.
        void drawShinyMark(int markX, int markY, int size, Color color);

        /**
         * File-browser row icons: a manila folder and a sheet of paper with a turned corner.
         *
         * Drawn rather than set in text. The browser used font glyphs -- a solid triangle for a
         * folder and a bullet for a file -- and a triangle in a list reads as "expand this row",
         * which is not what pressing A does. These are shapes with no other meaning.
         *
         * Vector rather than sprites for the same reason every other icon here is: they stay crisp
         * at any row height, they take the theme's colours, and they add nothing to romfs. Both are
         * anchored top-left in a `size` x `size` box, like drawShinyMark, so a caller can swap one
         * for the other without moving anything.
         *
         * `open` tilts the folder's front panel forward, which the browser uses to mark the ".."
         * row -- the one folder you are leaving rather than entering.
         */
        void drawFolderIcon(int iconX, int iconY, int size, Color color, bool open = false);
        void drawFileIcon(int iconX, int iconY, int size, Color color);
        /**
         * The search box's magnifier: a lens with a handle running down-right at 45 degrees.
         *
         * Drawn rather than set in text for the same reason as the two above -- the UI font has no
         * magnifier. THE HANDLE IS THE WHOLE ICON. This was a stroked circle with a short
         * horizontal stub beside it, which is not a magnifying glass at any size: it reads as a
         * lower-case "o" followed by an underscore. What makes the shape legible is the handle
         * being diagonal, round-capped and THICKER than the rim, so it reads as something held
         * rather than as two primitives that happen to touch.
         *
         * Anchored top-left in a `size` x `size` box, like drawShinyMark and the browser icons.
         */
        void drawSearchIcon(int iconX, int iconY, int size, Color color);
        /**
         * The Trade Evolve button's mark: a solid arrow pointing UP -- a triangular head over a
         * short shaft, the shape the games use for a Pokemon moving up a stage.
         *
         * Drawn rather than set in text for the same reason as the icons above: the UI font has no
         * such glyph, and a text caret reads as "expand this row", which is not what the button
         * does. Anchored top-left in a `size` x `size` box with its extents SYMMETRIC in it, so a
         * caller centres the icon by centring its box -- the contract every icon here keeps.
         */
        void drawEvolveArrow(int iconX, int iconY, int size, Color color);
        /**
         * The on-screen keyboard's two function-key marks: backspace, a tag pointing left with a
         * cross in it, and shift, an upward arrow drawn in outline -- or `filled` while shift is on.
         *
         * Drawn rather than set in text: neither the UI font nor its symbol fallbacks carry the
         * keyboard glyphs. Anchored top-left in a `size` x `size` box with symmetric extents, like
         * every icon here, so a caller centres one by centring its box.
         */
        void drawBackspaceIcon(int iconX, int iconY, int size, Color color);
        void drawShiftIcon(int iconX, int iconY, int size, Color color, bool filled);
        // Storage-grid cursor: a wide arrowhead pointing straight down, no shaft. Its POINT lands on
        // (tipX, tipY) with the head above, symmetric about that x. `headHeight` sizes the head --
        // the visible arrow; the mitred outline runs on below it to the point at tipY, adding ~27%
        // more. Drawn in the active cursor mode's colour.
        void drawPointerCursor(int tipX, int tipY, int headHeight, Color color);
        // Stadium (fully-rounded) pill = rounded rect with r = h/2. HOME's headers/badges/buttons.
        void drawPill(int pillX, int pillY, int pillWidth, int pillHeight, Color color);
        void drawPillBorder(int pillX, int pillY, int pillWidth, int pillHeight, Color color, int thickness = 1);
        // Soft drop shadow / elevation under a card, pill, or bar (layered translucent rounded rects).
        void drawSoftShadow(int shadowX, int shadowY, int shadowWidth, int shadowHeight, int cornerRadius);
        // Vertical two-stop gradient (screen backdrops, header/pill fills).
        void drawVerticalGradient(int gradientX, int gradientY, int gradientWidth, int gradientHeight, Color top, Color bottom);
        // The signature stat radar. `values` must be in HOME vertex order
        // [HP, Attack, Defense, Speed, Sp.Def, Sp.Atk] (HP at top, clockwise). Draws the
        // web + spokes, a translucent filled polygon, and the outline. Labels are the caller's job.
        void drawStatHexagon(int centerX, int centerY, int hexRadius, const float *values, int count, float maxValue, Color fill, Color webColor, Color outline);
        // Two-tone rounded type badge (HOME style): colored icon chip + name. Returns total width drawn.
        int drawTypeBadge(int badgeX, int badgeY, const std::string &typeName, Color typeColor);
        // Draw a sprite with a gentle idle animation (bob + breathe) and a soft ground
        // shadow, driven by the frame tick. (x,y,boxW,boxH) is the nominal placement box;
        // (srcW,srcH) the sprite's native size; `phase` offsets the timing per sprite.
        void drawSpriteIdle(int spriteX, int spriteY, int boxWidth, int boxHeight, int sourceWidth, int sourceHeight, const unsigned char *data, int channels, float phase);

        int getWidth() const { return width; }
        int getHeight() const { return height; }

        // Screen transition fade (Phase 4.6). startFade() marks "now" as the fade start; call it when
        // a screen begins. drawFadeOverlay() draws a full-screen Background-colored veil that fades
        // from opaque to clear over ~0.22s (so the content materializes) — call it each frame just
        // before flush(). A no-op once the fade has elapsed.
        void startFade();
        void drawFadeOverlay();

        // Frame timing (updated each flush()). Use these to drive animation without
        // every screen having to read a clock. deltaSeconds = time since last frame.
        float getDeltaSeconds() const { return deltaSeconds; }
        double getTimeSeconds() const { return totalSeconds; }

        // Exposed so callers can do their own layout math (centering, right-align, wrapping).
        // Returns the pixel size of `text` in the given style's font.
        void measureText(const std::string &text, int &outWidth, int &outHeight, TextStyle style = TextStyle::Body);
        // Line height (font ascent+descent) for a style — for consistent vertical spacing.
        int lineHeight(TextStyle style = TextStyle::Body) const;
        // The y to hand drawText so the text's CAPITAL LETTERS are centred on `centerY` -- which is what
        // lines a label up with a badge, a dot or the middle of a row. Centring the LINE BOX instead
        // (centerY - lineHeight / 2) leaves the capitals a few pixels high, because the box keeps room
        // below the baseline for descenders that most labels never use.
        int textYCenteredOn(int centerY, TextStyle style = TextStyle::Body) const;

    private:
        // Lazily build + cache a NanoVG image from a raw sprite pixel buffer, keyed by the buffer
        // pointer (SpriteManager caches sprites for the session, so pointers are stable). Converts
        // RGB→RGBA if needed (NanoVG images are RGBA). Returns the image handle (>=0), or -1.
        int nvgImageFor(const unsigned char *data, int dialogWidth, int dialogHeight, int channels);
        // Set NanoVG's font face + size for a text style (Heading/Title get a heavier faux-bold).
        void applyTextStyle(TextStyle style) const;
        // Begin the NanoVG frame lazily if one isn't already active. Returns true if vg is valid
        // and a frame is ready to draw into (so callers do `if (!ensureFrame()) return;`).
        bool ensureFrame();

        SDL_Window *window;
        SDL_GLContext glContext;
        NVGcontext *vg;
        int fontSans;             // Nunito (primary); the symbol fonts are added as fallbacks to it
        int fontSym;              // Noto Sans Symbols (♂ ♀ ★ …)
        int fontSym2;             // Noto Sans Symbols 2 (card suits ♥ …)
        bool frameActive = false; // true between the frame's first draw/clear and flush()
        int width;
        int height;

        // Frame timing (see getDeltaSeconds/getTimeSeconds).
        uint64_t lastCounter = 0;
        float deltaSeconds = 0.0f;
        double totalSeconds = 0.0;
        double fadeStart = -100.0; // start time of the current screen fade (see startFade)

        // NanoVG image handles keyed by the source pixel-buffer pointer.
        std::map<const unsigned char *, int> imageCache;
    };
}

#endif
