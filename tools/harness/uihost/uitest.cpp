/**
 * The three existing tiers all stop short of the application. syntax_check answers "does it
 * compile", the harnesses answer "is the byte work right", and the self test answers "is the data
 * right" -- but every one of them calls functions directly. Nothing until now pressed a button. A
 * screen you can enter but not leave, a label drawn off its panel, a pokemon rendered into an empty
 * slot, a button that quietly stopped doing anything: none of that is visible to a test that never
 * runs update() and draw().
 *
 * This runs the shipped screens, panels, dialogs and modals -- the same sources the .nro compiles --
 * against real saves, with a synthesized PadState and a framebuffer that records primitives instead
 * of rasterizing them. See tools/harness/uihost/ for the two substitutions and the one fidelity
 * caveat (text advance widths are averaged, so text-driven layout lands within a few pixels rather
 * than exactly).
 *
 *   ./tools/harness/build-uitest.sh [report-path] [save-root]
 *
 * Findings go through Test::Report, so they group and print like every other suite. Exits non-zero
 * if any check fails.
 */
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <set>
#include <span>
#include <type_traits>
#include <string>
#include <vector>

#include "../SaveTree.h" // saves are found by their TITLE FOLDER, not by a guessed filename

#include "Enums/GameVersion.h"
#include "Conversion/Convert.h"
#include "Encryption/Encryption.h"
#include "Enums/LanguageID.h"
#include "Pokemon/Pokemon7USUM.h"
#include "Test/TestReport.h"
#include "Test/TestSuite.h"
#include "Trainer/Bank.h"
#include "Trainer/Trainer.h"
#include "Trainer/Trainer1RBY.h"
#include "Trainer/Trainer2GSC.h"
#include "Trainer/Trainer3FRLG.h"
#include "Trainer/Trainer4DP.h"
#include "Trainer/Trainer4PT.h"
#include "Trainer/Trainer4HGSS.h"
#include "Trainer/Trainer5BW.h"
#include "Trainer/Trainer5B2W2.h"
#include "Trainer/Trainer6XY.h"
#include "Trainer/Trainer6ORAS.h"
#include "Trainer/Trainer7SM.h"
#include "Trainer/Trainer7USUM.h"
#include "Trainer/Trainer8SWSH.h"
#include "Trainer/Trainer7LGPE.h"
#include "Test/TestRunner.h"
#include "Trainer/Trainer8LA.h"
#include "Trainer/Trainer9LZA.h"
#include "Names/LocationNames.h" // the met-list dead-end hint is checked against the egg-source table
#include "Names/ItemPouches.h"   // Add All Items is checked against the same legal lists
#include "Trainer/Inventory8SWSH.h"
#include "Trainer/Inventory9SV.h"
#include "Trainer/Trainer3RSE.h"
#include "Trainer/Trainer8BDSP.h"
#include "Trainer/Trainer9SV.h"
#include "UI/ListSearch.h"
#include "UI/Common.h"
#include "UI/ScreenChrome.h" // NAV_BAR_HEIGHT -- the one-nav-bar rule is checked against its geometry
#include "UI/Dialogs/FileBrowserDialog.h"
#include "UI/Dialogs/KeyboardDialog.h"
#include "UI/Dialogs/StatEditDialog.h"
#include "Globals.h"         // g_allowIllegalEdits -- the Hyper Training row gives way to it
#include "Utils/Settings.h"
#include "Utils/StringHelpers.h"
#include "Save/RtcFooter.h"
#include "Save/SaveFileName.h"
#include "Save/SaveEnvelope.h"
#include "UI/SpriteManager.h"
#include "UI/TrainerViewScreen.h"

#include "HostInput.h"
#include "UIDriver.h"
#include <sys/stat.h>   // the browser suite builds a fixture directory
#include <unistd.h>    // rmdir

using UI::TrainerViewScreen;
using UIHost::Driver;
namespace Btn {
    constexpr u64 A = HidNpadButton_A,     B     = HidNpadButton_B;
    constexpr u64 X = HidNpadButton_X,     Y     = HidNpadButton_Y;
    constexpr u64 Up = HidNpadButton_Up,   Down  = HidNpadButton_Down;
    constexpr u64 Left = HidNpadButton_Left, Right = HidNpadButton_Right;
    constexpr u64 L = HidNpadButton_L,     R     = HidNpadButton_R;
    constexpr u64 ZL = HidNpadButton_ZL,   ZR    = HidNpadButton_ZR;
    constexpr u64 Plus = HidNpadButton_Plus, Minus = HidNpadButton_Minus;
}

namespace {

    std::vector<uint8_t> readFile(const std::string& path) {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return {};
        std::fseek(f, 0, SEEK_END);
        const long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        std::vector<uint8_t> out(n > 0 ? (size_t)n : 0);
        if (!out.empty() && std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
        std::fclose(f);
        return out;
    }

    /// Walk the HOME menu to `index` from wherever the cursor is, then confirm the cursor arrived.
    /// The menu is a 3-pill column (0..2) with a 3-icon row under it (3..5), so reaching an icon
    /// means going down the column first and then along the row.
    /// How many bottom nav bars this frame drew. There must never be more than one.
    ///
    /// The bar is a full-width rounded sheet pinned to the bottom edge (UI::drawNavBar), so it has a
    /// signature no other primitive shares. Counting it is the only way to catch the failure that
    /// prompted this: a dialog printing its own strip of controller badges directly above the
    /// screen's, listing the same keys twice -- and, in the save picker's case, listing them
    /// DIFFERENTLY. Nothing else in the harness would have noticed, because both bars drew fine.
    int navBarCount(const UIHost::Frame& frame, int screenWidth, int screenHeight) {
        int count = 0;
        for (const UIHost::Draw* shape : frame.ops("filledRoundedRect")) {
            if (shape->drawX == 0 && shape->drawWidth == screenWidth && shape->drawY == screenHeight - UI::NAV_BAR_HEIGHT)
                ++count;
        }
        return count;
    }

    /// Assert the one-bar rule at a moment when some overlay owns input. `where` names that moment.
    ///
    /// The bar must also be drawn AFTER every full-screen scrim. Each dialog dims the whole screen
    /// behind it, and a bar drawn before that was dimmed with everything else -- so the keys the
    /// dialog owns read as the screen's behind it. The save picker's file browser shipped that way.
    void expectOneNavBar(Driver& d, const char* where) {
        const UIHost::Frame& frame = d.frameData();
        const int bars = navBarCount(frame, 1280, 720);
        d.expect(bars == 1, "the frame drew more than one nav bar; controls are listed twice", std::string(where) + " bars=" + std::to_string(bars));
        int lastBarDraw = -1;
        int lastScrimDraw = -1;
        for (int drawIndex = 0; drawIndex < static_cast<int>(frame.draws.size()); ++drawIndex) {
            const UIHost::Draw& draw = frame.draws[drawIndex];
            if (draw.kind != UIHost::Kind::Shape || !draw.visible) continue;
            if (std::strcmp(draw.op, "filledRoundedRect") == 0 && draw.drawX == 0 && draw.drawWidth == 1280 && draw.drawY == 720 - UI::NAV_BAR_HEIGHT)
                lastBarDraw = drawIndex;
            if (std::strcmp(draw.op, "filledRect") == 0 && draw.drawX == 0 && draw.drawY == 0 && draw.drawWidth == 1280 && draw.drawHeight == 720 && draw.color.alpha < 255)
                lastScrimDraw = drawIndex;
        }
        d.expect(lastBarDraw > lastScrimDraw, "the nav bar is under a dialog's scrim, so the dialog's keys are dimmed with the screen behind it", std::string(where) + " barDraw=" + std::to_string(lastBarDraw) + " scrimDraw=" + std::to_string(lastScrimDraw));
    }

    void gotoHome(Driver& d, TrainerViewScreen& s, int index) {
        d.press(Btn::Up, 6);                       // back to the top of the pill column
        if (index <= 2) {
            d.press(Btn::Down, index);
        } else {
            d.press(Btn::Down, 3);                 // 0 -> 1 -> 2 -> icon row (lands on 3)
            d.press(Btn::Right, index - 3);
        }
        d.expect(s.homeMenuIndex == index, "the HOME menu cursor did not reach the requested entry", "wanted=" + std::to_string(index) + " got=" + std::to_string(s.homeMenuIndex));
    }

    const char* modeName(TrainerViewScreen::ViewMode m) {
        switch (m) {
            case TrainerViewScreen::ViewMode::Party:    return "Party";
            case TrainerViewScreen::ViewMode::Boxes:    return "Boxes";
            case TrainerViewScreen::ViewMode::Items:    return "Items";
            case TrainerViewScreen::ViewMode::Storage:  return "Storage";
            case TrainerViewScreen::ViewMode::Trainer:  return "Trainer";
            default:                                    return "Settings";
        }
    }

    /// The screen renders at all, and lands on the HOME menu rather than inside a view.
    void testOpens(Test::Context& context, TrainerViewScreen& s, Trainer::Trainer& tr) {
        Driver d(context, s, "open");
        d.step("open the editor").frame(2);
        d.expect(!d.frameData().draws.empty(), "the editor drew nothing on its first frames");
        d.expect(!s.detailViewActive, "the editor opened inside a view instead of on the HOME menu");
        // The HOME menu's live box-preview card has a sprite to draw only when the box it shows is not empty.
        bool previewHasPokemon = false;
        const int box = s.selectedBoxIndex;
        if (box >= 0 && box < static_cast<int>(tr.boxes.size())) {
            for (int slot = 0; slot < static_cast<int>(tr.getSlotsPerBox()); ++slot)
                if (!Test::isEmptySlot(tr.boxes[box][slot].get())) { previewHasPokemon = true; break; }
        }
        if (previewHasPokemon)
            d.seeSprites(1);
        else
            context.note("this save's current box is empty, so the HOME box preview's sprites were not checked", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
    }

    /// Every HOME destination opens AND closes again. The failure this exists for is a view you can
    /// enter and not get out of, which no logic test can see and which strands the user completely.
    void testEveryDestinationOpensAndCloses(Test::Context& context, TrainerViewScreen& s) {
        using VM = TrainerViewScreen::ViewMode;
        const VM expect[6] = { VM::Boxes, VM::Party, VM::Storage, VM::Items, VM::Trainer, VM::Settings };

        for (int i = 0; i < 6; ++i) {
            Driver d(context, s, std::string("enter-") + modeName(expect[i]));
            d.step(std::string("walk to HOME entry ") + std::to_string(i));
            gotoHome(d, s, i);
            d.expect(s.selectedMode == expect[i], "focusing a HOME entry selected the wrong view", std::string("entry=") + std::to_string(i) + " want=" + modeName(expect[i]) + " got=" + modeName(s.selectedMode));

            d.step(std::string("open ") + modeName(expect[i])).press(Btn::A).frame();
            if (!d.expect(s.detailViewActive, "pressing A on a HOME entry did not open its view", std::string("view=") + modeName(expect[i])))
                continue;

            d.step(std::string("leave ") + modeName(expect[i])).press(Btn::B).frame(2);
            d.expect(!s.detailViewActive, "a view could be entered but not left with B", std::string("view=") + modeName(expect[i]));
            d.expect(!s.goBack, "leaving a view exited the whole editor instead of returning to HOME", std::string("view=") + modeName(expect[i]));
        }
    }

    /// The box grid's real geometry, read from the hit-test rectangles the panel publishes.
    ///
    /// This is the rule CLAUDE.md singles out as easy to get wrong and invisible until it is on a
    /// screen: five rows always, a width that follows the box size, and a narrow box CENTRED at the
    /// normal cell pitch rather than stretched to fill the panel. Stretching is the obvious wrong
    /// implementation, and it fails exactly here -- uniform pitch and column count.
    void testBoxGridGeometry(Test::Context& context, TrainerViewScreen& s, Trainer::Trainer& tr) {
        Driver d(context, s, "box-grid");
        d.step("open the box view");
        gotoHome(d, s, 0);
        d.press(Btn::A).frame();
        if (!d.expect(s.detailViewActive, "could not open the box view")) return;

        const int slots = static_cast<int>(tr.getSlotsPerBox());
        const int wantCols = UI::boxGridColumns(slots);

        std::vector<UI::TouchButton> cells;
        for (const auto& b : s.touchButtons)
            if (b.buttonId >= 0 && b.buttonId < slots) cells.push_back(b);

        if (!d.expect(static_cast<int>(cells.size()) == slots, "the box grid did not lay out one cell per slot", "slots=" + std::to_string(slots) + " cells=" + std::to_string(cells.size())))
            return;

        std::set<int> xs, ys, ws, hs;
        for (const auto& c : cells) {
            xs.insert(c.hitX);
            ys.insert(c.hitY);
            ws.insert(c.hitWidth);
            hs.insert(c.hitHeight);
        }

        d.expect(static_cast<int>(xs.size()) == wantCols, "the box grid has the wrong column count", "slots=" + std::to_string(slots) + " want=" + std::to_string(wantCols) + " got=" + std::to_string(xs.size()));
        d.expect(ys.size() == static_cast<size_t>(UI::BOX_GRID_ROWS), "the box grid is not five rows", "rows=" + std::to_string(ys.size()));
        // One pitch for every cell. A stretched narrow grid produces cells wider than the bank's,
        // which is what makes the cursor change size as it crosses panes in the storage view.
        d.expect(ws.size() == 1 && hs.size() == 1, "box grid cells are not a uniform size (a narrow grid was stretched, not centred)", "distinctW=" + std::to_string(ws.size()) + " distinctH=" + std::to_string(hs.size()));
    }

    /// A sprite is only drawn where a Pokemon actually is.
    ///
    /// The visual half of the rule that a non-null box slot is not an occupied one: several games
    /// keep a live blank entity in every empty slot on purpose. Counting sprites whose centre lands
    /// in a grid cell against the slots that hold a real species catches a panel that renders the
    /// blanks -- which reads to the user as thirty Pokemon in an empty box.
    void testNoSpritesInEmptySlots(Test::Context& context, TrainerViewScreen& s, Trainer::Trainer& tr) {
        Driver d(context, s, "empty-slots");
        d.step("open the box view");
        gotoHome(d, s, 0);
        d.press(Btn::A).frame(2);
        if (!d.expect(s.detailViewActive, "could not open the box view")) return;

        const int slots = static_cast<int>(tr.getSlotsPerBox());
        const int box   = s.selectedBoxIndex;
        if (box < 0 || box >= static_cast<int>(tr.boxes.size())) return;

        int occupied = 0;
        for (int i = 0; i < slots && i < static_cast<int>(Trainer::BOX_SLOTS); ++i) {
            const auto& pk = tr.boxes[box][i];
            if (pk && pk->speciesID() != 0) ++occupied;
        }

        std::vector<UI::TouchButton> cells;
        for (const auto& b : s.touchButtons)
            if (b.buttonId >= 0 && b.buttonId < slots) cells.push_back(b);
        if (cells.empty()) return;

        int inCells = 0;
        for (const UIHost::Draw& dr : d.frameData().draws) {
            if (dr.kind != UIHost::Kind::Sprite || !dr.visible) continue;
            const int cx = dr.centerX(), cy = dr.centerY();
            for (const auto& c : cells)
                if (cx >= c.hitX && cx < c.hitX + c.hitWidth && cy >= c.hitY && cy < c.hitY + c.hitHeight) {
                    ++inCells;
                    break;
                }
        }

        d.expect(inCells <= occupied, "the box grid drew more Pokemon than the box holds (empty slots were rendered)", "box=" + std::to_string(box + 1) + " occupied=" + std::to_string(occupied) + " spritesInCells=" + std::to_string(inCells));
    }

    /// THE SUMMARY PANEL CARRIES THE LEGALITY VERDICT, bottom-left, and it must not land on top of
    /// the centred "A / tap: Edit" hint it shares that row with. Both are drawn from measured text
    /// into a fixed 452px panel, so whether they fit is arithmetic nobody can eyeball from the
    /// source -- and an overlap is the kind of defect that only shows up for the one Pokemon whose
    /// issue count pushes the label a few pixels wider.
    void testSummaryShowsLegality(Test::Context& context, TrainerViewScreen& s, Trainer::Trainer& tr) {
        Driver d(context, s, "summary-legality");
        d.step("open the box view");
        gotoHome(d, s, 0);
        d.press(Btn::A).frame(2);
        if (!d.expect(s.detailViewActive, "could not open the box view")) return;

        // The panel only resolves a Pokemon for an occupied slot; on an empty one it draws
        // "No Pokemon selected" and there is no verdict to show, which is correct.
        int occupied = -1;
        const int box = s.selectedBoxIndex;
        if (box >= 0 && box < static_cast<int>(tr.boxes.size())) {
            for (int slot = 0; slot < static_cast<int>(tr.getSlotsPerBox()); ++slot)
                if (!Test::isEmptySlot(tr.boxes[box][slot].get())) { occupied = slot; break; }
        }
        if (occupied < 0) {
            context.note("this save's first box is empty, so the summary verdict was not exercised", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }
        s.selectedItemIndex = occupied;
        d.step("the summary panel names the verdict").frame();

        const UIHost::Draw* verdict = d.frameData().first("Legality:");
        if (!d.expect(verdict != nullptr, "the summary panel drew no legality verdict for an occupied slot", "box=" + std::to_string(box + 1) + " slot=" + std::to_string(occupied + 1)))
            return;

        const UIHost::Draw* hint = d.frameData().first("A / tap: Edit");
        if (!d.expect(hint != nullptr, "the summary panel drew no edit hint to place the verdict against"))
            return;

        // Bottom-LEFT: left of the centred hint, on the same row, and clear of it.
        d.expect(verdict->drawX < hint->drawX, "the legality verdict is not in the summary panel's bottom-left corner", "verdictX=" + std::to_string(verdict->drawX) + " hintX=" + std::to_string(hint->drawX));
        d.expect(verdict->right() < hint->drawX, "the legality verdict overlaps the edit hint it shares the bottom row with", "verdict=\"" + verdict->text + "\" right=" + std::to_string(verdict->right()) + " hintX=" + std::to_string(hint->drawX));
        d.expect(verdict->drawY == hint->drawY, "the legality verdict is not on the summary panel's bottom row", "verdictY=" + std::to_string(verdict->drawY) + " hintY=" + std::to_string(hint->drawY));
    }

    /// UI::wrapTextToWidth, asked directly. The overlay check below can only bite on whatever its
    /// save's Pokemon happen to say, so the helper's own contract is pinned here on inputs chosen to
    /// reach every branch: an ordinary sentence, a word wider than the whole line, and text with no
    /// spaces at all whose characters are three bytes each.
    void testWrapTextToWidth(Test::Context& context) {
        UI::PKSEFramebuffer framebuffer;
        constexpr int maxLineWidth = 300;
        const auto measuredWidth = [&](const std::string& line) {
            int lineWidth = 0, lineHeight = 0;
            framebuffer.measureText(line, lineWidth, lineHeight, UI::TextStyle::Caption);
            return lineWidth;
        };
        // Every character whole: no line may begin on a continuation byte or stop partway through
        // a sequence. Half a character draws as nothing, which is the failure to catch.
        const auto isWholeUtf8 = [](const std::string& line) {
            size_t byteIndex = 0;
            while (byteIndex < line.size()) {
                const unsigned char lead = static_cast<unsigned char>(line[byteIndex]);
                const size_t sequenceLength = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
                if (sequenceLength == 0 || byteIndex + sequenceLength > line.size()) return false;
                for (size_t continuation = 1; continuation < sequenceLength; ++continuation)
                    if ((static_cast<unsigned char>(line[byteIndex + continuation]) & 0xC0) != 0x80) return false;
                byteIndex += sequenceLength;
            }
            return true;
        };
        const auto withoutSpaces = [](std::string value) {
            value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
            return value;
        };

        struct WrapCase { const char* label; std::string text; };
        const WrapCase cases[] = {
            {"sentence", "Nothing in FireRed produces Seadra at level 15: it would have to evolve from Horsea at level 32"},
            {"unbreakable word", "Location " + std::string(120, 'W') + " is not a place"},
            {"no spaces, multi-byte", "\xE3\x83\x94\xE3\x82\xAB\xE3\x83\x81\xE3\x83\xA5\xE3\x82\xA6"
                                      "\xE3\x81\xAF\xE3\x83\x88\xE3\x82\xAD\xE3\x83\xAF\xE3\x81\xAE"
                                      "\xE3\x82\x82\xE3\x82\x8A\xE3\x81\xA7\xE3\x81\xA4\xE3\x81\x8B"
                                      "\xE3\x81\xBE\xE3\x81\x88\xE3\x82\x8B\xE3\x81\x93\xE3\x81\xA8"},
        };
        for (const WrapCase& wrapCase : cases) {
            const std::string where = std::string("case=") + wrapCase.label;
            const std::vector<std::string> lines = UI::wrapTextToWidth(framebuffer, wrapCase.text, maxLineWidth, UI::TextStyle::Caption);
            context.check(lines.size() > 1, "text far wider than the line came back on one line", where + " lines=" + std::to_string(lines.size()));
            std::string rejoined;
            for (const std::string& line : lines) {
                context.check(!line.empty(), "wrapping produced an empty line", where);
                context.check(measuredWidth(line) <= maxLineWidth, "a wrapped line is wider than the width it was wrapped to", where + " width=" + std::to_string(measuredWidth(line)) + " line=\"" + line + "\"");
                context.check(isWholeUtf8(line), "wrapping cut a multi-byte character in half", where);
                context.check(line.front() != ' ' && line.back() != ' ', "a wrapped line starts or ends on the space it was broken at", where + " line=\"" + line + "\"");
                rejoined += line;
            }
            // NOTHING LOST, NOTHING REORDERED -- spaces aside, the lines are the input.
            context.check(withoutSpaces(rejoined) == withoutSpaces(wrapCase.text), "wrapping lost or reordered part of the text", where);
        }

        // A WORD IS ONLY EVER BROKEN WHEN IT CANNOT FIT WHOLE. The sentence has no word anywhere near
        // the line's width, so every one of its words must survive intact on some line.
        const std::vector<std::string> sentenceLines = UI::wrapTextToWidth(framebuffer, cases[0].text, maxLineWidth, UI::TextStyle::Caption);
        std::vector<std::string> wrappedWords;
        for (const std::string& line : sentenceLines) {
            size_t wordStart = 0;
            while (wordStart <= line.size()) {
                size_t wordEnd = line.find(' ', wordStart);
                if (wordEnd == std::string::npos) wordEnd = line.size();
                wrappedWords.push_back(line.substr(wordStart, wordEnd - wordStart));
                wordStart = wordEnd + 1;
            }
        }
        std::vector<std::string> inputWords;
        for (size_t wordStart = 0; wordStart <= cases[0].text.size();) {
            size_t wordEnd = cases[0].text.find(' ', wordStart);
            if (wordEnd == std::string::npos) wordEnd = cases[0].text.size();
            inputWords.push_back(cases[0].text.substr(wordStart, wordEnd - wordStart));
            wordStart = wordEnd + 1;
        }
        context.check(wrappedWords == inputWords, "wrapping broke a word that fitted on a line of its own", "words in=" + std::to_string(inputWords.size()) + " out=" + std::to_string(wrappedWords.size()));

        // Text that fits comes back exactly as it went in, as one line.
        const std::vector<std::string> shortLines = UI::wrapTextToWidth(framebuffer, "Matches a wild encounter", maxLineWidth, UI::TextStyle::Caption);
        context.check(shortLines.size() == 1 && shortLines[0] == "Matches a wild encounter", "text that fits the line was changed", "lines=" + std::to_string(shortLines.size()));
    }

    /// THE LEGALITY OVERLAY KEEPS ITS TEXT INSIDE ITS CARD. Drawing each message as one line runs
    /// Layer 3's -- a game, a species, a place and a level -- straight out of the card's right edge.
    /// Whether a line fits is measured text in a fixed-width card, which nobody can eyeball from the
    /// source, so every occupied slot of the first box is opened and every line of its report is held
    /// to the card's edges.
    ///
    /// Real Pokemon alone cannot be relied on to say anything long -- three of four test saves' first
    /// boxes did not -- so one is also POSED with an impossible origin, which every format with a later
    /// generation above it reports in a sentence far wider than the card's message column.
    void testLegalityOverlayStaysInCard(Test::Context& context, TrainerViewScreen& s, Trainer::Trainer& tr) {
        Driver d(context, s, "legality-overlay");
        d.step("open the box view");
        gotoHome(d, s, 0);
        d.press(Btn::A).frame();
        if (!d.expect(s.detailViewActive, "could not open the box view")) return;

        const int box = s.selectedBoxIndex;
        if (box < 0 || box >= static_cast<int>(tr.boxes.size())) return;

        // Reads the frame just drawn. Returns how many CONTINUATION lines the overlay drew -- message
        // lines beyond one per issue -- or -1 when the frame could not be read, already recorded.
        const auto checkOverlayFrame = [&](const std::string& where) -> int {
            const std::vector<UIHost::Draw>& draws = d.frameData().draws;
            int headingIndex = -1;
            for (int drawIndex = static_cast<int>(draws.size()) - 1; drawIndex >= 0 && headingIndex < 0; --drawIndex)
                if (draws[drawIndex].kind == UIHost::Kind::Text && draws[drawIndex].text == "Legality")
                    headingIndex = drawIndex;
            if (!d.expect(headingIndex >= 0, "the legality overlay drew no heading", where)) return -1;
            const UIHost::Draw& heading = draws[headingIndex];
            // The card is the nearest filled panel drawn before the heading that contains it -- the
            // details page's own left panel contains that point too, but is drawn much earlier.
            const UIHost::Draw* card = nullptr;
            for (int drawIndex = headingIndex - 1; drawIndex >= 0 && !card; --drawIndex) {
                const UIHost::Draw& candidate = draws[drawIndex];
                if (std::strcmp(candidate.op, "filledRoundedRect") == 0 && heading.drawX >= candidate.drawX && heading.drawX < candidate.right() && heading.drawY >= candidate.drawY && heading.drawY < candidate.bottom())
                    card = &candidate;
            }
            if (!d.expect(card != nullptr, "could not find the legality overlay's card", where)) return -1;

            // Each issue draws ONE tag, and every message line after its first sits in the message
            // column with no tag beside it -- which is how a wrapped issue is told apart.
            int tagCount = 0, messageLineCount = 0, messageX = -1;
            for (size_t drawIndex = static_cast<size_t>(headingIndex) + 1; drawIndex < draws.size(); ++drawIndex) {
                const UIHost::Draw& line = draws[drawIndex];
                if (line.kind != UIHost::Kind::Text || !line.visible) continue;
                if (line.drawX < card->drawX || line.drawX >= card->right() || line.drawY < card->drawY || line.drawY >= card->bottom())
                    continue;
                d.expect(line.right() <= card->right(), "a legality line runs out of the card's right edge", where + " right=" + std::to_string(line.right()) + " cardRight=" + std::to_string(card->right()) + " line=\"" + line.text + "\"");
                d.expect(line.bottom() <= card->bottom(), "a legality line runs out of the card's bottom edge", where + " bottom=" + std::to_string(line.bottom()) + " cardBottom=" + std::to_string(card->bottom()) + " line=\"" + line.text + "\"");
                if (line.text == "[note]" || line.text == "[warning]" || line.text == "[illegal]") {
                    ++tagCount;
                    if (messageX < 0 && drawIndex + 1 < draws.size()) messageX = draws[drawIndex + 1].drawX;
                    continue;
                }
                if (line.drawX == messageX && line.text.find("more not shown") == std::string::npos) ++messageLineCount;
            }
            return messageLineCount > tagCount ? messageLineCount - tagCount : 0;
        };

        int openedCount = 0, wrappedLineCount = 0, firstOccupied = -1;
        for (int slot = 0; slot < static_cast<int>(tr.boxes[box].size()); ++slot) {
            if (Test::isEmptySlot(tr.boxes[box][slot].get())) continue;
            if (firstOccupied < 0) firstOccupied = slot;
            const std::string where = "box=" + std::to_string(box + 1) + " slot=" + std::to_string(slot + 1);
            s.selectedItemIndex = slot;
            d.step("open the details page").press(Btn::A).frame(2);
            if (!d.expect(s.details.active, "A on an occupied slot did not open the details page", where)) return;
            s.details.legalityOverlay = true;
            d.step("open the legality overlay").frame();
            ++openedCount;
            const int continuationLines = checkOverlayFrame(where);
            if (continuationLines < 0) return;
            wrappedLineCount += continuationLines;
            s.details.legalityOverlay = false;
            d.step("close the details page").press(Btn::B).frame(2);
            if (!d.expect(!s.details.active, "B did not close an unedited details page", where)) return;
        }
        if (openedCount == 0) {
            context.note("this save's first box is empty, so the legality overlay was not exercised", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }
        if (wrappedLineCount == 0)
            context.note("no legality message in this save's first box was long enough to wrap", "title=\"" + Enums::getGameVersionName(context.version) + "\" opened=" + std::to_string(openedCount));

        // THE POSED PASS. An origin from a LATER generation than the record is reported as
        // "Origin game X is from Generation N, which this Generation M record cannot have come from",
        // which is wider than the message column in every format. The version has to survive the
        // format's own field: Gen 3 keeps four bits, so Legends: Z-A does not fit there and
        // HeartGold (7) does.
        const std::string where = "box=" + std::to_string(box + 1) + " slot=" + std::to_string(firstOccupied + 1) + " posed";
        s.selectedItemIndex = firstOccupied;
        d.step("open the details page on the Pokemon to pose").press(Btn::A).frame(2);
        if (!d.expect(s.details.active, "A on the Pokemon to pose did not open the details page", where)) return;

        // POSED ONLY ONCE THE PAGE IS OPEN, so the page's edit snapshot holds the real bytes: putting
        // the origin back then leaves the page clean, where posing first made the restore itself read
        // as an unsaved edit and B raised the discard prompt instead of closing. Restored the moment
        // the frame is drawn, before anything can return, because this is the trainer every later
        // test reads.
        ::Pokemon::Pokemon* posed = tr.boxes[box][firstOccupied].get();
        const int formatGeneration = Enums::getVersionGeneration(Enums::getGroupRepVersion(posed->getGameGroup()));
        const uint8_t originalOrigin = posed->originGame();
        bool originPosed = false;
        for (Enums::GameVersion laterVersion : {Enums::GameVersion::ZA, Enums::GameVersion::HG}) {
            if (!posed->hasOriginGame() || Enums::getVersionGeneration(static_cast<uint8_t>(laterVersion)) <= formatGeneration)
                continue;
            posed->setOriginGame(static_cast<uint8_t>(laterVersion));
            if (posed->originGame() == static_cast<uint8_t>(laterVersion)) { originPosed = true; break; }
            posed->setOriginGame(originalOrigin);
        }
        if (originPosed) {
            s.details.legalityOverlay = true;
            d.step("open the legality overlay on the posed Pokemon").frame();
            posed->setOriginGame(originalOrigin);

            const int posedContinuationLines = checkOverlayFrame(where);
            // "Origin game" opens the message, so it is on its first line whatever the wrap does --
            // a phrase from later in the sentence can be split across two lines and never match.
            d.expect(d.frameData().has("Origin game"), "the posed impossible origin was not reported, so the long-message check proved nothing", where);
            d.expect(posedContinuationLines > 0, "a message wider than the card was not wrapped onto a second line", where);
            s.details.legalityOverlay = false;
        } else {
            context.note("no later generation can be stamped on this format, so the long-message pose was skipped", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
        }
        d.step("close the details page").press(Btn::B).frame(2);
        d.expect(!s.details.active, "B did not close the details page after the pose was undone", where);
    }

    /// IN THE ITEMS POUCH, Y SEARCHES AND MINUS ADDS -- and neither key does the other's job. With
    /// Add Item on Y the search branch returns first: adding an item becomes unreachable while the nav
    /// bar goes on advertising it, and nothing presses either key to notice. So both are pressed, both
    /// of the bar's badges are read and tapped, and each key is checked for the OTHER's effect as well
    /// as its own.
    void testItemsKeys(Test::Context& context, TrainerViewScreen& s, Trainer::Trainer& tr) {
        Driver d(context, s, "items-keys");
        d.step("open the Items view");
        gotoHome(d, s, 3);
        d.press(Btn::A).frame();
        if (!d.expect(s.detailViewActive && s.selectedMode == TrainerViewScreen::ViewMode::Items, "could not open the Items view"))
            return;

        // THE BAR NAMES BOTH KEYS. Copied out of g_navHits, which is rebuilt on every frame.
        const auto badgeFor = [](u64 button, UI::NavHit& found) {
            for (const UI::NavHit& hit : UI::g_navHits)
                if (hit.button == button) { found = hit; return true; }
            return false;
        };
        // The badge's LABEL is the last text drawn inside its tap target -- a face-button badge
        // draws its own letter first.
        const auto labelOf = [&](const UI::NavHit& hit) {
            std::string label;
            for (const UIHost::Draw& draw : d.frameData().draws)
                if (draw.kind == UIHost::Kind::Text && draw.visible && draw.drawX >= hit.hitX && draw.right() <= hit.hitX + hit.hitWidth && draw.centerY() >= hit.hitY && draw.centerY() < hit.hitY + hit.hitHeight)
                    label = draw.text;
            return label;
        };
        UI::NavHit addBadge{}, searchBadge{};
        if (!d.expect(badgeFor(Btn::Minus, addBadge), "the Items nav bar has no Minus badge")) return;
        if (!d.expect(badgeFor(Btn::Y, searchBadge), "the Items nav bar has no Y badge")) return;
        d.expect(labelOf(addBadge) == "Add Item", "the Items nav bar's Minus badge does not say Add Item", "label=\"" + labelOf(addBadge) + "\"");
        d.expect(labelOf(searchBadge) == "Search", "the Items nav bar's Y badge does not say Search", "label=\"" + labelOf(searchBadge) + "\"");

        // MINUS OPENS THE ADD PICKER. A pouch can legitimately have nothing to add -- full, or already
        // holding every id it can -- and says so on the status line instead, so walk the pouches to
        // one that can take an item, and require at least a reason from every one that cannot.
        bool addPickerOpened = false;
        for (size_t pouchAttempt = 0; pouchAttempt < tr.items.size() && !addPickerOpened; ++pouchAttempt) {
            const std::string where = "pouch=" + std::to_string(s.selectedCategory);
            s.storageStatus.clear();
            d.step("press Minus in " + where).press(Btn::Minus).frame();
            d.expect(!s.itemSearch.isFiltering(), "Minus in the Items view started a search", where);
            if (s.pickerActive) {
                addPickerOpened = true;
                d.expect(s.pickerKind == s.pouchPickerKind(), "Minus opened a picker, but not the pouch's add-item picker", where);
                break;
            }
            d.expect(!s.storageStatus.empty(), "Minus in the Items view did nothing at all -- no add picker and no reason why", where);
            d.step("next pouch").press(Btn::R).frame();
        }
        if (addPickerOpened) {
            d.step("close the add picker").press(Btn::B).frame();
            d.expect(!s.pickerActive, "B did not close the add-item picker");

            // THE BADGE, TAPPED, DOES THE SAME: its label, the button it maps to and the handler all
            // have to agree, or a touch user finds a working key they cannot reach.
            d.step("tap the nav bar's Add Item badge").tap(addBadge.hitX + addBadge.hitWidth / 2, addBadge.hitY + addBadge.hitHeight / 2).frame(2);
            d.expect(s.pickerActive && s.pickerKind == s.pouchPickerKind(), "tapping the nav bar's Add Item badge did not open the add-item picker");
            if (s.pickerActive) d.press(Btn::B).frame();
        } else {
            context.note("no pouch in this save could take a new item, so the add-item picker was not opened", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
        }

        // Y SEARCHES, AND OPENS NOTHING ELSE.
        d.step("search the pouch with Y").type("a").press(Btn::Y).frame(2);
        d.expect(s.itemSearch.isFiltering(), "Y in the Items view did not start a search");
        d.expect(!s.pickerActive, "Y in the Items view opened the add-item picker");
        d.step("clear the search").type("").press(Btn::Y).frame(2);
        d.expect(!s.itemSearch.isFiltering(), "accepting an empty search did not clear the Items filter");

        d.step("tap the nav bar's Search badge").type("a").tap(searchBadge.hitX + searchBadge.hitWidth / 2, searchBadge.hitY + searchBadge.hitHeight / 2).frame(2);
        d.expect(s.itemSearch.isFiltering(), "tapping the nav bar's Search badge did not start a search");
        d.expect(!s.pickerActive, "tapping the nav bar's Search badge opened the add-item picker");
        d.step("clear the search").type("").press(Btn::Y).frame(2);
    }

    /// How a save is opened from its file, and how a trainer is written back and opened again, for the
    /// one format it holds. Add All Items checks what a RELOAD of the written save holds, because an
    /// item the writer drops on the floor is still in the trainer the editor added it to.
    struct SaveLoader {
        std::function<std::unique_ptr<Trainer::Trainer>()> open;
        std::function<std::unique_ptr<Trainer::Trainer>(Trainer::Trainer&)> writeAndReopen;
    };

    const Trainer::InventoryItem* findItem(const std::vector<Trainer::InventoryItem>& pouch, uint16_t itemId) {
        for (const Trainer::InventoryItem& item : pouch)
            if (item.itemId == itemId) return &item;
        return nullptr;
    }

    /// Every stack that has a count must come back from a reload with that count, and nothing the
    /// trainer does not hold may appear. Compared as a sorted list of {id, count} per pouch: a bag can
    /// hold two stacks of one item (a Crystal or Ruby bag past 99, a Sword/Shield bag past 999), and
    /// an id-indexed writer is free to read them back in id order.
    void expectItemsSurviveReload(Test::Context& context, const Trainer::Trainer& written, const Trainer::Trainer& reopened, const std::string& where) {
        if (!context.check(written.items.size() == reopened.items.size(), "a reload of the written save has a different number of pouches", where + " written=" + std::to_string(written.items.size()) + " reopened=" + std::to_string(reopened.items.size())))
            return;
        const auto stacksOf = [](const std::vector<Trainer::InventoryItem>& pouch) {
            std::vector<std::pair<uint16_t, uint16_t>> stacks;
            for (const Trainer::InventoryItem& item : pouch)
                if (item.count != 0) stacks.emplace_back(item.itemId, item.count);
            std::sort(stacks.begin(), stacks.end());
            return stacks;
        };
        int differingPouchCount = 0;
        std::string firstProblem;
        for (size_t pouchIndex = 0; pouchIndex < written.items.size(); ++pouchIndex) {
            const auto writtenStacks = stacksOf(written.items[pouchIndex]);
            const auto reopenedStacks = stacksOf(reopened.items[pouchIndex]);
            if (writtenStacks == reopenedStacks) continue;
            if (!differingPouchCount++) {
                size_t stackIndex = 0;
                while (stackIndex < writtenStacks.size() && stackIndex < reopenedStacks.size() && writtenStacks[stackIndex] == reopenedStacks[stackIndex]) ++stackIndex;
                const auto describe = [&](const std::vector<std::pair<uint16_t, uint16_t>>& stacks) {
                    return stackIndex < stacks.size() ? std::to_string(stacks[stackIndex].first) + "x" + std::to_string(stacks[stackIndex].second) : std::string("none");
                };
                firstProblem = "pouch=" + std::to_string(pouchIndex) + " wroteStacks=" + std::to_string(writtenStacks.size()) + " readStacks=" + std::to_string(reopenedStacks.size()) + " firstDifference wrote=" + describe(writtenStacks) + " read=" + describe(reopenedStacks);
            }
        }
        context.check(differingPouchCount == 0, "Add All Items did not survive writing the save and reading it back", where + " pouches=" + std::to_string(differingPouchCount) + " " + firstProblem);
    }

    /// What one item should hold after Add All Items gave `amount`, checked against the bag before it.
    /// The per-item ceilings PKHeX documents are spelled out here as well as in the screen, for the
    /// items that have one, so a rule dropped from itemMaxCount() is caught rather than agreed with.
    int expectedCeiling(Enums::GameVersion group, int pouchIndex, uint16_t itemId, int screenCeiling) {
        using Version = Enums::GameVersion;
        if (group == Version::GSC && itemId >= 243 && itemId <= 249) return 1;                  // HM01-HM07
        if (Enums::isGen3Group(group) && itemId >= 339 && itemId <= 346) return 1;             // HM01-HM08
        if ((group == Version::DP || group == Version::PT || group == Version::HGSS) && itemId >= 420 && itemId <= 427) return 1;
        if (group == Version::SWSH && pouchIndex == static_cast<int>(Trainer::PouchType8SWSH::TMs) && !(itemId >= 1130 && itemId < 1230)) return 1;
        if (group == Version::SV && pouchIndex == static_cast<int>(Trainer::PouchType9SV::Ingredients)) {
            const bool isPick = (itemId >= 2334 && itemId <= 2342) || (itemId >= 2385 && itemId <= 2394) || itemId == 2548;
            if (((itemId >= 2311 && itemId <= 2400) || (itemId >= 2417 && itemId <= 2437)) && !isPick) return 1;   // picnic accessories
        }
        if (group == Version::ZA && (itemId == 2612 || itemId == 2619)) return 0;               // Cherished Ring, Colorful Screw
        if ((group == Version::SM || group == Version::USUM) && itemId == 797) return 2;        // the Z-Ring, given twice
        if (Names::isKeyItem(group, itemId)) return 1;
        return screenCeiling;
    }

    /// Add All Items, driven through its dialog: the nav bar names it, ZR opens it, the amount and the
    /// key-item switch respond to keys and taps, B leaves the bag alone, and A gives every legal item
    /// what the issue asks for -- the amount, clamped to the item's limit, never lowering a stack,
    /// and no key items unless they were asked for. Then the save is written and read back.
    void testAddAllItems(Test::Report& report, const std::string& target, Enums::GameVersion group, const SaveLoader& loader) {
        Test::Context context;
        context.report = &report; context.suite = "ui"; context.target = target; context.group = group;
        report.setScope(target, "ui");
        std::printf("  add-all-items  %s\n", target.c_str());

        for (const bool withKeyItems : {false, true}) {
            std::unique_ptr<Trainer::Trainer> tr = loader.open();
            if (!context.check(tr != nullptr, "the save could not be opened for the Add All Items test")) return;
            context.version = tr->getGameVersion(); context.trainer = tr.get();
            const std::vector<std::vector<Trainer::InventoryItem>> itemsBefore = tr->items;
            TrainerViewScreen s(*tr, target, std::string("/tmp/pkse-uitest"), 0, AccountUid{}, false);
            const std::string pass = withKeyItems ? "keyItems=1" : "keyItems=0";
            Driver d(context, s, withKeyItems ? "add-all-items-with-key-items" : "add-all-items");

            d.step("open the Items view");
            gotoHome(d, s, 3);
            d.press(Btn::A).frame();
            if (!d.expect(s.detailViewActive && s.selectedMode == TrainerViewScreen::ViewMode::Items, "could not open the Items view", pass)) return;

            bool barNamesIt = false;
            for (const UI::NavHit& hit : UI::g_navHits)
                if (hit.button == Btn::ZR) barNamesIt = true;
            d.expect(barNamesIt && d.see("Add All"), "the Items nav bar does not offer Add All on ZR", pass);

            d.step("ZR opens Add All Items").press(Btn::ZR).frame();
            if (!d.expect(s.addAllItemsDialogActive, "ZR in the Items view did not open Add All Items", pass)) return;
            d.see("Add All Items");
            expectOneNavBar(d, "Add All Items open");
            d.expect(s.addAllItemsAmount == 999, "Add All Items does not start at the most an item can hold", "amount=" + std::to_string(s.addAllItemsAmount));

            if (!withKeyItems) {
                d.step("step the amount");
                d.press(Btn::Left).frame();
                d.expect(s.addAllItemsAmount == 998, "Left did not take 1 off the amount", "amount=" + std::to_string(s.addAllItemsAmount));
                d.press(Btn::ZR).frame();
                d.expect(s.addAllItemsAmount == 999, "ZR raised the amount past 999", "amount=" + std::to_string(s.addAllItemsAmount));
                d.press(Btn::ZL, 12).frame();
                d.expect(s.addAllItemsAmount == 1, "the amount went below 1", "amount=" + std::to_string(s.addAllItemsAmount));
                d.press(Btn::R).press(Btn::Right, 2).frame();
                d.expect(s.addAllItemsAmount == 13, "R and Right did not add 10 and 1", "amount=" + std::to_string(s.addAllItemsAmount));
                d.press(Btn::L).frame();
                d.expect(s.addAllItemsAmount == 3, "L did not take 10 off the amount", "amount=" + std::to_string(s.addAllItemsAmount));

                d.step("the key-item switch, by key and by tap");
                d.press(Btn::X).frame();
                d.expect(s.addAllItemsIncludesKeyItems && d.see("Key items given early"), "X did not switch key items on, or no warning was shown", "on=" + std::to_string(s.addAllItemsIncludesKeyItems));
                int switchCenterX = -1, switchCenterY = -1;
                for (const UI::TouchButton& button : s.touchButtons)
                    if (button.buttonId == 41) { switchCenterX = button.hitX + button.hitWidth / 2; switchCenterY = button.hitY + button.hitHeight / 2; }
                if (d.expect(switchCenterX >= 0, "the Include Key Items switch is not a touch target"))
                    d.tap(switchCenterX, switchCenterY).frame(2);
                d.expect(!s.addAllItemsIncludesKeyItems && d.see("Key items are left out"), "tapping the Include Key Items switch did not switch key items off", "on=" + std::to_string(s.addAllItemsIncludesKeyItems));

                d.step("B cancels and changes nothing").press(Btn::B).frame();
                d.expect(!s.addAllItemsDialogActive, "B did not close Add All Items");
                bool unchanged = tr->items.size() == itemsBefore.size();
                for (size_t pouchIndex = 0; unchanged && pouchIndex < itemsBefore.size(); ++pouchIndex) {
                    unchanged = tr->items[pouchIndex].size() == itemsBefore[pouchIndex].size();
                    for (size_t itemIndex = 0; unchanged && itemIndex < itemsBefore[pouchIndex].size(); ++itemIndex)
                        unchanged = tr->items[pouchIndex][itemIndex].itemId == itemsBefore[pouchIndex][itemIndex].itemId && tr->items[pouchIndex][itemIndex].count == itemsBefore[pouchIndex][itemIndex].count;
                }
                d.expect(unchanged && !s.hasUnsavedChanges, "cancelling Add All Items changed the bag");
                d.step("reopen with ZR").press(Btn::ZR).frame();
                d.expect(s.addAllItemsAmount == 3, "Add All Items forgot the amount between openings", "amount=" + std::to_string(s.addAllItemsAmount));
            } else {
                d.step("switch key items on").press(Btn::X).frame();
            }

            const int amount = s.addAllItemsAmount;
            s.storageStatus.clear();
            int addCenterX = -1, addCenterY = -1;
            for (const UI::TouchButton& button : s.touchButtons)
                if (button.buttonId == 1) { addCenterX = button.hitX + button.hitWidth / 2; addCenterY = button.hitY + button.hitHeight / 2; }
            if (withKeyItems && d.expect(addCenterX >= 0, "the Add button is not a touch target"))
                d.step("tap Add").tap(addCenterX, addCenterY).frame(TrainerViewScreen::TAP_PRESS_FRAMES + 1);
            else
                d.step("A adds").press(Btn::A).frame();
            if (!d.expect(!s.addAllItemsDialogActive, "Add All Items stayed open after Add", pass)) return;
            std::printf("      %s amount=%d: %s\n", pass.c_str(), s.addAllItemsAmount, s.storageStatus.c_str());
            d.expect(s.storageStatus.rfind("Added or topped up", 0) == 0 || s.storageStatus.rfind("Every item is already", 0) == 0, "Add All Items did not say what it did", pass + " status=\"" + s.storageStatus + "\"");

            int addedCount = 0, wrongCount = 0, loweredCount = 0, keyItemCount = 0, missingCount = 0;
            std::string firstProblem;
            const auto problem = [&](int& counter, const std::string& text) { if (!counter++ && firstProblem.empty()) firstProblem = text; };
            for (size_t pouchIndex = 0; pouchIndex < tr->items.size() && pouchIndex < itemsBefore.size(); ++pouchIndex) {
                const auto& before = itemsBefore[pouchIndex];
                const auto& after = tr->items[pouchIndex];
                // Add All Items only raises counts and appends, so every stack the bag held is still at its
                // own position -- which also keeps two stacks of one item apart.
                for (size_t itemIndex = 0; itemIndex < before.size(); ++itemIndex) {
                    const bool kept = itemIndex < after.size() && after[itemIndex].itemId == before[itemIndex].itemId && after[itemIndex].count >= before[itemIndex].count;
                    if (!kept)
                        problem(loweredCount, "pouch=" + std::to_string(pouchIndex) + " index=" + std::to_string(itemIndex) + " item=" + std::to_string(before[itemIndex].itemId) + " before=" + std::to_string(before[itemIndex].count) + " after=" + (itemIndex < after.size() ? std::to_string(after[itemIndex].itemId) + "x" + std::to_string(after[itemIndex].count) : std::string("gone")));
                }
                const bool pouchFull = static_cast<int>(after.size()) >= s.pouchCapacity(static_cast<int>(pouchIndex));
                for (const uint16_t itemId : Names::getPouchItems(group, pouchIndex)) {
                    const Trainer::InventoryItem* was = findItem(before, itemId);
                    const Trainer::InventoryItem* now = findItem(after, itemId);
                    const int previousCount = was ? was->count : 0;
                    const int currentCount = now ? now->count : 0;
                    const std::string where = "pouch=" + std::to_string(pouchIndex) + " item=" + std::to_string(itemId) + " before=" + std::to_string(previousCount) + " after=" + std::to_string(currentCount);
                    if (!withKeyItems && Names::isKeyItem(group, itemId)) {
                        if (currentCount != previousCount) problem(keyItemCount, where);
                        continue;
                    }
                    const int ceiling = expectedCeiling(group, static_cast<int>(pouchIndex), itemId, s.pouchItemMaxCount(static_cast<int>(pouchIndex)));
                    if (!now && !was) {
                        if (!pouchFull && ceiling > 0) problem(missingCount, where + " (the pouch had room)");
                        continue;
                    }
                    const int expectedCount = std::max(previousCount, std::min(previousCount + amount, ceiling));
                    if (currentCount != expectedCount) problem(wrongCount, where + " expected=" + std::to_string(expectedCount));
                    else if (currentCount > previousCount) ++addedCount;
                }
            }
            d.expect(loweredCount == 0, "Add All Items lowered or removed an item the bag already held", pass + " " + firstProblem);
            d.expect(keyItemCount == 0, "Add All Items changed a key item while key items were switched off", pass + " " + firstProblem);
            d.expect(wrongCount == 0, "Add All Items gave an item the wrong count", pass + " amount=" + std::to_string(amount) + " " + firstProblem);
            d.expect(missingCount == 0, "Add All Items left out a legal item while its pouch still had room", pass + " " + firstProblem);
            d.expect(addedCount > 0 || s.storageStatus.rfind("Every item is already", 0) == 0, "Add All Items added nothing and did not say why", pass);
            d.expect(addedCount == 0 || s.hasUnsavedChanges, "Add All Items changed the bag without marking the save as changed", pass);
            if (withKeyItems) {
                int keyItemsGiven = 0;
                for (size_t pouchIndex = 0; pouchIndex < tr->items.size(); ++pouchIndex)
                    for (const Trainer::InventoryItem& item : tr->items[pouchIndex])
                        if (item.count > 0 && Names::isKeyItem(group, item.itemId)) ++keyItemsGiven;
                d.expect(keyItemsGiven > 0, "Add All Items with key items switched on holds no key item at all", pass);
            }

            if (std::unique_ptr<Trainer::Trainer> reopened = loader.writeAndReopen(*tr))
                expectItemsSurviveReload(context, *tr, *reopened, pass);
            else
                context.fail("the save written after Add All Items could not be read back", pass);
        }
    }

    /// The Boxes view's Y opens the per-Pokemon menu the Storage view opens with A, so Clone, Export and
    /// Release are reachable outside Storage, and X opens the box menu (Sort / Search) in both views.
    void testBoxMenus(Test::Context& context, TrainerViewScreen& s, Trainer::Trainer& tr) {
        using Screen = TrainerViewScreen;
        Driver d(context, s, "box-menus");
        d.step("open the box view");
        gotoHome(d, s, 0);
        d.press(Btn::A).frame();
        if (!d.expect(s.detailViewActive, "could not open the box view")) return;

        const int boxCount = static_cast<int>(tr.getBoxCount());
        const int slotsPerBox = static_cast<int>(tr.getSlotsPerBox());
        const auto occupied = [&](int box, int slot) {
            const auto& pokemon = tr.boxes[box][slot];
            return pokemon && pokemon->speciesID() != 0;
        };
        int sourceBox = -1, sourceSlot = -1, freeSlots = 0;
        for (int box = 0; box < boxCount; ++box)
            for (int slot = 0; slot < slotsPerBox; ++slot) {
                if (!occupied(box, slot)) { ++freeSlots; continue; }
                if (sourceBox < 0) { sourceBox = box; sourceSlot = slot; }
            }
        if (sourceBox < 0) {
            context.note("this save has no boxed Pokemon, so the box menus were not exercised", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }
        s.selectedBoxIndex = sourceBox;
        s.selectedItemIndex = sourceSlot;
        d.frame();

        d.step("the nav bar names both menus").see("Sort/Search");
        d.step("Y opens the Pokemon menu").press(Btn::Y).frame();
        if (!d.expect(s.storageMenuActive, "Y in the Boxes view did not open the Pokemon menu")) return;
        d.expect(s.menuPane == 0 && s.menuBox == sourceBox && s.menuSlot == sourceSlot, "the Pokemon menu opened on the wrong slot");
        d.see("Clone");
        d.notSee("Find…");

        // CLONE: one more Pokemon in the save, a copy of the source. Undone at once.
        std::vector<bool> occupiedBefore;
        for (int box = 0; box < boxCount; ++box)
            for (int slot = 0; slot < slotsPerBox; ++slot) occupiedBefore.push_back(occupied(box, slot));
        s.storageMenuIndex = Screen::STORAGE_MENU_ROW_CLONE;
        d.step("clone from the Boxes view").press(Btn::A).frame();
        d.expect(!s.storageMenuActive, "choosing Clone left the Pokemon menu open");
        int cloneBox = -1, cloneSlot = -1;
        for (int box = 0; box < boxCount && cloneBox < 0; ++box)
            for (int slot = 0; slot < slotsPerBox; ++slot)
                if (!occupiedBefore[box * slotsPerBox + slot] && occupied(box, slot)) {
                    cloneBox = box; cloneSlot = slot; break;
                }
        if (freeSlots > 0) {
            if (d.expect(cloneBox >= 0, "Clone in the Boxes view placed nothing although a slot was free")) {
                d.expect(tr.boxes[cloneBox][cloneSlot]->speciesID() == tr.boxes[sourceBox][sourceSlot]->speciesID(), "the clone is not the same species as its source");
                tr.boxes[cloneBox][cloneSlot].reset();
            }
        } else {
            d.expect(cloneBox < 0, "Clone placed a Pokemon in a save with no free slot");
        }

        // MOVE: the Boxes view's grab, which B lets go of.
        s.selectedBoxIndex = sourceBox;
        s.selectedItemIndex = sourceSlot;
        d.step("Move grabs the slot").press(Btn::Y).frame();
        s.storageMenuIndex = Screen::STORAGE_MENU_ROW_MOVE;
        d.press(Btn::A).frame();
        d.expect(s.swapActive && s.swapSourceBox == sourceBox && s.swapSourceSlot == sourceSlot, "Move in the Boxes view did not grab the slot");
        d.step("B lets go").press(Btn::B).frame();
        d.expect(!s.swapActive, "B did not let go of a grabbed slot");
        d.expect(s.detailViewActive, "B with a grabbed slot left the box view");

        // X: the box menu, and its Search highlights and jumps.
        d.step("X opens the box menu").press(Btn::X).frame();
        if (!d.expect(s.boxMenuActive, "X in the Boxes view did not open the box menu")) return;
        d.see("Sort");
        d.see("Search…");
        const std::string speciesName = Trainer::getSpeciesName(tr.boxes[sourceBox][sourceSlot]->speciesID());
        s.selectedBoxIndex = (sourceBox + 1) % boxCount;
        s.boxMenuIndex = Screen::BOX_MENU_ROW_SEARCH;
        d.step("search for the source's species").type(speciesName).press(Btn::A).frame(2);
        d.expect(s.storageSearch.isFiltering(), "Search in the box menu did not set a query");
        d.expect(s.selectedItemIndex >= 0 && s.storageSlotMatchesSearch(0, s.selectedBoxIndex, s.selectedItemIndex), "the cursor did not jump to a matching Pokemon", "box=" + std::to_string(s.selectedBoxIndex) + " slot=" + std::to_string(s.selectedItemIndex));
        s.boxMenuIndex = Screen::BOX_MENU_ROW_SEARCH;
        d.step("clear the search").press(Btn::X).frame();
        s.boxMenuIndex = Screen::BOX_MENU_ROW_SEARCH;
        d.type("").press(Btn::A).frame(2);
        d.expect(!s.storageSearch.isFiltering(), "accepting an empty search did not clear the box search");
        d.step("leave the box view").press(Btn::B).frame(2);

        // Storage: X opens the same box menu, and the Pokemon menu no longer carries Find.
        d.step("open the storage view");
        gotoHome(d, s, 2);
        d.press(Btn::A).frame();
        if (!d.expect(s.detailViewActive, "could not open the storage view")) return;
        d.step("X opens the box menu in Storage").press(Btn::X).frame();
        d.expect(s.boxMenuActive, "X in the Storage view did not open the box menu");
        d.see("Search…");
        d.step("B closes the box menu").press(Btn::B).frame();
        d.expect(!s.boxMenuActive, "B did not close the box menu");
        s.selectedBoxIndex = sourceBox;
        s.stSaveSlot = sourceSlot;
        d.step("A opens the Pokemon menu in Storage").press(Btn::A).frame();
        if (d.expect(s.storageMenuActive, "A in Storage's Menu mode did not open the Pokemon menu"))
            d.notSee("Find…");
        d.press(Btn::B).frame();
    }

    /// A Pokemon carried from the bank into this save becomes the receiving player's. The withdraw
    /// hands the open save's trainer to the conversion, which makes them the active handler; a
    /// FireRed Venusaur once reached Violet showing "(none)" for its handler because only Gen 1/2
    /// were handed one. Driven through the real Storage view so the call site is what is tested,
    /// then put back: the save and the bank are shared with every later test.
    void testWithdrawSetsHandler(Test::Context& context, TrainerViewScreen& screen, Trainer::Trainer& trainer) {
        using Screen = TrainerViewScreen;
        const std::unique_ptr<Pokemon::Pokemon> blankPokemon = trainer.createBlankPokemon();
        if (!blankPokemon || !blankPokemon->hasHandler() || trainer.trainerName.empty() || !screen.bank) {
            context.note("this format has no handler, or the save no trainer name, so the withdraw handler test was skipped", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }

        // A FireRed Pokemon, the reported case. PKSE cannot build a PK3 from nothing, so it starts as a
        // Gen 7 record written down into FireRed with no receiver -- what a banked FireRed Pokemon is.
        // The first species this game's dex has.
        std::unique_ptr<Pokemon::Pokemon> fireRedPokemon;
        for (const uint16_t speciesId : {3, 25, 133, 1}) {
            Pokemon::Pokemon7USUM gen7Pokemon;
            gen7Pokemon.setSpecies(speciesId);
            gen7Pokemon.setExp(125000);
            gen7Pokemon.setTID16(24680);
            gen7Pokemon.setOTName(Utils::utf8ToUtf16("KIASTA"));
            gen7Pokemon.recalculateStats();
            Conversion::Result conversionResult = Conversion::Result::Ok;
            auto candidate = Conversion::convert(gen7Pokemon, Enums::GameVersion::FRLG, conversionResult, static_cast<uint8_t>(Enums::GameVersion::FR));
            if (candidate && Conversion::canConvert(*candidate, trainer.getGameGroup(), conversionResult) && conversionResult == Conversion::Result::Ok) {
                fireRedPokemon = std::move(candidate);
                break;
            }
        }
        if (!fireRedPokemon) {
            context.note("none of the probe species converts into this game, so the withdraw handler test was skipped", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }

        int saveBox = -1, saveSlot = -1;
        for (int boxIndex = 0; boxIndex < static_cast<int>(trainer.getBoxCount()) && saveBox < 0; ++boxIndex)
            for (int slotIndex = 0; slotIndex < static_cast<int>(trainer.getSlotsPerBox()); ++slotIndex) {
                const auto& occupant = trainer.boxes[boxIndex][slotIndex];
                if ((!occupant || occupant->speciesID() == 0) && !screen.storageSlotLocked(0, boxIndex, slotIndex)) { saveBox = boxIndex; saveSlot = slotIndex; break; }
            }
        int bankSlot = -1;
        for (int slotIndex = 0; slotIndex < static_cast<int>(Trainer::Bank::BANK_SLOTS_PER_BOX) && bankSlot < 0; ++slotIndex)
            if (!screen.bank->boxes[0][slotIndex]) bankSlot = slotIndex;
        if (saveBox < 0 || bankSlot < 0) {
            context.note("no free save or bank slot, so the withdraw handler test was skipped", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }
        std::unique_ptr<Pokemon::Pokemon> originalSaveSlot = std::move(trainer.boxes[saveBox][saveSlot]);
        screen.bank->boxes[0][bankSlot] = std::move(fireRedPokemon);

        Driver driver(context, screen, "withdraw-handler");
        driver.step("open the storage view");
        gotoHome(driver, screen, 2);
        driver.press(Btn::A).frame();
        if (driver.expect(screen.detailViewActive && screen.selectedMode == Screen::ViewMode::Storage, "could not open the storage view")) {
            screen.storageFocusPane = 1;
            screen.bank->currentBox = 0;
            screen.stBankBox = 0;
            screen.stBankSlot = bankSlot;
            driver.step("A opens the Pokemon menu on the banked FireRed Pokemon").press(Btn::A).frame();
            if (driver.expect(screen.storageMenuActive, "A on a bank slot did not open the Pokemon menu")) {
                screen.storageMenuIndex = Screen::STORAGE_MENU_ROW_MOVE;
                driver.step("Move picks it up").press(Btn::A).frame();
                if (driver.expect(screen.carrying(), "Move did not pick up the bank Pokemon")) {
                    screen.storageFocusPane = 0;
                    screen.selectedBoxIndex = saveBox;
                    screen.stSaveSlot = saveSlot;
                    driver.step("A puts it down in the save").press(Btn::A).frame();
                    if (screen.moveConfirmActive()) driver.step("continue past the transfer notice").press(Btn::A).frame();
                    const auto& placed = trainer.boxes[saveBox][saveSlot];
                    if (driver.expect(placed && placed->getGameGroup() == trainer.getGameGroup(), "the withdrawn FireRed Pokemon did not land in the save, converted")) {
                        const std::string where = "box=" + std::to_string(saveBox) + " slot=" + std::to_string(saveSlot);
                        driver.expect(placed->currentHandler() == 1, "a Pokemon withdrawn from another game is not in this save's trainer's hands", where + " currentHandler=" + std::to_string(placed->currentHandler()));
                        driver.expect(placed->htName() == Utils::utf8ToUtf16(trainer.trainerName) && placed->htGender() == trainer.trainerGender, "a Pokemon withdrawn from another game does not name this save's trainer as its handler", where + " ht=\"" + Utils::utf16ToUtf8(placed->htName()) + "\" trainer=\"" + trainer.trainerName + "\"");
                        if (placed->hasHandlerLanguage()) {
                            const uint8_t expectedLanguage = Enums::safeLanguageForGroup(trainer.getGameGroup(), trainer.language());
                            driver.expect(placed->htLanguage() == expectedLanguage, "a withdrawn Pokemon's handler language is not this save's", where + " htLanguage=" + std::to_string(placed->htLanguage()) + " expected=" + std::to_string(expectedLanguage));
                        }
                    }
                }
            }
        }

        trainer.boxes[saveBox][saveSlot] = std::move(originalSaveSlot);
        screen.bank->boxes[0][bankSlot].reset();
    }

    /// The cursor responds to the d-pad. A button that stopped doing anything is otherwise silent.
    void testCursorMoves(Test::Context& context, TrainerViewScreen& s) {
        Driver d(context, s, "cursor");
        d.step("open the box view");
        gotoHome(d, s, 0);
        d.press(Btn::A).frame();
        if (!d.expect(s.detailViewActive, "could not open the box view")) return;

        s.selectedItemIndex = 0;
        d.step("press Right in the box grid").press(Btn::Right).frame();
        const int afterRight = s.selectedItemIndex;
        d.expect(afterRight != 0, "Right did not move the box cursor", "selectedItemIndex stayed at " + std::to_string(afterRight));

        d.step("press Down in the box grid").press(Btn::Down).frame();
        d.expect(s.selectedItemIndex != afterRight, "Down did not move the box cursor", "selectedItemIndex stayed at " + std::to_string(s.selectedItemIndex));
    }

    /// The trainer card shows the save's own OT name -- the simplest end-to-end proof that what was
    /// parsed out of the file is what reaches the screen.
    void testTrainerCardShowsSaveData(Test::Context& context, TrainerViewScreen& s, Trainer::Trainer& tr) {
        Driver d(context, s, "trainer-card");
        d.step("open the trainer card");
        gotoHome(d, s, 4);
        d.press(Btn::A).frame(2);
        if (!d.expect(s.detailViewActive, "could not open the trainer card")) return;
        if (tr.trainerName.empty()) {
            context.note("this save has no OT name, so the trainer card check was skipped");
            return;
        }
        d.step("look for the OT name").see(tr.trainerName);
    }

    /// Renaming a box: the keyboard answer reaches both the save and the screen, and a CANCEL
    /// changes nothing. The cancel half matters as much as the accept half -- treating a cancel as
    /// an empty string would blank the name the user was editing.
    void testBoxRename(Test::Context& context, TrainerViewScreen& s, Trainer::Trainer& tr) {
        if (!tr.supportsBoxNames()) {
            context.note("this format has no box names, so the rename test was skipped", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }
        Driver d(context, s, "box-rename");
        d.step("open the box view");
        gotoHome(d, s, 0);
        d.press(Btn::A).frame();
        if (!d.expect(s.detailViewActive, "could not open the box view")) return;

        const int box = s.selectedBoxIndex;
        if (box < 0 || box >= static_cast<int>(tr.boxNames.size())) return;
        const std::string original = tr.boxNames[box];

        // Focus the box-name header, which the panel marks with slot index -1. Up from the TOP ROW
        // lands on it in exactly one press; from anywhere else it walks up the grid first. Pressing
        // Up repeatedly does NOT converge -- the header sits in a cycle (top row -> header -> bottom
        // row -> ... -> top row) -- so the cursor is placed first and pressed once.
        s.selectedItemIndex = 0;
        d.step("focus the box-name header").press(Btn::Up);
        if (!d.expect(s.selectedItemIndex == -1, "Up from the top row did not reach the box-name header", "selectedItemIndex=" + std::to_string(s.selectedItemIndex)))
            return;

        const std::string wanted = "UITEST";
        d.step("rename the box").type(wanted).press(Btn::A).frame(2);
        const std::string after = tr.boxNames[box];
        d.expect(after == wanted, "a box rename did not reach the save", "wanted=\"" + wanted + "\" got=\"" + after + "\" was=\"" + original + "\"");
        d.step("the new name is on screen").see(wanted);

        s.selectedItemIndex = 0;
        d.step("cancel a rename").press(Btn::Up);
        d.cancelPrompt().press(Btn::A).frame(2);
        d.expect(tr.boxNames[box] == wanted, "cancelling a rename changed the box name anyway", "expected=\"" + wanted + "\" got=\"" + tr.boxNames[box] + "\"");

        tr.boxNames[box] = original;   // leave the in-memory save as we found it
    }

    /// A tapped dialog button is SEEN before it acts.
    ///
    /// Moving the selection and synthesising the button press in the same frame runs the action before
    /// any frame has been drawn with the row highlighted or the button held. On a list dialog that
    /// leaves the cursor on the tapped row afterwards, which reads as feedback arriving late; on a
    /// glyph confirm nothing moves at all -- the dialog simply vanishes. Two shapes, two behaviours,
    /// neither of them showing the user what they hit.
    ///
    /// Both halves are asserted, and the first is the one that regresses silently: an implementation
    /// that goes back to acting immediately still LOOKS right in a screenshot of the end state.
    void testTapIsSeenBeforeItActs(Test::Context& context, TrainerViewScreen& s, Trainer::Trainer& tr) {
        Driver d(context, s, "tap-feedback");
        d.step("open the items view");
        gotoHome(d, s, 3);
        d.press(Btn::A).frame(2);
        if (!d.expect(s.detailViewActive, "could not open the items view")) return;

        // The first pouch can be empty (a Gold or Pearl save's is), so R moves on to one with an item.
        for (size_t pouchIndex = 0; pouchIndex < tr.items.size() && s.visibleItemIndices().empty(); ++pouchIndex)
            d.step("next pouch, looking for an item").press(Btn::R).frame();
        if (s.visibleItemIndices().empty()) {
            context.note("this save's bag is empty, so tap feedback was not exercised", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }

        d.step("open the item edit dialog").press(Btn::A).frame(2);
        if (!d.expect(s.itemEditDialogActive, "A on an item did not open the edit dialog")) return;

        // Cancel is id 0 in every dialog here. Its rect comes from the screen's own hit list, so the
        // test taps exactly where the dialog says the button is rather than at a copied coordinate.
        const UI::TouchButton* cancel = nullptr;
        for (const auto& b : s.touchButtons)
            if (b.buttonId == 0) { cancel = &b; break; }
        if (!d.expect(cancel != nullptr, "the item edit dialog published no Cancel button to tap")) return;

        const int tapX = cancel->hitX + cancel->hitWidth / 2;
        const int tapY = cancel->hitY + cancel->hitHeight / 2;
        const int buttonX = cancel->hitX, buttonY = cancel->hitY;

        d.step("tap Cancel").tap(tapX, tapY);

        // Half one: the dialog is still up. The press has been recorded, not delivered.
        d.expect(s.itemEditDialogActive, "a tapped dialog button acted in its own frame, before it was ever drawn held", "dialogActive=0");

        // Half two: it is drawn held. The accent ring drawGlyphButton strokes around a pressed
        // button, at that button's own rectangle -- so this fails both if the ring is missing and if
        // it is drawn somewhere other than the button the user actually touched.
        bool held = false;
        for (const UIHost::Draw& dr : d.frameData().draws) {
            if (dr.kind != UIHost::Kind::Shape || std::string(dr.op) != "roundedRect" || !dr.visible) continue;
            if (dr.drawX != buttonX || dr.drawY != buttonY) continue;
            if (dr.color.red == UI::Colors::Primary.red && dr.color.green == UI::Colors::Primary.green && dr.color.blue == UI::Colors::Primary.blue) { held = true; break; }
        }
        d.expect(held, "a tapped dialog button was not drawn held while its press waited", "buttonX=" + std::to_string(buttonX) + " buttonY=" + std::to_string(buttonY));

        // ...and then it acts. A press that is deferred and never delivered is a dead button, which
        // is the failure this half exists to catch.
        d.step("let the press land").frame(TrainerViewScreen::TAP_PRESS_FRAMES + 1);
        d.expect(!s.itemEditDialogActive, "a tapped dialog button never delivered its press -- the dialog stayed open", "framesWaited=" + std::to_string(TrainerViewScreen::TAP_PRESS_FRAMES + 1));
        (void)tr;
    }

    /// An EGG's details page must not read as a foreign Pokemon.
    ///
    /// Gen 3 stamps every egg -- on every cartridge -- with the Japanese word for egg as its nickname
    /// and language 1, and leaves the OT bytes in the cartridge's own charset beside them. A reader
    /// that lets that language byte pick the text table shows a player's own English save as
    /// nickname タマゴ, OT ＫＩＡＳＴＡ and Language Japanese, which is what was reported. The page
    /// has to show what the games show: no placeholder, the trainer's own name, and no language
    /// claim at all until it hatches.
    ///
    /// The gate is the ENTITY's own answer, not a generation: a format whose eggs carry real text
    /// (Gen 4 on) reports no placeholder and is skipped rather than held to a Gen 3 rule.
    void testEggTextIsNotForeign(Test::Context& context, TrainerViewScreen& s, Trainer::Trainer& tr) {
        int partySlot = -1, boxIndex = -1, boxSlot = -1;
        for (int slot = 0; slot < static_cast<int>(tr.party.size()); ++slot) {
            if (tr.party[slot] && tr.party[slot]->eggTextIsPlaceholder()) { partySlot = slot; break; }
        }
        if (partySlot < 0) {
            for (int box = 0; box < static_cast<int>(tr.boxes.size()) && boxSlot < 0; ++box) {
                for (int slot = 0; slot < static_cast<int>(tr.boxes[box].size()); ++slot) {
                    if (tr.boxes[box][slot] && tr.boxes[box][slot]->eggTextIsPlaceholder()) {
                        boxIndex = box; boxSlot = slot; break;
                    }
                }
            }
        }
        if (partySlot < 0 && boxSlot < 0) {
            context.note("this save holds no egg with placeholder text, so the egg details page was not exercised", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }

        Driver d(context, s, "egg-details");
        if (partySlot >= 0) {
            d.step("open the party view");
            gotoHome(d, s, 1);
            d.press(Btn::A).frame();
            if (!d.expect(s.detailViewActive, "could not open the party view")) return;
            s.selectedPartyIndex = partySlot;
        } else {
            d.step("open the box holding the egg");
            gotoHome(d, s, 0);
            d.press(Btn::A).frame();
            if (!d.expect(s.detailViewActive, "could not open the box view")) return;
            s.selectedBoxIndex = boxIndex;
            s.selectedItemIndex = boxSlot;
        }
        d.step("open the details page on the egg").press(Btn::A).frame(2);
        if (!d.expect(s.details.active, "A on an egg did not open the details page")) return;

        // The rows are read by SCROLLING the page rather than by guessing where they sit: the left
        // column only draws what is on screen, and the Language row starts below the fold.
        bool sawPlaceholder = false, sawJapanese = false, sawHatchNote = false, sawTrainerName = false;
        for (int scroll = 0; scroll <= 600; scroll += 40) {
            s.details.leftScroll = scroll;
            d.frame();
            sawPlaceholder |= d.frameData().has("タマゴ");
            sawJapanese |= d.frameData().has("Japanese");
            sawHatchNote |= d.frameData().has("(set when it hatches)");
            sawTrainerName |= tr.trainerName.empty() || d.frameData().has(tr.trainerName);
        }
        // The stored placeholder, byte for byte what the games write. Showing it anywhere means the
        // page is reading an egg's nickname field as if it were a name.
        d.expect(!sawPlaceholder, "an egg's details page showed the stored Japanese placeholder");
        // The OT the player actually has. Read through the Japanese table the same bytes come out as
        // full-width letters, so this fails on exactly the reported bug rather than on its shape.
        d.expect(sawTrainerName, "an egg's details page did not show the save's own trainer name", "want=\"" + tr.trainerName + "\"");
        // An egg has no language yet -- the hatch writes the game's own.
        d.expect(!sawJapanese, "an egg's details page claimed a language its record cannot answer for");
        d.expect(sawHatchNote, "the Language row did not say the language is set when the egg hatches");
    }

    /// The Alpha row on the details page: shown exactly where the format stores the flag, and A or a
    /// tap on it flips the flag on the Pokemon being edited.
    ///
    /// Becoming an Alpha also rewrites the size, so the record's bytes are put back afterwards: the
    /// trainer is shared with every later test on this save.
    void testAlphaToggle(Test::Context& context, TrainerViewScreen& s, Trainer::Trainer& tr) {
        int boxIndex = -1, boxSlot = -1;
        for (int box = 0; box < static_cast<int>(tr.boxes.size()) && boxSlot < 0; ++box) {
            for (int slot = 0; slot < static_cast<int>(tr.boxes[box].size()); ++slot) {
                if (!Test::isEmptySlot(tr.boxes[box][slot].get()) && !tr.boxes[box][slot]->isEgg()) { boxIndex = box; boxSlot = slot; break; }
            }
        }
        if (boxSlot < 0) {
            context.note("this save's boxes hold no Pokemon, so the Alpha row was not exercised", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }

        Driver d(context, s, "alpha-toggle");
        d.step("open the box view");
        gotoHome(d, s, 0);
        d.press(Btn::A).frame();
        if (!d.expect(s.detailViewActive, "could not open the box view")) return;
        s.selectedBoxIndex = boxIndex;
        s.selectedItemIndex = boxSlot;
        d.step("open the details page").press(Btn::A).frame(2);
        if (!d.expect(s.details.active, "A on an occupied slot did not open the details page")) return;

        Pokemon::Pokemon* pokemon = s.detailsTargetPokemon();
        if (!d.expect(pokemon != nullptr, "the details page has no Pokemon to edit")) return;
        constexpr int alphaField = 34;
        const auto& leftColumnOrder = s.details.leftOrder;
        const bool alphaRowListed = std::find(leftColumnOrder.begin(), leftColumnOrder.end(), alphaField) != leftColumnOrder.end();
        if (!pokemon->hasAlphaFlag()) {
            d.expect(!alphaRowListed, "the details page offers an Alpha row for a format with no Alpha flag");
            return;
        }
        if (!d.expect(alphaRowListed, "the details page offers no Alpha row although the format stores the flag")) return;

        const std::span<const std::byte> originalSpan = pokemon->getData();
        const std::vector<std::byte> originalBytes(originalSpan.begin(), originalSpan.end());
        const bool originalAlpha = pokemon->isAlpha();

        s.details.selectedField = alphaField;
        d.step("bring the Alpha row into view").frame(2);
        d.see("Alpha");
        d.step("toggle Alpha with A").press(Btn::A).frame(2);
        pokemon = s.detailsTargetPokemon();
        if (d.expect(pokemon != nullptr, "the details page lost its Pokemon after the toggle")) {
            d.expect(pokemon->isAlpha() != originalAlpha, "A on the Alpha row did not change the flag", "before=" + std::to_string(originalAlpha));
            d.expect(s.hasUnsavedChanges, "toggling Alpha did not mark the save as changed");
        }

        // The tap goes through the row's own published hit rectangle, the path a touch screen takes.
        const UI::TouchButton* alphaButton = nullptr;
        for (const auto& button : s.touchButtons)
            if (button.buttonId == alphaField) { alphaButton = &button; break; }
        if (d.expect(alphaButton != nullptr, "the Alpha row published no touch target")) {
            d.step("toggle Alpha back with a tap").tap(alphaButton->hitX + alphaButton->hitWidth / 2, alphaButton->hitY + alphaButton->hitHeight / 2).frame(TrainerViewScreen::TAP_PRESS_FRAMES + 1);
            pokemon = s.detailsTargetPokemon();
            d.expect(pokemon && pokemon->isAlpha() == originalAlpha, "a tap on the Alpha row did not change the flag back");
        }

        pokemon = s.detailsTargetPokemon();
        if (pokemon) {
            std::memcpy(pokemon->getData().data(), originalBytes.data(), originalBytes.size());
        }
    }

    /// Hyper Training, seen and edited. The stat editor's row sets and clears one stat's flag, the
    /// stat follows it while the stored IV stays put, Cancel keeps nothing, the details page and the
    /// party card both mark a trained IV, and the row refuses what the game could not have done:
    /// below the format's level, or on an IV that is already maximal.
    void testHyperTraining(Test::Context& context, TrainerViewScreen& s, Trainer::Trainer& tr) {
        int boxIndex = -1, boxSlot = -1;
        for (int box = 0; box < static_cast<int>(tr.boxes.size()) && boxSlot < 0; ++box) {
            for (int slot = 0; slot < static_cast<int>(tr.boxes[box].size()); ++slot) {
                if (!Test::isEmptySlot(tr.boxes[box][slot].get()) && !tr.boxes[box][slot]->isEgg()) { boxIndex = box; boxSlot = slot; break; }
            }
        }
        if (boxSlot < 0) {
            context.note("this save's boxes hold no Pokemon, so the Hyper Training row was not exercised", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }

        Pokemon::Pokemon* target = tr.boxes[boxIndex][boxSlot].get();
        const std::span<const std::byte> originalSpan = target->getData();
        const std::vector<std::byte> originalBytes(originalSpan.begin(), originalSpan.end());
        // Let's Go keeps its party in the boxes, and a saved edit is mirrored into the party copy, so
        // the party is put back as well as the box slot.
        std::vector<std::vector<std::byte>> originalPartyBytes;
        for (const auto& member : tr.party) {
            if (!member) { originalPartyBytes.emplace_back(); continue; }
            const std::span<const std::byte> memberSpan = member->getData();
            originalPartyBytes.emplace_back(memberSpan.begin(), memberSpan.end());
        }
        auto restore = [&]() {
            std::memcpy(target->getData().data(), originalBytes.data(), originalBytes.size());
            for (size_t partyIndex = 0; partyIndex < tr.party.size() && partyIndex < originalPartyBytes.size(); ++partyIndex)
                if (tr.party[partyIndex] && !originalPartyBytes[partyIndex].empty())
                    std::memcpy(tr.party[partyIndex]->getData().data(), originalPartyBytes[partyIndex].data(), originalPartyBytes[partyIndex].size());
        };
        const int maxIV = target->maxIV();
        // A known starting point whatever the save holds: old enough to train, HP untrained and below
        // the maximum. A Legends: Arceus Pokemon also needs the HOME visit its flags would come from.
        if (target->hasHyperTraining()) {
            target->setLevel(100);
            target->setIV(0, 10);
            target->setHyperTrainFlags(0);
            if (target->hasHomeTracker() && target->homeTracker() == 0) target->setHomeTracker(0x0123456789ABCDEFull);
            target->recalculateStats();
            target->refreshChecksum();
        }

        Driver d(context, s, "hyper-training");
        d.step("open the box view");
        gotoHome(d, s, 0);
        d.press(Btn::A).frame();
        if (!d.expect(s.detailViewActive, "could not open the box view")) return;
        s.selectedBoxIndex = boxIndex;
        s.selectedItemIndex = boxSlot;
        d.step("open the details page").press(Btn::A).frame(2);
        if (!d.expect(s.details.active, "A on an occupied slot did not open the details page")) return;
        Pokemon::Pokemon* pokemon = s.detailsTargetPokemon();
        if (!d.expect(pokemon == target, "the details page is not editing the Pokemon it was opened on")) { restore(); return; }

        s.details.selectedField = 0;
        d.step("open the HP stat editor").press(Btn::A).frame();
        if (!d.expect(s.statEdit.dialogActive, "A on the HP row did not open the stat editor")) { restore(); return; }
        if (!pokemon->hasHyperTraining()) {
            d.notSee("Hyper Trained");
            d.step("close the stat editor").press(Btn::B).frame();
            restore();
            return;
        }
        d.see("Hyper Trained");
        d.see("Plays as 10");

        const uint16_t untrainedHP = pokemon->statHPMax();
        d.step("select the Hyper Trained row").press(Btn::Down).frame();
        d.expect(s.statEdit.mode == UI::Dialogs::StatEditMode::HyperTrain, "Down from the IV row did not select the Hyper Trained row");
        d.step("set it").press(Btn::Right).frame();
        d.expect(s.statEdit.currentHyperTrained, "Right on the Hyper Trained row did not set it");
        d.see("Plays as " + std::to_string(maxIV));
        d.step("save the stat").press(Btn::A).frame();
        d.expect(!s.statEdit.dialogActive, "A did not close the stat editor");
        pokemon = s.detailsTargetPokemon();
        if (!d.expect(pokemon != nullptr, "the details page lost its Pokemon after the edit")) { restore(); return; }
        d.expect(pokemon->isHyperTrained(0), "saving the stat editor did not set HP's Hyper Training flag", "flags=" + std::to_string(pokemon->hyperTrainFlags()));
        d.expect(pokemon->hyperTrainFlags() == 0x01, "saving one stat's Hyper Training touched another stat's flag", "flags=" + std::to_string(pokemon->hyperTrainFlags()));
        d.expect(pokemon->ivHP() == 10, "Hyper Training changed the stored IV", "ivHP=" + std::to_string(pokemon->ivHP()));
        d.expect(pokemon->checksumValid(), "the record's checksum is stale after Hyper Training");
        d.expect(s.hasUnsavedChanges, "Hyper Training did not mark the save as changed");
        // Shedinja's HP is 1 whatever its IV, so it has nothing to show here.
        if (pokemon->speciesID() != 292 && untrainedHP != 0)
            d.expect(pokemon->statHPMax() > untrainedHP, "the HP stat did not follow its Hyper Training flag", "before=" + std::to_string(untrainedHP) + " after=" + std::to_string(pokemon->statHPMax()));
        d.step("the details page marks the trained IV").frame();
        d.see("HT");
        d.see("HT: Hyper Trained, plays as " + std::to_string(maxIV));

        // The tap path: the first tap on the row selects it, the second flips it.
        d.step("reopen the HP stat editor").press(Btn::A).frame();
        if (!d.expect(s.statEdit.dialogActive && s.statEdit.currentHyperTrained, "the reopened stat editor does not show HP as Hyper Trained")) { restore(); return; }
        for (int tapIndex = 0; tapIndex < 2; ++tapIndex) {
            const UI::TouchButton* rowButton = nullptr;
            for (const auto& button : s.touchButtons)
                if (button.buttonId == 12) { rowButton = &button; break; }
            if (!d.expect(rowButton != nullptr, "the Hyper Trained row published no touch target")) { restore(); return; }
            d.step(tapIndex == 0 ? "tap the Hyper Trained row" : "tap it again").tap(rowButton->hitX + rowButton->hitWidth / 2, rowButton->hitY + rowButton->hitHeight / 2).frame();
        }
        d.expect(s.statEdit.mode == UI::Dialogs::StatEditMode::HyperTrain && !s.statEdit.currentHyperTrained, "two taps on the Hyper Trained row did not select it and clear it");
        d.step("cancel").press(Btn::B).frame();
        pokemon = s.detailsTargetPokemon();
        d.expect(pokemon && pokemon->isHyperTrained(0), "Cancel kept the cleared Hyper Training flag");

        // Raising a trained IV to the maximum leaves a flag the game could never have set; it drops.
        d.step("raise the trained IV to the maximum").press(Btn::A).frame();
        d.press(Btn::R, 3).frame();
        d.expect(s.statEdit.currentIV == maxIV && !s.statEdit.currentHyperTrained, "raising a Hyper Trained IV to the maximum kept the flag", "iv=" + std::to_string(s.statEdit.currentIV));
        d.see("IV is already " + std::to_string(maxIV));
        d.step("cancel").press(Btn::B).frame();

        // Below the format's level the row refuses to set the flag, says why, and gives way to the
        // illegal-values override.
        pokemon = s.detailsTargetPokemon();
        if (!pokemon) { restore(); return; }
        pokemon->setHyperTrainFlags(0);
        pokemon->setLevel(static_cast<uint8_t>(pokemon->hyperTrainMinimumLevel() - 1));
        d.step("open the stat editor below the level").press(Btn::A).frame();
        d.press(Btn::Down).frame();
        d.press(Btn::Right).frame();
        d.expect(!s.statEdit.currentHyperTrained, "the Hyper Trained row set a flag below the level the game requires", "level=" + std::to_string(pokemon->hyperTrainMinimumLevel() - 1));
        d.see("Needs Lv. " + std::to_string(pokemon->hyperTrainMinimumLevel()));
        g_allowIllegalEdits = true;
        d.press(Btn::Right).frame();
        g_allowIllegalEdits = false;
        d.expect(s.statEdit.currentHyperTrained, "the illegal-values override did not let the Hyper Trained row set the flag");
        d.step("cancel").press(Btn::B).frame();
        restore();
    }

    /// The party card, where a team is checked at a glance, marks a Hyper Trained IV too.
    void testHyperTrainingPartyMark(Test::Context& context, TrainerViewScreen& s, Trainer::Trainer& tr) {
        Pokemon::Pokemon* partyMember = nullptr;
        for (auto& member : tr.party)
            if (!Test::isEmptySlot(member.get()) && !member->isEgg()) { partyMember = member.get(); break; }
        if (!partyMember) {
            context.note("this save's party is empty, so the party card's Hyper Training mark was not checked", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }
        if (!partyMember->hasHyperTraining()) return;
        const uint8_t savedPartyFlags = partyMember->hyperTrainFlags();
        partyMember->setHyperTrainFlags(0);
        Driver party(context, s, "hyper-training-party");
        party.step("open the party view");
        gotoHome(party, s, 1);
        party.press(Btn::A).frame(2);
        if (party.expect(s.detailViewActive && s.selectedMode == TrainerViewScreen::ViewMode::Party, "could not open the party view")) {
            // Counted, not merely looked for: another member of a real team may be trained already.
            const size_t untrainedMarkCount = party.frameData().textExact("HT").size();
            partyMember->setHyperTrainFlags(0x01);
            party.step("Hyper Train a party member's HP").frame();
            party.expect(party.frameData().textExact("HT").size() == untrainedMarkCount + 1, "the party card does not mark a Hyper Trained IV", "before=" + std::to_string(untrainedMarkCount) + " after=" + std::to_string(party.frameData().textExact("HT").size()));
        }
        partyMember->setHyperTrainFlags(savedPartyFlags);
    }

    /// The picker's search box.
    ///
    /// The property that matters is not "a filter happened" but that the filtered list still writes
    /// the value it DRAWS. `pickerSel` indexes the visible rows once a query is set, so anything
    /// that reads it as an index into the full list applies whatever happens to sit at that
    /// position -- naming one species on screen and writing another, with nothing to show for it.
    void testPickerSearch(Test::Context& context, TrainerViewScreen& s, Trainer::Trainer& tr) {
        Driver d(context, s, "picker-search");
        d.step("open the box view");
        gotoHome(d, s, 0);
        d.press(Btn::A).frame();
        if (!d.expect(s.detailViewActive, "could not open the box view")) return;

        // Land on an occupied slot; an empty one has no details page to open.
        int occupied = -1;
        const int box = s.selectedBoxIndex;
        if (box >= 0 && box < static_cast<int>(tr.boxes.size())) {
            for (int slot = 0; slot < static_cast<int>(tr.boxes[box].size()); ++slot)
                if (!Test::isEmptySlot(tr.boxes[box][slot].get())) { occupied = slot; break; }
        }
        if (occupied < 0) {
            context.note("this save's first box is empty, so the picker search was not exercised", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }
        s.selectedItemIndex = occupied;
        d.step("open the details modal").press(Btn::A).frame(2);
        if (!d.expect(s.details.active, "A on an occupied slot did not open the details modal")) return;

        expectOneNavBar(d, "details page open");

        s.details.selectedField = 32;                       // the Species row
        d.step("open the species picker").press(Btn::A).frame(2);
        if (!d.expect(s.pickerActive, "A on the Species row did not open a picker")) return;
        // The case the user reported: a picker drawn OVER the details page, which paints its own
        // bar. Two bars here means the page is still advertising Edit / Ribbons / Save while the
        // picker holds input.
        expectOneNavBar(d, "species picker open over the details page");
        d.expect(!s.overlayNavHint().empty(), "an open picker does not name its own controls, so the bar shows the page's");

        const int unfilteredCount = s.pickerVisibleCount();
        d.expect(unfilteredCount > 1, "the species picker offered nothing to search", "count=" + std::to_string(unfilteredCount));
        d.expect(!s.pickerSearch.isFiltering(), "a picker opened with a filter already applied");

        // The query is typed with the keyboard over the bottom of the screen, so the rows have to be
        // laid out above it -- with the selected one among them -- and while it is up the one bar names
        // ITS keys. Checked on the whole unfiltered list, the case that overruns the keyboard; the
        // keyboard is published the way UIManager does it, and nothing else about the picker changes.
        {
            UI::Dialogs::KeyboardState keyboard;
            Utils::KeyboardRequest request;
            request.header = "Search Species";
            request.maxChars = 32;
            keyboard.open(request, 1280, 720);
            UI::Dialogs::setActiveKeyboard(&keyboard);
            s.pickerSel = unfilteredCount / 2; // deep in the list, where the window has to scroll to it
            d.step("the picker drawn under an open keyboard").frame();
            bool everyRowAboveKeyboard = true, selectedRowShown = false;
            for (const auto& button : s.touchButtons) {
                if (button.hitY + button.hitHeight > keyboard.cardTop()) everyRowAboveKeyboard = false;
                if (button.buttonId == s.pickerSel) selectedRowShown = true;
            }
            d.expect(!s.touchButtons.empty(), "the picker drew no rows at all under the keyboard");
            d.expect(everyRowAboveKeyboard, "a picker row was laid out under the keyboard, where it cannot be seen", "keyboardTop=" + std::to_string(keyboard.cardTop()));
            d.expect(selectedRowShown, "the selected row was not among the rows left above the keyboard");
            expectOneNavBar(d, "keyboard open over the species picker");
            d.expect(s.overlayNavHint() == keyboard.navHint(), "the bar did not name the keyboard's keys while it held input");
            UI::Dialogs::setActiveKeyboard(nullptr);
            s.pickerSel = 0;
            d.frame();
        }

        // "chu" is in Pikachu, Raichu and Pichu, and in few enough other names that the list must
        // shrink. A query no dex can match would prove nothing about the filter working.
        d.step("search the species list").type("chu").press(Btn::Y).frame(2);
        const int filteredCount = s.pickerVisibleCount();
        d.expect(s.pickerSearch.isFiltering(), "typing a query did not put the picker into search mode");
        d.expect(filteredCount > 0, "searching \"chu\" matched no species at all");
        d.expect(filteredCount < unfilteredCount, "the search did not narrow the list", "before=" + std::to_string(unfilteredCount) + " after=" + std::to_string(filteredCount));

        // EVERY visible row must match, and each must resolve to the same value the row draws.
        bool everyRowMatches = true, everyRowResolves = true;
        for (int visibleRow = 0; visibleRow < filteredCount; ++visibleRow) {
            const int row = s.pickerRowForVisibleIndex(visibleRow);
            if (row < 0 || row >= s.pickerCount) { everyRowResolves = false; continue; }
            const int value = (s.pickerUsesOrder() && row < static_cast<int>(s.pickerOrder.size())) ? s.pickerOrder[row] : row;
            const std::string label = UI::Dialogs::pickerRowLabel(s, s.pickerKind, value);
            if (!s.pickerSearch.matches(label.c_str())) everyRowMatches = false;
        }
        d.expect(everyRowMatches,  "a row survived the filter without matching the query");
        d.expect(everyRowResolves, "a visible row did not map back to a row in the full list");

        // The cursor's value must be the one its row shows -- the whole point of the indirection.
        s.pickerSel = 0;
        const int firstRow = s.pickerRowForVisibleIndex(0);
        const int expectedValue = (s.pickerUsesOrder() && firstRow >= 0 && firstRow < static_cast<int>(s.pickerOrder.size())) ? s.pickerOrder[firstRow] : firstRow;
        d.expect(s.pickerSelectedValue() == expectedValue, "the selected value did not follow the filtered row", "value=" + std::to_string(s.pickerSelectedValue()) + " expected=" + std::to_string(expectedValue));
        d.step("the filtered list is on screen").see(UI::Dialogs::pickerRowLabel(s, s.pickerKind, expectedValue));

        // An empty query clears the filter -- the only way out, so it has to work.
        d.step("clear the search").type("").press(Btn::Y).frame(2);
        d.expect(!s.pickerSearch.isFiltering(), "an empty query did not clear the filter");
        d.expect(s.pickerVisibleCount() == unfilteredCount, "clearing the search did not restore the list", "restored=" + std::to_string(s.pickerVisibleCount()) + " original=" + std::to_string(unfilteredCount));

        // A query nothing matches must say so and refuse to apply, rather than writing row 0.
        d.step("search for something absent").type("zzzznotaspecies").press(Btn::Y).frame(2);
        d.expect(s.pickerVisibleCount() == 0, "a nonsense query still matched something");
        d.see("Nothing matches that search.");
        const Pokemon::Pokemon* before = s.detailsTargetPokemon();
        const uint16_t speciesBefore = before ? before->speciesID() : 0;
        d.step("A on an empty result does nothing").press(Btn::A).frame(2);
        const Pokemon::Pokemon* after = s.detailsTargetPokemon();
        d.expect(after && after->speciesID() == speciesBefore, "pressing A with no search results changed the Pokemon anyway");
        d.expect(s.pickerActive, "pressing A with no search results closed the picker");

        d.step("leave the picker").type("").press(Btn::Y).frame(2);
        d.press(Btn::B).frame(2);

        // A dead end that has an answer must give it.
        //
        // The met list is in-world PLACES. Who an egg came from -- "Nursery Couple", "a Nursery
        // worker", Riley -- is a different bank of ids belonging to the Egg Loc field, so searching
        // one here finds nothing, correctly. A user hit exactly that and reported the entry missing:
        // it was one row away with nothing to say so. The empty state names the row now.
        {
            const Pokemon::Pokemon* target = s.detailsTargetPokemon();
            const Names::LocationTable eggSources = target ? Names::getEggSourceLocationTable(target->originGame()) : Names::LocationTable{nullptr, 0};
            bool eggSourceMatchesQuery = false;
            for (uint16_t sourceIndex = 0; sourceIndex < eggSources.count; ++sourceIndex)
                if (eggSources.names[sourceIndex][0] != '\0' && UI::listSearchMatches(eggSources.names[sourceIndex], "nursery"))
                    eggSourceMatchesQuery = true;

            if (!target || !target->hasMetData() || !eggSourceMatchesQuery) {
                context.note("the met-location dead-end hint was not exercised: this origin has no egg-source list", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            } else {
                s.details.selectedField = 25;               // the Met row
                d.step("open the met-location picker").press(Btn::A).frame(2);
                if (d.expect(s.pickerActive, "A on the Met row did not open a picker")) {
                    d.step("search the met list for an egg source").type("nursery").press(Btn::Y).frame(2);
                    d.expect(s.pickerVisibleCount() == 0, "an egg source turned up in the met-location list, which holds places only", "matches=" + std::to_string(s.pickerVisibleCount()));
                    d.see("Egg Loc");
                    d.step("leave the met picker").type("").press(Btn::Y).frame(2);
                    d.press(Btn::B).frame(2);
                }
            }
        }
    }

    /// Hosts the on-screen keyboard the way UIManager does -- a screen underneath, the card on top --
    /// with a nav bar naming the keyboard's keys, as a real screen's bar does while it is up.
    class KeyboardHostScreen : public UI::UIScreen {
    public:
        UI::Dialogs::KeyboardState keyboard;
        std::vector<std::string> reportedTexts; // every text the keyboard handed its caller, in order

        void open(Utils::KeyboardRequest request) {
            reportedTexts.clear();
            request.textChanged = [this](const std::string& typedText) { reportedTexts.push_back(typedText); };
            keyboard.open(request, 1280, 720);
        }
        void update(const PadState& pad, const UI::TouchInput& touch) override { keyboard.update(pad, touch); }
        void draw(UI::PKSEFramebuffer& framebuffer) override {
            framebuffer.clear(UI::Colors::Background);
            UI::drawNavBar(framebuffer, keyboard.navHint());
            UI::Dialogs::drawKeyboard(keyboard, framebuffer);
        }
    };

    int keyIndexFor(const UI::Dialogs::KeyboardState& keyboard, UI::Dialogs::KeyboardKeyAction action, const std::string& label = std::string()) {
        for (int keyIndex = 0; keyIndex < static_cast<int>(keyboard.keys.size()); ++keyIndex) {
            const UI::Dialogs::KeyboardKey& key = keyboard.keys[keyIndex];
            if (key.action == action && (label.empty() || key.lowerLabel == label)) return keyIndex;
        }
        return -1;
    }

    void tapKey(Driver& d, const UI::Dialogs::KeyboardState& keyboard, int keyIndex) {
        if (keyIndex < 0) return;
        const UI::Dialogs::KeyboardKey& key = keyboard.keys[keyIndex];
        d.tap(key.keyX + key.keyWidth / 2, key.keyY + key.keyHeight / 2);
    }

    /// PKSE's own keyboard, driven with nothing but pad presses and taps. Needs no save: every rule
    /// it has is about the text it holds and the keys it draws.
    void testOnScreenKeyboard(Test::Context& context) {
        using UI::Dialogs::KeyboardKeyAction;
        KeyboardHostScreen host;
        const UI::Dialogs::KeyboardState& keyboard = host.keyboard;
        Driver d(context, host, "keyboard");
        const auto textIs = [&](const std::string& expected, const std::string& what) {
            d.expect(keyboard.text == expected, what, "text=\"" + keyboard.text + "\" expected=\"" + expected + "\"");
        };

        Utils::KeyboardRequest nickname;
        nickname.header = "Nickname";
        nickname.guide = "Nickname";
        nickname.initialText = "Pika";
        nickname.maxChars = 12;
        host.open(nickname);
        d.step("open a text prompt").frame();
        d.see("Nickname");
        d.see("Pika");
        d.see("4 / 12");
        d.see("Shift");
        expectOneNavBar(d, "keyboard open");

        // The card sits above the nav bar, and every key is inside it and a full touch target.
        d.expect(keyboard.cardY + keyboard.cardHeight <= 720 - UI::NAV_BAR_HEIGHT, "the keyboard card runs into the nav bar", "cardBottom=" + std::to_string(keyboard.cardY + keyboard.cardHeight));
        bool everyKeyInCard = true, everyKeyTouchable = true, keysOverlap = false;
        for (size_t keyIndex = 0; keyIndex < keyboard.keys.size(); ++keyIndex) {
            const UI::Dialogs::KeyboardKey& key = keyboard.keys[keyIndex];
            if (key.keyX < keyboard.cardX || key.keyX + key.keyWidth > keyboard.cardX + keyboard.cardWidth || key.keyY < keyboard.cardY || key.keyY + key.keyHeight > keyboard.cardY + keyboard.cardHeight)
                everyKeyInCard = false;
            if (key.keyHeight < UI::TouchTargetMin) everyKeyTouchable = false;
            for (size_t otherIndex = keyIndex + 1; otherIndex < keyboard.keys.size(); ++otherIndex) {
                const UI::Dialogs::KeyboardKey& other = keyboard.keys[otherIndex];
                if (key.keyX < other.keyX + other.keyWidth && other.keyX < key.keyX + key.keyWidth && key.keyY < other.keyY + other.keyHeight && other.keyY < key.keyY + key.keyHeight)
                    keysOverlap = true;
            }
        }
        d.expect(everyKeyInCard, "a key is drawn outside the keyboard card");
        d.expect(everyKeyTouchable, "a key is shorter than the minimum touch target");
        d.expect(!keysOverlap, "two keys overlap, so one tap could land on either");

        // The cursor opens on q; two steps right is e.
        d.step("type with the controller").press(Btn::Right, 2).press(Btn::A);
        textIs("Pikae", "A did not type the focused key");
        d.expect(!host.reportedTexts.empty() && host.reportedTexts.back() == "Pikae", "an edit was not handed to the prompt's caller as it happened -- a live search could not follow it");

        d.step("shift capitalises one letter").press(Btn::X);
        d.expect(!d.frameData().ops("shift-on").empty(), "X did not turn shift on");
        d.expect(!d.frameData().textExact("E").empty(), "the letter keys did not show capitals while shifted");
        d.press(Btn::A);
        textIs("PikaeE", "a shifted key did not type its capital");
        d.press(Btn::A);
        textIs("PikaeEe", "shift stayed on after one letter");

        d.step("B deletes").press(Btn::B);
        textIs("PikaeE", "B did not delete the character before the caret");
        d.step("hold B through the whole field").hold(Btn::B, 120);
        textIs("", "holding B did not keep deleting");
        d.expect(!keyboard.finished, "holding B ran on past the empty field and closed the prompt");
        d.step("a fresh B on an empty field cancels").press(Btn::B);
        d.expect(keyboard.finished && !keyboard.accepted, "a fresh B on an empty field did not cancel");
        d.expect(!keyboard.result().accepted && keyboard.result().text.empty(), "a cancelled prompt still handed back text -- the caller would blank the field");

        // The left stick steps the selection exactly as the D-pad does: one key per push, nothing
        // below the press threshold, the dominant axis on a diagonal, and the same repeat when held.
        // Each case types whatever key it lands on into an empty field, so the two routes compare
        // by the text they produce.
        Utils::KeyboardRequest stickPrompt;
        stickPrompt.header = "Stick";
        stickPrompt.guide = "Stick";
        stickPrompt.maxChars = 12;
        const auto typedAfter = [&](const std::function<void()>& navigate) {
            host.open(stickPrompt);
            d.frame();
            navigate();
            d.press(Btn::A);
            return keyboard.text;
        };
        d.step("the left stick moves the keyboard selection like the D-pad");
        const std::string dpadOneStep = typedAfter([&] { d.press(Btn::Right); });
        const std::string stickOneStep = typedAfter([&] { d.tilt(20000, 0); });
        d.expect(!dpadOneStep.empty() && stickOneStep == dpadOneStep, "a stick push right did not move like D-pad Right", "stick=\"" + stickOneStep + "\" dpad=\"" + dpadOneStep + "\"");
        const std::string noStep = typedAfter([] {});
        d.expect(typedAfter([&] { d.tilt(8000, 0); }) == noStep, "a stick barely off centre moved the selection");
        d.expect(typedAfter([&] { d.tilt(20000, 12000); }) == dpadOneStep, "a diagonal stick push did not follow its dominant axis");
        d.expect(typedAfter([&] { d.tilt(0, -20000); }) == typedAfter([&] { d.press(Btn::Down); }), "a stick push down did not move like D-pad Down");
        const std::string dpadHeld = typedAfter([&] { d.hold(Btn::Right, 40); });
        const std::string stickHeld = typedAfter([&] { d.tilt(20000, 0, 40); });
        d.expect(dpadHeld != dpadOneStep && stickHeld == dpadHeld, "a held stick did not repeat like a held D-pad", "stick=\"" + stickHeld + "\" dpad=\"" + dpadHeld + "\"");

        Utils::KeyboardRequest boxName;
        boxName.header = "Rename Box";
        boxName.guide = "Box name";
        boxName.initialText = "Ab";
        boxName.maxChars = 8;
        host.open(boxName);
        d.step("type at the caret, not the end").frame().press(Btn::L).press(Btn::A);
        textIs("Aqb", "a key typed with the caret moved did not land at the caret");
        d.press(Btn::Y);
        textIs("Aq b", "Y did not type a space at the caret");

        boxName.initialText = "abcdefgh";
        host.open(boxName);
        d.step("a full field refuses a character").frame().press(Btn::A);
        textIs("abcdefgh", "a character was typed past the prompt's length cap");
        {
            const auto tallies = d.frameData().textExact("8 / 8");
            d.expect(!tallies.empty() && tallies.front()->color.red == UI::Colors::ShinyStar.red && tallies.front()->color.green == UI::Colors::ShinyStar.green, "the character count did not flag the refused character");
        }

        d.step("ZL/ZR switch layouts").press(Btn::ZR);
        d.expect(keyboard.page == UI::Dialogs::KeyboardPage::Symbols, "ZR did not move to the symbols");
        d.expect(!d.frameData().textExact("1").empty() && !d.frameData().textExact("\u2642").empty(), "the symbols page did not draw its digits and gender signs");
        d.press(Btn::ZR);
        d.expect(keyboard.page == UI::Dialogs::KeyboardPage::Accents, "ZR did not move on to the accents");
        d.expect(!d.frameData().textExact("\u00e9").empty(), "the accents page did not draw its letters");
        d.press(Btn::ZR);
        d.expect(keyboard.page == UI::Dialogs::KeyboardPage::Letters, "ZR did not wrap back to the letters");
        d.press(Btn::ZL);
        d.expect(keyboard.page == UI::Dialogs::KeyboardPage::Accents, "ZL did not wrap back to the accents");

        boxName.initialText = "";
        host.open(boxName);
        d.step("tap a key").frame();
        d.see("Box name"); // the guide stands in for an empty field...
        tapKey(d, keyboard, keyIndexFor(keyboard, KeyboardKeyAction::Type, "w"));
        textIs("w", "tapping a key did not type it");
        d.notSee("Box name"); // ...and gives way to the first character
        const int okIndex = keyIndexFor(keyboard, KeyboardKeyAction::Accept);
        d.step("tap OK").frame();
        tapKey(d, keyboard, okIndex);
        d.expect(!keyboard.finished, "a tapped OK closed the prompt before its press was drawn");
        d.expect(keyboard.pressedKeyIndex == okIndex, "a tapped OK was not drawn held");
        d.frame(UI::Dialogs::KeyboardState::TAP_PRESS_FRAMES);
        d.expect(keyboard.finished && keyboard.accepted && keyboard.result().text == "w", "a tapped OK did not accept the text once its press had shown");

        boxName.initialText = "Hi";
        host.open(boxName);
        d.step("the bar's + badge accepts").frame();
        const UIHost::Draw* barOk = nullptr;
        for (const UIHost::Draw* label : d.frameData().textExact("OK"))
            if (label->drawY > 720 - UI::NAV_BAR_HEIGHT) barOk = label;
        if (d.expect(barOk != nullptr, "the nav bar did not offer +: OK")) {
            d.tap(barOk->centerX(), barOk->centerY());
            d.expect(keyboard.finished && keyboard.accepted, "tapping +: OK in the bar did not accept");
        }

        boxName.initialText = "Hi";
        host.open(boxName);
        d.step("minus cancels").frame().press(Btn::Minus);
        d.expect(keyboard.finished && !keyboard.accepted, "- did not cancel the prompt");
        boxName.initialText = "Hi";
        host.open(boxName);
        d.step("plus accepts").frame().press(Btn::Plus);
        d.expect(keyboard.finished && keyboard.accepted && keyboard.result().text == "Hi", "+ did not accept");

        // A number opens with its old value SELECTED: the first digit replaces it.
        Utils::KeyboardRequest money;
        money.header = "Money";
        money.guide = "0 - 9999999";
        money.initialText = "1500";
        money.maxChars = 7;
        money.digitsOnly = true;
        host.open(money);
        d.step("open a number prompt").frame();
        d.see("1500");
        d.see("0 - 9999999");
        d.notSee("+/-");
        d.notSee("Shift");
        d.expect(keyboard.textSelected, "a number prompt did not open with its old value selected");
        d.press(Btn::A);
        textIs("1", "the first digit did not replace the selected value");
        d.press(Btn::Right).press(Btn::A);
        textIs("12", "a second digit was not appended");
        d.press(Btn::Y);
        textIs("12", "Y typed into a number");

        money.allowNegative = true;
        money.initialText = "5";
        money.maxChars = 2;
        host.open(money);
        d.step("the sign key").frame();
        d.see("+/-");
        tapKey(d, keyboard, keyIndexFor(keyboard, KeyboardKeyAction::ToggleSign));
        textIs("-5", "the sign key did not negate the value");
        tapKey(d, keyboard, keyIndexFor(keyboard, KeyboardKeyAction::ToggleSign));
        textIs("5", "the sign key did not put the value back");

        // The hand-off: the console's keyboard is the only way to type Japanese, Chinese or Korean,
        // and whatever it returns is the answer -- there is no second OK to press here.
        Utils::KeyboardRequest name;
        name.header = "Nickname";
        name.guide = "Nickname";
        name.maxChars = 20;
        host.open(name);
        d.step("hand off to the console's keyboard").frame();
        UIHost::clearPrompts();
        UIHost::queueText("\u30d4\u30ab\u30c1\u30e5\u30a6");
        tapKey(d, keyboard, keyIndexFor(keyboard, KeyboardKeyAction::SystemKeyboard));
        d.frame(UI::Dialogs::KeyboardState::TAP_PRESS_FRAMES);
        d.expect(UIHost::promptsRaised() == 1, "the hand-off key did not open the console's keyboard", "raised=" + std::to_string(UIHost::promptsRaised()));
        d.expect(keyboard.finished && keyboard.accepted && keyboard.text == "\u30d4\u30ab\u30c1\u30e5\u30a6", "text typed on the console's keyboard did not come back as the answer");

        name.initialText = "Hi";
        host.open(name);
        d.step("a cancelled hand-off comes back").frame();
        UIHost::clearPrompts();
        UIHost::queueCancel();
        tapKey(d, keyboard, keyIndexFor(keyboard, KeyboardKeyAction::SystemKeyboard));
        // B held from answering the console's keyboard is still down when it returns. It was
        // meant for that keyboard, and must not delete a character from this one.
        d.hold(Btn::B, UI::Dialogs::KeyboardState::TAP_PRESS_FRAMES + 4);
        d.expect(UIHost::promptsRaised() == 1, "the hand-off key did not open the console's keyboard");
        d.expect(!keyboard.finished, "cancelling the console's keyboard also closed this one");
        textIs("Hi", "a button held through the console's keyboard reached this one as a press");
        tapKey(d, keyboard, keyIndexFor(keyboard, KeyboardKeyAction::Type, "q"));
        textIs("Hiq", "the keyboard did not take input again after a cancelled hand-off");
        UIHost::clearPrompts();
    }

    /// Press buttons at the UI for a while and see whether it holds together.
    ///
    /// No assertions of its own -- the driver's per-frame invariants do the work: every frame must
    /// draw something, and nothing may be drawn completely off-screen. What this really tests is
    /// that no reachable input sequence crashes, which is the failure a user reports as "it froze"
    /// and which nothing else here would ever reach. The sequence is a fixed-seed LCG so a failure
    /// reproduces exactly.
    void testInputFuzz(Test::Context& context, TrainerViewScreen& s, int frames) {
        static const u64 buttonMasks[] = { Btn::A, Btn::B, Btn::Up, Btn::Down, Btn::Left, Btn::Right,
                                        Btn::X, Btn::Y, Btn::L, Btn::R };
        Driver d(context, s, "fuzz");
        d.step("random input sweep");
        uint32_t seed = 0x5EED1234u;
        for (int i = 0; i < frames; ++i) {
            seed = seed * 1664525u + 1013904223u;
            const u64 b = buttonMasks[(seed >> 16) % (sizeof buttonMasks / sizeof buttonMasks[0])];
            // Answer any prompt the sweep happens to open, so it does not stall on a blocked applet.
            if (UIHost::pendingPrompts() == 0) UIHost::queueText("FUZZ");
            d.press(b);
        }
        UIHost::clearPrompts();
    }

    /// FAVORITE FOLDERS AND THE LAST FOLDER, through the screen's own keys. The dialog suite checks
    /// the state; this checks what only the screen decides: that ZL and ZR reach the browser at all,
    /// that the bar names them, and that each browser reopens in ITS OWN last folder -- the trade
    /// partner picker where the last save was picked, the import browser where the last bank was.
    /// Settings go to a scratch file, so nothing here lands in a real settings.cfg.
    void testBrowserFolders(Test::Context& context, TrainerViewScreen& s) {
        const std::string root = "/tmp/pkse-uitest-folders";
        const std::string saveFolder = root + "/saves";
        const std::string importFolder = root + "/banks";
        const std::string settingsFile = root + "-settings.cfg";
        mkdir(root.c_str(), 0777);
        mkdir(saveFolder.c_str(), 0777);
        mkdir(importFolder.c_str(), 0777);
        const auto writeOneByte = [](const std::string& path) {
            if (FILE* handle = std::fopen(path.c_str(), "wb")) { std::fputc(0, handle); std::fclose(handle); }
        };
        writeOneByte(saveFolder + "/Pokemon Ruby.sav");
        writeOneByte(importFolder + "/notabank.bnk");
        std::remove(settingsFile.c_str());
        Utils::setSettingsFileOverride(settingsFile);
        Utils::loadSettings();

        Driver d(context, s, "browser-folders");
        const auto rowOf = [&](const std::string& name) {
            for (size_t row = 0; row < s.fileBrowser.entries.size(); ++row)
                if (s.fileBrowser.entries[row].name == name) return static_cast<int>(row);
            return -1;
        };

        // The trade partner picker opens in the save browser's last folder.
        Utils::setLastFolder(Utils::LastFolderKind::SaveFiles, saveFolder);
        Utils::setLastFolder(Utils::LastFolderKind::ImportFiles, importFolder);
        d.step("open the trade partner picker");
        s.openTradePartnerBrowser();
        d.frame();
        d.expect(s.fileBrowser.active && s.fileBrowser.directory == saveFolder, "the trade partner picker did not reopen in the last save folder", "got=\"" + s.fileBrowser.directory + "\"");
        d.see("ZL");
        d.see("Favorites");
        d.see("Add Favorite");
        expectOneNavBar(d, "trade partner file browser open");

        d.step("ZR favorites the folder");
        d.press(Btn::ZR).frame();
        d.expect(Utils::isFavoriteFolder(saveFolder), "ZR in the file browser did not favorite the folder", "favorites=" + std::to_string(Utils::favoriteFolders().size()));
        d.see("Unfavorite");
        d.expect(d.frameData().has("\xE2\x98\x85"), "a favorite folder has no star beside its path");

        // Somewhere else, then back through the list.
        d.step("B up to the fixture root, then ZL");
        d.press(Btn::B).frame();
        d.expect(s.fileBrowser.directory == root, "B did not go up out of the save folder", "got=\"" + s.fileBrowser.directory + "\"");
        d.see("Add Favorite");
        d.press(Btn::ZL).frame();
        d.expect(s.fileBrowser.showFavorites, "ZL did not open the favorites list");
        d.see("Favorite folders");
        d.see("Remove Favorite");
        expectOneNavBar(d, "favorite folders list open");
        d.see(saveFolder);
        d.step("A on the favorite");
        d.press(Btn::A).frame();
        d.expect(!s.fileBrowser.showFavorites && s.fileBrowser.directory == saveFolder, "A on a favorite did not open that folder", "got=\"" + s.fileBrowser.directory + "\"");

        // Picking a file remembers the folder, even though no Pokemon is open to trade.
        d.step("pick the save");
        Utils::setLastFolder(Utils::LastFolderKind::SaveFiles, root);
        const int saveRow = rowOf("Pokemon Ruby.sav");
        if (d.expect(saveRow >= 0, "the fixture save is not listed")) {
            s.fileBrowser.selectedIndex = saveRow;
            d.press(Btn::A).frame();
            d.expect(Utils::lastFolder(Utils::LastFolderKind::SaveFiles) == saveFolder, "picking a trade partner save did not remember its folder", "got=\"" + Utils::lastFolder(Utils::LastFolderKind::SaveFiles) + "\"");
            d.expect(Utils::lastFolder(Utils::LastFolderKind::ImportFiles) == importFolder, "picking a trade partner save moved the import browser's last folder");
        }
        s.fileBrowser.close();

        // The import browser keeps its own.
        d.step("open the import browser");
        s.openPKSMImportBrowser();
        d.frame();
        d.expect(s.fileBrowser.active && s.fileBrowser.directory == importFolder, "the import browser did not reopen in its own last folder", "got=\"" + s.fileBrowser.directory + "\"");
        d.press(Btn::ZL).frame();
        d.expect(s.fileBrowser.showFavorites && rowOf(saveFolder) == 0, "the import browser does not list the favorites the save browser added");
        d.press(Btn::B).frame();
        d.expect(!s.fileBrowser.showFavorites && s.fileBrowser.directory == importFolder, "B in the favorites list did not return to the import folder", "got=\"" + s.fileBrowser.directory + "\"");
        Utils::setLastFolder(Utils::LastFolderKind::ImportFiles, root);
        const int bankRow = rowOf("notabank.bnk");
        if (d.expect(bankRow >= 0, "the fixture bank is not listed")) {
            s.fileBrowser.selectedIndex = bankRow;
            d.step("pick the bank");
            d.press(Btn::A).frame();
            d.expect(Utils::lastFolder(Utils::LastFolderKind::ImportFiles) == importFolder, "picking a bank did not remember its folder", "got=\"" + Utils::lastFolder(Utils::LastFolderKind::ImportFiles) + "\"");
            d.expect(Utils::lastFolder(Utils::LastFolderKind::SaveFiles) == saveFolder, "picking a bank moved the save browser's last folder");
        }
        s.pksmImport.reset();
        s.fileBrowser.close();

        std::remove((saveFolder + "/Pokemon Ruby.sav").c_str());
        std::remove((importFolder + "/notabank.bnk").c_str());
        rmdir(saveFolder.c_str());
        rmdir(importFolder.c_str());
        rmdir(root.c_str());
        std::remove(settingsFile.c_str());
        Utils::setSettingsFileOverride("");
        Utils::loadSettings();
    }

    void runUI(Test::Report& report, const std::string& target, Enums::GameVersion group, Trainer::Trainer& tr, u64 titleId) {
        Test::Context context;
        context.report = &report; context.suite = "ui"; context.target = target;
        // The trainer names its own title, the same way the on-console runner asks a loose save.
        context.group = group; context.version = tr.getGameVersion(); context.trainer = &tr; context.titleId = titleId;
        report.setScope(target, "ui");

        std::printf("  ui  %s\n", target.c_str());

        // A fresh screen per test: these drive real state, and a test that inherited the previous
        // one's cursor position would pass or fail depending on what ran before it.
        auto fresh = [&]() {
            return std::make_unique<TrainerViewScreen>(tr, target, std::string("/tmp/pkse-uitest"), titleId, AccountUid{}, false);
        };

        { auto s = fresh(); testOpens(context, *s, tr); }
        { auto s = fresh(); testEveryDestinationOpensAndCloses(context, *s); }
        { auto s = fresh(); testBoxGridGeometry(context, *s, tr); }
        { auto s = fresh(); testNoSpritesInEmptySlots(context, *s, tr); }
        { auto s = fresh(); testSummaryShowsLegality(context, *s, tr); }
        { auto s = fresh(); testLegalityOverlayStaysInCard(context, *s, tr); }
        { auto s = fresh(); testCursorMoves(context, *s); }
        { auto s = fresh(); testTrainerCardShowsSaveData(context, *s, tr); }
        { auto s = fresh(); testBoxRename(context, *s, tr); }
        { auto s = fresh(); testEggTextIsNotForeign(context, *s, tr); }
        { auto s = fresh(); testPickerSearch(context, *s, tr); }
        { auto s = fresh(); testAlphaToggle(context, *s, tr); }
        { auto s = fresh(); testHyperTraining(context, *s, tr); }
        { auto s = fresh(); testHyperTrainingPartyMark(context, *s, tr); }
        { auto s = fresh(); testItemsKeys(context, *s, tr); }
        { auto s = fresh(); testBoxMenus(context, *s, tr); }
        { auto s = fresh(); testWithdrawSetsHandler(context, *s, tr); }
        { auto s = fresh(); testTapIsSeenBeforeItActs(context, *s, tr); }
        // The save dialog, raised the way X raises it from the HOME menu: its keys go over its scrim.
        { auto s = fresh(); Driver d(context, *s, "save-dialog-bar"); d.step("raise the save dialog"); s->saveConfirmActive = true; d.frame(); expectOneNavBar(d, "save dialog open"); s->saveConfirmActive = false; }
        // Once, not per save: the browsers do not depend on which save is open.
        static bool browserFoldersTested = false;
        if (!browserFoldersTested) { browserFoldersTested = true; auto s = fresh(); testBrowserFolders(context, *s); }
        { auto s = fresh(); testInputFuzz(context, *s, 240); }

        // The ON-CONSOLE suite, run here too. It drives the same screens through the same journeys
        // and applies the same change-verification; the only thing it cannot do off-console is
        // render, so context.framebuffer stays null and the draw path is skipped. Running it here is what lets
        // the journeys be debugged in seconds instead of by pushing an .nro to a console.
        {
            Test::Context uctx;
            uctx.report = &report; uctx.suite = "ui"; uctx.target = target;
            uctx.group = group; uctx.version = tr.getGameVersion(); uctx.trainer = &tr; uctx.titleId = titleId;
            uctx.workDirectory = "/tmp/pkse-uitest-work";
            report.setScope(target, "ui");
            // The console runs the journeys with swkbd refused (Runner::start), because the applet
            // BLOCKS and an automated pass that reaches one waits for a human. Mirror that here, or
            // the host would quietly answer prompts the console cannot.
            g_suppressKeyboard = true;
            const int chunks = Test::uiPlan(uctx);
            std::printf("  ui-journeys %s  (%d chunks)\n", target.c_str(), chunks);
            // Draw the driven screen after every chunk, exactly as TestRunnerScreen does on console.
            // Without this the host ran the whole walkthrough without ever DEREFERENCING the screen
            // it publishes, which is how a driven screen left pointing at a freed trainer passed
            // here and crashed on hardware. Drawing into the recorder costs little and closes the
            // one gap between the two runs.
            UI::PKSEFramebuffer probe;
            for (int c = 0; c < chunks; ++c) {
                report.setScope(target, "ui");
                Test::uiRun(uctx, c);
                if (UI::UIScreen* driven = Test::Runner::drivenScreen()) driven->draw(probe);
            }
            // The suite MUST have torn itself down: the runner frees the trainer the moment it moves
            // to the next target, so anything still published here outlives the data behind it.
            g_suppressKeyboard = false;
            report.setScope(target, "ui");
            uctx.check(Test::Runner::drivenScreen() == nullptr, "the ui suite finished with a screen still published as the driven screen", "chunks=" + std::to_string(chunks));
        }

        //
        // A save with no Pokemon in it is a real thing a user has -- a fresh playthrough, a save
        // they cleared out -- and the on-console run found two of them. It is also the state the
        // journeys are least able to survive: A on an empty box slot opens the CREATOR, so the step
        // that means "open the species picker" instead APPLIES a species and writes a Pokemon into
        // the box. The editor is dirty from then on, every later B raises the save prompt instead of
        // going back, and each remaining journey reports its own navigation as broken. Two such
        // saves produced 84 failures, not one of which was about the thing being tested.
        //
        // The suite must SKIP those steps and say so, not press through them. This runs the whole
        // walkthrough against an emptied copy and requires it to add no failures at all.
        {
            for (auto& box : tr.boxes)
                for (auto& slot : box)
                    slot.reset();
            for (auto& member : tr.party)
                member.reset();

            const int failuresBefore = report.failures();
            Test::Context uctx;
            uctx.report = &report; uctx.suite = "ui"; uctx.target = target;
            uctx.group = group; uctx.version = tr.getGameVersion(); uctx.trainer = &tr; uctx.titleId = titleId;
            uctx.workDirectory = "/tmp/pkse-uitest-work";
            report.setScope(target, "ui");
            // The console runs the journeys with swkbd refused (Runner::start), because the applet
            // BLOCKS and an automated pass that reaches one waits for a human. Mirror that here, or
            // the host would quietly answer prompts the console cannot.
            g_suppressKeyboard = true;
            const int chunks = Test::uiPlan(uctx);
            std::printf("  ui-journeys %s (emptied save)  (%d chunks)\n", target.c_str(), chunks);
            UI::PKSEFramebuffer probe;
            for (int c = 0; c < chunks; ++c) {
                report.setScope(target, "ui");
                Test::uiRun(uctx, c);
                if (UI::UIScreen* driven = Test::Runner::drivenScreen()) driven->draw(probe);
            }
            g_suppressKeyboard = false;
            report.setScope(target, "ui");
            uctx.check(report.failures() == failuresBefore, "walking a save with no Pokemon in it produced failures; the steps that need one must be skipped, not pressed through", "added=" + std::to_string(report.failures() - failuresBefore));
        }
    }

    template <typename TrainerT>
    bool openSCBlock(Test::Report& report, const char* label, Enums::GameVersion group, const std::string& path, u64 titleId) {
        std::vector<uint8_t> bytes = readFile(path);
        if (bytes.empty()) return false;
        std::vector<Save::Block> blocks = Encryption::decrypt(bytes.data(), bytes.size());
        if (blocks.empty()) { std::printf("  !! %s: decrypt produced no blocks\n", label); return false; }
        TrainerT trainer(std::move(blocks));
        runUI(report, label, group, trainer, titleId);
        return true;
    }

    /// A loose save, read the way its own class reads one. Every group from Gen 1 to Gen 7 bar Gen 3
    /// once had Items pouches that L/R could not reach, and it shipped because no host run opened
    /// one of their saves: the walk is the same for every group, but each one answers for its own bag.
    template <typename TrainerT>
    bool openLoose(Test::Report& report, const char* label, Enums::GameVersion group, const std::string& path) {
        std::vector<uint8_t> bytes = readFile(path);
        if (bytes.empty()) return false;
        // As Save::openExternalSave does: an RTC footer comes off only when the file does not
        // detect as it stands. A class must never be built over bytes its own detect() refuses.
        if (!TrainerT::detect(bytes) && !(Save::stripRtcFooter(bytes) && TrainerT::detect(bytes))) {
            std::printf("  !! %s: not a save this class opens: %s\n", label, path.c_str());
            return false;
        }
        TrainerT trainer(std::move(bytes), path);
        runUI(report, label, group, trainer, 0);
        return true;
    }

    /// TrainerT's own detect(); Gen 1's also reports the Japanese layout, which this does not need.
    template <typename TrainerT>
    bool detectsAs(const std::vector<uint8_t>& bytes) {
        if constexpr (std::is_same_v<TrainerT, Trainer::Trainer1RBY>) {
            bool japanese = false;
            return TrainerT::detect(bytes, japanese);
        } else {
            return TrainerT::detect(bytes);
        }
    }

    /// A loose save file, read and written by its own class.
    template <typename TrainerT>
    SaveLoader looseLoader(const std::string& path) {
        return {
            [path]() -> std::unique_ptr<Trainer::Trainer> {
                std::vector<uint8_t> bytes = readFile(path);
                if (bytes.empty()) return nullptr;
                if (!detectsAs<TrainerT>(bytes) && !(Save::stripRtcFooter(bytes) && detectsAs<TrainerT>(bytes))) return nullptr;
                return std::make_unique<TrainerT>(std::move(bytes), path);
            },
            [path](Trainer::Trainer& trainer) -> std::unique_ptr<Trainer::Trainer> {
                std::vector<uint8_t> written = static_cast<TrainerT&>(trainer).serialize();
                return std::make_unique<TrainerT>(std::move(written), path);
            }};
    }

    /// A Switch save of encrypted blocks.
    template <typename TrainerT>
    SaveLoader scBlockLoader(const std::string& path) {
        return {
            [path]() -> std::unique_ptr<Trainer::Trainer> {
                std::vector<uint8_t> bytes = readFile(path);
                if (bytes.empty()) return nullptr;
                std::vector<Save::Block> blocks = Encryption::decrypt(bytes.data(), bytes.size());
                if (blocks.empty()) return nullptr;
                return std::make_unique<TrainerT>(std::move(blocks));
            },
            [](Trainer::Trainer& trainer) -> std::unique_ptr<Trainer::Trainer> {
                trainer.updateItemBlock();
                std::vector<uint8_t> written = Encryption::encrypt(trainer.getBlocks());
                std::vector<Save::Block> blocks = Encryption::decrypt(written.data(), written.size());
                if (blocks.empty()) return nullptr;
                return std::make_unique<TrainerT>(std::move(blocks));
            }};
    }

    /// Add All Items on every format PKSE edits, from the save tree. Separate from runUI because one
    /// of these (Ruby/Sapphire/Emerald) is not walked there, and this test needs each one.
    int runAddAllItems(Test::Report& report, const std::string& saveRoot) {
        struct Target { const char* label; Enums::GameVersion group; SaveLoader loader; };
        const std::string letsGoPath = SaveTree::letsGoPikachu(saveRoot);
        std::string bdspPath = SaveTree::folderFor(saveRoot, "Shining Pearl") + "/SaveData.bin";
        if (readFile(bdspPath).empty()) bdspPath = SaveTree::forTitle(saveRoot, "Shining Pearl");
        const std::vector<Target> targets = {
            {"Red", Enums::GameVersion::RBY, looseLoader<Trainer::Trainer1RBY>(SaveTree::red(saveRoot))},
            {"Gold", Enums::GameVersion::GSC, looseLoader<Trainer::Trainer2GSC>(SaveTree::forTitle(saveRoot, "Gold"))},
            {"Crystal", Enums::GameVersion::GSC, looseLoader<Trainer::Trainer2GSC>(SaveTree::forTitle(saveRoot, "Crystal"))},
            {"Ruby", Enums::GameVersion::RSE, looseLoader<Trainer::Trainer3RSE>(SaveTree::ruby(saveRoot))},
            {"Emerald", Enums::GameVersion::RSE, looseLoader<Trainer::Trainer3RSE>(SaveTree::emerald(saveRoot))},
            {"FireRed", Enums::GameVersion::FRLG, looseLoader<Trainer::Trainer3FRLG>(SaveTree::fireRed(saveRoot))},
            {"Pearl", Enums::GameVersion::DP, looseLoader<Trainer::Trainer4DP>(SaveTree::forTitle(saveRoot, "Pearl"))},
            {"Platinum", Enums::GameVersion::PT, looseLoader<Trainer::Trainer4PT>(SaveTree::forTitle(saveRoot, "Platinum"))},
            {"SoulSilver", Enums::GameVersion::HGSS, looseLoader<Trainer::Trainer4HGSS>(SaveTree::forTitle(saveRoot, "SoulSilver"))},
            {"Black", Enums::GameVersion::BW, looseLoader<Trainer::Trainer5BW>(SaveTree::forTitle(saveRoot, "Black"))},
            {"Black 2", Enums::GameVersion::B2W2, looseLoader<Trainer::Trainer5B2W2>(SaveTree::forTitle(saveRoot, "Black 2"))},
            {"X", Enums::GameVersion::XY, looseLoader<Trainer::Trainer6XY>(SaveTree::forTitle(saveRoot, "X"))},
            {"Omega Ruby", Enums::GameVersion::ORAS, looseLoader<Trainer::Trainer6ORAS>(SaveTree::forTitle(saveRoot, "Omega Ruby"))},
            {"Sun", Enums::GameVersion::SM, looseLoader<Trainer::Trainer7SM>(SaveTree::forTitle(saveRoot, "Sun"))},
            {"Ultra Sun", Enums::GameVersion::USUM, looseLoader<Trainer::Trainer7USUM>(SaveTree::ultraSun(saveRoot))},
            {"Let's Go Pikachu", Enums::GameVersion::GG, SaveLoader{
                [letsGoPath]() -> std::unique_ptr<Trainer::Trainer> {
                    std::vector<uint8_t> bytes = readFile(letsGoPath);
                    if (bytes.size() < Trainer::SAVE_SIZE7_LGPE) return nullptr;
                    std::vector<uint8_t> area(bytes.begin(), bytes.begin() + Trainer::SAVE_SIZE7_LGPE);
                    return std::make_unique<Trainer::Trainer7LGPE>(Trainer::createBlocksFromSaveData7LGPE(area));
                },
                [letsGoPath](Trainer::Trainer& trainer) -> std::unique_ptr<Trainer::Trainer> {
                    trainer.updateItemBlock();
                    std::vector<uint8_t> bytes = readFile(letsGoPath);
                    if (bytes.size() < Trainer::SAVE_SIZE7_LGPE) return nullptr;
                    Trainer::writeBlocksToSaveData7LGPE(bytes, trainer.getBlocks());
                    std::vector<uint8_t> area(bytes.begin(), bytes.begin() + Trainer::SAVE_SIZE7_LGPE);
                    return std::make_unique<Trainer::Trainer7LGPE>(Trainer::createBlocksFromSaveData7LGPE(area));
                }}},
            {"Shield", Enums::GameVersion::SWSH, scBlockLoader<Trainer::Trainer8SWSH>(SaveTree::shield(saveRoot))},
            {"Shining Pearl", Enums::GameVersion::BDSP, SaveLoader{
                [bdspPath]() -> std::unique_ptr<Trainer::Trainer> {
                    std::vector<uint8_t> bytes = readFile(bdspPath);
                    if (bytes.empty()) return nullptr;
                    return std::make_unique<Trainer::Trainer8BDSP>(std::move(bytes));
                },
                [](Trainer::Trainer& trainer) -> std::unique_ptr<Trainer::Trainer> {
                    auto& bdsp = static_cast<Trainer::Trainer8BDSP&>(trainer);
                    bdsp.updateItemBlock();
                    bdsp.recomputeHash();
                    return std::make_unique<Trainer::Trainer8BDSP>(bdsp.getSaveData());
                }}},
            {"Legends Arceus", Enums::GameVersion::PLA, scBlockLoader<Trainer::Trainer8LA>(SaveTree::legendsArceus(saveRoot))},
            {"Violet", Enums::GameVersion::SV, scBlockLoader<Trainer::Trainer9SV>(SaveTree::violet(saveRoot))},
            {"Legends Z-A", Enums::GameVersion::ZA, scBlockLoader<Trainer::Trainer9LZA>(SaveTree::legendsZA(saveRoot))},
        };
        std::set<Enums::GameVersion> groupsTested;
        for (const Target& target : targets) {
            if (!target.loader.open()) {
                report.setScope(target.label, "ui");
                Test::Context context; context.report = &report; context.suite = "ui"; context.target = target.label;
                context.skip("no save for this title, so Add All Items was not tested on it", SaveTree::missing(saveRoot, target.label));
                continue;
            }
            testAddAllItems(report, target.label, target.group, target.loader);
            groupsTested.insert(target.group);
        }
        return static_cast<int>(groupsTested.size());
    }

    /// A refusal the file browser reports has to be DRAWN. The save picker always set its reason in
    /// `status`, and the browser drew that only for an empty folder -- which a refused file never is,
    /// being a row of the listing -- so every refusal was invisible and the pick read as a dead A
    /// button. Checked on the drawn frame, because the state was right all along.
    void testFileBrowserShowsRefusal(Test::Context& context) {
        const std::string root = "/tmp/pkse-uitest-refusal";
        const std::string fileName = "Pokemon Ruby.srm";
        mkdir(root.c_str(), 0777);
        if (FILE* handle = std::fopen((root + "/" + fileName).c_str(), "wb")) {
            std::fputc(0, handle);
            std::fclose(handle);
        }

        UI::Dialogs::FileBrowserState browser;
        browser.open("Open a Save File", "Save files", {root}, Save::saveFileExtensions(), Save::saveFileExactNames());
        browser.status = "\"" + fileName + "\" is not a save PKSE can open (1 bytes).";
        UI::PKSEFramebuffer framebuffer;
        std::vector<UI::TouchButton> taps;
        UIHost::Recorder::get().beginFrame();
        UI::Dialogs::drawFileBrowser(browser, framebuffer, taps);
        framebuffer.flush();
        const UIHost::Frame& frame = UIHost::Recorder::get().last();

        context.check(!browser.entries.empty() && frame.has(fileName), "the refusal fixture did not list its own file", "file=\"" + fileName + "\"");
        context.check(frame.has("is not a save PKSE can open"), "a refused file's reason was not drawn while the folder lists it", "status=\"" + browser.status + "\"");
        context.check(frame.has("Save files") && !frame.has("Bank files"), "the save browser does not name the files it lists as save files");

        // A container PKSE cannot read says what to change, at the END of the line -- so when a long name
        // makes the line too wide for the strip, the name is what gets cut, never the advice.
        {
            std::vector<uint8_t> compressedSave(0x8000, 0);
            const std::string rzipMagic = "#RZIPv\x01#";
            std::copy(rzipMagic.begin(), rzipMagic.end(), compressedSave.begin());
            const char* reason = Save::describeUnreadableSaveContainer(compressedSave);
            const std::string longName = "Pokemon - Ruby Version (USA, Europe) (Rev 2) [RetroArch, VBA Next] save.srm";
            browser.status = "\"" + longName + "\" " + (reason ? reason : "(no reason)") + ".";
            UIHost::Recorder::get().beginFrame();
            UI::Dialogs::drawFileBrowser(browser, framebuffer, taps);
            framebuffer.flush();
            context.check(reason && UIHost::Recorder::get().last().has("SaveRAM Compression, then save in the game again."), "a refusal that says what to change lost its advice on a long file name", "status=\"" + browser.status + "\"");
        }

        browser.status.clear();
        browser.showAllFiles = false;
        std::remove((root + "/" + fileName).c_str());
        browser.refresh();
        UIHost::Recorder::get().beginFrame();
        UI::Dialogs::drawFileBrowser(browser, framebuffer, taps);
        framebuffer.flush();
        context.check(UIHost::Recorder::get().last().has("No save files here."), "an empty folder in the save browser does not say it holds no save files");
        rmdir(root.c_str());
    }
}

int main(int argc, char** argv) {
    const std::string reportFilePath  = (argc > 1) ? argv[1] : std::string("uitest-report.log");
    const std::string saveRoot = (argc > 2) ? argv[2] : (std::getenv("HOME") ? std::string(std::getenv("HOME")) + "/pkse-test-saves" : std::string());

    Test::Report report;
    report.reset();

    std::printf("PKSE UI tests, host run\n");

    // The bank is real storage on the SD card. Point it somewhere disposable: the storage view
    // constructs one, and a test run must not touch a bank the user cares about.
    Trainer::Bank::setRootOverride("/tmp/pkse-uitest-bank");
    UI::SpriteManager::init();

    // Screenshots of the first failure per test, as SVG. A headless UI test is only useful if you
    // can still see the screen that broke; the build script points this at build-harness/shots.
    if (const char* sd = std::getenv("PKSE_UITEST_SHOTS")) UIHost::Driver::setShotDir(sd);

    // The file browser first, because it needs NO save: it builds its own fixture directory and
    // asserts exactly which files the picker offers. That makes it the one UI check that still runs
    // on a machine with no test saves at all -- and it is the check that would have caught a real
    // shipped bug, where every Gen 6 and Gen 7 save was filtered out of the picker for having no
    // file extension.
    {
        report.setScope("-", "browser");
        Test::Context context;
        context.report = &report;
        context.suite = "browser";
        context.target = "-";
        context.workDirectory = "/tmp/pkse-uitest-browser";
        mkdir(context.workDirectory.c_str(), 0777);
        const int chunks = Test::fileBrowserPlan(context);
        for (int chunk = 0; chunk < chunks; ++chunk) {
            report.setScope("-", "browser");
            Test::fileBrowserRun(context, chunk);
        }
        rmdir(context.workDirectory.c_str());
        report.setScope("-", "browser");
        testFileBrowserShowsRefusal(context);
    }

    // The bank's OPEN BOX, which needs no save file either -- it is a property of the bank. Moving
    // it has to register as an unsaved bank change, or the Save/Discard prompt never appears and
    // there is no way to either keep it or undo it. It lives in the bank's serialized image for
    // exactly that reason (hasChanged() is serialize() != savedImage), so this checks the whole
    // loop: dirty on move, kept on save, reverted on discard, and an older bank file still opens.
    // The text wrapper needs no save either: its contract is checked on inputs built to reach every
    // branch, which no real save's Pokemon can be relied on to say.
    {
        report.setScope("-", "text-wrap");
        Test::Context context;
        context.report = &report;
        context.suite = "text-wrap";
        context.target = "-";
        testWrapTextToWidth(context);
    }

    // The on-screen keyboard needs no save either: its rules are about the text it holds and the
    // keys it draws, and every one of them is reachable from a pad and a touchscreen.
    {
        report.setScope("-", "keyboard");
        Test::Context context;
        context.report = &report;
        context.suite = "keyboard";
        context.target = "-";
        testOnScreenKeyboard(context);
    }

    {
        report.setScope("-", "bank-box");
        Test::Context context;
        context.report = &report;
        context.suite = "bank-box";
        context.target = "-";
        const std::string bankFile = "/tmp/pkse-uitest-bank/bank.dat";
        mkdir("/tmp/pkse-uitest-bank", 0777);
        std::remove(bankFile.c_str()); // start from no bank at all every run

        {
            Trainer::Bank bank;
            context.check(bank.currentBox == 0, "a fresh bank did not open on box 1", "currentBox=" + std::to_string(bank.currentBox));
            context.check(!bank.hasChanged(), "a freshly loaded bank already read as changed");

            bank.currentBox = 7;
            context.check(bank.hasChanged(), "moving the bank's open box did not mark the bank as changed -- the Save/Discard prompt would never appear, so the box could be neither kept nor undone");
            context.check(bank.save(), "saving the bank after a box move failed");
            context.check(!bank.hasChanged(), "the bank still read as changed right after saving");
        }
        {
            Trainer::Bank bank; // reopened: the saved box has to come back
            context.check(bank.currentBox == 7, "the saved open box did not survive a reload", "currentBox=" + std::to_string(bank.currentBox));
            context.check(!bank.hasChanged(), "a bank reloaded from its own file read as changed");

            // Discard: load() is that path, and it must put the box back to the last SAVED one
            // rather than leaving the one the user wandered to.
            bank.currentBox = 12;
            context.check(bank.hasChanged(), "a second box move did not mark the bank as changed");
            bank.load();
            context.check(bank.currentBox == 7, "discarding bank changes kept the box the user had scrolled to", "currentBox=" + std::to_string(bank.currentBox) + " expected=7");
            context.check(!bank.hasChanged(), "the bank read as changed after discarding");
        }
        // A BANK WRITTEN BEFORE THIS SECTION EXISTED still has to open, on box 1 and clean. The
        // section is appended, so trimming it off is exactly what such a file looks like.
        {
            std::vector<uint8_t> image = readFile(bankFile);
            if (context.check(image.size() > 6, "could not re-read the scratch bank to trim its open-box section")) {
                std::FILE* trimmed = std::fopen(bankFile.c_str(), "wb");
                if (trimmed) {
                    std::fwrite(image.data(), 1, image.size() - 6, trimmed); // drop "CBOX" + the u16
                    std::fclose(trimmed);
                }
                Trainer::Bank bank;
                context.check(bank.currentBox == 0, "a bank file predating the open-box section did not default to box 1", "currentBox=" + std::to_string(bank.currentBox));
                context.check(!bank.hasChanged(), "a bank file predating the open-box section read as changed on load, so opening it would prompt to save something nobody edited");
            }
        }
        std::remove(bankFile.c_str());
    }

    int opened = 0;
    if (!saveRoot.empty()) {
        std::printf("  saves: %s\n\n", saveRoot.c_str());

        std::vector<uint8_t> frlg = readFile(SaveTree::fireRed(saveRoot));
        if (!frlg.empty()) {
            Trainer::Trainer3FRLG trainer(frlg, "FireRed_e.sav");
            runUI(report, "FireRed", Enums::GameVersion::FRLG, trainer, 0x0100554023408000ULL);
            ++opened;
        }
        // Let's Go is not an SCBlock game and not a flat GBA file -- it is the one format with its
        // own block builder, which is exactly why it was missing here and why the console run was
        // the first thing to walk its editor.
        std::vector<uint8_t> gg = readFile(SaveTree::letsGoPikachu(saveRoot));
        if (gg.size() >= Trainer::SAVE_SIZE7_LGPE) {
            std::vector<uint8_t> area(gg.begin(), gg.begin() + Trainer::SAVE_SIZE7_LGPE);
            Trainer::Trainer7LGPE trainer(Trainer::createBlocksFromSaveData7LGPE(area));
            runUI(report, "Let's Go Pikachu", Enums::GameVersion::GG, trainer, 0x010003F003A34000ULL);
            ++opened;
        }
        {
            std::vector<uint8_t> red = readFile(SaveTree::red(saveRoot));
            if (!red.empty()) {
                Trainer::Trainer1RBY trainer(red, "Pokemon Red.dat");
                runUI(report, "Red", Enums::GameVersion::RBY, trainer, 0);
                ++opened;
            }
        }
        opened += openLoose<Trainer::Trainer2GSC>(report, "Gold", Enums::GameVersion::GSC, SaveTree::forTitle(saveRoot, "Gold")) ? 1 : 0;
        opened += openLoose<Trainer::Trainer4DP>(report, "Pearl", Enums::GameVersion::DP, SaveTree::forTitle(saveRoot, "Pearl")) ? 1 : 0;
        opened += openLoose<Trainer::Trainer4PT>(report, "Platinum", Enums::GameVersion::PT, SaveTree::forTitle(saveRoot, "Platinum")) ? 1 : 0;
        opened += openLoose<Trainer::Trainer4HGSS>(report, "SoulSilver", Enums::GameVersion::HGSS, SaveTree::forTitle(saveRoot, "SoulSilver")) ? 1 : 0;
        opened += openLoose<Trainer::Trainer5BW>(report, "Black", Enums::GameVersion::BW, SaveTree::forTitle(saveRoot, "Black")) ? 1 : 0;
        opened += openLoose<Trainer::Trainer5B2W2>(report, "Black 2", Enums::GameVersion::B2W2, SaveTree::forTitle(saveRoot, "Black 2")) ? 1 : 0;
        opened += openLoose<Trainer::Trainer6XY>(report, "X", Enums::GameVersion::XY, SaveTree::forTitle(saveRoot, "X")) ? 1 : 0;
        opened += openLoose<Trainer::Trainer6ORAS>(report, "Omega Ruby", Enums::GameVersion::ORAS, SaveTree::forTitle(saveRoot, "Omega Ruby")) ? 1 : 0;
        opened += openLoose<Trainer::Trainer7SM>(report, "Sun", Enums::GameVersion::SM, SaveTree::forTitle(saveRoot, "Sun")) ? 1 : 0;
        opened += openLoose<Trainer::Trainer7USUM>(report, "Ultra Sun", Enums::GameVersion::USUM, SaveTree::ultraSun(saveRoot)) ? 1 : 0;
        opened += openSCBlock<Trainer::Trainer8SWSH>(report, "Shield", Enums::GameVersion::SWSH, SaveTree::shield(saveRoot), 0x01008DB008C2C000ULL) ? 1 : 0;
        // Legends: Arceus is the one format whose created Pokemon carries a stored height and
        // weight, so the creator journey checks something here it checks nowhere else.
        opened += openSCBlock<Trainer::Trainer8LA>(report, "Legends Arceus", Enums::GameVersion::PLA, SaveTree::legendsArceus(saveRoot), 0x01001F5010DFA000ULL) ? 1 : 0;
        opened += openSCBlock<Trainer::Trainer9LZA>(report, "Legends Z-A", Enums::GameVersion::ZA, SaveTree::legendsZA(saveRoot), 0x0100F43008C44000ULL) ? 1 : 0;
        // Every format with Hyper Training is walked, or its stat editor row ships untested: these two
        // were once opened only for Add All Items. Shining Pearl's save is one flat file, not blocks.
        {
            std::string bdspPath = SaveTree::folderFor(saveRoot, "Shining Pearl") + "/SaveData.bin";
            if (readFile(bdspPath).empty()) bdspPath = SaveTree::forTitle(saveRoot, "Shining Pearl");
            std::vector<uint8_t> bdsp = readFile(bdspPath);
            if (!bdsp.empty()) {
                Trainer::Trainer8BDSP trainer(std::move(bdsp));
                runUI(report, "Shining Pearl", Enums::GameVersion::BDSP, trainer, 0x010018E011D92000ULL);
                ++opened;
            }
        }
        opened += openSCBlock<Trainer::Trainer9SV>(report, "Violet", Enums::GameVersion::SV, SaveTree::violet(saveRoot), 0x01008F6008C5E000ULL) ? 1 : 0;
    }

    if (!saveRoot.empty()) {
        // Every one of the nineteen save formats, so a pouch or a writer that cannot take a full bag
        // is found on the format it breaks.
        const int addAllGroupCount = runAddAllItems(report, saveRoot);
        report.setScope("-", "ui");
        Test::Context context; context.report = &report; context.suite = "ui";
        context.check(addAllGroupCount == 19, "Add All Items was not tested on every save format", "formats=" + std::to_string(addAllGroupCount) + "/19");
    }

    if (opened == 0) {
        report.setScope("-", "ui");
        Test::Context context; context.report = &report;
        context.skip("no saves were opened, so no UI test ran at all", "saveRoot=\"" + saveRoot + "\"");
    }

    std::printf("\n");
    for (const auto& t : report.tallies())
        std::printf("  %-28s %-6s %7d checks  %4d fail  %4d warn  %4d skip\n", t.target.c_str(), t.suite.c_str(), t.checks, t.fails, t.warns, t.skips);

    std::printf("\nfindings:\n");
    for (const auto& g : report.groups()) {
        std::printf("  [%s] %s: %s  x%d\n", Test::severityName(g.severity), g.suite.c_str(), g.what.c_str(), g.occurrenceCount);
        for (size_t i = 0; i < g.samples.size() && i < 3; ++i)
            std::printf("        %s\n", g.samples[i].c_str());
    }
    if (report.groups().empty()) std::printf("  (none)\n");

    report.write(reportFilePath, "PKSE UI TESTS (host, real screens driven with synthetic input)\n");
    std::printf("\nchecks=%d failures=%d warnings=%d skipped=%d notes=%d  shots=%d  -> %s\n", report.checks(), report.failures(), report.warnings(), report.skips(), report.notes(), UIHost::Driver::shotsWritten(), reportFilePath.c_str());
    return report.failures() == 0 ? 0 : 1;
}
