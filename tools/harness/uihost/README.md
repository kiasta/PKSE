# UI tests (host)

> Self-contained by design: everything it needs lives in here. It makes **no** change to
> `include/` or `src/`, adds no header, define, flag or `#ifdef` to the shipped tree, and is not
> referenced by the `Makefile`, the `.vcxproj`, or any product source.

## What this is

The other three verification tiers all stop short of the application:

| tier | question | but |
|---|---|---|
| `syntax_check.py` | does it compile? | never runs anything |
| `syntax_check.py --warnings` | does the optimizer object? | never runs anything |
| `tools/harness/*` + self test | is the byte and table work right? | calls functions **directly** |

None of them ever presses a button. A view you can enter but not leave, a label drawn off its
panel, a Pokémon rendered into an empty slot, a button that quietly stopped responding, an input
sequence that crashes — all invisible to a test that never calls `update()` and `draw()`.

This runs the **real screens, panels, dialogs and modals** — the same sources the `.nro` compiles —
against **real saves**, driven by synthetic input, and asserts on what they actually drew.

```bash
./tools/harness/uihost/run.sh [report-path] [save-root]     # save-root defaults to ~/pkse-test-saves
```

Findings go through `Test::Report`, so they group, count and print exactly like the nine on-console
suites, in the same report format. Exits non-zero if any check fails.

## How it works without touching the tree

An immediate-mode UI has no retained widget tree, which is why "test the UI" looked impossible here —
Cypress works because the DOM is a queryable structure and PKSE keeps nothing between frames.
**`RecordingFramebuffer.cpp` rebuilds exactly that structure**: every primitive is recorded as it is
drawn, so each frame becomes a queryable list of shapes, text and sprites with bounds, colours and
clip state. Assertions run against that, so a test says what the player would *see*.

Three substitutions, all in this directory, and each is the only file that touches its console
service:

| shipped file | replaced by | why |
|---|---|---|
| `src/UI/PKSEFramebuffer.cpp` | `RecordingFramebuffer.cpp` | **every** SDL/GL/NanoVG symbol in the whole UI layer is in that one file — so no GPU, window or context is needed |
| `src/UI/TouchInput.cpp` | `HostTouchInput.cpp` | scripted taps; the press/release edge logic is copied verbatim |
| `<switch.h>` | `hostnx/switch.h` | first on the include path, so the shipped headers stay as they are |

Buttons need no substitute at all: `PadState` is a plain struct and libnx's readers are `constexpr`
reads over it, so the driver fills one in and the screens **cannot tell the difference**.

Sprites are real. `SpriteManager` builds paths as `romfs:/sprites/…`, which off-console is just a
relative path — so `run.sh` drops a directory named `romfs:` beside the binary pointing at the
repo's own `romfs/`. Sprite presence changes layout (a missing one draws a placeholder), so this is
what keeps host frames faithful to console frames.

## Writing a test

```cpp
Driver d(context, screen, "box-view");
d.step("open the box view");            // names the action; appears in every finding
gotoHome(d, screen, 0);
d.press(Btn::A).frame();                // one press = down-edge frame + release frame
d.see("Charizard");                     // visible text  (cy.contains)
d.notSee("Bad Egg");
d.seeNear("Lv. 100", 640, 200);         // positional, with tolerance
d.seeSprites(1);
d.type("NEWNAME").press(Btn::A);        // queues the swkbd answer  (cy.type)
d.expect(screen.selectedBoxIndex == 3, "the box did not change");
```

Every frame is also held to invariants for free, with no test written for them: the frame must draw
something, and nothing may land completely off-screen. That is what makes `testInputFuzz` — a
fixed-seed sweep of random presses — worth running at all.

On the first failure of each test the driver writes an **SVG of the failing frame** to
`build-harness/uitest/shots/`, and the report includes the frame's visible text. A headless UI test
is only useful if you can still see the screen that broke.

## Fidelity — the honest caveats

- **Text advance widths are averaged**, not measured from Nunito, so layout driven by
  `measureText` (centring, right-alignment, ellipsis truncation) lands within a few pixels of the
  console rather than exactly on it. Write positional assertions with a tolerance; anything needing
  pixel-exact text placement belongs in the on-console pass.
- **Four screens are not drivable here** — `SaveSelectScreen` (fs/ns save enumeration),
  `SystemIcons` (ns title icons), `UI.cpp` (applet loop) and `TestRunnerScreen` (applet loop). They
  reach console services with no host equivalent. They are *excluded from the build* rather than
  stubbed, so a missing symbol is a build error that names the problem instead of a stub quietly
  returning a fiction.
- **The save commit is scripted**, not real (`UIHost::setSaveResult`). The write path is
  console-only, but the UI's behaviour around both outcomes is not — a fixed answer either way would
  leave one branch untested.
- **The button bit values in `hostnx/switch.h` are copied from libnx** and nothing here can prove
  they still match, since the real header only exists on console. They are stable ABI. If a test
  passes on the host but the same press does something else on hardware, check those first.

This does not replace the on-console self test. It is the tier below it: fast, headless, and run
before the `.nro` ever reaches a console.
