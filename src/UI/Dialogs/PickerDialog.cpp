#include <cstdint>
#include <string>

#include "UI/Dialogs/PickerDialog.h"
#include "UI/TrainerViewScreen.h"
#include "UI/Common.h"
#include "UI/ScreenChrome.h"     // drawScrollbar
#include "UI/PKSEFramebuffer.h"
#include "Trainer/Trainer.h"     // getNatureName / getAbilityName
#include "Names/MoveNames.h"     // getMoveName / getMoveCount
#include "Names/ItemNames.h"     // getItemNameG3 (Gen 3 has its own item id space)
#include "Enums/Ball.h"          // getBallName
#include "Enums/LanguageID.h"    // getLanguageName
#include "Enums/GameVersion.h"   // getGameVersionName (Origin picker)
#include "Names/LocationNames.h" // getMetLocationName (Met Location picker)
#include "Names/FormNames.h"     // getFormName (Form picker)

using namespace Trainer;

namespace UI {
namespace Dialogs {

    static const char* const GENDER_LABELS[3] = { "Male", "Female", "Genderless" };

    int pickerOptionCount(PickerKind kind) {
        switch (kind) {
            case PickerKind::Nature: return 25;
            case PickerKind::Gender: return 3;
            case PickerKind::Move:   return static_cast<int>(Names::getMoveCount());
            case PickerKind::Item:   return static_cast<int>(getItemCount());
            case PickerKind::ItemG3: return static_cast<int>(Names::getItemCountG3());
            case PickerKind::Level:  return 100;
            case PickerKind::Friendship: return 256;
            case PickerKind::Ball:       return 38;   // 0=(none) .. 37=Origin Ball
            case PickerKind::Ability:    return 311;  // AbilityNames covers ids 0-310 (through Gen 9)
            case PickerKind::Language:   return 11;   // 0-10 (6 is unused, shown as "-")
            case PickerKind::Origin:     return 53;   // version ids 0-52 (getGameVersionName)
            case PickerKind::MetLevel:   return 100;  // met level 1-100
            case PickerKind::MetLocation: return 0;   // ids supplied via pickerOrder (caller sets count)
            case PickerKind::Form:        return 0;   // form count is species-dependent (caller sets count)
            case PickerKind::StatNature:  return 25;  // same 25 natures as the real nature
            case PickerKind::Species:    return 1026; // ids 0-1025 (0 = None)
            // Pouch lists are supplied through pickerOrder, so the caller sets pickerCount.
            case PickerKind::PouchItem:
            case PickerKind::PouchItemG3: return 0;
        }
        return 0;
    }

    const char* pickerOptionLabel(PickerKind kind, int index) {
        switch (kind) {
            case PickerKind::Nature: return getNatureName(static_cast<uint8_t>(index));
            case PickerKind::Gender: return (index >= 0 && index < 3) ? GENDER_LABELS[index] : "?";
            case PickerKind::Move:   return Names::getMoveName(static_cast<uint16_t>(index));
            case PickerKind::Item:   return getItemName(static_cast<uint16_t>(index));
            case PickerKind::ItemG3: return Names::getItemNameG3(static_cast<uint16_t>(index));
            case PickerKind::Level:  { static std::string s; s = std::to_string(index + 1); return s.c_str(); }
            case PickerKind::Friendship: { static std::string s; s = std::to_string(index); return s.c_str(); }
            case PickerKind::Ball:       return Enums::getBallName(static_cast<uint8_t>(index));
            case PickerKind::Ability:    return getAbilityName(static_cast<uint16_t>(index));
            case PickerKind::Language:   return Enums::getLanguageName(static_cast<uint8_t>(index));
            case PickerKind::Origin:     { static std::string s; s = Enums::getGameVersionName(static_cast<Enums::GameVersion>(index)); return s.c_str(); }
            case PickerKind::MetLevel:   { static std::string s; s = std::to_string(index + 1); return s.c_str(); }
            case PickerKind::MetLocation: return "";  // labelled in drawPickerDialog (needs the origin version)
            case PickerKind::Form:        return "";  // labelled in drawPickerDialog (needs the species)
            case PickerKind::StatNature:  return getNatureName(static_cast<uint8_t>(index));
            case PickerKind::Species:    return getSpeciesName(static_cast<uint16_t>(index));
            case PickerKind::PouchItem:   return getItemName(static_cast<uint16_t>(index));
            case PickerKind::PouchItemG3: return Names::getItemNameG3(static_cast<uint16_t>(index));
        }
        return "?";
    }

    const char* pickerTitle(PickerKind kind) {
        switch (kind) {
            case PickerKind::Nature: return "Select Nature";
            case PickerKind::Gender: return "Select Gender";
            case PickerKind::Move:   return "Select Move";
            case PickerKind::Item:
            case PickerKind::ItemG3: return "Select Held Item";
            case PickerKind::Level:  return "Select Level";
            case PickerKind::Friendship: return "Select Friendship";
            case PickerKind::Ball:       return "Select Ball";
            case PickerKind::Ability:    return "Select Ability";
            case PickerKind::Language:   return "Select Language";
            case PickerKind::Origin:     return "Select Origin Game";
            case PickerKind::MetLevel:   return "Select Met Level";
            case PickerKind::MetLocation: return "Select Met Location";
            case PickerKind::Form:        return "Select Form";
            case PickerKind::StatNature:  return "Select Stat Nature (mint)";
            case PickerKind::Species:    return "Select Species";
            case PickerKind::PouchItem:
            case PickerKind::PouchItemG3: return "Add Item to Pouch";
        }
        return "Select";
    }

    bool pickerSupportsSearch(PickerKind kind) {
        switch (kind) {
            case PickerKind::Move:
            case PickerKind::Item:
            case PickerKind::ItemG3:
            case PickerKind::PouchItem:
            case PickerKind::PouchItemG3:
                return true;
            default:
                return false;
        }
    }

    void drawPickerDialog(TrainerViewScreen& screen, PKSEFramebuffer& fb) {
        const int W = fb.getWidth(), H = fb.getHeight();
        const PickerKind kind = screen.pickerKind;
        const int count = screen.pickerCount > 0 ? screen.pickerCount : 0;
        int sel = screen.pickerSel;
        if (count > 0) {
            if (sel < 0) sel = 0;
            if (sel >= count) sel = count - 1;
        } else {
            sel = 0;
        }
        const bool searchable = pickerSupportsSearch(kind);
        const bool filtered = searchable && !screen.pickerSearchQuery.empty();
        const bool liveSearch = searchable && screen.pickerSearchKeyboardActive;

        // Dim behind + centered panel.
        fb.drawFilledRect(0, 0, W, H, Color(0, 0, 0, 150));
        const int pw = 560, ph = liveSearch ? 286 : H - 120;
        const int px = (W - pw) / 2, py = liveSearch ? 24 : 60;
        fb.drawFilledRoundedRect(px, py, pw, ph, 16, Colors::Panel);
        fb.drawRoundedRect(px, py, pw, ph, 16, Colors::Accent, 2);

        // Title + position caption. A pouch picker opened to change an existing item's type says so
        // rather than "Add Item to Pouch".
        const char* title = (screen.itemPickerReplace &&
                             (kind == PickerKind::PouchItem || kind == PickerKind::PouchItemG3))
                          ? "Change Item To" : pickerTitle(kind);
        fb.drawText(px + 20, py + (searchable ? 8 : 16), title, Colors::Text, TextStyle::Heading);
        {
            std::string pos = count > 0
                            ? std::to_string(sel + 1) + " / " + std::to_string(count)
                            : "0 / 0";
            int pwi, phi; fb.measureText(pos, pwi, phi, TextStyle::Caption);
            fb.drawText(px + pw - 20 - pwi, py + (searchable ? 14 : 22),
                        pos, Colors::TextDim, TextStyle::Caption);
        }
        const int headerBottom = searchable ? py + 60 : py + 52;
        if (searchable) {
            const std::string searchText = liveSearch
                ? "Search: " + (screen.pickerSearchQuery.empty()
                              ? std::string("all options") : screen.pickerSearchQuery)
                : filtered ? "Search: " + screen.pickerSearchQuery
                           : "X: Search by name";
            fb.drawText(px + 20, py + 40, searchText, Colors::TextDim, TextStyle::Caption);
        }
        fb.drawHDivider(px + 20, headerBottom, pw - 40);

        // Scrollable list window centered on the selection.
        const int rowH = 40;
        const int listTop = headerBottom + 12, listBottom = py + ph - 48;
        int visible = (listBottom - listTop) / rowH;
        if (visible < 1) visible = 1;
        int first = 0;
        if (count > 0) {
            first = sel - visible / 2;
            if (first > count - visible) first = count - visible;
            if (first < 0) first = 0;
        }

        screen.touchButtons.clear();
        // Ability and Move put legal options first and render that prefix green. Other mapped pickers
        // filter or search their original values, so they need the same row -> value indirection.
        //
        // A kind that fills pickerOrder and is missing from this list is the sharp edge: labels would
        // keep using the row index while the write uses pickerOrder, naming one value and writing
        // another with nothing on screen to show it.
        const bool mapped = (kind == PickerKind::Ability || kind == PickerKind::Species
                          || kind == PickerKind::Move    || kind == PickerKind::Item
                          || kind == PickerKind::ItemG3  || kind == PickerKind::PouchItem
                          || kind == PickerKind::PouchItemG3 || kind == PickerKind::MetLocation
                          || kind == PickerKind::Ball    || kind == PickerKind::Form
                          || kind == PickerKind::Gender)
                          && !screen.pickerOrder.empty();
        for (int i = 0; i < visible && (first + i) < count; ++i) {
            const int idx = first + i;
            const int val = (mapped && idx < static_cast<int>(screen.pickerOrder.size())) ? screen.pickerOrder[idx] : idx;
            const int ry = listTop + i * rowH;
            const bool s = (idx == sel);
            if (s) {
                fb.drawFilledRoundedRect(px + 12, ry, pw - 24, rowH - 4, 8, Colors::Selected);
                fb.drawRoundedRect(px + 12, ry, pw - 24, rowH - 4, 8, Colors::Accent, 2);
            }
            const bool legal = mapped && idx < screen.pickerLegalCount;
            const Color col = legal ? Color(120, 210, 130) : (s ? Colors::Text : Colors::TextDim);
            // Met Location / Form resolve their names through mon-specific context (origin version /
            // species) that the free pickerOptionLabel() can't see; everything else is context-free.
            const char* label;
            static std::string formLbl;
            if (kind == PickerKind::MetLocation) {
                label = Names::getMetLocationName(screen.pickerMetVersion, static_cast<uint16_t>(val));
            } else if (kind == PickerKind::Form) {
                const char* fn = Names::getFormName(screen.pickerFormSpecies, static_cast<uint8_t>(val));
                formLbl = (fn[0] != '\0') ? std::string(fn)
                                          : (val == 0 ? std::string("Base") : ("Form " + std::to_string(val)));
                label = formLbl.c_str();
            } else {
                label = pickerOptionLabel(kind, val);
            }
            fb.drawText(px + 28, ry + (rowH - 4 - fb.lineHeight(TextStyle::Body)) / 2, label, col);
            screen.touchButtons.push_back({ idx, px + 12, ry, pw - 24, rowH - 4 });  // id = option row
        }
        if (count == 0) {
            const char* emptyText = filtered ? "No matching options" : "No options available";
            int ew, eh; fb.measureText(emptyText, ew, eh, TextStyle::Body);
            fb.drawText(px + (pw - ew) / 2, listTop + (listBottom - listTop - eh) / 2,
                        emptyText, Colors::TextDim);
        }

        // Scrollbar on the panel's right edge (same thumb as everywhere else) when the list overflows.
        if (count > visible)
            drawScrollbar(fb, px + pw - 14, listTop, visible * rowH, count * rowH, first * rowH);

        const char* controls = liveSearch
            ? "Results update live as you type"
            : !searchable
            ? "A: Select    B: Cancel    L/R: Page"
            : count == 0
                ? "B: Cancel    X: Edit Search    Y: Clear Search"
                : filtered
                    ? "A: Select    B: Cancel    X: Edit    Y: Clear    L/R: Page"
                    : "A: Select    B: Cancel    X: Search    L/R: Page";
        fb.drawText(px + 20, py + ph - 34, controls, Colors::TextDim, TextStyle::Caption);
    }
}
}
