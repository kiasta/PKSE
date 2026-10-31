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
#include "Names/ItemPouches.h" // Add All Items is checked against the same legal lists
#include "Trainer/Inventory8SWSH.h"
#include "Trainer/Inventory9SV.h"
#include "Trainer/Inventory9LZA.h" // the Z-A pouch the Cherished Ring check names
#include "Trainer/Trainer3RSE.h"
#include "Trainer/Trainer8BDSP.h"
#include "Trainer/Trainer9SV.h"
#include "UI/ListSearch.h"
#include "UI/Panels/UnmappedSlot.h" // the placeholder mark and label the unmapped-slot journeys look for
#include "UI/Panels/ItemsPanel.h" // pouchDisplayName -- the Add All ceiling is looked up by the pouch's name
#include "UI/Common.h"
#include "UI/ScreenChrome.h" // NAV_BAR_HEIGHT -- the one-nav-bar rule is checked against its geometry
#include "UI/Dialogs/FileBrowserDialog.h"
#include "UI/Dialogs/KeyboardDialog.h"
#include "UI/Dialogs/StatEditDialog.h"
#include "UI/Dialogs/RibbonEditDialog.h" // the grid geometry and tile ids the ribbon test drives
#include "Names/RibbonNames.h"
#include "Names/FormNames.h" // getDisplayName -- the party journey looks for the name the panel draws
#include "Names/ItemNames.h" // getItemNameFor -- the held-item picker is checked against the game's own table
#include "UI/Dialogs/PickerDialog.h"
#include "Globals.h" // g_allowIllegalEdits -- the Hyper Training row gives way to it
#include "Utils/Settings.h"
#include "Utils/StringHelpers.h"
#include "Save/RtcFooter.h"
#include "Save/SaveFileName.h"
#include "Save/SaveEnvelope.h"
#include "UI/SpriteManager.h"
#include "UI/TrainerViewScreen.h"

#include "HostInput.h"
#include "UIDriver.h"
#include <sys/stat.h> // the browser suite builds a fixture directory
#include <unistd.h> // rmdir

using UI::TrainerViewScreen;
using UIHost::Driver;
namespace Btn
{
    constexpr u64 A = HidNpadButton_A;
    constexpr u64 B = HidNpadButton_B;
    constexpr u64 X = HidNpadButton_X;
    constexpr u64 Y = HidNpadButton_Y;
    constexpr u64 Up = HidNpadButton_Up;
    constexpr u64 Down = HidNpadButton_Down;
    constexpr u64 Left = HidNpadButton_Left;
    constexpr u64 Right = HidNpadButton_Right;
    constexpr u64 L = HidNpadButton_L;
    constexpr u64 R = HidNpadButton_R;
    constexpr u64 ZL = HidNpadButton_ZL;
    constexpr u64 ZR = HidNpadButton_ZR;
    constexpr u64 Plus = HidNpadButton_Plus;
    constexpr u64 Minus = HidNpadButton_Minus;
}

namespace
{

    std::vector<uint8_t> readFile(const std::string &path)
    {
        FILE *file = std::fopen(path.c_str(), "rb");
        if (!file)
        {
            return {};
        }
        std::fseek(file, 0, SEEK_END);
        const long fileSize = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        std::vector<uint8_t> out(fileSize > 0 ? static_cast<size_t>(fileSize) : 0);
        if (!out.empty() && std::fread(out.data(), 1, out.size(), file) != out.size())
        {
            out.clear();
        }
        std::fclose(file);
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
    int navBarCount(const UIHost::Frame &frame, int screenWidth, int screenHeight)
    {
        int navBarDrawCount = 0;
        for (const UIHost::Draw *shape : frame.primitives("filledRoundedRect"))
        {
            if (shape->drawX == 0 && shape->drawWidth == screenWidth && shape->drawY == screenHeight - UI::NAV_BAR_HEIGHT)
            {
                ++navBarDrawCount;
            }
        }
        return navBarDrawCount;
    }

    /// Assert the one-bar rule at a moment when some overlay owns input. `where` names that moment.
    ///
    /// The bar must also be drawn AFTER every full-screen scrim. Each dialog dims the whole screen
    /// behind it, and a bar drawn before that was dimmed with everything else -- so the keys the
    /// dialog owns read as the screen's behind it. The save picker's file browser shipped that way.
    void expectOneNavBar(Driver &driver, const char *where)
    {
        const UIHost::Frame &frame = driver.frameData();
        const int bars = navBarCount(frame, 1280, 720);
        driver.expect(bars == 1, "the frame drew more than one nav bar; controls are listed twice", std::string(where) + " bars=" + std::to_string(bars));
        int lastBarDraw = -1;
        int lastScrimDraw = -1;
        for (int drawIndex = 0; drawIndex < static_cast<int>(frame.draws.size()); ++drawIndex)
        {
            const UIHost::Draw &draw = frame.draws[drawIndex];
            if (draw.kind != UIHost::Kind::Shape || !draw.visible)
            {
                continue;
            }
            if (std::strcmp(draw.primitive, "filledRoundedRect") == 0 && draw.drawX == 0 && draw.drawWidth == 1280 && draw.drawY == 720 - UI::NAV_BAR_HEIGHT)
            {
                lastBarDraw = drawIndex;
            }
            if (std::strcmp(draw.primitive, "filledRect") == 0 && draw.drawX == 0 && draw.drawY == 0 && draw.drawWidth == 1280 && draw.drawHeight == 720 && draw.color.alpha < 255)
            {
                lastScrimDraw = drawIndex;
            }
        }
        driver.expect(lastBarDraw > lastScrimDraw, "the nav bar is under a dialog's scrim, so the dialog's keys are dimmed with the screen behind it", std::string(where) + " barDraw=" + std::to_string(lastBarDraw) + " scrimDraw=" + std::to_string(lastScrimDraw));
    }

    void gotoHome(Driver &driver, TrainerViewScreen &screen, int homeEntryIndex)
    {
        driver.press(Btn::Up, 6); // back to the top of the pill column
        if (homeEntryIndex <= 2)
        {
            driver.press(Btn::Down, homeEntryIndex);
        }
        else
        {
            driver.press(Btn::Down, 3); // 0 -> 1 -> 2 -> icon row (lands on 3)
            driver.press(Btn::Right, homeEntryIndex - 3);
        }
        driver.expect(screen.homeMenuIndex == homeEntryIndex, "the HOME menu cursor did not reach the requested entry", "wanted=" + std::to_string(homeEntryIndex) + " got=" + std::to_string(screen.homeMenuIndex));
    }

    const char *modeName(TrainerViewScreen::ViewMode viewMode)
    {
        switch (viewMode)
        {
        case TrainerViewScreen::ViewMode::Party:
            return "Party";
        case TrainerViewScreen::ViewMode::Boxes:
            return "Boxes";
        case TrainerViewScreen::ViewMode::Items:
            return "Items";
        case TrainerViewScreen::ViewMode::Storage:
            return "Storage";
        case TrainerViewScreen::ViewMode::Trainer:
            return "Trainer";
        default:
            return "Settings";
        }
    }

    /// The screen renders at all, and lands on the HOME menu rather than inside a view.
    void testOpens(Test::Context &context, TrainerViewScreen &screen, Trainer::Trainer &trainer)
    {
        Driver driver(context, screen, "open");
        driver.step("open the editor").frame(2);
        driver.expect(!driver.frameData().draws.empty(), "the editor drew nothing on its first frames");
        driver.expect(!screen.detailViewActive, "the editor opened inside a view instead of on the HOME menu");
        // The HOME menu's live box-preview card has a sprite to draw only when the box it shows is not empty.
        bool previewHasPokemon = false;
        const int box = screen.selectedBoxIndex;
        if (box >= 0 && box < static_cast<int>(trainer.boxes.size()))
        {
            for (int slot = 0; slot < static_cast<int>(trainer.getSlotsPerBox()); ++slot)
            {
                if (!Test::isEmptySlot(trainer.boxes[box][slot].get()))
                {
                    previewHasPokemon = true;
                    break;
                }
            }
        }
        if (previewHasPokemon)
        {
            driver.seeSprites(1);
        }
        else
        {
            context.note("this save's current box is empty, so the HOME box preview's sprites were not checked", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
        }
    }

    /// Every HOME destination opens AND closes again. The failure this exists for is a view you can
    /// enter and not get out of, which no logic test can see and which strands the user completely.
    void testEveryDestinationOpensAndCloses(Test::Context &context, TrainerViewScreen &screen)
    {
        using VM = TrainerViewScreen::ViewMode;
        const VM expect[6] = {VM::Boxes, VM::Party, VM::Storage, VM::Items, VM::Trainer, VM::Settings};

        for (int homeEntryIndex = 0; homeEntryIndex < 6; ++homeEntryIndex)
        {
            Driver driver(context, screen, std::string("enter-") + modeName(expect[homeEntryIndex]));
            driver.step(std::string("walk to HOME entry ") + std::to_string(homeEntryIndex));
            gotoHome(driver, screen, homeEntryIndex);
            driver.expect(screen.selectedMode == expect[homeEntryIndex], "focusing a HOME entry selected the wrong view", std::string("entry=") + std::to_string(homeEntryIndex) + " want=" + modeName(expect[homeEntryIndex]) + " got=" + modeName(screen.selectedMode));

            driver.step(std::string("open ") + modeName(expect[homeEntryIndex])).press(Btn::A).frame();
            if (!driver.expect(screen.detailViewActive, "pressing A on a HOME entry did not open its view", std::string("view=") + modeName(expect[homeEntryIndex])))
            {
                continue;
            }

            driver.step(std::string("leave ") + modeName(expect[homeEntryIndex])).press(Btn::B).frame(2);
            driver.expect(!screen.detailViewActive, "a view could be entered but not left with B", std::string("view=") + modeName(expect[homeEntryIndex]));
            driver.expect(!screen.goBack, "leaving a view exited the whole editor instead of returning to HOME", std::string("view=") + modeName(expect[homeEntryIndex]));
        }
    }

    /// The box grid's real geometry, read from the hit-test rectangles the panel publishes.
    ///
    /// This is the rule CLAUDE.md singles out as easy to get wrong and invisible until it is on a
    /// screen: five rows always, a width that follows the box size, and a narrow box CENTRED at the
    /// normal cell pitch rather than stretched to fill the panel. Stretching is the obvious wrong
    /// implementation, and it fails exactly here -- uniform pitch and column count.
    void testBoxGridGeometry(Test::Context &context, TrainerViewScreen &screen, Trainer::Trainer &trainer)
    {
        Driver driver(context, screen, "box-grid");
        driver.step("open the box view");
        gotoHome(driver, screen, 0);
        driver.press(Btn::A).frame();
        if (!driver.expect(screen.detailViewActive, "could not open the box view"))
        {
            return;
        }

        const int slots = static_cast<int>(trainer.getSlotsPerBox());
        const int wantedColumnCount = UI::boxGridColumns(slots);

        std::vector<UI::TouchButton> cells;
        for (const auto &touchButton : screen.touchButtons)
        {
            if (touchButton.buttonId >= 0 && touchButton.buttonId < slots)
            {
                cells.push_back(touchButton);
            }
        }

        if (!driver.expect(static_cast<int>(cells.size()) == slots, "the box grid did not lay out one cell per slot", "slots=" + std::to_string(slots) + " cells=" + std::to_string(cells.size())))
        {
            return;
        }

        std::set<int> cellLefts;
        std::set<int> cellTops;
        std::set<int> cellWidths;
        std::set<int> cellHeights;
        for (const auto &cell : cells)
        {
            cellLefts.insert(cell.hitX);
            cellTops.insert(cell.hitY);
            cellWidths.insert(cell.hitWidth);
            cellHeights.insert(cell.hitHeight);
        }

        driver.expect(static_cast<int>(cellLefts.size()) == wantedColumnCount, "the box grid has the wrong column count", "slots=" + std::to_string(slots) + " want=" + std::to_string(wantedColumnCount) + " got=" + std::to_string(cellLefts.size()));
        driver.expect(cellTops.size() == static_cast<size_t>(UI::BOX_GRID_ROWS), "the box grid is not five rows", "rows=" + std::to_string(cellTops.size()));
        // One pitch for every cell. A stretched narrow grid produces cells wider than the bank's,
        // which is what makes the cursor change size as it crosses panes in the storage view.
        driver.expect(cellWidths.size() == 1 && cellHeights.size() == 1, "box grid cells are not a uniform size (a narrow grid was stretched, not centred)", "distinctW=" + std::to_string(cellWidths.size()) + " distinctH=" + std::to_string(cellHeights.size()));
    }

    /// A sprite is only drawn where a Pokemon actually is.
    ///
    /// The visual half of the rule that a non-null box slot is not an occupied one: several games
    /// keep a live blank entity in every empty slot on purpose. Counting sprites whose centre lands
    /// in a grid cell against the slots that hold a real species catches a panel that renders the
    /// blanks -- which reads to the user as thirty Pokemon in an empty box.
    void testNoSpritesInEmptySlots(Test::Context &context, TrainerViewScreen &screen, Trainer::Trainer &trainer)
    {
        Driver driver(context, screen, "empty-slots");
        driver.step("open the box view");
        gotoHome(driver, screen, 0);
        driver.press(Btn::A).frame(2);
        if (!driver.expect(screen.detailViewActive, "could not open the box view"))
        {
            return;
        }

        const int slots = static_cast<int>(trainer.getSlotsPerBox());
        const int box = screen.selectedBoxIndex;
        if (box < 0 || box >= static_cast<int>(trainer.boxes.size()))
        {
            return;
        }

        int occupied = 0;
        for (int slotIndex = 0; slotIndex < slots && slotIndex < static_cast<int>(Trainer::BOX_SLOTS); ++slotIndex)
        {
            const auto &slotPokemon = trainer.boxes[box][slotIndex];
            if (slotPokemon && slotPokemon->speciesID() != 0)
            {
                ++occupied;
            }
        }

        std::vector<UI::TouchButton> cells;
        for (const auto &touchButton : screen.touchButtons)
        {
            if (touchButton.buttonId >= 0 && touchButton.buttonId < slots)
            {
                cells.push_back(touchButton);
            }
        }
        if (cells.empty())
        {
            return;
        }

        int inCells = 0;
        for (const UIHost::Draw &draw : driver.frameData().draws)
        {
            if (draw.kind != UIHost::Kind::Sprite || !draw.visible)
            {
                continue;
            }
            const int drawCenterX = draw.centerX();
            const int drawCenterY = draw.centerY();
            for (const auto &cell : cells)
            {
                if (drawCenterX >= cell.hitX && drawCenterX < cell.hitX + cell.hitWidth && drawCenterY >= cell.hitY && drawCenterY < cell.hitY + cell.hitHeight)
                {
                    ++inCells;
                    break;
                }
            }
        }

        driver.expect(inCells <= occupied, "the box grid drew more Pokemon than the box holds (empty slots were rendered)", "box=" + std::to_string(box + 1) + " occupied=" + std::to_string(occupied) + " spritesInCells=" + std::to_string(inCells));
    }

    /// THE SUMMARY PANEL CARRIES THE LEGALITY VERDICT, bottom-left, and it must not land on top of
    /// the centred "A / tap: Edit" hint it shares that row with. Both are drawn from measured text
    /// into a fixed 452px panel, so whether they fit is arithmetic nobody can eyeball from the
    /// source -- and an overlap is the kind of defect that only shows up for the one Pokemon whose
    /// issue count pushes the label a few pixels wider.
    void testSummaryShowsLegality(Test::Context &context, TrainerViewScreen &screen, Trainer::Trainer &trainer)
    {
        Driver driver(context, screen, "summary-legality");
        driver.step("open the box view");
        gotoHome(driver, screen, 0);
        driver.press(Btn::A).frame(2);
        if (!driver.expect(screen.detailViewActive, "could not open the box view"))
        {
            return;
        }

        // The panel only resolves a Pokemon for an occupied slot; on an empty one it draws
        // "No Pokemon selected" and there is no verdict to show, which is correct.
        int occupied = -1;
        const int box = screen.selectedBoxIndex;
        if (box >= 0 && box < static_cast<int>(trainer.boxes.size()))
        {
            for (int slot = 0; slot < static_cast<int>(trainer.getSlotsPerBox()); ++slot)
            {
                if (!Test::isEmptySlot(trainer.boxes[box][slot].get()))
                {
                    occupied = slot;
                    break;
                }
            }
        }
        if (occupied < 0)
        {
            context.note("this save's first box is empty, so the summary verdict was not exercised", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }
        screen.selectedItemIndex = occupied;
        driver.step("the summary panel names the verdict").frame();

        const UIHost::Draw *verdict = driver.frameData().first("Legality:");
        if (!driver.expect(verdict != nullptr, "the summary panel drew no legality verdict for an occupied slot", "box=" + std::to_string(box + 1) + " slot=" + std::to_string(occupied + 1)))
        {
            return;
        }

        const UIHost::Draw *hint = driver.frameData().first("A / tap: Edit");
        if (!driver.expect(hint != nullptr, "the summary panel drew no edit hint to place the verdict against"))
        {
            return;
        }

        // Bottom-LEFT: left of the centred hint, on the same row, and clear of it.
        driver.expect(verdict->drawX < hint->drawX, "the legality verdict is not in the summary panel's bottom-left corner", "verdictX=" + std::to_string(verdict->drawX) + " hintX=" + std::to_string(hint->drawX));
        driver.expect(verdict->right() < hint->drawX, "the legality verdict overlaps the edit hint it shares the bottom row with", "verdict=\"" + verdict->text + "\" right=" + std::to_string(verdict->right()) + " hintX=" + std::to_string(hint->drawX));
        driver.expect(verdict->drawY == hint->drawY, "the legality verdict is not on the summary panel's bottom row", "verdictY=" + std::to_string(verdict->drawY) + " hintY=" + std::to_string(hint->drawY));
    }

    /// UI::wrapTextToWidth, asked directly. The overlay check below can only bite on whatever its
    /// save's Pokemon happen to say, so the helper's own contract is pinned here on inputs chosen to
    /// reach every branch: an ordinary sentence, a word wider than the whole line, and text with no
    /// spaces at all whose characters are three bytes each.
    void testWrapTextToWidth(Test::Context &context)
    {
        UI::PKSEFramebuffer framebuffer;
        constexpr int maxLineWidth = 300;
        const auto measuredWidth = [&](const std::string &line)
        {
            int lineWidth = 0;
            int lineHeight = 0;
            framebuffer.measureText(line, lineWidth, lineHeight, UI::TextStyle::Caption);
            return lineWidth;
        };
        // Every character whole: no line may begin on a continuation byte or stop partway through
        // a sequence. Half a character draws as nothing, which is the failure to catch.
        const auto isWholeUtf8 = [](const std::string &line)
        {
            size_t byteIndex = 0;
            while (byteIndex < line.size())
            {
                const unsigned char lead = static_cast<unsigned char>(line[byteIndex]);
                const size_t sequenceLength = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
                if (sequenceLength == 0 || byteIndex + sequenceLength > line.size())
                {
                    return false;
                }
                for (size_t continuation = 1; continuation < sequenceLength; ++continuation)
                {
                    if ((static_cast<unsigned char>(line[byteIndex + continuation]) & 0xC0) != 0x80)
                    {
                        return false;
                    }
                }
                byteIndex += sequenceLength;
            }
            return true;
        };
        const auto withoutSpaces = [](std::string value)
        {
            value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
            return value;
        };

        struct WrapCase
        {
            const char *label;
            std::string text;
        };
        const WrapCase cases[] = {
            {"sentence", "Nothing in FireRed produces Seadra at level 15: it would have to evolve from Horsea at level 32"},
            {"unbreakable word", "Location " + std::string(120, 'W') + " is not a place"},
            {"no spaces, multi-byte",
                "\xE3\x83\x94\xE3\x82\xAB\xE3\x83\x81\xE3\x83\xA5\xE3\x82\xA6"
                "\xE3\x81\xAF\xE3\x83\x88\xE3\x82\xAD\xE3\x83\xAF\xE3\x81\xAE"
                "\xE3\x82\x82\xE3\x82\x8A\xE3\x81\xA7\xE3\x81\xA4\xE3\x81\x8B"
                "\xE3\x81\xBE\xE3\x81\x88\xE3\x82\x8B\xE3\x81\x93\xE3\x81\xA8"},
        };
        for (const WrapCase &wrapCase : cases)
        {
            const std::string where = std::string("case=") + wrapCase.label;
            const std::vector<std::string> lines = UI::wrapTextToWidth(framebuffer, wrapCase.text, maxLineWidth, UI::TextStyle::Caption);
            context.check(lines.size() > 1, "text far wider than the line came back on one line", where + " lines=" + std::to_string(lines.size()));
            std::string rejoined;
            for (const std::string &line : lines)
            {
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
        for (const std::string &line : sentenceLines)
        {
            size_t wordStart = 0;
            while (wordStart <= line.size())
            {
                size_t wordEnd = line.find(' ', wordStart);
                if (wordEnd == std::string::npos)
                {
                    wordEnd = line.size();
                }
                wrappedWords.push_back(line.substr(wordStart, wordEnd - wordStart));
                wordStart = wordEnd + 1;
            }
        }
        std::vector<std::string> inputWords;
        for (size_t wordStart = 0; wordStart <= cases[0].text.size();)
        {
            size_t wordEnd = cases[0].text.find(' ', wordStart);
            if (wordEnd == std::string::npos)
            {
                wordEnd = cases[0].text.size();
            }
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
    void testLegalityOverlayStaysInCard(Test::Context &context, TrainerViewScreen &screen, Trainer::Trainer &trainer)
    {
        Driver driver(context, screen, "legality-overlay");
        driver.step("open the box view");
        gotoHome(driver, screen, 0);
        driver.press(Btn::A).frame();
        if (!driver.expect(screen.detailViewActive, "could not open the box view"))
        {
            return;
        }

        const int box = screen.selectedBoxIndex;
        if (box < 0 || box >= static_cast<int>(trainer.boxes.size()))
        {
            return;
        }

        // Reads the frame just drawn. Returns how many CONTINUATION lines the overlay drew -- message
        // lines beyond one per issue -- or -1 when the frame could not be read, already recorded.
        const auto checkOverlayFrame = [&](const std::string &where) -> int
        {
            const std::vector<UIHost::Draw> &draws = driver.frameData().draws;
            int headingIndex = -1;
            for (int drawIndex = static_cast<int>(draws.size()) - 1; drawIndex >= 0 && headingIndex < 0; --drawIndex)
            {
                if (draws[drawIndex].kind == UIHost::Kind::Text && draws[drawIndex].text == "Legality")
                {
                    headingIndex = drawIndex;
                }
            }
            if (!driver.expect(headingIndex >= 0, "the legality overlay drew no heading", where))
            {
                return -1;
            }
            const UIHost::Draw &heading = draws[headingIndex];
            // The card is the nearest filled panel drawn before the heading that contains it -- the
            // details page's own left panel contains that point too, but is drawn much earlier.
            const UIHost::Draw *card = nullptr;
            for (int drawIndex = headingIndex - 1; drawIndex >= 0 && !card; --drawIndex)
            {
                const UIHost::Draw &candidate = draws[drawIndex];
                if (std::strcmp(candidate.primitive, "filledRoundedRect") == 0 && heading.drawX >= candidate.drawX && heading.drawX < candidate.right() && heading.drawY >= candidate.drawY && heading.drawY < candidate.bottom())
                {
                    card = &candidate;
                }
            }
            if (!driver.expect(card != nullptr, "could not find the legality overlay's card", where))
            {
                return -1;
            }

            // Each issue draws ONE tag, and every message line after its first sits in the message
            // column with no tag beside it -- which is how a wrapped issue is told apart.
            int tagCount = 0;
            int messageLineCount = 0;
            int messageX = -1;
            for (size_t drawIndex = static_cast<size_t>(headingIndex) + 1; drawIndex < draws.size(); ++drawIndex)
            {
                const UIHost::Draw &line = draws[drawIndex];
                if (line.kind != UIHost::Kind::Text || !line.visible)
                {
                    continue;
                }
                if (line.drawX < card->drawX || line.drawX >= card->right() || line.drawY < card->drawY || line.drawY >= card->bottom())
                {
                    continue;
                }
                driver.expect(line.right() <= card->right(), "a legality line runs out of the card's right edge", where + " right=" + std::to_string(line.right()) + " cardRight=" + std::to_string(card->right()) + " line=\"" + line.text + "\"");
                driver.expect(line.bottom() <= card->bottom(), "a legality line runs out of the card's bottom edge", where + " bottom=" + std::to_string(line.bottom()) + " cardBottom=" + std::to_string(card->bottom()) + " line=\"" + line.text + "\"");
                if (line.text == "[note]" || line.text == "[warning]" || line.text == "[illegal]")
                {
                    ++tagCount;
                    if (messageX < 0 && drawIndex + 1 < draws.size())
                    {
                        messageX = draws[drawIndex + 1].drawX;
                    }
                    continue;
                }
                if (line.drawX == messageX && line.text.find("more not shown") == std::string::npos)
                {
                    ++messageLineCount;
                }
            }
            return messageLineCount > tagCount ? messageLineCount - tagCount : 0;
        };

        int openedCount = 0;
        int wrappedLineCount = 0;
        int firstOccupied = -1;
        for (int slot = 0; slot < static_cast<int>(trainer.boxes[box].size()); ++slot)
        {
            if (Test::isEmptySlot(trainer.boxes[box][slot].get()))
            {
                continue;
            }
            if (firstOccupied < 0)
            {
                firstOccupied = slot;
            }
            const std::string where = "box=" + std::to_string(box + 1) + " slot=" + std::to_string(slot + 1);
            screen.selectedItemIndex = slot;
            driver.step("open the details page").press(Btn::A).frame(2);
            if (!driver.expect(screen.details.active, "A on an occupied slot did not open the details page", where))
            {
                return;
            }
            screen.details.legalityOverlay = true;
            driver.step("open the legality overlay").frame();
            ++openedCount;
            const int continuationLines = checkOverlayFrame(where);
            if (continuationLines < 0)
            {
                return;
            }
            wrappedLineCount += continuationLines;
            screen.details.legalityOverlay = false;
            driver.step("close the details page").press(Btn::B).frame(2);
            if (!driver.expect(!screen.details.active, "B did not close an unedited details page", where))
            {
                return;
            }
        }
        if (openedCount == 0)
        {
            context.note("this save's first box is empty, so the legality overlay was not exercised", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }
        if (wrappedLineCount == 0)
        {
            context.note("no legality message in this save's first box was long enough to wrap", "title=\"" + Enums::getGameVersionName(context.version) + "\" opened=" + std::to_string(openedCount));
        }

        // THE POSED PASS. An origin from a LATER generation than the record is reported as
        // "Origin game X is from Generation N, which this Generation M record cannot have come from",
        // which is wider than the message column in every format. The version has to survive the
        // format's own field: Gen 3 keeps four bits, so Legends: Z-A does not fit there and
        // HeartGold (7) does.
        const std::string where = "box=" + std::to_string(box + 1) + " slot=" + std::to_string(firstOccupied + 1) + " posed";
        screen.selectedItemIndex = firstOccupied;
        driver.step("open the details page on the Pokemon to pose").press(Btn::A).frame(2);
        if (!driver.expect(screen.details.active, "A on the Pokemon to pose did not open the details page", where))
        {
            return;
        }

        // POSED ONLY ONCE THE PAGE IS OPEN, so the page's edit snapshot holds the real bytes: putting
        // the origin back then leaves the page clean, where posing first made the restore itself read
        // as an unsaved edit and B raised the discard prompt instead of closing. Restored the moment
        // the frame is drawn, before anything can return, because this is the trainer every later
        // test reads.
        ::Pokemon::Pokemon *posed = trainer.boxes[box][firstOccupied].get();
        const int formatGeneration = Enums::getVersionGeneration(Enums::getGroupRepVersion(posed->getGameGroup()));
        const uint8_t originalOrigin = posed->originGame();
        bool originPosed = false;
        for (Enums::GameVersion laterVersion : {Enums::GameVersion::ZA, Enums::GameVersion::HG})
        {
            if (!posed->hasOriginGame() || Enums::getVersionGeneration(static_cast<uint8_t>(laterVersion)) <= formatGeneration)
            {
                continue;
            }
            posed->setOriginGame(static_cast<uint8_t>(laterVersion));
            if (posed->originGame() == static_cast<uint8_t>(laterVersion))
            {
                originPosed = true;
                break;
            }
            posed->setOriginGame(originalOrigin);
        }
        if (originPosed)
        {
            screen.details.legalityOverlay = true;
            driver.step("open the legality overlay on the posed Pokemon").frame();
            posed->setOriginGame(originalOrigin);

            const int posedContinuationLines = checkOverlayFrame(where);
            // "Origin game" opens the message, so it is on its first line whatever the wrap does --
            // a phrase from later in the sentence can be split across two lines and never match.
            driver.expect(driver.frameData().has("Origin game"), "the posed impossible origin was not reported, so the long-message check proved nothing", where);
            driver.expect(posedContinuationLines > 0, "a message wider than the card was not wrapped onto a second line", where);
            screen.details.legalityOverlay = false;
        }
        else
        {
            context.note("no later generation can be stamped on this format, so the long-message pose was skipped", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
        }
        driver.step("close the details page").press(Btn::B).frame(2);
        driver.expect(!screen.details.active, "B did not close the details page after the pose was undone", where);
    }

    /// IN THE ITEMS POUCH, Y SEARCHES AND MINUS ADDS -- and neither key does the other's job. With
    /// Add Item on Y the search branch returns first: adding an item becomes unreachable while the nav
    /// bar goes on advertising it, and nothing presses either key to notice. So both are pressed, both
    /// of the bar's badges are read and tapped, and each key is checked for the OTHER's effect as well
    /// as its own.
    void testItemsKeys(Test::Context &context, TrainerViewScreen &screen, Trainer::Trainer &trainer)
    {
        Driver driver(context, screen, "items-keys");
        driver.step("open the Items view");
        gotoHome(driver, screen, 3);
        driver.press(Btn::A).frame();
        if (!driver.expect(screen.detailViewActive && screen.selectedMode == TrainerViewScreen::ViewMode::Items, "could not open the Items view"))
        {
            return;
        }

        // THE BAR NAMES BOTH KEYS. Copied out of g_navHits, which is rebuilt on every frame.
        const auto badgeFor = [](u64 button, UI::NavHit &found)
        {
            for (const UI::NavHit &hit : UI::g_navHits)
            {
                if (hit.button == button)
                {
                    found = hit;
                    return true;
                }
            }
            return false;
        };
        // The badge's LABEL is the last text drawn inside its tap target -- a face-button badge
        // draws its own letter first.
        const auto labelOf = [&](const UI::NavHit &hit)
        {
            std::string label;
            for (const UIHost::Draw &draw : driver.frameData().draws)
            {
                if (draw.kind == UIHost::Kind::Text && draw.visible && draw.drawX >= hit.hitX && draw.right() <= hit.hitX + hit.hitWidth && draw.centerY() >= hit.hitY && draw.centerY() < hit.hitY + hit.hitHeight)
                {
                    label = draw.text;
                }
            }
            return label;
        };
        UI::NavHit addBadge{};
        UI::NavHit searchBadge{};
        if (!driver.expect(badgeFor(Btn::Minus, addBadge), "the Items nav bar has no Minus badge"))
        {
            return;
        }
        if (!driver.expect(badgeFor(Btn::Y, searchBadge), "the Items nav bar has no Y badge"))
        {
            return;
        }
        driver.expect(labelOf(addBadge) == "Add Item", "the Items nav bar's Minus badge does not say Add Item", "label=\"" + labelOf(addBadge) + "\"");
        driver.expect(labelOf(searchBadge) == "Search", "the Items nav bar's Y badge does not say Search", "label=\"" + labelOf(searchBadge) + "\"");

        // MINUS OPENS THE ADD PICKER. A pouch can legitimately have nothing to add -- full, or already
        // holding every id it can -- and says so on the status line instead, so walk the pouches to
        // one that can take an item, and require at least a reason from every one that cannot.
        bool addPickerOpened = false;
        for (size_t pouchAttempt = 0; pouchAttempt < trainer.items.size() && !addPickerOpened; ++pouchAttempt)
        {
            const std::string where = "pouch=" + std::to_string(screen.selectedCategory);
            screen.storageStatus.clear();
            driver.step("press Minus in " + where).press(Btn::Minus).frame();
            driver.expect(!screen.itemSearch.isFiltering(), "Minus in the Items view started a search", where);
            if (screen.pickerActive)
            {
                addPickerOpened = true;
                driver.expect(screen.pickerKind == screen.pouchPickerKind(), "Minus opened a picker, but not the pouch's add-item picker", where);
                break;
            }
            driver.expect(!screen.storageStatus.empty(), "Minus in the Items view did nothing at all -- no add picker and no reason why", where);
            driver.step("next pouch").press(Btn::R).frame();
        }
        if (addPickerOpened)
        {
            driver.step("close the add picker").press(Btn::B).frame();
            driver.expect(!screen.pickerActive, "B did not close the add-item picker");

            // THE BADGE, TAPPED, DOES THE SAME: its label, the button it maps to and the handler all
            // have to agree, or a touch user finds a working key they cannot reach.
            driver.step("tap the nav bar's Add Item badge").tap(addBadge.hitX + addBadge.hitWidth / 2, addBadge.hitY + addBadge.hitHeight / 2).frame(2);
            driver.expect(screen.pickerActive && screen.pickerKind == screen.pouchPickerKind(), "tapping the nav bar's Add Item badge did not open the add-item picker");
            if (screen.pickerActive)
            {
                driver.press(Btn::B).frame();
            }
        }
        else
        {
            context.note("no pouch in this save could take a new item, so the add-item picker was not opened", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
        }

        // Y SEARCHES, AND OPENS NOTHING ELSE.
        driver.step("search the pouch with Y").type("a").press(Btn::Y).frame(2);
        driver.expect(screen.itemSearch.isFiltering(), "Y in the Items view did not start a search");
        driver.expect(!screen.pickerActive, "Y in the Items view opened the add-item picker");
        driver.step("clear the search").type("").press(Btn::Y).frame(2);
        driver.expect(!screen.itemSearch.isFiltering(), "accepting an empty search did not clear the Items filter");

        driver.step("tap the nav bar's Search badge").type("a").tap(searchBadge.hitX + searchBadge.hitWidth / 2, searchBadge.hitY + searchBadge.hitHeight / 2).frame(2);
        driver.expect(screen.itemSearch.isFiltering(), "tapping the nav bar's Search badge did not start a search");
        driver.expect(!screen.pickerActive, "tapping the nav bar's Search badge opened the add-item picker");
        driver.step("clear the search").type("").press(Btn::Y).frame(2);
    }

    /// How a save is opened from its file, and how a trainer is written back and opened again, for the
    /// one format it holds. Add All Items checks what a RELOAD of the written save holds, because an
    /// item the writer drops on the floor is still in the trainer the editor added it to.
    struct SaveLoader
    {
        std::function<std::unique_ptr<Trainer::Trainer>()> open;
        std::function<std::unique_ptr<Trainer::Trainer>(Trainer::Trainer &)> writeAndReopen;
    };

    const Trainer::InventoryItem *findItem(const std::vector<Trainer::InventoryItem> &pouch, uint16_t itemId)
    {
        for (const Trainer::InventoryItem &item : pouch)
        {
            if (item.itemId == itemId)
            {
                return &item;
            }
        }
        return nullptr;
    }

    /// Every stack that has a count must come back from a reload with that count, and nothing the
    /// trainer does not hold may appear. Compared as a sorted list of {id, count} per pouch: a bag can
    /// hold two stacks of one item (a Crystal or Ruby bag past 99, a Sword/Shield bag past 999), and
    /// an id-indexed writer is free to read them back in id order.
    void expectItemsSurviveReload(Test::Context &context, const Trainer::Trainer &written, const Trainer::Trainer &reopened, const std::string &where)
    {
        if (!context.check(written.items.size() == reopened.items.size(), "a reload of the written save has a different number of pouches", where + " written=" + std::to_string(written.items.size()) + " reopened=" + std::to_string(reopened.items.size())))
        {
            return;
        }
        const auto stacksOf = [](const std::vector<Trainer::InventoryItem> &pouch)
        {
            std::vector<std::pair<uint16_t, uint16_t>> stacks;
            for (const Trainer::InventoryItem &item : pouch)
            {
                if (item.count != 0)
                {
                    stacks.emplace_back(item.itemId, item.count);
                }
            }
            std::sort(stacks.begin(), stacks.end());
            return stacks;
        };
        int differingPouchCount = 0;
        std::string firstProblem;
        for (size_t pouchIndex = 0; pouchIndex < written.items.size(); ++pouchIndex)
        {
            const auto writtenStacks = stacksOf(written.items[pouchIndex]);
            const auto reopenedStacks = stacksOf(reopened.items[pouchIndex]);
            if (writtenStacks == reopenedStacks)
            {
                continue;
            }
            if (!differingPouchCount++)
            {
                size_t stackIndex = 0;
                while (stackIndex < writtenStacks.size() && stackIndex < reopenedStacks.size() && writtenStacks[stackIndex] == reopenedStacks[stackIndex])
                {
                    ++stackIndex;
                }
                const auto describe = [&](const std::vector<std::pair<uint16_t, uint16_t>> &stacks) { return stackIndex < stacks.size() ? std::to_string(stacks[stackIndex].first) + "x" + std::to_string(stacks[stackIndex].second) : std::string("none"); };
                firstProblem = "pouch=" + std::to_string(pouchIndex) + " wroteStacks=" + std::to_string(writtenStacks.size()) + " readStacks=" + std::to_string(reopenedStacks.size()) + " firstDifference wrote=" + describe(writtenStacks) + " read=" + describe(reopenedStacks);
            }
        }
        context.check(differingPouchCount == 0, "Add All Items did not survive writing the save and reading it back", where + " pouches=" + std::to_string(differingPouchCount) + " " + firstProblem);
    }

    /// What one item should hold after Add All Items gave `amount`, checked against the bag before it.
    /// The per-item ceilings PKHeX documents are spelled out here as well as in the screen, for the
    /// items that have one, so a rule dropped from itemMaxCount() is caught rather than agreed with.
    /// The pouch's stack ceiling, from PKHeX's PlayerBag*.cs tables and the pouch's own name. Kept
    /// apart from the screen's pouchItemMaxCount on purpose: a check that asks the code under test
    /// for its answer agrees with every bug in it, which is how Ruby/Sapphire/Emerald's 999 passed.
    int expectedPouchCeiling(const Trainer::Trainer &trainer, int pouchIndex)
    {
        using Version = Enums::GameVersion;
        const Version group = trainer.getGameGroup();
        const std::string pouchName = UI::Panels::pouchDisplayName(trainer, pouchIndex);
        const auto named = [&](const char *fragment) { return pouchName.find(fragment) != std::string::npos; };
        if (group == Version::RBY || group == Version::GSC)
        {
            return 99;
        }
        const bool isTechnicalMachinePouch = named("TM");
        if (group == Version::GG)
        {
            return isTechnicalMachinePouch ? 1 : 999;
        }
        if (named("Key"))
        {
            return 1;
        }
        switch (group)
        {
        case Version::RSE:
            return (named("Berr") || named("PC")) ? 999 : 99;
        case Version::DP:
        case Version::PT:
        case Version::HGSS:
            return isTechnicalMachinePouch ? 99 : 999;
        case Version::BW:
        case Version::B2W2:
        case Version::XY:
        case Version::ORAS:
            return isTechnicalMachinePouch ? 1 : 999;
        case Version::SM:
        case Version::USUM:
            return (isTechnicalMachinePouch || named("Z-Crystal")) ? 1 : 999;
        case Version::PLA:
            return named("Recipe") ? 1 : 999;
        case Version::ZA:
            return (isTechnicalMachinePouch || named("Mega")) ? 1 : 999;
        default:
            return 999;
        }
    }

    int expectedCeiling(const Trainer::Trainer &trainer, Enums::GameVersion group, int pouchIndex, uint16_t itemId, int pouchCeiling)
    {
        using Version = Enums::GameVersion;
        if (group == Version::GSC && itemId >= 243 && itemId <= 249)
        {
            return 1; // HM01-HM07
        }
        if (Enums::isGen3Group(group) && itemId >= 339 && itemId <= 346)
        {
            return 1; // HM01-HM08
        }
        if ((group == Version::DP || group == Version::PT || group == Version::HGSS) && itemId >= 420 && itemId <= 427)
        {
            return 1;
        }
        if (group == Version::SWSH && pouchIndex == static_cast<int>(Trainer::PouchType8SWSH::TMs) && !(itemId >= 1130 && itemId < 1230))
        {
            return 1;
        }
        if (group == Version::SV && pouchIndex == static_cast<int>(Trainer::PouchType9SV::Ingredients))
        {
            const bool isPick = (itemId >= 2334 && itemId <= 2342) || (itemId >= 2385 && itemId <= 2394) || itemId == 2548;
            if (((itemId >= 2311 && itemId <= 2400) || (itemId >= 2417 && itemId <= 2437)) && !isPick)
            {
                return 1; // picnic accessories
            }
        }
        if (group == Version::ZA && (itemId == 2612 || itemId == 2619))
        {
            return 0; // Cherished Ring; the Colorful Screw's ceiling is its own count, which Add All never lowers
        }
        if (group == Version::ZA && itemId == 2618)
        {
            for (const auto &block : trainer.getBlocks())
            {
                if (block.key == 0x0ABC6547)
                {
                    return 9999; // Mega Shard, once the save carries the 2.0.1 block (PKHeX PlayerBag9a)
                }
            }
        }
        if (group == Version::GG && pouchIndex == static_cast<int>(Trainer::PouchType7LGPE::Battle) && itemId > 100)
        {
            return 1; // Mega Stones (PKHeX PlayerBag7b)
        }
        if ((group == Version::SM || group == Version::USUM) && itemId == 797)
        {
            return 2; // the Z-Ring, given twice
        }
        if (Names::isKeyItem(group, itemId))
        {
            return 1;
        }
        return pouchCeiling;
    }

    /// Add All Items, driven through its dialog: the nav bar names it, ZR opens it, the amount and the
    /// key-item switch respond to keys and taps, B leaves the bag alone, and A gives every legal item
    /// what the issue asks for -- the amount, clamped to the item's limit, never lowering a stack,
    /// and no key items unless they were asked for. Then the save is written and read back.
    void testAddAllItems(Test::Report &report, const std::string &target, Enums::GameVersion group, const SaveLoader &loader)
    {
        Test::Context context;
        context.report = &report;
        context.suite = "ui";
        context.target = target;
        context.group = group;
        report.setScope(target, "ui");
        std::printf("  add-all-items  %s\n", target.c_str());

        for (const bool withKeyItems : {false, true})
        {
            std::unique_ptr<Trainer::Trainer> trainer = loader.open();
            if (!context.check(trainer != nullptr, "the save could not be opened for the Add All Items test"))
            {
                return;
            }
            context.version = trainer->getGameVersion();
            context.trainer = trainer.get();
            const std::vector<std::vector<Trainer::InventoryItem>> itemsBefore = trainer->items;
            TrainerViewScreen screen(*trainer, target, std::string("/tmp/pkse-uitest"), 0, AccountUid{}, false);
            const std::string pass = withKeyItems ? "keyItems=1" : "keyItems=0";
            Driver driver(context, screen, withKeyItems ? "add-all-items-with-key-items" : "add-all-items");

            driver.step("open the Items view");
            gotoHome(driver, screen, 3);
            driver.press(Btn::A).frame();
            if (!driver.expect(screen.detailViewActive && screen.selectedMode == TrainerViewScreen::ViewMode::Items, "could not open the Items view", pass))
            {
                return;
            }

            bool barNamesIt = false;
            for (const UI::NavHit &hit : UI::g_navHits)
            {
                if (hit.button == Btn::ZR)
                {
                    barNamesIt = true;
                }
            }
            driver.expect(barNamesIt && driver.see("Add All"), "the Items nav bar does not offer Add All on ZR", pass);

            driver.step("ZR opens Add All Items").press(Btn::ZR).frame();
            if (!driver.expect(screen.addAllItemsDialogActive, "ZR in the Items view did not open Add All Items", pass))
            {
                return;
            }
            driver.see("Add All Items");
            expectOneNavBar(driver, "Add All Items open");
            driver.expect(screen.addAllItemsAmount == 999, "Add All Items does not start at the most an item can hold", "amount=" + std::to_string(screen.addAllItemsAmount));

            if (!withKeyItems)
            {
                driver.step("step the amount");
                driver.press(Btn::Left).frame();
                driver.expect(screen.addAllItemsAmount == 998, "Left did not take 1 off the amount", "amount=" + std::to_string(screen.addAllItemsAmount));
                driver.press(Btn::ZR).frame();
                driver.expect(screen.addAllItemsAmount == 999, "ZR raised the amount past 999", "amount=" + std::to_string(screen.addAllItemsAmount));
                driver.press(Btn::ZL, 12).frame();
                driver.expect(screen.addAllItemsAmount == 1, "the amount went below 1", "amount=" + std::to_string(screen.addAllItemsAmount));
                driver.press(Btn::R).press(Btn::Right, 2).frame();
                driver.expect(screen.addAllItemsAmount == 13, "R and Right did not add 10 and 1", "amount=" + std::to_string(screen.addAllItemsAmount));
                driver.press(Btn::L).frame();
                driver.expect(screen.addAllItemsAmount == 3, "L did not take 10 off the amount", "amount=" + std::to_string(screen.addAllItemsAmount));

                driver.step("the key-item switch, by key and by tap");
                driver.press(Btn::X).frame();
                driver.expect(screen.addAllItemsIncludesKeyItems && driver.see("Key items given early"), "X did not switch key items on, or no warning was shown", "on=" + std::to_string(screen.addAllItemsIncludesKeyItems));
                int switchCenterX = -1;
                int switchCenterY = -1;
                for (const UI::TouchButton &button : screen.touchButtons)
                {
                    if (button.buttonId == 41)
                    {
                        switchCenterX = button.hitX + button.hitWidth / 2;
                        switchCenterY = button.hitY + button.hitHeight / 2;
                    }
                }
                if (driver.expect(switchCenterX >= 0, "the Include Key Items switch is not a touch target"))
                {
                    driver.tap(switchCenterX, switchCenterY).frame(2);
                }
                driver.expect(!screen.addAllItemsIncludesKeyItems && driver.see("Key items are left out"), "tapping the Include Key Items switch did not switch key items off", "on=" + std::to_string(screen.addAllItemsIncludesKeyItems));

                driver.step("B cancels and changes nothing").press(Btn::B).frame();
                driver.expect(!screen.addAllItemsDialogActive, "B did not close Add All Items");
                bool unchanged = trainer->items.size() == itemsBefore.size();
                for (size_t pouchIndex = 0; unchanged && pouchIndex < itemsBefore.size(); ++pouchIndex)
                {
                    unchanged = trainer->items[pouchIndex].size() == itemsBefore[pouchIndex].size();
                    for (size_t itemIndex = 0; unchanged && itemIndex < itemsBefore[pouchIndex].size(); ++itemIndex)
                    {
                        unchanged = trainer->items[pouchIndex][itemIndex].itemId == itemsBefore[pouchIndex][itemIndex].itemId && trainer->items[pouchIndex][itemIndex].count == itemsBefore[pouchIndex][itemIndex].count;
                    }
                }
                driver.expect(unchanged && !screen.hasUnsavedChanges, "cancelling Add All Items changed the bag");
                driver.step("reopen with ZR").press(Btn::ZR).frame();
                driver.expect(screen.addAllItemsAmount == 3, "Add All Items forgot the amount between openings", "amount=" + std::to_string(screen.addAllItemsAmount));
            }
            else
            {
                driver.step("switch key items on").press(Btn::X).frame();
            }

            const int amount = screen.addAllItemsAmount;
            screen.storageStatus.clear();
            int addCenterX = -1;
            int addCenterY = -1;
            for (const UI::TouchButton &button : screen.touchButtons)
            {
                if (button.buttonId == 1)
                {
                    addCenterX = button.hitX + button.hitWidth / 2;
                    addCenterY = button.hitY + button.hitHeight / 2;
                }
            }
            if (withKeyItems && driver.expect(addCenterX >= 0, "the Add button is not a touch target"))
            {
                driver.step("tap Add").tap(addCenterX, addCenterY).frame(TrainerViewScreen::TAP_PRESS_FRAMES + 1);
            }
            else
            {
                driver.step("A adds").press(Btn::A).frame();
            }
            if (!driver.expect(!screen.addAllItemsDialogActive, "Add All Items stayed open after Add", pass))
            {
                return;
            }
            std::printf("      %s amount=%d: %s\n", pass.c_str(), screen.addAllItemsAmount, screen.storageStatus.c_str());
            driver.expect(screen.storageStatus.rfind("Added or topped up", 0) == 0 || screen.storageStatus.rfind("Every item is already", 0) == 0, "Add All Items did not say what it did", pass + " status=\"" + screen.storageStatus + "\"");

            int addedCount = 0;
            int wrongCount = 0;
            int loweredCount = 0;
            int keyItemCount = 0;
            int missingCount = 0;
            std::string firstProblem;
            const auto problem = [&](int &counter, const std::string &text)
            {
                if (!counter++ && firstProblem.empty())
                {
                    firstProblem = text;
                }
            };
            for (size_t pouchIndex = 0; pouchIndex < trainer->items.size() && pouchIndex < itemsBefore.size(); ++pouchIndex)
            {
                const auto &before = itemsBefore[pouchIndex];
                const auto &after = trainer->items[pouchIndex];
                // Add All Items only raises counts and appends, so every stack the bag held is still at its
                // own position -- which also keeps two stacks of one item apart.
                for (size_t itemIndex = 0; itemIndex < before.size(); ++itemIndex)
                {
                    const bool kept = itemIndex < after.size() && after[itemIndex].itemId == before[itemIndex].itemId && after[itemIndex].count >= before[itemIndex].count;
                    if (!kept)
                    {
                        problem(loweredCount, "pouch=" + std::to_string(pouchIndex) + " index=" + std::to_string(itemIndex) + " item=" + std::to_string(before[itemIndex].itemId) + " before=" + std::to_string(before[itemIndex].count) + " after=" + (itemIndex < after.size() ? std::to_string(after[itemIndex].itemId) + "x" + std::to_string(after[itemIndex].count) : std::string("gone")));
                    }
                }
                const bool pouchFull = static_cast<int>(after.size()) >= screen.pouchCapacity(static_cast<int>(pouchIndex));
                for (const uint16_t itemId : Names::getPouchItems(group, pouchIndex))
                {
                    const Trainer::InventoryItem *was = findItem(before, itemId);
                    const Trainer::InventoryItem *now = findItem(after, itemId);
                    const int previousCount = was ? was->count : 0;
                    const int currentCount = now ? now->count : 0;
                    const std::string where = "pouch=" + std::to_string(pouchIndex) + " item=" + std::to_string(itemId) + " before=" + std::to_string(previousCount) + " after=" + std::to_string(currentCount);
                    if (!withKeyItems && Names::isKeyItem(group, itemId))
                    {
                        if (currentCount != previousCount)
                        {
                            problem(keyItemCount, where);
                        }
                        continue;
                    }
                    const int ceiling = expectedCeiling(*trainer, group, static_cast<int>(pouchIndex), itemId, expectedPouchCeiling(*trainer, static_cast<int>(pouchIndex)));
                    if (!now && !was)
                    {
                        if (!pouchFull && ceiling > 0)
                        {
                            problem(missingCount, where + " (the pouch had room)");
                        }
                        continue;
                    }
                    const int expectedCount = std::max(previousCount, std::min(previousCount + amount, ceiling));
                    if (currentCount != expectedCount)
                    {
                        problem(wrongCount, where + " expected=" + std::to_string(expectedCount));
                    }
                    else if (currentCount > previousCount)
                    {
                        ++addedCount;
                    }
                }
            }
            driver.expect(loweredCount == 0, "Add All Items lowered or removed an item the bag already held", pass + " " + firstProblem);
            driver.expect(keyItemCount == 0, "Add All Items changed a key item while key items were switched off", pass + " " + firstProblem);
            driver.expect(wrongCount == 0, "Add All Items gave an item the wrong count", pass + " amount=" + std::to_string(amount) + " " + firstProblem);
            driver.expect(missingCount == 0, "Add All Items left out a legal item while its pouch still had room", pass + " " + firstProblem);
            driver.expect(addedCount > 0 || screen.storageStatus.rfind("Every item is already", 0) == 0, "Add All Items added nothing and did not say why", pass);
            driver.expect(addedCount == 0 || screen.hasUnsavedChanges, "Add All Items changed the bag without marking the save as changed", pass);
            if (withKeyItems)
            {
                int keyItemsGiven = 0;
                for (size_t pouchIndex = 0; pouchIndex < trainer->items.size(); ++pouchIndex)
                {
                    for (const Trainer::InventoryItem &item : trainer->items[pouchIndex])
                    {
                        if (item.count > 0 && Names::isKeyItem(group, item.itemId))
                        {
                            ++keyItemsGiven;
                        }
                    }
                }
                driver.expect(keyItemsGiven > 0, "Add All Items with key items switched on holds no key item at all", pass);
            }

            if (std::unique_ptr<Trainer::Trainer> reopened = loader.writeAndReopen(*trainer))
            {
                expectItemsSurviveReload(context, *trainer, *reopened, pass);
            }
            else
            {
                context.fail("the save written after Add All Items could not be read back", pass);
            }
        }
    }

    /// The ribbon editor, driven on one Pokemon of every format: Y opens it (and opens nothing where the
    /// format stores no ribbons), every ribbon is a tile with its art, A / L / R / taps / ZL / ZR change
    /// exactly the selected ribbon or all of them, X affixes where the format displays one, every edit
    /// keeps the checksum and marks the save, and the edited ribbons come back from a written save.
    void testRibbonEditor(Test::Report &report, const std::string &target, Enums::GameVersion group, const SaveLoader &loader)
    {
        Test::Context context;
        context.report = &report;
        context.suite = "ui";
        context.target = target;
        context.group = group;
        report.setScope(target, "ui");
        std::printf("  ribbon-editor  %s\n", target.c_str());

        std::unique_ptr<Trainer::Trainer> trainer = loader.open();
        if (!context.check(trainer != nullptr, "the save could not be opened for the ribbon editor test"))
        {
            return;
        }
        context.version = trainer->getGameVersion();
        context.trainer = trainer.get();
        // A box Pokemon where there is one; several test saves keep theirs only in the party.
        int boxIndex = -1;
        int boxSlot = -1;
        int partyIndex = -1;
        for (int box = 0; box < static_cast<int>(trainer->boxes.size()) && boxSlot < 0; ++box)
        {
            for (int slot = 0; slot < static_cast<int>(trainer->boxes[box].size()); ++slot)
            {
                if (!Test::isEmptySlot(trainer->boxes[box][slot].get()) && !trainer->boxes[box][slot]->isEgg())
                {
                    boxIndex = box;
                    boxSlot = slot;
                    break;
                }
            }
        }
        for (int memberIndex = 0; boxSlot < 0 && memberIndex < static_cast<int>(trainer->party.size()); ++memberIndex)
        {
            if (!Test::isEmptySlot(trainer->party[memberIndex].get()) && !trainer->party[memberIndex]->isEgg())
            {
                partyIndex = memberIndex;
                break;
            }
        }
        if (boxSlot < 0 && partyIndex < 0)
        {
            context.skip("this save holds no Pokemon, so the ribbon editor was not exercised", "title=\"" + target + "\"");
            return;
        }
        const auto targetIn = [&](Trainer::Trainer &searchedTrainer) -> Pokemon::Pokemon * { return boxSlot >= 0 ? searchedTrainer.boxes[boxIndex][boxSlot].get() : searchedTrainer.party[partyIndex].get(); };
        const std::string where = boxSlot >= 0 ? "box=" + std::to_string(boxIndex) + " slot=" + std::to_string(boxSlot) : "party=" + std::to_string(partyIndex);

        TrainerViewScreen screen(*trainer, target, std::string("/tmp/pkse-uitest"), 0, AccountUid{}, false);
        Driver driver(context, screen, "ribbon-editor");
        if (boxSlot >= 0)
        {
            driver.step("open the box view");
            gotoHome(driver, screen, 0);
            driver.press(Btn::A).frame();
            if (!driver.expect(screen.detailViewActive, "could not open the box view"))
            {
                return;
            }
            screen.selectedBoxIndex = boxIndex;
            screen.selectedItemIndex = boxSlot;
        }
        else
        {
            driver.step("open the party view");
            gotoHome(driver, screen, 1);
            driver.press(Btn::A).frame();
            if (!driver.expect(screen.detailViewActive && screen.selectedMode == TrainerViewScreen::ViewMode::Party, "could not open the party view"))
            {
                return;
            }
            screen.selectedPartyIndex = partyIndex;
        }
        driver.step("open the details page").press(Btn::A).frame(2);
        if (!driver.expect(screen.details.active, "A on an occupied slot did not open the details page", where))
        {
            return;
        }
        Pokemon::Pokemon *pokemon = screen.detailsTargetPokemon();
        if (!driver.expect(pokemon == targetIn(*trainer), "the details page is not editing the Pokemon it was opened on", where))
        {
            return;
        }
        const auto navOffers = [](u64 button)
        {
            for (const UI::NavHit &hit : UI::g_navHits)
            {
                if (hit.button == button)
                {
                    return true;
                }
            }
            return false;
        };

        const std::span<const Names::RibbonDefinition> ribbons = Names::getRibbonDefinitions(group);
        const int ribbonCount = static_cast<int>(ribbons.size());
        if (ribbons.empty())
        {
            driver.step("a format with no ribbons offers no ribbon editor");
            driver.expect(!navOffers(Btn::Y), "the details page offers Y on a format that stores no ribbons");
            driver.notSee("Ribbons");
            driver.press(Btn::Y).frame();
            driver.expect(!screen.details.ribbonOverlay, "Y opened a ribbon editor on a format that stores no ribbons");
            return;
        }
        uint8_t *entityData = reinterpret_cast<uint8_t *>(pokemon->getData().data());
        const auto valueOf = [&](int ribbonIndex) { return Names::getRibbonValue(entityData, ribbons[ribbonIndex]); };
        const size_t affixOffset = Conversion::affixedRibbonOffset(group);
        // A PK3 shares its ribbon word with the fateful-encounter bit, which no ribbon edit may move.
        const bool fatefulBefore = pokemon->isFatefulEncounter();

        driver.step("Y opens the editor");
        driver.expect(navOffers(Btn::Y) && driver.see("Ribbons"), "the details page does not offer Y: Ribbons");
        driver.press(Btn::Y).frame();
        if (!driver.expect(screen.details.ribbonOverlay, "Y on the details page did not open the ribbon editor"))
        {
            return;
        }
        driver.see("Ribbons & Marks");
        expectOneNavBar(driver, "ribbon editor open");
        driver.expect(navOffers(Btn::ZR) && navOffers(Btn::ZL) && navOffers(Btn::A) && navOffers(Btn::B) && driver.see("Give All") && driver.see("Remove All"), "the nav bar does not name the ribbon editor's keys");
        driver.expect((affixOffset != 0) == navOffers(Btn::X), "the nav bar offers X: Affix where the format has no affixed ribbon, or omits it where it has one", "affixOffset=" + std::to_string(affixOffset));
        driver.expect(driver.seeSprites(static_cast<size_t>(std::min(ribbonCount, UI::Dialogs::RIBBON_GRID_COLUMNS * UI::Dialogs::RIBBON_GRID_VISIBLE_ROWS))), "the editor did not draw one ribbon image per tile", "ribbons=" + std::to_string(ribbonCount));
        int tileButtonCount = 0;
        for (const UI::TouchButton &button : screen.touchButtons)
        {
            if (button.buttonId >= UI::Dialogs::RIBBON_TILE_BUTTON_ID_BASE)
            {
                ++tileButtonCount;
            }
        }
        driver.expect(tileButtonCount == std::min(ribbonCount, UI::Dialogs::RIBBON_GRID_COLUMNS * UI::Dialogs::RIBBON_GRID_VISIBLE_ROWS), "not every visible ribbon tile is a touch target", "tiles=" + std::to_string(tileButtonCount));

        driver.step("ZL removes every ribbon").press(Btn::ZL).frame();
        int ownedCount = 0;
        for (int ribbonIndex = 0; ribbonIndex < ribbonCount; ++ribbonIndex)
        {
            ownedCount += valueOf(ribbonIndex) != 0;
        }
        driver.expect(ownedCount == 0, "ZL left ribbons on the Pokemon", "owned=" + std::to_string(ownedCount));
        if (affixOffset != 0)
        {
            driver.expect(entityData[affixOffset] == Conversion::AFFIXED_RIBBON_NONE, "removing every ribbon left one affixed", "affixed=" + std::to_string(entityData[affixOffset]));
        }
        driver.expect(pokemon->checksumValid(), "the record's checksum is stale after removing every ribbon");
        driver.expect(screen.hasUnsavedChanges, "removing every ribbon did not mark the save as changed");
        driver.see("0 of " + std::to_string(ribbonCount) + " owned");

        driver.step("the cursor walks the grid");
        driver.press(Btn::Right, 3).press(Btn::Down).frame();
        const int walkedIndex = std::min(ribbonCount - 1, 3 + UI::Dialogs::RIBBON_GRID_COLUMNS);
        driver.expect(screen.details.selectedRibbonIndex == walkedIndex, "Right x3 and Down did not land on the tile below the fourth", "selected=" + std::to_string(screen.details.selectedRibbonIndex) + " wanted=" + std::to_string(walkedIndex));
        driver.press(Btn::Up, 3).press(Btn::Left, 6).frame();
        driver.expect(screen.details.selectedRibbonIndex == 0, "Up and Left did not stop at the first tile", "selected=" + std::to_string(screen.details.selectedRibbonIndex));

        driver.step("A gives the selected ribbon, and only it").press(Btn::A).frame();
        driver.expect(valueOf(0) == ribbons[0].maxCount, "A did not give the selected ribbon", "value=" + std::to_string(valueOf(0)));
        ownedCount = 0;
        for (int ribbonIndex = 0; ribbonIndex < ribbonCount; ++ribbonIndex)
        {
            ownedCount += valueOf(ribbonIndex) != 0;
        }
        driver.expect(ownedCount == 1, "A changed a ribbon other than the selected one", "owned=" + std::to_string(ownedCount));
        driver.expect(pokemon->checksumValid(), "the record's checksum is stale after giving a ribbon");
        driver.see(Names::getRibbonName(ribbons[0], valueOf(0)));
        driver.see("1 of " + std::to_string(ribbonCount) + " owned");

        if (affixOffset != 0)
        {
            const int affixIndex = Names::getRibbonAffixIndex(ribbons[0]);
            driver.step("X affixes the owned ribbon").press(Btn::X).frame();
            driver.expect(affixIndex >= 0 && entityData[affixOffset] == affixIndex, "X did not affix the selected ribbon", "affixed=" + std::to_string(entityData[affixOffset]) + " wanted=" + std::to_string(affixIndex));
            driver.see("Affixed");
            driver.expect(pokemon->checksumValid(), "the record's checksum is stale after affixing a ribbon");
            driver.step("removing the affixed ribbon takes it down").press(Btn::A).frame();
            driver.expect(valueOf(0) == 0 && entityData[affixOffset] == Conversion::AFFIXED_RIBBON_NONE, "removing the affixed ribbon left it affixed", "affixed=" + std::to_string(entityData[affixOffset]));
            driver.step("X refuses an unowned ribbon").press(Btn::X).frame();
            driver.expect(entityData[affixOffset] == Conversion::AFFIXED_RIBBON_NONE, "X affixed a ribbon the Pokemon does not own");
            driver.press(Btn::A).frame();
        }
        else
        {
            driver.step("X does nothing where the format has no affixed ribbon").press(Btn::X).frame();
            driver.expect(screen.details.ribbonOverlay, "X closed the ribbon editor");
        }
        driver.step("A again removes it").press(Btn::A).frame();
        driver.expect(valueOf(0) == 0, "A on an owned ribbon did not remove it", "value=" + std::to_string(valueOf(0)));

        // A rank or a count steps by one on L/R and stops at its ends.
        int steppedIndex = -1;
        for (int ribbonIndex = 0; ribbonIndex < ribbonCount && steppedIndex < 0; ++ribbonIndex)
        {
            if (ribbons[ribbonIndex].maxCount > 1)
            {
                steppedIndex = ribbonIndex;
            }
        }
        if (steppedIndex >= 0)
        {
            screen.details.selectedRibbonIndex = steppedIndex;
            driver.step("R and L step a counted ribbon").frame();
            driver.press(Btn::R, 2).frame();
            driver.expect(valueOf(steppedIndex) == 2, "R x2 did not raise the counted ribbon to 2", "value=" + std::to_string(valueOf(steppedIndex)));
            driver.press(Btn::L).frame();
            driver.expect(valueOf(steppedIndex) == 1, "L did not lower the counted ribbon by one", "value=" + std::to_string(valueOf(steppedIndex)));
            driver.press(Btn::L, 2).frame();
            driver.expect(valueOf(steppedIndex) == 0, "L lowered the counted ribbon past none", "value=" + std::to_string(valueOf(steppedIndex)));
            driver.press(Btn::R, ribbons[steppedIndex].maxCount + 2).frame();
            driver.expect(valueOf(steppedIndex) == ribbons[steppedIndex].maxCount, "R raised the counted ribbon past its maximum", "value=" + std::to_string(valueOf(steppedIndex)));
            driver.press(Btn::A).frame();
            driver.expect(valueOf(steppedIndex) == 0, "A on a counted ribbon did not clear it", "value=" + std::to_string(valueOf(steppedIndex)));
        }

        driver.step("the first tap selects a tile, the second toggles it");
        const int tappedIndex = std::min(ribbonCount - 1, 5);
        screen.details.selectedRibbonIndex = 0;
        driver.frame();
        const UI::TouchButton *tile = nullptr;
        for (const UI::TouchButton &button : screen.touchButtons)
        {
            if (button.buttonId == UI::Dialogs::RIBBON_TILE_BUTTON_ID_BASE + tappedIndex)
            {
                tile = &button;
                break;
            }
        }
        if (driver.expect(tile != nullptr, "the tile to tap published no touch target"))
        {
            const int tileCenterX = tile->hitX + tile->hitWidth / 2, tileCenterY = tile->hitY + tile->hitHeight / 2;
            driver.tap(tileCenterX, tileCenterY).frame(TrainerViewScreen::TAP_PRESS_FRAMES + 1);
            driver.expect(screen.details.selectedRibbonIndex == tappedIndex && valueOf(tappedIndex) == 0, "the first tap on a tile changed the ribbon instead of only selecting it", "selected=" + std::to_string(screen.details.selectedRibbonIndex));
            driver.tap(tileCenterX, tileCenterY).frame(TrainerViewScreen::TAP_PRESS_FRAMES + 1);
            driver.expect(valueOf(tappedIndex) == ribbons[tappedIndex].maxCount, "a second tap on the selected tile did not give the ribbon", "value=" + std::to_string(valueOf(tappedIndex)));
        }

        driver.step("ZR gives every ribbon").press(Btn::ZR).frame();
        int fullCount = 0;
        for (int ribbonIndex = 0; ribbonIndex < ribbonCount; ++ribbonIndex)
        {
            fullCount += valueOf(ribbonIndex) == ribbons[ribbonIndex].maxCount;
        }
        driver.expect(fullCount == ribbonCount, "ZR left a ribbon below its maximum", "full=" + std::to_string(fullCount) + "/" + std::to_string(ribbonCount));
        driver.expect(pokemon->checksumValid(), "the record's checksum is stale after giving every ribbon");
        driver.expect(pokemon->isFatefulEncounter() == fatefulBefore, "a ribbon edit moved the fateful-encounter bit", std::string("before=") + (fatefulBefore ? "1" : "0"));
        driver.see(std::to_string(ribbonCount) + " of " + std::to_string(ribbonCount) + " owned");
        expectOneNavBar(driver, "ribbon editor after edits");

        driver.step("B closes the editor and keeps the page").press(Btn::B).frame();
        driver.expect(!screen.details.ribbonOverlay && screen.details.active, "B did not close the ribbon editor back to the details page");
        // The Ribbons row is the left pane's last, so scroll the pane to its end (the draw clamps it).
        screen.details.leftScroll = 1 << 20;
        driver.step("the Ribbons row counts them").frame(2);
        const std::string ribbonRowText = std::to_string(Names::getMonRibbons(entityData, group).size()) + "  (Y)";
        driver.expect(driver.see("Ribbons") && !driver.frameData().textExact(ribbonRowText).empty(), "the Ribbons row does not count the ribbons the editor gave", "want=\"" + ribbonRowText + "\"");

        // The ribbons must reach the file: save the page's edits, then write and read the save back.
        const std::vector<uint8_t> editedValues = [&]
        {
            std::vector<uint8_t> values;
            for (int ribbonIndex = 0; ribbonIndex < ribbonCount; ++ribbonIndex)
            {
                values.push_back(valueOf(ribbonIndex));
            }
            return values;
        }();
        driver.step("X saves the page").press(Btn::X).frame();
        driver.step("B closes the page").press(Btn::B).frame(2);
        driver.expect(!screen.details.active, "B did not close the details page after X saved it");
        // What a real save does before writing: re-encode the boxes, then overlay the party (Let's Go
        // keeps its party inside the box storage, so the party goes second).
        trainer->updateBoxBlock();
        trainer->updatePartyBlock();
        std::unique_ptr<Trainer::Trainer> reopened = loader.writeAndReopen(*trainer);
        if (!context.check(reopened != nullptr, "the save written after editing ribbons could not be read back"))
        {
            return;
        }
        const Pokemon::Pokemon *reread = targetIn(*reopened);
        if (!context.check(!Test::isEmptySlot(reread), "the edited Pokemon is gone from the written save", where))
        {
            return;
        }
        const uint8_t *rereadData = reinterpret_cast<const uint8_t *>(reread->getData().data());
        int differingCount = 0;
        for (int ribbonIndex = 0; ribbonIndex < ribbonCount; ++ribbonIndex)
        {
            differingCount += Names::getRibbonValue(rereadData, ribbons[ribbonIndex]) != editedValues[ribbonIndex];
        }
        context.check(differingCount == 0, "edited ribbons did not survive writing the save and reading it back", where + " differing=" + std::to_string(differingCount) + "/" + std::to_string(ribbonCount));
        context.check(reread->checksumValid(), "the edited Pokemon's checksum is invalid in the written save", where);
    }

    /// The Boxes view's Y opens the per-Pokemon menu the Storage view opens with A, so Clone, Export and
    /// Release are reachable outside Storage, and X opens the box menu (Sort / Search) in both views.
    void testBoxMenus(Test::Context &context, TrainerViewScreen &screen, Trainer::Trainer &trainer)
    {
        using Screen = TrainerViewScreen;
        Driver driver(context, screen, "box-menus");
        driver.step("open the box view");
        gotoHome(driver, screen, 0);
        driver.press(Btn::A).frame();
        if (!driver.expect(screen.detailViewActive, "could not open the box view"))
        {
            return;
        }

        const int boxCount = static_cast<int>(trainer.getBoxCount());
        const int slotsPerBox = static_cast<int>(trainer.getSlotsPerBox());
        const auto occupied = [&](int box, int slot)
        {
            const auto &pokemon = trainer.boxes[box][slot];
            return pokemon && pokemon->speciesID() != 0;
        };
        int sourceBox = -1;
        int sourceSlot = -1;
        int freeSlots = 0;
        for (int box = 0; box < boxCount; ++box)
        {
            for (int slot = 0; slot < slotsPerBox; ++slot)
            {
                if (!occupied(box, slot))
                {
                    ++freeSlots;
                    continue;
                }
                if (sourceBox < 0)
                {
                    sourceBox = box;
                    sourceSlot = slot;
                }
            }
        }
        if (sourceBox < 0)
        {
            context.note("this save has no boxed Pokemon, so the box menus were not exercised", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }
        screen.selectedBoxIndex = sourceBox;
        screen.selectedItemIndex = sourceSlot;
        driver.frame();

        driver.step("the nav bar names both menus").see("Sort/Search");
        driver.step("Y opens the Pokemon menu").press(Btn::Y).frame();
        if (!driver.expect(screen.storageMenuActive, "Y in the Boxes view did not open the Pokemon menu"))
        {
            return;
        }
        driver.expect(screen.menuPane == 0 && screen.menuBox == sourceBox && screen.menuSlot == sourceSlot, "the Pokemon menu opened on the wrong slot");
        driver.see("Clone");
        driver.notSee("Find…");

        // CLONE: one more Pokemon in the save, a copy of the source. Undone at once.
        std::vector<bool> occupiedBefore;
        for (int box = 0; box < boxCount; ++box)
        {
            for (int slot = 0; slot < slotsPerBox; ++slot)
            {
                occupiedBefore.push_back(occupied(box, slot));
            }
        }
        screen.storageMenuIndex = Screen::STORAGE_MENU_ROW_CLONE;
        driver.step("clone from the Boxes view").press(Btn::A).frame();
        driver.expect(!screen.storageMenuActive, "choosing Clone left the Pokemon menu open");
        int cloneBox = -1;
        int cloneSlot = -1;
        for (int box = 0; box < boxCount && cloneBox < 0; ++box)
        {
            for (int slot = 0; slot < slotsPerBox; ++slot)
            {
                if (!occupiedBefore[box * slotsPerBox + slot] && occupied(box, slot))
                {
                    cloneBox = box;
                    cloneSlot = slot;
                    break;
                }
            }
        }
        if (freeSlots > 0)
        {
            if (driver.expect(cloneBox >= 0, "Clone in the Boxes view placed nothing although a slot was free"))
            {
                driver.expect(trainer.boxes[cloneBox][cloneSlot]->speciesID() == trainer.boxes[sourceBox][sourceSlot]->speciesID(), "the clone is not the same species as its source");
                trainer.boxes[cloneBox][cloneSlot].reset();
            }
        }
        else
        {
            driver.expect(cloneBox < 0, "Clone placed a Pokemon in a save with no free slot");
        }

        // MOVE: the Boxes view's grab, which B lets go of.
        screen.selectedBoxIndex = sourceBox;
        screen.selectedItemIndex = sourceSlot;
        driver.step("Move grabs the slot").press(Btn::Y).frame();
        screen.storageMenuIndex = Screen::STORAGE_MENU_ROW_MOVE;
        driver.press(Btn::A).frame();
        driver.expect(screen.swapActive && screen.swapSourceBox == sourceBox && screen.swapSourceSlot == sourceSlot, "Move in the Boxes view did not grab the slot");
        driver.step("B lets go").press(Btn::B).frame();
        driver.expect(!screen.swapActive, "B did not let go of a grabbed slot");
        driver.expect(screen.detailViewActive, "B with a grabbed slot left the box view");

        // X: the box menu, and its Search highlights and jumps.
        driver.step("X opens the box menu").press(Btn::X).frame();
        if (!driver.expect(screen.boxMenuActive, "X in the Boxes view did not open the box menu"))
        {
            return;
        }
        driver.see("Sort");
        driver.see("Search…");
        const std::string speciesName = Trainer::getSpeciesName(trainer.boxes[sourceBox][sourceSlot]->speciesID());
        screen.selectedBoxIndex = (sourceBox + 1) % boxCount;
        screen.boxMenuIndex = Screen::BOX_MENU_ROW_SEARCH;
        driver.step("search for the source's species").type(speciesName).press(Btn::A).frame(2);
        driver.expect(screen.storageSearch.isFiltering(), "Search in the box menu did not set a query");
        driver.expect(screen.selectedItemIndex >= 0 && screen.storageSlotMatchesSearch(0, screen.selectedBoxIndex, screen.selectedItemIndex), "the cursor did not jump to a matching Pokemon", "box=" + std::to_string(screen.selectedBoxIndex) + " slot=" + std::to_string(screen.selectedItemIndex));
        screen.boxMenuIndex = Screen::BOX_MENU_ROW_SEARCH;
        driver.step("clear the search").press(Btn::X).frame();
        screen.boxMenuIndex = Screen::BOX_MENU_ROW_SEARCH;
        driver.type("").press(Btn::A).frame(2);
        driver.expect(!screen.storageSearch.isFiltering(), "accepting an empty search did not clear the box search");
        driver.step("leave the box view").press(Btn::B).frame(2);

        // Storage: X opens the same box menu, and the Pokemon menu no longer carries Find.
        driver.step("open the storage view");
        gotoHome(driver, screen, 2);
        driver.press(Btn::A).frame();
        if (!driver.expect(screen.detailViewActive, "could not open the storage view"))
        {
            return;
        }
        driver.step("X opens the box menu in Storage").press(Btn::X).frame();
        driver.expect(screen.boxMenuActive, "X in the Storage view did not open the box menu");
        driver.see("Search…");
        driver.step("B closes the box menu").press(Btn::B).frame();
        driver.expect(!screen.boxMenuActive, "B did not close the box menu");
        screen.selectedBoxIndex = sourceBox;
        screen.stSaveSlot = sourceSlot;
        driver.step("A opens the Pokemon menu in Storage").press(Btn::A).frame();
        if (driver.expect(screen.storageMenuActive, "A in Storage's Menu mode did not open the Pokemon menu"))
        {
            driver.notSee("Find…");
        }
        driver.press(Btn::B).frame();
    }

    /// A Pokemon carried from the bank into this save becomes the receiving player's. The withdraw
    /// hands the open save's trainer to the conversion, which makes them the active handler; a
    /// FireRed Venusaur once reached Violet showing "(none)" for its handler because only Gen 1/2
    /// were handed one. Driven through the real Storage view so the call site is what is tested,
    /// then put back: the save and the bank are shared with every later test.
    void testWithdrawSetsHandler(Test::Context &context, TrainerViewScreen &screen, Trainer::Trainer &trainer)
    {
        using Screen = TrainerViewScreen;
        const std::unique_ptr<Pokemon::Pokemon> blankPokemon = trainer.createBlankPokemon();
        if (!blankPokemon || !blankPokemon->hasHandler() || trainer.trainerName.empty() || !screen.bank)
        {
            context.note("this format has no handler, or the save no trainer name, so the withdraw handler test was skipped", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }

        // A FireRed Pokemon, the reported case. PKSE cannot build a PK3 from nothing, so it starts as a
        // Gen 7 record written down into FireRed with no receiver -- what a banked FireRed Pokemon is.
        // The first species this game's dex has.
        std::unique_ptr<Pokemon::Pokemon> fireRedPokemon;
        for (const uint16_t speciesId : {3, 25, 133, 1})
        {
            Pokemon::Pokemon7USUM gen7Pokemon;
            gen7Pokemon.setSpecies(speciesId);
            gen7Pokemon.setExp(125000);
            gen7Pokemon.setTID16(24680);
            gen7Pokemon.setOTName(Utils::utf8ToUtf16("KIASTA"));
            gen7Pokemon.recalculateStats();
            Conversion::Result conversionResult = Conversion::Result::Ok;
            auto candidate = Conversion::convert(gen7Pokemon, Enums::GameVersion::FRLG, conversionResult, static_cast<uint8_t>(Enums::GameVersion::FR));
            if (candidate && Conversion::canConvert(*candidate, trainer.getGameGroup(), conversionResult) && conversionResult == Conversion::Result::Ok)
            {
                fireRedPokemon = std::move(candidate);
                break;
            }
        }
        if (!fireRedPokemon)
        {
            context.note("none of the probe species converts into this game, so the withdraw handler test was skipped", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }

        int saveBox = -1;
        int saveSlot = -1;
        for (int boxIndex = 0; boxIndex < static_cast<int>(trainer.getBoxCount()) && saveBox < 0; ++boxIndex)
        {
            for (int slotIndex = 0; slotIndex < static_cast<int>(trainer.getSlotsPerBox()); ++slotIndex)
            {
                const auto &occupant = trainer.boxes[boxIndex][slotIndex];
                if ((!occupant || occupant->speciesID() == 0) && !screen.storageSlotLocked(0, boxIndex, slotIndex))
                {
                    saveBox = boxIndex;
                    saveSlot = slotIndex;
                    break;
                }
            }
        }
        int bankSlot = -1;
        for (int slotIndex = 0; slotIndex < static_cast<int>(Trainer::Bank::BANK_SLOTS_PER_BOX) && bankSlot < 0; ++slotIndex)
        {
            if (!screen.bank->boxes[0][slotIndex])
            {
                bankSlot = slotIndex;
            }
        }
        if (saveBox < 0 || bankSlot < 0)
        {
            context.note("no free save or bank slot, so the withdraw handler test was skipped", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }
        std::unique_ptr<Pokemon::Pokemon> originalSaveSlot = std::move(trainer.boxes[saveBox][saveSlot]);
        screen.bank->boxes[0][bankSlot] = std::move(fireRedPokemon);

        Driver driver(context, screen, "withdraw-handler");
        driver.step("open the storage view");
        gotoHome(driver, screen, 2);
        driver.press(Btn::A).frame();
        if (driver.expect(screen.detailViewActive && screen.selectedMode == Screen::ViewMode::Storage, "could not open the storage view"))
        {
            screen.storageFocusPane = 1;
            screen.bank->currentBox = 0;
            screen.stBankBox = 0;
            screen.stBankSlot = bankSlot;
            driver.step("A opens the Pokemon menu on the banked FireRed Pokemon").press(Btn::A).frame();
            if (driver.expect(screen.storageMenuActive, "A on a bank slot did not open the Pokemon menu"))
            {
                screen.storageMenuIndex = Screen::STORAGE_MENU_ROW_MOVE;
                driver.step("Move picks it up").press(Btn::A).frame();
                if (driver.expect(screen.carrying(), "Move did not pick up the bank Pokemon"))
                {
                    screen.storageFocusPane = 0;
                    screen.selectedBoxIndex = saveBox;
                    screen.stSaveSlot = saveSlot;
                    driver.step("A puts it down in the save").press(Btn::A).frame();
                    if (screen.moveConfirmActive())
                    {
                        driver.step("continue past the transfer notice").press(Btn::A).frame();
                    }
                    const auto &placed = trainer.boxes[saveBox][saveSlot];
                    if (driver.expect(placed && placed->getGameGroup() == trainer.getGameGroup(), "the withdrawn FireRed Pokemon did not land in the save, converted"))
                    {
                        const std::string where = "box=" + std::to_string(saveBox) + " slot=" + std::to_string(saveSlot);
                        driver.expect(placed->currentHandler() == 1, "a Pokemon withdrawn from another game is not in this save's trainer's hands", where + " currentHandler=" + std::to_string(placed->currentHandler()));
                        driver.expect(placed->htName() == Utils::utf8ToUtf16(trainer.trainerName) && placed->htGender() == trainer.trainerGender, "a Pokemon withdrawn from another game does not name this save's trainer as its handler", where + " ht=\"" + Utils::utf16ToUtf8(placed->htName()) + "\" trainer=\"" + trainer.trainerName + "\"");
                        if (placed->hasHandlerLanguage())
                        {
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
    void testCursorMoves(Test::Context &context, TrainerViewScreen &screen)
    {
        Driver driver(context, screen, "cursor");
        driver.step("open the box view");
        gotoHome(driver, screen, 0);
        driver.press(Btn::A).frame();
        if (!driver.expect(screen.detailViewActive, "could not open the box view"))
        {
            return;
        }

        screen.selectedItemIndex = 0;
        driver.step("press Right in the box grid").press(Btn::Right).frame();
        const int afterRight = screen.selectedItemIndex;
        driver.expect(afterRight != 0, "Right did not move the box cursor", "selectedItemIndex stayed at " + std::to_string(afterRight));

        driver.step("press Down in the box grid").press(Btn::Down).frame();
        driver.expect(screen.selectedItemIndex != afterRight, "Down did not move the box cursor", "selectedItemIndex stayed at " + std::to_string(screen.selectedItemIndex));
    }

    /// The trainer card shows the save's own OT name -- the simplest end-to-end proof that what was
    /// parsed out of the file is what reaches the screen.
    void testTrainerCardShowsSaveData(Test::Context &context, TrainerViewScreen &screen, Trainer::Trainer &trainer)
    {
        Driver driver(context, screen, "trainer-card");
        driver.step("open the trainer card");
        gotoHome(driver, screen, 4);
        driver.press(Btn::A).frame(2);
        if (!driver.expect(screen.detailViewActive, "could not open the trainer card"))
        {
            return;
        }
        if (trainer.trainerName.empty())
        {
            context.note("this save has no OT name, so the trainer card check was skipped");
            return;
        }
        driver.step("look for the OT name").see(trainer.trainerName);
    }

    /// Renaming a box: the keyboard answer reaches both the save and the screen, and a CANCEL
    /// changes nothing. The cancel half matters as much as the accept half -- treating a cancel as
    /// an empty string would blank the name the user was editing.
    void testBoxRename(Test::Context &context, TrainerViewScreen &screen, Trainer::Trainer &trainer)
    {
        if (!trainer.supportsBoxNames())
        {
            context.note("this format has no box names, so the rename test was skipped", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }
        Driver driver(context, screen, "box-rename");
        driver.step("open the box view");
        gotoHome(driver, screen, 0);
        driver.press(Btn::A).frame();
        if (!driver.expect(screen.detailViewActive, "could not open the box view"))
        {
            return;
        }

        const int box = screen.selectedBoxIndex;
        if (box < 0 || box >= static_cast<int>(trainer.boxNames.size()))
        {
            return;
        }
        const std::string original = trainer.boxNames[box];

        // Focus the box-name header, which the panel marks with slot index -1. Up from the TOP ROW
        // lands on it in exactly one press; from anywhere else it walks up the grid first. Pressing
        // Up repeatedly does NOT converge -- the header sits in a cycle (top row -> header -> bottom
        // row -> ... -> top row) -- so the cursor is placed first and pressed once.
        screen.selectedItemIndex = 0;
        driver.step("focus the box-name header").press(Btn::Up);
        if (!driver.expect(screen.selectedItemIndex == -1, "Up from the top row did not reach the box-name header", "selectedItemIndex=" + std::to_string(screen.selectedItemIndex)))
        {
            return;
        }

        const std::string wanted = "UITEST";
        driver.step("rename the box").type(wanted).press(Btn::A).frame(2);
        const std::string after = trainer.boxNames[box];
        driver.expect(after == wanted, "a box rename did not reach the save", "wanted=\"" + wanted + "\" got=\"" + after + "\" was=\"" + original + "\"");
        driver.step("the new name is on screen").see(wanted);

        screen.selectedItemIndex = 0;
        driver.step("cancel a rename").press(Btn::Up);
        driver.cancelPrompt().press(Btn::A).frame(2);
        driver.expect(trainer.boxNames[box] == wanted, "cancelling a rename changed the box name anyway", "expected=\"" + wanted + "\" got=\"" + trainer.boxNames[box] + "\"");

        trainer.boxNames[box] = original; // leave the in-memory save as we found it
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
    void testTapIsSeenBeforeItActs(Test::Context &context, TrainerViewScreen &screen, Trainer::Trainer &trainer)
    {
        Driver driver(context, screen, "tap-feedback");
        driver.step("open the items view");
        gotoHome(driver, screen, 3);
        driver.press(Btn::A).frame(2);
        if (!driver.expect(screen.detailViewActive, "could not open the items view"))
        {
            return;
        }

        // The first pouch can be empty (a Gold or Pearl save's is), so R moves on to one with an item.
        for (size_t pouchIndex = 0; pouchIndex < trainer.items.size() && screen.visibleItemIndices().empty(); ++pouchIndex)
        {
            driver.step("next pouch, looking for an item").press(Btn::R).frame();
        }
        if (screen.visibleItemIndices().empty())
        {
            context.note("this save's bag is empty, so tap feedback was not exercised", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }

        driver.step("open the item edit dialog").press(Btn::A).frame(2);
        if (!driver.expect(screen.itemEditDialogActive, "A on an item did not open the edit dialog"))
        {
            return;
        }

        // Cancel is id 0 in every dialog here. Its rect comes from the screen's own hit list, so the
        // test taps exactly where the dialog says the button is rather than at a copied coordinate.
        const UI::TouchButton *cancel = nullptr;
        for (const auto &touchButton : screen.touchButtons)
        {
            if (touchButton.buttonId == 0)
            {
                cancel = &touchButton;
                break;
            }
        }
        if (!driver.expect(cancel != nullptr, "the item edit dialog published no Cancel button to tap"))
        {
            return;
        }

        const int tapX = cancel->hitX + cancel->hitWidth / 2;
        const int tapY = cancel->hitY + cancel->hitHeight / 2;
        const int buttonX = cancel->hitX, buttonY = cancel->hitY;

        driver.step("tap Cancel").tap(tapX, tapY);

        // Half one: the dialog is still up. The press has been recorded, not delivered.
        driver.expect(screen.itemEditDialogActive, "a tapped dialog button acted in its own frame, before it was ever drawn held", "dialogActive=0");

        // Half two: it is drawn held. The accent ring drawGlyphButton strokes around a pressed
        // button, at that button's own rectangle -- so this fails both if the ring is missing and if
        // it is drawn somewhere other than the button the user actually touched.
        bool held = false;
        for (const UIHost::Draw &draw : driver.frameData().draws)
        {
            if (draw.kind != UIHost::Kind::Shape || std::string(draw.primitive) != "roundedRect" || !draw.visible)
            {
                continue;
            }
            if (draw.drawX != buttonX || draw.drawY != buttonY)
            {
                continue;
            }
            if (draw.color.red == UI::Colors::Primary.red && draw.color.green == UI::Colors::Primary.green && draw.color.blue == UI::Colors::Primary.blue)
            {
                held = true;
                break;
            }
        }
        driver.expect(held, "a tapped dialog button was not drawn held while its press waited", "buttonX=" + std::to_string(buttonX) + " buttonY=" + std::to_string(buttonY));

        // ...and then it acts. A press that is deferred and never delivered is a dead button, which
        // is the failure this half exists to catch.
        driver.step("let the press land").frame(TrainerViewScreen::TAP_PRESS_FRAMES + 1);
        driver.expect(!screen.itemEditDialogActive, "a tapped dialog button never delivered its press -- the dialog stayed open", "framesWaited=" + std::to_string(TrainerViewScreen::TAP_PRESS_FRAMES + 1));
        (void)trainer;
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
    void testEggTextIsNotForeign(Test::Context &context, TrainerViewScreen &screen, Trainer::Trainer &trainer)
    {
        int partySlot = -1;
        int boxIndex = -1;
        int boxSlot = -1;
        for (int slot = 0; slot < static_cast<int>(trainer.party.size()); ++slot)
        {
            if (trainer.party[slot] && trainer.party[slot]->eggTextIsPlaceholder())
            {
                partySlot = slot;
                break;
            }
        }
        if (partySlot < 0)
        {
            for (int box = 0; box < static_cast<int>(trainer.boxes.size()) && boxSlot < 0; ++box)
            {
                for (int slot = 0; slot < static_cast<int>(trainer.boxes[box].size()); ++slot)
                {
                    if (trainer.boxes[box][slot] && trainer.boxes[box][slot]->eggTextIsPlaceholder())
                    {
                        boxIndex = box;
                        boxSlot = slot;
                        break;
                    }
                }
            }
        }
        if (partySlot < 0 && boxSlot < 0)
        {
            context.note("this save holds no egg with placeholder text, so the egg details page was not exercised", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }

        Driver driver(context, screen, "egg-details");
        if (partySlot >= 0)
        {
            driver.step("open the party view");
            gotoHome(driver, screen, 1);
            driver.press(Btn::A).frame();
            if (!driver.expect(screen.detailViewActive, "could not open the party view"))
            {
                return;
            }
            screen.selectedPartyIndex = partySlot;
        }
        else
        {
            driver.step("open the box holding the egg");
            gotoHome(driver, screen, 0);
            driver.press(Btn::A).frame();
            if (!driver.expect(screen.detailViewActive, "could not open the box view"))
            {
                return;
            }
            screen.selectedBoxIndex = boxIndex;
            screen.selectedItemIndex = boxSlot;
        }
        driver.step("open the details page on the egg").press(Btn::A).frame(2);
        if (!driver.expect(screen.details.active, "A on an egg did not open the details page"))
        {
            return;
        }

        // The rows are read by SCROLLING the page rather than by guessing where they sit: the left
        // column only draws what is on screen, and the Language row starts below the fold.
        bool sawPlaceholder = false;
        bool sawJapanese = false;
        bool sawHatchNote = false;
        bool sawTrainerName = false;
        for (int scroll = 0; scroll <= 600; scroll += 40)
        {
            screen.details.leftScroll = scroll;
            driver.frame();
            sawPlaceholder |= driver.frameData().has("タマゴ");
            sawJapanese |= driver.frameData().has("Japanese");
            sawHatchNote |= driver.frameData().has("(set when it hatches)");
            sawTrainerName |= trainer.trainerName.empty() || driver.frameData().has(trainer.trainerName);
        }
        // The stored placeholder, byte for byte what the games write. Showing it anywhere means the
        // page is reading an egg's nickname field as if it were a name.
        driver.expect(!sawPlaceholder, "an egg's details page showed the stored Japanese placeholder");
        // The OT the player actually has. Read through the Japanese table the same bytes come out as
        // full-width letters, so this fails on exactly the reported bug rather than on its shape.
        driver.expect(sawTrainerName, "an egg's details page did not show the save's own trainer name", "want=\"" + trainer.trainerName + "\"");
        // An egg has no language yet -- the hatch writes the game's own.
        driver.expect(!sawJapanese, "an egg's details page claimed a language its record cannot answer for");
        driver.expect(sawHatchNote, "the Language row did not say the language is set when the egg hatches");
    }

    /// The Alpha row on the details page: shown exactly where the format stores the flag, and A or a
    /// tap on it flips the flag on the Pokemon being edited.
    ///
    /// Becoming an Alpha also rewrites the size, so the record's bytes are put back afterwards: the
    /// trainer is shared with every later test on this save.
    void testAlphaToggle(Test::Context &context, TrainerViewScreen &screen, Trainer::Trainer &trainer)
    {
        int boxIndex = -1;
        int boxSlot = -1;
        for (int box = 0; box < static_cast<int>(trainer.boxes.size()) && boxSlot < 0; ++box)
        {
            for (int slot = 0; slot < static_cast<int>(trainer.boxes[box].size()); ++slot)
            {
                if (!Test::isEmptySlot(trainer.boxes[box][slot].get()) && !trainer.boxes[box][slot]->isEgg())
                {
                    boxIndex = box;
                    boxSlot = slot;
                    break;
                }
            }
        }
        if (boxSlot < 0)
        {
            context.note("this save's boxes hold no Pokemon, so the Alpha row was not exercised", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }

        Driver driver(context, screen, "alpha-toggle");
        driver.step("open the box view");
        gotoHome(driver, screen, 0);
        driver.press(Btn::A).frame();
        if (!driver.expect(screen.detailViewActive, "could not open the box view"))
        {
            return;
        }
        screen.selectedBoxIndex = boxIndex;
        screen.selectedItemIndex = boxSlot;
        driver.step("open the details page").press(Btn::A).frame(2);
        if (!driver.expect(screen.details.active, "A on an occupied slot did not open the details page"))
        {
            return;
        }

        Pokemon::Pokemon *pokemon = screen.detailsTargetPokemon();
        if (!driver.expect(pokemon != nullptr, "the details page has no Pokemon to edit"))
        {
            return;
        }
        constexpr int alphaField = 34;
        const auto &leftColumnOrder = screen.details.leftOrder;
        const bool alphaRowListed = std::find(leftColumnOrder.begin(), leftColumnOrder.end(), alphaField) != leftColumnOrder.end();
        if (!pokemon->hasAlphaFlag())
        {
            driver.expect(!alphaRowListed, "the details page offers an Alpha row for a format with no Alpha flag");
            return;
        }
        if (!driver.expect(alphaRowListed, "the details page offers no Alpha row although the format stores the flag"))
        {
            return;
        }

        const std::span<const std::byte> originalSpan = pokemon->getData();
        const std::vector<std::byte> originalBytes(originalSpan.begin(), originalSpan.end());
        const bool originalAlpha = pokemon->isAlpha();

        screen.details.selectedField = alphaField;
        driver.step("bring the Alpha row into view").frame(2);
        driver.see("Alpha");
        driver.step("toggle Alpha with A").press(Btn::A).frame(2);
        pokemon = screen.detailsTargetPokemon();
        if (driver.expect(pokemon != nullptr, "the details page lost its Pokemon after the toggle"))
        {
            driver.expect(pokemon->isAlpha() != originalAlpha, "A on the Alpha row did not change the flag", "before=" + std::to_string(originalAlpha));
            driver.expect(screen.hasUnsavedChanges, "toggling Alpha did not mark the save as changed");
        }

        // The tap goes through the row's own published hit rectangle, the path a touch screen takes.
        const UI::TouchButton *alphaButton = nullptr;
        for (const auto &button : screen.touchButtons)
        {
            if (button.buttonId == alphaField)
            {
                alphaButton = &button;
                break;
            }
        }
        if (driver.expect(alphaButton != nullptr, "the Alpha row published no touch target"))
        {
            driver.step("toggle Alpha back with a tap").tap(alphaButton->hitX + alphaButton->hitWidth / 2, alphaButton->hitY + alphaButton->hitHeight / 2).frame(TrainerViewScreen::TAP_PRESS_FRAMES + 1);
            pokemon = screen.detailsTargetPokemon();
            driver.expect(pokemon && pokemon->isAlpha() == originalAlpha, "a tap on the Alpha row did not change the flag back");
        }

        pokemon = screen.detailsTargetPokemon();
        if (pokemon)
        {
            std::memcpy(pokemon->getData().data(), originalBytes.data(), originalBytes.size());
        }
    }

    /// Hyper Training, seen and edited. The stat editor's row sets and clears one stat's flag, the
    /// stat follows it while the stored IV stays put, Cancel keeps nothing, the details page and the
    /// party card both mark a trained IV, and the row refuses what the game could not have done:
    /// below the format's level, or on an IV that is already maximal.
    void testHyperTraining(Test::Context &context, TrainerViewScreen &screen, Trainer::Trainer &trainer)
    {
        int boxIndex = -1;
        int boxSlot = -1;
        for (int box = 0; box < static_cast<int>(trainer.boxes.size()) && boxSlot < 0; ++box)
        {
            for (int slot = 0; slot < static_cast<int>(trainer.boxes[box].size()); ++slot)
            {
                if (!Test::isEmptySlot(trainer.boxes[box][slot].get()) && !trainer.boxes[box][slot]->isEgg())
                {
                    boxIndex = box;
                    boxSlot = slot;
                    break;
                }
            }
        }
        if (boxSlot < 0)
        {
            context.note("this save's boxes hold no Pokemon, so the Hyper Training row was not exercised", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }

        Pokemon::Pokemon *target = trainer.boxes[boxIndex][boxSlot].get();
        const std::span<const std::byte> originalSpan = target->getData();
        const std::vector<std::byte> originalBytes(originalSpan.begin(), originalSpan.end());
        // Let's Go keeps its party in the boxes, and a saved edit is mirrored into the party copy, so
        // the party is put back as well as the box slot.
        std::vector<std::vector<std::byte>> originalPartyBytes;
        for (const auto &member : trainer.party)
        {
            if (!member)
            {
                originalPartyBytes.emplace_back();
                continue;
            }
            const std::span<const std::byte> memberSpan = member->getData();
            originalPartyBytes.emplace_back(memberSpan.begin(), memberSpan.end());
        }
        auto restore = [&]()
        {
            std::memcpy(target->getData().data(), originalBytes.data(), originalBytes.size());
            for (size_t partyIndex = 0; partyIndex < trainer.party.size() && partyIndex < originalPartyBytes.size(); ++partyIndex)
            {
                if (trainer.party[partyIndex] && !originalPartyBytes[partyIndex].empty())
                {
                    std::memcpy(trainer.party[partyIndex]->getData().data(), originalPartyBytes[partyIndex].data(), originalPartyBytes[partyIndex].size());
                }
            }
        };
        const int maxIV = target->maxIV();
        // A known starting point whatever the save holds: old enough to train, HP untrained and below
        // the maximum. A Legends: Arceus Pokemon also needs the HOME visit its flags would come from.
        if (target->hasHyperTraining())
        {
            target->setLevel(100);
            target->setIV(0, 10);
            target->setHyperTrainFlags(0);
            if (target->hasHomeTracker() && target->homeTracker() == 0)
            {
                target->setHomeTracker(0x0123456789ABCDEFull);
            }
            target->recalculateStats();
            target->refreshChecksum();
        }

        Driver driver(context, screen, "hyper-training");
        driver.step("open the box view");
        gotoHome(driver, screen, 0);
        driver.press(Btn::A).frame();
        if (!driver.expect(screen.detailViewActive, "could not open the box view"))
        {
            return;
        }
        screen.selectedBoxIndex = boxIndex;
        screen.selectedItemIndex = boxSlot;
        driver.step("open the details page").press(Btn::A).frame(2);
        if (!driver.expect(screen.details.active, "A on an occupied slot did not open the details page"))
        {
            return;
        }
        Pokemon::Pokemon *pokemon = screen.detailsTargetPokemon();
        if (!driver.expect(pokemon == target, "the details page is not editing the Pokemon it was opened on"))
        {
            restore();
            return;
        }

        screen.details.selectedField = 0;
        driver.step("open the HP stat editor").press(Btn::A).frame();
        if (!driver.expect(screen.statEdit.dialogActive, "A on the HP row did not open the stat editor"))
        {
            restore();
            return;
        }
        if (!pokemon->hasHyperTraining())
        {
            driver.notSee("Hyper Trained");
            driver.step("close the stat editor").press(Btn::B).frame();
            restore();
            return;
        }
        driver.see("Hyper Trained");
        driver.see("Plays as 10");

        const uint16_t untrainedHP = pokemon->statHPMax();
        driver.step("select the Hyper Trained row").press(Btn::Down).frame();
        driver.expect(screen.statEdit.mode == UI::Dialogs::StatEditMode::HyperTrain, "Down from the IV row did not select the Hyper Trained row");
        driver.step("set it").press(Btn::Right).frame();
        driver.expect(screen.statEdit.currentHyperTrained, "Right on the Hyper Trained row did not set it");
        driver.see("Plays as " + std::to_string(maxIV));
        driver.step("save the stat").press(Btn::A).frame();
        driver.expect(!screen.statEdit.dialogActive, "A did not close the stat editor");
        pokemon = screen.detailsTargetPokemon();
        if (!driver.expect(pokemon != nullptr, "the details page lost its Pokemon after the edit"))
        {
            restore();
            return;
        }
        driver.expect(pokemon->isHyperTrained(0), "saving the stat editor did not set HP's Hyper Training flag", "flags=" + std::to_string(pokemon->hyperTrainFlags()));
        driver.expect(pokemon->hyperTrainFlags() == 0x01, "saving one stat's Hyper Training touched another stat's flag", "flags=" + std::to_string(pokemon->hyperTrainFlags()));
        driver.expect(pokemon->ivHP() == 10, "Hyper Training changed the stored IV", "ivHP=" + std::to_string(pokemon->ivHP()));
        driver.expect(pokemon->checksumValid(), "the record's checksum is stale after Hyper Training");
        driver.expect(screen.hasUnsavedChanges, "Hyper Training did not mark the save as changed");
        // Shedinja's HP is 1 whatever its IV, so it has nothing to show here.
        if (pokemon->speciesID() != 292 && untrainedHP != 0)
        {
            driver.expect(pokemon->statHPMax() > untrainedHP, "the HP stat did not follow its Hyper Training flag", "before=" + std::to_string(untrainedHP) + " after=" + std::to_string(pokemon->statHPMax()));
        }
        driver.step("the details page marks the trained IV").frame();
        driver.see("HT");
        driver.see("HT: Hyper Trained, plays as " + std::to_string(maxIV));

        // The tap path: the first tap on the row selects it, the second flips it.
        driver.step("reopen the HP stat editor").press(Btn::A).frame();
        if (!driver.expect(screen.statEdit.dialogActive && screen.statEdit.currentHyperTrained, "the reopened stat editor does not show HP as Hyper Trained"))
        {
            restore();
            return;
        }
        for (int tapIndex = 0; tapIndex < 2; ++tapIndex)
        {
            const UI::TouchButton *rowButton = nullptr;
            for (const auto &button : screen.touchButtons)
            {
                if (button.buttonId == 12)
                {
                    rowButton = &button;
                    break;
                }
            }
            if (!driver.expect(rowButton != nullptr, "the Hyper Trained row published no touch target"))
            {
                restore();
                return;
            }
            driver.step(tapIndex == 0 ? "tap the Hyper Trained row" : "tap it again").tap(rowButton->hitX + rowButton->hitWidth / 2, rowButton->hitY + rowButton->hitHeight / 2).frame();
        }
        driver.expect(screen.statEdit.mode == UI::Dialogs::StatEditMode::HyperTrain && !screen.statEdit.currentHyperTrained, "two taps on the Hyper Trained row did not select it and clear it");
        driver.step("cancel").press(Btn::B).frame();
        pokemon = screen.detailsTargetPokemon();
        driver.expect(pokemon && pokemon->isHyperTrained(0), "Cancel kept the cleared Hyper Training flag");

        // Raising a trained IV to the maximum leaves a flag the game could never have set; it drops.
        driver.step("raise the trained IV to the maximum").press(Btn::A).frame();
        driver.press(Btn::R, 3).frame();
        driver.expect(screen.statEdit.currentIV == maxIV && !screen.statEdit.currentHyperTrained, "raising a Hyper Trained IV to the maximum kept the flag", "iv=" + std::to_string(screen.statEdit.currentIV));
        driver.see("IV is already " + std::to_string(maxIV));
        driver.step("cancel").press(Btn::B).frame();

        // Below the format's level the row refuses to set the flag, says why, and gives way to the
        // illegal-values override.
        pokemon = screen.detailsTargetPokemon();
        if (!pokemon)
        {
            restore();
            return;
        }
        pokemon->setHyperTrainFlags(0);
        pokemon->setLevel(static_cast<uint8_t>(pokemon->hyperTrainMinimumLevel() - 1));
        driver.step("open the stat editor below the level").press(Btn::A).frame();
        driver.press(Btn::Down).frame();
        driver.press(Btn::Right).frame();
        driver.expect(!screen.statEdit.currentHyperTrained, "the Hyper Trained row set a flag below the level the game requires", "level=" + std::to_string(pokemon->hyperTrainMinimumLevel() - 1));
        driver.see("Needs Lv. " + std::to_string(pokemon->hyperTrainMinimumLevel()));
        g_allowIllegalEdits = true;
        driver.press(Btn::Right).frame();
        g_allowIllegalEdits = false;
        driver.expect(screen.statEdit.currentHyperTrained, "the illegal-values override did not let the Hyper Trained row set the flag");
        driver.step("cancel").press(Btn::B).frame();
        restore();
    }

    /// The party card, where a team is checked at a glance, marks a Hyper Trained IV too.
    void testHyperTrainingPartyMark(Test::Context &context, TrainerViewScreen &screen, Trainer::Trainer &trainer)
    {
        Pokemon::Pokemon *partyMember = nullptr;
        for (auto &member : trainer.party)
        {
            if (!Test::isEmptySlot(member.get()) && !member->isEgg())
            {
                partyMember = member.get();
                break;
            }
        }
        if (!partyMember)
        {
            context.note("this save's party is empty, so the party card's Hyper Training mark was not checked", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }
        if (!partyMember->hasHyperTraining())
        {
            return;
        }
        const uint8_t savedPartyFlags = partyMember->hyperTrainFlags();
        partyMember->setHyperTrainFlags(0);
        Driver party(context, screen, "hyper-training-party");
        party.step("open the party view");
        gotoHome(party, screen, 1);
        party.press(Btn::A).frame(2);
        if (party.expect(screen.detailViewActive && screen.selectedMode == TrainerViewScreen::ViewMode::Party, "could not open the party view"))
        {
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
    void testPickerSearch(Test::Context &context, TrainerViewScreen &screen, Trainer::Trainer &trainer)
    {
        Driver driver(context, screen, "picker-search");
        driver.step("open the box view");
        gotoHome(driver, screen, 0);
        driver.press(Btn::A).frame();
        if (!driver.expect(screen.detailViewActive, "could not open the box view"))
        {
            return;
        }

        // Land on an occupied slot; an empty one has no details page to open.
        int occupied = -1;
        const int box = screen.selectedBoxIndex;
        if (box >= 0 && box < static_cast<int>(trainer.boxes.size()))
        {
            for (int slot = 0; slot < static_cast<int>(trainer.boxes[box].size()); ++slot)
            {
                if (!Test::isEmptySlot(trainer.boxes[box][slot].get()))
                {
                    occupied = slot;
                    break;
                }
            }
        }
        if (occupied < 0)
        {
            context.note("this save's first box is empty, so the picker search was not exercised", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }
        screen.selectedItemIndex = occupied;
        driver.step("open the details modal").press(Btn::A).frame(2);
        if (!driver.expect(screen.details.active, "A on an occupied slot did not open the details modal"))
        {
            return;
        }

        expectOneNavBar(driver, "details page open");

        screen.details.selectedField = 32; // the Species row
        driver.step("open the species picker").press(Btn::A).frame(2);
        if (!driver.expect(screen.pickerActive, "A on the Species row did not open a picker"))
        {
            return;
        }
        // The case the user reported: a picker drawn OVER the details page, which paints its own
        // bar. Two bars here means the page is still advertising Edit / Ribbons / Save while the
        // picker holds input.
        expectOneNavBar(driver, "species picker open over the details page");
        driver.expect(!screen.overlayNavHint().empty(), "an open picker does not name its own controls, so the bar shows the page's");

        const int unfilteredCount = screen.pickerVisibleCount();
        driver.expect(unfilteredCount > 1, "the species picker offered nothing to search", "count=" + std::to_string(unfilteredCount));
        driver.expect(!screen.pickerSearch.isFiltering(), "a picker opened with a filter already applied");

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
            screen.pickerSel = unfilteredCount / 2; // deep in the list, where the window has to scroll to it
            driver.step("the picker drawn under an open keyboard").frame();
            bool everyRowAboveKeyboard = true;
            bool selectedRowShown = false;
            for (const auto &button : screen.touchButtons)
            {
                if (button.hitY + button.hitHeight > keyboard.cardTop())
                {
                    everyRowAboveKeyboard = false;
                }
                if (button.buttonId == screen.pickerSel)
                {
                    selectedRowShown = true;
                }
            }
            driver.expect(!screen.touchButtons.empty(), "the picker drew no rows at all under the keyboard");
            driver.expect(everyRowAboveKeyboard, "a picker row was laid out under the keyboard, where it cannot be seen", "keyboardTop=" + std::to_string(keyboard.cardTop()));
            driver.expect(selectedRowShown, "the selected row was not among the rows left above the keyboard");
            expectOneNavBar(driver, "keyboard open over the species picker");
            driver.expect(screen.overlayNavHint() == keyboard.navHint(), "the bar did not name the keyboard's keys while it held input");
            UI::Dialogs::setActiveKeyboard(nullptr);
            screen.pickerSel = 0;
            driver.frame();
        }

        // "chu" is in Pikachu, Raichu and Pichu, and in few enough other names that the list must
        // shrink. A query no dex can match would prove nothing about the filter working.
        driver.step("search the species list").type("chu").press(Btn::Y).frame(2);
        const int filteredCount = screen.pickerVisibleCount();
        driver.expect(screen.pickerSearch.isFiltering(), "typing a query did not put the picker into search mode");
        driver.expect(filteredCount > 0, "searching \"chu\" matched no species at all");
        driver.expect(filteredCount < unfilteredCount, "the search did not narrow the list", "before=" + std::to_string(unfilteredCount) + " after=" + std::to_string(filteredCount));

        // EVERY visible row must match, and each must resolve to the same value the row draws.
        bool everyRowMatches = true;
        bool everyRowResolves = true;
        for (int visibleRow = 0; visibleRow < filteredCount; ++visibleRow)
        {
            const int row = screen.pickerRowForVisibleIndex(visibleRow);
            if (row < 0 || row >= screen.pickerCount)
            {
                everyRowResolves = false;
                continue;
            }
            const int pickerValue = (screen.pickerUsesOrder() && row < static_cast<int>(screen.pickerOrder.size())) ? screen.pickerOrder[row] : row;
            const std::string label = UI::Dialogs::pickerRowLabel(screen, screen.pickerKind, pickerValue);
            if (!screen.pickerSearch.matches(label.c_str()))
            {
                everyRowMatches = false;
            }
        }
        driver.expect(everyRowMatches, "a row survived the filter without matching the query");
        driver.expect(everyRowResolves, "a visible row did not map back to a row in the full list");

        // The cursor's value must be the one its row shows -- the whole point of the indirection.
        screen.pickerSel = 0;
        const int firstRow = screen.pickerRowForVisibleIndex(0);
        const int expectedValue = (screen.pickerUsesOrder() && firstRow >= 0 && firstRow < static_cast<int>(screen.pickerOrder.size())) ? screen.pickerOrder[firstRow] : firstRow;
        driver.expect(screen.pickerSelectedValue() == expectedValue, "the selected value did not follow the filtered row", "value=" + std::to_string(screen.pickerSelectedValue()) + " expected=" + std::to_string(expectedValue));
        driver.step("the filtered list is on screen").see(UI::Dialogs::pickerRowLabel(screen, screen.pickerKind, expectedValue));

        // An empty query clears the filter -- the only way out, so it has to work.
        driver.step("clear the search").type("").press(Btn::Y).frame(2);
        driver.expect(!screen.pickerSearch.isFiltering(), "an empty query did not clear the filter");
        driver.expect(screen.pickerVisibleCount() == unfilteredCount, "clearing the search did not restore the list", "restored=" + std::to_string(screen.pickerVisibleCount()) + " original=" + std::to_string(unfilteredCount));

        // A query nothing matches must say so and refuse to apply, rather than writing row 0.
        driver.step("search for something absent").type("zzzznotaspecies").press(Btn::Y).frame(2);
        driver.expect(screen.pickerVisibleCount() == 0, "a nonsense query still matched something");
        driver.see("Nothing matches that search.");
        const Pokemon::Pokemon *before = screen.detailsTargetPokemon();
        const uint16_t speciesBefore = before ? before->speciesID() : 0;
        driver.step("A on an empty result does nothing").press(Btn::A).frame(2);
        const Pokemon::Pokemon *after = screen.detailsTargetPokemon();
        driver.expect(after && after->speciesID() == speciesBefore, "pressing A with no search results changed the Pokemon anyway");
        driver.expect(screen.pickerActive, "pressing A with no search results closed the picker");

        driver.step("leave the picker").type("").press(Btn::Y).frame(2);
        driver.press(Btn::B).frame(2);

        // A dead end that has an answer must give it.
        //
        // The met list is in-world PLACES. Who an egg came from -- "Nursery Couple", "a Nursery
        // worker", Riley -- is a different bank of ids belonging to the Egg Loc field, so searching
        // one here finds nothing, correctly. A user hit exactly that and reported the entry missing:
        // it was one row away with nothing to say so. The empty state names the row now.
        {
            const Pokemon::Pokemon *target = screen.detailsTargetPokemon();
            const Names::LocationTable eggSources = target ? Names::getEggSourceLocationTable(target->originGame()) : Names::LocationTable{nullptr, 0};
            bool eggSourceMatchesQuery = false;
            for (uint16_t sourceIndex = 0; sourceIndex < eggSources.count; ++sourceIndex)
            {
                if (eggSources.names[sourceIndex][0] != '\0' && UI::listSearchMatches(eggSources.names[sourceIndex], "nursery"))
                {
                    eggSourceMatchesQuery = true;
                }
            }

            if (!target || !target->hasMetData() || !eggSourceMatchesQuery)
            {
                context.note("the met-location dead-end hint was not exercised: this origin has no egg-source list", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            }
            else
            {
                screen.details.selectedField = 25; // the Met row
                driver.step("open the met-location picker").press(Btn::A).frame(2);
                if (driver.expect(screen.pickerActive, "A on the Met row did not open a picker"))
                {
                    driver.step("search the met list for an egg source").type("nursery").press(Btn::Y).frame(2);
                    driver.expect(screen.pickerVisibleCount() == 0, "an egg source turned up in the met-location list, which holds places only", "matches=" + std::to_string(screen.pickerVisibleCount()));
                    driver.see("Egg Loc");
                    driver.step("leave the met picker").type("").press(Btn::Y).frame(2);
                    driver.press(Btn::B).frame(2);
                }
            }
        }
    }

    /// Hosts the on-screen keyboard the way UIManager does -- a screen underneath, the card on top --
    /// with a nav bar naming the keyboard's keys, as a real screen's bar does while it is up.
    class KeyboardHostScreen : public UI::UIScreen
    {
    public:
        UI::Dialogs::KeyboardState keyboard;
        std::vector<std::string> reportedTexts; // every text the keyboard handed its caller, in order

        void open(Utils::KeyboardRequest request)
        {
            reportedTexts.clear();
            request.textChanged = [this](const std::string &typedText) { reportedTexts.push_back(typedText); };
            keyboard.open(request, 1280, 720);
        }
        void update(const PadState &pad, const UI::TouchInput &touch) override { keyboard.update(pad, touch); }
        void draw(UI::PKSEFramebuffer &framebuffer) override
        {
            framebuffer.clear(UI::Colors::Background);
            UI::drawNavBar(framebuffer, keyboard.navHint());
            UI::Dialogs::drawKeyboard(keyboard, framebuffer);
        }
    };

    int keyIndexFor(const UI::Dialogs::KeyboardState &keyboard, UI::Dialogs::KeyboardKeyAction action, const std::string &label = std::string())
    {
        for (int keyIndex = 0; keyIndex < static_cast<int>(keyboard.keys.size()); ++keyIndex)
        {
            const UI::Dialogs::KeyboardKey &key = keyboard.keys[keyIndex];
            if (key.action == action && (label.empty() || key.lowerLabel == label))
            {
                return keyIndex;
            }
        }
        return -1;
    }

    void tapKey(Driver &driver, const UI::Dialogs::KeyboardState &keyboard, int keyIndex)
    {
        if (keyIndex < 0)
        {
            return;
        }
        const UI::Dialogs::KeyboardKey &key = keyboard.keys[keyIndex];
        driver.tap(key.keyX + key.keyWidth / 2, key.keyY + key.keyHeight / 2);
    }

    /// PKSE's own keyboard, driven with nothing but pad presses and taps. Needs no save: every rule
    /// it has is about the text it holds and the keys it draws.
    void testOnScreenKeyboard(Test::Context &context)
    {
        using UI::Dialogs::KeyboardKeyAction;
        KeyboardHostScreen host;
        const UI::Dialogs::KeyboardState &keyboard = host.keyboard;
        Driver driver(context, host, "keyboard");
        const auto textIs = [&](const std::string &expected, const std::string &what) { driver.expect(keyboard.text == expected, what, "text=\"" + keyboard.text + "\" expected=\"" + expected + "\""); };

        Utils::KeyboardRequest nickname;
        nickname.header = "Nickname";
        nickname.guide = "Nickname";
        nickname.initialText = "Pika";
        nickname.maxChars = 12;
        host.open(nickname);
        driver.step("open a text prompt").frame();
        driver.see("Nickname");
        driver.see("Pika");
        driver.see("4 / 12");
        driver.see("Shift");
        expectOneNavBar(driver, "keyboard open");

        // The card sits above the nav bar, and every key is inside it and a full touch target.
        driver.expect(keyboard.cardY + keyboard.cardHeight <= 720 - UI::NAV_BAR_HEIGHT, "the keyboard card runs into the nav bar", "cardBottom=" + std::to_string(keyboard.cardY + keyboard.cardHeight));
        bool everyKeyInCard = true;
        bool everyKeyTouchable = true;
        bool keysOverlap = false;
        for (size_t keyIndex = 0; keyIndex < keyboard.keys.size(); ++keyIndex)
        {
            const UI::Dialogs::KeyboardKey &key = keyboard.keys[keyIndex];
            if (key.keyX < keyboard.cardX || key.keyX + key.keyWidth > keyboard.cardX + keyboard.cardWidth || key.keyY < keyboard.cardY || key.keyY + key.keyHeight > keyboard.cardY + keyboard.cardHeight)
            {
                everyKeyInCard = false;
            }
            if (key.keyHeight < UI::TouchTargetMin)
            {
                everyKeyTouchable = false;
            }
            for (size_t otherIndex = keyIndex + 1; otherIndex < keyboard.keys.size(); ++otherIndex)
            {
                const UI::Dialogs::KeyboardKey &other = keyboard.keys[otherIndex];
                if (key.keyX < other.keyX + other.keyWidth && other.keyX < key.keyX + key.keyWidth && key.keyY < other.keyY + other.keyHeight && other.keyY < key.keyY + key.keyHeight)
                {
                    keysOverlap = true;
                }
            }
        }
        driver.expect(everyKeyInCard, "a key is drawn outside the keyboard card");
        driver.expect(everyKeyTouchable, "a key is shorter than the minimum touch target");
        driver.expect(!keysOverlap, "two keys overlap, so one tap could land on either");

        // The cursor opens on q; two steps right is e.
        driver.step("type with the controller").press(Btn::Right, 2).press(Btn::A);
        textIs("Pikae", "A did not type the focused key");
        driver.expect(!host.reportedTexts.empty() && host.reportedTexts.back() == "Pikae", "an edit was not handed to the prompt's caller as it happened -- a live search could not follow it");

        driver.step("shift capitalises one letter").press(Btn::X);
        driver.expect(!driver.frameData().primitives("shift-on").empty(), "X did not turn shift on");
        driver.expect(!driver.frameData().textExact("E").empty(), "the letter keys did not show capitals while shifted");
        driver.press(Btn::A);
        textIs("PikaeE", "a shifted key did not type its capital");
        driver.press(Btn::A);
        textIs("PikaeEe", "shift stayed on after one letter");

        driver.step("B deletes").press(Btn::B);
        textIs("PikaeE", "B did not delete the character before the caret");
        driver.step("hold B through the whole field").hold(Btn::B, 120);
        textIs("", "holding B did not keep deleting");
        driver.expect(!keyboard.finished, "holding B ran on past the empty field and closed the prompt");
        driver.step("a fresh B on an empty field cancels").press(Btn::B);
        driver.expect(keyboard.finished && !keyboard.accepted, "a fresh B on an empty field did not cancel");
        driver.expect(!keyboard.result().accepted && keyboard.result().text.empty(), "a cancelled prompt still handed back text -- the caller would blank the field");

        // The left stick steps the selection exactly as the D-pad does: one key per push, nothing
        // below the press threshold, the dominant axis on a diagonal, and the same repeat when held.
        // Each case types whatever key it lands on into an empty field, so the two routes compare
        // by the text they produce.
        Utils::KeyboardRequest stickPrompt;
        stickPrompt.header = "Stick";
        stickPrompt.guide = "Stick";
        stickPrompt.maxChars = 12;
        const auto typedAfter = [&](const std::function<void()> &navigate)
        {
            host.open(stickPrompt);
            driver.frame();
            navigate();
            driver.press(Btn::A);
            return keyboard.text;
        };
        driver.step("the left stick moves the keyboard selection like the D-pad");
        const std::string dpadOneStep = typedAfter([&] { driver.press(Btn::Right); });
        const std::string stickOneStep = typedAfter([&] { driver.tilt(20000, 0); });
        driver.expect(!dpadOneStep.empty() && stickOneStep == dpadOneStep, "a stick push right did not move like D-pad Right", "stick=\"" + stickOneStep + "\" dpad=\"" + dpadOneStep + "\"");
        const std::string noStep = typedAfter([] {});
        driver.expect(typedAfter([&] { driver.tilt(8000, 0); }) == noStep, "a stick barely off centre moved the selection");
        driver.expect(typedAfter([&] { driver.tilt(20000, 12000); }) == dpadOneStep, "a diagonal stick push did not follow its dominant axis");
        driver.expect(typedAfter([&] { driver.tilt(0, -20000); }) == typedAfter([&] { driver.press(Btn::Down); }), "a stick push down did not move like D-pad Down");
        const std::string dpadHeld = typedAfter([&] { driver.hold(Btn::Right, 40); });
        const std::string stickHeld = typedAfter([&] { driver.tilt(20000, 0, 40); });
        driver.expect(dpadHeld != dpadOneStep && stickHeld == dpadHeld, "a held stick did not repeat like a held D-pad", "stick=\"" + stickHeld + "\" dpad=\"" + dpadHeld + "\"");

        Utils::KeyboardRequest boxName;
        boxName.header = "Rename Box";
        boxName.guide = "Box name";
        boxName.initialText = "Ab";
        boxName.maxChars = 8;
        host.open(boxName);
        driver.step("type at the caret, not the end").frame().press(Btn::L).press(Btn::A);
        textIs("Aqb", "a key typed with the caret moved did not land at the caret");
        driver.press(Btn::Y);
        textIs("Aq b", "Y did not type a space at the caret");

        boxName.initialText = "abcdefgh";
        host.open(boxName);
        driver.step("a full field refuses a character").frame().press(Btn::A);
        textIs("abcdefgh", "a character was typed past the prompt's length cap");
        {
            const auto tallies = driver.frameData().textExact("8 / 8");
            driver.expect(!tallies.empty() && tallies.front()->color.red == UI::Colors::ShinyStar.red && tallies.front()->color.green == UI::Colors::ShinyStar.green, "the character count did not flag the refused character");
        }

        driver.step("ZL/ZR switch layouts").press(Btn::ZR);
        driver.expect(keyboard.page == UI::Dialogs::KeyboardPage::Symbols, "ZR did not move to the symbols");
        driver.expect(!driver.frameData().textExact("1").empty() && !driver.frameData().textExact("\u2642").empty(), "the symbols page did not draw its digits and gender signs");
        driver.press(Btn::ZR);
        driver.expect(keyboard.page == UI::Dialogs::KeyboardPage::Accents, "ZR did not move on to the accents");
        driver.expect(!driver.frameData().textExact("\u00e9").empty(), "the accents page did not draw its letters");
        driver.press(Btn::ZR);
        driver.expect(keyboard.page == UI::Dialogs::KeyboardPage::Letters, "ZR did not wrap back to the letters");
        driver.press(Btn::ZL);
        driver.expect(keyboard.page == UI::Dialogs::KeyboardPage::Accents, "ZL did not wrap back to the accents");

        boxName.initialText = "";
        host.open(boxName);
        driver.step("tap a key").frame();
        driver.see("Box name"); // the guide stands in for an empty field...
        tapKey(driver, keyboard, keyIndexFor(keyboard, KeyboardKeyAction::Type, "w"));
        textIs("w", "tapping a key did not type it");
        driver.notSee("Box name"); // ...and gives way to the first character
        const int okIndex = keyIndexFor(keyboard, KeyboardKeyAction::Accept);
        driver.step("tap OK").frame();
        tapKey(driver, keyboard, okIndex);
        driver.expect(!keyboard.finished, "a tapped OK closed the prompt before its press was drawn");
        driver.expect(keyboard.pressedKeyIndex == okIndex, "a tapped OK was not drawn held");
        driver.frame(UI::Dialogs::KeyboardState::TAP_PRESS_FRAMES);
        driver.expect(keyboard.finished && keyboard.accepted && keyboard.result().text == "w", "a tapped OK did not accept the text once its press had shown");

        boxName.initialText = "Hi";
        host.open(boxName);
        driver.step("the bar's + badge accepts").frame();
        const UIHost::Draw *barOk = nullptr;
        for (const UIHost::Draw *label : driver.frameData().textExact("OK"))
        {
            if (label->drawY > 720 - UI::NAV_BAR_HEIGHT)
            {
                barOk = label;
            }
        }
        if (driver.expect(barOk != nullptr, "the nav bar did not offer +: OK"))
        {
            driver.tap(barOk->centerX(), barOk->centerY());
            driver.expect(keyboard.finished && keyboard.accepted, "tapping +: OK in the bar did not accept");
        }

        boxName.initialText = "Hi";
        host.open(boxName);
        driver.step("minus cancels").frame().press(Btn::Minus);
        driver.expect(keyboard.finished && !keyboard.accepted, "- did not cancel the prompt");
        boxName.initialText = "Hi";
        host.open(boxName);
        driver.step("plus accepts").frame().press(Btn::Plus);
        driver.expect(keyboard.finished && keyboard.accepted && keyboard.result().text == "Hi", "+ did not accept");

        // A number opens with its old value SELECTED: the first digit replaces it.
        Utils::KeyboardRequest money;
        money.header = "Money";
        money.guide = "0 - 9999999";
        money.initialText = "1500";
        money.maxChars = 7;
        money.digitsOnly = true;
        host.open(money);
        driver.step("open a number prompt").frame();
        driver.see("1500");
        driver.see("0 - 9999999");
        driver.notSee("+/-");
        driver.notSee("Shift");
        driver.expect(keyboard.textSelected, "a number prompt did not open with its old value selected");
        driver.press(Btn::A);
        textIs("1", "the first digit did not replace the selected value");
        driver.press(Btn::Right).press(Btn::A);
        textIs("12", "a second digit was not appended");
        driver.press(Btn::Y);
        textIs("12", "Y typed into a number");

        money.allowNegative = true;
        money.initialText = "5";
        money.maxChars = 2;
        host.open(money);
        driver.step("the sign key").frame();
        driver.see("+/-");
        tapKey(driver, keyboard, keyIndexFor(keyboard, KeyboardKeyAction::ToggleSign));
        textIs("-5", "the sign key did not negate the value");
        tapKey(driver, keyboard, keyIndexFor(keyboard, KeyboardKeyAction::ToggleSign));
        textIs("5", "the sign key did not put the value back");

        // The hand-off: the console's keyboard is the only way to type Japanese, Chinese or Korean,
        // and whatever it returns is the answer -- there is no second OK to press here.
        Utils::KeyboardRequest name;
        name.header = "Nickname";
        name.guide = "Nickname";
        name.maxChars = 20;
        host.open(name);
        driver.step("hand off to the console's keyboard").frame();
        UIHost::clearPrompts();
        UIHost::queueText("\u30d4\u30ab\u30c1\u30e5\u30a6");
        tapKey(driver, keyboard, keyIndexFor(keyboard, KeyboardKeyAction::SystemKeyboard));
        driver.frame(UI::Dialogs::KeyboardState::TAP_PRESS_FRAMES);
        driver.expect(UIHost::raisedPromptCount() == 1, "the hand-off key did not open the console's keyboard", "raised=" + std::to_string(UIHost::raisedPromptCount()));
        driver.expect(keyboard.finished && keyboard.accepted && keyboard.text == "\u30d4\u30ab\u30c1\u30e5\u30a6", "text typed on the console's keyboard did not come back as the answer");

        name.initialText = "Hi";
        host.open(name);
        driver.step("a cancelled hand-off comes back").frame();
        UIHost::clearPrompts();
        UIHost::queueCancel();
        tapKey(driver, keyboard, keyIndexFor(keyboard, KeyboardKeyAction::SystemKeyboard));
        // B held from answering the console's keyboard is still down when it returns. It was
        // meant for that keyboard, and must not delete a character from this one.
        driver.hold(Btn::B, UI::Dialogs::KeyboardState::TAP_PRESS_FRAMES + 4);
        driver.expect(UIHost::raisedPromptCount() == 1, "the hand-off key did not open the console's keyboard");
        driver.expect(!keyboard.finished, "cancelling the console's keyboard also closed this one");
        textIs("Hi", "a button held through the console's keyboard reached this one as a press");
        tapKey(driver, keyboard, keyIndexFor(keyboard, KeyboardKeyAction::Type, "q"));
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
    void testInputFuzz(Test::Context &context, TrainerViewScreen &screen, int frames)
    {
        static const u64 buttonMasks[] = {Btn::A, Btn::B, Btn::Up, Btn::Down, Btn::Left, Btn::Right, Btn::X, Btn::Y, Btn::L, Btn::R};
        Driver driver(context, screen, "fuzz");
        driver.step("random input sweep");
        uint32_t seed = 0x5EED1234u;
        for (int frameIndex = 0; frameIndex < frames; ++frameIndex)
        {
            seed = seed * 1664525u + 1013904223u;
            const u64 buttonMask = buttonMasks[(seed >> 16) % (sizeof buttonMasks / sizeof buttonMasks[0])];
            // Answer any prompt the sweep happens to open, so it does not stall on a blocked applet.
            if (UIHost::pendingPromptCount() == 0)
            {
                UIHost::queueText("FUZZ");
            }
            driver.press(buttonMask);
        }
        UIHost::clearPrompts();
    }

    /// FAVORITE FOLDERS AND THE LAST FOLDER, through the screen's own keys. The dialog suite checks
    /// the state; this checks what only the screen decides: that ZL and ZR reach the browser at all,
    /// that the bar names them, and that each browser reopens in ITS OWN last folder -- the trade
    /// partner picker where the last save was picked, the import browser where the last bank was.
    /// Settings go to a scratch file, so nothing here lands in a real settings.cfg.
    void testBrowserFolders(Test::Context &context, TrainerViewScreen &screen)
    {
        const std::string root = "/tmp/pkse-uitest-folders";
        const std::string saveFolder = root + "/saves";
        const std::string importFolder = root + "/banks";
        const std::string settingsFile = root + "-settings.cfg";
        mkdir(root.c_str(), 0777);
        mkdir(saveFolder.c_str(), 0777);
        mkdir(importFolder.c_str(), 0777);
        const auto writeOneByte = [](const std::string &path)
        {
            if (FILE *handle = std::fopen(path.c_str(), "wb"))
            {
                std::fputc(0, handle);
                std::fclose(handle);
            }
        };
        writeOneByte(saveFolder + "/Pokemon Ruby.sav");
        writeOneByte(importFolder + "/notabank.bnk");
        std::remove(settingsFile.c_str());
        Utils::setSettingsFileOverride(settingsFile);
        Utils::loadSettings();

        Driver driver(context, screen, "browser-folders");
        const auto rowOf = [&](const std::string &name)
        {
            for (size_t row = 0; row < screen.fileBrowser.entries.size(); ++row)
            {
                if (screen.fileBrowser.entries[row].name == name)
                {
                    return static_cast<int>(row);
                }
            }
            return -1;
        };

        // The trade partner picker opens in the save browser's last folder.
        Utils::setLastFolder(Utils::LastFolderKind::SaveFiles, saveFolder);
        Utils::setLastFolder(Utils::LastFolderKind::ImportFiles, importFolder);
        driver.step("open the trade partner picker");
        screen.openTradePartnerBrowser();
        driver.frame();
        driver.expect(screen.fileBrowser.active && screen.fileBrowser.directory == saveFolder, "the trade partner picker did not reopen in the last save folder", "got=\"" + screen.fileBrowser.directory + "\"");
        driver.see("ZL");
        driver.see("Favorites");
        driver.see("Add Favorite");
        expectOneNavBar(driver, "trade partner file browser open");

        driver.step("ZR favorites the folder");
        driver.press(Btn::ZR).frame();
        driver.expect(Utils::isFavoriteFolder(saveFolder), "ZR in the file browser did not favorite the folder", "favorites=" + std::to_string(Utils::favoriteFolders().size()));
        driver.see("Unfavorite");
        driver.expect(driver.frameData().has("\xE2\x98\x85"), "a favorite folder has no star beside its path");

        // Somewhere else, then back through the list.
        driver.step("B up to the fixture root, then ZL");
        driver.press(Btn::B).frame();
        driver.expect(screen.fileBrowser.directory == root, "B did not go up out of the save folder", "got=\"" + screen.fileBrowser.directory + "\"");
        driver.see("Add Favorite");
        driver.press(Btn::ZL).frame();
        driver.expect(screen.fileBrowser.showFavorites, "ZL did not open the favorites list");
        driver.see("Favorite folders");
        driver.see("Remove Favorite");
        expectOneNavBar(driver, "favorite folders list open");
        driver.see(saveFolder);
        driver.step("A on the favorite");
        driver.press(Btn::A).frame();
        driver.expect(!screen.fileBrowser.showFavorites && screen.fileBrowser.directory == saveFolder, "A on a favorite did not open that folder", "got=\"" + screen.fileBrowser.directory + "\"");

        // Picking a file remembers the folder, even though no Pokemon is open to trade.
        driver.step("pick the save");
        Utils::setLastFolder(Utils::LastFolderKind::SaveFiles, root);
        const int saveRow = rowOf("Pokemon Ruby.sav");
        if (driver.expect(saveRow >= 0, "the fixture save is not listed"))
        {
            screen.fileBrowser.selectedIndex = saveRow;
            driver.press(Btn::A).frame();
            driver.expect(Utils::lastFolder(Utils::LastFolderKind::SaveFiles) == saveFolder, "picking a trade partner save did not remember its folder", "got=\"" + Utils::lastFolder(Utils::LastFolderKind::SaveFiles) + "\"");
            driver.expect(Utils::lastFolder(Utils::LastFolderKind::ImportFiles) == importFolder, "picking a trade partner save moved the import browser's last folder");
        }
        screen.fileBrowser.close();

        // The import browser keeps its own.
        driver.step("open the import browser");
        screen.openPKSMImportBrowser();
        driver.frame();
        driver.expect(screen.fileBrowser.active && screen.fileBrowser.directory == importFolder, "the import browser did not reopen in its own last folder", "got=\"" + screen.fileBrowser.directory + "\"");
        driver.press(Btn::ZL).frame();
        driver.expect(screen.fileBrowser.showFavorites && rowOf(saveFolder) == 0, "the import browser does not list the favorites the save browser added");
        driver.press(Btn::B).frame();
        driver.expect(!screen.fileBrowser.showFavorites && screen.fileBrowser.directory == importFolder, "B in the favorites list did not return to the import folder", "got=\"" + screen.fileBrowser.directory + "\"");
        Utils::setLastFolder(Utils::LastFolderKind::ImportFiles, root);
        const int bankRow = rowOf("notabank.bnk");
        if (driver.expect(bankRow >= 0, "the fixture bank is not listed"))
        {
            screen.fileBrowser.selectedIndex = bankRow;
            driver.step("pick the bank");
            driver.press(Btn::A).frame();
            driver.expect(Utils::lastFolder(Utils::LastFolderKind::ImportFiles) == importFolder, "picking a bank did not remember its folder", "got=\"" + Utils::lastFolder(Utils::LastFolderKind::ImportFiles) + "\"");
            driver.expect(Utils::lastFolder(Utils::LastFolderKind::SaveFiles) == saveFolder, "picking a bank moved the save browser's last folder");
        }
        screen.pksmImport.reset();
        screen.fileBrowser.close();

        std::remove((saveFolder + "/Pokemon Ruby.sav").c_str());
        std::remove((importFolder + "/notabank.bnk").c_str());
        rmdir(saveFolder.c_str());
        rmdir(importFolder.c_str());
        rmdir(root.c_str());
        std::remove(settingsFile.c_str());
        Utils::setSettingsFileOverride("");
        Utils::loadSettings();
    }

    /// The first unlocked box slot holding a Pokemon of the save's own format, or false.
    bool findBoxedPokemon(TrainerViewScreen &screen, Trainer::Trainer &trainer, int &foundBox, int &foundSlot)
    {
        for (int boxIndex = 0; boxIndex < static_cast<int>(trainer.getBoxCount()); ++boxIndex)
        {
            for (int slotIndex = 0; slotIndex < static_cast<int>(trainer.getSlotsPerBox()); ++slotIndex)
            {
                const auto &occupant = trainer.boxes[boxIndex][slotIndex];
                if (occupant && occupant->speciesID() != 0 && !occupant->isEgg() && !screen.storageSlotLocked(0, boxIndex, slotIndex))
                {
                    foundBox = boxIndex;
                    foundSlot = slotIndex;
                    return true;
                }
            }
        }
        return false;
    }

    /// Every edit of the details page ends the same way: stats recalculated when the edit affects
    /// them, the checksum refreshed, the GAME marked changed only when the Pokemon is the game's.
    /// The Gen 4-7 setters are plain writes, so an edit that skipped the commit left a record whose
    /// checksum no longer matched -- harmless in the save (the writers refresh), fatal in the bank,
    /// whose loader drops a record with a bad checksum.
    void testDetailsEditCommits(Test::Context &context, TrainerViewScreen &screen, Trainer::Trainer &trainer)
    {
        int boxIndex = -1;
        int boxSlot = -1;
        if (!findBoxedPokemon(screen, trainer, boxIndex, boxSlot) || !trainer.boxes[boxIndex][boxSlot]->hasNature())
        {
            context.note("no boxed Pokemon with a nature, so the details commit test was skipped", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }
        const std::unique_ptr<Pokemon::Pokemon> originalPokemon = trainer.boxes[boxIndex][boxSlot]->clone();
        const bool unsavedBefore = screen.hasUnsavedChanges;

        Driver driver(context, screen, "details-commit");
        driver.step("open the box view");
        gotoHome(driver, screen, 0);
        driver.press(Btn::A).frame();
        if (!driver.expect(screen.detailViewActive, "could not open the box view"))
        {
            return;
        }
        // Nature is the edit with the most to commit: it changes the stats as well as the record.
        auto changeNature = [&](const char *where) -> Pokemon::Pokemon *
        {
            constexpr int natureField = 7;
            screen.details.selectedField = natureField;
            driver.step(std::string("open the nature picker (") + where + ")").press(Btn::A).frame();
            if (!driver.expect(screen.pickerActive && screen.pickerKind == UI::Dialogs::PickerKind::Nature, "A on the Nature row did not open the nature picker", where))
            {
                return nullptr;
            }
            driver.step("pick the next nature").press(Btn::Down).press(Btn::A).frame();
            return screen.detailsTargetPokemon();
        };
        auto expectCommitted = [&](const Pokemon::Pokemon *edited, const char *where)
        {
            if (!driver.expect(edited != nullptr, "the details page lost its Pokemon after the edit", where))
            {
                return;
            }
            const std::string detail = std::string(where) + " species=" + std::to_string(edited->speciesID()) + " nature=" + std::to_string(edited->nature());
            driver.expect(edited->nature() != originalPokemon->nature(), "picking another nature did not change it", detail);
            driver.expect(edited->isStructurallyValid(), "a details edit left the record with a stale checksum (the bank drops such a record on its next load)", detail);
            auto recalculated = edited->clone();
            recalculated->recalculateStats();
            driver.expect(edited->statATK() == recalculated->statATK() && edited->statSPE() == recalculated->statSPE(), "a nature edit left the stats from before it", detail + " atk=" + std::to_string(edited->statATK()) + " want=" + std::to_string(recalculated->statATK()));
        };

        screen.selectedBoxIndex = boxIndex;
        screen.selectedItemIndex = boxSlot;
        driver.step("open the details page on a boxed Pokemon").press(Btn::A).frame(2);
        if (driver.expect(screen.details.active, "A on an occupied slot did not open the details page"))
        {
            expectCommitted(changeNature("save"), "save");
            driver.expect(screen.hasUnsavedChanges, "a details edit of a save Pokemon did not mark the save as changed");
            // A move: no stats to recalculate, which is exactly the edit that used to skip the
            // checksum (recalculating the stats refreshes it in several formats).
            constexpr int firstMoveField = 10;
            screen.details.selectedField = firstMoveField;
            const uint16_t moveBefore = screen.detailsTargetPokemon()->move(0);
            driver.step("open the first move's picker").press(Btn::A).frame();
            if (driver.expect(screen.pickerActive && screen.pickerKind == UI::Dialogs::PickerKind::Move, "A on the first move row did not open the move picker"))
            {
                driver.step("pick another move").press(Btn::Down).press(Btn::A).frame();
                const Pokemon::Pokemon *edited = screen.detailsTargetPokemon();
                const std::string detail = "move=" + std::to_string(edited ? edited->move(0) : 0) + " before=" + std::to_string(moveBefore);
                if (driver.expect(edited && edited->move(0) != moveBefore, "picking another move did not change it", detail))
                {
                    driver.expect(edited->isStructurallyValid(), "a move edit left the record with a stale checksum (the bank drops such a record on its next load)", detail);
                }
            }
        }
        screen.details.active = false;
        screen.pickerActive = false;
        trainer.boxes[boxIndex][boxSlot] = originalPokemon->clone();
        screen.hasUnsavedChanges = unsavedBefore;

        // The same edit on a BANK Pokemon: committed the same way, but the game is not changed by it.
        if (!screen.bank)
        {
            return;
        }
        int bankSlot = -1;
        for (int slotIndex = 0; slotIndex < static_cast<int>(Trainer::Bank::BANK_SLOTS_PER_BOX) && bankSlot < 0; ++slotIndex)
        {
            if (!screen.bank->boxes[0][slotIndex])
            {
                bankSlot = slotIndex;
            }
        }
        if (bankSlot < 0)
        {
            return;
        }
        screen.bank->boxes[0][bankSlot] = originalPokemon->clone();
        screen.openStorageEditor(1, 0, bankSlot);
        driver.frame(2);
        if (driver.expect(screen.details.active, "the storage editor did not open on a bank Pokemon"))
        {
            expectCommitted(changeNature("bank"), "bank");
            driver.expect(screen.hasUnsavedChanges == unsavedBefore, "editing a BANK Pokemon marked the game save as changed");
        }
        screen.details.active = false;
        screen.pickerActive = false;
        screen.bank->boxes[0][bankSlot].reset();
        screen.hasUnsavedChanges = unsavedBefore;
    }

    /// Put Back (B with a Pokemon in hand) sends every carried cell home, and nothing is ever lost
    /// or duplicated on the way. Three cases, each a way the old Put Back lost or made a Pokemon:
    /// a COPY carried back (it used to land as a second Pokemon), a foreign-format cell (it used to
    /// land in the save unconverted), and a cell with nowhere to go (it used to be deleted).
    void testPutBackKeepsEveryPokemon(Test::Context &context, TrainerViewScreen &screen, Trainer::Trainer &trainer)
    {
        int boxIndex = -1;
        int boxSlot = -1;
        if (!screen.bank || !findBoxedPokemon(screen, trainer, boxIndex, boxSlot))
        {
            context.note("no bank or no boxed Pokemon, so the put-back test was skipped", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }
        using Screen = TrainerViewScreen;
        auto countSave = [&]()
        {
            int savePokemonCount = 0;
            for (const auto &box : trainer.boxes)
                for (const auto &pokemon : box)
                    savePokemonCount += (pokemon && pokemon->speciesID() != 0) ? 1 : 0;
            return savePokemonCount;
        };
        auto countBank = [&]()
        {
            int bankPokemonCount = 0;
            for (const auto &box : screen.bank->boxes)
                for (const auto &pokemon : box)
                    bankPokemonCount += pokemon ? 1 : 0;
            return bankPokemonCount;
        };
        const bool unsavedBefore = screen.hasUnsavedChanges;
        const int saveBefore = countSave();
        const int bankBefore = countBank();

        Driver driver(context, screen, "put-back");
        driver.step("open the storage view");
        gotoHome(driver, screen, 2);
        driver.press(Btn::A).frame();
        if (!driver.expect(screen.detailViewActive && screen.selectedMode == Screen::ViewMode::Storage, "could not open the storage view"))
        {
            return;
        }

        // A copy (Multi mode's Y) of one slot, then B: the copy is dropped, the original stays.
        screen.storageFocusPane = 0;
        screen.selectedBoxIndex = boxIndex;
        screen.stSaveSlot = boxSlot;
        screen.currentlySelecting = true;
        screen.selectPane = 0;
        screen.selectBox = boxIndex;
        const int saveColumns = screen.paneCols(0);
        screen.selectDimensions = {boxSlot % saveColumns, boxSlot / saveColumns};
        screen.grabSelection(false);
        if (driver.expect(screen.carriedCount() == 1, "copying one occupied slot did not put one Pokemon in hand"))
        {
            driver.step("B puts the copy back").press(Btn::B).frame();
            driver.expect(!screen.carrying(), "B did not empty the hands after a copy");
            driver.expect(countSave() == saveBefore && countBank() == bankBefore, "putting a COPY back made a second Pokemon", "save=" + std::to_string(countSave()) + " want=" + std::to_string(saveBefore) + " bank=" + std::to_string(countBank()) + " want=" + std::to_string(bankBefore));
        }

        // A foreign-format Pokemon whose recorded home is a save slot: Put Back converts nothing, so
        // it must go to the bank rather than into the save as a record the game cannot read.
        const Enums::GameVersion foreignGroup = trainer.getGameGroup() == Enums::GameVersion::USUM ? Enums::GameVersion::SM : Enums::GameVersion::USUM;
        auto foreignPokemon = std::make_unique<Pokemon::Pokemon7USUM>();
        foreignPokemon->setSpecies(25);
        foreignPokemon->setExp(1000);
        foreignPokemon->recalculateStats();
        foreignPokemon->refreshChecksum();
        std::unique_ptr<Pokemon::Pokemon> carried = std::move(foreignPokemon);
        if (trainer.getGameGroup() == Enums::GameVersion::USUM)
        {
            Conversion::Result conversionResult = Conversion::Result::Ok;
            carried = Conversion::convert(*carried, foreignGroup, conversionResult, 0);
        }
        if (carried)
        {
            const std::unique_ptr<Pokemon::Pokemon> savedSlot = trainer.boxes[boxIndex][boxSlot] ? trainer.boxes[boxIndex][boxSlot]->clone() : nullptr;
            trainer.boxes[boxIndex][boxSlot].reset(); // its "home" is free, so only the format check can refuse it
            screen.clearCarried();
            screen.moveMon.push_back(std::move(carried));
            screen.moveMonOrigins.push_back(Screen::CarriedCellOrigin{0, boxIndex, boxSlot, false});
            screen.selectDimensions = {1, 1};
            screen.heldPane = 0;
            screen.heldFromBox = boxIndex;
            screen.heldFromSlot = boxSlot;
            const bool returned = screen.returnHeldToOrigin();
            driver.expect(returned && !screen.carrying(), "a foreign-format Pokemon could not be put back anywhere");
            const auto &home = trainer.boxes[boxIndex][boxSlot];
            driver.expect(!home || home->speciesID() == 0, "Put Back placed a foreign-format Pokemon in the save without converting it", "group=" + std::to_string(static_cast<int>(home ? home->getGameGroup() : Enums::GameVersion::Invalid)));
            driver.expect(countBank() == bankBefore + 1, "a foreign-format Pokemon that could not go home did not go to the bank", "bank=" + std::to_string(countBank()) + " want=" + std::to_string(bankBefore + 1));
            // Undo: take it back out of the bank.
            for (auto &box : screen.bank->boxes)
                for (auto &pokemon : box)
                    if (pokemon && pokemon->getGameGroup() == foreignGroup && pokemon->speciesID() == 25)
                        pokemon.reset();
            trainer.boxes[boxIndex][boxSlot] = savedSlot ? savedSlot->clone() : nullptr;
        }

        // A cell with nowhere to go (no bank, home taken by another format) stays in hand.
        {
            auto strandedPokemon = std::make_unique<Pokemon::Pokemon7USUM>();
            strandedPokemon->setSpecies(25);
            strandedPokemon->refreshChecksum();
            std::unique_ptr<Trainer::Bank> bankAside = std::move(screen.bank);
            screen.clearCarried();
            screen.moveMon.push_back(std::move(strandedPokemon));
            const bool saveIsUsum = trainer.getGameGroup() == Enums::GameVersion::USUM;
            screen.moveMonOrigins.push_back(Screen::CarriedCellOrigin{saveIsUsum ? 1 : 0, 0, 0, false});
            screen.selectDimensions = {1, 1};
            const bool returned = screen.returnHeldToOrigin();
            driver.expect(!returned && screen.carriedCount() == 1, "Put Back deleted a Pokemon that had nowhere to go instead of keeping it in hand");
            driver.expect(countSave() == saveBefore, "Put Back put a Pokemon somewhere it does not belong", "save=" + std::to_string(countSave()) + " want=" + std::to_string(saveBefore));
            screen.clearCarried();
            screen.bank = std::move(bankAside);
        }
        screen.hasUnsavedChanges = unsavedBefore;
    }

    /// THE BANK AND THE GAME SAVE ARE SEPARATE. The bank prompt's Discard reloads the bank ONLY and
    /// never touches the save's boxes (a withdrawn Pokemon stays in the save until the game save's
    /// own Save/Discard decides it); the game save never writes the bank; the bank prompt's Save &
    /// Exit writes the bank and never the game.
    void testBankSavedSeparately(Test::Context &context, TrainerViewScreen &screen, Trainer::Trainer &trainer)
    {
        using Screen = TrainerViewScreen;
        int boxIndex = -1;
        int boxSlot = -1;
        if (!screen.bank || !findBoxedPokemon(screen, trainer, boxIndex, boxSlot))
        {
            context.note("no bank or no boxed Pokemon, so the bank-separation test was skipped", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }
        const int bankBox = static_cast<int>(screen.bank->currentBox);
        int bankSlot = -1;
        for (int slotIndex = 0; slotIndex < static_cast<int>(Trainer::Bank::BANK_SLOTS_PER_BOX) && bankSlot < 0; ++slotIndex)
        {
            if (!screen.bank->boxes[bankBox][slotIndex])
            {
                bankSlot = slotIndex;
            }
        }
        if (bankSlot < 0 || screen.bank->hasChanged())
        {
            context.note("the open bank box is full or already changed, so the bank-separation test was skipped", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
            return;
        }
        // The bank file is put back afterwards: the bank prompt's Save & Exit writes it.
        const std::string bankFile = "/tmp/pkse-uitest-bank/bank.dat";
        std::vector<char> bankFileBefore;
        bool bankFileExisted = false;
        if (FILE *bankStream = std::fopen(bankFile.c_str(), "rb"))
        {
            bankFileExisted = true;
            char buffer[4096];
            size_t readCount = 0;
            while ((readCount = std::fread(buffer, 1, sizeof(buffer), bankStream)) > 0)
                bankFileBefore.insert(bankFileBefore.end(), buffer, buffer + readCount);
            std::fclose(bankStream);
        }
        const std::span<const std::byte> originalSpan = trainer.boxes[boxIndex][boxSlot]->getData();
        const std::vector<std::byte> originalBytes(originalSpan.begin(), originalSpan.end());
        const bool unsavedBefore = screen.hasUnsavedChanges;
        // Let's Go compacts its storage every frame, so a Pokemon is found by its bytes, not by the
        // slot it was in, and every box is put back afterwards.
        std::vector<std::vector<std::unique_ptr<Pokemon::Pokemon>>> boxesBefore(trainer.boxes.size());
        for (size_t snapshotBox = 0; snapshotBox < trainer.boxes.size(); ++snapshotBox)
            for (const auto &pokemon : trainer.boxes[snapshotBox])
                boxesBefore[snapshotBox].push_back(pokemon ? pokemon->clone() : nullptr);
        const auto restoreBoxes = [&]()
        {
            for (size_t snapshotBox = 0; snapshotBox < trainer.boxes.size() && snapshotBox < boxesBefore.size(); ++snapshotBox)
                for (size_t snapshotSlot = 0; snapshotSlot < trainer.boxes[snapshotBox].size() && snapshotSlot < boxesBefore[snapshotBox].size(); ++snapshotSlot)
                    trainer.boxes[snapshotBox][snapshotSlot] = boxesBefore[snapshotBox][snapshotSlot] ? boxesBefore[snapshotBox][snapshotSlot]->clone() : nullptr;
        };
        const auto countSaveBoxed = [&]() -> int
        {
            int boxedCount = 0;
            for (const auto &box : trainer.boxes)
                for (const auto &pokemon : box)
                    if (pokemon && pokemon->speciesID() != 0)
                        ++boxedCount;
            return boxedCount;
        };
        // A save can hold byte-identical duplicates, so the test Pokemon's copies are counted.
        const auto countOriginalCopies = [&]() -> int
        {
            int copyCount = 0;
            for (const auto &box : trainer.boxes)
                for (const auto &pokemon : box)
                    if (pokemon && pokemon->getData().size() == originalBytes.size() && std::memcmp(pokemon->getData().data(), originalBytes.data(), originalBytes.size()) == 0)
                        ++copyCount;
            return copyCount;
        };
        const int boxedBefore = countSaveBoxed();
        const int copiesBefore = countOriginalCopies();

        Driver driver(context, screen, "bank-saved-separately");
        driver.step("open the storage view");
        gotoHome(driver, screen, 2);
        driver.press(Btn::A).frame();
        if (!driver.expect(screen.detailViewActive && screen.selectedMode == Screen::ViewMode::Storage, "could not open the storage view"))
        {
            return;
        }
        auto deposit = [&](const char *which) -> bool
        {
            screen.storageFocusPane = 0;
            screen.selectedBoxIndex = boxIndex;
            screen.stSaveSlot = boxSlot;
            driver.step(std::string("pick up a save Pokemon (") + which + ")").press(Btn::A).frame();
            if (screen.storageMenuActive)
            {
                screen.storageMenuIndex = Screen::STORAGE_MENU_ROW_MOVE;
                driver.press(Btn::A).frame();
            }
            if (!driver.expect(screen.carrying(), "could not pick up a save Pokemon", which))
            {
                return false;
            }
            screen.storageFocusPane = 1;
            screen.stBankBox = bankBox;
            screen.stBankSlot = bankSlot;
            driver.step("put it down in the bank").press(Btn::A).frame();
            if (screen.moveConfirmActive())
            {
                driver.press(Btn::A).frame();
            }
            return driver.expect(!screen.carrying() && screen.bank->boxes[bankBox][bankSlot] && countSaveBoxed() == boxedBefore - 1, "a deposit did not move the Pokemon from the save to the bank", which);
        };

        if (deposit("then Discard"))
        {
            driver.step("B asks about the bank").press(Btn::B).frame();
            if (driver.expect(screen.storageExitConfirmActive, "leaving Storage after a deposit did not ask about the bank"))
            {
                expectOneNavBar(driver, "bank exit prompt open");
                driver.expect(screen.overlayNavHint().find("A: Confirm") != std::string::npos, "the bank exit prompt's keys are not on the nav bar", screen.overlayNavHint());
                // The bank and the game save are separate: after a deposit the bank's prompt saves
                // the bank (Save & Exit), never "keep it for the game save".
                driver.expect(std::string(screen.storageExitConfirmLabel(Screen::STORAGE_EXIT_ROW_SAVE)) == "Save & Exit", "after a deposit the bank prompt no longer offers Save & Exit", screen.storageExitConfirmLabel(Screen::STORAGE_EXIT_ROW_SAVE));
                screen.storageExitConfirmIndex = Screen::STORAGE_EXIT_ROW_DISCARD;
                driver.step("Discard").press(Btn::A).frame();
                // Discard is the BANK's: the bank goes back to its file, and the save's half of the
                // move (the emptied slot) is left for the game save's own Save/Discard.
                driver.expect(!screen.bank->boxes[bankBox][bankSlot] && !screen.bank->hasChanged(), "bank Discard after a deposit did not put the bank back to its saved state");
                driver.expect(countSaveBoxed() == boxedBefore - 1 && countOriginalCopies() == copiesBefore - 1, "bank Discard put the deposited Pokemon back in the save -- Discard must touch the bank only", "boxed=" + std::to_string(countSaveBoxed()) + " want=" + std::to_string(boxedBefore - 1));
                driver.expect(screen.hasUnsavedChanges, "bank Discard cleared the game save's dirty flag -- the emptied save slot is still an unsaved game change");
            }
        }
        restoreBoxes();
        screen.hasUnsavedChanges = unsavedBefore;

        // THE BANK AND THE GAME SAVE ARE SAVED SEPARATELY. After a
        // deposit, the game save writes the game and NOT the bank; leaving the bank then asks about
        // the bank, and its Save & Exit writes the bank and NOT the game.
        const auto readBankFile = [&]() -> std::vector<uint8_t>
        {
            std::vector<uint8_t> bankBytes;
            if (FILE *bankStream = std::fopen(bankFile.c_str(), "rb"))
            {
                uint8_t chunk[4096];
                size_t readCount = 0;
                while ((readCount = std::fread(chunk, 1, sizeof(chunk), bankStream)) > 0)
                    bankBytes.insert(bankBytes.end(), chunk, chunk + readCount);
                std::fclose(bankStream);
            }
            return bankBytes;
        };
        screen.detailViewActive = true;
        screen.selectedMode = Screen::ViewMode::Storage;
        if (deposit("then the game save"))
        {
            const std::vector<uint8_t> bankFileBeforeGameSave = readBankFile();
            UIHost::setSaveResult(true);
            screen.performSave("/tmp/pkse-uitest/journal-save", false);
            driver.expect(screen.bank->hasChanged() && screen.bank->boxes[bankBox][bankSlot], "the game save wrote (or dropped) the bank's changes -- the bank is saved only from its own prompt");
            driver.expect(readBankFile() == bankFileBeforeGameSave, "the game save wrote the bank file -- the bank and the game save are separate");

            screen.detailViewActive = true;
            screen.selectedMode = Screen::ViewMode::Storage;
            driver.step("B asks about the bank").press(Btn::B).frame();
            if (driver.expect(screen.storageExitConfirmActive, "leaving Storage with bank changes did not ask about the bank"))
            {
                driver.expect(std::string(screen.storageExitConfirmLabel(Screen::STORAGE_EXIT_ROW_SAVE)) == "Save & Exit", "the bank prompt does not offer Save & Exit", screen.storageExitConfirmLabel(Screen::STORAGE_EXIT_ROW_SAVE));
                const int gameSavesBefore = UIHost::saveCallCount();
                const bool gameDirtyBefore = screen.hasUnsavedChanges;
                screen.storageExitConfirmIndex = Screen::STORAGE_EXIT_ROW_SAVE;
                driver.step("Save & Exit").press(Btn::A).frame();
                driver.expect(!screen.bank->hasChanged() && readBankFile() != bankFileBeforeGameSave, "the bank prompt's Save & Exit did not write the bank");
                driver.expect(UIHost::saveCallCount() == gameSavesBefore && screen.hasUnsavedChanges == gameDirtyBefore, "the bank prompt's Save & Exit touched the game save -- the bank and the game save are separate");
            }

            // The Pokemon is now saved in the bank. Withdraw it to the save, then Discard the bank:
            // the save keeps the withdrawn Pokemon (the game save decides it) and the bank gets
            // its saved copy back.
            screen.detailViewActive = true;
            screen.selectedMode = Screen::ViewMode::Storage;
            screen.storageFocusPane = 1;
            screen.stBankBox = bankBox;
            screen.stBankSlot = bankSlot;
            driver.step("pick up the banked Pokemon").press(Btn::A).frame();
            if (screen.storageMenuActive)
            {
                screen.storageMenuIndex = Screen::STORAGE_MENU_ROW_MOVE;
                driver.press(Btn::A).frame();
            }
            int freeBox = -1;
            int freeSlot = -1;
            // Searched through the pane's own geometry: Let's Go's storage is not one vector per box.
            for (int searchBox = 0; searchBox < screen.paneBoxes(0) && freeBox < 0; ++searchBox)
                for (int searchSlot = 0; searchSlot < screen.paneSlots(0) && freeBox < 0; ++searchSlot)
                {
                    const auto &candidate = screen.storageSlot(0, searchBox, searchSlot);
                    if ((!candidate || candidate->speciesID() == 0) && !screen.storageSlotLocked(0, searchBox, searchSlot))
                    {
                        freeBox = searchBox;
                        freeSlot = searchSlot;
                    }
                }
            if (driver.expect(screen.carrying() && freeBox >= 0, "could not pick up the banked Pokemon, or the save has no free slot"))
            {
                screen.storageFocusPane = 0;
                screen.selectedBoxIndex = freeBox;
                screen.stSaveSlot = freeSlot;
                driver.step("put it down in the save").press(Btn::A).frame();
                if (screen.moveConfirmActive())
                {
                    driver.press(Btn::A).frame();
                }
                driver.expect(!screen.carrying() && countOriginalCopies() == copiesBefore && !screen.bank->boxes[bankBox][bankSlot], "a withdrawal did not move the Pokemon from the bank to the save", "box=" + std::to_string(freeBox) + " slot=" + std::to_string(freeSlot) + " carrying=" + std::to_string(screen.carrying()) + " copies=" + std::to_string(countOriginalCopies()) + " want=" + std::to_string(copiesBefore));
                driver.step("B asks about the bank").press(Btn::B).frame();
                if (driver.expect(screen.storageExitConfirmActive, "leaving Storage after a withdrawal did not ask about the bank"))
                {
                    screen.storageExitConfirmIndex = Screen::STORAGE_EXIT_ROW_DISCARD;
                    driver.step("Discard").press(Btn::A).frame();
                    driver.expect(countOriginalCopies() == copiesBefore && countSaveBoxed() == boxedBefore, "bank Discard undid a withdrawal from the save -- Discard must touch the bank only", "boxed=" + std::to_string(countSaveBoxed()) + " want=" + std::to_string(boxedBefore));
                    driver.expect(screen.bank->boxes[bankBox][bankSlot] && !screen.bank->hasChanged(), "bank Discard after a withdrawal did not put the bank back to its saved state");
                    driver.expect(screen.hasUnsavedChanges, "bank Discard cleared the game save's dirty flag after a withdrawal");
                }
            }
        }

        // Put everything back, the bank file included.
        screen.bank->boxes[bankBox][bankSlot].reset();
        restoreBoxes();
        screen.hasUnsavedChanges = unsavedBefore;
        if (bankFileExisted)
        {
            if (FILE *bankStream = std::fopen(bankFile.c_str(), "wb"))
            {
                std::fwrite(bankFileBefore.data(), 1, bankFileBefore.size(), bankStream);
                std::fclose(bankStream);
            }
        }
        else
        {
            std::remove(bankFile.c_str());
        }
        screen.bank->load();
    }

    /// + waits for whatever owns the next button press, and the bars name the right keys.
    void testPlusWaitsAndExitHints(Test::Context &context, TrainerViewScreen &screen, Trainer::Trainer &trainer)
    {
        using Screen = TrainerViewScreen;
        int boxIndex = -1;
        int boxSlot = -1;
        Driver driver(context, screen, "plus-waits");
        if (screen.bank && findBoxedPokemon(screen, trainer, boxIndex, boxSlot))
        {
            driver.step("open the storage view");
            gotoHome(driver, screen, 2);
            driver.press(Btn::A).frame();
            if (driver.expect(screen.detailViewActive && screen.selectedMode == Screen::ViewMode::Storage, "could not open the storage view"))
            {
                screen.storageFocusPane = 0;
                screen.selectedBoxIndex = boxIndex;
                screen.stSaveSlot = boxSlot;
                screen.storageMenuActive = true;
                screen.menuPane = 0;
                screen.menuBox = boxIndex;
                screen.menuSlot = boxSlot;
                driver.step("+ with the Pokemon menu open").press(Btn::Plus).frame();
                driver.expect(!screen.hasRequestedExit() && !screen.saveConfirmActive, "+ left the app (or raised the save prompt) over an open menu");
                driver.expect(screen.storageMenuActive, "+ closed the open menu instead of waiting for it");
                screen.storageMenuActive = false;
            }
        }
        // The save dialog raised by leaving: its A discards, and the bar has to say so.
        screen.saveConfirmActive = true;
        screen.exitingWithUnsavedChanges = true;
        driver.step("the save dialog raised by leaving").frame();
        driver.expect(screen.overlayNavHint().find("A: Discard & Exit") != std::string::npos, "the leaving save dialog's bar does not say that A discards", screen.overlayNavHint());
        expectOneNavBar(driver, "leaving save dialog open");
        screen.saveConfirmActive = false;
        screen.exitingWithUnsavedChanges = false;
    }

    /// Items and EVs are offered in the save's own terms: a Gen 2 held item comes from the Gen 2
    /// table, and the EV ceiling is the generation's (255 per stat before Gen 6, 252 from it).
    void testGenerationLimits(Test::Context &context, TrainerViewScreen &screen, Trainer::Trainer &trainer)
    {
        int boxIndex = -1;
        int boxSlot = -1;
        // A save whose boxes are empty lends its first party member to an empty box for the test.
        std::unique_ptr<Pokemon::Pokemon> lentSlotBefore;
        bool lent = false;
        if (!findBoxedPokemon(screen, trainer, boxIndex, boxSlot))
        {
            if (trainer.party.empty() || Test::isEmptySlot(trainer.party[0].get()) || trainer.boxes.empty() || trainer.boxes[0].empty())
            {
                return;
            }
            boxIndex = 0;
            boxSlot = 0;
            lentSlotBefore = std::move(trainer.boxes[0][0]);
            trainer.boxes[0][0] = trainer.party[0]->clone();
            lent = true;
        }
        struct LentSlotRestorer
        {
            Trainer::Trainer &trainer;
            std::unique_ptr<Pokemon::Pokemon> &slotBefore;
            bool active;
            ~LentSlotRestorer()
            {
                if (active)
                    trainer.boxes[0][0] = std::move(slotBefore);
            }
        } lentSlotRestorer{trainer, lentSlotBefore, lent};
        Pokemon::Pokemon &pokemon = *trainer.boxes[boxIndex][boxSlot];
        const Enums::GameVersion group = pokemon.getGameGroup();
        const int generation = Enums::getVersionGeneration(Enums::getGroupRepVersion(group));
        const int expectedCap = generation <= 2 ? pokemon.maxEV() : generation <= 5 ? 255 : 252;
        // Per-item stack ceilings PKHeX applies inside a mixed pouch (PlayerBag7b, PlayerBag9a).
        if (group == Enums::GameVersion::GG)
        {
            constexpr uint16_t shinyCharmItemId = 632;
            const int shinyCharmCap = screen.itemMaxCount(static_cast<int>(Trainer::PouchType7LGPE::KeyItems), shinyCharmItemId);
            context.check(shinyCharmCap == 1, "a Let's Go key item in the mixed Items pouch can be stacked", "cap=" + std::to_string(shinyCharmCap));
        }
        if (group == Enums::GameVersion::ZA)
        {
            constexpr uint16_t cherishedRingItemId = 2612;
            const int cherishedRingCap = screen.itemMaxCount(static_cast<int>(Trainer::PouchType9LZA::KeyItems), cherishedRingItemId);
            context.check(cherishedRingCap == 0, "the Cherished Ring, which the player never holds, can be given", "cap=" + std::to_string(cherishedRingCap));
        }
        // Gender comes from the FORM's entry in this game's table: Cosplay Pikachu (Omega Ruby/Alpha
        // Sapphire form 1) is always female, though Pikachu itself is not.
        screen.buildGenderPickerOrder(25, 1, Enums::GameVersion::ORAS, 1);
        context.check(screen.pickerOrder == std::vector<int>{1}, "the gender picker for Cosplay Pikachu offers more than female", "rows=" + std::to_string(screen.pickerOrder.size()));
        screen.pickerOrder.clear();
        context.check(UI::Dialogs::legalEffortValueCap(pokemon) == expectedCap, "the EV editor's legal ceiling is not this generation's", "generation=" + std::to_string(generation) + " cap=" + std::to_string(UI::Dialogs::legalEffortValueCap(pokemon)) + " want=" + std::to_string(expectedCap));

        Driver driver(context, screen, "held-item-table");
        driver.step("open the box view");
        gotoHome(driver, screen, 0);
        driver.press(Btn::A).frame();
        if (!driver.expect(screen.detailViewActive, "could not open the box view"))
        {
            return;
        }
        screen.selectedBoxIndex = boxIndex;
        screen.selectedItemIndex = boxSlot;
        driver.step("open the details page").press(Btn::A).frame(2);
        if (!driver.expect(screen.details.active, "A on an occupied slot did not open the details page"))
        {
            return;
        }
        constexpr int heldItemField = 14;
        if (generation <= 1)
        {
            screen.details.active = false;
            return; // Gen 1 holds no item: the byte is the catch rate
        }
        screen.details.selectedField = heldItemField;
        driver.step("open the held-item picker").press(Btn::A).frame();
        if (driver.expect(screen.pickerActive, "A on the held-item row opened no picker"))
        {
            const int optionCount = UI::Dialogs::pickerOptionCount(screen.pickerKind);
            int mismatches = 0;
            std::string firstMismatch;
            for (int itemId = 1; itemId < optionCount && itemId < 300; ++itemId)
            {
                const char *offered = UI::Dialogs::pickerOptionLabel(screen.pickerKind, itemId);
                const char *gameName = Names::getItemNameFor(group, static_cast<uint16_t>(itemId));
                if (std::strcmp(offered ? offered : "", gameName ? gameName : "") != 0)
                {
                    if (mismatches++ == 0)
                        firstMismatch = "id=" + std::to_string(itemId) + " offered=\"" + (offered ? offered : "") + "\" game=\"" + (gameName ? gameName : "") + "\"";
                }
            }
            driver.expect(mismatches == 0, "the held-item picker names items from another generation's table", firstMismatch + " mismatches=" + std::to_string(mismatches));
        }
        screen.pickerActive = false;
        screen.details.active = false;
    }

    /// A new backup goes beside the session's own folder: in the title's backup folder, which the
    /// backup list reads. It used to be built from the display name, which carries no title id and
    /// for FireRed/LeafGreen holds a '/', so the folder landed where no list would show it.
    void testNamedBackupFolder(Test::Context &context, Trainer::Trainer &trainer)
    {
        const std::string titleDirectory = "/tmp/pkse-uitest-backups/0100000000000000";
        const std::string snapshotDirectory = titleDirectory + "/Snapshot 2026-01-01";
        mkdir("/tmp/pkse-uitest-backups", 0777);
        mkdir(titleDirectory.c_str(), 0777);
        mkdir(snapshotDirectory.c_str(), 0777);
        if (FILE *seedFile = std::fopen((snapshotDirectory + "/main").c_str(), "wb"))
        {
            std::fputs("seed", seedFile);
            std::fclose(seedFile);
        }
        TrainerViewScreen screen(trainer, "FireRed/LeafGreen", snapshotDirectory, 0, AccountUid{}, true);
        context.check(!TrainerViewScreen::backupNameIsUsable("/../"), "a backup name with no usable characters was accepted");
        const std::string createdDirectory = screen.createNamedBackupDir("Snapshot 2026-01-01 Edited");
        context.check(createdDirectory == titleDirectory + "/Snapshot 2026-01-01 Edited", "a new backup was not made beside the session's own folder", "created=\"" + createdDirectory + "\"");
        struct stat seedStatus{};
        context.check(stat((createdDirectory + "/main").c_str(), &seedStatus) == 0, "a new backup was not seeded with a copy of the session's folder", createdDirectory);
        context.check(stat((snapshotDirectory + "/main").c_str(), &seedStatus) == 0, "making a new backup disturbed the snapshot it was copied from", snapshotDirectory);
        std::remove((createdDirectory + "/main").c_str());
        rmdir(createdDirectory.c_str());
        std::remove((snapshotDirectory + "/main").c_str());
        rmdir(snapshotDirectory.c_str());
        rmdir(titleDirectory.c_str());
        rmdir("/tmp/pkse-uitest-backups");
    }

    /// Every Pokemon in the save's boxes.
    int countBoxedPokemon(const Trainer::Trainer &trainer)
    {
        int boxedPokemonCount = 0;
        for (const auto &box : trainer.boxes)
            for (const auto &pokemon : box)
                boxedPokemonCount += (pokemon && pokemon->speciesID() != 0) ? 1 : 0;
        return boxedPokemonCount;
    }

    /**
     * A counted Gen 1/2 box entry PKSE cannot read (MissingNo. here) is kept and written back at its
     * slot, so the Storage view must treat that slot as taken: shown as a "?" placeholder, never
     * grabbed, dropped onto, sorted, filled by Clone or Put Back, or opened. It used to read as an
     * empty slot, so a Pokemon dropped there overfilled the list and the last entry was cut on save.
     * The box is filled around the entry first, so every "find a free slot" path meets a full box.
     */
    void testUnmappedBoxSlotStaysPut(Test::Report &report, const std::string &target, const std::vector<uint8_t> &saveBytes)
    {
        Test::Context context;
        context.report = &report;
        context.suite = "ui";
        context.target = target;
        context.group = Enums::GameVersion::RBY;
        report.setScope(target, "ui");
        Trainer::Bank::setRootOverride("/tmp/pkse-uitest-bank");

        // Plant MissingNo. (internal 0x1F) in the live copy of the current box: a new last entry, or
        // over the last one in a full box.
        Trainer::Trainer1RBY probeTrainer(saveBytes, "Pokemon Red.dat");
        const size_t liveBoxOffset = probeTrainer.currentBoxFlush().liveBoxOffset;
        const size_t boxCapacity = probeTrainer.getSlotsPerBox();
        const int boxIndex = probeTrainer.getCurrentBox();
        const size_t storedCount = saveBytes[liveBoxOffset];
        if (!context.check(storedCount <= boxCapacity && !probeTrainer.party.empty(), "the Red save has no party Pokemon or a malformed current box, so the unmapped-slot journey cannot run"))
        {
            return;
        }
        const int glitchSlot = static_cast<int>(std::min(storedCount, boxCapacity - 1));
        std::vector<uint8_t> glitchedBytes = saveBytes;
        glitchedBytes[liveBoxOffset] = static_cast<uint8_t>(glitchSlot + 1);
        glitchedBytes[liveBoxOffset + 1 + glitchSlot] = 0x1F;
        glitchedBytes[liveBoxOffset + 1 + glitchSlot + 1] = 0xFF;
        glitchedBytes[liveBoxOffset + 1 + boxCapacity + 1 + glitchSlot * Pokemon::SIZE_1STORED] = 0x1F;
        Trainer::Trainer1RBY trainer(glitchedBytes, "Pokemon Red.dat");
        context.trainer = &trainer;
        context.version = trainer.getGameVersion();
        auto glitchKept = [&]() { return !trainer.boxes[boxIndex][glitchSlot] && trainer.isBoxSlotUnmapped(static_cast<size_t>(boxIndex), static_cast<size_t>(glitchSlot)); };
        if (!context.check(glitchKept(), "the planted MissingNo. did not load as a kept unmapped box entry"))
        {
            return;
        }
        // Fill the box around it: capacity - 1 Pokemon plus the kept entry is a full box.
        const std::unique_ptr<Pokemon::Pokemon> donor = trainer.party[0]->clone();
        int otherSlot = -1;
        for (int slotIndex = 0; slotIndex < static_cast<int>(boxCapacity); ++slotIndex)
        {
            if (slotIndex == glitchSlot)
                continue;
            if (!trainer.boxes[boxIndex][slotIndex])
                trainer.boxes[boxIndex][slotIndex] = donor->clone();
            if (otherSlot < 0)
                otherSlot = slotIndex;
        }
        int expectedBoxedCount = countBoxedPokemon(trainer);

        using Screen = TrainerViewScreen;
        TrainerViewScreen screen(trainer, target, std::string("/tmp/pkse-uitest"), 0, AccountUid{}, false);
        Driver driver(context, screen, "unmapped-box-slot");
        driver.step("open the storage view");
        gotoHome(driver, screen, 2);
        driver.press(Btn::A).frame();
        if (!driver.expect(screen.detailViewActive && screen.selectedMode == Screen::ViewMode::Storage && screen.bank, "could not open the storage view"))
        {
            return;
        }
        screen.storageFocusPane = 0;
        screen.selectedBoxIndex = boxIndex;
        screen.stSaveSlot = glitchSlot;

        driver.step("the kept entry is drawn as a '?' in its own slot and named in the info row").frame();
        bool markInSlot = false;
        for (const auto &touchTarget : screen.storageTouchTargets)
        {
            if (touchTarget.pane != 0 || touchTarget.box != boxIndex || touchTarget.slot != glitchSlot)
                continue;
            for (const UIHost::Draw *draw : driver.frameData().textExact(UI::Panels::UNMAPPED_SLOT_MARK))
                markInSlot = markInSlot || (draw->centerX() >= touchTarget.hitX && draw->centerX() < touchTarget.hitX + touchTarget.hitWidth && draw->centerY() >= touchTarget.hitY && draw->centerY() < touchTarget.hitY + touchTarget.hitHeight);
        }
        driver.expect(markInSlot, "the kept entry's slot is not drawn with the '?' placeholder");
        driver.see(UI::Panels::UNMAPPED_SLOT_LABEL);
        driver.expect(screen.storageSlotLocked(0, boxIndex, glitchSlot), "the kept entry's slot is not locked");

        driver.step("A on the kept entry opens neither the Pokemon menu nor the creator").press(Btn::A).frame();
        driver.expect(!screen.storageMenuActive && !screen.creator.active && !screen.pickerActive, "A on the kept entry's slot opened a menu or the creator");
        screen.creator.active = false;
        screen.pickerActive = false;
        screen.storageMenuActive = false;

        driver.step("Move mode cannot grab the kept entry");
        screen.cursorMode = Screen::CursorMode::Move;
        screen.pickupSingle();
        driver.expect(!screen.carrying(), "the kept entry's slot was picked up");

        driver.step("a carried Pokemon cannot be dropped on the kept entry");
        screen.stSaveSlot = otherSlot;
        screen.pickupSingle();
        if (driver.expect(screen.carriedCount() == 1, "could not pick up a Pokemon beside the kept entry"))
        {
            screen.stSaveSlot = glitchSlot;
            screen.putDownBlock();
            driver.frame();
            driver.expect(screen.carriedCount() == 1 && glitchKept(), "a Pokemon was dropped on the kept entry's slot");
            driver.press(Btn::B).frame();
            driver.expect(!screen.carrying() && trainer.boxes[boxIndex][otherSlot] && glitchKept(), "B did not put the Pokemon back home beside the kept entry");
        }

        driver.step("a multi-select over the whole box leaves the kept entry behind, and puts back around it");
        screen.cursorMode = Screen::CursorMode::Multi;
        screen.currentlySelecting = true;
        screen.selectPane = 0;
        screen.selectBox = boxIndex;
        screen.selectDimensions = {0, 0};
        screen.stSaveSlot = static_cast<int>(boxCapacity) - 1;
        screen.grabSelection(true);
        driver.expect(screen.carriedCount() == static_cast<int>(boxCapacity) - 1 && glitchKept(), "a whole-box multi-select did not leave exactly the kept entry behind", "carried=" + std::to_string(screen.carriedCount()));
        screen.putDownBlock();
        driver.frame();
        driver.expect(!screen.carrying() && countBoxedPokemon(trainer) == expectedBoxedCount && glitchKept(), "putting the box back down did not land every Pokemon around the kept entry", "boxed=" + std::to_string(countBoxedPokemon(trainer)) + " want=" + std::to_string(expectedBoxedCount));
        screen.clearCarried();

        driver.step("sorting the box pins the kept entry");
        screen.sortStorageBox(0, boxIndex);
        driver.frame();
        driver.expect(countBoxedPokemon(trainer) == expectedBoxedCount && glitchKept(), "sorting moved a Pokemon onto the kept entry or lost one");

        driver.step("Clone into a full box spills over the kept entry into another box");
        screen.cursorMode = Screen::CursorMode::Menu;
        screen.stSaveSlot = otherSlot;
        driver.press(Btn::A).frame();
        if (driver.expect(screen.storageMenuActive, "A on a Pokemon did not open its menu"))
        {
            screen.storageMenuIndex = Screen::STORAGE_MENU_ROW_CLONE;
            driver.press(Btn::A).frame();
            ++expectedBoxedCount;
            driver.expect(countBoxedPokemon(trainer) == expectedBoxedCount && glitchKept(), "Clone filled the kept entry's slot (or made no copy)", "boxed=" + std::to_string(countBoxedPokemon(trainer)) + " want=" + std::to_string(expectedBoxedCount));
        }
        screen.storageMenuActive = false;

        driver.step("Put Back whose home is the kept entry's slot finds a free slot instead");
        screen.clearCarried();
        screen.moveMon.push_back(donor->clone());
        screen.moveMonOrigins.push_back(Screen::CarriedCellOrigin{0, boxIndex, glitchSlot, false});
        screen.selectDimensions = {1, 1};
        screen.heldPane = 0;
        screen.heldFromBox = boxIndex;
        screen.heldFromSlot = glitchSlot;
        const bool returned = screen.returnHeldToOrigin();
        ++expectedBoxedCount;
        driver.expect(returned && countBoxedPokemon(trainer) == expectedBoxedCount && glitchKept(), "Put Back landed on the kept entry's slot (or lost the Pokemon)");
        driver.frame();

        driver.step("save and reload keep every Pokemon and the kept entry");
        Trainer::Trainer1RBY reloadedTrainer(trainer.serialize(), "Pokemon Red.dat");
        driver.expect(countBoxedPokemon(reloadedTrainer) == expectedBoxedCount && reloadedTrainer.isBoxSlotUnmapped(static_cast<size_t>(boxIndex), static_cast<size_t>(glitchSlot)), "a save and reload lost a Pokemon or the kept entry", "boxed=" + std::to_string(countBoxedPokemon(reloadedTrainer)) + " want=" + std::to_string(expectedBoxedCount));
        screen.hasUnsavedChanges = false;
    }

    /**
     * A counted Gen 2 party entry PKSE cannot read (species 252 here) keeps its party slot: the Party
     * view shows it there as "Unknown species", A on it opens nothing, the Pokemon after it open as
     * themselves, and a save and reload keep the whole party.
     */
    void testUnmappedPartySlotStaysPut(Test::Report &report, const std::string &target, const std::string &path)
    {
        std::vector<uint8_t> saveBytes = readFile(path);
        if (saveBytes.empty())
            return;
        if (!Trainer::Trainer2GSC::detect(saveBytes))
            Save::stripRtcFooter(saveBytes);
        if (!Trainer::Trainer2GSC::detect(saveBytes))
            return;
        Test::Context context;
        context.report = &report;
        context.suite = "ui";
        context.target = target;
        context.group = Enums::GameVersion::GSC;
        report.setScope(target, "ui");
        Trainer::Bank::setRootOverride("/tmp/pkse-uitest-bank");

        // The international party lists (Gold/Silver 0x288A, Crystal 0x2865), found by their header.
        Trainer::Trainer2GSC probeTrainer(saveBytes, path);
        std::vector<uint8_t> listHeader{static_cast<uint8_t>(probeTrainer.party.size())};
        for (auto &pokemon : probeTrainer.party)
            listHeader.push_back(static_cast<uint8_t>(pokemon->getData()[1]));
        listHeader.push_back(0xFF);
        size_t partyOffset = 0;
        for (const size_t candidateOffset : {size_t{0x288A}, size_t{0x2865}})
        {
            if (candidateOffset + listHeader.size() <= saveBytes.size() && std::equal(listHeader.begin(), listHeader.end(), saveBytes.begin() + candidateOffset))
                partyOffset = candidateOffset;
        }
        if (!context.check(partyOffset != 0 && !probeTrainer.party.empty(), "the save's party list was not found, so the unmapped party journey cannot run"))
        {
            return;
        }
        // Plant species 252 over party slot 1, then add a Pokemon so one follows it.
        std::vector<uint8_t> glitchedBytes = saveBytes;
        glitchedBytes[partyOffset + 1] = 0xFC;
        glitchedBytes[partyOffset + 1 + 6 + 1] = 0xFC;
        Trainer::Trainer2GSC trainer(glitchedBytes, path);
        context.trainer = &trainer;
        context.version = trainer.getGameVersion();
        if (trainer.party.empty())
            trainer.party.push_back(probeTrainer.party[0]->clone());
        const size_t partyCount = trainer.party.size();
        const uint16_t secondSpecies = trainer.party[0]->speciesID();
        if (!context.check(trainer.getUnmappedPartySlots() == std::vector<size_t>{0}, "the planted species-252 entry did not load as a kept unmapped party entry"))
        {
            return;
        }

        TrainerViewScreen screen(trainer, target, std::string("/tmp/pkse-uitest"), 0, AccountUid{}, false);
        Driver driver(context, screen, "unmapped-party-slot");
        driver.step("open the party view");
        gotoHome(driver, screen, 1);
        driver.press(Btn::A).frame();
        if (!driver.expect(screen.selectedMode == TrainerViewScreen::ViewMode::Party, "could not open the party view"))
        {
            return;
        }
        driver.see(std::string("Slot 1:  ") + UI::Panels::UNMAPPED_SLOT_LABEL);
        driver.see(Names::getDisplayName(secondSpecies, trainer.party[0]->form(), trainer.party[0]->species(), trainer.getGameGroup()));

        driver.step("A on the kept entry's slot opens nothing");
        screen.selectedPartyIndex = 0;
        driver.press(Btn::A).frame();
        driver.expect(!screen.details.active, "A on the kept entry's party slot opened the details editor");
        screen.details.active = false;

        driver.step("A on party slot 2 opens the first Pokemon");
        screen.selectedPartyIndex = 1;
        driver.press(Btn::A).frame();
        driver.expect(screen.details.active && screen.details.partyIndex == 0, "party slot 2 did not open the Pokemon shown there", "partyIndex=" + std::to_string(screen.details.partyIndex));
        screen.details.active = false;
        driver.frame();

        driver.step("save and reload keep the whole party, the kept entry in slot 1");
        Trainer::Trainer2GSC reloadedTrainer(trainer.serialize(), path);
        driver.expect(reloadedTrainer.party.size() == partyCount && reloadedTrainer.getUnmappedPartySlots() == std::vector<size_t>{0} && reloadedTrainer.party[0]->speciesID() == secondSpecies, "a save and reload lost a party Pokemon or moved the kept entry", "party=" + std::to_string(reloadedTrainer.party.size()) + " want=" + std::to_string(partyCount));
        screen.hasUnsavedChanges = false;
    }

    void runUI(Test::Report &report, const std::string &target, Enums::GameVersion group, Trainer::Trainer &trainer, u64 titleId)
    {
        Test::Context context;
        context.report = &report;
        context.suite = "ui";
        context.target = target;
        // The trainer names its own title, the same way the on-console runner asks a loose save.
        context.group = group;
        context.version = trainer.getGameVersion();
        context.trainer = &trainer;
        context.titleId = titleId;
        report.setScope(target, "ui");

        std::printf("  ui  %s\n", target.c_str());
        // Again for every target: the on-console journeys at the end of this function point the
        // bank at their own work directory and, having no way to read the previous root, put back
        // the default (the SD card's) when they finish.
        Trainer::Bank::setRootOverride("/tmp/pkse-uitest-bank");

        // A fresh screen per test: these drive real state, and a test that inherited the previous
        // one's cursor position would pass or fail depending on what ran before it.
        auto fresh = [&]() { return std::make_unique<TrainerViewScreen>(trainer, target, std::string("/tmp/pkse-uitest"), titleId, AccountUid{}, false); };

        {
            auto screen = fresh();
            testOpens(context, *screen, trainer);
        }
        {
            auto screen = fresh();
            testEveryDestinationOpensAndCloses(context, *screen);
        }
        {
            auto screen = fresh();
            testBoxGridGeometry(context, *screen, trainer);
        }
        {
            auto screen = fresh();
            testNoSpritesInEmptySlots(context, *screen, trainer);
        }
        {
            auto screen = fresh();
            testSummaryShowsLegality(context, *screen, trainer);
        }
        {
            auto screen = fresh();
            testLegalityOverlayStaysInCard(context, *screen, trainer);
        }
        {
            auto screen = fresh();
            testCursorMoves(context, *screen);
        }
        {
            auto screen = fresh();
            testTrainerCardShowsSaveData(context, *screen, trainer);
        }
        {
            auto screen = fresh();
            testBoxRename(context, *screen, trainer);
        }
        {
            auto screen = fresh();
            testEggTextIsNotForeign(context, *screen, trainer);
        }
        {
            auto screen = fresh();
            testPickerSearch(context, *screen, trainer);
        }
        {
            auto screen = fresh();
            testAlphaToggle(context, *screen, trainer);
        }
        {
            auto screen = fresh();
            testHyperTraining(context, *screen, trainer);
        }
        {
            auto screen = fresh();
            testHyperTrainingPartyMark(context, *screen, trainer);
        }
        {
            auto screen = fresh();
            testItemsKeys(context, *screen, trainer);
        }
        {
            auto screen = fresh();
            testBoxMenus(context, *screen, trainer);
        }
        {
            auto screen = fresh();
            testWithdrawSetsHandler(context, *screen, trainer);
        }
        {
            auto screen = fresh();
            testTapIsSeenBeforeItActs(context, *screen, trainer);
        }
        {
            auto screen = fresh();
            testDetailsEditCommits(context, *screen, trainer);
        }
        {
            auto screen = fresh();
            testPutBackKeepsEveryPokemon(context, *screen, trainer);
        }
        {
            auto screen = fresh();
            testBankSavedSeparately(context, *screen, trainer);
        }
        {
            auto screen = fresh();
            testPlusWaitsAndExitHints(context, *screen, trainer);
        }
        {
            auto screen = fresh();
            testGenerationLimits(context, *screen, trainer);
        }
        // The save dialog, raised the way X raises it from the HOME menu: its keys go over its scrim.
        {
            auto screen = fresh();
            Driver driver(context, *screen, "save-dialog-bar");
            driver.step("raise the save dialog");
            screen->saveConfirmActive = true;
            driver.frame();
            expectOneNavBar(driver, "save dialog open");
            screen->saveConfirmActive = false;
        }
        // Once, not per save: the browsers do not depend on which save is open.
        static bool browserFoldersTested = false;
        if (!browserFoldersTested)
        {
            browserFoldersTested = true;
            auto screen = fresh();
            testBrowserFolders(context, *screen);
        }
        static bool namedBackupTested = false;
        if (!namedBackupTested)
        {
            namedBackupTested = true;
            testNamedBackupFolder(context, trainer);
        }
        {
            auto screen = fresh();
            testInputFuzz(context, *screen, 240);
        }

        // The ON-CONSOLE suite, run here too. It drives the same screens through the same journeys
        // and applies the same change-verification; the only thing it cannot do off-console is
        // render, so context.framebuffer stays null and the draw path is skipped. Running it here is what lets
        // the journeys be debugged in seconds instead of by pushing an .nro to a console.
        {
            Test::Context uiContext;
            uiContext.report = &report;
            uiContext.suite = "ui";
            uiContext.target = target;
            uiContext.group = group;
            uiContext.version = trainer.getGameVersion();
            uiContext.trainer = &trainer;
            uiContext.titleId = titleId;
            uiContext.workDirectory = "/tmp/pkse-uitest-work";
            report.setScope(target, "ui");
            // The console runs the journeys with swkbd refused (Runner::start), because the applet
            // BLOCKS and an automated pass that reaches one waits for a human. Mirror that here, or
            // the host would quietly answer prompts the console cannot.
            g_suppressKeyboard = true;
            const int chunks = Test::uiPlan(uiContext);
            std::printf("  ui-journeys %s  (%d chunks)\n", target.c_str(), chunks);
            // Draw the driven screen after every chunk, exactly as TestRunnerScreen does on console.
            // Without this the host ran the whole walkthrough without ever DEREFERENCING the screen
            // it publishes, which is how a driven screen left pointing at a freed trainer passed
            // here and crashed on hardware. Drawing into the recorder costs little and closes the
            // one gap between the two runs.
            UI::PKSEFramebuffer probe;
            for (int chunkIndex = 0; chunkIndex < chunks; ++chunkIndex)
            {
                report.setScope(target, "ui");
                Test::uiRun(uiContext, chunkIndex);
                if (UI::UIScreen *driven = Test::Runner::drivenScreen())
                {
                    driven->draw(probe);
                }
            }
            // The suite MUST have torn itself down: the runner frees the trainer the moment it moves
            // to the next target, so anything still published here outlives the data behind it.
            g_suppressKeyboard = false;
            report.setScope(target, "ui");
            uiContext.check(Test::Runner::drivenScreen() == nullptr, "the ui suite finished with a screen still published as the driven screen", "chunks=" + std::to_string(chunks));
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
            for (auto &box : trainer.boxes)
            {
                for (auto &slot : box)
                {
                    slot.reset();
                }
            }
            for (auto &member : trainer.party)
            {
                member.reset();
            }

            const int failuresBefore = report.failures();
            Test::Context uiContext;
            uiContext.report = &report;
            uiContext.suite = "ui";
            uiContext.target = target;
            uiContext.group = group;
            uiContext.version = trainer.getGameVersion();
            uiContext.trainer = &trainer;
            uiContext.titleId = titleId;
            uiContext.workDirectory = "/tmp/pkse-uitest-work";
            report.setScope(target, "ui");
            // The console runs the journeys with swkbd refused (Runner::start), because the applet
            // BLOCKS and an automated pass that reaches one waits for a human. Mirror that here, or
            // the host would quietly answer prompts the console cannot.
            g_suppressKeyboard = true;
            const int chunks = Test::uiPlan(uiContext);
            std::printf("  ui-journeys %s (emptied save)  (%d chunks)\n", target.c_str(), chunks);
            UI::PKSEFramebuffer probe;
            for (int chunkIndex = 0; chunkIndex < chunks; ++chunkIndex)
            {
                report.setScope(target, "ui");
                Test::uiRun(uiContext, chunkIndex);
                if (UI::UIScreen *driven = Test::Runner::drivenScreen())
                {
                    driven->draw(probe);
                }
            }
            g_suppressKeyboard = false;
            report.setScope(target, "ui");
            uiContext.check(report.failures() == failuresBefore, "walking a save with no Pokemon in it produced failures; the steps that need one must be skipped, not pressed through", "added=" + std::to_string(report.failures() - failuresBefore));
        }
    }

    template <typename TrainerT>
    bool openSCBlock(Test::Report &report, const char *label, Enums::GameVersion group, const std::string &path, u64 titleId)
    {
        std::vector<uint8_t> bytes = readFile(path);
        if (bytes.empty())
        {
            return false;
        }
        std::vector<Save::Block> blocks = Encryption::decrypt(bytes.data(), bytes.size());
        if (blocks.empty())
        {
            std::printf("  !! %s: decrypt produced no blocks\n", label);
            return false;
        }
        TrainerT trainer(std::move(blocks));
        runUI(report, label, group, trainer, titleId);
        return true;
    }

    /// A loose save, read the way its own class reads one. Every group from Gen 1 to Gen 7 bar Gen 3
    /// once had Items pouches that L/R could not reach, and it shipped because no host run opened
    /// one of their saves: the walk is the same for every group, but each one answers for its own bag.
    template <typename TrainerT>
    bool openLoose(Test::Report &report, const char *label, Enums::GameVersion group, const std::string &path)
    {
        std::vector<uint8_t> bytes = readFile(path);
        if (bytes.empty())
        {
            return false;
        }
        // As Save::openExternalSave does: an RTC footer comes off only when the file does not
        // detect as it stands. A class must never be built over bytes its own detect() refuses.
        if (!TrainerT::detect(bytes) && !(Save::stripRtcFooter(bytes) && TrainerT::detect(bytes)))
        {
            std::printf("  !! %s: not a save this class opens: %s\n", label, path.c_str());
            return false;
        }
        TrainerT trainer(std::move(bytes), path);
        runUI(report, label, group, trainer, 0);
        return true;
    }

    /// TrainerT's own detect(); Gen 1's also reports the Japanese layout, which this does not need.
    template <typename TrainerT>
    bool detectsAs(const std::vector<uint8_t> &bytes)
    {
        if constexpr (std::is_same_v<TrainerT, Trainer::Trainer1RBY>)
        {
            bool japanese = false;
            return TrainerT::detect(bytes, japanese);
        }
        else
        {
            return TrainerT::detect(bytes);
        }
    }

    /// A loose save file, read and written by its own class.
    template <typename TrainerT>
    SaveLoader looseLoader(const std::string &path)
    {
        return {[path]() -> std::unique_ptr<Trainer::Trainer>
            {
                std::vector<uint8_t> bytes = readFile(path);
                if (bytes.empty())
                {
                    return nullptr;
                }
                if (!detectsAs<TrainerT>(bytes) && !(Save::stripRtcFooter(bytes) && detectsAs<TrainerT>(bytes)))
                {
                    return nullptr;
                }
                return std::make_unique<TrainerT>(std::move(bytes), path);
            },
            [path](Trainer::Trainer &trainer) -> std::unique_ptr<Trainer::Trainer>
            {
                std::vector<uint8_t> written = static_cast<TrainerT &>(trainer).serialize();
                return std::make_unique<TrainerT>(std::move(written), path);
            }};
    }

    /// A Switch save of encrypted blocks.
    template <typename TrainerT>
    SaveLoader scBlockLoader(const std::string &path)
    {
        return {[path]() -> std::unique_ptr<Trainer::Trainer>
            {
                std::vector<uint8_t> bytes = readFile(path);
                if (bytes.empty())
                {
                    return nullptr;
                }
                std::vector<Save::Block> blocks = Encryption::decrypt(bytes.data(), bytes.size());
                if (blocks.empty())
                {
                    return nullptr;
                }
                return std::make_unique<TrainerT>(std::move(blocks));
            },
            [](Trainer::Trainer &trainer) -> std::unique_ptr<Trainer::Trainer>
            {
                trainer.updateItemBlock();
                std::vector<uint8_t> written = Encryption::encrypt(trainer.getBlocks());
                std::vector<Save::Block> blocks = Encryption::decrypt(written.data(), written.size());
                if (blocks.empty())
                {
                    return nullptr;
                }
                return std::make_unique<TrainerT>(std::move(blocks));
            }};
    }

    /// Let's Go keeps its blocks in the first SAVE_SIZE7_LGPE bytes of the file, so it is written
    /// back over the file read from disk rather than as a fresh one.
    SaveLoader letsGoLoader(const std::string &savePath)
    {
        return SaveLoader{[savePath]() -> std::unique_ptr<Trainer::Trainer>
            {
                std::vector<uint8_t> bytes = readFile(savePath);
                if (bytes.size() < Trainer::SAVE_SIZE7_LGPE)
                {
                    return nullptr;
                }
                std::vector<uint8_t> area(bytes.begin(), bytes.begin() + Trainer::SAVE_SIZE7_LGPE);
                return std::make_unique<Trainer::Trainer7LGPE>(Trainer::createBlocksFromSaveData7LGPE(area));
            },
            [savePath](Trainer::Trainer &trainer) -> std::unique_ptr<Trainer::Trainer>
            {
                trainer.updateItemBlock();
                std::vector<uint8_t> bytes = readFile(savePath);
                if (bytes.size() < Trainer::SAVE_SIZE7_LGPE)
                {
                    return nullptr;
                }
                Trainer::writeBlocksToSaveData7LGPE(bytes, trainer.getBlocks());
                std::vector<uint8_t> area(bytes.begin(), bytes.begin() + Trainer::SAVE_SIZE7_LGPE);
                return std::make_unique<Trainer::Trainer7LGPE>(Trainer::createBlocksFromSaveData7LGPE(area));
            }};
    }

    /// Brilliant Diamond/Shining Pearl is one flat file whose hash is recomputed on write.
    SaveLoader brilliantDiamondLoader(const std::string &savePath)
    {
        return SaveLoader{[savePath]() -> std::unique_ptr<Trainer::Trainer>
            {
                std::vector<uint8_t> bytes = readFile(savePath);
                if (bytes.empty())
                {
                    return nullptr;
                }
                return std::make_unique<Trainer::Trainer8BDSP>(std::move(bytes));
            },
            [](Trainer::Trainer &trainer) -> std::unique_ptr<Trainer::Trainer>
            {
                auto &brilliantDiamondTrainer = static_cast<Trainer::Trainer8BDSP &>(trainer);
                brilliantDiamondTrainer.updateItemBlock();
                brilliantDiamondTrainer.recomputeHash();
                return std::make_unique<Trainer::Trainer8BDSP>(brilliantDiamondTrainer.getSaveData());
            }};
    }

    /// One save of every format PKSE edits, from the save tree, with how to open it and write it back.
    /// Shared by the tests that must cover each format: runUI does not walk Ruby/Sapphire/Emerald.
    struct FormatTarget
    {
        const char *label;
        Enums::GameVersion group;
        SaveLoader loader;
    };
    std::vector<FormatTarget> everyFormatTarget(const std::string &saveRoot)
    {
        using Target = FormatTarget;
        std::string bdspPath = SaveTree::folderFor(saveRoot, "Shining Pearl") + "/SaveData.bin";
        if (readFile(bdspPath).empty())
        {
            bdspPath = SaveTree::forTitle(saveRoot, "Shining Pearl");
        }
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
            {"Let's Go Pikachu", Enums::GameVersion::GG, letsGoLoader(SaveTree::letsGoPikachu(saveRoot))},
            {"Shield", Enums::GameVersion::SWSH, scBlockLoader<Trainer::Trainer8SWSH>(SaveTree::shield(saveRoot))},
            {"Shining Pearl", Enums::GameVersion::BDSP, brilliantDiamondLoader(bdspPath)},
            {"Legends Arceus", Enums::GameVersion::PLA, scBlockLoader<Trainer::Trainer8LA>(SaveTree::legendsArceus(saveRoot))},
            {"Violet", Enums::GameVersion::SV, scBlockLoader<Trainer::Trainer9SV>(SaveTree::violet(saveRoot))},
            {"Legends Z-A", Enums::GameVersion::ZA, scBlockLoader<Trainer::Trainer9LZA>(SaveTree::legendsZA(saveRoot))},
        };
        return targets;
    }

    /// Add All Items on every format PKSE edits.
    int runAddAllItems(Test::Report &report, const std::string &saveRoot)
    {
        std::set<Enums::GameVersion> groupsTested;
        for (const FormatTarget &target : everyFormatTarget(saveRoot))
        {
            if (!target.loader.open())
            {
                report.setScope(target.label, "ui");
                Test::Context context;
                context.report = &report;
                context.suite = "ui";
                context.target = target.label;
                context.skip("no save for this title, so Add All Items was not tested on it", SaveTree::missing(saveRoot, target.label));
                continue;
            }
            testAddAllItems(report, target.label, target.group, target.loader);
            groupsTested.insert(target.group);
        }
        return static_cast<int>(groupsTested.size());
    }

    /// The ribbon editor on every format PKSE edits, Gen 1 and Gen 2 included: they must offer none.
    int runRibbonEditor(Test::Report &report, const std::string &saveRoot)
    {
        std::set<Enums::GameVersion> groupsTested;
        for (const FormatTarget &target : everyFormatTarget(saveRoot))
        {
            if (!target.loader.open())
            {
                report.setScope(target.label, "ui");
                Test::Context context;
                context.report = &report;
                context.suite = "ui";
                context.target = target.label;
                context.skip("no save for this title, so the ribbon editor was not tested on it", SaveTree::missing(saveRoot, target.label));
                continue;
            }
            testRibbonEditor(report, target.label, target.group, target.loader);
            groupsTested.insert(target.group);
        }
        return static_cast<int>(groupsTested.size());
    }

    /// A refusal the file browser reports has to be DRAWN. The save picker always set its reason in
    /// `status`, and the browser drew that only for an empty folder -- which a refused file never is,
    /// being a row of the listing -- so every refusal was invisible and the pick read as a dead A
    /// button. Checked on the drawn frame, because the state was right all along.
    void testFileBrowserShowsRefusal(Test::Context &context)
    {
        const std::string root = "/tmp/pkse-uitest-refusal";
        const std::string fileName = "Pokemon Ruby.srm";
        mkdir(root.c_str(), 0777);
        if (FILE *handle = std::fopen((root + "/" + fileName).c_str(), "wb"))
        {
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
        const UIHost::Frame &frame = UIHost::Recorder::get().last();

        context.check(!browser.entries.empty() && frame.has(fileName), "the refusal fixture did not list its own file", "file=\"" + fileName + "\"");
        context.check(frame.has("is not a save PKSE can open"), "a refused file's reason was not drawn while the folder lists it", "status=\"" + browser.status + "\"");
        context.check(frame.has("Save files") && !frame.has("Bank files"), "the save browser does not name the files it lists as save files");

        // A container PKSE cannot read says what to change, at the END of the line -- so when a long name
        // makes the line too wide for the strip, the name is what gets cut, never the advice.
        {
            std::vector<uint8_t> compressedSave(0x8000, 0);
            const std::string rzipMagic = "#RZIPv\x01#";
            std::copy(rzipMagic.begin(), rzipMagic.end(), compressedSave.begin());
            const char *reason = Save::describeUnreadableSaveContainer(compressedSave);
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

int main(int argc, char **argv)
{
    const std::string reportFilePath = (argc > 1) ? argv[1] : std::string("uitest-report.log");
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
    if (const char *shotDirectory = std::getenv("PKSE_UITEST_SHOTS"))
    {
        UIHost::Driver::setShotDirectory(shotDirectory);
    }

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
        for (int chunk = 0; chunk < chunks; ++chunk)
        {
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
            if (context.check(image.size() > 6, "could not re-read the scratch bank to trim its open-box section"))
            {
                std::FILE *trimmed = std::fopen(bankFile.c_str(), "wb");
                if (trimmed)
                {
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
    if (!saveRoot.empty())
    {
        std::printf("  saves: %s\n\n", saveRoot.c_str());

        std::vector<uint8_t> frlg = readFile(SaveTree::fireRed(saveRoot));
        if (!frlg.empty())
        {
            Trainer::Trainer3FRLG trainer(frlg, "FireRed_e.sav");
            runUI(report, "FireRed", Enums::GameVersion::FRLG, trainer, 0x0100554023408000ULL);
            ++opened;
        }
        // Let's Go is not an SCBlock game and not a flat GBA file -- it is the one format with its
        // own block builder, which is exactly why it was missing here and why the console run was
        // the first thing to walk its editor.
        std::vector<uint8_t> letsGoSaveBytes = readFile(SaveTree::letsGoPikachu(saveRoot));
        if (letsGoSaveBytes.size() >= Trainer::SAVE_SIZE7_LGPE)
        {
            std::vector<uint8_t> area(letsGoSaveBytes.begin(), letsGoSaveBytes.begin() + Trainer::SAVE_SIZE7_LGPE);
            Trainer::Trainer7LGPE trainer(Trainer::createBlocksFromSaveData7LGPE(area));
            runUI(report, "Let's Go Pikachu", Enums::GameVersion::GG, trainer, 0x010003F003A34000ULL);
            ++opened;
        }
        {
            std::vector<uint8_t> red = readFile(SaveTree::red(saveRoot));
            if (!red.empty())
            {
                Trainer::Trainer1RBY trainer(red, "Pokemon Red.dat");
                runUI(report, "Red", Enums::GameVersion::RBY, trainer, 0);
                testUnmappedBoxSlotStaysPut(report, "Red", red);
                ++opened;
            }
        }
        opened += openLoose<Trainer::Trainer2GSC>(report, "Gold", Enums::GameVersion::GSC, SaveTree::forTitle(saveRoot, "Gold")) ? 1 : 0;
        testUnmappedPartySlotStaysPut(report, "Gold", SaveTree::forTitle(saveRoot, "Gold"));
        testUnmappedPartySlotStaysPut(report, "Crystal", SaveTree::forTitle(saveRoot, "Crystal"));
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
            if (readFile(bdspPath).empty())
            {
                bdspPath = SaveTree::forTitle(saveRoot, "Shining Pearl");
            }
            std::vector<uint8_t> bdsp = readFile(bdspPath);
            if (!bdsp.empty())
            {
                Trainer::Trainer8BDSP trainer(std::move(bdsp));
                runUI(report, "Shining Pearl", Enums::GameVersion::BDSP, trainer, 0x010018E011D92000ULL);
                ++opened;
            }
        }
        opened += openSCBlock<Trainer::Trainer9SV>(report, "Violet", Enums::GameVersion::SV, SaveTree::violet(saveRoot), 0x01008F6008C5E000ULL) ? 1 : 0;
    }

    if (!saveRoot.empty())
    {
        // Every one of the nineteen save formats, so a pouch or a writer that cannot take a full bag
        // is found on the format it breaks.
        const int addAllGroupCount = runAddAllItems(report, saveRoot);
        report.setScope("-", "ui");
        Test::Context context;
        context.report = &report;
        context.suite = "ui";
        context.check(addAllGroupCount == 19, "Add All Items was not tested on every save format", "formats=" + std::to_string(addAllGroupCount) + "/19");
        const int ribbonGroupCount = runRibbonEditor(report, saveRoot);
        report.setScope("-", "ui");
        context.check(ribbonGroupCount == 19, "the ribbon editor was not tested on every save format", "formats=" + std::to_string(ribbonGroupCount) + "/19");
    }

    if (opened == 0)
    {
        report.setScope("-", "ui");
        Test::Context context;
        context.report = &report;
        context.skip("no saves were opened, so no UI test ran at all", "saveRoot=\"" + saveRoot + "\"");
    }

    std::printf("\n");
    for (const auto &tally : report.tallies())
    {
        std::printf("  %-28s %-6s %7d checks  %4d fail  %4d warn  %4d skip\n", tally.target.c_str(), tally.suite.c_str(), tally.checks, tally.fails, tally.warns, tally.skips);
    }

    std::printf("\nfindings:\n");
    for (const auto &findingGroup : report.groups())
    {
        std::printf("  [%s] %s: %s  x%d\n", Test::severityName(findingGroup.severity), findingGroup.suite.c_str(), findingGroup.what.c_str(), findingGroup.occurrenceCount);
        for (size_t sampleIndex = 0; sampleIndex < findingGroup.samples.size() && sampleIndex < 3; ++sampleIndex)
        {
            std::printf("        %s\n", findingGroup.samples[sampleIndex].c_str());
        }
    }
    if (report.groups().empty())
    {
        std::printf("  (none)\n");
    }

    report.write(reportFilePath, "PKSE UI TESTS (host, real screens driven with synthetic input)\n");
    std::printf("\nchecks=%d failures=%d warnings=%d skipped=%d notes=%d  shots=%d  -> %s\n", report.checks(), report.failures(), report.warnings(), report.skips(), report.notes(), UIHost::Driver::writtenShotCount(), reportFilePath.c_str());
    return report.failures() == 0 ? 0 : 1;
}
