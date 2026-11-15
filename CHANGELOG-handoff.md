# Cambios tras la revisión del agente de minibox (2026-11-15 20:5x)

1. **CNR 10x alto — corregido** en `tas2101.c`:
   `c->cnr.stat[0].uvalue = 10 * (s64) val;` (antes `100 *`).
   La tabla `tas2101_snrtable[]` está en décimas de dB y `uvalue` en centésimas.
   Verificado en vivo: 12130 H 27500 → `cnr=1290` (12,90 dB).
2. **`MODULE_VERSION` a 0.3** en `tbs-qbox22.c`.
   Aviso: `dkms install --force` NO recoge fuentes nuevas si el módulo ya está
   instalado; hay que `dkms remove -m tbs-qbox22 -v 0.3 --all` y volver a
   add/build/install.
3. **`dvbs-blindscan` reescrito**: el `.conf` usa `DELIVERY_SYSTEM` y
   `MODULATION` reales de `get_frontend` (`dvbs-tune` imprime `ds=` y `mod=`),
   dedupe por frecuencia+SR y comprobación de estabilidad (repite la medida y
   descarta si la SR medida no se repite).
4. **Artefactos** limpiados con `make clean`.
5. **Resultados del cuadrante VL** copiados a `lab/results/`.
6. **`tools/dvbs-spectrum.c`** marcado como EXPERIMENTAL / NO FIABLE en su
   cabecera.

## Hallazgo nuevo

El **AGC del TAS2101 no es reproducible**: repetidas medidas en 1800/1820/1830/
1840/1860 MHz con `-w` 40/400/1500 no reproducen los nulos profundos de la
primera pasada (quedan en 50–55k). La detección de portadoras por AGC no es
fiable; el barrido bueno es el **plano**: `dvbs-blindscan --no-agc`.

## En marcha

```
sudo /usr/local/bin/dvbs-blindscan --no-agc --step 4 --srs 45000 --out /tmp/scan_full
```

301 puntos por cuadrante × 4, ~1,3 s por punto → ~26 min. Para SR pequeños hay
que repetir con `--srs 8000` (los <10 MS/s no enganchan con el truco del SR
ancho). **No lanzar otro proceso que use `/dev/dvb/adapter0/frontend0` mientras
corra.**
