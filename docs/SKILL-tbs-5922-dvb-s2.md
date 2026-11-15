---
name: tbs-5922-dvb-s2
description: "Operar el TBS 5922 (QBOX22) DVB-S2 USB en minibox."
category: radio
---

# TBS 5922 / QBOX22 — DVB-S2 USB en minibox (Debian 13, kernel 6.12)

Sonda satelital USB que se anuncia como `734c:5922 "TBS 5922" / "TBS QBOX22"`:
puente **Cypress FX2** + demodulador **Tmax TAS2100/2101** + sintonizador
**Airoha AV2012**. **No hay soporte en mainline**: ni `dvb-usb-tbsqbox22`, ni
`tas2101`, ni `av201x` existen en el kernel de Debian. Todo va por un paquete
DKMS local.

Estado en esta máquina: **funcionando** (adapter0 con frontend0/demux0/dvr0/net0,
engancha transpondedores reales de Hispasat 30W y `dvbv5-scan` saca canales).
El barrido completo sólo ha cubierto el cuadrante **VL** hasta la fecha; HL, VH y
HH quedan pendientes (`sudo dvbs-blindscan --step 8 --out /tmp/blindscan_30W`).

## Dónde está todo

```
/usr/src/tbs-qbox22-0.3/                 fuentes DKMS (README.md incluido)
/usr/src/tbs-qbox22-0.3/tools/           dvbs-tune.c, dvbs-blindscan.py
/usr/local/bin/dvbs-tune                 sonda de una frecuencia
/usr/local/bin/dvbs-blindscan            barrido a ciegas (2 fases)
/lib/firmware/dvb-usb-tbsqbox-id5922.fw  firmware FX2 (8 KiB, imprescindible)
/etc/modules-load.d/tbs-qbox22.conf      autocarga: dvb-usb-tbsqbox22
```

Recompilar / reinstalar:

```sh
sudo dkms build   -m tbs-qbox22 -v 0.3
sudo dkms install -m tbs-qbox22 -v 0.3 --force
sudo modprobe dvb-usb-tbsqbox22
```

Origen del código: `crazycat69/linux_media` rama `latest` (base 6.5), ficheros
`tbs-qbox22.c/.h`, `tas2101.c/.h/_priv.h`, `av201x.c/.h/_priv.h` + headers
privados (`dvb-usb.h`, `dvb-pll.h`, `include/media/dvb-usb-ids.h`) que el
paquete de headers de Debian no instala.

## Tres arreglos obligatorios sobre el original

1. `EXPORT_SYMBOL_GPL(av201x_attach)` — con `EXPORT_SYMBOL` el `dvb_attach()`
   falla con *"failing symbol_get of non-GPLONLY symbol av201x_attach"* y no se
   engancha el sintonizador (el frontend queda sin `tuner_ops`, ver trampa 1).
2. En el Makefile: `-include linux/version.h` y
   `-DCONFIG_DVB_TAS2101=1 -DCONFIG_DVB_TAS2101_MODULE=1` +
   `-DCONFIG_MEDIA_TUNER_AV201X=1 -DCONFIG_MEDIA_TUNER_AV201X_MODULE=1`.
   Los headers usan `IS_ENABLED(CONFIG_...)`: fuera del árbol, sin esos defines
   se compilan los stubs vacíos que devuelven NULL.
3. Parámetro `tas2101.lock_retries` (uint, 0644, por defecto 15): reintentos de
   lock de 20 ms tras cada sintonía.

## Uso

```sh
# una frecuencia: -f IF en kHz, -s SR en ksps, -p V|H, -b L(banda baja)|H(alta)
dvbs-tune -a 0 -f 1530000 -s 27500 -p H -b H -w 2000
# -> lock=1 status=0x1f str=53590 cnr=1290 msr=27500 ds=6 mod=9 ...
#    msr = SR medida (ksps), ds 6=DVBS2/5=DVBS, mod 9=PSK/8, 0=QPSK
#    cnr va en centesimas de dB (1290 = 12,90 dB)

# barrido a ciegas completo (4 cuadrantes V/H x banda baja/alta).
# --no-agc = barrido plano (recomendado: el AGC del TAS2101 no es reproducible)
sudo dvbs-blindscan --no-agc --step 4 --srs 45000 --out /tmp/blindscan_30W
sudo dvbs-blindscan --no-agc --step 4 --srs 8000  --out /tmp/blindscan_30W_b
# deja /tmp/blindscan_30W.csv (traza: mezcla filas de AGC y de lock) y
# /tmp/blindscan_30W.conf (DVBv5; puede traer entradas duplicadas: deduplicar
# antes de pasarlo a dvbv5-scan, aunque dvbv5-scan no se rompe con ellas)

# enumerar canales de lo encontrado
dvbv5-scan -a 0 -l EXTENDED -o /tmp/canales.conf /tmp/blindscan_30W.conf
```

IF = frecuencia del satélite − LO del LNB (9750 banda baja, 10600 banda alta).

## Trampas (todas comprobadas a base de hostias)

1. **La API DVBv5 exige `DTV_TUNE`.** Mandar `DTV_FREQUENCY`, `DTV_SYMBOL_RATE`,
   `DTV_VOLTAGE` y `DTV_TONE` sin `DTV_TUNE` **no sintoniza nada**: el voltaje y
   el tono sí se aplican (el registro `LNB_CTRL` cambia), lo que despista y hace
   creer que hay señal. Fue el primer fallo de la herramienta de pruebas.
2. **`FE_READ_STATUS` miente**: `tas2101_read_status()` pone siempre
   `FE_HAS_SIGNAL|FE_HAS_CARRIER`. Sólo `FE_HAS_LOCK` (reg `0x31` == `0x75`) es
   real.
3. **Hace falta esperar ~0,7–0,9 s al lock.** El `set_frontend` del driver sólo
   sondea 15×20 ms y devuelve `-EINVAL`; el demod engancha después. Con `-w 500`
   se pierden la mitad de los transpondedores; usar `-w 2000`.
4. **El AGC no es reproducible**: el `SIGSTR` del TAS2101 depende del estado
   de adaptación y oscila; los "nulos" que se ven en un barrido no salen en el
   siguiente. Por eso la fase de detección por AGC **no es fiable** y el
   barrido bueno es el plano (`--no-agc`), que prueba todas las frecuencias.
   **Cada sintonía cuesta ~0,75 s** (el FX2 atiende cada transferencia de control
   con ~35 ms de latencia y el driver escribe registro a registro). Un barrido
   de 1200 puntos son ~25 min por SR. No hay atajo: tocar sólo el PLL del
   sintonizador y leer el AGC (`dvbs-spectrum`, ~50 ms/punto) **no reproduce**
   los niveles reales; el AGC del TAS2101 sólo es fiable tras la secuencia
   completa de `set_frontend`.
5. **Tolerancia de SR muy ancha**: para un mux de 27,5 MS/s el demod engancha
   pidiendo desde 20 hasta 45 MS/s y devuelve la SR real en los registros
   `GET_SRATE` (`0x5c/0x5d`) — eso es lo que expone `dvbs-tune` como `msr`, y es
   la base del barrido a ciegas. Con SR pequeños (<10 MS/s) hay que probar SR
   cercanos.
6. **El sintonizador está detrás de una puerta I2C**: para leer/escribir el
   AV2012 (0x63) hay que poner el bit `0x80` del registro `0x06` del demod
   (0x68). `dvbs-spectrum` y los scripts de registro lo hacen.
7. **`/dev/i2c-9` es `root:i2c` 660**: para I2C directo hace falta `sudo` o
   meter al usuario en el grupo `i2c`. El `LNB_CTRL` es el registro `0x10` del
   demod: bit `0x40` = 18 V (si no, 13 V), bit `0x01` = tono 22 kHz.
8. **No escribir números de serie** en notas ni ficheros del proyecto.

## Blindscan: qué hay y qué no

El **TBS 5922 sí hace blindscan bajo Windows**, pero lo ejecuta el *driver de
Windows*: la herramienta oficial (`tbs-blindscan_v3.0.2.2.exe`) es un PE nativo
de 32 bits que sólo importa `kernel32/user32/oleaut32/advapi32` — es una app
BDA/DirectShow que habla por COM con el driver de TBS. No se puede usar aquí.

En Linux el `tas2101` **no expone** `FE_CAN_BLINDSEARCH` ni la API DVBv5 de
blindscan, así que `blindscan(1)` de dvb-apps no sirve. Opciones reales:

* `dvbs-blindscan` (lo instalado): fase 1 mapa de AGC cada 8–10 MHz con el
  ancho de banda máximo (SR 45000 → BW 40 MHz), fase 2 caza de lock en cada
  portadora probando varios SR y leyendo luego la SR real. 25–45 min los cuatro
  cuadrantes. Es lo que ha encontrado los transpondedores actuales.
* Pila **neumo** de deeptho (`neumo-drivers` + `neumo-blindscan`): única vía
  "asistida por hardware" en Linux; implementa *spectrum scan* para tas2101
  (resolución pobre y marcada como incompleta para este demod) y **sustituye
  toda la pila DVB** del equipo. El qbox22 está en su lista de USB soportados.

## Resultados verificados (Hispasat 30W, 2026-11)

* H: 11302 H 30000, 11382 H 30000, 12130 H 27500, 12284 H 27500,
  12360 H 27500, 12399 H 27500, 12437 H 27500.
* V: mux de 34280 MS/s entre 11296 y 11340 MHz (el "test card" 34290 V de las
  tablas de LyngSat) y uno de 10200 MS/s en 11504 MHz.
* `dvbv5-scan` sobre 12130 H 27500 devuelve servicios reales (RTP1, RTP2,
  Record Europa...), o sea TS decodificado de punta a punta.
* La tabla `/usr/share/dvb/dvb-s/Hispasat-30W-5-30.0W` (dtv-scan-tables) tiene
  entradas obsoletas: varios SR pequeños no existen ya.

## Inventario completo de ficheros (en `/usr/src/tbs-qbox22-0.3/`)

```
Makefile  dkms.conf  README.md          driver + empaquetado DKMS
*.c *.h include/media/dvb-usb-ids.h     fuentes y headers privados
tools/dvbs-tune.c                       fuente de dvbs-tune
tools/dvbs-blindscan.py                 fuente de dvbs-blindscan (2 fases)
tools/dvbs-spectrum.c                   espectro rápido por PLL+AGC (EXPERIMENTAL:
                                        no reproduce los nulos reales, ver trampa 4)
docs/SKILL-tbs-5922-dvb-s2.md           copia de este skill
lab/INDEX.md                            índice de los scripts de exploración
lab/probe_table.py                      sondear un fichero de dtv-scan-tables
lab/subset_table.py                     extraer transpondedores concretos
lab/agcsweep.py                         barrido de AGC fiable (referencia)
lab/acqwin.py                           ventana de adquisición de SR y frecuencia
lab/lowsr.py                            comportamiento con SR pequeños
lab/hypo.py                             descartar hipótesis de cristal/polarización
lab/satprobe.py                         smoke test contra 30 TP conocidos
lab/tunerregs.sh                        volcado de registros tras sintonizar
lab/dvbs-blindscan-v1.py                primera versión (fuerza bruta pura)
lab/results/                            trazas y .conf del cuadrante VL ya barrido
```

Los scripts de `lab/` llaman a `/tmp/dvbs-tune`: copiarlo antes con
`sudo cp /usr/local/bin/dvbs-tune /tmp/dvbs-tune`. El detalle de qué registro
del TAS2101 es cada cosa (0x06 puerta, 0x10 LNB_CTRL, 0x31 estado, 0x42 AGC,
0x5c SR medida...) está en `lab/INDEX.md`.

## Estado del barrido

`--no-agc --step 4 --srs 45000` = 301 puntos por cuadrante, ~1,3 s por punto
(unos 26 min los cuatro). En modo `--no-agc` no se hace la pasada de AGC: se
sintoniza directamente cada frecuencia, se para en el primer lock y se valida
repitiendo la medida (si la SR medida no se repite, el hit se descarta) y
deduplicando por frecuencia+SR.

Pendiente en esta máquina: HL, VH y HH del barrido de 30W (VL ya hecho, sus
trazas están en `lab/results/`).
