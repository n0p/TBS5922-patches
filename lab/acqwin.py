#!/usr/bin/env python3
"""Measure the demod's symbol-rate and frequency acquisition windows."""
import subprocess


def t(f, sr, pol="H", band="H", w=500):
    r = subprocess.run(["/tmp/dvbs-tune", "-a", "0", "-f", str(f), "-s", str(sr),
                        "-p", pol, "-b", band, "-w", str(w)],
                       capture_output=True, text=True)
    kv = dict(p.split("=", 1) for p in r.stdout.split() if "=" in p)
    return kv.get("lock") == "1", kv.get("str"), kv.get("cnr")


print("# SR sweep at 12130 H (nominal SR 27500, IF 1530000)")
for sr in [10000, 15000, 18000, 20000, 22000, 24000, 25000, 26000, 27000, 27500,
           28000, 29000, 30000, 32000, 33333, 36000, 40000, 45000]:
    ok, s, c = t(1530000, sr)
    print(f"  SR={sr:6d} lock={int(ok)} str={s} cnr={c}", flush=True)

print("# frequency sweep at SR 27500 around IF 1530000")
for f in range(1500000, 1560001, 500):
    ok, s, c = t(f, 27500)
    print(f"  f={f} lock={int(ok)} str={s} cnr={c}", flush=True)
