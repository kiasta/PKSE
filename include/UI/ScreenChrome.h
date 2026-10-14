#ifndef UI_SCREEN_CHROME_H
#define UI_SCREEN_CHROME_H

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "UI/PKSEFramebuffer.h"
#include "UI/Common.h"
#include "UI/TouchInput.h"

namespace UI
{

    constexpr int CHROME_CORNER_RADIUS = 18; // curve on the content-facing edge of both bars
    constexpr int HEADER_HEIGHT = 64;
    constexpr int NAV_BAR_HEIGHT = 46;

    // Draw (or, with measureOnly, just measure) one controller badge, shaped like the real button:
    // face buttons are round, shoulders are rounded rectangles, +/- are round with a drawn bar, and
    // the d-pad is a cross with the unused axis dimmed. `cy` is the badge's vertical CENTRE.
    // Returns the width consumed, or 0 if the token isn't a button PKSE knows how to draw.
    inline int buttonGlyph(PKSEFramebuffer &framebuffer, int badgeX, int centerY, const std::string &buttonToken,
                           bool measureOnly)
    {
        // Mid-grey badge with the bar colour as ink. Both are theme colours, so the pair inverts
        // itself: dark letter on light grey in the dark theme, white letter on grey in the light one.
        const Color fillColor = Colors::TextDim;
        const Color inkColor = Colors::Panel;
        constexpr int faceButtonRadius = 12; // face-button radius

        auto centred = [&](const std::string &s, int boxX, int boxWidth)
        {
            int textWidth, th;
            framebuffer.measureText(s, textWidth, th, TextStyle::Caption);
            framebuffer.drawText(boxX + (boxWidth - textWidth) / 2, centerY - th / 2, s, inkColor, TextStyle::Caption);
        };

        // Face buttons.
        if (buttonToken.size() == 1 &&
            (buttonToken[0] == 'A' || buttonToken[0] == 'B' || buttonToken[0] == 'X' || buttonToken[0] == 'Y'))
        {
            if (!measureOnly)
            {
                framebuffer.drawFilledCircle(badgeX + faceButtonRadius, centerY, faceButtonRadius, fillColor);
                centred(buttonToken, badgeX, faceButtonRadius * 2);
            }
            return faceButtonRadius * 2;
        }

        // Plus / Minus. The bars are drawn rather than typed -- Nunito's '+' and '-' are far too
        // thin to read at badge size, and '-' sits at x-height instead of centred.
        if (buttonToken == "+" || buttonToken == "Plus" || buttonToken == "-" || buttonToken == "Minus")
        {
            if (!measureOnly)
            {
                framebuffer.drawFilledCircle(badgeX + faceButtonRadius, centerY, faceButtonRadius, fillColor);
                framebuffer.drawFilledRoundedRect(badgeX + faceButtonRadius - 6, centerY - 1, 13, 3, 1, inkColor);
                if (buttonToken == "+" || buttonToken == "Plus")
                    framebuffer.drawFilledRoundedRect(badgeX + faceButtonRadius - 1, centerY - 6, 3, 13, 1, inkColor);
            }
            return faceButtonRadius * 2;
        }

        // Shoulders, and the slash pairs the hints use ("L/R", "ZL/ZR"): rounded rects sized to text.
        if (buttonToken == "L" || buttonToken == "R" || buttonToken == "ZL" || buttonToken == "ZR" ||
            buttonToken == "L/R" || buttonToken == "ZL/ZR")
        {
            int textWidth, th;
            framebuffer.measureText(buttonToken, textWidth, th, TextStyle::Caption);
            const int badgeWidth = textWidth + 14, h = 22;
            if (!measureOnly)
            {
                framebuffer.drawFilledRoundedRect(badgeX, centerY - h / 2, badgeWidth, h, 7, fillColor);
                centred(buttonToken, badgeX, badgeWidth);
            }
            return badgeWidth;
        }

        // D-pad. "Up/Down" and "Left/Right" dim the axis they don't use, so the badge itself says
        // which way the stick moves rather than relying on the label to explain it.
        if (buttonToken == "Arrows" || buttonToken == "D-Pad" || buttonToken == "Up/Down" ||
            buttonToken == "Left/Right")
        {
            constexpr int crossSize = 24, armThickness = 9; // across the cross, and one arm's thickness
            if (!measureOnly)
            {
                const Color dim(fillColor.red, fillColor.green, fillColor.blue, 70);
                const bool verticalOnly = (buttonToken == "Up/Down"), hOnly = (buttonToken == "Left/Right");
                const int verticalArmX = badgeX + (crossSize - armThickness) / 2, vy = centerY - crossSize / 2;
                const int horizontalArmX = badgeX, hy = centerY - armThickness / 2;
                // Dim arm first, so the solid one wins where they overlap in the middle.
                if (verticalOnly)
                {
                    framebuffer.drawFilledRoundedRect(horizontalArmX, hy, crossSize, armThickness, 3, dim);
                    framebuffer.drawFilledRoundedRect(verticalArmX, vy, armThickness, crossSize, 3, fillColor);
                }
                else if (hOnly)
                {
                    framebuffer.drawFilledRoundedRect(verticalArmX, vy, armThickness, crossSize, 3, dim);
                    framebuffer.drawFilledRoundedRect(horizontalArmX, hy, crossSize, armThickness, 3, fillColor);
                }
                else
                {
                    framebuffer.drawFilledRoundedRect(verticalArmX, vy, armThickness, crossSize, 3, fillColor);
                    framebuffer.drawFilledRoundedRect(horizontalArmX, hy, crossSize, armThickness, 3, fillColor);
                }
            }
            return crossSize;
        }

        // A single d-pad direction: a rounded-square badge with a geometric triangle arrow. Used by
        // the edit dialogs' -1 / +1 steps (the shoulders take the +/-10 and +/-100 steps).
        if (buttonToken == "Left" || buttonToken == "Right" || buttonToken == "Up" || buttonToken == "Down")
        {
            constexpr int badgeSize = 22;
            if (!measureOnly)
            {
                framebuffer.drawFilledRoundedRect(badgeX, centerY - badgeSize / 2, badgeSize, badgeSize, 6, fillColor);
                const std::string arrowGlyph = buttonToken == "Left"    ? "\xE2\x97\x80"  // ◀
                                        : buttonToken == "Right" ? "\xE2\x96\xB6"  // ▶
                                        : buttonToken == "Up"    ? "\xE2\x96\xB2"  // ▲
                                                         : "\xE2\x96\xBC"; // ▼
                // Measure AND draw the arrow at Caption size -- drawSymbol otherwise defaults to Body,
                // which overflowed this 22px badge and mis-centred the glyph (the ◀ was clipping away).
                int textWidth, th;
                framebuffer.measureText(arrowGlyph, textWidth, th, TextStyle::Caption);
                framebuffer.drawSymbol(badgeX + (badgeSize - textWidth) / 2, centerY - th / 2, arrowGlyph, inkColor,
                                       TextStyle::Caption);
            }
            return badgeSize;
        }
        return 0; // not a button we have a badge for
    }

    inline int buttonGlyphWidth(PKSEFramebuffer &framebuffer, const std::string &buttonToken)
    {
        return buttonGlyph(framebuffer, 0, 0, buttonToken, true);
    }

    // A pressable button that carries its controller badge ON the button (glyph + label, centred),
    // instead of relying on a separate "A: Confirm" guide line below it. `fill` lets destructive
    // actions stay red; `textColor` keeps the label legible on that fill. Screen-independent (does
    // NOT register a touch target) so any screen can use it and wire its own hit region --
    // drawEditChoiceButton wraps this for the TrainerViewScreen dialogs.
    //
    // `pressed` draws the button HELD: an accent ring and an accent tint over the caller's fill. A
    // tapped dialog button has no cursor resting on it the way a menu row does, so without this the
    // frame between a touch and the action it stands for would show nothing at all -- and that frame
    // now always exists, because a tap is deferred by one (see TrainerViewScreen::armTap). The ring
    // is drawn INSIDE the button's own rectangle, so a held button occupies exactly the space an
    // unheld one does and nothing around it shifts. The tint goes OVER the caller's fill rather than
    // replacing it, so a destructive button stays recognisably red while it is held.
    inline void drawGlyphButton(PKSEFramebuffer &framebuffer, int boxX, int boxY, int boxWidth, int boxHeight,
                                const std::string &glyph, const std::string &label,
                                Color fillColor = Colors::PanelAlt, Color textColor = Colors::Text,
                                bool pressed = false)
    {
        framebuffer.drawFilledRoundedRect(boxX, boxY, boxWidth, boxHeight, 8, fillColor);
        if (pressed)
        {
            Color pressTint = Colors::Primary;
            pressTint.alpha = 110;
            framebuffer.drawFilledRoundedRect(boxX, boxY, boxWidth, boxHeight, 8, pressTint);
            framebuffer.drawRoundedRect(boxX, boxY, boxWidth, boxHeight, 8, Colors::Primary, 3);
        }
        else
        {
            framebuffer.drawRoundedRect(boxX, boxY, boxWidth, boxHeight, 8, Colors::Border, 1);
        }
        const int glyphWidth = buttonGlyphWidth(framebuffer, glyph);
        int labelWidth, lh;
        framebuffer.measureText(label, labelWidth, lh);
        const int glyphX = boxX + (boxWidth - (glyphWidth + 10 + labelWidth)) / 2;
        buttonGlyph(framebuffer, glyphX, boxY + boxHeight / 2, glyph, false);
        framebuffer.drawText(glyphX + glyphWidth + 10, boxY + (boxHeight - lh) / 2, label, textColor);
    }

    //
    // Every screen already publishes a contextual hint string, so making the badges tappable gives
    // HOME-style on-screen action buttons everywhere at once. A tap resolves to that button's press
    // and the screen ORs it into padGetButtonsDown, so no existing handler changes -- there is one
    // input path, not two, and a tap can never diverge from what the physical button does.
    //
    // Only badges standing for exactly ONE button get a hit region. "L/R", "ZL/ZR" and the d-pad stay
    // informational: a single badge can't say whether you meant L or R, and splitting one in half
    // would leave each half far below a usable touch target.
    struct NavHit
    {
        int hitX, hitY, hitWidth, hitHeight;
        uint64_t button;
    };
    inline std::vector<NavHit> g_navHits;

    inline uint64_t navButtonFor(const std::string &buttonToken)
    {
        if (buttonToken == "A")
            return HidNpadButton_A;
        if (buttonToken == "B")
            return HidNpadButton_B;
        if (buttonToken == "X")
            return HidNpadButton_X;
        if (buttonToken == "Y")
            return HidNpadButton_Y;
        if (buttonToken == "+" || buttonToken == "Plus")
            return HidNpadButton_Plus;
        if (buttonToken == "-" || buttonToken == "Minus")
            return HidNpadButton_Minus;
        if (buttonToken == "L")
            return HidNpadButton_L;
        if (buttonToken == "R")
            return HidNpadButton_R;
        if (buttonToken == "ZL")
            return HidNpadButton_ZL;
        if (buttonToken == "ZR")
            return HidNpadButton_ZR;
        return 0; // multi-button or directional badge: informational only
    }

    // Hit-test a fresh tap against the badges captured during the PREVIOUS frame's draw (same
    // one-frame-late contract as TrainerViewScreen::touchedButtonId). Returns a mask to fold into
    // buttonsDown, or 0. Edge-triggered, so it behaves exactly like padGetButtonsDown.
    inline uint64_t navTouchButton(const TouchInput &touch)
    {
        if (!touch.justPressed())
            return 0;
        for (const NavHit &hit : g_navHits)
        {
            if (touch.x() >= hit.hitX && touch.x() < hit.hitX + hit.hitWidth &&
                touch.y() >= hit.hitY && touch.y() < hit.hitY + hit.hitHeight)
                return hit.button;
        }
        return 0;
    }

    /**
     * Breaks `text` into lines no wider than `maxLineWidth` when drawn in `style`: at spaces where it
     * can, and between characters where one word is wider than the whole line.
     *
     * MEASURED, NOT COUNTED. A character budget is wrong twice over: Nunito is proportional, so seven
     * narrow letters and seven wide ones differ by half their width, and a Japanese nickname or place
     * name has no spaces to break at at all. Breaking inside a word is the last resort, and it cuts on
     * a UTF-8 boundary -- never through a multi-byte character, which would draw as nothing.
     *
     * A run of spaces collapses to the one break it stands for; the text has no other whitespace the
     * layout should keep.
     */
    inline std::vector<std::string> wrapTextToWidth(PKSEFramebuffer &framebuffer, const std::string &text,
                                                    int maxLineWidth, TextStyle style)
    {
        const auto fits = [&](const std::string &candidate)
        {
            int candidateWidth = 0, candidateHeight = 0;
            framebuffer.measureText(candidate, candidateWidth, candidateHeight, style);
            return candidateWidth <= maxLineWidth;
        };
        // The byte just past the UTF-8 character that starts at `characterStart`.
        const auto nextCharacterEnd = [](const std::string &word, size_t characterStart)
        {
            size_t characterEnd = characterStart + 1;
            while (characterEnd < word.size() && (static_cast<unsigned char>(word[characterEnd]) & 0xC0) == 0x80)
                ++characterEnd;
            return characterEnd;
        };

        std::vector<std::string> lines;
        std::string currentLine;
        size_t wordStart = 0;
        while (wordStart < text.size())
        {
            size_t wordEnd = text.find(' ', wordStart);
            if (wordEnd == std::string::npos)
                wordEnd = text.size();
            std::string word = text.substr(wordStart, wordEnd - wordStart);
            wordStart = wordEnd + 1;
            if (word.empty())
                continue;

            const std::string joined = currentLine.empty() ? word : currentLine + " " + word;
            if (fits(joined))
            {
                currentLine = joined;
                continue;
            }
            if (!currentLine.empty())
            {
                lines.push_back(currentLine);
                currentLine.clear();
            }
            // A WORD WIDER THAN THE WHOLE LINE. Take the longest prefix that fits, one character at a
            // time; if not even one character fits, take it anyway, since a line that never advances
            // would loop forever.
            while (!fits(word))
            {
                size_t cut = 0;
                while (cut < word.size())
                {
                    const size_t candidateCut = nextCharacterEnd(word, cut);
                    if (!fits(word.substr(0, candidateCut)))
                        break;
                    cut = candidateCut;
                }
                if (cut == 0)
                    cut = nextCharacterEnd(word, 0);
                lines.push_back(word.substr(0, cut));
                word.erase(0, cut);
            }
            currentLine = word;
        }
        if (!currentLine.empty())
            lines.push_back(currentLine);
        return lines;
    }

    // Lay out a "Btn: Label  |  Btn: Label" hint as badge+label pairs, centred within [x, x+w] on
    // `cy`. A segment with no colon (e.g. "HOLDING") is a state marker and renders as accent text.
    // Shared by the screen nav bar and the dialog footer so the two always match.
    inline void drawNavHints(PKSEFramebuffer &framebuffer, int hintsX, int hintsWidth, int centerY,
                             const std::string &hint)
    {
        struct HintSegment
        {
            std::string buttonToken, label;
            int glyphWidth, labelWidth;
        };

        // Whoever draws last owns the taps, so an open modal's footer replaces the nav bar behind it
        // rather than leaving the background live. Clearing here (not in drawNavBar) also means the
        // list can't grow across frames if a screen ever draws a footer without a nav bar.
        g_navHits.clear();

        auto trim = [](const std::string &s)
        {
            const size_t firstNonSpace = s.find_first_not_of(" \t");
            if (firstNonSpace == std::string::npos)
                return std::string();
            return s.substr(firstNonSpace, s.find_last_not_of(" \t") - firstNonSpace + 1);
        };

        std::vector<HintSegment> segments;
        for (size_t hintIndex = 0; hintIndex <= hint.size();)
        {
            const size_t barPosition = hint.find('|', hintIndex);
            const std::string token = trim(
                hint.substr(hintIndex, barPosition == std::string::npos ? std::string::npos : barPosition - hintIndex));
            if (!token.empty())
            {
                HintSegment segment{};
                const size_t colon = token.find(':');
                if (colon == std::string::npos)
                {
                    segment.label = token;
                }
                else
                {
                    segment.buttonToken = trim(token.substr(0, colon));
                    segment.label = trim(token.substr(colon + 1));
                }
                segment.glyphWidth =
                    segment.buttonToken.empty() ? 0 : buttonGlyphWidth(framebuffer, segment.buttonToken);
                // A button we have no badge for still has to be readable: fall back to plain text
                // rather than silently dropping the button name and leaving a bare verb.
                if (segment.glyphWidth == 0 && !segment.buttonToken.empty())
                    segment.label = segment.buttonToken + ": " + segment.label;
                int textHeight;
                framebuffer.measureText(segment.label, segment.labelWidth, textHeight, TextStyle::Caption);
                segments.push_back(segment);
            }
            if (barPosition == std::string::npos)
                break;
            hintIndex = barPosition + 1;
        }
        if (segments.empty())
            return;

        constexpr int glyphGap = 8, maxItemGap = 26, minItemGap = 10, horizontalPadding = 20;
        int fixed = 0;
        for (const HintSegment &segment : segments)
            fixed += segment.glyphWidth + (segment.glyphWidth ? glyphGap : 0) + segment.labelWidth;

        const int count = static_cast<int>(segments.size());
        int itemGap = maxItemGap;
        if (count > 1)
            itemGap = std::clamp((hintsWidth - horizontalPadding * 2 - fixed) / (count - 1), minItemGap, maxItemGap);

        int centerX = hintsX + std::max(horizontalPadding, (hintsWidth - (fixed + itemGap * (count - 1))) / 2);
        for (const HintSegment &segment : segments)
        {
            const int segmentX = centerX;
            if (segment.glyphWidth)
            {
                buttonGlyph(framebuffer, centerX, centerY, segment.buttonToken, false);
                centerX += segment.glyphWidth + glyphGap;
            }
            int textWidth, textHeight;
            framebuffer.measureText(segment.label, textWidth, textHeight, TextStyle::Caption);
            framebuffer.drawText(centerX, centerY - textHeight / 2, segment.label,
                                 segment.glyphWidth ? Colors::Text : Colors::Accent, TextStyle::Caption);
            centerX += segment.labelWidth;
            // The badge AND its label are one tap target -- aiming at a 24px circle is unreasonable,
            // and the label is the part that says what will happen. Height is TouchTargetMin rather
            // than the bar height (46), so the target stays fingertip-sized.
            const uint64_t button = segment.glyphWidth ? navButtonFor(segment.buttonToken) : 0;
            if (button)
                g_navHits.push_back(
                    {segmentX, centerY - TouchTargetMin / 2, centerX - segmentX, TouchTargetMin, button});
            centerX += itemGap;
        }
    }

    // Top title bar: "PKSE" + a subtitle on a sheet that curves along its bottom edge.
    inline void drawTitleBar(PKSEFramebuffer &framebuffer, const std::string &subtitle)
    {
        framebuffer.drawSoftShadow(0, -40, framebuffer.getWidth(), HEADER_HEIGHT + 40, CHROME_CORNER_RADIUS);
        framebuffer.drawFilledRoundedRect(0, -CHROME_CORNER_RADIUS, framebuffer.getWidth(),
                                          HEADER_HEIGHT + CHROME_CORNER_RADIUS, CHROME_CORNER_RADIUS, Colors::Panel);
        framebuffer.drawText(20, 8, "PKSE", Colors::Accent, TextStyle::Title);
        int boxWidth, bh;
        framebuffer.measureText("PKSE", boxWidth, bh, TextStyle::Title);
        // Short accent underline beneath the wordmark. A full-width rule cannot work against a curved
        // sheet: a straight line would cut across the corners.
        framebuffer.drawFilledRoundedRect(20, 52, boxWidth, 3, 2, Colors::Accent);
        if (!subtitle.empty())
            framebuffer.drawText(20 + boxWidth + 16, 24, subtitle, Colors::TextDim, TextStyle::Body);
#ifdef PKSE_DEVELOPMENT_BUILD
        // Every screen of a development build says so, at the right end of the bar, so a
        // screenshot or a console left running is never mistaken for a release.
        const std::string developmentLabel = "DEVELOPMENT";
        int labelWidth, labelHeight;
        framebuffer.measureText(developmentLabel, labelWidth, labelHeight, TextStyle::Heading);
        framebuffer.drawText(framebuffer.getWidth() - 20 - labelWidth, (HEADER_HEIGHT - labelHeight) / 2,
                             developmentLabel, Colors::Danger, TextStyle::Heading);
#endif
    }

    // Bottom nav bar: a sheet that curves along its top edge, carrying the controller badges.
    inline void drawNavBar(PKSEFramebuffer &framebuffer, const std::string &hint)
    {
        const int screenWidth = framebuffer.getWidth(), barY = framebuffer.getHeight() - NAV_BAR_HEIGHT;
        framebuffer.drawSoftShadow(0, barY, screenWidth, NAV_BAR_HEIGHT + 40, CHROME_CORNER_RADIUS);
        framebuffer.drawFilledRoundedRect(0, barY, screenWidth, NAV_BAR_HEIGHT + CHROME_CORNER_RADIUS,
                                          CHROME_CORNER_RADIUS, Colors::Panel);
        drawNavHints(framebuffer, 0, screenWidth, barY + NAV_BAR_HEIGHT / 2, hint);
    }

    // A HOME-style selectable list tile: rounded (stadium), soft shadow, amber when
    // selected. `accent` marks a special/primary row (indigo-tinted when not selected, e.g. the
    // "Create New Backup" action); `enabled` false dims it (e.g. a "no saves" placeholder).
    inline void drawHomeTile(PKSEFramebuffer &framebuffer, int homeTileX, int homeTileY, int homeTileWidth,
                             int homeTileHeight, const std::string &label, bool selected, bool accent = false,
                             bool enabled = true)
    {
        framebuffer.drawSoftShadow(homeTileX, homeTileY, homeTileWidth, homeTileHeight, homeTileHeight / 2);
        const Color fillColor = selected ? Colors::Primary : Colors::PanelAlt;
        framebuffer.drawPill(homeTileX, homeTileY, homeTileWidth, homeTileHeight, fillColor);
        // An accent (primary-action) row is a normal tile with accent text + a thin accent outline —
        // NOT a filled highlight, which would read as a false selection next to the real (amber) one.
        if (accent && !selected)
            framebuffer.drawPillBorder(homeTileX, homeTileY, homeTileWidth, homeTileHeight, Colors::Accent, 2);
        const Color textColor2 = !enabled   ? Colors::TextDim
                          : selected ? Colors::PrimaryText
                          : accent   ? Colors::Accent
                                     : Colors::Text;
        int labelX = homeTileX + 28;
        if (selected)
        {
            framebuffer.drawSymbol(homeTileX + 20, homeTileY + homeTileHeight / 2 - 12, "\xE2\x96\xB6",
                                   Colors::PrimaryText);
            labelX = homeTileX + 48;
        }
        int labelWidth, lh;
        framebuffer.measureText(label, labelWidth, lh, TextStyle::Body);
        framebuffer.drawText(labelX, homeTileY + (homeTileHeight - lh) / 2, label, textColor2, TextStyle::Body);
    }

    // A thin scrollbar thumb on a scrolling viewport's right edge, drawn ONLY when the content
    // overflows. All pixels: `x` is the thumb's left edge, [trackY, trackY + trackH] the viewport,
    // contentH the full content height, scroll the current offset. Item lists pass contentH =
    // totalItems * rowH and scroll = firstItem * rowH. One helper so every scrolling surface in the
    // app gets the same thumb the details editor uses.
    inline void drawScrollbar(PKSEFramebuffer &framebuffer, int trackX, int trackY, int trackH, int contentH,
                              int scroll)
    {
        if (contentH <= trackH || trackH <= 0)
            return;
        const int maxS = contentH - trackH;
        int scrollOffset = scroll;
        if (scrollOffset < 0)
            scrollOffset = 0;
        if (scrollOffset > maxS)
            scrollOffset = maxS;
        const int thumbH = std::max(24, trackH * trackH / contentH);
        const int thumbY = trackY + (trackH - thumbH) * scrollOffset / maxS;
        framebuffer.drawFilledRoundedRect(trackX, thumbY, 3, thumbH, 2, Colors::Border);
    }
}

#endif
