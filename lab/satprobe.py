#!/usr/bin/env python3
"""Probe well-known transponders to see whether any RF chain is alive."""
import subprocess

# (sat, freq MHz, pol, symbol rate ksps)
TP = [
    ("19.2E", 11538, "V", 22000), ("19.2E", 10744, "H", 22000),
    ("19.2E", 11362, "H", 22000), ("19.2E", 11954, "H", 27500),
    ("19.2E", 12285, "V", 27500), ("19.2E", 10964, "H", 22000),
    ("13E",   11200, "V", 27500), ("13E",   11373, "H", 27500),
    ("13E",   11727, "V", 29900), ("13E",   12597, "V", 27500),
    ("13E",   10775, "H", 27500),
    ("28.2E", 10714, "H", 22000), ("28.2E", 11265, "V", 27500),
    ("28.2E", 12148, "H", 27500),
    ("30W",   11740, "V", 30000), ("30W",   12132, "H", 27500),
    ("30W",   11811, "V", 27500),
    ("5W",    11512, "V", 29950), ("5W",    12564, "V", 29950),
    ("16E",   10762, "H", 30000), ("16E",   11345, "H", 30000),
    ("9E",    11747, "H", 27500), ("9E",    12034, "V", 27500),
    ("23.5E", 11739, "V", 27500), ("23.5E", 11934, "V", 27500),
    ("0.8W",  11278, "V", 25000), ("0.8W",  11357, "H", 25000),
    ("30W",   10730, "V", 27500), ("19.2E", 12663, "H", 22000),
    ("13E",   11179, "H", 27500),
]

hits = 0
for sat, f, pol, sr in TP:
    lo = 9750 if f < 11700 else 10600
    ifreq = int((f - lo) * 1000)
    band = "L" if f < 11700 else "H"
    r = subprocess.run(["/tmp/dvbs-tune", "-a", "0", "-f", str(ifreq),
                        "-s", str(sr), "-p", pol, "-b", band, "-w", "400"],
                       capture_output=True, text=True)
    line = r.stdout.strip()
    ok = "lock=1" in line
    hits += ok
    print(("HIT  " if ok else "     ") + f"{sat:>6} {f} {pol} {sr}  if={ifreq} {line}",
          flush=True)
print(f"locks={hits}/{len(TP)}")
