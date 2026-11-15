# Parches

Serie incremental sobre el **estado de traspaso** del paquete, que está en
`baseline/handoff-0.3.tar.gz` (34 ficheros, sin artefactos de compilación).

Los cinco parches son los cambios de la revisión del 2026-11-15 sobre ese
traspaso. Aplicados en orden sobre el baseline reproducen **byte a byte** los
ficheros de la raíz de este repositorio (verificado con `patch --dry-run` +
`diff`).

| Parche | Fichero | Qué arregla |
|---|---|---|
| `0001-tas2101-cnr-en-decimas-de-dB.patch` | `tas2101.c` | El CNR se publicaba 10× alto: la tabla `tas2101_snrtable[]` está en décimas de dB y `cnr.stat[0].uvalue` va en centésimas, así que toca `10 * val` y no `100 * val`. Con el bug, `dvb-fe-tool` daba 128 dB donde hay 12,8 dB. |
| `0002-tbs-qbox22-version-0.3.patch` | `tbs-qbox22.c` | `MODULE_VERSION` a 0.3 para que `modinfo` no siga diciendo 0.2 con el paquete DKMS en 0.3. |
| `0003-dvbs-tune-informa-ds-y-mod.patch` | `tools/dvbs-tune.c` | Añade `ds=` (DELIVERY_SYSTEM) y `mod=` (MODULATION) leídos de `get_frontend`, para poder escribir un `.conf` DVBv5 con la modulación real. |
| `0004-dvbs-blindscan-ds-mod-dedupe-y-estabilidad.patch` | `tools/dvbs-blindscan.py` | `.conf` con `DELIVERY_SYSTEM`/`MODULATION` reales, dedupe por frecuencia+SR, comprobación de estabilidad (repite la medida y descarta si la SR no se repite) y modo `--no-agc` (barrido plano) tras comprobar que el AGC del TAS2101 no es reproducible. |
| `0005-dvbs-spectrum-marcar-experimental.patch` | `tools/dvbs-spectrum.c` | Marca la herramienta como EXPERIMENTAL / NO FIABLE: tocar sólo el PLL y leer el AGC no reproduce los niveles reales. |

## Aplicar

```sh
mkdir /tmp/base && tar xzf patches/baseline/handoff-0.3.tar.gz -C /tmp/base
cd /tmp/base
for p in /ruta/TBS5922-patches/patches/000*.patch; do patch -p1 < "$p"; done
```

## Verificar

```sh
rm -rf /tmp/verify && mkdir /tmp/verify
tar xzf patches/baseline/handoff-0.3.tar.gz -C /tmp/verify
cd /tmp/verify
for p in /ruta/TBS5922-patches/patches/000*.patch; do
    patch -p1 --dry-run < "$p" && patch -p1 -s < "$p"
done
for f in tas2101.c tbs-qbox22.c tools/dvbs-tune.c tools/dvbs-blindscan.py tools/dvbs-spectrum.c; do
    diff -q "$f" "/ruta/TBS5922-patches/$f"
done
```

## Notas

- El `README.md` de la raíz de este repositorio no es el del baseline: se ha
  reescrito para cubrir el paquete completo y el trabajo de blindscan. El texto
  del traspaso (con los cambios de la revisión) está en `../docs/`.
- El firmware `dvb-usb-tbsqbox-id5922.fw` no se incluye: es del fabricante.
