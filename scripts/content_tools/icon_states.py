#!/usr/bin/env python
"""\

Derive the _Press and _Disabled state variants of a UI icon from its _Off original.

The viewer's icon sets follow one rule: every state shares the same RGB, and only
the alpha channel is scaled.  Measured across the existing sets (AddItem,
TrashItem, MinusItem, OptionsMenu, ClipboardMenu, ...) the factors are:

    _Press      alpha * 0.67
    _Disabled   alpha * 0.25

Usage:

    python icon_states.py V:/Design/icons                     # in place, next to the _Off files
    python icon_states.py src/*.png -o indra/.../textures/icons
    python icon_states.py src -o out --dry-run

$LicenseInfo:firstyear=2026&license=viewerlgpl$
Alchemy Viewer Source Code
Copyright (C) 2026, Alchemy Viewer Project.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation;
version 2.1 of the License only.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
$/LicenseInfo$
"""

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required: pip install Pillow")

# state suffix -> alpha multiplier applied to the _Off original
STATES = {
    "Press": 0.67,
    "Disabled": 0.25,
}

OFF_SUFFIX = "_Off"


def derive(off_path, state, factor, out_dir, dry_run=False, force=False):
    """Write one state variant beside (or under out_dir) the given _Off icon."""
    stem = off_path.stem[: -len(OFF_SUFFIX)]
    dest = (out_dir or off_path.parent) / f"{stem}_{state}{off_path.suffix}"

    if dest.exists() and not force:
        print(f"  skip {dest.name} (exists, use --force to overwrite)")
        return False

    img = Image.open(off_path).convert("RGBA")
    alpha = img.getchannel("A").point(lambda a: min(255, int(round(a * factor))))
    img.putalpha(alpha)

    print(f"  {'would write' if dry_run else 'write'} {dest.name} (alpha x {factor})")
    if not dry_run:
        dest.parent.mkdir(parents=True, exist_ok=True)
        img.save(dest, optimize=True)
    return True


def collect(inputs):
    """Expand the command line into a sorted list of _Off png files."""
    found = []
    for raw in inputs:
        path = Path(raw)
        if path.is_dir():
            found.extend(path.glob(f"*{OFF_SUFFIX}.png"))
        elif path.is_file():
            if not path.stem.endswith(OFF_SUFFIX):
                print(f"ignoring {path.name}: not an {OFF_SUFFIX} original")
                continue
            found.append(path)
        else:
            print(f"ignoring {raw}: no such file or directory")
    return sorted(set(found))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("$LicenseInfo")[0].strip(),
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inputs", nargs="+", help="_Off png files, or directories holding them")
    parser.add_argument("-o", "--out-dir", type=Path, default=None,
                        help="where to write the variants (default: beside the original)")
    parser.add_argument("-f", "--force", action="store_true", help="overwrite existing variants")
    parser.add_argument("-n", "--dry-run", action="store_true", help="report without writing")
    args = parser.parse_args()

    originals = collect(args.inputs)
    if not originals:
        sys.exit(f"nothing to do: no *{OFF_SUFFIX}.png found")

    written = 0
    for off_path in originals:
        print(off_path.name)
        for state, factor in STATES.items():
            written += derive(off_path, state, factor, args.out_dir, args.dry_run, args.force)

    print(f"\n{len(originals)} icon(s), {written} variant(s) {'planned' if args.dry_run else 'written'}")


if __name__ == "__main__":
    main()
