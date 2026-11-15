#!/usr/bin/env python3
"""dvbs-blindscan - brute-force DVB-S/S2 blind scan for frontends without
hardware blindscan support (e.g. TBS 5922 / TAS2101).

Sweeps IF 950-2150 MHz for every LNB quadrant (polarisation x band) trying a
list of symbol rates and reports every transponder where the demod reports
FE_HAS_LOCK.  Results are appended to a CSV so a long run is resumable.

Usage:
  dvbs-blindscan [-a N] [--step MHZ] [--srs 22000,27500,...]
                 [--quadrants VL,HL,VH,HH] [--out FILE] [--timeout MS]
"""
import argparse
import os
import subprocess
import sys
import time

TUNE = "/tmp/dvbs-tune"

QUADRANTS = {
    # name: (polarisation, band)  -> LNB voltage / 22 kHz tone
    "VL": ("V", "L"),
    "HL": ("H", "L"),
    "VH": ("V", "H"),
    "HH": ("H", "H"),
}


def parse_args():
    ap = argparse.ArgumentParser()
    ap.add_argument("-a", "--adapter", type=int, default=0)
    ap.add_argument("--start", type=float, default=950.0, help="IF start MHz")
    ap.add_argument("--stop", type=float, default=2150.0, help="IF stop MHz")
    ap.add_argument("--step", type=float, default=6.0, help="IF step MHz")
    ap.add_argument("--srs", default="22000,27500,30000",
                    help="comma separated symbol rates in ksps")
    ap.add_argument("--quadrants", default="VL,HL,VH,HH")
    ap.add_argument("--timeout", type=int, default=60,
                    help="ms to poll for lock after each retune")
    ap.add_argument("--out", default="/tmp/dvbs-blindscan.csv")
    return ap.parse_args()


def main():
    args = parse_args()
    srs = [int(x) for x in args.srs.split(",") if x.strip()]
    quads = [q.strip().upper() for q in args.quadrants.split(",") if q.strip()]

    freqs = []
    f = args.start
    while f <= args.stop + 1e-6:
        freqs.append(int(round(f * 1000)))
        f += args.step

    total = len(freqs) * len(srs) * len(quads)
    print(f"# blindscan IF {args.start}-{args.stop} MHz step {args.step} MHz "
          f"({len(freqs)} points) x SR {srs} x {quads} = {total} tunes",
          flush=True)

    new = not os.path.exists(args.out)
    fh = open(args.out, "a")
    if new:
        fh.write("ts,quadrant,if_khz,sr_ksps,lock,status,str,cnr,ms\n")

    n = 0
    hits = 0
    t_start = time.time()
    for q in quads:
        pol, band = QUADRANTS[q]
        for freq in freqs:
            for sr in srs:
                n += 1
                r = subprocess.run(
                    [TUNE, "-a", str(args.adapter), "-f", str(freq),
                     "-s", str(sr), "-p", pol, "-b", band,
                     "-w", str(args.timeout)],
                    capture_output=True, text=True)
                line = r.stdout.strip()
                if not line:
                    continue
                kv = dict(p.split("=", 1) for p in line.split() if "=" in p)
                lock = kv.get("lock") == "1"
                fh.write("%s,%s,%s,%s,%d,%s,%s,%s,%s\n" % (
                    time.strftime("%Y-%m-%dT%H:%M:%S"), q, freq, sr,
                    1 if lock else 0, kv.get("status", ""), kv.get("str", ""),
                    kv.get("cnr", ""), kv.get("took", "")))
                if lock:
                    hits += 1
                    print(f"HIT {q} IF={freq} kHz SR={sr} ksps "
                          f"str={kv.get('str')} cnr={kv.get('cnr')}", flush=True)
                    fh.flush()
                if n % 50 == 0:
                    el = time.time() - t_start
                    eta = el / n * (total - n)
                    print(f"# {n}/{total} ({100.0*n/total:.1f}%) "
                          f"elapsed {el/60:.1f} min ETA {eta/60:.1f} min "
                          f"hits={hits}", flush=True)
                    fh.flush()
    fh.close()
    print(f"# done: {n} tunes, {hits} locks, {(time.time()-t_start)/60:.1f} min",
          flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
