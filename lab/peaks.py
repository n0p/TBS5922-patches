#!/usr/bin/env python3
"""peaks.py - enumera las portadoras que ve el TAS2101 en la ventana sintonizada.

Replica el protocolo que usa tbs5922.sys (Windows) en su fase de barrido:
  0x59 & 0x1f  -> número de portadoras detectadas
  0x58         -> selecciona el índice (bits 4:0, se preservan los bits 7:5)
  0x5a/0x5b    -> offset de frecuencia de esa portadora (15 bits con signo, kHz)
  0x5c/0x5d    -> SR medida (ksps)
  0x5e/0x5f    -> estado de modulación/FEC (0x5f & 0xe0 valida)

El driver Linux no usa estos registros: es lo que le falta para barrer sin
probar SRs a ciegas. Ver docs/RE-windows-blindscan.md.

Uso:
  sudo dvbs-tune -a 0 -f 1530000 -s 45000 -p H -b H -w 2000
  sudo python3 lab/peaks.py                 # lee el estado actual
  sudo python3 lab/peaks.py --tune 1530000 45000 H H   # sintoniza y lee
"""
import argparse
import shutil
import subprocess
import sys

DEMOD = 0x68


def i2c(bus, *args):
    if not shutil.which("i2ctransfer"):
        sys.exit("falta i2ctransfer (paquete i2c-tools)")
    cmd = ["i2ctransfer", "-y", str(bus)] + list(args)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"{' '.join(cmd)}: {r.stderr.strip()}")
    return r.stdout.strip()


def rd(bus, reg):
    out = i2c(bus, f"w1@{DEMOD:#x}", f"{reg:#x}", "r1")
    return int(out.split()[0], 16)


def wr(bus, reg, val):
    i2c(bus, f"w2@{DEMOD:#x}", f"{reg:#x}", f"{val:#x}")


def tune(args):
    tool = shutil.which("dvbs-tune") or "/usr/local/bin/dvbs-tune"
    cmd = [tool, "-a", "0", "-f", str(args.tune[0]), "-s", str(args.tune[1]),
           "-p", args.tune[2], "-b", args.tune[3], "-w", "2000"]
    print("+", " ".join(cmd), flush=True)
    r = subprocess.run(cmd, capture_output=True, text=True)
    print(r.stdout.strip() or r.stderr.strip(), flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bus", type=int, default=9)
    ap.add_argument("--tune", nargs=4, metavar=("IF_KHZ", "SR", "POL", "BAND"))
    ap.add_argument("--raw", action="store_true", help="vuelca los bytes crudos")
    args = ap.parse_args()
    bus = args.bus

    if args.tune:
        tune(args)

    st = rd(bus, 0x31)
    n = rd(bus, 0x59) & 0x1F
    sel = rd(bus, 0x58)
    print(f"0x31 status=0x{st:02x}  0x59=0x{rd(bus,0x59):02x} -> {n} portadora(s)"
          f"  0x58=0x{sel:02x}")
    if n == 0:
        return 0

    base = sel & 0xE0
    for i in range(n):
        wr(bus, 0x58, base | i)
        b = {r: rd(bus, r) for r in (0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F)}
        off = (b[0x5B] << 8) | b[0x5A]
        if off > 0x7FFF:
            off = ((b[0x5B] - 0x100) << 8) | b[0x5A]
        sr = (b[0x5D] << 8) | b[0x5C]
        mod = b[0x5F] & 0xE0
        ok = mod >= (0x40 if sr > 5000 else 0x60)
        flag = "ok" if ok else "descartada"
        print(f"  [{i}] off={off:+6d} kHz  sr={sr:6d} ksps  0x5e/0x5f="
              f"{b[0x5E]:02x}/{b[0x5F]:02x} mod=0x{mod:02x}  {flag}")
        if args.raw:
            print("       " + " ".join(f"{r:#04x}={v:02x}" for r, v in b.items()))
    wr(bus, 0x58, sel)
    return 0


if __name__ == "__main__":
    sys.exit(main())
