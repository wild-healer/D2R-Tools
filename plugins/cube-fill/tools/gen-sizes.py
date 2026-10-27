#!/usr/bin/env python3
# Writes src/sizes.inc: stock item sizes (invwidth x invheight) of every item in
# the base game's weapons/armor/misc.txt that is bigger than 1x1.
# Usage: gen-sizes.py <excel folder> > src/sizes.inc
import csv, sys

folder = sys.argv[1]
seen = set()
print("// Stock item sizes (invwidth x invheight of the base game's weapons/armor/misc.txt),")
print("// written by tools/gen-sizes.py. Items not listed are 1x1.")
for name in ("weapons", "armor", "misc"):
    with open(f"{folder}/{name}.txt", encoding="latin-1") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            code = (row.get("code") or "").strip()
            try:
                w, h = int(row["invwidth"]), int(row["invheight"])
            except (KeyError, ValueError):
                continue
            if code and code not in seen and (w, h) != (1, 1):
                seen.add(code)
                print(f'{{ "{code}", {w}, {h} }},')
