# 02 — Building and verifying

## Toolchain

You need devkitPro with the Switch toolchain, `DEVKITPRO` exported, and a **POSIX shell** — MSys2 on
Windows or an ordinary Linux shell. The Makefile's recipes are POSIX `sh`; it will not run under
PowerShell or `cmd`. Python 3 is needed once per checkout, for the asset fetchers below — not by the
build itself.

```bash
pacman -S switch-dev switch-sdl2 switch-glad switch-lz4
```

| Package | What it gives you |
|---|---|
| `switch-dev` | devkitA64 + libnx + switch-tools (`nacptool`, `elf2nro`) |
| `switch-sdl2` | window, GL context, input — **and** the mesa/EGL chain |
| `switch-glad` | the GL 4.3 loader NanoVG draws through |
| `switch-lz4` | nothing in the tree calls LZ4, but the Makefile links `-llz4` and the link fails without the library |

**Not** `switch-sdl2_image` / `switch-sdl2_ttf`. They went away with the NanoVG migration: PNGs are
decoded by `stb_image` in `SpriteManager`, text is drawn by NanoVG's own font atlas. Nothing links
them, and `main.cpp` deliberately does not initialise them.

On Arch-family Linux, add the devkitPro repos to `/etc/pacman.conf` and use the system `pacman`.
There is no `dkp-pacman` and no `DEVKITA64` to export — `base_tools` derives the whole toolchain
from `$DEVKITPRO` and puts `$DEVKITPRO/devkitA64/bin` on `PATH` itself.

There is one vendored third-party dependency besides NanoVG: `memecrypto/` (Gen 7 save signing).
It is plain C, already in the Makefile's `SOURCES`/`INCLUDES`, and needs no extra package. **It must
not be edited** — re-sync it from upstream (see `memecrypto/README.md`).

### The `sdl2` spelling trap

The Makefile resolves SDL's flags with `pkg-config`, asking for the module by its canonical
**lowercase** name:

```make
SDL_PKGCONFIG := $(DEVKITPRO)/portlibs/switch/bin/aarch64-none-elf-pkg-config
SDL_CFLAGS    := $(shell $(SDL_PKGCONFIG) --cflags sdl2)
SDL_LIBS      := $(shell $(SDL_PKGCONFIG) --libs --static sdl2)
```

That spelling is load-bearing off Windows. devkitPro installs `sdl2.pc`; on MSys2's
case-insensitive filesystem `SDL2` matched only by accident. On a case-sensitive host it resolves to
nothing, `$(shell)` swallows the error rather than failing, `SDL_CFLAGS`/`SDL_LIBS` expand to
**empty**, and the build runs all the way to the link before dying with every SDL and EGL symbol
undefined. If you see a wall of undefined `SDL_*`/`egl*` symbols, check this line first.

## Building

```bash
make                    # build -> PKSE.nro. Refuses to start if a romfs asset is missing.
make clean && make      # clean rebuild
make all                # an alias for `make`. Fetches nothing.

python tools/gen_fonts.py         # Nunito + both Noto Sans Symbols faces (SIL OFL) -> romfs/fonts/
python tools/gen_typeicons.py     # the 19 type icons (18 + Stellar)               -> romfs/sprites/types/
python tools/gen_marks.py         # the origin markings                             -> romfs/sprites/marks/
python tools/gen_hdsprites.py     # fetch + downscale the 256px sprites (Pillow)    -> romfs/sprites/pokemon_hd/
```

Everything under `romfs/` is **gitignored**, so a fresh checkout must run **all four fetchers**
once before `make` will do anything. `gen_hdsprites.py` (needs Pillow) is the long one.

### The build downloads nothing, and refuses to start without what it needs

Every romfs asset is fetched by its own script under `tools/`, sharing `tools/asset_fetch.py` for
the download half, run by hand once per checkout. The Makefile used to fetch in shell on the way into
a build; it does not any more. A build that reaches the network depends on GitHub being up, can
half-succeed and leave a partial file behind, and hides which upstream snapshot the `.nro` was
actually built from.

Instead the `check-assets` target (named so it cannot collide with the `assets/` directory, which
would let Make call it up to date) tests **one representative file per group** with `[ -s ]` and
fails naming the script to run:

```
error: romfs is missing assets the .nro must contain, and this build does not
       download anything. Fetch them once, then build again:
  UI fonts .......... python tools/gen_fonts.py
  origin markings ... python tools/gen_marks.py
```

It is a prerequisite of `$(BUILD)`, so it runs before anything compiles, and `make clean` still
works on a checkout with no assets at all. All four groups are mandatory — with no fonts nothing on
screen has text at all, and the rest are blank art — because **a missing asset otherwise links into a
perfectly healthy `.nro`** and surfaces only as blank art, or missing text, on the console.

The shell recipes the scripts replaced could not be run with a flag, could not check what they had
written, and treated a **zero-length file** — what an interrupted download leaves behind — as already
present, so that icon or font never came back on its own. Each script now checks a magic prefix
before writing (PNG's `89 50 4E 47`, TrueType's `00 01 00 00`), asserts its full count is on disk,
exits non-zero otherwise, and treats a zero-length file as absent.

Two deliberately different pinning choices. `gen_typeicons.py` pins the **same** PokeAPI/sprites
commit `gen_hdsprites.py` does, so the two fetchers can never disagree about which upstream snapshot
`romfs/` came from — bump them together. `gen_fonts.py` tracks google/fonts `main` and
`gen_marks.py` PKHeX `master`, deliberately: PKSE takes a handful of files out of repositories of
thousands and an upstream correction is something it wants, and nothing is re-fetched once on disk,
so the branch moving cannot change a build underneath anyone. `--force` re-fetches everything.

**Nineteen type icons, not eighteen.** The nineteenth is Stellar, which no species and no move has —
it is reachable only as a Tera type. The count follows `Names::getTypeNameCount()` (19, what can be
NAMED or DRAWN), never `Names::getTypeCount()` (18, what a species or move can BE).

The Pokémon sprites come from PokeAPI's HOME renders pinned to a commit; the script downscales
512 → 256 px and fetches only what is missing. It **mirrors the whole `home/` tree** — 3260 files,
~148 MB, normal + shiny + female + shiny-female — rather than a curated subset, so the art on disk is
always a superset of what `FormSpriteMapping` references.

> **Do not reintroduce a download list of any kind.** Two lists that must agree is exactly what
> silently lost Meloetta Pirouette and `raichu-mega-y`. `--force` re-fetches everything;
> `--only <stems>` repairs individual sprites.

## Versioning

The version lives in **one place**, the Makefile, in **two spellings**, because the `.nacp`'s
`display_version` holds only 15 characters:

```make
APP_VERSION      := 1.2.1-pre            # the .nacp one. 15 CHARACTERS MAX.
APP_VERSION_FULL := 1.2.1-pre-release    # -D'd in as PKSE_VERSION for Globals.h
```

`APP_VERSION` is what the home menu and hbmenu show. The name is fixed by libnx's `switch_rules`,
which hands it to `nacptool`, whose `display_version` field holds 15 characters and truncates past
it **silently** (exit 0, no warning). `APP_VERSION_FULL` has no limit and is where a long
pre-release tag goes.

Keep the short one an abbreviation of the long one. `Globals.h` needs no editing to bump a version:

```cpp
#ifndef PKSE_VERSION
#define PKSE_VERSION "0.0.0-NOVERSION"
#endif
inline const std::string VERSION_STRING = PKSE_VERSION;
```

Note `VERSION_STRING` is `const`, **not** `constexpr`. A constant-evaluated `std::string` must fit
libstdc++'s 15-character small-string buffer, because anything longer heap-allocates and an
allocation cannot escape constant evaluation. `constexpr` would put the 15-char limit back on both
spellings — and the fallback above is already sitting exactly on 15.

Two IntelliSense configs hardcode a copy of the version (`CppProperties.json`,
`.vscode/c_cpp_properties.json`) because they never run the Makefile. They do not affect the built
`.nro`, but bump them anyway so the editor does not show a stale version.

## There is exactly one build

SD-card logging is a **runtime setting** — Settings → "Enable Debug Logging" (`g_debugLogging`,
persisted to `settings.cfg`), **default off**. With it off PKSE writes no
`sdmc:/PKSE/logs/debug_*.log` and no `sdmc:/PKSE/trace.log`. There is no release build to produce
separately, and nothing to `make clean` between.

This replaced a `make ... prod` modifier that set `-DPKSE_PROD` and compiled every sink out.

> **Do not bring it back.** It made the shipped `.nro` a *different binary* from the tested one, it
> needed a clean rebuild to switch (objects did not depend on the define), and — the reason that
> actually mattered — a user who hit a bug on a release had no way to produce a log at all.

Every sink now checks the flag, so **adding a new sink without that check is the one way to get
this wrong.** The declarations in `Logger.h` did not change when the flag replaced the define, so
none of the ~300 call sites needed touching. Chapter 03 has the logging API.

The corollary for startup: `Utils::loadSettings()` is the **first** statement in `main()`, ahead of
the first log line. Read it any later and early-init logging follows the compiled-in default rather
than the user's choice. It is safe there — it only `fopen`s `sdmc:/PKSE/settings.cfg` (libnx mounts
`sdmc` before `main`), and `applyTheme` just swaps colour globals.

## Adding source files

`SOURCES` in the Makefile globs **per listed directory, not recursively**:

```make
SOURCES  := src src/Pokemon src/Encryption src/Enums src/UI src/UI/Panels src/UI/Dialogs \
            src/UI/Modals src/Trainer src/Names src/Utils src/Save src/Legality \
            src/Conversion nanovg memecrypto
INCLUDES := include nanovg memecrypto
```

A new `.cpp` in an existing listed directory is picked up automatically. A **new directory** must be
added to `SOURCES` or its objects silently never build and you get link errors that name symbols,
not files.

Headers under `include/` are found by their path relative to `include/`, e.g.
`#include "UI/Panels/ItemsPanel.h"`.

## Verifying a change

Each check answers a different question, and they are listed in cost order. Chapter 12 covers what
to look at when you test by hand.

| Check | Command | Time | Answers |
|---|---|---|---|
| Syntax | `python tools/syntax_check.py` | ~3 s | **Does it compile?** |
| Warnings | `python tools/syntax_check.py --warnings` | ~1 min | **Does the optimizer object?** |
| Build | `make` | minutes | **Does the `.nro` link, with every asset in it?** |
| Console | run the `.nro` on a Switch | by hand | **Does it actually work, against real saves?** |

**Run the first three before calling a change done.** Run the `.nro` on a console, against the
saves your change touches, before calling it shipped.

### `syntax_check.py`

The fast ground truth. It runs the **real devkitA64 cross compiler** with `-fsyntax-only` and
Makefile-derived flags over the whole project in parallel, in about three seconds — use it instead
of a full build while you work. IntelliSense is an approximation; only GCC decides whether the code
compiles.

```bash
python tools/syntax_check.py                     # every TU the Makefile builds
python tools/syntax_check.py src/UI/UI.cpp ...   # just these files
python tools/syntax_check.py -j 4                # cap parallelism
python tools/syntax_check.py --print-flags       # dump flags (for IDE config)
```

It derives `APP_VERSION`, `SOURCES`, `INCLUDES`, `ARCH`, `DEFINES` from the Makefile and SDL's flags
from `sdl2.pc`, so it tracks the build instead of duplicating it. Exits non-zero on any failure, so
it works as a pre-commit gate.

### `--warnings`

A different question, and **a passing syntax check does not answer it.** `-fsyntax-only` stops
before the optimizer, and `-Wstringop-overflow`, `-Wmaybe-uninitialized` and `-Warray-bounds` only
exist *after* it. Those warnings are structurally invisible to the syntax check and would otherwise
first appear in a full build.

```bash
python tools/syntax_check.py --warnings   # full -O2 codegen pass; non-zero on ANY warning
```

**The build is kept warning-free.** Run this before calling a change done.

The trap it exists for: GCC inlines every branch before checking, so a `vector` built one way in
branch A and another in branch B is analysed as though both ran. `resize()` after an `assign()`
elsewhere reads as *appending* to the already-allocated buffer and reports an overflow. The fix is
to build the buffer in one step (`assign` from a pointer range) rather than `resize()`-then-fill.
Real example, from `Trainer8SWSH::updateBoxBlock()`:

```cpp
// assign() from the pointer range, not resize()-then-fill. Both branches above
// are inlined into one body, and GCC cannot prove blankSlot is still empty
// here -- so it reads the resize as APPENDING 344 bytes onto the 344 the
// harvest branch allocated and reports a -Wstringop-overflow. Building the
// buffer in one step removes the pattern, and matches updatePartyBlock().
const uint8_t *encBytes = reinterpret_cast<const uint8_t *>(encryptedRecord);
blankSlot.assign(encBytes, encBytes + SIZE_PARTY8_SWSH);
```

### `make`

Only a real build proves the `.nro` **links** and that `check-assets` is satisfied. Neither compile
check runs the linker, so a new directory missing from `SOURCES`, or a symbol declared and never
defined, passes both and fails here.

### On a console

What none of the above can reach: the SD card, ROMFS (does the `.nro` actually *contain* the 3260
sprites, three fonts and nineteen type icons it will ask for?), an installed title's save, and
whether the bytes PKSE writes are ones the game accepts. For a change to a save format, the
strongest test is the simplest: open a real save, write it back **unedited**, and compare — an
editor that cannot reproduce a file it did not change will corrupt one it did. Then make the edit
and check each field you touched persisted on its own. Chapter 12 has the checklist.

## Editor / IDE configuration

Committed and intentional:

- `.vscode/` — VS Code
- `CppProperties.json` — Visual Studio, via **File → Open → Folder**, *not* the `.sln`

Both hardcode a native devkitPro path — `/opt/devkitpro` as committed; a Windows checkout needs
`C:/devkitPro` — and **never** `${env:DEVKITPRO}`, because the Windows installer sets that variable
to the MSys2-only path `/opt/devkitpro`, which a Windows-side IDE cannot resolve.

`PKSE.sln` / `PKSE.vcxproj` are the only project pair, kept because they get copy-pasted between
repositories. They are vestigial for IntelliSense: MSVC-mode IntelliSense cannot parse devkitA64's
libstdc++. Do not spend time on them.
