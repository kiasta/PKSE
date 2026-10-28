#!/usr/bin/env python3
"""Fetch the ribbon and mark artwork into romfs/sprites/ribbons/.

THESE ARE THE GAMES' OWN RIBBON ICONS, as PKHeX's ribbon editor draws them: one 30x30 image per
ribbon or mark, named after PKHeX's ribbon property in lower case (`ribbonchampionkalos.png`,
`ribbonmarklunchtime.png`). Names::RibbonDefinition::spriteName carries that stem, so a ribbon is
drawn by looking its own row up, never by a second table of filenames.

THE WHOLE UPSTREAM FOLDER IS MIRRORED, not a list of files. The folder is listed through the GitHub
API and every PNG in it is fetched, so a ribbon PKHeX adds arrives without editing this script --
and SuiteAssets checks that every sprite name the ribbon tables can produce is on the card, which
is what catches a table row whose art was never fetched.

Three ribbons have more than one image, chosen at runtime by the ribbon's value:
  * the five Gen 3 contest ribbons -- `ribbong3cool`, `...super`, `...hyper`, `...master` -- one per
    rank, because the format stores the rank reached rather than four flags;
  * Contest Memory and Battle Memory -- `ribboncountmemorycontest` and a gold `...2` drawn once the
    count reaches its maximum, the same switch PKHeX's RibbonSpriteUtil makes.

Not routed through pkhex_source.py: that module serves PKHeX.Core, and these live in
PKHeX.Drawing.Misc, so PKHEX_LOCAL would not find them.

Needs nothing but Python 3.

Run:  python tools/gen_ribbons.py            # fetch whatever is missing
      python tools/gen_ribbons.py --force    # re-fetch all of them
"""
import argparse
import json
import os
import sys
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from asset_fetch import USER_AGENT, fetch_all, present  # noqa: E402

UPSTREAM_FOLDER = "PKHeX.Drawing.Misc/Resources/img/ribbons"
LISTING_URL = "https://api.github.com/repos/kwsch/PKHeX/contents/%s?ref=master" % UPSTREAM_FOLDER
BASE_URL = "https://raw.githubusercontent.com/kwsch/PKHeX/master/%s" % UPSTREAM_FOLDER
OUT_DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "romfs", "sprites", "ribbons",
)
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


def upstream_file_names():
    """Every PNG in the upstream ribbon folder, by name."""
    request = urllib.request.Request(LISTING_URL, headers={
        "User-Agent": USER_AGENT,
        "Accept": "application/vnd.github+json",
    })
    with urllib.request.urlopen(request, timeout=60) as response:
        entries = json.loads(response.read().decode("utf-8"))
    names = sorted(entry["name"] for entry in entries
                   if entry.get("type") == "file" and entry["name"].endswith(".png"))
    if not names:
        raise SystemExit("ERROR: the upstream ribbon folder listed no images: %s" % LISTING_URL)
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--force", action="store_true",
                        help="re-fetch even when the image is already on disk")
    args = parser.parse_args()
    os.makedirs(OUT_DIR, exist_ok=True)

    names = upstream_file_names()
    jobs = [(name, "%s/%s" % (BASE_URL, name), os.path.join(OUT_DIR, name)) for name in names]
    failures = fetch_all(jobs, force=args.force, magic=PNG_MAGIC)
    if failures:
        sys.exit(1)

    missing = [name for name in names if not present(os.path.join(OUT_DIR, name))]
    if missing:
        print("ERROR: %d ribbon image(s) still missing: %s" % (len(missing), ", ".join(missing)),
              file=sys.stderr)
        sys.exit(1)
    print("all %d ribbon images present in %s" % (len(names), OUT_DIR))


if __name__ == "__main__":
    main()
