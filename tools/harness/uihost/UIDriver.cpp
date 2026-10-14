#include "UIDriver.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace {
    std::string gShotDir;
    int gShots = 0;

    std::string esc(const std::string& s) {
        std::string o;
        for (char c : s) {
            switch (c) {
                case '&': o += "&amp;";  break;
                case '<': o += "&lt;";   break;
                case '>': o += "&gt;";   break;
                case '"': o += "&quot;"; break;
                default:  o += c;
            }
        }
        return o;
    }

    std::string rgba(UI::Color c) {
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "rgba(%d,%d,%d,%.3f)", c.red, c.green, c.blue, c.alpha / 255.0f);
        return buffer;
    }

    float fontPx(UI::TextStyle s) {
        switch (s) {
            case UI::TextStyle::Caption: return 15.0f;
            case UI::TextStyle::Heading: return 26.0f;
            case UI::TextStyle::Title:   return 34.0f;
            default:                     return 19.0f;
        }
    }
}

namespace UIHost {

    void Driver::setShotDir(std::string dir) { gShotDir = std::move(dir); }
    int  Driver::shotsWritten()              { return gShots; }

    Driver::Driver(Test::Context& c, UI::UIScreen& s, std::string screenName)
        : context(c), screen(s), name(std::move(screenName)) {}

    std::string Driver::where() const {
        std::string w = "screen=" + name;
        if (!curStep.empty()) w += " step=\"" + curStep + "\"";
        w += " frame=" + std::to_string(Recorder::get().frameCount());
        return w;
    }

    Driver& Driver::step(std::string what) { curStep = std::move(what); return *this; }

    // Identical in shape to UIManager's on-console loop; only the two input sources differ.
    void Driver::pump(u64 held) {
        pad.buttons_old = pad.buttons_cur;
        pad.buttons_cur = held;
        touch.update();
        screen.update(pad, touch);
        Recorder::get().beginFrame();   // robust whether or not draw() opens with clear()
        screen.draw(framebuffer);
        framebuffer.flush();
        invariants();
    }

    Driver& Driver::frame(int n) {
        for (int i = 0; i < n; ++i) pump(0);
        return *this;
    }

    Driver& Driver::press(u64 button, int times) {
        for (int i = 0; i < times; ++i) { pump(button); pump(0); }
        return *this;
    }

    Driver& Driver::hold(u64 button, int frames) {
        for (int i = 0; i < frames; ++i) pump(button);
        pump(0);
        return *this;
    }

    Driver& Driver::tilt(int stickX, int stickY, int frames) {
        pad.sticks[0] = {stickX, stickY};
        for (int i = 0; i < frames; ++i) pump(0);
        pad.sticks[0] = {};
        pump(0);
        return *this;
    }

    Driver& Driver::tap(int x, int y) {
        setTouch(true, x, y);
        pump(0);
        setTouch(false, x, y);   // release keeps the position: the UI reads startX/startY on release
        pump(0);
        return *this;
    }

    Driver& Driver::drag(int x0, int y0, int x1, int y1, int steps) {
        setTouch(true, x0, y0);
        pump(0);
        for (int i = 1; i <= steps; ++i) {
            setTouch(true, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps);
            pump(0);
        }
        setTouch(false, x1, y1);
        pump(0);
        return *this;
    }

    // These need no test to be written for them: any sequence a test drives is checked for free,
    // which is what makes exploratory input sequences worth running at all.
    void Driver::invariants() {
        const Frame& f = Recorder::get().last();

        if (f.draws.empty()) {
            context.fail("a frame drew nothing at all", where());
            return;
        }

        int offscreen = 0;
        const Draw* worst = nullptr;
        for (const Draw& d : f.draws) {
            if (!d.visible) continue;                       // legitimately clipped out
            if (d.kind == Kind::Clear) continue;
            // Fully outside the 1280x720 panel: drawn, not clipped, and impossible to see.
            if (d.right() <= 0 || d.drawX >= 1280 || d.bottom() <= 0 || d.drawY >= 720) {
                ++offscreen;
                if (!worst) worst = &d;
            }
        }
        if (offscreen > 0 && worst) {
            std::string what = "drawing landed completely off-screen";
            std::string det  = where() + " op=" + worst->op +
                               " x=" + std::to_string(worst->drawX) + " y=" + std::to_string(worst->drawY) +
                               " w=" + std::to_string(worst->drawWidth) + " h=" + std::to_string(worst->drawHeight) +
                               " count=" + std::to_string(offscreen);
            if (!worst->text.empty()) det += " text=\"" + worst->text + "\"";
            context.fail(what, det);
        }
    }

    void Driver::onFailure(const std::string& what) {
        ++failuresHere;
        if (gShotDir.empty() || failuresHere > 1) return;   // one shot per driver, on first failure
        char buffer[512];
        std::snprintf(buffer, sizeof buffer, "%s/fail-%02d-%s.svg", gShotDir.c_str(), gShots + 1, name.c_str());
        screenshot(buffer);
        ++gShots;
        (void)what;
    }

    bool Driver::expect(bool ok, const std::string& what, const std::string& detail) {
        if (ok) { context.check(true, what); return true; }
        std::string det = where();
        if (!detail.empty()) det += " " + detail;
        // The visible text of the failing frame, capped. A UI failure is unreadable without it.
        std::string d = frameData().dump();
        if (d.size() > 1200) { d.resize(1200); d += "    ...\n"; }
        context.check(false, what, det + "\n  screen was:\n" + d);
        onFailure(what);
        return false;
    }

    bool Driver::see(const std::string& text) {
        return expect(frameData().has(text), "expected text was not on screen", "want=\"" + text + "\"");
    }

    bool Driver::notSee(const std::string& text) {
        return expect(!frameData().has(text), "text was on screen that should not have been",
                      "unwanted=\"" + text + "\"");
    }

    bool Driver::seeCount(const std::string& text, size_t expected) {
        const size_t got = frameData().count(text);
        return expect(got == expected, "wrong number of matches on screen",
                      "want=\"" + text + "\" expected=" + std::to_string(expected) +
                      " got=" + std::to_string(got));
    }

    bool Driver::seeNear(const std::string& text, int x, int y, int tol) {
        for (const Draw* d : frameData().text(text)) {
            if (std::abs(d->drawX - x) <= tol && std::abs(d->drawY - y) <= tol) return expect(true, "text near");
        }
        const Draw* any = frameData().first(text);
        std::string det = "want=\"" + text + "\" at=" + std::to_string(x) + "," + std::to_string(y) +
                          " tol=" + std::to_string(tol);
        if (any) det += " foundAt=" + std::to_string(any->drawX) + "," + std::to_string(any->drawY);
        else     det += " (text not drawn at all)";
        return expect(false, "text was not where it should be", det);
    }

    bool Driver::seeOp(const char* op, size_t atLeast) {
        const size_t got = frameData().ops(op).size();
        return expect(got >= atLeast, "expected primitive was not drawn",
                      std::string("op=") + op + " atLeast=" + std::to_string(atLeast) +
                      " got=" + std::to_string(got));
    }

    bool Driver::seeSprites(size_t atLeast) {
        const size_t got = frameData().sprites();
        return expect(got >= atLeast, "too few sprites were drawn",
                      "atLeast=" + std::to_string(atLeast) + " got=" + std::to_string(got));
    }

    void Driver::screenshot(const std::string& path) const {
        std::FILE* f = std::fopen(path.c_str(), "w");
        if (!f) return;
        const Frame& fr = frameData();
        std::fprintf(f, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1280\" height=\"720\" "
                        "viewBox=\"0 0 1280 720\">\n<rect width=\"1280\" height=\"720\" fill=\"#111\"/>\n");
        for (const Draw& d : fr.draws) {
            if (!d.visible) continue;
            if (d.kind == UIHost::Kind::Text) {
                std::fprintf(f, "<text x=\"%d\" y=\"%d\" font-family=\"sans-serif\" font-size=\"%.1f\" "
                                "fill=\"%s\" dominant-baseline=\"hanging\">%s</text>\n",
                             d.drawX, d.drawY, fontPx(d.style), rgba(d.color).c_str(), esc(d.text).c_str());
            } else if (d.kind == UIHost::Kind::Sprite) {
                std::fprintf(f,
                             "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"none\" "
                             "stroke=\"#6cf\" stroke-dasharray=\"4 3\"/>\n",
                             d.drawX, d.drawY, d.drawWidth, d.drawHeight);
            } else if (d.drawWidth > 0 && d.drawHeight > 0) {
                std::fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"%s\"/>\n",
                             d.drawX, d.drawY, d.drawWidth, d.drawHeight, rgba(d.color).c_str());
            }
        }
        std::fprintf(f, "</svg>\n");
        std::fclose(f);
    }
}
