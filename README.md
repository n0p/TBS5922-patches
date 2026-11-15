# TBS 5922 / QBOX22 — driver DKMS y blindscan

Paquete **DKMS** completo para la sonda USB **TBS 5922** (USB `734c:5922`, se
anuncia como "TBS 5922" / "TBS QBOX22": puente Cypress FX2 + demodulador **Tmax
TAS2100/TAS2101** + sintonizador **Airoha AV2012**), más las herramientas de
barrido y la ingeniería inversa del blindscan del driver de Windows.

El kernel de Debian no trae ninguno de los tres módulos: no están en mainline.
Este paquete los compila contra los headers del kernel en uso.

Probado en Debian 13 con kernel `6.12.113+deb13-amd64`, x86_64.

## Estructura

```
Makefile  dkms.conf  install.sh        empaquetado DKMS (modo dual: make a mano o DKMS)
tbs-qbox22.c/.h                        driver dvb-usb (módulo dvb-usb-tbsqbox22)
tas2101.c/.h/_priv.h                   demodulador (módulo tas2101)
av201x.c/.h/_priv.h                    sintonizador (módulo av201x)
dvb-usb.h  dvb-pll.h  include/media/   headers privados que los headers de Debian no traen
tools/                                 dvbs-tune, dvbs-blindscan, dvbs-spectrum
lab/                                   scripts de exploración + detector de portadoras
docs/RE-windows-blindscan.md           RE de tbs5922.sys (radare2): cómo barre Windows
docs/README-driver.md                  README del traspaso original
docs/SKILL-tbs-5922-dvb-s2.md          notas operativas (skill)
patches/                               5 parches sobre el baseline + baseline
results/hispasat-30w-2026-11-15/       barrido completo de 30W y pruebas del detector
CHANGELOG-handoff.md                   qué cambió en la revisión del 2026-11-15
```

## Instalación

```sh
sudo ./install.sh
```

Copia el árbol a `/usr/src/tbs-qbox22-0.3`, hace `dkms remove --all` + `add` +
`build` + `install` y recarga el módulo. **Importante**: `dkms install --force`
no recoge fuentes nuevas si el módulo ya está instalado (el `.ko` sigue siendo
el viejo, y `modinfo` sigue diciendo la versión anterior); por eso el script
siempre quita y vuelve a añadir.

A mano, el equivalente:

```sh
sudo rsync -a --exclude .git ./ /usr/src/tbs-qbox22-0.3/
sudo dkms remove  -m tbs-qbox22 -v 0.3 --all
sudo dkms add     -m tbs-qbox22 -v 0.3
sudo dkms build   -m tbs-qbox22 -v 0.3
sudo dkms install -m tbs-qbox22 -v 0.3
sudo modprobe dvb-usb-tbsqbox22
```

Sin DKMS (pruebas): `make` deja los tres `.ko` en la raíz; `make install`
instala en el árbol de módulos del kernel en uso.

Hace falta el firmware del fabricante en
`/lib/firmware/dvb-usb-tbsqbox-id5922.fw` (8 KiB, **no se redistribuye aquí**).
Sin él el dispositivo no arranca en caliente (*"will try to load a firmware"* y
se queda ahí). `dkms.conf` trae `AUTOINSTALL="yes"`, así que se recompila solo
en cada actualización de kernel.

## Comprobar

```sh
ls -l /dev/dvb/adapter0/                      # demux0 dvr0 frontend0 net0
dvbs-tune -f 1530000 -s 27500 -p H -b H -w 2000
# freq=1530000 sr=27500 lock=1 status=0x1f str=... cnr=1240 msr=27500 ds=6 mod=9
```

`-f` es la FI en kHz (frecuencia del satélite − LO del LNB: 9750 banda baja,
10600 banda alta), `-s` la SR en ksps. `msr` es la SR **medida** por el demod
(registros `0x5c/0x5d`), no la pedida. `cnr` va en centésimas de dB (1240 =
12,40 dB) y `ds`/`mod` son DELIVERY_SYSTEM y MODULATION detectados.

## Parches

`patches/` contiene los cinco cambios de la revisión del 2026-11-15 sobre el
baseline del traspaso (`patches/baseline/handoff-0.3.tar.gz`), con el detalle en
`patches/README.md`:

1. **CNR 10× alto** en `tas2101.c` — la tabla está en décimas de dB y
   `cnr.stat[0].uvalue` en centésimas: `10 * val`, no `100 * val`.
2. **`MODULE_VERSION` 0.3** en `tbs-qbox22.c`.
3. `dvbs-tune` informa `ds=` y `mod=`.
4. `dvbs-blindscan`: `.conf` con modulación real, dedupe, comprobación de
   estabilidad y modo `--no-agc`.
5. `dvbs-spectrum` marcado EXPERIMENTAL.

Verificado: aplicados en orden sobre el baseline reproducen byte a byte los
ficheros de este repositorio.

## Herramientas

* **`dvbs-tune`** — sonda de una frecuencia: fija 13/18 V, tono 22 kHz, SR e IF;
  informa lock, AGC, CNR, SR medida, DELIVERY_SYSTEM y MODULATION.
* **`dvbs-blindscan`** — barrido a ciegas en dos fases (mapa de AGC y caza de
  lock probando SR), con `--no-agc` para barrido plano. Escribe `*.csv` (traza)
  y `*.conf` (DVBv5, listo para `dvbv5-scan`).
* **`dvbs-spectrum`** — espectro rápido tocando sólo el PLL + AGC.
  **EXPERIMENTAL / no fiable**: el AGC del TAS2101 no es reproducible así.
* **`lab/peaks.py`** — enumera las portadoras que ve el demod en la ventana
  sintonizada (`0x59`/`0x58`/`0x5a–0x5f`, protocolo del driver de Windows).
* **`lab/peakscan.py`** — barrido completo con ese detector: SR real y offset
  por portadora, sin probar SR ni esperar lock.

## Blindscan: lo que hace el driver de Windows

`docs/RE-windows-blindscan.md` tiene el análisis completo de `tbs5922.sys`
(2.0.3.2 y la build de 2020: **el código es idéntico byte a byte**). Resumen:

* El barrido lo hace el **driver**, en un hilo de sistema lanzado desde una
  propiedad KS: recorta a 950–2150 MHz y barre en pasos fijos con **SR 45000**,
  sin esperar lock (sale cuando el estado de `0x31` llega a nivel 2).
* En cada punto lee **`0x59 & 0x1f` = número de portadoras detectadas** y, por
  cada índice escrito en `0x58`, obtiene de `0x5a/0x5b` el **offset de
  frecuencia** (15 bits con signo, kHz), de `0x5c/0x5d` la **SR medida** y de
  `0x5f` la validez de la portadora.
* O sea: el TAS2101 **sí tiene detector multi-portadora**. El driver Linux no lo
  usaba, y de ahí que hubiera que probar SR a ciegas.

Verificado en hardware (`lab/peaks.py`, Hispasat 30W):

| Punto | `0x59` | offset | SR real |
|---|---|---|---|
| IF 1530 H (12130 H 27500) | 1 | +520 kHz | **27 500** |
| IF 1550 V (11300 V 34280) | 1 | +2 840 kHz | **34 280** |
| IF 1000 H (vacía) | 0 | — | — |

Y el paso del driver de Windows (12 MHz) resulta **demasiado grueso** en este
demod: el detector sólo ve la portadora a ±7 MHz del centro. Barriendo
1450–1650 MHz en V/L (4 muxes de 34280): con 12 MHz salen 2, con 6 MHz salen
los 4 (11222,4 / 11262,4 / 11302,9 / 11343,0).

## Resultados

`results/hispasat-30w-2026-11-15/`: barrido plano completo de los cuatro
cuadrantes (`--no-agc --step 4 --srs 45000`, 301 puntos/cuadrante), con
`135` locks → `30` grupos → **24 transpondedores** en el `.conf` limpio. Incluye
las trazas del detector de portadoras a 12 y 6 MHz para el mismo tramo.

## Realidades de este hardware

* **Blindscan: el chip sí puede, el módulo Linux no lo expone.** El TAS2101 tiene
  un detector multi-portadora real —`0x59` número de portadoras, `0x58` índice,
  `0x5a/0x5b` offset de frecuencia, `0x5c/0x5d` SR medida, `0x5f` validez— que es
  justo lo que usa el driver de Windows y está verificado aquí con
  `lab/peaks.py` (SR 27500 y 34280 exactas, 0 en frecuencia vacía). Lo que falta
  no es hardware sino API: `tas2101` no publica `FE_CAN_BLINDSEARCH` ni la
  propiedad DVBv5 de blindscan, así que `blindscan(1)` de dvb-apps no sirve y hay
  que sintonizar y leer registros (`dvbs-blindscan`, `lab/peakscan.py`).
* **Cada sintonía cuesta ~0,75 s** (el FX2 atiende cada transferencia con ~35 ms
  de latencia y el driver escribe registro a registro). El lock real llega a los
  0,7–0,9 s, por eso `-w 2000` es lo fiable y con `-w 500` se pierde la mitad.
* **Tolerancia de SR muy ancha**: un mux de 27,5 MS/s engancha pidiendo entre 20
  y 45 MS/s. Eso permite barrer con un SR ancho y leer la SR real, pero los SR
  pequeños (<10 MS/s) necesitan valores cercanos.
* **El AGC no es reproducible**: `SIGSTR` depende del estado de adaptación del
  demod y los "nulos" de un barrido no salen en el siguiente. De ahí `--no-agc`.
* **`status` miente**: `tas2101_read_status()` pone siempre
  `FE_HAS_SIGNAL|FE_HAS_CARRIER`; sólo `FE_HAS_LOCK` (`0x31` == `0x75`) es real.
* **La API DVBv5 exige `DTV_TUNE`**: frecuencia/SR/voltaje sin `DTV_TUNE` no
  sintonizan nada (el voltaje y el tono sí se aplican, lo que despista).

## Procedencia

Código base: árbol `crazycat69/linux_media` (rama `latest`, base 6.5), adaptado
a 6.12 por el autor del traspaso, con los arreglos del `CHANGELOG-handoff.md`
(`EXPORT_SYMBOL_GPL(av201x_attach)`, defines del Makefile para
`IS_ENABLED(CONFIG_...)`, parámetro `tas2101.lock_retries`). La ingeniería
inversa de `tbs5922.sys` y las herramientas del detector de portadoras son de
esta revisión. Licencia del driver: GPL-2.0 (módulo de kernel).

## Pendientes

* Llevar el detector de portadoras al driver (`tas2101_read_status()` con la
  escalera de `0x31`, y un `tas2101_scan_point()` que replique `0xba3c`) para
  que el barrido no dependa de `i2ctransfer` desde espacio de usuario.
* Rehacer `dvbs-blindscan` sobre `peakscan` (6 MHz, SR 45000) y comparar contra
  los 24 transpondedores de referencia.
* Repetir el barrido con `--srs 8000` para los muxes de SR pequeño.
* Mapear el `(GUID, PropertyId)` exacto de la propiedad privada que dispara el
  barrido en el driver de Windows (el handler está en RVA `0xa054`; su tabla no
  es una `KSPROPERTY_SET` estándar).
