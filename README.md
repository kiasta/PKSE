<p align="center">
  <img src="assets/banner.png" alt="PKSE - Pokemon Save Editor" width="640">
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-AGPL%20v3-blue.svg" alt="License: AGPL v3"></a>
</p>

# **PKSE - Pokemon Save Editor**
PKSE is a homebrew application for conveniently editing Pokemon save files on the Nintendo Switch, without having to transfer save files to your PC.

## **Features**
- Backup and restore save files, directly on the console.
- Edit party and box Pokemon: species, level, stats, IVs/EVs (AVs in Let's Go), nature, ability, moves, held item, ball, OT/met/origin, shininess and gender.
- Edit trainer info and item pouches.
- **Pokemon creator** — build a Pokemon from scratch in any supported game's format, with legal options highlighted.
- **Legality checker** — flags illegal values as you edit (informational; it never blocks or auto-changes anything).
- **Cross-game bank** — PKSE-native persistent storage that every supported game shares. Deposit from one game and withdraw into another and the Pokemon is converted into the destination's format on the way out, preserving its origin (OT, IDs, met data, IVs/nature/PID). Moves the destination can't legally know are cleared, since an impossible move corrupts the Pokemon in some games.

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


### Known gaps
- Transferring *into* Gen 3 rebuilds the Pokemon's PID. Gen 3 derives nature, gender, shininess and ability slot from the PID, so PKSE searches for a PID that reproduces all four — those traits are preserved (the original PID is kept in the rare case no match is found). The trade-off is that the PID itself changes, and the resulting PID/IV pair won't correspond to a real Gen 3 RNG frame; PKSE warns you before the conversion. Custom nicknames also fall back to the species name.
- Ribbons are counted but not individually displayed or editable.

---

## **Prerequisites**

### 1. Install Required Tools
Ensure the following tools and dependencies are installed:

#### **1.1. devkitPro**
- Download and install [devkitPro](https://devkitpro.org/wiki/Getting_Started).
- Ensure `Switch Development` is selected during installation.

#### **1.2. zlib installation** (Optional, will implement compressed logic in future versions)
- In the MSys2 shell, run ```pacman -S switch-zlib``` to install the zlib for compression support.

---

### 2. Set Up Environmental Variables
Set the `DEVKITPRO` environment variable to the installation path of devkitPro.

#### On Windows:
```bash
setx DEVKITPRO "C:\devkitPro"
```
#### On macOS/Linux:
Add the following line to your shell configuration file (~/.bashrc or ~/.zshrc):
```bash
export DEVKITPRO=/opt/devkitpro
```

Restart your terminal or run the command to apply the changes.

---

### 3. Configure Visual Studio Code

To configure IntelliSense in VS Code:

#### **3.1. Install Extensions**
- C/C++ by Microsoft
- DevkitPro Tools (if available)

#### **3.2. Create a c_cpp_properties.json File**
Create or update the file in .vscode/c_cpp_properties.json with the following content:
```json
{
  "configurations": [
    {
      "name": "Switch",
      "includePath": [
        "${workspaceFolder}/include/**",
        "${workspaceFolder}/src/**",
        "${env:DEVKITPRO}/libnx/include",
        "${env:DEVKITPRO}/portlibs/switch/include", // We should include optional libraries here
        "${env:DEVKITPRO}/devkitA64/aarch64-none-elf/include"
      ],
      "defines": [],
      "compilerPath": "${env:DEVKITPRO}/devkitA64/bin/aarch64-none-elf-g++.exe",
      "cStandard": "c11",
      "cppStandard": "c++20", // This version is necessary
      "intelliSenseMode": "linux-gcc-arm64"
    }
  ],
  "version": 4
}
```

---

## **4. Build the Project**

#### **4.1. Fetch the Pokemon sprites** (one time)

The HD Pokemon sprites are **not** downloaded by `make`. Fetch them once from the [PokeAPI HOME renders](https://github.com/PokeAPI/sprites) and downscale them into `romfs/` with the bundled script — it needs Python 3 and [Pillow](https://pypi.org/project/Pillow/) (`pip install pillow`):

```bash
python tools/gen_hdsprites.py
```

This writes 256px PNGs into `romfs/sprites/pokemon_hd/` (every base species plus alternate forms, normal + shiny). You only need to re-run it after bumping the script's pinned PokeAPI ref or adding a new generation; pass `--force` to re-fetch everything.

#### **4.2. Build**

Open MSys2 (should have been included with the devkitPro toolset), navigate to the root directory and run:

```bash
make clean && make all
```

`make all` downloads the type icons and UI font (if they're missing), then generates the `.nro` in the build directory, which you can deploy to your Nintendo Switch. If the type icons, font and sprites are already present, skip the downloads with:

```bash
make clean && make
```
or  

```bash
make clean && make all prod
```

`prod` argument ensures no debug or trace logs are being written clogging up space on the sdcard.

---

## **Regenerating the data tables**

Most of the game data PKSE relies on — species / move / ability / item names, learnsets, per-species info (abilities, gender ratios, forms), item-pouch contents, met-location names, move PP and Pokedex entry placement — lives in **generated** source files under `src/Names/` and `src/Pokemon/`. These are **committed to the repo**, but a normal build never regenerates them: `make` just compiles them, and you do **not** need any of the tools below to build PKSE.

You only need to regenerate a table when its upstream data changes — a new game, a DLC that adds Pokemon / moves / items, or a correction in [PKHeX](https://github.com/kwsch/PKHeX). The generators live in `tools/` and are run **by hand, one at a time**.

### PKHeX-derived tables (Python 3)

These read their inputs **straight from GitHub** — only Python 3 and an internet connection are needed, no local PKHeX checkout:

```bash
python tools/gen_encounters.py      # legality encounters
python tools/gen_evolutions.py      # legality evolutions
python tools/gen_itempouches.py     # which items belong in each bag pocket
python tools/gen_itempresence.py    # which items a Pokemon may legally hold, per game
python tools/gen_learnsets.py       # per-game learnable-move bitsets
python tools/gen_locations.py       # met-location names
python tools/gen_moveinfo.py        # base PP per move
python tools/gen_movenames.py       # move display names
python tools/gen_movepresence.py    # which moves exist in each game
python tools/gen_personal.py        # abilities / gender / friendship / forms / per-game presence
python tools/gen_pladex.py          # Legends: Arceus Pokedex statistics-entry lookup
python tools/gen_speciesnames.py    # species display names
python tools/gen_svdex.py           # which Scarlet/Violet regional Pokedex a species+FORM is in
python tools/gen_swshdex.py         # which of Sword/Shield's three Pokedexes a species is in
```

Each fetches its PKHeX files via `tools/pkhex_source.py` and caches them under `tools/.pkhex_cache/` (gitignored) so re-runs are offline. By default they use a **pinned PKHeX commit** — the one the committed tables were built from — so regenerating reproduces the existing tables exactly. Two overrides:

- **Adopt newer PKHeX data** — set `PKHEX_REF` to a newer commit, tag, or `master`, then review the regenerated diff before committing:
  ```bash
  PKHEX_REF=master python tools/gen_personal.py
  ```
- **Read from a local PKHeX checkout** instead of the network — point `PKHEX_LOCAL` at its `PKHeX.Core` directory:
  ```bash
  PKHEX_LOCAL=/path/to/PKHeX/PKHeX.Core python tools/gen_learnsets.py
  ```

(From PowerShell, set the variable first, e.g. `$env:PKHEX_REF = "master"`.)

After regenerating, review the diff to the affected file(s) and commit it.

---

## **Troubleshooting**

### Common Issues

- **`make` not found**:  
  Ensure `make` is installed and in your `PATH`.

- **Undefined references**:  
  Verify that your `includePath` is correctly configured in `c_cpp_properties.json`.

- **libnx-related errors**:  
  Ensure `libnx` is properly installed and that `DEVKITPRO` is set correctly.

- **Permission issues on Windows**:  
  Run VS Code or your terminal as Administrator if file access errors occur.

---

## **Credits**

- PKHeX Team: core save editing logic are derived from the PKHeX project. Visit their official repository: https://github.com/kwsch/PKHeX.
- PokeAPI Team: for their work on sprites: https://github.com/PokeAPI/sprites
- libnx and devkitPro communities for Switch homebrew development tools. Visit their official website: https://devkitpro.org/wiki/Getting_Started.

## **License**

This project is licensed under the [GNU Affero General Public License v3.0](LICENSE). See `LICENSE` for details.

