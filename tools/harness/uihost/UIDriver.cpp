#include "UIDriver.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace
{
    std::string g_shotDirectory;
    int g_writtenShotCount = 0;

    std::string escapeXml(const std::string &text)
    {
        std::string escaped;
        for (char character : text)
        {
            switch (character)
            {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            default:
                escaped += character;
            }
        }
        return escaped;
    }

    std::string toSvgColor(UI::Color color)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "rgba(%d,%d,%d,%.3f)", color.red, color.green, color.blue, color.alpha / 255.0f);
        return buffer;
    }

    float getFontSize(UI::TextStyle style)
    {
        switch (style)
        {
        case UI::TextStyle::Caption:
            return 15.0f;
        case UI::TextStyle::Heading:
            return 26.0f;
        case UI::TextStyle::Title:
            return 34.0f;
        default:
            return 19.0f;
        }
    }
}

namespace UIHost
{

    void Driver::setShotDirectory(std::string shotDirectory)
    {
        g_shotDirectory = std::move(shotDirectory);
    }
    int Driver::writtenShotCount()
    {
        return g_writtenShotCount;
    }

    Driver::Driver(Test::Context &testContext, UI::UIScreen &drivenScreen, std::string drivenScreenName) : context(testContext), screen(drivenScreen), screenName(std::move(drivenScreenName)) {}

    std::string Driver::describeLocation() const
    {
        std::string location = "screen=" + screenName;
        if (!currentStep.empty())
        {
            location += " step=\"" + currentStep + "\"";
        }
        location += " frame=" + std::to_string(Recorder::get().frameCount());
        return location;
    }

    Driver &Driver::step(std::string description)
    {
        currentStep = std::move(description);
        return *this;
    }

    // Identical in shape to UIManager's on-console loop; only the two input sources differ.
    void Driver::pump(u64 heldButtons)
    {
        pad.buttons_old = pad.buttons_cur;
        pad.buttons_cur = heldButtons;
        touch.update();
        screen.update(pad, touch);
        Recorder::get().beginFrame();   // robust whether or not draw() opens with clear()
        screen.draw(framebuffer);
        framebuffer.flush();
        checkInvariants();
    }

    Driver &Driver::frame(int idleFrameCount)
    {
        for (int frameIndex = 0; frameIndex < idleFrameCount; ++frameIndex)
        {
            pump(0);
        }
        return *this;
    }

    Driver &Driver::press(u64 button, int pressCount)
    {
        for (int pressIndex = 0; pressIndex < pressCount; ++pressIndex)
        {
            pump(button);
            pump(0);
        }
        return *this;
    }

    Driver &Driver::hold(u64 button, int heldFrameCount)
    {
        for (int frameIndex = 0; frameIndex < heldFrameCount; ++frameIndex)
        {
            pump(button);
        }
        pump(0);
        return *this;
    }

    Driver &Driver::tilt(int stickX, int stickY, int tiltFrameCount)
    {
        pad.sticks[0] = {stickX, stickY};
        for (int frameIndex = 0; frameIndex < tiltFrameCount; ++frameIndex)
        {
            pump(0);
        }
        pad.sticks[0] = {};
        pump(0);
        return *this;
    }

    Driver &Driver::tap(int tapX, int tapY)
    {
        setTouch(true, tapX, tapY);
        pump(0);
        setTouch(false, tapX, tapY);   // release keeps the position: the UI reads startX/startY on release
        pump(0);
        return *this;
    }

    Driver &Driver::drag(int startX, int startY, int endX, int endY, int stepCount)
    {
        setTouch(true, startX, startY);
        pump(0);
        for (int stepIndex = 1; stepIndex <= stepCount; ++stepIndex)
        {
            setTouch(true, startX + (endX - startX) * stepIndex / stepCount, startY + (endY - startY) * stepIndex / stepCount);
            pump(0);
        }
        setTouch(false, endX, endY);
        pump(0);
        return *this;
    }

    // These need no test to be written for them: any sequence a test drives is checked for free,
    // which is what makes exploratory input sequences worth running at all.
    void Driver::checkInvariants()
    {
        const Frame &lastFrame = Recorder::get().last();

        if (lastFrame.draws.empty())
        {
            context.fail("a frame drew nothing at all", describeLocation());
            return;
        }

        int offscreenDrawCount = 0;
        const Draw *firstOffscreenDraw = nullptr;
        for (const Draw &draw : lastFrame.draws)
        {
            if (!draw.visible)
            {
                continue;                       // legitimately clipped out
            }
            if (draw.kind == Kind::Clear)
            {
                continue;
            }
            // Fully outside the 1280x720 panel: drawn, not clipped, and impossible to see.
            if (draw.right() <= 0 || draw.drawX >= 1280 || draw.bottom() <= 0 || draw.drawY >= 720)
            {
                ++offscreenDrawCount;
                if (!firstOffscreenDraw)
                {
                    firstOffscreenDraw = &draw;
                }
            }
        }
        if (offscreenDrawCount > 0 && firstOffscreenDraw)
        {
            std::string description = "drawing landed completely off-screen";
            std::string detailText = describeLocation() + " op=" + firstOffscreenDraw->primitive + " x=" + std::to_string(firstOffscreenDraw->drawX) + " y=" + std::to_string(firstOffscreenDraw->drawY) + " w=" + std::to_string(firstOffscreenDraw->drawWidth) + " h=" + std::to_string(firstOffscreenDraw->drawHeight) + " count=" + std::to_string(offscreenDrawCount);
            if (!firstOffscreenDraw->text.empty())
            {
                detailText += " text=\"" + firstOffscreenDraw->text + "\"";
            }
            context.fail(description, detailText);
        }
    }

    void Driver::onFailure(const std::string &description)
    {
        ++failureCount;
        if (g_shotDirectory.empty() || failureCount > 1)
        {
            return;   // one shot per driver, on first failure
        }
        char buffer[512];
        std::snprintf(buffer, sizeof buffer, "%s/fail-%02d-%s.svg", g_shotDirectory.c_str(), g_writtenShotCount + 1, screenName.c_str());
        screenshot(buffer);
        ++g_writtenShotCount;
        static_cast<void>(description);
    }

    bool Driver::expect(bool passed, const std::string &description, const std::string &detail)
    {
        if (passed)
        {
            context.check(true, description);
            return true;
        }
        std::string detailText = describeLocation();
        if (!detail.empty())
        {
            detailText += " " + detail;
        }
        // The visible text of the failing frame, capped. A UI failure is unreadable without it.
        std::string screenText = frameData().dump();
        if (screenText.size() > 1200)
        {
            screenText.resize(1200);
            screenText += "    ...\n";
        }
        context.check(false, description, detailText + "\n  screen was:\n" + screenText);
        onFailure(description);
        return false;
    }

    bool Driver::see(const std::string &text)
    {
        return expect(frameData().has(text), "expected text was not on screen", "want=\"" + text + "\"");
    }

    bool Driver::notSee(const std::string &text)
    {
        return expect(!frameData().has(text), "text was on screen that should not have been", "unwanted=\"" + text + "\"");
    }

    bool Driver::seeCount(const std::string &text, size_t expected)
    {
        const size_t foundCount = frameData().matchCount(text);
        return expect(foundCount == expected, "wrong number of matches on screen", "want=\"" + text + "\" expected=" + std::to_string(expected) + " got=" + std::to_string(foundCount));
    }

    bool Driver::seeNear(const std::string &text, int expectedX, int expectedY, int tolerance)
    {
        for (const Draw *match : frameData().text(text))
        {
            if (std::abs(match->drawX - expectedX) <= tolerance && std::abs(match->drawY - expectedY) <= tolerance)
            {
                return expect(true, "text near");
            }
        }
        const Draw *firstMatch = frameData().first(text);
        std::string detailText = "want=\"" + text + "\" at=" + std::to_string(expectedX) + "," + std::to_string(expectedY) + " tol=" + std::to_string(tolerance);
        if (firstMatch)
        {
            detailText += " foundAt=" + std::to_string(firstMatch->drawX) + "," + std::to_string(firstMatch->drawY);
        }
        else
        {
            detailText += " (text not drawn at all)";
        }
        return expect(false, "text was not where it should be", detailText);
    }

    bool Driver::seePrimitive(const char *primitive, size_t minimumCount)
    {
        const size_t foundCount = frameData().primitives(primitive).size();
        return expect(foundCount >= minimumCount, "expected primitive was not drawn", std::string("op=") + primitive + " atLeast=" + std::to_string(minimumCount) + " got=" + std::to_string(foundCount));
    }

    bool Driver::seeSprites(size_t minimumCount)
    {
        const size_t foundCount = frameData().spriteCount();
        return expect(foundCount >= minimumCount, "too few sprites were drawn", "atLeast=" + std::to_string(minimumCount) + " got=" + std::to_string(foundCount));
    }

    void Driver::screenshot(const std::string &path) const
    {
        std::FILE *svgFile = std::fopen(path.c_str(), "w");
        if (!svgFile)
        {
            return;
        }
        const Frame &lastFrame = frameData();
        std::fprintf(svgFile, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1280\" height=\"720\" viewBox=\"0 0 1280 720\">\n<rect width=\"1280\" height=\"720\" fill=\"#111\"/>\n");
        for (const Draw &draw : lastFrame.draws)
        {
            if (!draw.visible)
            {
                continue;
            }
            if (draw.kind == UIHost::Kind::Text)
            {
                std::fprintf(svgFile, "<text x=\"%d\" y=\"%d\" font-family=\"sans-serif\" font-size=\"%.1f\" fill=\"%s\" dominant-baseline=\"hanging\">%s</text>\n", draw.drawX, draw.drawY, getFontSize(draw.style), toSvgColor(draw.color).c_str(), escapeXml(draw.text).c_str());
            }
            else if (draw.kind == UIHost::Kind::Sprite)
            {
                std::fprintf(svgFile, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"none\" stroke=\"#6cf\" stroke-dasharray=\"4 3\"/>\n", draw.drawX, draw.drawY, draw.drawWidth, draw.drawHeight);
            }
            else if (draw.drawWidth > 0 && draw.drawHeight > 0)
            {
                std::fprintf(svgFile, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"%s\"/>\n", draw.drawX, draw.drawY, draw.drawWidth, draw.drawHeight, toSvgColor(draw.color).c_str());
            }
        }
        std::fprintf(svgFile, "</svg>\n");
        std::fclose(svgFile);
    }
}
