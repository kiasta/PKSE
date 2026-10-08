<p align="center">
  <img src="assets/banner.png" alt="PKSE - Pokemon Save Editor" width="640">
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-AGPL%20v3-blue.svg" alt="License: AGPL v3"></a>
</p>

# **PKSE - Pokemon Save Editor**
PKSE is a homebrew application for conveniently editing Pokemon save files on the Nintendo Switch, without having to transfer save files to your PC. It edits the Switch games' own saves, and save files from every earlier generation — Red and Blue through Ultra Sun and Ultra Moon — copied onto the SD card.

## **Features**
- Backup and restore save files, directly on the console.
- Edit party and box Pokemon: species, level, stats, IVs/EVs (AVs in Let's Go), nature, ability, moves, held item, ball, OT/met/origin, shininess and gender.
- Edit trainer info and item pouches.
- **Older generations** — open a save file from an emulator, a cartridge dump or a 3DS save manager and edit it the same way. Boxes, party, items, trainer details, the creator, the legality checker and the bank all work as they do for the Switch games.
- **Pokemon creator** — build a Pokemon from scratch in any supported game's format, with legal options highlighted.
- **Legality checker** — flags illegal values as you edit, and checks whether a real encounter in the Pokemon's origin game could have produced it (informational; it never blocks or auto-changes anything).
- **Cross-game bank** — PKSE-native persistent storage that every supported game shares, 200 boxes deep. Deposit from one game and withdraw into another and the Pokemon is converted into the destination's format on the way out, preserving its origin (OT, IDs, met data, IVs/nature/PID). Moves the destination can't legally know are cleared, since an impossible move corrupts the Pokemon in some games. Red/Blue/Yellow and Gold/Silver/Crystal Pokemon travel forward through Poke Transporter, exactly as the Virtual Console games did, and never back.
- **PKSM bank import** — read a PKSM `.bnk` file straight off the SD card (Minus in the Storage view opens a file browser) and pull its Pokemon into PKSE's bank, keeping your box layout and box names. Every generation a PKSM bank can hold is covered — Gens 1 through 9 — and the preview reports exactly what it found before anything is written.

## **Screenshots**

Each row is the same screen in the dark and light themes. Click any shot for full size.

<a href="assets/screenshots/0.jpg"><img src="assets/screenshots/0.jpg" alt="Title selection (dark theme)" width="380"></a>
<a href="assets/screenshots/1.jpg"><img src="assets/screenshots/1.jpg" alt="Title selection (light theme)" width="380"></a>

<a href="assets/screenshots/2.jpg"><img src="assets/screenshots/2.jpg" alt="Main menu (dark theme)" width="380"></a>
<a href="assets/screenshots/3.jpg"><img src="assets/screenshots/3.jpg" alt="Main menu (light theme)" width="380"></a>

<a href="assets/screenshots/4.jpg"><img src="assets/screenshots/4.jpg" alt="Box view (dark theme)" width="380"></a>
<a href="assets/screenshots/5.jpg"><img src="assets/screenshots/5.jpg" alt="Box view (light theme)" width="380"></a>

<a href="assets/screenshots/6.jpg"><img src="assets/screenshots/6.jpg" alt="Pokemon details (dark theme)" width="380"></a>
<a href="assets/screenshots/7.jpg"><img src="assets/screenshots/7.jpg" alt="Pokemon details (light theme)" width="380"></a>

<a href="assets/screenshots/8.jpg"><img src="assets/screenshots/8.jpg" alt="Editing a stat (dark theme)" width="380"></a>
<a href="assets/screenshots/9.jpg"><img src="assets/screenshots/9.jpg" alt="Editing a stat (light theme)" width="380"></a>

<a href="assets/screenshots/10.jpg"><img src="assets/screenshots/10.jpg" alt="Party view (dark theme)" width="380"></a>
<a href="assets/screenshots/11.jpg"><img src="assets/screenshots/11.jpg" alt="Party view (light theme)" width="380"></a>

<a href="assets/screenshots/12.jpg"><img src="assets/screenshots/12.jpg" alt="Storage and bank (dark theme)" width="380"></a>
<a href="assets/screenshots/13.jpg"><img src="assets/screenshots/13.jpg" alt="Storage and bank (light theme)" width="380"></a>

## **Title Compatibility**

All seven mainline Switch titles are implemented, and all of them interconnect through the bank.

| Generation | Title | Status |
|---|---|---|
| 3 | FireRed / LeafGreen | Implemented — hardware validated |
| 7 | Let's Go, Pikachu! / Eevee! | Implemented — hardware validated |
| 8 | Sword / Shield | Implemented — hardware validated |
| 8 | Brilliant Diamond / Shining Pearl | Implemented — hardware validated |
| 8 | Legends: Arceus | Implemented — hardware validated |
| 9 | Scarlet / Violet | Implemented — hardware validated |
| 9 | Legends: Z-A | Implemented — hardware validated |

FireRed and LeafGreen ship as a separate Switch title per language; all twelve are recognised.

### Earlier generations

Every main-series game before the Switch is implemented too — 28 titles. Their saves are files rather than installed games, so they are opened from the SD card: see [Opening an older game's save](#opening-an-older-games-save).

| Generation | Titles |
|---|---|
| 1 | Red / Blue / Yellow |
| 2 | Gold / Silver / Crystal |
| 3 | Ruby / Sapphire / Emerald, FireRed / LeafGreen (the GBA games' saves, as well as the Switch release's) |
| 4 | Diamond / Pearl / Platinum, HeartGold / SoulSilver |
| 5 | Black / White, Black 2 / White 2 |
| 6 | X / Y, Omega Ruby / Alpha Sapphire |
| 7 | Sun / Moon, Ultra Sun / Ultra Moon |

### Known gaps
- Transferring *into* Gen 3 rebuilds the Pokemon's PID. Gen 3 derives nature, gender, shininess and ability slot from the PID, so PKSE searches for a PID that reproduces all four — those traits are preserved (the original PID is kept in the rare case no match is found). The trade-off is that the PID itself changes, and the resulting PID/IV pair won't correspond to a real Gen 3 RNG frame; PKSE warns you before the conversion. A nickname Gen 3's character set can't spell falls back to the species name, and an original trainer name it can't spell is left blank rather than replaced.
- Pokemon from Generations 4, 5 and 6 can be stored in the bank, but PKSE doesn't convert them into another game's format yet.
- Ribbons and marks are listed on the details page but can't be edited.
- Korean Gold/Silver saves aren't supported. They use a different character table and checksum, so PKSE refuses them rather than half-opening one.
- Red/Blue, Gold/Silver, Ruby/Sapphire, FireRed/LeafGreen (as a GBA save file), Diamond/Pearl and HeartGold/SoulSilver each write an identical save with no version byte, so PKSE can only name the pair.

---

## **Using PKSE**

Copy `PKSE.nro` to `/switch/` on your SD card and start it from the Homebrew Menu. Open the Homebrew Menu through title override — hold **R** while starting any game — rather than from the Album, because applet mode leaves homebrew much less memory.

Pick a user, a game, then a backup to work on (or create a new one). Backups live in `sdmc:/PKSE/`, one folder per title, named after the game and its title ID. When you save, PKSE asks where to write: the backup you are editing, a new backup, or the game's own save.

### Opening an older game's save

An older game's save is a file, not an installed game. Copy it onto your SD card, press **Y — Open Save File** on the save picker and browse to it. The browser lists `.sav`, `.srm`, `.dat`, `.sgm`, `.dsv`, `.duc`, `.dss`, `.fla` and `.SaveRAM` files, second-player and save-slot files (`.sa2`–`.sa4`, `.srm2`, `.sav1`–`.sav9`), and 3DS saves named `main`; **X** shows every file. PKSE identifies the game from the file's contents, and if it can't open a file it says why, under the list. Before it writes to a file you opened this way it copies the original to `sdmc:/PKSE/FileBackups`.

Saves straight from an emulator open as they are, and are written back in the same shape so the emulator keeps reading them:

- a real-time clock saved after the game's save (mGBA, BGB, VBA-M, SameBoy, the Analogue Pocket and many flashcarts), including **TGB Dual**'s and the **MiSTer** Game Boy and GBA cores';
- RetroArch's **VBA Next** and **Beetle GBA** cores, whose `.srm` files are 136 KiB rather than 128 KiB, and its **meteor** core;
- **DeSmuME** and **DraStic** `.dsv` files;
- **no$gba** `.SAV` files saved uncompressed or "Compressed (fast/rlu)";
- **Action Replay DS**, **Action Replay DSi** and **MAX Drive DS** `.duc` and `.dss` dumps;
- **BizHawk** 1.x Game Boy Advance saves;
- Game Boy saves from **Nintendo Switch Online**;
- saves followed by unused space — a whole-chip dump, a flashcart file such as YSMenu's 1 MiB DS saves, or GBE+'s 128 KiB Game Boy saves.

Some files are recognised but can't be read yet, because the save inside is compressed. PKSE says which, and what to change:

- **RetroArch** with *SaveRAM Compression* on: turn it off (Settings > Saving), then save in the game again;
- **no$gba** "Compressed (good/lz)" saves: set no$gba's *SAV/SNA File Format* to *Uncompressed*, then save again;
- **Goomba Color** and **Retron 5** saves, which PKSE can't open yet;
- a ZIP archive: extract the save from it first;
- a whole 3DS save container from GodMode9: export the save with Checkpoint or JKSM and open its `main`.

### Logs

Settings → **Enable Debug Logging** (off by default) writes a log to `sdmc:/PKSE/logs/`. If you hit a bug, turn it on, reproduce the problem and attach the log to your report.

---

## **Building PKSE**

### 1. Install devkitPro and the packages PKSE links

Install [devkitPro](https://devkitpro.org/wiki/Getting_Started). On Windows its installer sets up an MSys2 shell to build from — select *Switch Development* when it asks. Then install the packages:

```bash
pacman -S switch-dev switch-sdl2 switch-glad switch-lz4
```

On Windows, run that in the devkitPro MSys2 shell. On Arch-family Linux, add devkitPro's repositories to `/etc/pacman.conf` and use the system `sudo pacman`; on other Linux distributions and on macOS, devkitPro's package manager is `dkp-pacman` (see their Getting Started guide for your platform).

That is everything the build needs: `switch-dev` is the toolchain (devkitA64, libnx, `pkg-config` and the Switch tools), `switch-sdl2` provides the window, GL context and input (pulling in Mesa/EGL), `switch-glad` is the OpenGL loader NanoVG draws through, and the Makefile links `switch-lz4`. `switch-sdl2_image`, `switch-sdl2_ttf` and `switch-zlib` are **not** needed.

You also need **Python 3** for the asset scripts in step 3, plus [Pillow](https://pypi.org/project/Pillow/) (`pip install pillow`) for the sprite script.

### 2. Check that `DEVKITPRO` is set

The Makefile needs `DEVKITPRO` to point at the devkitPro install, and stops with *"Please set DEVKITPRO in your environment"* if it doesn't. The Windows installer sets it, and on Linux `switch-dev` installs `/etc/profile.d/devkit-env.sh`, which sets it for login shells — so open a new shell after installing and check:

```bash
echo $DEVKITPRO        # /opt/devkitpro
```

If it prints nothing (a shell that doesn't read `/etc/profile.d`, for example), set it yourself:

```bash
export DEVKITPRO=/opt/devkitpro
```

### 3. Fetch the romfs assets (once per checkout)

The fonts and art PKSE bundles into the `.nro` are not in the repository — `romfs/` is gitignored — and **the build downloads nothing**. Fetch them once with the four scripts in `tools/`:

```bash
python tools/gen_fonts.py        # the UI fonts: Nunito, Noto Sans Symbols and Symbols 2 (SIL OFL)
python tools/gen_typeicons.py    # the 19 type icons: 18 types plus Stellar
python tools/gen_marks.py        # the origin marks, from PKHeX
python tools/gen_hdsprites.py    # every Pokemon HOME render, downscaled to 256px (needs Pillow)
```

The first three take seconds. `gen_hdsprites.py` is the long one: it mirrors PokeAPI's whole HOME sprite tree — 3,260 files, about 148 MB, with shiny and female variants — into `romfs/sprites/pokemon_hd/`.

Each script fetches only what is missing, so re-running one is cheap and safe; `--force` re-fetches everything, and `gen_hdsprites.py --only 778 10091` repairs individual sprites. The sprites and type icons are pinned to one PokeAPI commit. The fonts follow Google Fonts' `main` branch and the marks follow PKHeX's `master`, but nothing already on disk is fetched again unless you pass `--force`.

If any of this is missing, `make` stops before compiling anything and names the script to run, rather than building an `.nro` with blank art or no text.

### 4. Build

From the repository root, in a POSIX shell — the devkitPro MSys2 shell on Windows (not PowerShell or cmd), or any shell on Linux or macOS:

```bash
make                   # or: make -j$(nproc)
```

This produces **`PKSE.nro`** in the repository root; copy it to `/switch/` on your SD card. `make clean` removes the build output, and `make all` is an alias for `make`.

There is no separate production build. SD-card logging is a **runtime** setting — Settings → *Enable Debug Logging*, off by default — so the `.nro` you test is the one you ship, and a user who hits a bug can always produce a log.

To check that the code compiles without a full build, `python tools/syntax_check.py` runs the real cross compiler over every source file in a few seconds, with no linking and no MSys2 shell needed. `--warnings` does a full optimizing pass instead and fails on any warning — the build is kept warning-free.

### 5. Editor setup (optional)

The build doesn't need an IDE. For IntelliSense in VS Code, install the C/C++ extension and create `.vscode/c_cpp_properties.json` along these lines:

```json
{
  "configurations": [
    {
      "name": "Switch (devkitA64)",
      "compilerPath": "/opt/devkitpro/devkitA64/bin/aarch64-none-elf-g++",
      "compilerArgs": [
        "-march=armv8-a+crc+crypto", "-mtune=cortex-a57", "-mtp=soft", "-fPIE",
        "-fno-rtti", "-fno-exceptions", "-ftls-model=local-exec"
      ],
      "includePath": [
        "${workspaceFolder}/include",
        "${workspaceFolder}/nanovg",
        "${workspaceFolder}/memecrypto",
        "${workspaceFolder}/build",
        "/opt/devkitpro/libnx/include",
        "/opt/devkitpro/portlibs/switch/include",
        "/opt/devkitpro/portlibs/switch/include/SDL2"
      ],
      "defines": ["__SWITCH__", "_REENTRANT", "NVG_NO_STB"],
      "cStandard": "gnu17",
      "cppStandard": "c++20",
      "intelliSenseMode": "linux-gcc-arm64"
    }
  ],
  "version": 4
}
```

On Windows, replace `/opt/devkitpro` with the real path — normally `C:/devkitPro` — and add `.exe` to the compiler path. Don't use `${env:DEVKITPRO}`: on Windows it holds the MSys2 path `/opt/devkitpro`, which VS Code can't resolve, so every libnx include silently fails.

In Visual Studio, use **File → Open → Folder**, which reads the committed `CppProperties.json`; point its devkitPro paths at your install. `python tools/syntax_check.py --print-flags` prints the exact flags the build uses.

---

## **Regenerating the data tables**

Most of the game data PKSE relies on — names in nine languages, per-game species data, learnsets, evolutions, the legality checker's encounter tables, item pouches, move PP, Pokedex layouts and the fixed save-block tables — lives in **generated** source files under `src/` and `include/`. These are **committed to the repo**, so a normal build never regenerates them: `make` just compiles them, and you do **not** need any of this to build PKSE.

You only need to regenerate when upstream data changes — a new game, a DLC that adds Pokemon / moves / items, or a correction in [PKHeX](https://github.com/kwsch/PKHeX). The generators live in `tools/` and are **not** part of the build.

### Everything at once

```bash
python tools/regenerate.py                  # every generator, then the list of committed files that changed
python tools/regenerate.py --list           # show what would run, and run nothing
python tools/regenerate.py --tables         # the data tables only
python tools/regenerate.py --assets         # the four romfs asset scripts only (--force re-fetches)
python tools/regenerate.py --ref <commit>   # pin PKHeX to one commit for this run
```

It finds every `tools/gen_*.py` by itself. By default it **also regenerates any sibling directory that is itself a PKSE checkout**; `--repo .` limits it to this one, and `--list` shows which checkouts it found.

### One generator at a time

| Script | Generates |
|---|---|
| `gen_speciesnames.py` | species names |
| `gen_movenames.py` | move names |
| `gen_simplenames.py` | ability, nature and type names |
| `gen_itemnames.py` | item names |
| `gen_locations.py` | met-location names |
| `gen_formnames.py` | form names in the eight languages besides English (needs the .NET SDK, see below) |
| `gen_ribbonnames.py` | ribbon names in the eight languages besides English |
| `gen_personaltables.py` | per-game species data — stats, types, abilities, gender ratio, growth rate, forms — one file per save format |
| `gen_personal.py` | the cross-game species table: per-game presence and form counts |
| `gen_gen1.py` | Red/Blue/Yellow's own tables: species index, base stats, types and move PP |
| `gen_learnsets.py` | per-game learnable-move sets |
| `gen_moveinfo.py` | per-game base PP |
| `gen_movepresence.py` | which moves exist in each game |
| `gen_itempouches.py` | which items belong in each bag pocket |
| `gen_itempresence.py` | which items a Pokemon may legally hold, per game |
| `gen_evolutions.py` | the legality checker's evolution tables |
| `gen_encounters.py` | the legality checker's encounter tables — wild, static, gift, trade, raid and Mystery Gift |
| `gen_blocktables.py` | the fixed save-block tables for Gens 5–7 |
| `gen_dexformtables.py` | Pokedex form-index tables for X/Y, Omega Ruby/Alpha Sapphire, Sun/Moon, Ultra Sun/Ultra Moon and Let's Go |
| `gen_dextable8swsh.py` | which of Sword/Shield's three Pokedexes a species is in |
| `gen_dextable8la.py` | Legends: Arceus's research-log lookup |
| `gen_dextable9sv.py` | which Scarlet/Violet regional Pokedex a species and form is in |
| `gen_gen2text.py` | the Gen 2 character tables |
| `gen_gen4text.py` | the Gen 4 character tables |

### Where the data comes from

The generators read PKHeX's source and resources **straight from GitHub** through `tools/pkhex_source.py` — only Python 3 and an internet connection are needed, no local PKHeX checkout — and cache them under `tools/.pkhex_cache/` (gitignored), so re-runs are offline.

By default they follow **PKHeX's `master` branch**, resolved to one commit per run, so regenerating adopts upstream corrections — which makes the regenerated diff the review gate. Two overrides:

- **Reproduce a table exactly** — pin `PKHEX_REF` to the commit (or tag, or branch) it was built from. `src/Names/FormNamesLocalized.cpp` records the commit it was generated from.
  ```bash
  PKHEX_REF=<commit> python tools/gen_personaltables.py
  ```
- **Use a PKHeX checkout you already have** instead of downloading — point `PKHEX_LOCAL` at it, either the repository root or its `PKHeX.Core` folder:
  ```bash
  PKHEX_LOCAL=/path/to/PKHeX python tools/gen_learnsets.py
  ```

(From PowerShell, set the variable first, e.g. `$env:PKHEX_REF = "master"`.)

`gen_formnames.py` needs more than Python: it compiles PKHeX.Core to call PKHeX's own form converter, so it needs the [.NET 10 SDK](https://dotnet.microsoft.com/download), and it also reads localized form names from PokeAPI (cached under `tools/.pokeapi_cache/`).

After regenerating, review the diff to the affected file(s) and commit it.

---

## **Troubleshooting**

### Common Issues

- **`error: romfs is missing assets the .nro must contain`**:  
  A fresh checkout has no fonts or art. Run the script the message names ([step 3](#3-fetch-the-romfs-assets-once-per-checkout)), then build again.

- **`Please set DEVKITPRO in your environment`**:  
  See [step 2](#2-check-that-devkitpro-is-set).

- **`make` not found**:  
  On Windows, build from the devkitPro MSys2 shell, not PowerShell or cmd — the Makefile uses POSIX `sh`. On Linux, install your distribution's `make`.

- **Undefined references when linking**:  
  A package from [step 1](#1-install-devkitpro-and-the-packages-pkse-links) is missing — check `switch-sdl2`, `switch-glad` and `switch-lz4`. If *every* SDL and EGL symbol is undefined, `pkg-config` didn't find SDL2: `$DEVKITPRO/portlibs/switch/bin/aarch64-none-elf-pkg-config --libs sdl2` should print linker flags.

- **The editor shows errors but `make` succeeds**:  
  IntelliSense only approximates the cross compiler ([step 5](#5-editor-setup-optional)). `python tools/syntax_check.py` is the authority on whether the code compiles.

- **libnx-related errors**:  
  Ensure `libnx` is properly installed (it comes with `switch-dev`) and that `DEVKITPRO` is set correctly.

- **Permission issues on Windows**:  
  Run VS Code or your terminal as Administrator if file access errors occur.

---

## **Credits**

- PKHeX Team: core save editing logic, the game data tables and the origin marks are derived from the PKHeX project. Visit their official repository: https://github.com/kwsch/PKHeX.
- PKSM Team: for their work on the 3DS and their bank system. Visit their official repository: https://github.com/FlagBrew/PKSM.
- PokeAPI Team: for their work on sprites and type icons: https://github.com/PokeAPI/sprites
- libnx and devkitPro communities for Switch homebrew development tools. Visit their official website: https://devkitpro.org/wiki/Getting_Started.
- SciresM, for [memecrypto](https://github.com/FlagBrew/memecrypto), which signs Sun/Moon and Ultra Sun/Ultra Moon saves (vendored in `memecrypto/`, GPLv3).
- Mikko Mononen, for [NanoVG](https://github.com/memononen/nanovg), which draws the UI, and Sean Barrett, for [stb_image](https://github.com/nothings/stb), which decodes the sprites.
- The UI fonts, [Nunito](https://fonts.google.com/specimen/Nunito) and [Noto Sans Symbols](https://fonts.google.com/noto/specimen/Noto+Sans+Symbols), under the SIL Open Font License.

## **License**

This project is licensed under the [GNU Affero General Public License v3.0](LICENSE). See `LICENSE` for details.
