# 08 — The UI layer

## Immediate mode, and what it costs you

There is **no widget tree**. Every frame, every screen clears and redraws itself from plain state.
There is no `Button` object, no event handler registration, no layout engine.

```
update(pad, touch)   ──>   mutate plain state
draw(fb)             ──>   read that state, emit primitives
```

The trade: **adding a control is two edits in two places, and keeping them in sync is manual.** A
button drawn in `draw()` does nothing until `update()` handles its input. A button handled in
`update()` is invisible until `draw()` renders it. Chapter 09 is a set of recipes that keep the two
halves together.

What you get in return: no lifecycle, no stale state, no invalidation bugs, and — because the whole
frame is a function of state — a frame can be recorded primitive by primitive and inspected like a
DOM, with no widget objects to reach into.

## `PKSEFramebuffer` — the drawing surface

`src/UI/PKSEFramebuffer.cpp` is **the only file in the tree that touches SDL, GL or NanoVG.** That is
deliberate and load-bearing: the whole UI layer can be compiled against a different implementation of
that one file (a recording one, say, on a PC) without a single `#ifdef` anywhere else.

SDL provides the window, the GL context and input. **It does not render.** NanoVG (vendored in
`nanovg/`, GL loaded by glad) does all the drawing: antialiased vector graphics, rounded rects,
alpha blending, image paints, and text through its own font atlas.

The public API was deliberately kept identical across the SDL_Renderer → NanoVG migration, so
screens and panels stay decoupled from the backend.

### Primitives

```cpp
void clear(Color color);
void drawPixel(int pixelX, int pixelY, Color color);
void drawRect(int rectX, int rectY, int rectWidth, int rectHeight, Color color);        // outline
void drawFilledRect(int rectX, int rectY, int rectWidth, int rectHeight, Color color);
void drawText(int textX, int textY, const std::string &text, Color color, TextStyle style = TextStyle::Body);
void drawSymbol(int symbolX, int symbolY, const std::string &symbol, Color color,
                TextStyle style = TextStyle::Body);
void drawImage(int imageX, int imageY, int sourceWidth, int sourceHeight,
               const unsigned char *imageData, int channels);
void drawImageScaled(int imageX, int imageY, int sourceWidth, int sourceHeight,
                     int destWidth, int destHeight, const unsigned char *imageData, int channels);
void flush();
```

`drawText` places text with `(textX, textY)` as its **top-left**, not its baseline — to centre a
label on a line, use `textYCenteredOn` (see "Rules: the UI layer" below). `drawSymbol` routes through
the fallback symbol font for glyphs Nunito lacks (♂ ♀ ★ ♥ ◀ ▶ ▲ ▼ ×).

### Rounded / vector primitives (the HOME look)

```cpp
void drawFilledRoundedRect(int rectX, int rectY, int rectWidth, int rectHeight, int cornerRadius, Color color);
void drawRoundedRect(int rectX, int rectY, int rectWidth, int rectHeight, int cornerRadius, Color color,
                     int thickness = 1);
void drawFilledCircle(int centerX, int centerY, int radius, Color color);
void drawCircle(int centerX, int centerY, int radius, Color color, int thickness = 1);
void drawFilledEllipse(int centerX, int centerY, int radiusX, int radiusY, Color color);
void drawPill(int pillX, int pillY, int pillWidth, int pillHeight, Color color);   // radius = height/2
void drawPillBorder(int pillX, int pillY, int pillWidth, int pillHeight, Color color, int thickness = 1);
void drawSoftShadow(int shadowX, int shadowY, int shadowWidth, int shadowHeight, int cornerRadius);
void drawVerticalGradient(int gradientX, int gradientY, int gradientWidth, int gradientHeight,
                          Color top, Color bottom);
```

A rounded rect has a `cornerRadius`; a circle has a `radius`. They are not the same thing, and the
parameter names keep them apart.

### Themed helpers and set pieces

```cpp
void drawCard(int cardX, int cardY, int cardWidth, int cardHeight);           // surface fill + border
void drawSelectionHighlight(int highlightX, int highlightY, int highlightWidth, int highlightHeight);
void drawHDivider(int dividerX, int dividerY, int dividerWidth);
void drawEgg(int centerX, int centerY, int size);                             // cream oval + teal spots
void drawShinyMark(int markX, int markY, int size, Color color);              // HOME's four-pointed sparkle
void drawPointerCursor(int tipX, int tipY, int headHeight, Color color);
void drawFolderIcon(int iconX, int iconY, int size, Color color, bool open = false);
void drawFileIcon(int iconX, int iconY, int size, Color color);
void drawSearchIcon(int iconX, int iconY, int size, Color color);
void drawEvolveArrow(int iconX, int iconY, int size, Color color);
void drawBackspaceIcon(int iconX, int iconY, int size, Color color);          // keyboard keys
void drawShiftIcon(int iconX, int iconY, int size, Color color, bool filled);
int  drawTypeBadge(int badgeX, int badgeY, const std::string &typeName, Color typeColor);  // returns width
void drawStatHexagon(int centerX, int centerY, int hexRadius, const float *values, int count,
                     float maxValue, Color fill, Color webColor, Color outline);
void drawSpriteIdle(int spriteX, int spriteY, int boxWidth, int boxHeight, int sourceWidth,
                    int sourceHeight, const unsigned char *data, int channels, float phase);
```

The icons are drawn as vector shapes rather than set in a font or loaded from romfs — see "Row icons
are drawn" under "Rules: the UI layer" below.

`drawStatHexagon`'s `values` must be in **HOME vertex order**: `[HP, Attack, Defense, Speed, Sp.Def,
Sp.Atk]` — HP at top, clockwise. Labels are the caller's job.

`drawSpriteIdle` adds a bob-and-breathe animation plus a ground shadow, driven by the frame tick;
`phase` offsets the timing so several sprites do not move in lockstep.

### Measurement, clipping, timing

```cpp
void measureText(const std::string &text, int &outWidth, int &outHeight, TextStyle style = TextStyle::Body);
int  lineHeight(TextStyle style = TextStyle::Body) const;
int  textYCenteredOn(int centerY, TextStyle style = TextStyle::Body) const;   // drawText Y for a centred label
void setClipRect(int clipX, int clipY, int clipWidth, int clipHeight);
void clearClip();
int  getWidth() const;    // 1280
int  getHeight() const;   // 720
float  getDeltaSeconds() const;
double getTimeSeconds()  const;
void startFade();          // call when a screen begins
void drawFadeOverlay();    // call each frame just before flush()
```

`measureText` is how everything centres and right-aligns:

```cpp
int valueWidth, valueHeight;
framebuffer.measureText(value, valueWidth, valueHeight, TextStyle::Body);
framebuffer.drawText(rowX + rowWidth - 18 - valueWidth, rowY + 11, value, Colors::Text);   // right-aligned
```

`setClipRect` needs an active frame; primitives outside the clip are not rendered. Always pair with
`clearClip()`.

### Text styles

```cpp
enum class TextStyle { Caption, Body, Heading, Title, Count };
```

`Body` is the default and matches the old single-size text, so existing `drawText(...)` calls are
unchanged. `Heading` and `Title` are faux-bold. The fonts are Nunito (primary) with Noto Sans
Symbols and Noto Sans Symbols 2 added as NanoVG **fallbacks**, which is what lets a species name
like `Nidoran♀` render in one call.

### Sprite texture caching

`SpriteManager` (`src/UI/SpriteManager.cpp`) lazily decodes PNGs from ROMFS via `stb_image` into raw
pixel buffers, LRU-bounded. It builds paths from ids and never enumerates the directory. `PKSEFramebuffer`
uploads those buffers to NanoVG image handles (`nvgCreateImageRGBA`), also LRU-bounded, **keyed by the
address of the pixel buffer**:

```cpp
std::map<const unsigned char*, int> imageCache;
void invalidateImage(const unsigned char* data);
static void onSpriteEvicted(const unsigned char* data);   // hooked to SpriteManager's evict callback
```

That coupling is necessary: when the sprite cache frees a buffer, its texture has to go too —
otherwise a later sprite allocated at that same address is drawn with the freed one's image.

Sprite cache keys pack `speciesId | shiny<<16 | icon<<17 | formId<<18`.

## Screen chrome

`include/UI/ScreenChrome.h` is header-only and holds everything shared between screens.

```cpp
constexpr int CHROME_CORNER_RADIUS = 18;   // curve on the content-facing edge of both bars
constexpr int HEADER_HEIGHT        = 64;
constexpr int NAV_BAR_HEIGHT       = 46;

void drawTitleBar(PKSEFramebuffer &framebuffer, const std::string &subtitle);
void drawNavBar(PKSEFramebuffer &framebuffer, const std::string &hint);
void drawNavHints(PKSEFramebuffer &framebuffer, int hintsX, int hintsWidth, int centerY, const std::string &hint);
void drawHomeTile(PKSEFramebuffer &framebuffer, int homeTileX, int homeTileY, int homeTileWidth,
                  int homeTileHeight, const std::string &label, bool selected, bool accent = false,
                  bool enabled = true);
void drawGlyphButton(PKSEFramebuffer &framebuffer, int boxX, int boxY, int boxWidth, int boxHeight,
                     const std::string &glyph, const std::string &label,
                     Color fillColor = Colors::PanelAlt, Color textColor = Colors::Text,
                     bool pressed = false);
void drawScrollbar(PKSEFramebuffer &framebuffer, int trackX, int trackY, int trackH, int contentH, int scroll);
int  buttonGlyph(PKSEFramebuffer &framebuffer, int badgeX, int centerY, const std::string &buttonToken,
                 bool measureOnly);
int  buttonGlyphWidth(PKSEFramebuffer &framebuffer, const std::string &buttonToken);
std::vector<std::string> wrapTextToWidth(PKSEFramebuffer &framebuffer, const std::string &text,
                                         int maxLineWidth, TextStyle style);
```

`drawGlyphButton`'s `pressed` draws the button HELD — a tint over the caller's fill plus a ring inside
its own rectangle, so nothing around it shifts and a red button stays recognisably red. It exists for
the deferred tap (see "The one-frame-late touch contract" below): a tapped button is shown held for a
few frames before its action runs.

Both bars are rounded **sheets** whose far edge runs off-screen, so only the edge facing the content
is curved: the header curves along its bottom, the nav bar along its top. A soft shadow does the
separating, which is why neither carries a hard divider rule.

### Controller badges and the hint string

`buttonGlyph` draws a badge shaped like the real button — face buttons are circles, shoulders are
rounded rects, `+`/`−` are circles with *drawn* bars (Nunito's `+` and `−` are far too thin at badge
size and `−` sits at x-height instead of centred), and the d-pad is a cross with the unused axis
dimmed.

Recognised tokens:

```
A  B  X  Y            face buttons (circle)
+  Plus  -  Minus     circle with drawn bars
L  R  ZL  ZR  L/R  ZL/ZR   rounded rect sized to the text
Arrows  D-Pad  Up/Down  Left/Right   d-pad cross (unused axis dimmed)
Left  Right  Up  Down  single-direction rounded square with a triangle
```

A token `buttonGlyph` does not recognise returns width `0`, and `drawNavHints` falls back to plain
text (`"Btn: Label"`) rather than silently dropping the button name and leaving a bare verb.

**The hint string is the contract.** Screens publish `"Btn: Label  |  Btn: Label"` and
`drawNavHints` lays it out:

```cpp
instructions = "Arrows: Navigate  |  L/R: Box  |  A: Details  |  X: Release  |  B: Back";
drawNavBar(fb, instructions);
```

A segment with **no colon** is a state marker and renders as accent text (`"HOLDING"`).

### Tappable badges

Every badge standing for exactly one button gets a hit region, so the nav bar doubles as on-screen
action buttons:

```cpp
struct NavHit { int hitX, hitY, hitWidth, hitHeight; uint64_t button; };
inline std::vector<NavHit> g_navHits;

uint64_t navTouchButton(const TouchInput& touch);   // edge-triggered; returns a button mask, or 0
```

and the screen folds it into `buttonsDown` at the very top of `update()`:

```cpp
u64 buttonsDown = controllerNavigation.apply(padGetButtonsDown(&pad), padGetButtons(&pad), /* … */)
                | navTouchButton(touch);   // nav-bar badges are tappable
```

(`controllerNavigation` is the left-stick-as-D-pad and held-repeat layer; see "Rules: the UI layer".)
A nav badge's hit region is the badge **and** its label, `TouchTargetMin` tall, because the label is
the part that says what will happen.

**There is one input path, not two.** A tap resolves to that button's press and no existing handler
changes, so a tap can never diverge from what the physical button does.

`L/R`, `ZL/ZR` and the d-pad badges stay **informational only** — a single badge cannot say whether
you meant L or R, and splitting one in half leaves each half far below a usable touch target.

`g_navHits` is cleared inside `drawNavHints`, not `drawNavBar`, so the last bar drawn in a frame is
the one whose badges are live.

### One bar per frame

**A dialog never lists its own controls.** The nav bar is the single place badges live, and whatever
**owns input** names the keys.

```cpp
std::string TrainerViewScreen::overlayNavHint() const;   // "" when no overlay owns input
```

Two things draw a bar — the screen, and the full-screen details page, which paints over it — so the
hints for any dialog that can appear over either have to come from one place. Both ask
`overlayNavHint()` first; only when it is empty does each fall through to its own view hints.

Three dialogs used to draw a strip inside their card *as well*: the file browser, the
save-destination dialog and the picker. That put two rows of controller badges on screen for one
dialog — and they disagreed. The browser card said `A: Open | B: Up / Close | X: All Files | L/R:
Page`; the bar behind it said `A: Open | Y: Search | X: Show All Files | B: Back`. Neither was
complete. `drawDialogFooter` no longer exists; `DialogFrame.h` says why in its place.

Two consequences worth knowing:

- **The screen skips its own bar while the details page is up.** That page paints an opaque gradient
  over the whole screen and draws its own, so the one underneath was invisible work — and
  indistinguishable, to a test, from the real double-bar bug.
- **The legality and ribbon overlays are deliberately absent from `overlayNavHint()`.** They dim the
  entire page, bar included, and carry their own `B / tap: close` line inside the card. Naming it on
  a bar the overlay has just dimmed would be the second hint this whole thing exists to remove.

**The invariant, stated so it can be checked: at most one full-width rounded sheet pinned to the
bottom edge per frame.** The nav bar is the only primitive of that shape, so counting them in a
recorded frame finds a double bar. The case that was reported, and the one to check first, is a picker
open over the details page.

## Colours and theming

`include/UI/Common.h`. Two families:

```cpp
namespace Colors {
    // Fixed literals — theme-independent
    constexpr Color Black, White, Gray, LightGray, DarkGray, Red, Green, Blue, Yellow,
                    Cyan, Magenta, Orange;
    constexpr Color PartnerHeart, PartyNumber, PartyBadge, PartyBadgeText;   // Let's Go specifics

    // Semantic — MUTABLE, swapped wholesale by applyTheme()
    inline Color Background, Panel, PanelAlt, Selected, Border, Text, TextDim,
                 Accent, AccentDim, Primary, PrimaryText, Warning, ShinyStar,
                 CursorMenu, CursorMove, CursorMulti;
}

enum class ThemeMode { Dark, Light };
inline ThemeMode g_themeMode = ThemeMode::Dark;
inline void applyTheme(ThemeMode mode);
```

Screens reference `Colors::Text` / `Colors::Panel` and pick up a theme change automatically on the
next frame, because everything redraws each frame. **Use the semantic colours**, not literals, for
anything the theme should follow.

The palette is HOME-inspired "warm on cool": indigo (`Accent`) is the hero, amber (`Primary`) is
used sparingly for the single primary or selected element, with `PrimaryText` as the dark ink drawn
on top of it.

Four colours are theme-aware **because they had to be** — a bright value that reads on the dark UI
was nearly invisible on light-mode white: `Warning`, `ShinyStar`, and the three cursor colours.

Red is reserved for genuine risk. In the settings view, benign toggles get an amber "On" pill and
the two that can damage real data (illegal values, game-save injection) get a red one.

### Layout constants

```cpp
constexpr int GRID_CURSOR_HEIGHT = 32;   // arrowhead height, absolute (NOT a fraction of the disc)
constexpr int TouchTargetMin     = 56;   // minimum comfortable touch target, in framebuffer px
```

`GRID_CURSOR_HEIGHT` is absolute because the Boxes view and the storage view size their cells differently
— scaling off the cell gave a visibly bigger cursor in one view than the other.

`TouchTargetMin` is the floor for anything tappable. The UI renders at 1280×720 on the ~6" handheld
screen, and touch points map 1:1 to pixels.

## Input

### Buttons

libnx's `PadState`, latched once per frame by `padUpdate(&pad)`. `padGetButtonsDown` is
edge-triggered (this frame's presses only).

```cpp
HidNpadButton_A / _B / _X / _Y
HidNpadButton_Up / _Down / _Left / _Right
HidNpadButton_L / _R / _ZL / _ZR
HidNpadButton_Plus / _Minus
```

`PadState` is a plain struct and libnx's readers are inline reads over it, so a `PadState` built in
memory drives a real screen exactly as the console's does. A synthetic pad is not a mock; it is the
same bytes the console hands over.

**The left stick is the D-pad.** Screens never read the stick themselves: `UI/NavigationRepeat.h`
converts it once at the top of `update()` (see "Rules: the UI layer"), so every handler below sees
ordinary D-pad bits.

### Touch

```cpp
class TouchInput {
    void update();                 // once per frame, after hidInitializeTouchScreen()
    bool isDown() const;
    bool justPressed() const;      // touch began this frame
    bool justReleased() const;     // touch ended this frame (use the start position)
    int  x() const, y() const;
    int  startX() const, startY() const;
    bool dragged() const;          // moved past a small threshold since the press
};
```

Edge-triggered to mirror `padGetButtonsDown`.

### The one-frame-late touch contract

**Touch targets are captured during `draw()` and hit-tested on the next `update()`.**

```cpp
// UI/Common.h -- lives there, not in TrainerViewScreen, because dialogs (the file browser) are
// drawn by more than one screen and a dialog cannot name a screen type without depending on it
struct TouchButton { int buttonId, hitX, hitY, hitWidth, hitHeight; };

// TrainerViewScreen
std::vector<TouchButton> touchButtons;                     // popup / dialog buttons
int touchedButtonId(const TouchInput &touch) const;        // id of a tapped button, or -1
struct TouchTarget { int pane, box, slot, hitX, hitY, hitWidth, hitHeight; };   // storage grid slots
std::vector<TouchTarget> storageTouchTargets;
```

A drawing function clears the list and repopulates it:

```cpp
screen.touchButtons.clear();
/* … for each row … */
screen.touchButtons.push_back({ itemIndex, tileX, rowY, tileWidth, tileHeight });   // id = absolute item index
```

**Only the active overlay populates `touchButtons`.** Whoever draws last owns the taps. That is why
every dialog's draw begins with `screen.touchButtons.clear()` — the modal underneath must not stay
live behind it.

The idiomatic consumer maps a tapped id back onto the *same button* the handler already understands,
so there is only ever one code path:

```cpp
switch (touchedButtonId(touch)) {          // the stat editor's step row
    case 34: buttonsDown |= HidNpadButton_ZL;    break;
    case 30: buttonsDown |= HidNpadButton_L;     break;
    case 31: buttonsDown |= HidNpadButton_Left;  break;
    case 32: buttonsDown |= HidNpadButton_Right; break;
    case 33: buttonsDown |= HidNpadButton_R;     break;
    case 35: buttonsDown |= HidNpadButton_ZR;    break;
    default: break;
}
```

**Do not write a separate action for a tap.** Fold it into `buttonsDown` and let the existing button
handler run.

**A tap that CONFIRMS or OPENS something is deferred, not folded in the same frame.** Setting the
selection and synthesising `A` in one frame runs the action before any frame has drawn the row
highlighted: the user gets no confirmation of what they hit, and on a glyph confirm (Cancel / Release)
nothing visibly moves at all — the dialog just vanishes. So the handler records the tap and returns:

```cpp
const int touchedButton = touchedButtonId(touch);
if (touchedButton == 1 && armTap(touchedButton, HidNpadButton_A)) return;
if (touchedButton == 0 && armTap(touchedButton, HidNpadButton_B)) return;
```

`armTap(buttonId, button)` stores `pendingTapButton` / `pendingTapButtonId` and starts
`pendingTapFrames` at `TAP_PRESS_FRAMES` (4 frames, ~66 ms — one frame is under the threshold where a
change reads as a change rather than a flicker). Meanwhile `drawEditChoiceButton` draws that id held,
and when the count runs out the top of `update()` ORs the button into `buttonsDown` — without
returning, so a physical button pressed meanwhile is still honoured. `SaveSelectScreen` and
`BackupSelectionScreen` have their own `armTap` of the same shape.

Two consequences of "one frame late": a tap on the very first frame a dialog is open does nothing
(harmless, imperceptible), and anything that opens a **blocking applet** must be deferred a frame so
its highlight renders first (chapter 03).

## The three UI component kinds

| Kind | Directory | Signature | Is |
|---|---|---|---|
| **Panel** | `UI/Panels/` | `void drawX(TrainerViewScreen&, PKSEFramebuffer&, int panelX, int panelY, int panelWidth, int panelHeight)` | a region of the main content area |
| **Dialog** | `UI/Dialogs/` | `void drawX(TrainerViewScreen&, PKSEFramebuffer&)` | a centred modal card over a scrim |
| **Modal** | `UI/Modals/` | `void drawX(TrainerViewScreen&, PKSEFramebuffer&)` | a **full-screen page** |

All three are **free functions in a nested namespace**, not classes. They own no state; they read
and write `TrainerViewScreen`. Their headers forward-declare `PKSEFramebuffer` and
`TrainerViewScreen` rather than including them, so the include graph stays shallow:

```cpp
namespace UI {
    class PKSEFramebuffer;
    class TrainerViewScreen;
}
namespace UI { namespace Panels {
    void drawItems(UI::TrainerViewScreen& screen, UI::PKSEFramebuffer& fb,
                   int x, int y, int width, int height);
} }
```

### Panels

| Function | File |
|---|---|
| `drawHomeMenu` | `HomeMenuPanel.cpp` |
| `drawPartyPokemon` | `PartyPokemonPanel.cpp` |
| `drawBoxPokemon`, `drawBoxSummaryPanel` | `BoxPokemonPanel.cpp` |
| `drawItems`, `pouchDisplayName` | `ItemsPanel.cpp` |
| `drawStorageView` + 12 popups | `StoragePanel.cpp` |

`StoragePanel.cpp` also owns the storage view's confirmations, which live there rather than in
`Dialogs/` because they are part of that view's flow: `drawStorageActionMenu`,
`drawStorageGroupMenu`, `drawStorageReleaseConfirm`, `drawStorageExitConfirm`,
`drawCreatorKeepConfirm`, `drawDetailsDiscardConfirm`, `drawTradeEvolveChoice`,
`drawTradeEvolveConfirm`, `drawTradePartnerTitlePicker`, `drawGen3ConvertConfirm`,
`drawVirtualConsoleTransferConfirm`, `drawLgpeTransferConfirm`.

`drawPartyPokemon` is the odd one out: it takes `const Trainer&` rather than the screen, because it
needs nothing else. Prefer that when a panel is genuinely read-only.

### Dialogs, and the shared frame

`include/UI/Dialogs/DialogFrame.h` gives every modal card the same look:

```cpp
constexpr int DIALOG_CORNER_RADIUS = 16;

inline int drawDialogFrame(PKSEFramebuffer &framebuffer, int dialogX, int dialogY, int dialogWidth,
                           int dialogHeight, const std::string &title, Color titleColor) {
    // scrim, soft shadow, rounded Panel card with a Border outline, Heading title at +24/+16,
    // then a 2px Accent rule at +56, inset by DIALOG_CORNER_RADIUS so it stops short of the curve
    return dialogY + 78;      // <- where content starts
}
```

There is **no `drawDialogFooter()`** — a dialog does not list its own controls (see "One bar per
frame" above), and `DialogFrame.h` says so where the function used to be.

`drawDialogFrame` **returns the content Y**, which is the idiom every dialog uses:

```cpp
constexpr int dialogWidth = 560, dialogHeight = 360;
const int dialogX = (framebuffer.getWidth() - dialogWidth) / 2;
const int dialogY = (framebuffer.getHeight() - dialogHeight) / 2;
int contentY = drawDialogFrame(framebuffer, dialogX, dialogY, dialogWidth, dialogHeight, "Edit Item",
                               Colors::Text);
screen.touchButtons.clear();
/* … draw content at contentY, advancing it … */
```

Destructive dialogs pass `Colors::Red` as the title colour.

### Shared edit controls

`include/UI/Dialogs/EditControls.h` keeps the two value editors (Edit Stat, Edit Amount)
**identical**, so they feel the same:

```cpp
struct EditStep { const char *glyph; const char *delta; int id; };
inline const EditStep EDIT_STEPS[6] = {
    {"ZL", "-100", 34}, {"L", "-10", 30}, {"Left", "-1", 31},
    {"Right", "+1", 32}, {"R", "+10", 33}, {"ZR", "+100", 35},
};

void drawEditStepRow(TrainerViewScreen &, PKSEFramebuffer &, int editStepRowX, int editStepRowWidth,
                     int rowY, int rowH);
void drawEditChoiceButton(TrainerViewScreen &, PKSEFramebuffer &, int boxX, int boxY, int boxWidth,
                          int boxHeight, const char *glyph, const char *label, int itemId,
                          Color fill = Colors::PanelAlt, Color textColor = Colors::Text);
```

**Reserved touch ids: 30–35 are the step buttons, 0 = Cancel (B), 1 = Confirm (A).** Pick something
else for a new control in one of those dialogs.

Note the comment on `drawEditChoiceButton`: it is deliberately **not** accent-filled, because
colouring "Confirm" as if selected implies a cursor that is not there — both buttons are always
available. It does draw itself HELD while its id is the pending tap (`pendingTapButtonId`).

### The reusable picker

One dialog serves twenty different "choose a value from a list" needs (`PickerKind`).

```cpp
enum class PickerKind {
    Nature, Gender, Move, Item, ItemG3, ItemG1, Level, Friendship, Ball, Ability,
    Language, Origin, MetLevel, MetLocation, Form, StatNature, Species,
    PouchItem, PouchItemG1, PouchItemG3
};

int         pickerOptionCount(PickerKind kind);
const char *pickerOptionLabel(PickerKind kind, int index);
const char *pickerTitle(PickerKind kind);
std::string pickerRowLabel(const TrainerViewScreen &screen, PickerKind kind, int value);  // what a row DRAWS
void        drawPickerDialog(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer);
```

State lives on the screen:

```cpp
bool             pickerActive;
Dialogs::PickerKind pickerKind;
int              pickerSel, pickerCount, pickerSlot;
std::vector<int> pickerOrder;      // row -> value indirection (filter/reorder). Empty = identity.
int              pickerLegalCount; // rows [0, pickerLegalCount) render GREEN
uint8_t          pickerMetVersion; // MetLocation: which game's location table names each id
uint16_t         pickerFormSpecies;// Form: whose forms are listed
bool             pickerMetIsEgg;   // MetLocation: true routes the pick to the EGG location
ListSearch       pickerSearch;     // the Y search box
std::vector<int> pickerFilteredRows;  // visible row -> row, while a query is set
```

Opening one is four or five lines:

```cpp
pickerKind  = Dialogs::PickerKind::Nature;
pickerCount = Dialogs::pickerOptionCount(pickerKind);
pickerSel   = pokemon->nature();
pickerActive = true;
```

`pickerOrder` is what makes the picker useful rather than a raw enum dump. Filtered/reordered kinds:

| Builder | Effect |
|---|---|
| `buildAbilityPickerOrder` | the species' legal ability slots sort to the top and render green |
| `buildMovePickerOrder` | learnable moves first; illegal ones still offered, PKHeX-style |
| `buildFormPickerOrder` | storable forms only (drops battle-only and forms this game lacks) |
| `buildGenderPickerOrder` | only genders the species can be — one row when fixed |
| `buildCreatorSpeciesOrder` | the open game's dex only (the creator) |
| `buildSpeciesPickerOrder` | the same dex filter, keyed on the **mon's** group (species edit) |

> **The sharp edge**: a kind that fills `pickerOrder` and is **missing from
> `TrainerViewScreen::pickerUsesOrder()`** would keep labelling rows by index while the write uses
> `pickerOrder` — naming one value and writing another, with nothing on screen to show it.
> `drawPickerDialog` and `pickerSelectedValue()` both ask that one predicate, so if you add a
> filtered kind, add it there.

`MetLocation` and `Form` return `""` from `pickerOptionLabel` and are labelled inside
`drawPickerDialog`, because they need mon-specific context (origin version, species) the free
function cannot see.

### The details modal

`Modals/PokemonDetailsModal.cpp` — a full-screen three-column page, HOME's "Check Summary" editor.

```
┌─ top bar: name, variant, gender, shiny, Lv/No., "Unsaved changes", close ─┐
├──────────────┬─────────────────────────┬─────────────────────────────────┤
│ LEFT         │ CENTER                  │ RIGHT                            │
│ render       │ stat table (0-5)        │ moves 0-3   (ids 10-13)          │
│ type badges  │ shiny (6) nature (7)    │ held item   (id 14)              │
│ scrollable   │ gender (8) level (9)    │                                  │
│ info rows    │ + hexagon (Y cycles)    │                                  │
│ (ids 15-33)  │                         │                                  │
└──────────────┴─────────────────────────┴─────────────────────────────────┘
```

**Field ids are a flat integer space** shared by the cursor, the touch ids and the edit dispatch:

| Range | Column | Meaning |
|---|---|---|
| 0–5 | centre | the six stats |
| 6, 7, 8, 9 | centre | shiny, nature, gender, level |
| 10–13 | right | move slots 0–3 |
| 14 | right | held item |
| 15–33 | left | Ability (15), Friendship (16), Egg (17), Met Lv (18), Ball (19), Language (20), Origin (21), Pokerus (22), Nickname (23), EXP (24), Met (25), Form (26), Stat Nature (27), Met Date (28), Egg Loc (29), Egg Date (30), Fateful (31), Species (32), Evolve / Trade Evolve (33) |
| 94, 99 | — | ribbon overlay, close button |

The left column is **built at draw time** into `details.leftOrder`, in draw order, so conditional
rows appear only when actually shown and the cursor visits exactly what is on screen:

```cpp
std::vector<int> leftOrder;
auto editRow = [&](const char *label, const std::string &value, int fieldIndex) {
    leftOrder.push_back(fieldIndex);          // even when scrolled off screen
    const int rowY = contentY - scroll;
    const bool isSelected = (selectedField == fieldIndex);
    if (isSelected) selectedRowY = contentY;
    if (rowVisible(rowY)) {
        if (isSelected) { /* selection pill */ }
        /* label left-aligned, value right-aligned via measureText */
        screen.touchButtons.push_back({ fieldIndex, leftX + 8, rowY, leftWidth - 16, rowHeight });
    }
    contentY += rowHeight;
};
/* … */
screen.details.leftOrder = leftOrder;         // hand the nav its draw-order field list
```

`row(...)` is the read-only sibling — same layout, no id, no highlight, not in `leftOrder`.

Navigation reads that list rather than a hardcoded range, and skips rows that exist but cannot be
edited:

```cpp
std::vector<int> leftFields = details.leftOrder;
if (leftFields.empty()) leftFields = { /* a fallback list */ };   // first frame, before a draw has run
const bool skipGender = pokemon && !genderEditable(*pokemon);
const bool skipNature = pokemon && !pokemon->hasNature();
```

Which rows exist is decided by **asking the entity** (`hasMetData()`, `hasEggData()`,
`hasMetDate()`, `hasBall()`, …), never by naming games — see "Rules: the UI layer".

### The edit baseline

Editing is **live** — a picker's choice is applied straight to the entity — so "unsaved changes"
needs a baseline to diff against:

```cpp
void snapshotEditTarget();   // capture the target's bytes (on open, and again on X = Save)
bool pokemonEditDirty();     // does the target differ from the snapshot?
void restoreEditTarget();    // roll back to the snapshot
void closeDetailsModal();    // reset all modal state and close
```

`X` saves (re-snapshots), `B`/close discards after a confirmation. A brand-new creator mon asks
Keep/Discard instead, because every field of it is "unsaved" by nature.

## `TrainerViewScreen`'s state block

The header carries a large flat block of **public** `bool` / `int` state that panels, dialogs and
modals read and write directly.

> This is the current, acknowledged state-management approach. **Follow it** rather than introducing
> a new pattern, unless refactoring is the task.

The biggest cohesive clusters are grouped into nested structs; the rest are flat, single-purpose,
and default-initialised in place so the constructor only wires up its arguments.

```cpp
enum class ViewMode { Party, Boxes, Items, Storage, Trainer, Settings };
enum class CursorMode { Menu, Move, Multi };     // storage cursor: red / blue / green
enum class EditSource { Party, Box, Bank };

ViewMode selectedMode; int currentPage, selectedCategory, selectedBoxIndex, selectedPartyIndex;
bool detailViewActive; int selectedItemIndex;
int  homeMenuIndex;            // 0 Boxes 1 Party 2 Storage | 3 Items 4 Trainer 5 Settings
ControllerNavigation controllerNavigation;   // left stick as D-pad + held repeat
int  settingsSelectedRow, trainerSelectedRow, pendingTrainerEdit;
bool swapActive; int swapSourceBox, swapSourceSlot;
bool pendingHeaderRename;

std::unique_ptr<Trainer::Bank> bank;
CursorMode cursorMode; int storageFocusPane, stSaveSlot, stBankBox, stBankSlot;
// no stSaveBox: the save pane's BOX is selectedBoxIndex above, shared with the Boxes view
std::string storageStatus; int storageStatusFrames;
std::vector<std::unique_ptr<Pokemon::Pokemon>> moveMon;   // the carried BLOCK
std::pair<int,int> selectDimensions;                      // anchor while selecting; width×height while carrying
bool currentlySelecting; int selectPane, selectBox;
int  heldPane, heldFromBox, heldFromSlot;

struct StatEditState  { … } statEdit;
struct DetailsState   { … } details;
struct CreatorState   { … } creator;

bool hasUnsavedChanges, saveConfirmActive, saveInjectConfirmActive, itemEditDialogActive, …;
std::vector<TouchButton> touchButtons;
std::vector<TouchTarget> storageTouchTargets;
```

`moveMon` deserves a note. A Move-mode pick-up is just a **1×1 block**, so this is the *only* carry
state — there is no separate "held single Pokémon" to keep in sync, and every guard that asks "are
we holding something?" (compaction, sort, exit, box rename) answers for both cases at once:

```cpp
bool carrying() const { return !moveMon.empty(); }
int  carriedCount() const;                     // non-null cells
const Pokemon::Pokemon* firstCarried() const;
```

A null cell in `moveMon` is a **hole** — the source slot was empty, or it was locked and
deliberately left behind.

## Overlay precedence

Both `update()` and `draw()` order overlays, and **the two orders must agree**: the thing drawn last
is the thing on top, and the thing that grabs input first is the thing that owns it.

`update()` — early `return` after handling, so an open overlay swallows everything below:

```
+ (exit)  →  picker  →  details modal  →  item edit  →  item remove confirm
          →  stat edit  →  save confirm / inject confirm  →  storage menus and confirms
          →  view-specific input  →  HOME menu
```

(Abridged: the file browser and the PKSM import preview/result also own input while up.)

`draw()` — main content first, then bottom-up:

```cpp
if (details.active)                    Modals::drawPokemonDetailsModal(*this, framebuffer);
if (pickerActive)                      Dialogs::drawPickerDialog(*this, framebuffer);      // OVER the modal
if (itemEditDialogActive)              Dialogs::drawItemEditDialog(*this, framebuffer);
if (itemRemoveConfirmActive)           Dialogs::drawItemRemoveConfirm(*this, framebuffer);
if (statEdit.dialogActive)             Dialogs::drawStatEditDialog(*this, framebuffer);
if (saveConfirmActive)                 Dialogs::drawSaveConfirmDialog(*this, framebuffer);
if (saveInjectConfirmActive)           Dialogs::drawSaveInjectConfirm(*this, framebuffer); // OVER the picker
if (storageMenuActive)                 Panels::drawStorageActionMenu(*this, framebuffer);
if (groupMenuActive)                   Panels::drawStorageGroupMenu(*this, framebuffer);
if (releaseConfirmActive)              Panels::drawStorageReleaseConfirm(*this, framebuffer);
if (storageExitConfirmActive)          Panels::drawStorageExitConfirm(*this, framebuffer);
if (creator.keepConfirmActive)         Panels::drawCreatorKeepConfirm(*this, framebuffer);
if (details.discardConfirmActive)      Panels::drawDetailsDiscardConfirm(*this, framebuffer);
if (tradeEvolveChoiceActive)           Panels::drawTradeEvolveChoice(*this, framebuffer);
if (tradeEvolveConfirmActive)          Panels::drawTradeEvolveConfirm(*this, framebuffer);
if (tradePartnerTitles.active)         Panels::drawTradePartnerTitlePicker(*this, framebuffer);
if (gen3ConvertConfirmActive)          Panels::drawGen3ConvertConfirm(*this, framebuffer);
if (virtualConsoleTransferConfirmActive) Panels::drawVirtualConsoleTransferConfirm(*this, framebuffer);
if (lgpeTransferConfirmActive)         Panels::drawLgpeTransferConfirm(*this, framebuffer);
if (fileBrowser.active)                Dialogs::drawFileBrowser(fileBrowser, framebuffer, touchButtons);
if (pksmImport.previewActive)          Dialogs::drawPKSMImportPreview(*this, framebuffer);
if (pksmImport.resultActive)           Dialogs::drawPKSMImportResult(*this, framebuffer);
// status pill LAST — it can be raised from inside the modal, which draws after the main body
```

Because only the last drawn overlay repopulates `touchButtons`, this ordering is also what makes
touch land on the right thing.

## Search

`UI::ListSearch` (`include/UI/ListSearch.h`) is the one search box, used by every list that has one,
so a qualifying list never rolls its own. Putting it in one component is also what stops a user who
learned the picker's search from having to learn the file browser's separately.

**The rule: a DIALOG that asks the user to pick an element out of a list or array gets a search box;
the two screens that pick WHAT TO OPEN do not.** Every picker kind (species, move, met/egg location,
form, item, ball, nature, …), the Items pouch and the file browser have one. The **save/title picker**
and the **Save Backups screen** deliberately do not: those are steps in the boot flow choosing a
target rather than element pickers, and each is a short list that fits on screen whole.

| List | Key | Component | How it filters |
|---|---|---|---|
| Picker (all twenty kinds) | `Y` | `screen.pickerSearch` | Rebuilds `pickerFilteredRows`; `pickerSel` then indexes visible rows |
| File browser | `Y` | `fileBrowser.search` | `refresh()` re-lists the folder through the query; `..` always survives |
| Items pouch | `Y` | `screen.itemSearch` | Inside `visibleItemIndices()`, the one funnel every consumer already used |
| Box / Bank | `X` → Find | storage action menu | Highlights matches and dims the rest; a grid is not filtered |

### The rules

1. **`Y` opens it, everywhere it exists.** There is no per-list key override: `drawListSearchBox`
   draws `Y: Search` and takes no label to change it.
2. **A key the search claims loses whatever it did before — MOVE that action, do not leave it
   behind.** The Items pouch's Add Item sat on `Y`. The search branch returns first, so adding an item
   became unreachable while the nav bar still advertised it, and no touch target reached it either;
   nothing failed, the action was simply gone. It is on `-` now — what `-` already means in Storage:
   bring something IN. When moving an action, check that the new key AND its nav badge both do it, and
   that neither key still triggers the other's effect.
3. **Accepting an EMPTY query clears the filter.** That is the whole contract, so no list needs a
   second key and no user can get stuck in a filter.
4. **Matching is case-insensitive, and every whitespace-separated term must appear, in any order** —
   "couple nursery" finds "Nursery Couple". Substring, not prefix, so "ball" finds "Poke Ball".
5. **Filter on the string the row DRAWS.** `Dialogs::pickerRowLabel()` is shared by the filter and the
   draw for exactly this reason: the Met Location picker resolves a name through the mon's origin
   version and the Form picker through its species, so filtering on the underlying value would hide
   rows the user can plainly see.
6. **A filtered-to-nothing list must say so.** Every one prints "Nothing matches that search" plus its
   tally ("0 of 632"). A blank panel reads as missing data — and was once reported as exactly that.

### Both exclusions had a search box, and both are gone

The save/title picker's sat on `X`, because that screen's `Y` already opens a save FILE — the one
place the shared key differed, over a grid of at most a handful of game tiles all on screen at once.
The Save Backups screen's sat on `Y`, over a list whose row 0 is an ACTION ("Create New Backup") that
had to be exempted from every query for the screen to stay usable — a filter needing a permanent
exception to the thing it filters is the shape of a control that does not belong. The
`searchButtonLabel` parameter that existed only to spell that `X` went with them. Dropping the backup
box also put the eighth row back inside its card: at `LIST_MAX_VISIBLE = 8` the list had run 10px past
the card's bottom edge. The file browser the save picker raises with `Y` is a selection dialog and
keeps its search.

### The trap: two index spaces

Once a query is set, a list has TWO index spaces: the rows on screen and the rows in the data.
**The picker's `pickerSel` indexes the VISIBLE rows**, and nothing may use it as an index into the
full list — with "nurse" typed, visible row 0 is some row far down the real one, and applying
`pickerSel` directly writes whatever value happens to sit at position 0 instead. The picker keeps
the spaces apart with `pickerRowForVisibleIndex()` and `pickerSelectedValue()`, and every apply site
(there are ~15) goes through the latter. Getting it wrong names one thing on screen and writes
another, with nothing visible to show for it. The file browser and the Items view dodge this entirely
by filtering at the point the list is BUILT, so there is only ever one index space; prefer that shape
where a list is cheap to rebuild.

### Box / Bank search highlights, it does not filter

A box is a grid the player arranged and storage is positional, so hiding slots would move everything
else. Matches keep an accent ring, the rest dim, and the cursor jumps to the first match. It is
reached from the storage action menu (`X` → Find) rather than a key of its own, because `Y`, `X`, `ZL`
and `ZR` are all taken in that view.

### Egg locations live in bank 6, and the egg picker offers them

A report said BDSP's "Nursery Couple" could not be found — correctly: `getLocationTable()` returns
bank 0 (places), egg sources are bank 6 (people: the Nursery Couple, "a treasure hunter", Riley…), and
the egg picker enumerated only the former. `getMetLocationName()` could always DISPLAY them, so an
imported egg read correctly while nothing could select one. The Location picker now carries
`pickerMetIsEgg`, and with it set offers the egg sources too (`Names::getEggSourceLocationTable`). The met-location picker still shows bank 0
alone, which is the deliberate editorial choice: those are not places a Pokemon is met.

**Which is why the MET list's empty state names the Egg Loc row.** The same reporter searched
"nursery" in the *met* picker and found nothing — correct, and useless: the entry was one row away
with nothing on screen to say so. When a met-location search matches no place but DOES match an egg
source for that origin, the empty state names the entry and says to set it on the Egg Loc row. Being
right about where a value lives is not the same as helping someone find it.

For BDSP the table genuinely holds "Nursery Couple" **twice**, at 60006 and 60010 — PKHeX's own
`text_bdsp_60000_en.txt` verbatim, not a generator bug — so do not "dedupe" it.

## The software keyboard

```cpp
// Utils/Keyboard.h
struct KeyboardResult { bool accepted = false; std::string text; };
KeyboardResult promptText(const std::string& header, const std::string& guide,
                          const std::string& initial, int maxChars,
                          const std::function<void(const std::string &)> &textChanged = nullptr);

struct NumberResult { bool accepted = false; int value = 0; };
NumberResult promptNumber(const std::string& header, int initial, int minValue, int maxValue);

// The console's own keyboard (swkbd). The only way to type Japanese, Chinese or Korean.
KeyboardResult promptSystemText(const std::string& header, const std::string& guide,
                                const std::string& initial, int maxChars);
```

**PKSE draws its own keyboard** (`UI/Dialogs/KeyboardDialog.h`), in the app's theme. The call is still
synchronous — every call site reads *prompt, then apply*, exactly as it did against swkbd — but what runs
underneath is PKSE's:

- `UIManager` registers `runKeyboard` with `Utils::setKeyboardPresenter` in its constructor and clears it
  in its destructor. `promptText` / `promptNumber` build a `Utils::KeyboardRequest` and hand it over;
  with no presenter registered they fall back to swkbd.
- `runKeyboard` opens a `Dialogs::KeyboardState`, publishes it (`setActiveKeyboard`) and runs a loop of
  its own: `padUpdate` / `touch.update` → `keyboard.update` → draw `keyboardBackdrop` → `drawKeyboard` →
  `flush`. `keyboardBackdrop` is the screen whose `update()` raised the prompt — every screen loop sets it
  just before calling `update()` — so **the screen keeps drawing under the keyboard.** That is the whole
  reason for doing this: swkbd owns the display, so nothing PKSE drew could react to what was typed.
  `appletMainLoop()` is pumped every frame, so the console never sees a hang.
- **The loop shares `UIManager`'s `pad` and `touch`**, so the edge state carries straight across. The
  press that raised the prompt is still held on the keyboard's first frame, which is not an edge, so it
  is not typed. In the other direction, after OK or Cancel the loop keeps drawing the screen until every
  answering button is released and one more frame has cleared the release edges: the screen reads the
  same pad the moment `promptText` returns, and a `+` still held from OK would otherwise reach it as a
  press — which closes the app. Stick directions are not waited on, because a drifting stick never lets go.

The card sits above the nav bar (its top is `cardTop()`, 334 at 1280×720): a field showing the header,
the text with its caret and a character tally, over four rows of `TouchTargetMin`-tall keys. Widths are
counted in half keys so the wide bottom-row keys stay integers. Text prompts get three pages — letters,
symbols, accents — cycled with ZL/ZR or the page keys; the symbols include the games' own `♂ ♀ … ×`.
`promptNumber` gets a number pad instead, **opened with the old value selected** so the first digit
replaces it, with a sign key only when `minValue < 0`.

| Input | Does |
|---|---|
| D-pad / left stick | Move between keys (held: repeats after 24 frames, then every 4). The keyboard runs its own loop, so it samples the stick itself (`AnalogNavigation`) |
| A | Press the key (held: repeats on typing keys only) |
| B | Delete. A *fresh* B on an empty field cancels; a held one stops at empty |
| Y / X | Space / Shift. The Shift key cycles once → locked → off |
| L / R | Move the caret |
| ZL / ZR | Previous / next page |
| + / − | OK / Cancel |
| Tap | A typing key fires at once; OK and the hand-off key fire after `TAP_PRESS_FRAMES`, drawn held. A tap in the field moves the caret |

While the keyboard is up, `Dialogs::activeKeyboard()` is non-null, and the two bars that could be on
screen — `TrainerViewScreen::overlayNavHint()` and the save picker's — ask it first. One bar per frame,
as everywhere. A list that must stay readable keeps its rows above `cardTop()`: the picker narrows its
list window so the selection stays among the rows the user can see.

**It has no input method.** The `あ漢한` key hands the text to `promptSystemText`. Whatever is accepted
there is the answer, with no second OK; a cancel comes back to PKSE's keyboard with `waitingForRelease`
set, so the buttons that answered swkbd are not typed here as well.

### Searching while typing

`ListSearch::promptForQuery(listName, FilterTiming, queryChanged)`. With `WhileTyping` the query follows
every edit and `queryChanged` re-filters under the keyboard; **a cancel puts the old query back** and runs
`queryChanged` once more for it. With `OnAccept` the query changes once, when the prompt is accepted.

| List | Timing | Why |
|---|---|---|
| Picker | while typing | re-filters from the value selected before typing, so a cancel lands back on it |
| Items | while typing | resets the cursor per key, and restores it if the query ends where it began |
| Box / Bank | while typing, highlight only | the cursor jumps on accept only — a jump moves `Bank::currentBox`, an unsaved bank change |
| File browser | on accept | `refresh()` re-reads the folder off the card, too slow to repeat per key in a big one |

### The rules

1. **Only from `update()`, never `draw()`.** The keyboard draws whole frames of its own; started mid-draw
   it would nest them inside a half-built one.
2. **Never launch swkbd twice in one frame.** A library applet cannot be torn down and relaunched back to
   back. This now binds `promptSystemText` alone — PKSE's keyboard is not an applet.
3. **Treat a cancel as "no change", not as an empty string.** Returning `""` for a cancel would blank
   whatever the user was editing, which is the opposite of what they asked for.

`maxChars` is a character limit (the games specify characters); the keyboard counts code points and
flashes the tally when a character does not fit.

The returned text is **not** validated against the destination game's character set. The caller must
still encode it and handle the characters that do not map — `canStoreNickname` /
`canStoreBoxName`.

---

## Rules: the UI layer

The rules below are the ones the rest of this chapter leans on, each with the reasoning and the
defect it came from. The body above describes the machinery; this is what it must not be bent into.

### The shape

Immediate-mode style rendered via **NanoVG on GL**, wrapped by `PKSEFramebuffer`.
`UI::UIManager::run()` (`src/UI/UI.cpp`) drives a screen sequence: **UserSelection → TitleSelection →
BackupSelection → TrainerView**. Every screen implements `UI::UIScreen` (`update(pad)` / `draw(fb)` /
`shouldExit()`). `TrainerViewScreen` is the main editor and composes **Panels** (`src/UI/Panels/`),
**Dialogs** (`src/UI/Dialogs/`) and **Modals** (`src/UI/Modals/PokemonDetailsModal`). Its header holds
a large flat block of public `bool`/`int` state that panels and dialogs read and mutate directly — the
current, acknowledged approach; follow it rather than introducing a new pattern unless refactoring is
the task.

Rendering goes through `PKSEFramebuffer` (`src/UI/PKSEFramebuffer.cpp`), NanoVG-backed (vendored in
`nanovg/`, GL via glad), text in the bundled **Nunito** (`romfs:/fonts/Nunito.ttf`). SDL2 is still
linked but only provides window, GL context and input. The public API (`clear` / `drawRect` /
`drawFilledRect` / `drawText` / `drawImage` / `drawImageScaled` / `flush`) was deliberately kept
identical across the NanoVG migration so screens stay decoupled from the backend. Both caches —
`SpriteManager`'s decoded pixels and `PKSEFramebuffer`'s NanoVG images — are LRU-bounded, and cache
keys pack `speciesId | shiny | icon | formId`.

The HOME-style UI overhaul is **complete**; what remains is polish.

### Sprites are named by PokeAPI id, and there are two lookups

Sprite files are named by **PokeAPI id**: base species by national dex number (`925.png`, `925s.png`
shiny), alternate forms by PokeAPI's 10000+ form ids, mapped in `FormSpriteMapping`. Since the
fetcher (`tools/gen_hdsprites.py`) mirrors the whole upstream tree, **every render already exists on
disk** — adding a form is one mapping row, no re-fetch. `SpriteManager` builds paths from ids and
never enumerates the directory.

**Two lookups, not one.** `getFormSpriteId()` returns a `uint32_t` and covers the *variant* families
(a base plus regional forms, megas, fusions). PokeAPI keys ~200 renders by **name** instead — the
*peer* families, where no form is the default: `201-a`, `493-fire`, `666-meadow`,
`869-ruby-cream-berry-sweet`, `585-summer`. Those have no numeric id, so `getFormSpriteName()` returns
a string stem (`""` otherwise) and `SpriteManager` tries it first. Four traps:

- **A form id is not guessable — look it up.** The ids are not contiguous per generation and do not
  follow dex order; a wrong-but-existing id renders a different Pokemon with no error. Resolve against
  `https://pokeapi.co/api/v2/pokemon/<id>/` and check the returned `name`. Near-duplicate names are the
  sharp edge: `10118` is `zygarde-10-power-construct`, while plain `zygarde-10` is `10181`.
- **Form *indices* come from PKHeX, not from the id order.** ~90 species have Mega forms with real
  indices in the personal table, so they must be named and drawn correctly even though the picker
  never offers them. PKHeX's `FormConverter` decides ordering and has real exceptions: single-mega
  species put Mega at form 1, but Charizard/Mewtwo/Raichu use Mega X/Y, Absol/Garchomp/Lucario use
  Mega + Mega Z, Slowbro's Mega *precedes* its Galarian form, and
  Meowstic/Magearna/Tatsugiri/Zygarde/Greninja/Floette each have their own list. Derive indices from
  `FormConverter`, then check them against `formCount` in `PersonalInfoTable`.
- **Form 1 is not reliably the regional variant.** Both tables group Alolan/Galarian/Hisuian/Paldean
  species into fall-through `case` blocks keyed on `formId == 1`. Indices are assigned in the order
  forms joined the *series*, so a species that already had an alternate form keeps it at 1 and the
  regional variant lands later: **Slowbro** is Normal/Mega/Galarian, **Darmanitan** is
  Standard/Zen/Galarian/Galarian Zen. Check `formCount` before adding to one of those groups; anything
  above 2 needs its own `case`. When PKHeX and intuition disagree, the tiebreak is `PersonalInfoTable`:
  Slowbro form 1 = Shell Armor proves it is the Mega.
- **PokeAPI's default variety is not always game form 0.** For every species but one they agree, so
  `getFormSpriteId` short-circuits form 0 to the bare species id. **Maushold (species 925) is the
  exception**: PokeAPI's `925` is Family of *Four* while the game numbers Family of *Three* as form 0,
  so it is special-cased ahead of that short-circuit (form 0 → `10257`, form 1 → `925`).

Not every form has art: PokeAPI has no entry for `poltchageist-artisan`, `sinistcha-masterpiece`, the
Let's Go partner Pikachu/Eevee, the PLA Lord/Lady bosses, Arceus's Legend Plate, Mothim's cloaks,
Sinistea/Polteageist Antique, or Scatterbug/Spewpa's patterns (only Vivillon's are drawn). **When art
genuinely does not exist, add no mapping** and let `SpriteManager` fall back to the base species. A
missing *shiny* is normal too — the cap Pikachu have none, correctly.

### Naming a form, drawing a form, and offering a form are three different questions

`FormNames::getFormName` names *every* form a buffer can contain and `FormSpriteMapping` draws every
one that has art — battle-only included, because the details view has to render what is there, and an
unnamed form showing as "Form N" is how a wrong index hides. Only what may be *selected* is filtered,
by `Pokemon::isBattleOnlyForm` (`include/Pokemon/FormInfo.h`) via `buildFormPickerOrder`. Note
`formCount` counts defined forms, not storable ones.

**The picker offers permanent forms only, with no override.** A temporary form — one the game creates
in battle or while riding and then discards — is never selectable, and `g_allowIllegalEdits` does
**not** lift it: that setting is for values the games can hold but shouldn't (a 255 EV, an off-dex
species), whereas a temporary form gets overwritten regardless. **Mega Evolution and Primal Reversion
count as temporary in every game, Z-A included** — PKHeX exposes megas in its Z-A dropdown, but that
is editor permissiveness, and PKHeX's own tables class every mega as battle-only. Take PKHeX as truth
for form *indices*, not for editor policy. The mon's current form is always listed even when
temporary, so an existing one stays visible and reversible. When auditing form coverage, do **not**
skip hidden forms — that is how Castform, Cramorant and Eternatus stayed unnamed.

### The left stick is the D-pad, everywhere the D-pad navigates

`UI/NavigationRepeat.h` turns the stick into one D-pad direction at a time (`AnalogNavigation`) and
adds held-repeat (`ControllerNavigation`), applied ONCE per screen at the top of `update()`, so every
handler below sees ordinary D-pad bits and none of them reads the stick. **Anything that reads the pad
on its own path must sample the stick too**: the on-screen keyboard runs its own loop, so it ORs
`AnalogNavigation`'s bits into its presses and its held set, sampled every frame — with the stick held,
the keyboard's cursor must move exactly as it does for the same D-pad direction. L/R repeat only where
they NAVIGATE (the picker, the file browser, the stat and item editors, view paging); where they ACT
(Randomize IVs, legality) they stay single-press.

### One nav bar per frame

**A dialog never lists its own controls.** The bottom bar (`UI::drawNavBar`) is the single place
controller badges live, and whatever OWNS INPUT names the keys — that is what
`TrainerViewScreen::overlayNavHint()` answers, and both things that draw a bar ask it first. The full
account, with the defect it came from and its two corollaries, is "One bar per frame" above. While
the keyboard is up it owns the bar (`Dialogs::activeKeyboard()`, asked first).

### PKSE draws its own keyboard

`UI/Dialogs/KeyboardDialog` is themed and sits behind the unchanged synchronous
`Utils::promptText` / `promptNumber`. `UIManager::runKeyboard` is the registered presenter: it pumps
frames of its own, drawing `keyboardBackdrop` — the screen whose `update()` raised the prompt — under
the card, so that screen stays live. That is what lets a search filter while typing
(`ListSearch::FilterTiming::WhileTyping`; a cancel restores the old query). It shares `UIManager`'s
`pad`/`touch`, so no press leaks across either way, and **holds the screen until the answering buttons
are released** — a `+` still held from OK would otherwise reach the screen and close the app. While it
is up it owns the nav bar, and a list that must stay readable keeps its rows above `cardTop()`. It has
no input method: Japanese, Chinese and Korean go through its hand-off key to `Utils::promptSystemText`
(swkbd), still a library applet with the one-per-frame rule. Two lists do not filter per key,
deliberately: the file browser (`refresh()` re-reads the card) and Box/Bank, which highlights live but
jumps only on accept — a jump moves `Bank::currentBox`, which is an unsaved bank change. The details
are "The software keyboard" above.

### Row icons are DRAWN, not set in text

The file browser used font glyphs — a solid triangle for a folder, a bullet for a file — and a
triangle in a list reads as "expand this row", which is not what A does there.
`PKSEFramebuffer::drawFolderIcon` / `drawFileIcon` draw a manila folder and a page with a turned
corner, joining `drawEgg`, `drawShinyMark` and `drawPointerCursor` as bespoke vector icons: crisp at
any row height, themed by the caller's `Color`, nothing added to romfs. Both anchor top-left in a
`size`×`size` box and centre their shape inside it, so swapping one for the other moves nothing.

`drawSearchIcon` carries the sharpest version of the lesson: it *was* a stroked circle with a short
horizontal stub beside it — two individually correct primitives that together read as a lower-case
`o` followed by an underscore. A magnifier needs its handle **diagonal, round-capped and thicker than
the rim**, starting where the cap's back edge meets the rim's INNER edge — any further in and the cap
intrudes on the glass as a chip out of the lens. **Look at an assembled icon, do not just describe
it**: every shape is an exact signed-distance form, so the same numbers can be rasterised on a PC and
viewed. `nvgLineCap` is state and nothing in that file wraps state in `nvgSave` / `nvgRestore`, so a
round cap must be set back to `NVG_BUTT` or the next stroke inherits it. The `..` row gets the
**open** folder variant, whose front panel is a splayed trapezoid rather than a nudged rectangle,
because at a 24px row a sideways shift is two pixels and reads as a rendering slip.

### Text that can outgrow its card is wrapped with `UI::wrapTextToWidth`

`wrapTextToWidth` (`ScreenChrome.h`) measures with `measureText`, never a character budget — Nunito is
proportional, and a Japanese name has no spaces to break at. It breaks inside a word only when the word
cannot fit whole, and then on a UTF-8 boundary. The legality overlay drew each finding as one line, and
Layer 3's sentences ran out of its 760px card. **A card holding wrapped text is SIZED from the wrapped
lines**, and one that still cannot fit says how many entries it left out rather than cutting them
silently.

A consequence for anyone checking drawn text: a wrapped message is several draw calls, so a phrase
from late in it can be split across two drawn lines. Look for a phrase from the START of the message —
a later one failing to match reads as the message missing when it is merely wrapped.

### A label is centred on its capitals: `PKSEFramebuffer::textYCenteredOn(centerY, style)`

`drawText` anchors the TOP of the line box, and NanoVG mixes two normalisations — the baseline sits
`ascent/(ascent-descent) × size` below that top while glyphs scale per EM — so neither a hand-picked
offset nor `centerY - lineHeight/2` puts capitals on a centre line. Hand-picked `-15` / `-11` offsets
on one screen left its titles 5px above their status dots, measured on a console screenshot.

**A list's visible row count is derived from the space above the nav bar, never a constant.** The same
screen's fixed 8 ran its last row under the bar and drew its footer entirely off screen.

### A capability predicate is the only honest way to ask what a format HAS

`hasNature`, `hasAbility`, `hasBall` and the rest default **true** because Gen 1 is the exception;
`hasHandler` defaults **false** because Gen 6 introduced the handling trainer and Gens 1–5 have none.
They exist because 0 is a real value for every one of those fields (chapter 06).

### A gate that names games instead of asking the entity will be wrong about the next one

The details page gated its Egg Loc / Egg Date rows on `modernFmt`, spelled
`!isGen3Group(group) && group != GG` — correct for the two formats it names and silently wrong for
Gens 1 and 2, which fell straight through it and drew both rows for formats that have no egg-location
field at all. `hasEggData()` and `hasMetDate()` ask the record instead. The second exists because
**`hasMetData()` cannot answer for both halves**: Crystal records a met location and a met level and
NO DATE, so a single predicate would either hide a row Crystal can fill or draw one it cannot. Every
one of those rows read `(none)` — which is exactly what the row says when the field exists and is
unset, so nothing on screen distinguished the two.

The HT row is the same argument in reverse: the getter cannot answer the question. An empty `htName()`
means "never traded" in a format that has a handler and "no such field" in one that does not, so a row
hiding itself whenever the name is empty tells a Sword/Shield owner nothing about a Pokemon that has
not been traded yet. The HT row shows for every format that has one (`hasHandler()`), reading `(none)`
when unset, and its label says which of OT/HT is the **active** handler — that is what the field
means, since the game gives friendship, affection and memories to whichever is in charge.

### Species is editable, and changing it re-points four other fields

`applySpeciesChange` is the one place that does it: form is clamped to something the new species has
(a form past its `formCount` is a Bad Egg in-game), gender is constrained to that species' ratio, the
ability moves to the same SLOT so a hidden-ability mon stays hidden, and EXP is re-derived from the
kept LEVEL because the growth rate is per species. Moves are deliberately **not** sanitized — an
illegal move on a swapped species is exactly what the legality checker exists to report, and deleting
it would repair the evidence out from under it. The species list is the same dex-filtered one the
creator uses (`buildSpeciesPickerOrder`), keyed on the **mon's** game group rather than the save's,
because a bank slot holds a foreign mon.

### DLC level is REPORTED, not detected, for Sword/Shield and Scarlet/Violet

Neither save records a DLC entitlement; every readable signal is a proxy and both proxies were wrong
for somebody. Block *presence* claims Crown Tundra for everyone (a patched game allocates the dex
blocks licence or no licence), and block *content* under-reports for anyone who owns a DLC but has not
yet seen a Pokemon in it. Both games therefore report their fullest content set unconditionally.
Nothing gates on the value — it is the title-bar label and the event log — so this decides what is
displayed and nothing else. **Legends: Z-A is deliberately excluded**: it stores a real
`SAVE_REVISION` u64, so its label is read rather than guessed, and the value genuinely drives which
Pokedex bits get written (`zaSecondMegaSlot`).

### There is one save-box cursor, not one per view

`selectedBoxIndex` answers "which box of this save is open" for the Boxes view and the Storage view's
save pane alike. The Storage pane used to keep a second copy (`stSaveBox`) that nothing reconciled:
walk to box 5 in Boxes, switch to Storage, and you were looking at box 1. Both were seeded from the
game's persisted current box at load and both written back, so they agreed at the start and end of a
session and nowhere in between — which is why it read as intermittent.

The BANK pane's box is genuinely separate (different container, 200 boxes) and lives **in the bank's
own image**, as `Bank::currentBox` — a trailing `CBOX` section, appended after the box names and
needing no version bump. **That is what makes moving it an unsaved bank change**, which is the point:
`hasChanged()` is `serialize() != savedImage`, so the bank image is the only place a cursor position
can sit and still reach the Save/Discard prompt. Save keeps it; Discard puts it back via `load()`. A
`settings.cfg` key was tried first and could not work in either direction: a setting is written the
moment it changes, so a cursor move became a persisted decision with no way to undo it — and no way to
save it either, since a box change alone left `hasChanged()` false. **The section goes AFTER the
names**: an older PKSE looks for `NAMS` immediately past the record table, so leading with `CBOX`
would make it find that instead and quietly drop every box name.

### The box summary panel carries the legality verdict, bottom-left

The editor page has always had a verdict, but that is a page you open one Pokemon at a time — finding
the single problem in a box meant opening thirty. The panel answers while the cursor moves:
*"Legality: no problems found"* (green) or *"Legality: N issue(s)"* (red). **"no problems found",
never "Legal"** — Layer 3 errs toward silence, so a clean report means nothing was found, not that
HOME will accept it (chapter 11). It is deliberately NOT a tap target: the whole panel is already one
(open the editor), and a second target inside it would make one corner behave differently; the issue
LIST stays on `R` in the editor.

**The edit hint moved from centred to right-aligned to make room, and it had to** — the panel is 452px
wide, a centred hint leaves the left 159px, and the label measures 203, so the two overlapped by 44px.
That is arithmetic nobody can eyeball from the source: the invariant is that the verdict label's right
edge stays left of the hint's left edge, and it is worth measuring rather than trusting.

### Settings rows are rows of ONE table

Label, value, on-state and destructiveness are four columns of the same array, and the
`SETTINGS_ROW_*` constants are the one list of row indices that both the drawing code and the A-button
handler read. That is a response to a real defect: the badge colours were a separate chain of
`index ==` tests, so inserting the auto-legalize row moved the labels and left the chain behind, and
every badge from there down lit for its NEIGHBOUR's setting. **A second list of row numbers is the
thing to refuse**, not the specific wrong number. Chapter 09, recipe 1, walks through adding a row.

### One setting gates every bank transfer warning

"Bank Storage Move Warning" (`g_moveWarn`) gates the Gen 3 down-convert, the Poke Transporter run out
of Gen 1/2, and the Let's Go AV/EV reset, in every generation. The first two used to warn regardless,
on the grounds that they are the destructive ones; that made the setting quietly narrower than its
label, so a user who had turned warnings off was still stopped on every FireRed drop with nothing on
screen to say why. How destructive a transfer is decides WHICH dialog is shown when a move matches more
than one (Gen 3, then Transporter, then Let's Go) and what it SAYS — never whether a dialog appears at
all.
