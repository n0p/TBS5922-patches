#!/usr/bin/env python3
"""Extract selected transponders of a dtv-scan-tables file into a new file."""
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
wanted = set(int(x) for x in sys.argv[3].split(","))

blocks = []
cur = None
for line in open(src):
    if line.strip().startswith("#"):
        continue
    m = re.match(r"\[(.+)\]\s*$", line.strip())
    if m:
        cur = [m.group(1), []]
        blocks.append(cur)
    elif cur is not None:
        cur[1].append(line)

out = []
for name, lines in blocks:
    freq = None
    for ln in lines:
        if "FREQUENCY" in ln and "=" in ln:
            try:
                freq = int(ln.split("=")[1].strip())
            except ValueError:
                pass
    if freq in wanted:
        out.append("[%s]\n" % name)
        out.extend(lines)

with open(dst, "w") as fh:
    fh.write("".join(out))
print(f"{dst}: {len([b for b in blocks if any('FREQUENCY' in l and str(w) in l for l in b[1] for w in wanted)])} blocks written")
