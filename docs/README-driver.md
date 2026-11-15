# TBS QBOX22 / TBS 5922 — driver DVB-S2 para Debian 13 (kernel 6.12)

Sonda satelital USB **TBS 5922** (USB `734c:5922`, se anuncia como "TBS 5922" /
"TBS QBOX22"): puente Cypress FX2 + demodulador **Tmax TAS2100/TAS2101** +
sintonizador **Airoha AV2012**.

## Por qué hay que compilar nada

El kernel de Debian no trae ni el driver USB (`dvb-usb-tbsqbox22`) ni el
demodulador (`tas2101`) ni el sintonizador (`av201x`): no están en mainline.
Este paquete DKMS los compila contra los headers del kernel en uso.

Origen del código: árbol `crazycat69/linux_media` (rama `latest`, base 6.5),
adaptado a 6.12:

* `tbs-qbox22.c/.h` — driver dvb-usb (módulo `dvb-usb-tbsqbox22`)
* `tas2101.c/.h/_priv.h` — demodulador (módulo `tas2101`)
* `av201x.c/.h/_priv.h` — sintonizador (módulo `av201x`)
* `dvb-usb.h`, `dvb-pll.h`, `include/media/dvb-usb-ids.h` — headers privados que
  el paquete de headers de Debian no instala.

Cambios aplicados sobre el original:

1. `EXPORT_SYMBOL_GPL(av201x_attach)` (con `EXPORT_SYMBOL` el `dvb_attach()`
   falla con *"failing symbol_get of non-GPLONLY symbol"* y no se engancha el
   sintonizador).
2. `-include linux/version.h` y `-DCONFIG_DVB_TAS2101` /
   `-DCONFIG_MEDIA_TUNER_AV201X` en el Makefile: los headers usan
   `IS_ENABLED(CONFIG_...)` y, fuera del árbol, sin esos defines se compilan los
   stubs vacíos.
3. Parámetro de módulo `tas2101.lock_retries` (por defecto 15) para acortar la
   espera de lock al escanear.

## Instalación / actualización

```sh
sudo dkms add     -m tbs-qbox22 -v 0.3      # solo la primera vez
sudo dkms build   -m tbs-qbox22 -v 0.3
sudo dkms install -m tbs-qbox22 -v 0.3 --force
sudo modprobe dvb-usb-tbsqbox22
```

Firmware: `/lib/firmware/dvb-usb-tbsqbox-id5922.fw` (8 KiB, firmware FX2 del
propio fabricante). Sin él el dispositivo no arranca en caliente
(*"will try to load a firmware"* y se queda ahí).

Autocarga en arranque: `/etc/modules-load.d/tbs-qbox22.conf`.
DKMS lo recompila solo en cada actualización de kernel (AUTOINSTALL=yes).

## Comprobación

```sh
ls -l /dev/dvb/adapter0/            # demux0 dvr0 frontend0 net0
dvb-fe-tool -a 0 -g                 # info del frontend
dvbs-tune -f 1530000 -s 27500 -p H -b H -w 2000   # IF kHz, SR ksps
```

`dvbs-tune` imprime `lock=1 status=0x1f ... msr=<SR medida>`.
`msr` es la velocidad de símbolo **medida por el demodulador** (registros
`GET_SRATE`), no la solicitada: sirve para barrer a ciegas.

## Herramientas

* `dvbs-tune` — sonda de una frecuencia: fija voltaje (13/18 V), tono 22 kHz,
  SR y IF; informa lock, AGC, CNR y SR medida.
* `dvbs-blindscan` — barrido a ciegas en dos fases:
  1. mapa de AGC en toda la FI (950–2150 MHz) por cuadrante (V/H × banda
     baja/alta) con el ancho de banda máximo;
  2. caza de lock en cada portadora detectada probando varios SR; al enganchar
     lee la SR real y afina la frecuencia.

  Genera `*.csv` (traza completa) y `*.conf` (formato DVBv5, listo para
  `dvbv5-scan`/`dvbv5-zap`).

## Realidades de este hardware

* **No hay blindscan de hardware**: el TAS2101 no implementa la API DVBv5 de
  blindscan (`FE_CAN_BLINDSEARCH` no está), así que `blindscan(1)` de dvb-apps
  no sirve. El barrido de `dvbs-blindscan` es a base de sintonizar y leer el
  estado del demodulador.
* **Cada sintonía cuesta ~0,75 s**: el puente FX2 atiende cada transferencia de
  control con ~35 ms de latencia y el driver escribe registro a registro.
  Un lock se declara a los ~0,7–0,9 s de sintonizar; por eso `-w 2000` es lo
  fiable (con `-w 500` se pierden la mitad de los transpondedores).
* **Tolerancia de SR muy ancha**: con un transpondedor de 27,5 MS/s el demod
  engancha pidiendo desde 20 hasta 45 MS/s, y devuelve la SR real en
  `GET_SRATE`. Eso permite barrer con un solo SR "ancho" y leer la SR de cada
  portadora. Para SR pequeños (<10 MS/s) hay que probar SR cercanos.
* **AGC útil**: `strength` viene del registro `SIGSTR` del demodulador
  (porcentaje calibrado por tabla). Las portadoras se ven como mínimos/máximos
  claros sobre el ruido; es lo que usa la fase 1 del barrido.
* **El AGC no es reproducible**: `SIGSTR` depende del estado de adaptación del
  demod; los nulos de un barrido no salen en el siguiente. El barrido fiable es
  el plano (`dvbs-blindscan --no-agc`), no la detección por AGC.
* **CNR**: `tas2101.c` publicaba el CNR 10x alto (la tabla está en décimas de
  dB y se multiplicaba por 100 en vez de por 10). Corregido: 1290 = 12,90 dB.
* **`status` miente**: `tas2101_read_status()` pone siempre
  `FE_HAS_SIGNAL|FE_HAS_CARRIER`; sólo `FE_HAS_LOCK` (registro `DEMOD_STATUS`
  == 0x75) es real.
* **La API DVBv5 exige `DTV_TUNE`**: mandar frecuencia/SR/voltaje sin
  `DTV_TUNE` no sintoniza nada (el voltaje y el tono sí se aplican, lo que
  despista). Fue el primer fallo de la herramienta de pruebas.

## Ficheros

```
/usr/src/tbs-qbox22-0.3/     fuentes DKMS + README + tools/
/usr/local/bin/dvbs-tune
/usr/local/bin/dvbs-blindscan
/lib/firmware/dvb-usb-tbsqbox-id5922.fw
/etc/modules-load.d/tbs-qbox22.conf
```
