# 03 — The runtime: startup, the frame loop, screens

## `main()`, in order

`src/main.cpp` is ~145 lines and every line is ordered on purpose. With the logging and error
branches stripped, it is:

```cpp
int main()
{
    Utils::loadSettings();        // FIRST. Gates every log sink below.
    logInfoToFile("Initializing PKSE...");
    Utils::cleanupOldLogs();

    nsInitialize();                                   // title metadata. Fatal if it fails.
    accountInitialize(AccountServiceType_Application);// user accounts (save enumeration). Fatal.
    romfsInit();                                      // bundled sprites + fonts. Non-fatal.
    plInitialize(PlServiceType_User);                 // the console's shared CJK fonts. Non-fatal.

    SDL_SetMainReady();
    SDL_Init(SDL_INIT_VIDEO);     // window + GL context + input ONLY. No SDL_image, no SDL_ttf.

    UI::SpriteManager::init();
    UI::SpriteManager::getSprite(25, false);          // smoke test: can we load Pikachu?

    {
        UI::UIManager uiManager;
        uiManager.run();
    }   // UIManager (and its SDL-backed framebuffer) destroyed HERE, before SDL_Quit

    UI::SpriteManager::cleanup();
    UI::SystemIcons::cleanup();
    SDL_Quit();
    romfsExit();
    plExit();                     // AFTER SDL_Quit: fontstash held pointers into pl's buffers
    accountExit();
    nsExit();
    return 0;
}
```

Five ordering constraints worth knowing:

1. **`loadSettings()` is first**, before the first log line. `g_debugLogging` gates every sink, so
   reading it any later means early-init diagnostics follow the compiled-in default instead of the
   user's choice — either losing exactly the init lines a bug report needs, or writing a file the
   user switched off. It is safe this early: it only `fopen`s `sdmc:/PKSE/settings.cfg` (libnx
   mounts `sdmc` before `main`), `applyTheme` just swaps colour globals, it needs no service, no
   ROMFS and no SDL, and nothing inside it logs.
2. **`ns` and `account` are fatal; ROMFS and `pl` are not.** Without ROMFS the app runs with no
   sprites; without `pl` every Latin script still renders and only CJK text draws blank.
3. **`pl` stays open for the whole run.** The shared-font buffers belong to the service and NanoVG
   is handed them with `freeData = 0` (chapter 08), so `plExit()` must come after `SDL_Quit()` has
   torn down the NanoVG context that points into them.
4. **`UIManager` lives in a nested scope** so its destructor (and its SDL-backed framebuffer's) runs
   before `SDL_Quit()`. Destroying a GL context after `SDL_Quit` is a crash.
5. **`cleanupOldLogs()` is gated with the sinks.** A run that writes no logs has no business
   deleting them either; turning logging off freezes the log directory as it stands.

## The screen loop

`UI::UIManager::run()` (`src/UI/UI.cpp`) is a sequence of nested `while` loops, one per screen, not
a state machine with a table. Each loop is the same handful of lines:

```cpp
while (appletMainLoop() && running && !screen.shouldExit()) {
    padUpdate(&pad);              // libnx: latch this frame's buttons
    touch.update();               // our own edge-triggered touch reader
    keyboardBackdrop = &screen;   // what the on-screen keyboard draws underneath itself
    screen.update(pad, touch);    // handle input, mutate state
    screen.draw(framebuffer);     // redraw EVERYTHING from that state
    framebuffer.drawFadeOverlay();// screen-transition veil (no-op once elapsed)
    framebuffer.flush();          // present + advance frame timers
}
```

`appletMainLoop()` is libnx's "is the app still allowed to run" check. **Anything that blocks for
long enough to stop pumping it makes the console decide the app has hung** — the HOME button stops
answering. That single fact is why long work is handed out in frame-sized slices and why the
keyboard runs a loop of its own rather than blocking (below).

`keyboardBackdrop` is there because PKSE draws its own keyboard (`UI/Dialogs/KeyboardDialog`).
`Utils::promptText`/`promptNumber` are synchronous; `UIManager::runKeyboard`, the registered
presenter, pumps frames of its own inside the `update()` that asked, drawing the backdrop screen
under the keyboard card so that screen stays live — which is what lets a search filter while it is
typed. It shares `UIManager`'s `pad` and `touch`, so the press that raised the prompt is not typed
into it and the press that answers it does not reach the screen. Chapter 08 has the rest.

### The screen graph

```
              run()
                │
                ▼
      handleSaveSelection()          SaveSelectScreen
                │                    (users + title grid; Y = open a save file)
      ┌─────────┴──────────────┐
      ▼                        ▼                    + = quit -> running = false
 handleBackupSelection()   handleExternalSave()
      │                        │
      ▼                        │
 BackupSelectionScreen         │
      │                        │
      ▼                        ▼
 handleTrainerView(...)  ──>  TrainerViewScreen  (the editor; everything happens here)
```

Each handler returns when its screen finishes, unwinding to `run()`, which rebuilds the picker (so
the save list is re-enumerated).

**There are two entry points into the editor, not one variation of one.** They differ in what
exists, not just in a parameter:

| Entry | Has | Lacks |
|---|---|---|
| `handleTrainerView` | title ID, account, backup directory | — |
| `handleExternalSave` | an opened trainer and its file path | title ID (`0`), account, backup tree |

`handleExternalSave` is how a Game Boy, GBA, DS or 3DS save opens. A loose file has no title ID and
belongs to no account, so it skips backup selection entirely and passes `titleId = 0`. That zero is
what `TrainerViewScreen::externalFileSession` keys on, and it collapses the save-destination picker
down to one option: write the file back where it came from.

The file is **opened before the picker is left**. The file browser runs `Save::openExternalSave` on
the pick — the open *is* the check, so nothing passes a lighter test and then fails in silence — and
hands the already-parsed trainer across:

```cpp
// UI.cpp — handleExternalSave, the essential shape
void UIManager::handleExternalSave(std::unique_ptr<Trainer::Trainer> trainer, const std::string &label,
                                   const std::string &path)
{
    if (!trainer) { logErrorToFile("No opened save came with the picked file", path.c_str()); return; }
    // No title id: 0 is passed, and the file path travels as the "backup dir" so the write-back
    // knows which file it came from.
    TrainerViewScreen trainerScreen(*trainer, label, path, 0, AccountUid{}, false);
    ...
}
```

The trainer is owned through the **base pointer** because the concrete class is not known until the
probe chain runs — thirteen formats across seven generations arrive this way.

### `std::visit` and `TrainerVariant`

`Save::readTrainerInfo()` returns a `std::variant` over the seven Switch `Trainer*` classes, because
the concrete type is only known at runtime from the title ID:

```cpp
using TrainerVariant =
    std::variant<Trainer7LGPE, Trainer8SWSH, Trainer9LZA, Trainer9SV, Trainer8LA, Trainer8BDSP, Trainer3FRLG>;
```

`handleTrainerView` unpacks it with `std::visit`, which gives a concrete reference that binds to the
`Trainer&` the editor wants:

```cpp
TrainerVariant trainerVariant = readTrainerInfo(backupDir.c_str(), titleId);
std::visit([&](auto &trainer) {
    TrainerViewScreen trainerScreen(trainer, titleName, backupDir, titleId, userUid, loadedFromCart);
    while (appletMainLoop() && !trainerScreen.shouldExit() && !trainerScreen.hasRequestedExit()) { ... }
}, trainerVariant);
```

The loose-file classes are deliberately **not** in the variant. They never go through the title-ID
dispatch, so putting them there would add states that path can never produce — and they could not
join anyway: a variant is a value type, and they are large, polymorphic and non-copyable. Their write
path dispatches on `getGameGroup()` and `static_cast`, never RTTI.

## The screen contract

```cpp
// include/UI/UIScreen.h
class UIScreen {
public:
    virtual ~UIScreen() = default;
    virtual void update(const PadState &pad, const TouchInput &touch) = 0;
    virtual void draw(PKSEFramebuffer &framebuffer) = 0;
    virtual bool shouldExit() const { return false; }
};
```

Three methods. That is the whole interface. Implementers:

| Screen | File | Role |
|---|---|---|
| `SaveSelectScreen` | `src/UI/SaveSelectScreen.cpp` | Combined user + title picker (JKSV-style) |
| `BackupSelectionScreen` | `src/UI/BackupSelectionScreen.cpp` | Pick an existing backup or make a new one |
| `TrainerViewScreen` | `src/UI/TrainerViewScreen.cpp` | The editor. ~7200 lines. Everything. |

`TrainerViewScreen` additionally exposes `hasRequestedExit()` (the `+` button = quit the app) as a
separate signal from `shouldExit()` (`B` = go back one screen), because they unwind different
distances.

### Golden rules for `update()` and `draw()`

**`draw()` must not mutate anything that decides behaviour.** It *does* legitimately write
`screen.touchButtons` and `details.leftOrder` — those are draw-time captures consumed next frame,
which is the deliberate one-frame-late contract described in chapter 08. Nothing else.

**Keyboard prompts are called only from `update()`, never `draw()`.** The loop is
update → draw → flush, so at `update()` time no NanoVG frame is open. PKSE's keyboard draws whole frames
of its own and swkbd suspends the app; either one started mid-draw would leave a half-built frame open
underneath it.

**At most one blocking applet per frame.** Launching a second immediately after the first returns —
both inside one `update()` — crashes on hardware; a library applet cannot be torn down and
relaunched back to back. For multi-field input use a single combined field, or spread prompts across
frames. This is why the code has *deferred edit* flags:

```cpp
// A box-name pill was tapped last frame: the header highlight has now been drawn once, so it is
// safe to open the (blocking) rename keyboard. Doing this inline with the tap suspended the app
// before the highlight ever rendered.
if (pendingHeaderRename) {
    pendingHeaderRename = false;
    if (selectedMode == ViewMode::Storage) {
        if (storageFocusPane == 0) renameBox(selectedBoxIndex); else renameBankBox(stBankBox);
    } else renameBox(selectedBoxIndex);
    return;                      // consume the whole frame
}
```

The same pattern appears as `pendingTrainerEdit` for the Name/Money rows. When you add anything that
opens a keyboard from a highlightable row, follow it: set a `pending*` flag, `return`, and act on
it at the top of the next `update()`.

## Global runtime state

`include/Globals.h` holds the handful of process-wide flags. `g_` on a mutable global is the only
prefix in this codebase. The settings are `inline bool`, persisted to `settings.cfg` by
`Utils::saveSettings()` and loaded by `Utils::loadSettings()`.

| Global | Default | Settings row | Meaning |
|---|---|---|---|
| `g_autoBackupEnabled` | `true` | Auto-Backup on Load | "Load from Title" makes a new timestamped folder each time (kept indefinitely — backups are never auto-pruned); off reuses one `Working/` copy. |
| `g_allowIllegalEdits` | `false` | Allow Illegal Values | Lifts the legal EV/AV caps (0–252 / 0–200 → 0–255). Does not lift the form picker's permanent-forms-only rule. |
| `g_autoLegalizeTransfers` | `false` | Auto-Legalize Unsupported Transfers | Rebuilds a Pokémon entering a game with no official transfer route as a legal native of it (chapter 10). |
| `g_moveWarn` | `true` | Bank Storage Move Warning | Confirm before a bank move that loses data. Covers **every** transfer warning in every generation — the Gen 3 down-convert, the Poke Transporter run out of Gen 1/2 and the Let's Go AV/EV reset. |
| `g_injectToGameSave` | `false` | Allow Inject Backups to Game Save | Unlocks writing an **older backup** over the live save. Does *not* gate saving a title-loaded session back — that is always allowed. |
| `g_debugLogging` | `false` | Enable Debug Logging | Gates **every** log sink. |
| `LOG_RETENTION_DAYS` | `30` | — | `constexpr`, not a setting. How long rotated logs are kept. |
| `BASE_SAVE_DIRECTORY` | `sdmc:/PKSE` | — | `constexpr`. |

Theme is separate: `UI::g_themeMode` in `UI/Common.h`, swapped by `UI::applyTheme()`, and its row
names its mode (Dark/Light) rather than toggling on and off. The display language has an eighth row,
defined but not drawn while `Names::DISPLAY_LANGUAGE_SELECTABLE` is false (chapter 07).

`g_injectToGameSave`'s narrow scope is deliberate and documented in `Globals.h`. Saving a session
that was loaded *from* the live save back *to* it is always allowed and needs no toggle — that is
simply what a save editor does, and the data being overwritten is the data just read. What the
toggle gates is opening an *older backup* and writing it over the current save, which rolls the game
backwards and loses everything played since.

`g_moveWarn` is one switch over all three warnings on purpose. Exempting the destructive ones would
make the setting quietly narrower than its label — it used to gate the Let's Go warning alone, so a
user who had turned warnings off was still stopped on every FireRed drop. How destructive a transfer
is decides which dialog appears and what it says, never whether one appears.

### Adding a setting

Settings rows are **rows of ONE table**: label, value, on-state and destructiveness are four columns
of parallel arrays in `drawSettingsView()`, and the `SETTINGS_ROW_*` constants at the top of
`TrainerViewScreen.cpp` are the one list of row indices that both the drawing code and the A-button
handler read. The badge colours used to be a separate chain of `index ==` tests, so inserting a row
left the chain behind and every badge from there down lit for its NEIGHBOUR's setting. **A second
list of row numbers is the thing to refuse.**

Worked in full in [chapter 09, recipe 1](09-ui-recipes.md):

1. `include/Globals.h` — declare `inline bool g_myFlag = false;` with a comment saying *why*.
2. `src/Utils/Settings.cpp` — a `strcmp` branch in `loadSettings()`, an `fprintf` in `saveSettings()`.
3. `src/UI/TrainerViewScreen.cpp`, top of file — add a `SETTINGS_ROW_*` constant and bump
   `SETTINGS_ROW_COUNT`. **Language must stay the last row**: it is hidden by stopping
   `SETTINGS_ROW_VISIBLE_COUNT` one row short, and a `static_assert` enforces that it is last.
4. `drawSettingsView()` — add the row to all four arrays (`labels`, `values`, `rowIsOn`,
   `rowIsDestructive`). Destructive means it can damage real data and lights **red**; benign toggles
   light amber.
5. The `ViewMode::Settings` input block — add the toggle branch on your `SETTINGS_ROW_*` constant,
   `postStatus()` what changed, and keep the `Utils::saveSettings()` call at the end.

There is a `static_assert` in `drawSettingsView` that stops the build if the rows no longer fit the
fixed-height panel, and it is asserted on the DEFINED count, not the visible one, so it keeps holding
on the day the Language row comes back. That is intentional — the settings view has no scrolling, so
a new row must either shrink the rows or add scrolling, and the assert forces that decision instead
of drawing past the panel edge. It has fired twice: the Language row as a seventh and the
auto-legalize row as an eighth, after which the pitch came down to 50 + 8 px (558 of 560 used). The
row height is also the touch target, and 50 is still above the ~44 px minimum.

## Logging

```cpp
Utils::logInfoToFile("message");
Utils::logInfoToFile("message", "context");
Utils::logErrorToFile("message");
Utils::logEventToFile(line);   // [EVENT] — what the USER did
Utils::logTest(line);          // sdmc:/PKSE/trace.log — one line per action, with its outcome
```

There is no console on a Switch, so `printf` goes nowhere. Use these. INFO, ERROR and `[EVENT]` go
to a dated file, `sdmc:/PKSE/logs/debug_YYYY-MM-DD.log`, rotated after `LOG_RETENTION_DAYS`.
`trace.log` is deliberately separate: one flat file, never rotated or truncated, meant to be copied
off the card whole when testing by hand. Delete it to start a fresh run.

**All sinks check `g_debugLogging`**, so nothing costs a user anything until they turn it on, and a
normal run leaves nothing on the card. **Any new sink must check it too** — it is the one way to get
the single-build design (chapter 02) wrong. The Settings row's status message names the log path,
because the point of the toggle is handing that file to someone: turn it on, reproduce, send the log.

### `[EVENT]` — the action log

`Utils::logEventToFile` records what the *user* did: every Pokémon added, released, moved, cloned or
modified; every item added, removed, retyped or recounted; every bank load/save/discard; the session
header and the save commit. One line of `KEY=value` pairs, no wrapping, values quoted where they can
contain spaces — so a whole class of event greps out with `grep 'EVENT.*RELEASE'`. Build the line
with the helpers in `include/Utils/EventLog.h` (`describePokemon`, `briefPokemon`, `logField`,
`logSlot`, `itemName`) — **never hand-rolled**, so a field means the same thing in every line.

It goes to the *same dated file* as the INFO/ERROR diagnostics on purpose: an action log is only
worth having next to the errors it caused. "The save failed" is not a bug report; "the save failed
right after a traded Zacian moved out of the bank" is one.

Four rules:

**Guard every call site with `if (g_debugLogging)`.** Arguments are evaluated before the sink can
check anything, so an unguarded `describePokemon` pays for dozens of table lookups and string joins
even with logging off — times thirty on a multi-select drop. Cheap constant lines (a bank save result)
do not need the guard.

**Log *before* a destructive mutation.** A released mon, a removed item and an overwritten ID are
all unreadable a line later.

**Log *after* a conversion**, so the record is the mon as it actually landed.

**Log refusals too** (`result=BLOCKED`, `REFUSED_POUCH_FULL`, `no-free-slot`). A refused action looks
identical to nothing happening from the user's side, which is exactly when they file a report.

A real call site, from the item-remove handler:

```cpp
if (g_debugLogging)
{
    Utils::logEventToFile(
        std::string("ITEM action=REMOVE ") +
        Utils::logField("pouch", Panels::pouchDisplayName(trainer.getGameGroup(), selectedCategory)) +
        " " + Utils::logField("item", Utils::itemName(pouch[rawIdx].itemId, trainer.getGameGroup())) +
        " id=" + std::to_string(pouch[rawIdx].itemId) +
        " count=" + std::to_string(pouch[rawIdx].count) +
        " mode=" + (trainer.itemsAreIdIndexed() ? "zeroed" : "erased") + " via=dialog");
}
```

`describePokemon(pokemon, save)` is the heavyweight one and answers the question that is otherwise a
round-trip to the user: ownership (`own=OWN|TRADED|NO_OT`, decided on OT name + full id32 + OT
gender, with the save's own OT logged alongside so the verdict is checkable), OT/HT and the handler
flag, origin/met, ball/language/ability/nature/held, PID/EC, IVs/EVs/AVs, and moves by name.
`briefPokemon(pokemon)` is the light one for events that name a mon in passing.

## Telling the user something

**`postStatus()` is PKSE's only way to say anything to the user.** There is no console, and
`logErrorToFile` writes somewhere they cannot read while the app is running.

```cpp
// TrainerViewScreen.h
void postStatus(const std::string &message, int frames = 240)
{
    storageStatus = message;
    storageStatusFrames = frames;
}
```

It renders as a centred pill just above the nav bar, drawn **last** in `draw()` so nothing paints
over it — a message can be raised from inside the details modal, and the modal is drawn after the
main body. The countdown lives in `update()`, not in the storage input handler, so a message posted
from anywhere (a save result, a box rename) actually expires.

Anything a user needs to know about must go through here, not only into the log.
