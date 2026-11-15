# Scripts de exploración — TBS 5922 / QBOX22

Todo esto se usó para depurar el driver y el barrido. No son herramientas de
producción (esas están en `/usr/local/bin` y en `../tools/`), pero sirven para
repetir las medidas o depurar otro hardware parecido.

Casi todos invocan `/tmp/dvbs-tune` (ruta histórica). Copiar antes:

```sh
sudo cp /usr/local/bin/dvbs-tune /tmp/dvbs-tune
```

| Fichero | Para qué sirve |
|---|---|
| `probe_table.py FILE` | Recorre un fichero de `dtv-scan-tables` (`/usr/share/dvb/dvb-s/...`) y sonda cada transpondedor con `dvbs-tune -w 500`. Calcula LO y tono según la frecuencia (9750/10600) y la polarización. |
| `subset_table.py SRC DST f1,f2,...` | Extrae de un fichero de canales sólo los transpondedores indicados (Hz). Se usó para quedarse con los que enganchaban y pasarlos a `dvbv5-scan`. |
| `agcsweep.py POL BAND START STOP STEP SR` | Barrido de AGC con sintonía completa. Imprime `str` por frecuencia y marca mínimos/máximos locales. Es la referencia fiable del nivel de señal: `python3 agcsweep.py H L 950000 2150000 20000 45000`. |
| `acqwin.py` | Mide la ventana de adquisición del demod: barre SR con frecuencia fija (12130 H) y luego frecuencia con SR fijo. Con esto se descubrió que un mux de 27,5 MS/s engancha pidiendo entre 20 y 45 MS/s. |
| `lowsr.py` | Comprueba transpondedores de SR pequeño con su SR real y con SR=45000, y lee los registros `GET_SRATE` (0x5c/0x5d). Documenta que los SR pequeños no enganchan con el truco del SR ancho. |
| `hypo.py` | Descartó hipótesis de cristal del sintonizador (24/16 MHz) y de polarización invertida probando frecuencias escaladas. |
| `satprobe.py` | Sondeo rápido de 30 transpondedores conocidos de Astra 19.2E, Hotbird 13E, Hispasat 30W, etc. Útil como smoke test cuando no se sabe a qué apunta la antena. |
| `tunerregs.sh [IF SR POL BAND]` | Abre la puerta I2C del demod (registro 0x06 bit 0x80) y vuelca registros del AV2012 (0x63) y del TAS2101 (0x68) tras sintonizar. Con esto se vio que el PLL del tuner bloquea (`0x0b` bit0) y que `TUNER_CTRL` vuelve a `0x96`. |
| `dvbs-blindscan-v1.py` | Primera versión del barrido (sólo fuerza bruta frecuencia × SR, sin fase de AGC). Se conserva por si falla la detección de portadoras; es más lenta (0,75–1,4 s por punto). |
| `peaks.py` | Enumera las portadoras que ve el demod en la ventana sintonizada usando el protocolo del driver de Windows (`0x59` cuenta, `0x58` índice, `0x5a–0x5f` offset/SR/estado). Requiere el frontend ya sintonizado con `dvbs-tune -s 45000`. Ver `../docs/RE-windows-blindscan.md`. |
| `peakscan.py -p V\|H -b L\|H --start MHZ --stop MHZ [--step 6]` | Barrido con ese detector: sintoniza a SR 45000 y lista las portadoras con su SR real y su offset. Equivale a la fase 1 del driver de Windows. Paso fiable medido: **6 MHz** (con 12 se pierden portadoras). ~1,7 s por punto. |

## Registros útiles del TAS2101 (i2c 0x68)

| Reg | Qué es |
|---|---|
| `0x04` | bit 0x08 se limpia cuando hay lock (el driver lo hace en `read_status`) |
| `0x06` | puerta I2C: bit `0x80` habilita el acceso al tuner |
| `0x10` | `LNB_CTRL`: bit `0x40` = 18 V (si no, 13 V), bit `0x01` = tono 22 kHz |
| `0x30` | bit 0x01 se pone al sintonizar (`set_frontend`) |
| `0x31` | `DEMOD_STATUS`; `0x75` = lock |
| `0x42/0x43` | `SIGSTR` (AGC, 12 bits: `((b1 & 0x0f) << 8) | b0`) |
| `0x59` | nº de portadoras que el demod ve en la ventana actual (`& 0x1f`, 0–31) — detector multi-portadora del TAS2101 |
| `0x58` | selección de portadora: `(v & 0xe0) | índice`; con ella se leen 0x5a–0x5f de esa portadora |
| `0x5a/0x5b` | offset de frecuencia de la portadora seleccionada, 15 bits con signo, kHz |
| `0x5e/0x5f` | modulación/FEC de esa portadora; `0x5f & 0xe0` la valida (`>=0x40` si SR>5000, `>=0x60` si no) |
| `0x4a/0x4b` | errores/BER; `0x4b & 0x0f > 9` descarta el candidato |
| `0x5c/0x5d` | `GET_SRATE` en ksps: SR **medida** cuando hay lock |
| `0x73/0x74` | `SET_SRATE` (lo que se pidió) |
| `0x75/0x76` | `FREQ_OS` (offset de frecuencia, el driver lo pone a 0) |
| `0x92/0x93` | `SNR` |
| `0xee/0xef` | `MODFEC` (modulación y FEC detectadas) |

I2C directo (necesita `sudo` o grupo `i2c`):

```sh
sudo i2ctransfer -y 9 w1@0x68 0x31 r1        # estado del demod
sudo i2ctransfer -y 9 w1@0x68 0x42 r2        # AGC crudo
sudo i2ctransfer -y 9 w1@0x68 0x5c r2        # SR medida
sudo i2ctransfer -y 9 w1@0x63 0x0b r1        # estado del tuner (bit0 = PLL lock), con la puerta abierta
```

## Cómo se llegó a cada conclusión

* **No sintonizaba**: faltaba `DTV_TUNE` en las propiedades DVBv5. Se detectó
  porque el registro `LNB_CTRL` sí cambiaba (voltaje y tono se aplican en el
  bucle de propiedades) pero el sintonizador nunca se programaba: el log
  `av201x: ENTER set_params` no aparecía hasta añadirlo.
* **No enganchaba el sintonizador**: `EXPORT_SYMBOL` en vez de
  `EXPORT_SYMBOL_GPL` en `av201x_attach` → `dvb_attach()` falla y el frontend
  se queda sin `tuner_ops`.
* **Locks intermitentes**: con `-w 500` se perdían; el lock real llega a los
  0,7–0,9 s. Con `-w 2000` es estable.
* **SR real**: los registros `GET_SRATE` devuelven la SR medida, que es lo que
  permite barrer a ciegas con un SR ancho y luego afinar.
* **Espectro rápido fallido**: leer sólo el AGC tras escribir el PLL del tuner
  (~50 ms/punto) no reproduce los nulos que sí aparecen tras una sintonía
  completa; el AGC del TAS2101 necesita la secuencia entera de `set_frontend`.
