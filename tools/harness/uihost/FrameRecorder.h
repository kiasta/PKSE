#ifndef PKSE_UIHOST_FRAME_RECORDER_H
#define PKSE_UIHOST_FRAME_RECORDER_H

#include <cstdint>
#include <string>
#include <vector>

#include "UI/Common.h"
#include "UI/PKSEFramebuffer.h"

/**
 * The thing an immediate-mode UI does not otherwise have: a queryable frame.
 *
 * Cypress works because the DOM is a retained tree you can select into. PKSE draws straight through
 * NanoVG and keeps nothing, so there is no tree to query -- which is the reason "test the UI" looked
 * impossible here. Recording every primitive as it is drawn rebuilds exactly that structure once per
 * frame: a list of shapes, text and sprites with positions, colours and bounds. Assertions run
 * against THAT, so a test says what the player would see rather than what a field happens to hold.
 *
 * The recording framebuffer is a drop-in for the real one because PKSEFramebuffer's public API was
 * deliberately kept backend-independent across the NanoVG migration. Nothing in the UI layer had to
 * change to make this work, and no product code knows the harness exists.
 */
namespace UIHost {

    enum class Kind : uint8_t { Clear, Shape, Text, Sprite };

    struct Draw {
        Kind        kind = Kind::Shape;
        const char* op   = "";        // which primitive: "filledRect", "pill", "card", "hexagon"...
        int drawX = 0, drawY = 0, drawWidth = 0, drawHeight = 0;
        UI::Color   color{0, 0, 0, 0};
        std::string text;             // Text/Symbol only
        UI::TextStyle style = UI::TextStyle::Body;
        /// False when the active clip rect excluded this primitive entirely. A draw that happened
        /// but could not be seen is the difference between "the label is missing" and "the label is
        /// scrolled out of its pane", and those are different bugs.
        bool visible = true;

        int right()   const { return drawX + drawWidth; }
        int bottom()  const { return drawY + drawHeight; }
        int centerX() const { return drawX + drawWidth / 2; }
        int centerY() const { return drawY + drawHeight / 2; }
    };

    /// One flushed frame.
    struct Frame {
        long index = 0;
        std::vector<Draw> draws;

        /// Every text primitive whose content CONTAINS `needle`, visible ones only by default.
        std::vector<const Draw*> text(const std::string& needle, bool visibleOnly = true) const;
        /// As text(), but the whole string must match.
        std::vector<const Draw*> textExact(const std::string& s, bool visibleOnly = true) const;
        bool   has(const std::string& needle) const { return !text(needle).empty(); }
        size_t count(const std::string& needle) const { return text(needle).size(); }
        /// First match in draw order, or nullptr.
        const Draw* first(const std::string& needle) const;
        std::vector<const Draw*> ops(const char* op, bool visibleOnly = true) const;
        size_t sprites() const;
        /// All visible text in draw order, one per line -- what a failure report prints so the
        /// reader can see the screen that failed instead of guessing at it.
        std::string dump() const;
    };

    /// Global sink the recording framebuffer writes into. One UI runs at a time in a harness, so a
    /// singleton is honest here rather than plumbing a pointer through PKSEFramebuffer's fixed API.
    class Recorder {
    public:
        static Recorder& get();

        void   beginFrame();          // drop the in-progress frame and start a new one
        Frame& building() { return cur; }
        const Frame& last() const { return done; }
        long   frameCount() const { return frames; }

        void push(Draw d);
        void endFrame();              // called from flush(): cur -> done

        void setClip(int x, int y, int w, int h);
        void clearClip();
        bool clipContains(int x, int y, int w, int h) const;

    private:
        Frame cur, done;
        long  frames = 0;
        bool  clipping = false;
        int   cx = 0, cy = 0, cw = 0, ch = 0;
    };
}

#endif
