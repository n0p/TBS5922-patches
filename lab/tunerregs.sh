#!/bin/bash
# Read AV2012 tuner registers behind the TAS2101 i2c gate, plus demod regs.
set -u
BUS=9
DEMOD=0x68
TUNER=0x63

rd() { sudo -n i2ctransfer -y $BUS w1@$1 "$2" r1 2>/dev/null; }
wr() { sudo -n i2ctransfer -y $BUS w2@$1 "$2" "$3" >/dev/null 2>&1; }
rd2() { sudo -n i2ctransfer -y $BUS w1@$1 "$2" r2 2>/dev/null | tr '\n' ' '; }

gate_on() {
    local v nv
    v=$(rd $DEMOD 0x06); v=$((v))
    nv=$((v | 0x80)); printf -v h '0x%02x' $nv; wr $DEMOD 0x06 "$h"
}
gate_off() {
    local v nv
    v=$(rd $DEMOD 0x06); nv=$((v & ~0x80)); printf -v h '0x%02x' $nv; wr $DEMOD 0x06 "$h"
}

freq=${1:-1140000}
sr=${2:-27500}
pol=${3:-V}
band=${4:-L}

/tmp/dvbs-tune -a 0 -f "$freq" -s "$sr" -p "$pol" -b "$band" -w 100 >/dev/null

echo "demod reg06 (gate) = $(rd $DEMOD 0x06)"
echo "demod 0x31 status  = $(rd $DEMOD 0x31)"
echo "demod LNB_CTRL 0x10= $(rd $DEMOD 0x10)"
echo "demod set SR 0x73/74 = $(rd2 $DEMOD 0x73)"

gate_on
echo "tuner registers (gate open):"
for r in 0x00 0x01 0x02 0x03 0x04 0x05 0x0b 0x0c 0x25; do
    echo "  tuner $r = $(rd $TUNER $r)"
done
gate_off
echo "demod reg06 after = $(rd $DEMOD 0x06)"
