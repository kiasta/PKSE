#!/usr/bin/env python3
"""Fetch the origin markings into romfs/sprites/marks/.

THESE ARE THE GAMES' OWN ORIGIN MARKS, which is why they are fetched rather than drawn: the Kalos
pentagon, the Alola clover, the Galar mark and the rest are what the games themselves stamp on a
Pokemon's summary, and PKHeX already carries clean 40x40 alpha masks of them. Bank, HOME and PKSM
all mark a Pokemon's origin this way.

GEN 3, 4 AND 5 HAVE NO MARK AND MUST NOT BE GIVEN ONE. Nothing existed before the Kalos pentagon,
so PKHeX has no resource for them and inventing one would be PKSE asserting a mark that does not
exist. Those origins are named in TEXT instead -- which they can be, because unlike a PK1/PK2 they
carry a version byte.

They are black silhouettes with an alpha channel; SpriteManager tints them to the theme, the same
thing PKHeX does with BlackToWhite in dark mode.

MARK_NAMES is the file half of `Trainer::OriginMark`. A mark added to that enum without a row here
is a missing file at runtime, and nothing on screen says which.

Not routed through pkhex_source.py: that module serves PKHeX.Core, and these live in the WinForms
resources, so PKHEX_LOCAL would not find them.

Needs nothing but Python 3.

Run:  python tools/gen_marks.py            # fetch whatever is missing
      python tools/gen_marks.py --force    # re-fetch all of them
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from asset_fetch import fetch_all, present  # noqa: E402

BASE_URL = "https://raw.githubusercontent.com/kwsch/PKHeX/master/PKHeX.WinForms/Resources/img/Markings"
OUT_DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "romfs", "sprites", "marks",
)

# Stored as <name>.png; upstream calls the file gen_<name>.png.
MARK_NAMES = ["vc", "6", "7", "8", "gg", "bs", "la", "sv", "za", "go", "gba"]
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--force", action="store_true",
                        help="re-fetch even when the marking is already on disk")
    args = parser.parse_args()
    os.makedirs(OUT_DIR, exist_ok=True)

    jobs = [(name, "%s/gen_%s.png" % (BASE_URL, name), os.path.join(OUT_DIR, "%s.png" % name))
            for name in MARK_NAMES]
    failures = fetch_all(jobs, force=args.force, magic=PNG_MAGIC)
    if failures:
        sys.exit(1)

    missing = [name for name in MARK_NAMES
               if not present(os.path.join(OUT_DIR, "%s.png" % name))]
    if missing:
        print("ERROR: %d origin marking(s) still missing: %s" % (len(missing), ", ".join(missing)),
              file=sys.stderr)
        sys.exit(1)
    print("all %d origin markings present in %s" % (len(MARK_NAMES), OUT_DIR))


if __name__ == "__main__":
    main()
