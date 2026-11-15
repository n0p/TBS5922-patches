#!/usr/bin/env python3
"""Quick AGC (signal strength) sweep across the IF band to locate energy."""
import subprocess
import sys

pol = sys.argv[1] if len(sys.argv) > 1 else "H"
band = sys.argv[2] if len(sys.argv) > 2 else "L"
start = int(sys.argv[3]) if len(sys.argv) > 3 else 950000
stop = int(sys.argv[4]) if len(sys.argv) > 4 else 2150000
step = int(sys.argv[5]) if len(sys.argv) > 5 else 20000
sr = sys.argv[6] if len(sys.argv) > 6 else "45000"

vals = []
f = start
while f <= stop:
    r = subprocess.run(["/tmp/dvbs-tune", "-a", "0", "-f", str(f), "-s", sr,
                        "-p", pol, "-b", band, "-w", "40"],
                       capture_output=True, text=True)
    kv = dict(p.split("=", 1) for p in r.stdout.split() if "=" in p)
    s = int(kv.get("str", "0"))
    vals.append((f, s))
    f += step

base = sorted(v for _, v in vals)[len(vals) // 2]
print(f"# pol={pol} band={band} sr={sr} median={base} n={len(vals)}")
for i, (f, s) in enumerate(vals):
    mark = ""
    if 0 < i < len(vals) - 1:
        if s < vals[i - 1][1] and s < vals[i + 1][1] and abs(s - base) > 3000:
            mark = "  <-- dip (strong)"
        elif s > vals[i - 1][1] and s > vals[i + 1][1] and abs(s - base) > 3000:
            mark = "  <-- peak"
    print(f"{f/1000:8.1f} MHz  str={s:6d}{mark}")
