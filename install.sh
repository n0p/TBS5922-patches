#!/bin/sh
# Instala/actualiza el driver tbs-qbox22 desde este árbol como paquete DKMS.
#
#   sudo ./install.sh
#
# Ojo: `dkms install --force` NO recoge fuentes nuevas cuando el módulo ya está
# instalado (el .ko sigue siendo el viejo). Por eso aquí se hace siempre
# remove --all + add + build + install.
set -eu

NAME=$(sed -n 's/^PACKAGE_NAME="\(.*\)"/\1/p' dkms.conf)
VER=$(sed -n 's/^PACKAGE_VERSION="\(.*\)"/\1/p' dkms.conf)
[ -n "$NAME" ] && [ -n "$VER" ] || { echo "dkms.conf sin PACKAGE_NAME/VERSION"; exit 1; }

[ "$(id -u)" -eq 0 ] || { echo "hay que ejecutarlo como root (sudo ./install.sh)"; exit 1; }

SRC=$(cd "$(dirname "$0")" && pwd)
DST="/usr/src/$NAME-$VER"

echo "== copiando $SRC -> $DST"
mkdir -p "$DST"
rsync -a --delete \
    --exclude '.git' --exclude '*.ko' --exclude '*.o' --exclude '.*.o.d' \
    --exclude '.module-common.o' --exclude '__pycache__' \
    "$SRC"/ "$DST"/

echo "== DKMS: remove/add/build/install ($NAME $VER)"
dkms remove  -m "$NAME" -v "$VER" --all >/dev/null 2>&1 || true
dkms add     -m "$NAME" -v "$VER"
dkms build   -m "$NAME" -v "$VER"
dkms install -m "$NAME" -v "$VER"

echo "== recargando"
modprobe -r dvb-usb-tbsqbox22 2>/dev/null || true
modprobe dvb-usb-tbsqbox22 || true

if [ ! -e /lib/firmware/dvb-usb-tbsqbox-id5922.fw ]; then
    echo
    echo "AVISO: falta /lib/firmware/dvb-usb-tbsqbox-id5922.fw (8 KiB, del"
    echo "fabricante). Sin ese firmware el dispositivo no arranca en caliente."
fi

echo
echo "== estado"
dkms status -m "$NAME" -v "$VER" || true
for m in tas2101 av201x dvb-usb-tbsqbox22; do
    printf '%-20s %s\n' "$m" "$(modinfo -F version "$m" 2>/dev/null || echo '(no cargado)')"
done
