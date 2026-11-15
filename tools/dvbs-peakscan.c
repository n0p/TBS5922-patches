/*
 * dvbs-peakscan - barrido de FI usando el detector de portadoras del TAS2101.
 *
 * Todo en un proceso y con ioctl directos (sin lanzar dvbs-tune ni
 * i2ctransfer por punto, que era el 60% del tiempo): abre el frontend una vez,
 * sintoniza actualizando solo DTV_FREQUENCY + DTV_TUNE, y en cada punto lee
 * 0x59 (cuantas portadoras ve) y, por cada indice escrito en 0x58, el bloque
 * 0x5a..0x5f (offset de frecuencia, SR medida y validez) del demodulador.
 *
 * Es la fase 1 del driver de Windows (ver docs/RE-windows-blindscan.md) pero
 * con paso de 6 MHz, que es lo que aguanta el detector (con 12 se pierden
 * portadoras).
 *
 * Uso:
 *   sudo dvbs-peakscan -p V -b L -s 1450 -e 1650 [-t 6] [-r 45000]
 *                      [-a 0] [-i BUS] [-n] [-o PREFIJO] [-q]
 *
 *   -p V|H   polarizacion (13/18 V)      -b L|H  banda (tono 22 kHz)
 *   -s/-e    FI inicial/final en MHz      -t      paso en MHz (6)
 *   -r       SR a pedir en el barrido (45000 ksps)
 *   -n       no confirmar cada portadora con una sintonizacion completa
 *   -o       prefijo de salida (.csv y .conf)
 *   -q       solo resultados
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <getopt.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/dvb/frontend.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define DEMOD_ADDR	0x68
#define MAX_PEAKS	32
#define MAX_RES		512

struct hit {
	int if_khz;
	int off_khz;
	int sr_ksps;
	int mod;
};

static long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* En fase 1 no esperamos lock: poner lock_retries=0 quita ~300 ms por punto
 * (el poll de 15x20 ms que hace tas2101_set_frontend). Se restaura al salir. */
#define RETRIES_PARAM "/sys/module/tas2101/parameters/lock_retries"

static int param_get(const char *path)
{
	FILE *f = fopen(path, "r");
	int v = -1;

	if (!f)
		return -1;
	if (fscanf(f, "%d", &v) != 1)
		v = -1;
	fclose(f);
	return v;
}

static int param_set(const char *path, int v)
{
	FILE *f = fopen(path, "w");

	if (!f)
		return -1;
	fprintf(f, "%d\n", v);
	fclose(f);
	return 0;
}

/* ---------- i2c ---------- */

static int find_qbox_i2c(void)
{
	DIR *d = opendir("/sys/class/i2c-dev");
	struct dirent *e;
	char fn[256], name[128];
	FILE *f;
	int found = -1;

	if (!d)
		return -1;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		snprintf(fn, sizeof(fn), "/sys/class/i2c-dev/%s/name", e->d_name);
		f = fopen(fn, "r");
		if (!f)
			continue;
		if (fgets(name, sizeof(name), f) && strstr(name, "QBOX22"))
			found = atoi(e->d_name + 4);
		fclose(f);
		if (found >= 0)
			break;
	}
	closedir(d);
	return found;
}

static int i2c_open(int bus)
{
	char path[64];

	snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
	return open(path, O_RDWR);
}

static int i2c_rd(int fd, uint8_t reg, uint8_t *buf, int len)
{
	struct i2c_msg m[2] = {
		{ .addr = DEMOD_ADDR, .flags = 0, .len = 1, .buf = &reg },
		{ .addr = DEMOD_ADDR, .flags = I2C_M_RD, .len = len, .buf = buf },
	};
	struct i2c_rdwr_ioctl_data d = { .msgs = m, .nmsgs = 2 };

	return ioctl(fd, I2C_RDWR, &d) < 0 ? -errno : 0;
}

static int i2c_wr(int fd, uint8_t reg, uint8_t val)
{
	uint8_t b[2] = { reg, val };
	struct i2c_msg m = { .addr = DEMOD_ADDR, .flags = 0, .len = 2, .buf = b };
	struct i2c_rdwr_ioctl_data d = { .msgs = &m, .nmsgs = 1 };

	return ioctl(fd, I2C_RDWR, &d) < 0 ? -errno : 0;
}

/* Escalera de estado del driver de Windows sobre 0x31. */
static int status_level(int fd)
{
	uint8_t v = 0;

	if (i2c_rd(fd, 0x31, &v, 1) < 0)
		return -1;
	if ((v & 0x75) == 0x75) return 6;
	if ((v & 0x35) == 0x35) return 5;
	if ((v & 0x15) == 0x15) return 4;
	if ((v & 0x05) == 0x05) return 3;
	if ((v & 0x03) == 0x03) return 2;
	if ((v & 0x01) == 0x01) return 1;
	return 0;
}

/* Lee las portadoras de la ventana actual. Devuelve cuantas. */
static int read_peaks(int fd, struct hit *out, int max, int if_khz)
{
	uint8_t n = 0, sel = 0;
	int i, got = 0;

	if (i2c_rd(fd, 0x59, &n, 1) < 0)
		return 0;
	n &= 0x1f;
	if (!n || i2c_rd(fd, 0x58, &sel, 1) < 0)
		return 0;

	for (i = 0; i < n && got < max; i++) {
		uint8_t b[6];
		int off, sr, mod;

		i2c_wr(fd, 0x58, (uint8_t)((sel & 0xe0) | i));
		if (i2c_rd(fd, 0x5a, b, sizeof(b)) < 0)
			continue;
		off = (b[1] << 8) | b[0];
		if (off > 0x7fff)
			off = ((b[1] - 0x100) << 8) | b[0];
		sr = (b[3] << 8) | b[2];
		mod = b[5] & 0xe0;
		if (mod < (sr > 5000 ? 0x40 : 0x60))
			continue;
		out[got].if_khz = if_khz;
		out[got].off_khz = off;
		out[got].sr_ksps = sr;
		out[got].mod = mod;
		got++;
	}
	i2c_wr(fd, 0x58, sel);
	return got;
}

/* ---------- frontend ---------- */

static void add_prop(struct dtv_property *p, int *n, uint32_t cmd, uint32_t val)
{
	p[*n].cmd = cmd;
	p[*n].u.data = val;
	(*n)++;
}

static int tune_fast(int fd, int if_khz)
{
	struct dtv_property p[2];
	struct dtv_properties c;
	int n = 0;

	memset(p, 0, sizeof(p));
	add_prop(p, &n, DTV_FREQUENCY, (uint32_t)if_khz);
	add_prop(p, &n, DTV_TUNE, 0);
	c.num = n;
	c.props = p;
	return ioctl(fd, FE_SET_PROPERTY, &c);
}

/* Sintonizacion completa (volcado de cache) + lectura de ds/mod reales. */
static int tune_full(int fd, int if_khz, int sr_ksps, int pol_v, int band_high,
		     int *ds, int *mod, int *sr_real)
{
	struct dtv_property p[12], q[4];
	struct dtv_properties c;
	int n = 0;

	memset(p, 0, sizeof(p));
	add_prop(p, &n, DTV_CLEAR, 0);
	add_prop(p, &n, DTV_DELIVERY_SYSTEM, SYS_DVBS2);
	add_prop(p, &n, DTV_FREQUENCY, (uint32_t)if_khz);
	add_prop(p, &n, DTV_SYMBOL_RATE, (uint32_t)(sr_ksps * 1000));
	add_prop(p, &n, DTV_INVERSION, INVERSION_AUTO);
	add_prop(p, &n, DTV_INNER_FEC, FEC_AUTO);
	add_prop(p, &n, DTV_MODULATION, PSK_8);
	add_prop(p, &n, DTV_VOLTAGE, pol_v ? SEC_VOLTAGE_13 : SEC_VOLTAGE_18);
	add_prop(p, &n, DTV_TONE, band_high ? SEC_TONE_ON : SEC_TONE_OFF);
	add_prop(p, &n, DTV_TUNE, 0);
	c.num = n;
	c.props = p;
	if (ioctl(fd, FE_SET_PROPERTY, &c) < 0 && errno != EINVAL)
		return -1;

	/* La confirmacion es el filtro de calidad: el detector de fase 1 da
	 * candidatos, pero solo vale lo que engancha de verdad. ds/mod salen de
	 * get_frontend y necesitan el demod enganchado. */
	int locked = 0;
	for (int w = 0; w < 75; w++) {
		fe_status_t st = 0;

		if (ioctl(fd, FE_READ_STATUS, &st) == 0 && (st & FE_HAS_LOCK)) {
			locked = 1;
			break;
		}
		usleep(20 * 1000);
	}
	if (!locked)
		return 1;

	memset(q, 0, sizeof(q));
	q[0].cmd = DTV_DELIVERY_SYSTEM;
	q[1].cmd = DTV_MODULATION;
	q[2].cmd = DTV_SYMBOL_RATE;
	q[3].cmd = DTV_STAT_CNR;
	c.num = 4;
	c.props = q;
	if (ioctl(fd, FE_GET_PROPERTY, &c) < 0)
		return -1;
	*ds = (int)q[0].u.data;
	*mod = (int)q[1].u.data;
	*sr_real = (int)(q[2].u.data / 1000);
	return 0;
}

static const char *ds_name(int ds)
{
	switch (ds) {
	case SYS_DVBS: return "DVBS";
	case SYS_DVBS2: return "DVBS2";
	default: return "DVBS2";
	}
}

static const char *mod_name(int mod)
{
	switch (mod) {
	case QPSK: return "QPSK";
	case PSK_8: return "PSK/8";
	case APSK_16: return "APSK/16";
	case APSK_32: return "APSK/32";
	default: return "AUTO";
	}
}

static int cmp_hit(const void *a, const void *b)
{
	const struct hit *x = a, *y = b;

	if (x->if_khz != y->if_khz)
		return x->if_khz - y->if_khz;
	return x->sr_ksps - y->sr_ksps;
}

int main(int argc, char **argv)
{
	int adapter = 0, bus = -1, pol_v = 1, band_high = 0, confirm = 1, quiet = 0;
	int step_mhz = 6, lo = 9750, opt, fd, ifd, i, nres = 0, npuntos = 0;
	int sr_req = 45000, settle_ms = 300, lr_old = -1, keep_retries = 0;
	long start_mhz = 950, stop_mhz = 2150;
	struct hit hits[MAX_RES];
	char path[128], cspath[160], confpath[160];
	FILE *csv = NULL, *conf = NULL;
	long t0;

	while ((opt = getopt(argc, argv, "a:p:b:s:e:t:r:i:o:w:nqkh")) != -1) {
		switch (opt) {
		case 'a': adapter = atoi(optarg); break;
		case 'p': pol_v = (optarg[0] == 'V' || optarg[0] == 'v'); break;
		case 'b': band_high = (optarg[0] == 'H' || optarg[0] == 'h'); break;
		case 's': start_mhz = atol(optarg); break;
		case 'e': stop_mhz = atol(optarg); break;
		case 't': step_mhz = atoi(optarg); break;
		case 'r': sr_req = atoi(optarg); break;
		case 'i': bus = atoi(optarg); break;
		case 'w': settle_ms = atoi(optarg); break;
		case 'o': snprintf(cspath, sizeof(cspath), "%s.csv", optarg);
			  snprintf(confpath, sizeof(confpath), "%s.conf", optarg);
			  break;
		case 'n': confirm = 0; break;
		case 'q': quiet = 1; break;
		case 'k': keep_retries = 1; break;
		default:
			fprintf(stderr, "uso: %s -p V|H -b L|H [-s MHz] [-e MHz] [-t MHz]"
				" [-r ksps] [-a N] [-i BUS] [-n] [-o PREFIJO]\n", argv[0]);
			return 2;
		}
	}
	lo = band_high ? 10600 : 9750;

	snprintf(path, sizeof(path), "/dev/dvb/adapter%d/frontend0", adapter);
	fd = open(path, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", path, strerror(errno));
		return 3;
	}
	if (bus < 0)
		bus = find_qbox_i2c();
	if (bus < 0) {
		fprintf(stderr, "no encuentro el i2c del QBOX22\n");
		return 3;
	}
	ifd = i2c_open(bus);
	if (ifd < 0) {
		fprintf(stderr, "open /dev/i2c-%d: %s\n", bus, strerror(errno));
		return 3;
	}
	if (cspath[0]) {
		csv = fopen(cspath, "w");
		conf = fopen(confpath, "w");
		if (!csv || !conf) {
			fprintf(stderr, "no puedo escribir %s\n", cspath);
			return 3;
		}
		fprintf(csv, "if_khz,off_khz,sr_ksps,mod,level\n");
	}

	/* props fijas del cuadrante: se envian una sola vez (sin DTV_CLEAR) */
	struct dtv_property base[9];
	struct dtv_properties bc;
	int bn = 0;

	lr_old = param_get(RETRIES_PARAM);
	/* Ojo: con lock_retries=0 el driver no llama a read_status y el demod
	 * NO se re-sintoniza (se queda pegado a la portadora anterior). Con 1
	 * basta: un sondeo de 20 ms, y ahorra ~280 ms por punto. */
	if (!keep_retries && lr_old > 1) {
		if (param_set(RETRIES_PARAM, 1) == 0 && !quiet)
			printf("# lock_retries %d -> 1 durante el barrido\n", lr_old);
	}

	memset(base, 0, sizeof(base));
	add_prop(base, &bn, DTV_DELIVERY_SYSTEM, SYS_DVBS2);
	add_prop(base, &bn, DTV_SYMBOL_RATE, (uint32_t)(sr_req * 1000));
	add_prop(base, &bn, DTV_INVERSION, INVERSION_AUTO);
	add_prop(base, &bn, DTV_INNER_FEC, FEC_AUTO);
	add_prop(base, &bn, DTV_MODULATION, PSK_8);
	add_prop(base, &bn, DTV_VOLTAGE, pol_v ? SEC_VOLTAGE_13 : SEC_VOLTAGE_18);
	add_prop(base, &bn, DTV_TONE, band_high ? SEC_TONE_ON : SEC_TONE_OFF);
	bc.num = bn;
	bc.props = base;
	ioctl(fd, FE_SET_PROPERTY, &bc);
	usleep(50 * 1000);

	if (!quiet)
		printf("# %s%s  FI %ld-%ld MHz paso %d, SR %d ksps, bus i2c-%d\n",
		       pol_v ? "V" : "H", band_high ? "/alta" : "/baja",
		       start_mhz, stop_mhz, step_mhz, sr_req, bus);

	t0 = now_ms();
	for (long f = start_mhz; f <= stop_mhz; f += step_mhz) {
		struct hit tmp[MAX_PEAKS];
		int n, lvl = 0, tries;

		/* 0x59 conserva el recuento del punto anterior: hay que limpiarlo
		 * antes de retunear o se leen portadoras fantasma con el offset
		 * viejo. Es escribible y se queda a 0 hasta que el detector
		 * publique algo nuevo. */
		i2c_wr(ifd, 0x59, 0);
		tune_fast(fd, (int)(f * 1000));
		/* El detector tarda ~300 ms en refrescar 0x59/0x5a-0x5f tras
		 * retunear: si se lee antes quedan los valores del punto anterior.
		 * Espera fija, sin break (0x59 puede ser distinto de cero con
		 * datos viejos). */
		usleep(settle_ms * 1000);
		lvl = status_level(ifd);
		n = read_peaks(ifd, tmp, MAX_PEAKS, (int)(f * 1000));
		npuntos++;
		for (i = 0; i < n && nres < MAX_RES; i++) {
			int sat = (int)(f * 1000) + lo * 1000 + tmp[i].off_khz;

			hits[nres++] = tmp[i];
			if (csv)
				fprintf(csv, "%d,%d,%d,0x%02x,%d\n", (int)(f * 1000),
					tmp[i].off_khz, tmp[i].sr_ksps, tmp[i].mod, lvl);
			if (!quiet)
				printf("%7ld MHz -> %8.2f MHz %s  SR=%6d  off=%+6d kHz"
				       "  0x5f&0xe0=0x%02x  nivel=%d\n", f,
				       sat / 1000.0, pol_v ? "V" : "H",
				       tmp[i].sr_ksps, tmp[i].off_khz, tmp[i].mod, lvl);
		}
	}
	long dt = now_ms() - t0;

	if (lr_old > 1 && !keep_retries)
		param_set(RETRIES_PARAM, lr_old);

	/* agrupa: misma SR y frecuencias a menos de 8 MHz = una portadora */
	qsort(hits, nres, sizeof(hits[0]), cmp_hit);
	struct hit uniq[MAX_RES];
	int nu = 0;

	for (i = 0; i < nres; i++) {
		if (nu && abs(hits[i].if_khz + hits[i].off_khz -
			      (uniq[nu - 1].if_khz + uniq[nu - 1].off_khz)) < 8000 &&
		    abs(hits[i].sr_ksps - uniq[nu - 1].sr_ksps) <= 500) {
			if (abs(hits[i].off_khz) < abs(uniq[nu - 1].off_khz))
				uniq[nu - 1] = hits[i];
			continue;
		}
		uniq[nu++] = hits[i];
	}

	printf("# %d puntos en %.1f s (%.0f ms/punto) -> %d detecciones, %d portadoras\n",
	       npuntos, dt / 1000.0, npuntos ? (double)dt / npuntos : 0.0, nres, nu);

	for (i = 0; i < nu; i++) {
		int if_khz = uniq[i].if_khz + uniq[i].off_khz;
		int sat_khz = if_khz + lo * 1000;
		int ds = -1, mod = -1, sr_real = -1;

		if (confirm) {
			int r = tune_full(fd, if_khz,
					  uniq[i].sr_ksps ? uniq[i].sr_ksps : sr_req,
					  pol_v, band_high, &ds, &mod, &sr_real);

			if (r > 0) {
				if (!quiet)
					printf("  %8.2f MHz %s  SR=%6d  descartada (sin lock)\n",
					       sat_khz / 1000.0, pol_v ? "V" : "H",
					       uniq[i].sr_ksps);
				continue;
			}
		}
		printf("  %8.2f MHz %s  SR=%6d  %s %s  (offset %+d kHz)\n",
		       sat_khz / 1000.0, pol_v ? "V" : "H",
		       sr_real > 0 ? sr_real : uniq[i].sr_ksps,
		       ds >= 0 ? ds_name(ds) : "DVBS2",
		       mod >= 0 ? mod_name(mod) : "AUTO", uniq[i].off_khz);
		if (conf) {
			fprintf(conf, "[%s_%d_%d]\n\tDELIVERY_SYSTEM = %s\n",
				pol_v ? "V" : "H", sat_khz / 1000,
				sr_real > 0 ? sr_real : uniq[i].sr_ksps,
				ds >= 0 ? ds_name(ds) : "DVBS2");
			fprintf(conf, "\tFREQUENCY = %d\n\tPOLARIZATION = %s\n",
				sat_khz, pol_v ? "VERTICAL" : "HORIZONTAL");
			fprintf(conf, "\tSYMBOL_RATE = %d\n\tMODULATION = %s\n",
				(sr_real > 0 ? sr_real : uniq[i].sr_ksps) * 1000,
				mod >= 0 ? mod_name(mod) : "AUTO");
			fprintf(conf, "\tINNER_FEC = AUTO\n\tINVERSION = AUTO\n\n");
		}
	}
	if (csv) fclose(csv);
	if (conf) {
		fclose(conf);
		printf("# escrito %s y %s\n", cspath, confpath);
	}
	close(ifd);
	close(fd);
	return 0;
}
