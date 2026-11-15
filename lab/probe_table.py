#!/usr/bin/env python3
"""Probe every transponder of a dtv-scan-tables dvb-s file with dvbs-tune."""
import re
import subprocess
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/dvb/dvb-s/Hispasat-30W-5-30.0W"
entries = []
cur = {}
name = None
for line in open(path):
    line = line.strip()
    if line.startswith("#") or not line:
        continue
    m = re.match(r"\[(.+)\]", line)
    if m:
        if cur:
            entries.append((name, cur))
        name, cur = m.group(1), {}
        continue
    if "=" in line:
        k, v = [x.strip() for x in line.split("=", 1)]
        cur[k] = v
if cur:
    entries.append((name, cur))

print(f"# {len(entries)} transponders from {path}", flush=True)
hits = 0
for name, e in entries:
    try:
        freq = int(e["FREQUENCY"]) // 1000
        sr = int(e["SYMBOL_RATE"]) // 1000
    except (KeyError, ValueError):
        continue
    pol = "H" if e.get("POLARIZATION", "").upper().startswith("H") else "V"
    band = "L" if freq < 11700 else "H"
    lo = 9750 if band == "L" else 10600
    ifreq = freq - lo
    ds = "1" if e.get("DELIVERY_SYSTEM", "DVBS2").upper() == "DVBS" else "2"
    r = subprocess.run(["/tmp/dvbs-tune", "-a", "0", "-f", str(ifreq * 1000),
                        "-s", str(sr), "-p", pol, "-b", band, "-d", ds,
                        "-w", "500"], capture_output=True, text=True)
    out = r.stdout.strip()
    ok = "lock=1" in out
    hits += ok
    print(("HIT " if ok else "    ") + f"{name[:28]:<28} {freq} {pol} {sr} {ds} if={ifreq} {out}",
          flush=True)
print(f"# locks={hits}/{len(entries)}")
