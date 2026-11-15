#!/usr/bin/env python3
"""dvbs-blindscan - blind scan for DVB-S/S2 frontends without hardware
blindscan support (tested with TBS 5922 / TAS2101 + AV2012).

Two stages:

  1. AGC survey: sweep the IF band with the widest tuner bandwidth (max symbol
     rate) and record the demod AGC level.  Carriers show up as pronounced
     dips/peaks against the noise floor.
  2. Lock hunt: around every AGC feature try a list of symbol rates.  On lock
     ask the demod for the measured symbol rate, delivery system and
     modulation (get_frontend), refine the frequency, re-check that the
     measurement is stable and drop duplicates.

Output: CSV with the whole trace plus a dvbv5 channel file with the found
transponders (ready for dvbv5-scan / dvbv5-zap).

Usage:
  dvbs-blindscan [--adapter N] [--quadrants VL,HL,VH,HH] [--step MHZ]
                 [--srs 45000,27500,8000,3000] [--out PREFIX] [--agc-only]
"""
import argparse
import subprocess
import sys
import time

TUNE = "/usr/local/bin/dvbs-tune"

QUADRANTS = {
    # name: (polarisation, band, LNB LO MHz)
    "VL": ("V", "L", 9750),
    "HL": ("H", "L", 9750),
    "VH": ("V", "H", 10600),
    "HH": ("H", "H", 10600),
}

DS_NAME = {5: "DVBS", 6: "DVBS2"}
MOD_NAME = {0: "QPSK", 9: "PSK/8", 10: "APSK/16", 11: "APSK/32"}


def parse_args():
    ap = argparse.ArgumentParser()
    ap.add_argument("-a", "--adapter", type=int, default=0)
    ap.add_argument("--start", type=float, default=950.0, help="IF start MHz")
    ap.add_argument("--stop", type=float, default=2150.0, help="IF stop MHz")
    ap.add_argument("--step", type=float, default=8.0, help="AGC survey step MHz")
    ap.add_argument("--srs", default="45000,27500,15000,8000,4000,2000")
    ap.add_argument("--quadrants", default="VL,HL,VH,HH")
    ap.add_argument("--out", default="/tmp/dvbs-blindscan")
    ap.add_argument("--agc-only", action="store_true")
    ap.add_argument("--no-agc", action="store_true",
                    help="barrido plano: prueba todas las frecuencias (el AGC "
                         "del TAS2101 no es reproducible, ver SKILL)")
    ap.add_argument("--min-dip", type=int, default=6000,
                    help="AGC deviation from median considered a carrier")
    return ap.parse_args()


def tune(freq_khz, sr_ksps, pol, band, wait_ms, adapter=0):
    r = subprocess.run([TUNE, "-a", str(adapter), "-f", str(int(freq_khz)),
                        "-s", str(int(sr_ksps)), "-p", pol, "-b", band,
                        "-w", str(wait_ms)],
                       capture_output=True, text=True)
    kv = {}
    for tok in r.stdout.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            kv[k] = v
    return kv


def measure(freq_khz, sr_ksps, pol, band, adapter):
    """Tune and return (msr, ds, mod, cnr) or None if no lock."""
    kv = tune(freq_khz, sr_ksps, pol, band, 2000, adapter)
    if kv.get("lock") != "1":
        return None
    try:
        return (int(kv.get("msr", "-1") or -1), int(kv.get("ds", "-1") or -1),
                int(kv.get("mod", "-1") or -1), int(kv.get("cnr", "-1") or -1))
    except ValueError:
        return None


def main():
    args = parse_args()
    srs = [int(x) for x in args.srs.split(",") if x.strip()]
    quads = [q.strip().upper() for q in args.quadrants.split(",") if q.strip()]
    csv_path = args.out + ".csv"
    conf_path = args.out + ".conf"

    freqs = []
    f = args.start
    while f <= args.stop + 1e-6:
        freqs.append(int(round(f * 1000)))
        f += args.step

    csv = open(csv_path, "w")
    csv.write("quadrant,if_khz,agc,sr_try,lock,cnr,measured_sr,ds,mod\n")
    conf = open(conf_path, "w")
    found = []
    t_start = time.time()

    print(f"# AGC survey {args.start}-{args.stop} MHz step {args.step} "
          f"({len(freqs)} points) x {quads}", flush=True)

    for q in quads:
        pol, band, lo = QUADRANTS[q]
        agc = []
        for i, fk in enumerate(freqs if not args.no_agc else []):
            kv = tune(fk, srs[0], pol, band, 40, args.adapter)
            try:
                s = int(kv.get("str", "0"))
            except ValueError:
                s = 0
            agc.append(s)
            csv.write(f"{q},{fk},{s},,0,,,,\n")
            if (i + 1) % 25 == 0:
                print(f"# {q} AGC {i+1}/{len(freqs)}", flush=True)
        csv.flush()

        vals = sorted(agc)
        med = vals[len(vals) // 2] if vals else 0
        idx = {fk: i for i, fk in enumerate(freqs)}
        if args.no_agc:
            merged = list(freqs)
            print(f"# {q}: barrido plano, {len(merged)} frecuencias (AGC solo "
                  f"como traza)", flush=True)
            for fk in merged:
                hit = None
                for sr in srs:
                    for offset in (0,):
                        ff = fk + int(offset)
                        m = measure(ff, sr, pol, band, args.adapter)
                        if m is None:
                            csv.write(f"{q},{ff},,{sr},0,,,,\n")
                            continue
                        msr, ds, mod, cnr = m
                        csv.write(f"{q},{ff},,{sr},1,{cnr},{msr},{ds},{mod}\n")
                        csv.flush()
                        hit = (ff, msr if msr > 0 else sr, ds, mod, cnr)
                        break
                    if hit:
                        break
                if not hit:
                    continue
                ff, msr, ds, mod, cnr = hit
                chk = measure(ff, msr, pol, band, args.adapter)
                if chk is None or abs(chk[0] - msr) > max(500, msr // 20):
                    continue
                sat_khz = ff + lo * 1000
                if any(t[1] == pol and abs(t[0] - sat_khz) < 5000
                       and abs(t[2] - msr) <= max(500, msr // 20) for t in found):
                    continue
                found.append((sat_khz, pol, msr, cnr))
                ds_name = DS_NAME.get(ds, "DVBS2")
                mod_name = MOD_NAME.get(mod, "AUTO")
                print(f"HIT {q} IF={ff/1000:.1f} -> {sat_khz/1000:.0f} MHz {pol} "
                      f"SR={msr} {ds_name} {mod_name} CNR={cnr/100:.1f} dB", flush=True)
                conf.write(f"[{pol}_{int(sat_khz/1000)}_{msr}]\n"
                           f"\tDELIVERY_SYSTEM = {ds_name}\n\tFREQUENCY = {sat_khz}\n"
                           f"\tPOLARIZATION = {'HORIZONTAL' if pol == 'H' else 'VERTICAL'}\n"
                           f"\tSYMBOL_RATE = {msr * 1000}\n\tMODULATION = {mod_name}\n"
                           f"\tINNER_FEC = AUTO\n\tINVERSION = AUTO\n\n")
                conf.flush()
            continue
        features = []
        for i, s in enumerate(agc):
            if i == 0 or i == len(agc) - 1:
                continue
            if abs(s - med) < args.min_dip:
                continue
            if (s < agc[i - 1] and s < agc[i + 1]) or \
               (s > agc[i - 1] and s > agc[i + 1]):
                features.append(freqs[i])
        # cluster features within 3 steps, keep the strongest deviation
        merged = []
        for fk in features:
            if merged and fk - merged[-1] <= 3 * args.step * 1000:
                if abs(agc[idx[fk]] - med) > abs(agc[idx[merged[-1]]] - med):
                    merged[-1] = fk
                continue
            merged.append(fk)
        print(f"# {q}: median AGC {med}, {len(merged)} carrier candidates",
              flush=True)

        if args.agc_only:
            continue

        for fk in merged:
            hit = None
            for sr in srs:
                for offset in (0, -args.step * 500, args.step * 500):
                    ff = fk + int(offset)
                    m = measure(ff, sr, pol, band, args.adapter)
                    if m is None:
                        csv.write(f"{q},{ff},,{sr},0,,,,\n")
                        continue
                    msr, ds, mod, cnr = m
                    csv.write(f"{q},{ff},,{sr},1,{cnr},{msr},{ds},{mod}\n")
                    csv.flush()
                    hit = (ff, msr if msr > 0 else sr, ds, mod, cnr)
                    break
                if hit:
                    break
            if not hit:
                continue

            ff, msr, ds, mod, cnr = hit
            # refine frequency at the measured symbol rate
            step_khz = max(500, int(args.step * 500))
            best = None
            for off in range(-4, 5):
                f2 = ff + off * step_khz
                m = measure(f2, msr, pol, band, args.adapter)
                if m is None:
                    continue
                m2_msr, m2_ds, m2_mod, m2_cnr = m
                csv.write(f"{q},{f2},,{msr},1,{m2_cnr},{m2_msr},{m2_ds},{m2_mod}\n")
                if best is None or m2_cnr > best[4]:
                    best = (f2, m2_msr if m2_msr > 0 else msr, m2_ds, m2_mod, m2_cnr)
            if best:
                ff, msr, ds, mod, cnr = best

            # stability check: the measured SR must repeat
            chk = measure(ff, msr, pol, band, args.adapter)
            if chk is None or abs(chk[0] - msr) > max(500, msr // 20):
                print(f"# discard {q} IF={ff/1000:.1f}: medicion inestable "
                      f"(msr {msr} vs {chk[0] if chk else 'sin lock'})", flush=True)
                continue

            sat_khz = ff + lo * 1000
            if any(t[1] == pol and abs(t[0] - sat_khz) < 5000
                   and abs(t[2] - msr) <= max(500, msr // 20) for t in found):
                print(f"# dup {q} IF={ff/1000:.1f} MHz (ya visto)", flush=True)
                continue
            found.append((sat_khz, pol, msr, cnr))

            ds_name = DS_NAME.get(ds, "DVBS2")
            mod_name = MOD_NAME.get(mod, "AUTO")
            print(f"HIT {q} IF={ff/1000:.1f} MHz -> {sat_khz/1000:.0f} MHz {pol} "
                  f"SR={msr} {ds_name} {mod_name} CNR={cnr/100:.1f} dB", flush=True)
            conf.write(f"[{pol}_{int(sat_khz/1000)}_{msr}]\n"
                       f"\tDELIVERY_SYSTEM = {ds_name}\n"
                       f"\tFREQUENCY = {sat_khz}\n"
                       f"\tPOLARIZATION = {'HORIZONTAL' if pol == 'H' else 'VERTICAL'}\n"
                       f"\tSYMBOL_RATE = {msr * 1000}\n"
                       f"\tMODULATION = {mod_name}\n"
                       f"\tINNER_FEC = AUTO\n\tINVERSION = AUTO\n\n")
            conf.flush()

    csv.close()
    conf.close()
    print(f"# done in {(time.time()-t_start)/60:.1f} min: {len(found)} transponders")
    print(f"# csv: {csv_path}  conf: {conf_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
