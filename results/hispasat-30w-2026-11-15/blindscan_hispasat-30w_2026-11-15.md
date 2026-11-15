# Blindscan Hispasat 30W — 2026-11-15

Origen: sudo dvbs-blindscan --no-agc --step 4 --srs 45000 --out /tmp/scan_full
Cuadrantes completos: HH HL VH VL 

HITs por cuadrante (de log):
     18 HH
     22 HL
     11 VH
     14 VL

Transpondedores en el .conf: 65

Ojo: los HIT con CNR=0.0 dB son locks espurios (VH 12098-12106, HH 11662) y
estan escritos en el .conf; descartar cnr<=0 y colapsar hits con la misma SR
separados menos de un ancho de banda antes de usar el .conf.

Version limpia: blindscan_hispasat-30w_2026-11-15.limpio.conf
  135 locks -> 30 grupos -> 24 transpondedores (descartados los grupos con
  cnr<=0). Dudosos aun: 10912 H 2500 y 12554 H 8000 (CNR=1.0 dB, una sola
  medida); con umbral cnr>=5 y >=2 medidas desaparecen.

Cross-check con el detector de portadoras (lab/peakscan.py, 6 MHz, V/L):
11222.5 / 11262.5 / 11302.5 / 11343.0 MHz 34280  vs.  11224 / 11264 / 11305 /
11344 del .conf limpio: coinciden.
