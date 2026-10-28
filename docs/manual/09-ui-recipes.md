# 09 — UI recipes

Step-by-step, with the exact files and the exact shape of each edit. Every recipe follows the same
discipline, so internalise it once:

> **Draw and handle are two edits in two places.** Add the visual in a `draw*` function, add the
> input in `TrainerViewScreen::update()`, and register a touch target so both work. Then check the
> nav-bar hint string, because a control nobody knows about does not exist.

Reserved touch ids to avoid colliding with:

| Id(s) | Meaning |
|---|---|
| `0` / `1` | Cancel (B) / Confirm (A) in the edit dialogs |
| `2` | Remove (Y) in the item edit dialog |
| `30`–`35` | The six ±1/±10/±100 step buttons |
| `40` | Item-type drop-down in the item edit dialog |
| `10` / `11` / `12` | IV row / EV-AV row / Hyper Trained row in the stat editor |
| `0`–`33` | Details-modal field ids (32 = Species, 33 = Evolve) |
| `94` / `99` | Open the ribbon editor / close button in the details modal |
| `200`+ | Ribbon editor tiles: `RIBBON_TILE_BUTTON_ID_BASE` + the row's index in the format's table |
| `100`–`105` | HOME menu pills and icons |

Panels and pickers use ids that are meaningful in their own context (an item index, an option row),
and that is fine because only one overlay populates `touchButtons` at a time.

---

## Recipe 1 — Add a settings row

Goal: a persisted on/off toggle that appears in Settings.

### 1. Declare the global

`include/Globals.h`:

```cpp
/// Settings toggle ("Confirm Releases"), persisted. Default ON.
///
/// Say WHY here, not just what. The next person reading this needs to know whether turning it off
/// is a convenience or a risk.
inline bool g_confirmRelease = true;
```

### 2. Persist it

`src/Utils/Settings.cpp` — one branch in each function:

```cpp
// in loadSettings()
} else if (strcmp(key, "confirmRelease") == 0) {
    g_confirmRelease = (strcmp(val, "0") != 0);
}

// in saveSettings()
fprintf(f, "confirmRelease=%d\n", g_confirmRelease ? 1 : 0);
```

**Read a missing key as the default, never as `false`.** `loadSettings` returns early when the file
does not exist, so a brand-new install keeps the compiled-in defaults. If you ever rename a key,
keep reading the old one (as `moveWarn` does for `lgpeMoveWarn`) so an existing `settings.cfg` does
not silently revert the user's choice — and only write the new one, so it ages out.

### 3. Draw the row

`src/UI/TrainerViewScreen.cpp` → `drawSettingsView()`:

**A row is a ROW of one table, not an entry in four lists that happen to line up.** Add your row to
every array in the same place:

```cpp
constexpr int rowCount = SETTINGS_ROW_COUNT;   // rows DEFINED; one shared constant, see step 5
const char* labels[rowCount] = {
    /* … */
    "Enable Debug Logging",
    "Confirm Releases",                        // NEW
    "Language",                                // stays LAST -- see below
};
std::string values[rowCount] = {
    /* … */
    g_debugLogging ? "On" : "Off",
    g_confirmRelease ? "On" : "Off",           // NEW
    Names::nameLanguageLabel(Names::displayLanguageIndex()),
};
const bool rowIsOn[rowCount]          = { /* … */ g_debugLogging, g_confirmRelease, false };
const bool rowIsDestructive[rowCount] = { /* … */ false,          false,            false };
```

**The Language row must stay last, and a `static_assert` enforces it.** It is defined but not shown
(`Names::DISPLAY_LANGUAGE_SELECTABLE` is false while the language is being moved onto the save
file), and it is hidden by `SETTINGS_ROW_VISIBLE_COUNT` trimming the final row — so a row added
*after* it would be the one that vanished. Put yours before it.

**Sizing the tables by `SETTINGS_ROW_COUNT` rather than the visible count is deliberate**: a defined
row keeps its label, value and badge on one line of one table whether it is on screen or not, so
un-hiding it is a one-constant change with nothing to re-thread.

and the loop — which runs to `SETTINGS_ROW_VISIBLE_COUNT`, not `rowCount` — needs nothing added at
all:

```cpp
Color pillFill = Colors::Background, pillText = Colors::Text;
if (rowIsOn[index]) {
    pillFill = rowIsDestructive[index] ? Colors::Danger : Colors::Primary;
    pillText = rowIsDestructive[index] ? Colors::White  : Colors::PrimaryText;
}
```

**Amber (`Colors::Primary`) for a benign toggle; red (`Colors::Danger`) for anything that can damage
real data.** Colour carries the risk, not just the text.

**The colours used to be a chain of `else if (index == 4 && g_someFlag)` tests, and that is exactly
the bug to avoid**: the chain was a SECOND list of row numbers, so inserting a row in the middle
moved `labels` and `values` and left the chain behind. Every badge from the insertion point down
then lit for its neighbour's setting — toggling the move warning recoloured the auto-legalize pill,
which is how it was reported. Keeping the colour in the same table as the label is what makes that
impossible rather than merely unlikely.

### 4. Mind the `static_assert`

```cpp
constexpr int rowW = 720, rowH = 50, rowGap = 8, rowsTop = 22, footerH = 26;
static_assert(headerHeight + rowsTop + rowCount * (rowH + rowGap) + footerH <= CONTENT_PANEL_HEIGHT,
              "Settings rows no longer fit the content panel -- shrink the rows or add scrolling");
```

Eight rows use 558 px of the 560 available. **A ninth row will fail this assert**, and that is the
point: it forces you to choose between shrinking `rowH` (which is also the touch target — do not go
below the ~44 px minimum) and adding scrolling, instead of quietly drawing past the panel edge. It
has already earned its keep twice, on the Language row and on the auto-legalize row.

### 5. Handle the input

`src/UI/TrainerViewScreen.cpp` → the `if (selectedMode == ViewMode::Settings)` block in `update()`:

```cpp
constexpr int settingsRowCount = SETTINGS_ROW_COUNT;
if (buttonsDown & HidNpadButton_Up)   settingsSelectedRow = (settingsSelectedRow - 1 + settingsRowCount) % settingsRowCount;
if (buttonsDown & HidNpadButton_Down) settingsSelectedRow = (settingsSelectedRow + 1) % settingsRowCount;
if (buttonsDown & HidNpadButton_A) {
    if (settingsSelectedRow == SETTINGS_ROW_AUTO_BACKUP)         g_autoBackupEnabled = !g_autoBackupEnabled;
    /* … */
    else if (settingsSelectedRow == SETTINGS_ROW_CONFIRM_RELEASE) g_confirmRelease = !g_confirmRelease;  // NEW
    Utils::saveSettings();             // persist immediately — never batch
}
```

**Name the rows, never number them.** The `SETTINGS_ROW_*` constants at the top of
`TrainerViewScreen.cpp` are the one list of row indices; the drawing code and this handler both read
them, so neither can name a different row than the other. They used to hold private copies (a
literal `7` here, a literal `7` there, and a comment naming each row by number) and the copies
drifted the first time a row was inserted in the middle. Add your constant there, bump
`SETTINGS_ROW_COUNT`, and the assert in step 4 checks the rest.

### 6. Verify

```bash
python tools/syntax_check.py src/UI/TrainerViewScreen.cpp src/Utils/Settings.cpp
python tools/syntax_check.py --warnings
```

---

## Recipe 2 — Add a field to the Pokémon details editor

Goal: an editable row in the left column of the details modal, backed by a picker.

Worked example: **Dynamax Level** (a Gen 8 field), field id `34` — the next free id after Species
(32) and Evolve (33).

### 1. Make sure the entity exposes it

If `Pokemon` has no accessor yet, add a virtual pair with a safe default (chapter 06):

```cpp
// include/Pokemon/Pokemon.h
virtual uint8_t dynamaxLevel() const noexcept { return 0; }
virtual void    setDynamaxLevel(uint8_t) noexcept {}
virtual bool    hasDynamax() const noexcept { return false; }   // 0 is a real level!
```

and override in the formats that have it (`Pokemon8SWSH`: offset `0x90`).

**Add the `hasXXX()` predicate if `0` is a legitimate value.** It is here — Dynamax Level 0 is real.

### 2. Draw the row

`src/UI/Modals/PokemonDetailsModal.cpp`, inside the left column, positioned where you want it in
draw order:

```cpp
if (p->hasDynamax())
    editRow("Dynamax Lv", std::to_string(p->dynamaxLevel()), 34);
```

`editRow` pushes `34` into `leftOrder` **and** registers the touch target, so navigation and touch
both pick it up with no further work. Use `row(...)` instead if the field should be read-only.

### 3. Extend the touch range

`src/UI/TrainerViewScreen.cpp`, in the details-modal input block:

```cpp
if (touchedButton >= 0 && touchedButton <= 34)   // was 33
{
    details.selectedField = touchedButton;        // draws highlighted this frame
    if (armTap(touchedButton, HidNpadButton_A)) return;   // ...and opens a few frames later
}
```

The tap is **deferred** through `armTap`, not folded into `buttonsDown` in the same frame — otherwise
the picker opens before the row has ever been drawn selected (chapter 08, "The one-frame-late touch
contract").

### 4. Handle A on the field

Same block, in the `if ((buttonsDown & HidNpadButton_A) && pokemon)` chain:

```cpp
} else if (fieldIndex == 34) {     // Dynamax Level
    pickerKind  = Dialogs::PickerKind::DynamaxLevel;
    pickerCount = Dialogs::pickerOptionCount(pickerKind);
    pickerSel   = pokemon->dynamaxLevel();
    pickerActive = true;
}
```

### 5. Apply the pick

In the `pickerActive` block, where each kind's `A` press is applied:

```cpp
case Dialogs::PickerKind::DynamaxLevel:
    pkPick->setDynamaxLevel(static_cast<uint8_t>(pickerSelectedValue()));   // NEVER pickerSel: see recipe 3
    pkPick->refreshChecksum();        // + recalculateStats() FIRST if the field affects stats
    hasUnsavedChanges = true;
    mirrorEditedPartyMember();        // keep an LGPE party member's box/party copies in sync
    break;
```

**Three things must happen on every edit** and forgetting any one is a distinct bug:

| Call | Why | Forgetting it means |
|---|---|---|
| `recalculateStats()` (stat-affecting fields only) | derive battle stats | the stored stats disagree with the IVs/EVs the details page shows |
| `refreshChecksum()` | the game validates the record | the game rejects the Pokémon or shows a Bad Egg |
| `hasUnsavedChanges = true` | drives the save prompt | the edit is silently lost on exit |
| `mirrorEditedPartyMember()` | Let's Go stores party members twice | the edit vanishes on save for an LGPE party member |

### 6. Add the picker kind

See recipe 3.

---

## Recipe 3 — Add a picker kind

`include/UI/Dialogs/PickerDialog.h`:

```cpp
enum class PickerKind {
    Nature, Gender, Move, /* … */, PouchItemG3,
    DynamaxLevel,                                    // NEW
};
```

`src/UI/Dialogs/PickerDialog.cpp` — **three switches, and the compiler will not warn you about a
missing case** (these switches end in `return` after the switch, not in a `default:`), so add all
three:

```cpp
int pickerOptionCount(PickerKind kind) {
    switch (kind) {
        /* … */
        case PickerKind::DynamaxLevel: return 11;    // 0-10
    }
    return 0;
}

const char* pickerOptionLabel(PickerKind kind, int index) {
    switch (kind) {
        /* … */
        case PickerKind::DynamaxLevel: { static std::string s; s = std::to_string(index); return s.c_str(); }
    }
    return "?";
}

const char* pickerTitle(PickerKind kind) {
    switch (kind) {
        /* … */
        case PickerKind::DynamaxLevel: return "Select Dynamax Level";
    }
    return "Select";
}
```

The `static std::string s;` idiom is existing style for numeric labels — the returned pointer is
only used before the next call.

### If the kind needs filtering or reordering

Two more steps, and **both are mandatory**:

**(a)** Write a `buildXPickerOrder()` on `TrainerViewScreen` that fills `pickerOrder` (and optionally
`pickerLegalCount`), then set `pickerCount = pickerOrder.size()`:

```cpp
void TrainerViewScreen::buildAbilityPickerOrder(uint16_t species, uint8_t form,
                                                Enums::GameVersion group, uint16_t current) {
    pickerOrder.clear();
    const Pokemon::AbilitySlots slots = Pokemon::getAbilitySlots(species, form, group);
    uint16_t legal[3];
    const int nLegal = slots.distinct(legal);
    for (int i = 0; i < nLegal; ++i) pickerOrder.push_back(legal[i]);
    pickerLegalCount = static_cast<int>(pickerOrder.size());
    /* … then, when illegal edits are allowed, append every other id … */
    /* … then set pickerSel to the ROW holding `current` … */
}
```

**(b)** Add the kind to `TrainerViewScreen::pickerUsesOrder()`, the one predicate
`drawPickerDialog` and `pickerSelectedValue()` both ask:

```cpp
bool TrainerViewScreen::pickerUsesOrder() const {
    switch (pickerKind) {
    case PickerKind::Ability:
    case PickerKind::Species:
    /* … */
    case PickerKind::MyNewKind:                    // <- HERE
        return !pickerOrder.empty();
    default:
        return false;
    }
}
```

> **This is the sharp edge, and the source comment says so.** A kind that fills `pickerOrder` but is
> missing from that switch keeps labelling rows by index while the write uses `pickerOrder` — it names
> one value and writes another, with nothing on screen to show it.

And always read the chosen value through `pickerSelectedValue()` when applying — **never `pickerSel`
directly**. Once a search query is set, `pickerSel` indexes the VISIBLE (filtered) rows, and
`pickerSelectedValue()` is what maps visible row → row → `pickerOrder` value:

```cpp
const int value = pickerSelectedValue();   // -1 if nothing is selectable
```

---

## Recipe 4 — Add a panel (a new content view)

Goal: a new full-content view, reachable from the HOME menu.

### 1. Header

`include/UI/Panels/StatsPanel.h`:

```cpp
#ifndef UI_PANELS_STATS_PANEL_H
#define UI_PANELS_STATS_PANEL_H

namespace UI {
    class PKSEFramebuffer;
    class TrainerViewScreen;
}

namespace UI {
namespace Panels {
    void drawStats(UI::TrainerViewScreen& screen, UI::PKSEFramebuffer& fb,
                   int x, int y, int width, int height);
}
}

#endif
```

Forward-declare, do not include. Match the signature exactly — every content panel takes the
screen, the framebuffer and its rectangle. (The snippets here keep short local names for brevity;
real code follows `docs/CODING_STANDARDS.md` — a geometry name carries its subject, so `panelX`,
`panelWidth`, never bare `x` / `w`.)

### 2. Implementation

`src/UI/Panels/StatsPanel.cpp`. The house style for a content panel:

```cpp
#include "UI/Panels/StatsPanel.h"
#include "UI/TrainerViewScreen.h"
#include "UI/Common.h"
#include "UI/PKSEFramebuffer.h"
#include "Trainer/Trainer.h"

namespace UI {
namespace Panels {
    void drawStats(TrainerViewScreen& screen, PKSEFramebuffer& fb,
                   int x, int y, int width, int height) {
        // Card + header band. The band is a rounded rect with its bottom corners squared off
        // against the body, which is the pattern every content panel uses.
        fb.drawFilledRoundedRect(x, y, width, height, 16, Colors::Panel);
        fb.drawRoundedRect(x, y, width, height, 16, Colors::Border, 1);
        constexpr int hH = 46;
        fb.drawFilledRoundedRect(x, y, width, hH, 16, Colors::AccentDim);
        fb.drawFilledRect(x, y + hH - 16, width, 16, Colors::AccentDim);
        fb.drawText(x + 22, y + (hH - fb.lineHeight(TextStyle::Heading)) / 2,
                    "Statistics", Colors::Text, TextStyle::Heading);

        screen.touchButtons.clear();        // this view owns the taps while it is drawn

        // Rows.
        constexpr int rowPitch = 52, tileH = 46;
        const int tileW = std::min(width - 60, 860);
        const int tileX = x + (width - tileW) / 2;
        int ry = y + hH + 40;
        for (int i = 0; i < rowCount; ++i) {
            const bool selected = (screen.selectedItemIndex == i);
            fb.drawSoftShadow(tileX, ry, tileW, tileH, tileH / 2);
            fb.drawFilledRoundedRect(tileX, ry, tileW, tileH, 12,
                                     selected ? Colors::Primary : Colors::PanelAlt);
            fb.drawText(tileX + 22, ry + (tileH - fb.lineHeight()) / 2, label(i),
                        selected ? Colors::PrimaryText : Colors::Text);
            screen.touchButtons.push_back({ i, tileX, ry, tileW, tileH });
            ry += rowPitch;
        }
    }
}
}
```

Nothing to add to the Makefile — `src/UI/Panels` is already in `SOURCES`.

### 3. Add the view mode

`include/UI/TrainerViewScreen.h`:

```cpp
enum class ViewMode { Party, Boxes, Items, Storage, Trainer, Settings, Stats };
```

`src/UI/TrainerViewScreen.cpp` → `draw()`:

```cpp
#include "UI/Panels/StatsPanel.h"
/* … */
case ViewMode::Stats:
    Panels::drawStats(*this, fb, contentX, CONTENT_PANEL_Y, contentPanelWidth, CONTENT_PANEL_HEIGHT);
    break;
```

### 4. Reach it from the HOME menu

`src/UI/Panels/HomeMenuPanel.cpp` — add an entry to the icon row:

```cpp
struct Icon { const char* label; const char* glyph; int idx; };
Icon icons[4] = { {"Items","I",3}, {"Trainer","T",4}, {"Settings","S",5}, {"Stats","G",6} };
const int slot = rW / 4;                                    // was / 3
for (int j = 0; j < 4; ++j) { /* … */ }
// touch id stays 100 + idx  ->  106
```

You will also need a case in `drawMenuIcon` for `kind == 3`, or reuse an existing glyph.

`src/UI/TrainerViewScreen.cpp`, **three** places in the HOME-menu input:

```cpp
// (a) wrap-around in the icon row
if (homeMenuIndex >= 3) {
    if (buttonsDown & HidNpadButton_Left)  homeMenuIndex = (homeMenuIndex == 3) ? 6 : homeMenuIndex - 1;
    if (buttonsDown & HidNpadButton_Right) homeMenuIndex = (homeMenuIndex == 6) ? 3 : homeMenuIndex + 1;
    buttonsDown &= ~(HidNpadButton_Left | HidNpadButton_Right);
}

// (b) index -> mode.  THIS SWITCH EXISTS TWICE — once for the cursor, once for the tap handler.
case 6: selectedMode = ViewMode::Stats; break;

// (c) tap range
if (homeTap >= 100 && homeTap <= 106) { homeMenuIndex = homeTap - 100; /* … */ }

// (d) A -> enter
case 6: detailViewActive = true; selectedItemIndex = 0; break;
```

### 5. Input and hint

Add a `if (selectedMode == ViewMode::Stats) { … }` block in the entered-view section of `update()`,
and a hint string in `draw()`:

```cpp
} else if (detailViewActive && selectedMode == ViewMode::Stats) {
    instructions = "Up/Down: Select  |  B: Back";
}
```

> **The `homeMenuIndex → ViewMode` switch is duplicated** (cursor path and tap path). Update both,
> then check both by hand: select the new icon with the D-pad (and the left stick) and press A, then
> tap it. The two must land on the same view — a tap that opens a different view than the cursor is
> exactly the bug a missed second switch produces.

---

## Recipe 5 — Add a dialog

Goal: a centred modal card with its own input.

### 1. Header + implementation

`include/UI/Dialogs/MyDialog.h` — same forward-declaration shape as recipe 4, signature
`void drawMyDialog(TrainerViewScreen&, PKSEFramebuffer&)`.

`src/UI/Dialogs/MyDialog.cpp`:

```cpp
#include "UI/Dialogs/MyDialog.h"
#include "UI/Dialogs/DialogFrame.h"
#include "UI/Dialogs/EditControls.h"   // drawEditChoiceButton
#include "UI/TrainerViewScreen.h"
#include "UI/Common.h"
#include "UI/PKSEFramebuffer.h"

namespace UI {
namespace Dialogs {
    void drawMyDialog(TrainerViewScreen& screen, PKSEFramebuffer& fb) {
        constexpr int w = 540, h = 260;
        const int x = (fb.getWidth() - w) / 2;
        const int y = (fb.getHeight() - h) / 2;
        int cy = drawDialogFrame(fb, x, y, w, h, "My Dialog", Colors::Text);  // Colors::Red if destructive

        screen.touchButtons.clear();     // whoever draws last owns the taps

        fb.drawText(x + 28, cy, "Body text.", Colors::Text);
        fb.drawText(x + 28, cy + 34, "Secondary explanation.", Colors::TextDim, TextStyle::Caption);

        const int bw = 190, bh = TouchTargetMin, by = y + h - bh - 18;
        drawEditChoiceButton(screen, fb, x + w - bw - 20 - bw - 16, by, bw, bh, "B", "Cancel",  0);
        drawEditChoiceButton(screen, fb, x + w - bw - 20,           by, bw, bh, "A", "Confirm", 1);
    }
}
}
```

### 2. State flag

`include/UI/TrainerViewScreen.h`:

```cpp
bool myDialogActive = false;
```

### 3. Input, at the right precedence

`src/UI/TrainerViewScreen.cpp` → `update()`. Place the block so it is **reached before** anything it
should overlay, and **return** so it swallows everything below:

```cpp
if (myDialogActive) {
    const int touchedButton = touchedButtonId(touch);
    if (touchedButton == 1 && armTap(touchedButton, HidNpadButton_A)) return;   // drawn held, fires later
    if (touchedButton == 0 && armTap(touchedButton, HidNpadButton_B)) return;
    if (buttonsDown & HidNpadButton_B) { myDialogActive = false; return; }
    if (buttonsDown & HidNpadButton_A) {
        /* do the thing */
        hasUnsavedChanges = true;
        myDialogActive = false;
        return;
    }
    return;                     // owns input while open
}
```

### 4. Draw, at matching precedence

```cpp
if (myDialogActive) Dialogs::drawMyDialog(*this, fb);
```

Place it in the same relative order in `draw()` as in `update()`. **They must agree** — see chapter
08's overlay table.

### 5. Nav hint

```cpp
} else if (myDialogActive) {
    instructions = "A: Confirm  |  B: Cancel";
}
```

The keys go in the **screen's** nav bar — never in a strip inside the card. There is no
`drawDialogFooter()`, and a dialog that can appear over the details page names its keys through
`TrainerViewScreen::overlayNavHint()`, which both bars ask first. One nav bar per frame (chapter 08).

---

## Recipe 6 — Add an item to a list view

The Items panel is the reference for any list. Two functions must agree on geometry, and the source
says so:

```cpp
// ItemsPanel.cpp
// Single-column touch-friendly tiles (this geometry mirrors the nav math in
// TrainerViewScreen::update() — keep them in sync).
constexpr int rowPitch = 52, tileH = 46;
const int itemsPerPage = (itemsHeight - 106 - listSearchBoxHeight()) / rowPitch;
```

```cpp
// TrainerViewScreen::update()
int itemsPerPage =
    (CONTENT_PANEL_HEIGHT - 106 - listSearchBoxHeight()) / 52;   // rows that fit (mirror ItemsPanel)
if (itemsPerPage < 1) itemsPerPage = 1;
```

If you change the pitch, change both. A mismatch shows up as the cursor scrolling to a row that is
not on screen.

### The filtered-index pattern

The panel shows only items with `count > 0` that match the search query (if one is set), so
`selectedItemIndex` indexes the **filtered** view and must be mapped back before touching data.
`visibleItemIndices()` is the one funnel every consumer uses, which is why the Items search filters
inside it rather than beside it:

```cpp
std::vector<int> visible = screen.visibleItemIndices();      // raw indices worth showing
const InventoryItem& item = pouch[visible[i]];               // display
const int rawIdx = visible[selectedItemIndex];               // edit
```

**Re-clamp both the selection and the page every frame**, because an item can leave the list from
three different places (list remove, dialog remove, an edit to count 0):

```cpp
if (totalItems > 0) {
    if (selectedItemIndex >= totalItems) selectedItemIndex = totalItems - 1;
    if (selectedItemIndex < 0) selectedItemIndex = 0;
    currentPage = selectedItemIndex / itemsPerPage;   // re-derive, so the two cannot disagree
} else {
    selectedItemIndex = 0;
    currentPage = 0;
}
```

Without this, removing every item on the last page strands the cursor on an out-of-range page
("6/5") until the pouch changes.

### Naming an item

**Always** `Names::getItemNameFor(group, id)`. Never the modern table directly. Every generation
numbers items differently and the wrong table gives a plausible wrong answer — resolving a FireRed
bag item through the modern table named Potion (g3 13) "Dusk Ball" (modern 13), and only the list
panel branched, so the panel and the dialog disagreed on screen.

---

## Recipe 7 — Add a confirmation before a destructive action

Pattern used by every destructive flow in the app. Four pieces of state:

```cpp
bool  releaseConfirmActive = false;
int   releasePane = 0, releaseBox = 0, releaseSlot = 0;   // WHAT it acts on, captured at raise time
bool  releaseGroup = false;                               // single vs block
```

**Capture the target when you raise the prompt, not when you answer it.** The cursor can move while
the dialog is open (it cannot here, but the pattern must survive a future change that lets it).

The action itself:

```cpp
if (buttonsDown & HidNpadButton_A) {
    auto& slot = storageSlot(releasePane, releaseBox, releaseSlot);
    if (g_debugLogging && slot) {                       // LOG BEFORE THE MUTATION
        Utils::logEventToFile("RELEASE " + Utils::logSlot("at", releasePane, releaseBox, releaseSlot)
                            + " " + Utils::describePokemon(*slot, trainer));
    }
    slot.reset();
    hasUnsavedChanges = true;
    releaseConfirmActive = false;
    postStatus("Released.", 200);
    return;
}
```

The destructive dialog uses `Colors::Red` for the frame title and a red Confirm button:

```cpp
int cy = drawDialogFrame(fb, x, y, w, h, "Remove Item", Colors::Red);
drawGlyphButton(fb, remX, by, bw, bh, "A", "Remove", Colors::Red, Colors::White);
```

### When a warning is gated and when it is not

PKSE has three independent lossy-move warnings, kept apart because they describe three unrelated
losses — but **all three are gated by the one `g_moveWarn` setting**, which covers every bank
transfer warning in every generation:

| Warning | Loss | Severity |
|---|---|---|
| Gen 3 down-convert | rebuilds the PID, drops nickname/ribbons/held item | 1 (highest) |
| Poké Transporter (Gen 1/2 → later) | rerolls the IVs, invents a PID, floors the EXP, drops Stat Experience | 2 |
| Let's Go transfer | resets AV/EV training to 0, drops unlearnable moves | 3 — recoverable by re-training |

A move can match more than one (an LGPE mon into FireRed). **The most severe notice supersedes**;
stacking two dialogs on one action reads as a bug.

The first two used to warn *regardless* of the setting, on the grounds that they are the destructive
ones. That made the setting quietly narrower than its label — someone who had turned warnings off
was still stopped on every FireRed drop, with nothing on the settings screen to say why. How
destructive a transfer is decides what its DIALOG says, not whether a dialog appears at all.

---

## Recipe 8 — Add a nav-bar hint

`draw()`'s `instructions` chain is a big if/else. Add your branch **in precedence order** — the most
specific state first, because the chain stops at the first match.

```cpp
std::string instructions;
if (swapActive)                     instructions = "HOLDING  |  Arrows: Move Cursor  |  …";
else if (statEdit.dialogActive)     instructions = "…";
else if (myDialogActive)            instructions = "A: Confirm  |  B: Cancel";   // NEW
else if (details.active)            instructions = "…";
else if (detailViewActive)          { /* per-view */ }
else                                instructions = "Arrows: Navigate  |  A: Open  |  …";
drawNavBar(fb, instructions);
```

Format: `"Btn: Label  |  Btn: Label"`. Use the token spellings `buttonGlyph` knows (chapter 08); an
unknown token falls back to plain text rather than disappearing. A segment with no colon renders as
an accent state marker (`"HOLDING"`).

**Every badge you name becomes tappable for free**, so listing an action here is also how you give it
a touch control.

> The existing `instructions` chain has an acknowledged `TODO` in the source about the nested
> conditionals. If you are already refactoring that area, a table-driven version would be welcome;
> otherwise match the surrounding style.

---

## Recipe 9 — Make something tappable

Three lines, always the same shape.

**In `draw()`**, after drawing the thing:

```cpp
screen.touchButtons.push_back({ myId, bx, by, bw, bh });
```

**In `update()`**, map it onto an existing button:

```cpp
if (touchedButtonId(touch) == myId && armTap(myId, HidNpadButton_A)) return;   // confirm / open
// or, for a control that only nudges a value (a step button):
if (touchedButtonId(touch) == myId) buttonsDown |= HidNpadButton_Right;
```

**Do not write a second action for the tap.** Route it to the button the handler already understands
so there is one code path. A tap that confirms or opens something goes through `armTap`, which draws
the button held for `TAP_PRESS_FRAMES` before the press fires — acting in the tap's own frame gives
the user no sign of what they hit.

Rules:

- Minimum size `TouchTargetMin` (56 px) in both axes. Grid slots are already larger.
- The **whole row**, not just the glyph. Aiming at a 24 px circle is unreasonable; and for a nav
  badge, the label is the part that says what will happen — which is why `drawNavHints` makes the
  badge *and* its label one target.
- **Only one overlay may populate `touchButtons`.** Clear it at the top of the drawing function that
  owns input this frame.
- If the tap opens a **keyboard prompt**, open it from `update()`, never from `draw()` — the keyboard
  draws whole frames of its own (chapter 08, "The software keyboard").

---

## Recipe 10 — Add a per-game behaviour difference

You have a behaviour that must differ in one game. In order of preference:

**1. A virtual on the base class with a safe default.** This is the right answer almost always.

```cpp
// Trainer.h
/// Longest OT name this game accepts, in characters. Defaults to 12 (every Switch title);
/// Gen 3 overrides to 7.
virtual size_t getMaxTrainerNameLength() const noexcept { return 12; }
```

```cpp
// Trainer3FRLG.h
size_t getMaxTrainerNameLength() const noexcept override { return 7; }
```

The default must be the **safe** answer, so a format that has not overridden it behaves correctly
rather than dangerously.

**2. A capability predicate**, when the difference is "does this exist at all" and `0` is a real
value (chapter 06):

```cpp
if (p->hasBall()) editRow("Ball", Enums::getBallName(p->ball()), 19);
```

**3. A `getGameGroup()` branch**, when the difference is genuinely about *which* table or id space to
use — not about behaviour:

```cpp
pickerKind = (pokemon->getGameGroup() == GameVersion::RBY)   ? Dialogs::PickerKind::ItemG1
           : Enums::isGen3Group(pokemon->getGameGroup())     ? Dialogs::PickerKind::ItemG3
                                                             : Dialogs::PickerKind::Item;
```

Note `Enums::isGen3Group`, not `== GameVersion::FRLG`: Gen 3 has two groups (FR/LG and R/S/E) sharing
one item id space, and a comparison against one of them silently mis-handles the other. Better still,
prefer a capability predicate when the question is "does this format have X" rather than "which id
space".

When a branch like this appears in **more than one place**, hoist it into a named helper. That is
exactly why `pouchPickerKind()` and `Names::getItemNameFor()` exist:

```cpp
/// Which item-id space the pouch picker should offer. Asked in one place because the answer is
/// per generation and there are now three of them -- a ternary at each call site was already the
/// wrong shape, and the id spaces name each other's items without erroring.
Dialogs::PickerKind pouchPickerKind() const;
```

**Never** branch on the concrete class with a cast unless you genuinely need a member the base does
not expose — and if you find yourself wanting to, the missing base virtual is usually the real fix.

---

## Recipe 11 — Add a whole new screen

Rare, but here is the shape.

```cpp
// include/UI/MyScreen.h
namespace UI {
    class MyScreen : public UIScreen {
    public:
        MyScreen(/* what it needs */);
        void update(const PadState& pad, const TouchInput& touch) override;
        void draw(PKSEFramebuffer& fb) override;
        bool shouldExit() const override { return exitRequested; }
        bool hasSelectedThing() const { return selected; }     // how the caller learns the outcome
    private:
        bool exitRequested = false, selected = false;
    };
}
```

Drive it from a `UIManager` handler, exactly like the others:

```cpp
void UIManager::handleMyScreen() {
    MyScreen screen;
    fb.startFade();
    while (appletMainLoop() && running && !screen.shouldExit()) {
        padUpdate(&pad);
        touch.update();
        screen.update(pad, touch);
        screen.draw(fb);
        fb.drawFadeOverlay();
        fb.flush();
        if (screen.hasSelectedThing()) { /* … */ return; }
    }
}
```

Draw the chrome so it matches everything else:

```cpp
drawTitleBar(fb, "My Screen");
/* … content … */
drawNavBar(fb, "A: Select  |  B: Back");
```

`fb.startFade()` before the loop gives the same materialise-in transition every other screen has.

---

## The checklist before you call a UI change done

1. Does it **draw**? (`draw()` branch added, in the right overlay order)
2. Does it **respond**? (`update()` branch added, at matching precedence, with an early `return` if
   it is an overlay)
3. Is it **tappable**? (`touchButtons.push_back`, `touchedButtonId` → `armTap(…)` or `buttonsDown |= …`)
4. Is it **discoverable**? (nav-bar hint)
5. Does an edit set **`hasUnsavedChanges`**?
6. Does an entity edit call **`recalculateStats()` then `refreshChecksum()`**, and
   **`mirrorEditedPartyMember()`**?
7. Is a destructive action **logged before** the mutation, guarded by `if (g_debugLogging)`?
8. Does it work for **every game**, including Gen 1 (no nature, no ball, 20-slot boxes) and Gen 3
   (7-char names, ~70-glyph charset)?
9. Does the **left stick** do what the D-pad does on it? (Free through `update()`; anything with its
   own input loop must sample the stick itself.)
10. Is there **exactly one nav bar** on screen while it is up, naming the keys that actually work?
11. `python tools/syntax_check.py` **and** `--warnings`.
12. Drive it by hand on a console — buttons, stick and touch — if you touched a screen, panel, dialog
    or modal. A UI defect can sit behind perfectly correct logic, and nothing short of driving the
    screen finds it.
