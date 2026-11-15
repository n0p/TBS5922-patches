#!/usr/bin/env python3
"""peakscan.py - barrido estilo driver de Windows: 12 MHz a SR 45000 y, en cada
punto, enumeración de las portadoras que ve el TAS2101 (0x59/0x58/0x5a-0x5f).

Es la fase 1 de tbs5922.sys (ver docs/RE-windows-blindscan.md) llevada a
espacio de usuario: no prueba SR a ciegas ni espera lock, pide 45000 y el demod
devuelve la SR real de cada portadora de la ventana.

Uso:
  sudo python3 lab/peakscan.py -p V -b L --start 1450 --stop 1650
  sudo python3 lab/peakscan.py -p H -b H --start 950 --stop 2150 --step 12
Requiere dvbs-tune en /usr/local/bin y i2ctransfer (sudo o grupo i2c).
"""
import argparse
import subprocess
import sys
import time

DEMOD = 0x68
TUNE = "/usr/local/bin/dvbs-tune"


def i2c(bus, *args):
    r = subprocess.run(["i2ctransfer", "-y", str(bus), *args], capture_output=True, text=True)
    if r.returncode != 0:
        raise OSError(r.stderr.strip())
    return r.stdout.strip()


def rd(bus, reg):
    out = i2c(bus, f"w1@{DEMOD:#x}", f"{reg:#x}", "r1")
    return int(out.split()[0], 16)


def wr(bus, reg, val):
    i2c(bus, f"w2@{DEMOD:#x}", f"{reg:#x}", f"{val:#x}")


def tune(f_khz, sr, pol, band, wait=1500):
    r = subprocess.run([TUNE, "-a", "0", "-f", str(f_khz), "-s", str(sr),
                        "-p", pol, "-b", band, "-w", str(wait)],
                       capture_output=True, text=True)
    kv = {}
    for tok in r.stdout.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            kv[k] = v
    return kv


def peaks(bus):
    n = rd(bus, 0x59) & 0x1F
    if n == 0:
        return []
    base = rd(bus, 0x58) & 0xE0
    out = []
    for i in range(n):
        wr(bus, 0x58, base | i)
        b = {r: rd(bus, r) for r in (0x5A, 0x5B, 0x5C, 0x5D, 0x5F)}
        off = (b[0x5B] << 8) | b[0x5A]
        if off > 0x7FFF:
            off = ((b[0x5B] - 0x100) << 8) | b[0x5A]
        sr = (b[0x5D] << 8) | b[0x5C]
        mod = b[0x5F] & 0xE0
        if mod >= (0x40 if sr > 5000 else 0x60):
            out.append((off, sr, mod))
    wr(bus, 0x58, base)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-p", "--pol", choices="VH", required=True)
    ap.add_argument("-b", "--band", choices="LH", required=True)
    ap.add_argument("--start", type=float, default=950.0)
    ap.add_argument("--stop", type=float, default=2150.0)
    ap.add_argument("--step", type=float, default=12.0)
    ap.add_argument("--bus", type=int, default=9)
    a = ap.parse_args()

    # -b V|H en dvbs-tune es la banda: 'L'=baja, 'H'=alta (mismo valor)
    lo = 9750 if a.band == "L" else 10600
    f = a.start
    total = []
    t0 = time.time()
    while f <= a.stop + 1e-6:
        try:
            kv = tune(int(f * 1000), 45000, a.pol, a.band)
            if kv.get("errno", "0") not in ("0", ""):
                print(f"{f:7.1f} MHz  (sin frontend: {kv.get('errno')})", flush=True)
                f += a.step
                continue
            for off, sr, mod in peaks(a.bus):
                sat = f + lo + off / 1000.0
                total.append((sat, a.pol, sr, off, mod))
                print(f"{f:7.1f} MHz -> {sat:8.2f} MHz {a.pol}  SR={sr:6d}  "
                      f"off={off:+6d} kHz  0x5f&0xe0={mod:#02x}", flush=True)
        except OSError as e:
            print(f"{f:7.1f} MHz  ERROR {e}", flush=True)
        f += a.step
    print(f"# {len(total)} portadoras en {(time.time()-t0)/60:.1f} min")
    return 0


if __name__ == "__main__":
    sys.exit(main())
