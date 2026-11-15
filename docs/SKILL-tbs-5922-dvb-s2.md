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
~/src/TBS5922-patches/                   paquete + parches + herramientas + resultados
                                         (remoto: github.com/n0p/TBS5922-patches)
```

Recompilar / reinstalar:

```sh
sudo dkms build   -m tbs-qbox22 -v 0.3
sudo dkms install -m tbs-qbox22 -v 0.3 --force
sudo modprobe dvb-usb-tbsqbox22
```

`dkms install --force` **no siempre recoge los fuentes nuevos**: reutiliza el
`.ko` ya construido y el módulo instalado sigue con el código viejo. Si has
tocado `*.c`, borra y reconstruye el paquete entero:

```sh
sudo dkms remove  -m tbs-qbox22 -v 0.3 --all
sudo dkms add     -m tbs-qbox22 -v 0.3
sudo dkms build   -m tbs-qbox22 -v 0.3 && sudo dkms install -m tbs-qbox22 -v 0.3
```

Verificar que lo cargado es lo editado: el módulo dvb-usb se llama
`dvb_usb_tbsqbox22` (`modinfo -F version tbs-qbox22` no devuelve nada), así que
`modinfo -F version dvb_usb_tbsqbox22` contra `MODULE_VERSION` del fuente, y la
fecha del `.ko` de `/lib/modules/$(uname -r)/updates/dkms/` contra la del
último cambio en `/usr/src/tbs-qbox22-0.3/` — `.ko` anterior al `touch` = no
está cargado lo que crees. Para la prueba fuerte (¿reproduce el árbol lo
instalado?) usar `srcversion`, ver `references/auditar-paquete-driver.md` §3.

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
# -> lock=1 status=0x1f str=53590 cnr=12900 msr=27500 ds=6 mod=9 ...
#    msr = SR medida (ksps), ds 6=DVBS2/5=DVBS, mod 9=PSK/8, 0=QPSK
#    cnr va en MILESIMAS de dB (FE_SCALE_DECIBEL): 12900 = 12,90 dB

# barrido a ciegas completo (4 cuadrantes V/H x banda baja/alta).
# --no-agc = barrido plano (recomendado: el AGC del TAS2101 no es reproducible)
sudo dvbs-blindscan --no-agc --step 4 --srs 45000 --out /tmp/blindscan_30W
sudo dvbs-blindscan --no-agc --step 4 --srs 8000  --out /tmp/blindscan_30W_b
# deja /tmp/blindscan_30W.csv (traza: mezcla filas de AGC y de lock) y
# /tmp/blindscan_30W.conf (DVBv5; puede traer entradas duplicadas: deduplicar
# antes de pasarlo a dvbv5-scan, aunque dvbv5-scan no se rompe con ellas)
# MODULATION debe salir 8PSK/16APSK/32APSK: libdvbv5 trae el alias PSK/8, pero
# lo que escribe dvbv5-scan y lo que espera el formato es 8PSK

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
9. **Escalas de estadísticas** (API DVBv5): `FE_SCALE_DECIBEL` = 1/1000 dB y va
   en `svalue` (con signo); `FE_SCALE_RELATIVE` = 0..65535 en `uvalue`.
   `tas2101_read_snr()` calcula `val` en **décimas** de dB (la tabla lo canta:
   `tas2101_priv.h` `{10, 0x65a} /* 1.0 dB */`), así que el CNR correcto es
   `100 * val`; con `10 * val` la lectura sale 10× baja (1290 = 1,29 dB).
   **Un CNR de 1,x dB con lock es un error de escala, no señal mala**: DVB-S2 no
   engancha por debajo de ~5 dB. Y si una herramienta divide por 100 para pasar
   a dB, también está mal: el divisor es 1000.
   El parche `0001` del repo `TBS5922-patches` aplica `10 * val` y sus
   herramientas dividen por 100: las dos cosas están mal por lo mismo, así que
   esa serie no se da por buena sin corregirla antes a `100 * val` (y `/1000`
   en lo que imprima dB).
10. **Un barrido en marcha es dueño del frontend.** No lanzar `dvbs-tune`,
   `dvb-fe-tool`, `dvbv5-scan`/`-zap` ni recargar módulos mientras corre uno: es
   el mismo `frontend0` y un `modprobe -r` aborta el barrido a medias. Para
   auditar un barrido ajeno, sólo lecturas de ficheros: `pgrep -af`,
   `ls -l /proc/<pid>/fd`, `ps -o etime= -p <pid>`, la cabecera del log y el
   `wc -l` del `.csv`. Ojo: el dueño puede no ser tuyo (la web skywandering, un
   locker de captura, otro SSH). Si el `open` falla verás
   `Device or resource busy` y **después** una cascada de `Bad file descriptor`
   en todos los ioctls: eso es convivencia, no un driver degradado. Confirma
   con `sudo fuser -v /dev/dvb/adapter0/frontend0` antes de tocar el módulo.
   Para esperar a que termine un proceso ajeno usa `ps -p <pid>`, **no
   `kill -0 <pid>`**: sin permiso para señalizarlo `kill -0` devuelve EPERM y el
   bucle concluye que ya terminó cuando sigue vivo.

## Blindscan: qué hay y qué no

El **TBS 5922 sí hace blindscan bajo Windows**, y lo hace **el driver, no la
app**: `tbs5922.sys` lanza un hilo de sistema (`PsCreateSystemThread`, rutina en
RVA 0xa0fc) desde una propiedad KS y barre 950–2150 MHz **en pasos de 12 MHz con
SR fijo 45000**, sin esperar lock (sale cuando el estado de `0x31` llega a 2).
En cada punto el demod le dice **cuántas portadoras ve y cuáles** (`0x59` cuenta,
`0x58` índice, `0x5a/0x5b` offset de frecuencia, `0x5c/0x5d` SR real, `0x5f`
validez). Es decir: el TAS2101 **sí tiene detector multi-portadora**; el driver
Linux no lo usa y de ahí que haya que probar SR a ciegas. Detalle completo,
registros y pseudocódigo en `/usr/src/tbs-qbox22-0.3/docs/RE-windows-blindscan.md`;
sondas listas en `lab/peaks.py` (una ventana), `lab/peakscan.py` (barrido en
Python) y `tools/dvbs-peakscan.c` (el bueno: un proceso, ioctl directos,
340 ms/punto, 12 MHz, 34 s por cuadrante + confirmación por lock).

**El paso de 12 MHz del driver de Windows vale** (un intento mío anterior decía
que no: era artefacto de leer `0x59` antes de tiempo). Medido en 1450–1650 MHz
V/L con los 4 muxes de 34280: con 12 MHz salen los 4. Trampas que hay que
respetar, todas medidas:

1. `0x59` **retiene** el recuento del punto anterior: escribir 0 en `0x59` antes
   de retunear, si no salen portadoras fantasma con el offset viejo.
2. El detector tarda **~300 ms** en publicar (con 150 ms salen 0). No cortes al
   ver `0x59 != 0` ni al subir el nivel de `0x31` (oscila 1→5→1).
3. **`lock_retries=0` rompe el barrido**: el driver no llama a `read_status()` y
   el demod no se re-sintoniza (se queda pegado y repite offset). Usa 1 (~20 ms).
4. La fase 1 da **candidatos**: confirma cada uno con una sintonía real y
   `FE_HAS_LOCK` (en V/L completo: 20 candidatos → 7 reales).
Verificado también que con SR pedida 45000 devuelve la SR exacta (27 500 y 34 280
en dos muxes conocidos) y 0 portadoras en una frecuencia vacía: la lectura no es
ruido, es el detector.

El ejecutable `tbs-blindscan_v3.0.2.2.exe` no sirve aquí: PE32 que sólo importa
`kernel32/user32/oleaut32/advapi32` (es el instalador/launcher de la app de
escritorio, con un blob de 463 KB en overlay).

En Linux el `tas2101` **no expone** `FE_CAN_BLINDSEARCH` ni la API DVBv5 de
blindscan, pero **no porque el chip no sepa**: el detector de portadoras del
TAS2101 existe y funciona (`lab/peaks.py`, `lab/peakscan.py`). Lo que falta es
que el módulo lo publique; `blindscan(1)` de dvb-apps no sirve. Opciones reales:

* `dvbs-blindscan` (lo instalado): fase 1 mapa de AGC cada 8–10 MHz con el
  ancho de banda máximo (SR 45000 → BW 40 MHz), fase 2 caza de lock en cada
  portadora probando varios SR y leyendo luego la SR real. Con `--no-agc` son
  ~2 h los cuatro cuadrantes (ver «Estado del barrido»). Es lo que ha
  encontrado los transpondedores actuales.
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
lab/peaks.py                            enumera portadoras vía 0x59/0x58/0x5a-0x5f
                                        (protocolo del driver de Windows)
docs/RE-windows-blindscan.md            RE del blindscan de tbs5922.sys (radare2)
lab/results/                            trazas y .conf del cuadrante VL ya barrido
```

Los scripts de `lab/` llaman a `/tmp/dvbs-tune`: copiarlo antes con
`sudo cp /usr/local/bin/dvbs-tune /tmp/dvbs-tune`. El detalle de qué registro
del TAS2101 es cada cosa (0x06 puerta, 0x10 LNB_CTRL, 0x31 estado, 0x42 AGC,
0x5c SR medida...) está en `lab/INDEX.md`.

## Estado del barrido

`--no-agc --step 4 --srs 45000` = 301 puntos por cuadrante. En modo `--no-agc`
no se hace la pasada de AGC: se sintoniza cada frecuencia, se para en el primer
lock y se valida repitiendo la medida (si la SR medida no se repite, el hit se
descarta) y deduplicando por frecuencia+SR.

**El ETA lo manda el coste del punto vacío, y no es 1,3 s.** `measure()` usa
`-w 2000` y prueba hasta 3 offsets (±1 paso) por SR: un punto sin lock cuesta
~6–7 s, así que 301 × 4 cuadrantes ≈ **2 h por SR**, y una segunda pasada con
`--srs 8000` duplica. No estimes a ojo: muestrea el `-f` del hijo `dvbs-tune`
dos veces separadas ~90 s y saca puntos/s.

```sh
ps -o args= -p $(pgrep -P $(pgrep -f dvbs-blindscan | head -1) -f dvbs-tune); \
  sleep 90; ps -o args= -p $(pgrep -P $(pgrep -f dvbs-blindscan | head -1) -f dvbs-tune)
```

Para bajar el ETA, un `--wait` corto (400 ms para barrer, 2000 ms sólo para
refinar/validar) rinde más que probar más SR u offsets.

**El CSV no se vuelca en los fallos**: el `csv.flush()` del script sólo está en
la rama de lock, así que la traza en disco se queda en la cabecera hasta el
primer hit y un kill se lleva todo lo bufferizado. Antes de dar un barrido por
lanzado, comprobar que `wc -l` del `.csv` crece; si no, añadir `flush()` tras
cada escritura (o abrir con `buffering=1`).

Pendiente en esta máquina: los cuatro cuadrantes de 30W ya están barridos
(`--no-agc --step 4 --srs 45000`) y archivados con fecha y satélite en
`~/blindscan_hispasat-30w_2026-11-15.*` (+ copia en `results/` del repo):
135 locks → 30 grupos → 24 transpondedores. Quedan: portar el detector de
portadoras al driver (escalera de `0x31` + `tas2101_scan_point()`), rehacer
`dvbs-blindscan` sobre `peakscan` a 6 MHz y la pasada con `--srs 8000` para los
muxes de SR pequeño.
