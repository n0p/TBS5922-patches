#!/usr/bin/env python3
"""Try alternative LO / crystal hypotheses for Hispasat 30W."""
import subprocess


def tune(f, sr, pol, band, ds="2", w=600):
    r = subprocess.run(["/tmp/dvbs-tune", "-a", "0", "-f", str(f), "-s", str(sr),
                        "-p", pol, "-b", band, "-d", ds, "-w", str(w)],
                       capture_output=True, text=True)
    return r.stdout.strip()


# (label, IF kHz, SR ksps, pol, band)
tests = [
    ("10730H nominal  ", 980000, 27500, "H", "L"),
    ("10730H xtal24   ", 1102500, 27500, "H", "L"),
    ("10730H xtal16   ", 1653750, 27500, "H", "L"),
    ("10730V swap     ", 980000, 27500, "V", "L"),
    ("10890V nominal  ", 1140000, 27500, "V", "L"),
    ("10890V xtal24   ", 1282500, 27500, "V", "L"),
    ("10890H swap     ", 1140000, 27500, "H", "L"),
    ("11262V nominal  ", 1512000, 30000, "V", "L"),
    ("11262V xtal24   ", 1701000, 30000, "V", "L"),
    ("11302H nominal  ", 1552000, 30000, "H", "L"),
    ("12360H univ hi  ", 1760000, 27500, "H", "H"),
    ("12360H wideband ", 1960000, 27500, "H", "L"),
    ("12360V swap     ", 1760000, 27500, "V", "H"),
    ("12052H univ hi  ", 1452000, 27500, "H", "H"),
    ("12052H wideband ", 1652000, 27500, "H", "L"),
    ("12130H wideband ", 1730000, 27500, "H", "L"),
    ("10770H nominal  ", 1020000, 30000, "H", "L"),
    ("10770H xtal24   ", 1147500, 30000, "H", "L"),
]

for label, f, sr, pol, band in tests:
    out = tune(f, sr, pol, band)
    hit = "lock=1" in out
    print(("HIT " if hit else "    ") + label + out, flush=True)
