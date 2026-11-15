# Cómo hace el blindscan el driver de Windows (TBS5922)

Ingeniería inversa de `tbs5922.sys` (x64) a partir de los paquetes oficiales de
TBS. Fecha: 2026-11-15. Herramientas: radare2 6.2.7 (git), `objdump` binario y
un parser PE propio para las tablas `KSPROPERTY_SET`.

## Paquetes analizados

| Fichero | Versión | Tamaño x64 | Fecha |
|---|---|---|---|
| `tbs5922-windows-driver_v2.0.3.2.zip` | 2.0.3.2 | 72 880 B | 2013-11-18 |
| `TBS5922_release_20131118_win10.zip` | 2.0.3.2 (win10) | 79 424 B | 2020-05-18 |

Contenido: `tbs5922_x64/tbs5922.sys` + `TBS5922VHID.sys` + `.inf`/`.cat` y
`dpinst64.exe`. Sin PDB ni símbolos; PE con `ImageBase = 0x10000`,
`.text 0x380`, `PAGE 0x9580`, `INIT 0xea80`. Todas las direcciones de este
documento son **RVA** (VA = RVA + 0x10000).

**Las dos versiones son el mismo código**: los bytes de `.text`, `PAGE` e `INIT`
(0x380–0xea50) son idénticos byte a byte. Solo cambian el recurso de versión y
un blob añadido al final. El blindscan no se ha tocado entre 2013 y 2020.

## Arquitectura

Minidriver BDA clásico: importa `ks.sys` (`KsCreateFilterFactory`,
`KsPropertyHandler` no; usa `BdaCreateFilterFactoryEx`), `BdaSup.SYS`
(`BdaInitFilter`, `BdaValidateNodeProperty`) y `USBD.SYS`. Tres módulos:
demodulador TAS2101, sintonizador AV2012 y el puente FX2, todos dentro del
mismo `.sys` (a diferencia del driver Linux, que son tres `.ko`).

Acceso a registros:

| Dirección | Función | Implementación |
|---|---|---|
| `0xa4cc(ctx, dl=reg)` | leer registro del demod (0x68) | USB req `0xd0` (1 byte) |
| `0xa47c(ctx, dl=reg, r8b=val)` | escribir registro del demod | USB req `0xd0` |
| `0xa430(ctx, dl=reg, r8=buf, r9b=len)` | escribir bloque I2C al tuner | USB req `0xc6` |
| `0xa524(ctx, dl=0/1)` | **puerta I2C**: 0x06 bit 0x80 | RMW sobre 0x06 |
| `0xa634(ctx, sr)` | SET_SRATE: 0x74=sr>>8, 0x73=sr&0xff, y 0x75/0x76=0 | |
| `0xa690(ctx, pol)` | LNB_CTRL 0x10 bit 0x40 (18 V / 13 V) | |
| `0xa6d8(ctx, band)` | tono 22 kHz: 0x11=0x13, 0x12|=4, 0x34&=~0x40, bits bajos de 0x10 | |
| `0xacf8(ctx)` | estado del demod desde **0x31** (escalera, ver abajo) | |
| `0xb138(ctx, rec)` | sintoniza freq/SR de un registro de 12 B | |

Dispositivo privado `\Device\TBS5922PrivDevice` con **un solo IOCTL**,
`0x22c020`, que devuelve 8 bytes. No es el blindscan: es una consulta de
capacidades. Todo el resto va por propiedades KS.

## Punto de entrada

El blindscan **no** es un IOCTL: es una propiedad KS cuyo *set handler* está en
`0xa054`. Copia 5 dwords del buffer de entrada al contexto del filtro y lanza un
hilo de sistema:

```
0xa054(ctx, in):
    ctx->f_d4  = in[0]
    ctx->f_d8  = in[1]      ; frecuencia inicial en kHz
    ctx->f_dc  = in[2]      ; frecuencia final en kHz
    ctx->f_e0  = in[3]
    ctx->f_e4  = in[4]      ; polarización
    if in[3] != 0:
        ctx->f_c4    = 0    ; cancelación
        ctx->n_tp    = 0    ; ctx+0xa60
        ctx->state   = 1    ; ctx+0xa64 = "corriendo"
        PsCreateSystemThread(&h, 0x1fffff, NULL, NULL, NULL,
                             thread=0xa0fc, ctx)
    else:
        ctx->f_c4 = 1       ; cancelar
```

Estado hacia el usuario: `ctx+0xa64` (1 = corriendo, 5 = terminado),
`ctx+0xa60` = nº de transpondedores, `ctx+0xa68` = array de 5 dwords por
transpondedor (stride 0x14), `ctx+0xc4` = flag de cancelación.

El handler `0xa054` solo aparece referenciado en la tabla de punteros de
`.rdata:0x6040` (pares get/set seguidos, 8 B por entrada). **No** está en
ninguna de las tablas `KSPROPERTY_SET` de 72 B por ítem que sí se recuperan del
binario (esas son los sets BDA estándar: `EF30F379…` BdaDigitalDemodulator,
`1347D106…` BdaAutodemodulate, `FD0A5AF3…` BdaFrequencyFilter, `0FE4E87A…`,
`992CF102…`, `71985F45/47/48…`, `C6EFE5EB…` con 13 propiedades, `FAA8F3E5…`
con 11, `B45B50FF…` BdaVendor, `BAE951BD…`, `C62FDC56…`, `F84E2AB0…`). El
emparejamiento (GUID, PropertyId) del set privado del scan queda pendiente; el
protocolo de datos, en cambio, está completo.

## Algoritmo: dos fases

### Fase 1 — barrido grueso con detector de portadoras (0xa0fc → 0xbf04 → 0xbc64)

```
thread 0xa0fc(ctx):
    f_ini_mhz = clamp(ctx->f_d8 / 1000, 950, 2150)    ; magic 0x10624dd3 = /1000
    f_fin_mhz = clamp(ctx->f_dc / 1000, 950, 2150)
    ; buffer local: 200 registros de 12 B, rellenos con (pol, band) en +8/+9
    n = scan(ctx, f_ini_mhz, f_fin_mhz, buf, 200)     ; 0xbf04
    ctx->state = 5
```

`0xbc64` es el bucle:

```
for (f = max(950, ini); f <= min(2150, fin); f += 12)     ; paso = 12 MHz
    n += punto(ctx, f, 45000, rec + n*12, tope=200)       ; 0xba3c
    if (ctx->f_c4) break                                  ; cancelación
```

Y `0xba3c` es un punto (todo el protocolo de registros del barrido):

```
punto(ctx, f_mhz, sr=45000, out, max):
    0x36 &= ~0x01
    0x56 &= ~0x01
    set_srate(45000)                  ; 0x73/0x74, y 0x75/0x76 = 0
    puerta_i2c(1)                     ; 0x06 |= 0x80
    tuner_set(f_mhz, 45000)           ; 0xc2b4, ver abajo
    puerta_i2c(0)                     ; 0x06 &= 0x7f
    0x30 |= 0x01                      ; trigger de sintonía
    for i in 0..15:
        st = estado()                 ; 0xacf8, escalera sobre 0x31
        if st >= 2: break             ; NO espera lock completo
        sleep(50)                     ; 0x95c0(0x32)
    n = 0x59 & 0x1f                   ; nº de portadoras detectadas (0..31)
    if n == 0 or n > max: return 0
    for i in 0..n-1:
        0x58 = (0x58 & 0xe0) | i      ; selecciona la portadora i
        off = (0x5b << 8) | 0x5a      ; 15 bits con signo
        if off > 0x7fff: off = ((0x5b-0x100) << 8) | 0x5a
        if ctx->f_fc == 0: off = -off
        sr  = (0x5d << 8) | 0x5c      ; SR medida, ksps
        mod = 0x5f & 0xe0
        ok  = (sr > 5000) ? (mod >= 0x40) : (mod >= 0x60)
        if ok:
            out[i] = { f_mhz*1000 + off, sr, pol, band, 0 }   ; 12 B
            aceptados++
    return aceptados
```

`tuner_set` (`0xc2b4`) reescribe el sintonizador entero en cada punto, no solo
el PLL: bloque de 12 B en el registro 0x0c, 12 B en 0x0d, 11 B en 0x19, 6 B en
0x24, 1 B en 0x0c, otra vez 12 B en 0x0c, y después el divisor del PLL calculado
con `magic 0x2f684bdb` sobre `(f + 13)`.

### Escalera de estado del demod (0x31) — `0xacf8`

| Valor | Condición | Significado |
|---|---|---|
| 6 | `0x31 & 0x75 == 0x75` | lock completo |
| 5 | `& 0x35 == 0x35` | sincronizado, sin FEC ok |
| 4 | `& 0x15 == 0x15` | |
| 3 | `& 0x05 == 0x05` | |
| 2 | `& 0x03 == 0x03` | portadora detectada (basta para fase 1) |
| 1 | `& 0x01 == 0x01` | |
| 0 | — | nada |

### Fase 2 — confirmación y calidad (0xbf04)

```
for each rec in buf[0..n-1]:
    if rec.freq < 900000 or rec.freq > 2200000: skip
    if rec.sr < 800 or rec.sr > 50000: skip
    0x36 |= 0x01
    lnb_power(rec.pol)  ; 0x10 bit 0x40
    if ctx->f_d0 == 0: tono(rec.band)
    tune(rec)  ; 0xb138
    score = 0
    for k in 0..29:
        st = estado()
        score += umbral(st)      ; refuerza con 0x12 y 0x16
        sleep(sr <= 5000 ? 100 : sr <= 10000 ? 75 : 50)
        if score cruza umbral: break
    if (0x4b & 0x0f) > 9: skip          ; errores/BER
    leer 0x5e/0x5f
    ctx->tp[ctx->n_tp++] = { freq, sr, flags, calidad, modulacion }   ; 5 dwords
```

## Registros nuevos (no usados por el driver Linux)

| Reg | Qué es |
|---|---|
| `0x58` | selección de portadora: bits 4:0 = índice, bits 7:5 se preservan |
| `0x59` | nº de portadoras detectadas en la ventana actual (`& 0x1f`) |
| `0x5a/0x5b` | offset de frecuencia de la portadora seleccionada, 15 bits con signo, kHz |
| `0x5c/0x5d` | SR medida de la portadora seleccionada (ksps) |
| `0x5e/0x5f` | estado de modulación/FEC; `0x5f & 0xe0` valida la portadora |
| `0x4a/0x4b` | errores/BER; `0x4b & 0x0f > 9` descarta |
| `0x06` | puerta I2C (bit 0x80), ya conocido |
| `0x05` | bit 0x08 se limpia al encender LNB (`0xa568`) |
| `0x11/0x12/0x34` | secuencia del tono 22 kHz |
| `0x36/0x56` | bits 0 de habilitación antes de sintonizar |

## Por qué nuestro barrido no encuentra lo que este sí

1. **El demod tiene un detector multi-portadora** (0x58/0x59) que devuelve, por
   cada portadora de la ventana, su offset de frecuencia y su SR medida. El
   driver Linux no lo toca: deduce la SR de `0x5c/0x5d` solo tras lock, y para
   eso hay que acertar la SR pedida.
2. **No hace falta lock** para fase 1: con la escalera de 0x31 basta `>= 2`.
   De ahí que el driver Linux tarde 0,75 s por punto (espera lock) y este no.
3. **La fase 1 va a SR fijo 45000 y pasos de 12 MHz**, con ventana de ±22 MHz:
   4 muestras por portadora. Nuestro barrido prueba SRs a ciegas y con pasos de
   4–8 MHz.
4. **El AGC no interviene**: el "espectro" del driver Linux (PLL + AGC) estaba
   condenado a no ver nulos; aquí la detección la hace el propio demod.

## Plan de porte al driver Linux

1. `tas2101_read_status()`: implementar la escalera de 0x31 (devolver nivel) en
   vez de comparar solo con 0x75.
2. Nueva función `tas2101_scan_point(fe, f_khz, sr)` que replique `0xba3c`
   (secuencia de registros + enumeración de 0x58..0x5f) y devuelva una lista de
   portadoras. No expone API DVB: se consume desde el espacio de usuario vía
   `FE_GET_PROPERTY` con una propiedad privada o, más simple, desde
   `dvbs-tune --peaks` con `i2ctransfer`.
3. `dvbs-blindscan`: sustituir la fase 1 de AGC por el barrido de 12 MHz a
   SR 45000 leyendo 0x59/0x58..; la fase 2 se queda como está (sintonía +
   confirmación), pero alimentada con SR y frecuencia reales.
4. Medir: el paso de 12 MHz del driver de Windows es demasiado grueso en este
   demod (el detector sólo ve la portadora a ±7 MHz del centro); con 6 MHz son
   201 puntos por cuadrante, sin esperar lock y con SR/frecuencia reales.

## Verificación en hardware (hecha, 2026-11-15 21:40)

`lab/peaks.py` y `lab/peakscan.py` replican el protocolo contra `/dev/i2c-9` y
funcionan. Medido en Hispasat 30W:

| Punto | `0x59` | offset (0x5a/0x5b) | SR (0x5c/0x5d) | `0x5f & 0xe0` |
|---|---|---|---|---|
| IF 1530 H (12130 H 27500) | 1 | **+520 kHz** | **27 500** | 0xe0 |
| IF 1550 V (11300 V 34280) | 1 | **+2840 kHz** | **34 280** | 0xa0 |
| IF 1000 H (vacía) | 0 | — | — | — |

Con SR pedida 45000 el demod devuelve la SR real y el offset de frecuencia de la
portadora: no hace falta probar SR ni afinar a mano.

Barrido `lab/peakscan.py -p V -b L 1450 1650` (mismo tramo, 4 muxes reales de
34280):

| Paso | Puntos | Portadoras detectadas | Muxes distintos |
|---|---|---|---|
| 12 MHz | 18 | 2 | 2 de 4 |
| 6 MHz | 36 | 12 | **4 de 4** (11222.5, 11262.5, 11302.5, 11343.0) |

Moraleja: **12 MHz es demasiado paso para este demod** (el detector sólo ve la
portadora dentro de ~±7 MHz del centro sintonizado); 6 MHz sí. A 6 MHz son ~1,7 s
por punto (201 puntos/cuadrante ≈ 6 min), parecido al barrido plano actual pero
con SR y frecuencia reales y sin duplicados.

Comparación con el barrido plano archivado (`~/blindscan_hispasat-30w_2026-11-15.conf`):
ese tramo tiene **12 entradas para 4 portadoras**; el detector las agrupa en 4.

