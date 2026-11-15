#!/usr/bin/env python3
"""Check low-SR transponders with their real SR, then with a generic SR."""
import subprocess

TESTS = [
    # label, IF kHz, real SR
    ("11958H", 1358000, 7200),
    ("12003H", 1403000, 5000),
    ("12028H", 1428000, 3400),
    ("12207H", 1607000, 27500),
    ("12437H", 1837000, 27500),
    ("12169H", 1569000, 27500),
    ("12360H", 1760000, 27500),
    ("11731V", 1131000, 30000),
    ("11771V", 1171000, 30000),
]


def t(f, sr, pol, band, w=2500):
    r = subprocess.run(["/tmp/dvbs-tune", "-a", "0", "-f", str(f), "-s", str(sr),
                        "-p", pol, "-b", band, "-w", str(w)],
                       capture_output=True, text=True)
    out = r.stdout.strip()
    regs = subprocess.run(["sudo", "-n", "i2ctransfer", "-y", "9", "w1@0x68",
                           "0x5c", "r2"], capture_output=True, text=True).stdout.strip()
    return out, regs


for label, f, sr in TESTS:
    pol = "H" if label.endswith("H") else "V"
    band = "H" if f < 1500000 or label in ("11731V", "11771V") else "H"
    out, regs = t(f, sr, pol, band)
    print(f"{label} sr={sr}: {out} regs={regs}", flush=True)
    out2, regs2 = t(f, 45000, pol, band)
    print(f"{label} sr=45000: {out2} regs={regs2}", flush=True)
