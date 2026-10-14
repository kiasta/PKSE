#!/usr/bin/env bash
# PKSE UI tests -- drive the real screens on the HOST and assert on what they draw.
#
#   ./tools/harness/uihost/run.sh [report-path] [save-root]
#
# Everything this needs lives in this one directory. It makes NO changes to include/ or src/ and adds
# no build flag, define or header to the shipped tree. See README.md.
#
# It compiles the SAME screen, panel, dialog and modal sources the .nro does. Three things stand in
# for console hardware, all of them in this directory:
#
#   src/UI/PKSEFramebuffer.cpp -> RecordingFramebuffer.cpp   records draws; no GL, SDL or NanoVG
#   src/UI/TouchInput.cpp      -> HostTouchInput.cpp         scripted taps, identical edge logic
#   <switch.h>                 -> hostnx/switch.h            PadState + button bits, nothing else
#
# Buttons need no substitute: PadState is a plain struct and libnx's readers are constexpr reads
# over it, so the driver fills one in and the screens cannot tell.
#
# Exits non-zero if any check fails.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT="$ROOT/build-harness/uitest"
mkdir -p "$OUT/shots"

# Sprites. SpriteManager builds paths as "romfs:/sprites/..." -- a relative path, since there is no
# such mount off-console. A directory literally named "romfs:" next to the binary therefore resolves
# it, which loads the REAL art with no change to SpriteManager. Sprite presence changes layout (a
# missing one draws a placeholder), so this is what keeps host frames faithful to console frames.
ln -sfn "$ROOT/romfs" "$OUT/romfs:"

SRC=()
for d in src/Pokemon src/Names src/Enums src/Encryption src/Legality src/Conversion src/Trainer; do
  while IFS= read -r f; do SRC+=("$f"); done < <(find "$ROOT/$d" -name '*.cpp')
done
SRC+=("$ROOT/src/Save/Block.cpp")
# The envelope rules need no console, and the file browser test draws their refusal text -- the
# words a user reads instead of their save -- so the real ones are linked rather than restated.
SRC+=("$ROOT/src/Save/SaveEnvelope.cpp")

# Utils: all but FileUtilities (half Switch save-data plumbing; the ordinary file half is
# reimplemented in HostStubs.cpp), Logger (sinks stubbed) and Keyboard (swkbd, scripted).
for f in EventLog HelperUtilities MD5 SHA256 Settings StringHelpers; do SRC+=("$ROOT/src/Utils/$f.cpp"); done

SRC+=("$ROOT/src/Test/TestReport.cpp")   # findings share the self test's grouped report model
# The on-console UI suite and its journey engine. Building them here is the fast loop for the thing
# that otherwise can only be debugged by pushing an .nro to a console: the journeys and their
# change-verification are pure logic over the same screens, so they run identically off-console.
SRC+=("$ROOT/src/Test/UIJourney.cpp")
SRC+=("$ROOT/src/Test/SuiteUI.cpp")
# The file browser suite needs no save, so it runs here whether or not any save was found.
# It is the only automated check that the picker OFFERS the right files -- the shipped bug it
# exists for hid every Gen 6 and Gen 7 save behind an extension filter, which no logic test
# could see because no logic was wrong.
SRC+=("$ROOT/src/Test/SuiteFileBrowser.cpp")

# The UI under test. Excluded and why: PKSEFramebuffer + TouchInput are substituted above;
# SaveSelectScreen (fs/ns save enumeration), SystemIcons (ns title icons), UI.cpp (applet loop +
# pad init) and TestRunnerScreen (applet loop) reach console services with no host equivalent.
SRC+=("$ROOT/src/UI/TrainerViewScreen.cpp")
SRC+=("$ROOT/src/UI/BackupSelectionScreen.cpp")
SRC+=("$ROOT/src/UI/SpriteManager.cpp")
SRC+=("$ROOT/src/UI/ListSearch.cpp")   # the shared search box every list uses
for d in src/UI/Panels src/UI/Dialogs src/UI/Modals; do
  while IFS= read -r f; do SRC+=("$f"); done < <(find "$ROOT/$d" -name '*.cpp')
done

while IFS= read -r f; do SRC+=("$f"); done < <(find "$HERE" -maxdepth 1 -name '*.cpp')

# hostnx FIRST on the include path: that is what makes #include <switch.h> resolve here instead of
# failing, without the shipped headers being touched.
# memecrypto (Gen 7 save signing, reached through Trainer67) is C and must be compiled AS C --
# it uses `xor` as an identifier, which is an alternative token for `^` in C++.
MEMECRYPTO_A=$(sh "$ROOT/tools/harness/memecrypto.sh" "$OUT")

echo "Compiling ${#SRC[@]} sources for the host..."
g++ -std=c++20 -O1 -w \
    -include cstdint -include cstddef -include Globals.h \
    -I"$HERE/hostnx" -I"$ROOT/include" -I"$ROOT/include/UI" -I"$HERE" -I"$ROOT/memecrypto" \
    -o "$OUT/uitest" "${SRC[@]}" "$MEMECRYPTO_A" || { echo "BUILD FAILED"; exit 1; }

REPORT="${1:-$ROOT/uitest-report.log}"
echo
cd "$OUT" || exit 1
if [ $# -ge 2 ]; then PKSE_UITEST_SHOTS="$OUT/shots" ./uitest "$REPORT" "$2"
else                 PKSE_UITEST_SHOTS="$OUT/shots" ./uitest "$REPORT"; fi
