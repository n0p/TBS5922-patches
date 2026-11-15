/*
 * dvbs-spectrum - fast IF spectrum sweep for TBS 5922 / TAS2101 + AV2012.
 *
 * EXPERIMENTAL / NO FIABLE: el AGC del TAS2101 solo es valido tras la
 * secuencia completa de set_frontend; escribir solo el PLL del tuner y leer
 * SIGSTR da niveles planos que NO reproducen los nulos reales. Se conserva
 * como referencia de lo que NO funciona. Usar dvbs-blindscan.
 *
 * The TAS2101 has no hardware spectrum/scan engine exposed by the Linux
 * driver, but a full tune costs ~0.75 s (the FX2 bridge needs ~35 ms per
 * control transfer and the driver rewrites every register).  For a spectrum
 * we do not need a full retune: programming only the tuner PLL (one 5-byte
 * I2C write) and reading the demod AGC (one 1+2 byte I2C read) is enough.
 * That is ~100 ms per point instead of ~750 ms.
 *
 * The frontend is tuned once through the normal DVBv5 API (which also sets
 * LNB voltage/tone and the tuner bandwidth), then the sweep piggy-backs on
 * the dvb-usb I2C adapter through i2c-dev.
 *
 * Usage:
 *   dvbs-spectrum [-a N] [-p V|H] [-b L|H] [--start MHz] [--stop MHz]
 *                 [--step MHz] [--sr ksps] [--settle ms] [-o out.csv]
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
#include <sys/ioctl.h>
#include <linux/dvb/frontend.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>

#define XTAL_KHZ 27000

static const struct { unsigned raw, pct; } dbmtable[] = {
	{0xfff, 0x3333}, {0x778, 0x4CCC}, {0x621, 0x6666}, {0x55c, 0x7FFF},
	{0x40e, 0x9999}, {0x343, 0xB332}, {0x2b7, 0xCCCC}, {0x231, 0xE665},
	{0x1a1, 0xFFFF},
};

static int raw_to_pct(unsigned raw)
{
	int i, n = sizeof(dbmtable) / sizeof(dbmtable[0]);
	long val;

	for (i = 0; i < n; i++)
		if (dbmtable[i].raw < raw)
			break;
	if (i == 0)
		return dbmtable[0].pct * 100 / 0xFFFF;
	if (i >= n)
		i = n - 1;
	val = ((long)raw - dbmtable[i].raw) * dbmtable[i - 1].pct;
	val += ((long)dbmtable[i - 1].raw - raw) * dbmtable[i].pct;
	val /= (long)dbmtable[i - 1].raw - (long)dbmtable[i].raw;
	return (int)(val * 100 / 0xFFFF);
}

static void add_prop(struct dtv_property *p, int *n, uint32_t cmd, uint32_t val)
{
	p[*n].cmd = cmd;
	p[*n].u.data = val;
	(*n)++;
}

static int i2c_rd(int fd, int addr, uint8_t reg, uint8_t *buf, int len)
{
	struct i2c_msg msg[2];
	struct i2c_rdwr_ioctl_data d;

	msg[0].addr = addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &reg;
	msg[1].addr = addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = len;
	msg[1].buf = buf;
	d.msgs = msg;
	d.nmsgs = 2;
	return ioctl(fd, I2C_RDWR, &d);
}

static int i2c_wr(int fd, int addr, uint8_t *buf, int len)
{
	struct i2c_msg msg;
	struct i2c_rdwr_ioctl_data d;

	msg.addr = addr;
	msg.flags = 0;
	msg.len = len;
	msg.buf = buf;
	d.msgs = &msg;
	d.nmsgs = 1;
	return ioctl(fd, I2C_RDWR, &d);
}

static int pll_bytes(long freq_khz, uint8_t *b)
{
	uint32_t n;
	long n1 = (freq_khz + XTAL_KHZ / 2) / XTAL_KHZ;

	b[0] = 0x00;
	b[1] = n1 > 0xff ? 0xff : (uint8_t)n1;
	n = (uint32_t)(((freq_khz / 1000) << 17) / (XTAL_KHZ / 1000) +
		       (((freq_khz / 1000) << 17) % (XTAL_KHZ / 1000) ? 1 : 0));
	b[2] = (uint8_t)(n >> 9);
	b[3] = (uint8_t)(n >> 1);
	b[4] = (uint8_t)(((n & 1) << 7) | 0x50);
	return 0;
}

static int find_qbox_i2c(char *path, size_t sz)
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
	if (found < 0)
		return -1;
	snprintf(path, sz, "/dev/i2c-%d", found);
	return 0;
}

int main(int argc, char **argv)
{
	int adapter = 0, opt, pol_v = 1, band_high = 0;
	double start = 950.0, stop = 2150.0, step = 4.0;
	long sr = 45000, settle = 15;
	const char *out = "/tmp/spectrum.csv";
	char fe_path[128], i2c_path[64];

	static struct option lo[] = {
		{"start", 1, 0, 1}, {"stop", 1, 0, 2}, {"step", 1, 0, 3},
		{"sr", 1, 0, 4}, {"settle", 1, 0, 5}, {"out", 1, 0, 6},
		{0, 0, 0, 0}
	};
	while ((opt = getopt_long(argc, argv, "a:p:b:o:", lo, NULL)) != -1) {
		switch (opt) {
		case 'a': adapter = atoi(optarg); break;
		case 'p': pol_v = (optarg[0] == 'V' || optarg[0] == 'v'); break;
		case 'b': band_high = (optarg[0] == 'H' || optarg[0] == 'h'); break;
		case 'o': out = optarg; break;
		case 1: start = atof(optarg); break;
		case 2: stop = atof(optarg); break;
		case 3: step = atof(optarg); break;
		case 4: sr = atol(optarg); break;
		case 5: settle = atol(optarg); break;
		case 6: out = optarg; break;
		default: return 2;
		}
	}

	/* 1. full tune once: LNB voltage/tone, tuner bandwidth, i2c gate */
	snprintf(fe_path, sizeof(fe_path), "/dev/dvb/adapter%d/frontend0", adapter);
	int ffd = open(fe_path, O_RDWR | O_NONBLOCK);
	if (ffd < 0) {
		fprintf(stderr, "open %s: %s\n", fe_path, strerror(errno));
		return 3;
	}
	struct dtv_property p[10];
	struct dtv_properties cmd;
	int n = 0;
	add_prop(p, &n, DTV_CLEAR, 0);
	add_prop(p, &n, DTV_DELIVERY_SYSTEM, SYS_DVBS2);
	add_prop(p, &n, DTV_FREQUENCY, (uint32_t)(start * 1000));
	add_prop(p, &n, DTV_SYMBOL_RATE, (uint32_t)(sr * 1000));
	add_prop(p, &n, DTV_INVERSION, INVERSION_AUTO);
	add_prop(p, &n, DTV_INNER_FEC, FEC_AUTO);
	add_prop(p, &n, DTV_VOLTAGE, pol_v ? SEC_VOLTAGE_13 : SEC_VOLTAGE_18);
	add_prop(p, &n, DTV_TONE, band_high ? SEC_TONE_ON : SEC_TONE_OFF);
	add_prop(p, &n, DTV_TUNE, 0);
	cmd.num = n;
	cmd.props = p;
	ioctl(ffd, FE_SET_PROPERTY, &cmd);

	if (find_qbox_i2c(i2c_path, sizeof(i2c_path)) < 0) {
		fprintf(stderr, "cannot find the QBOX22 i2c adapter\n");
		return 4;
	}
	int ifd = open(i2c_path, O_RDWR);
	if (ifd < 0) {
		fprintf(stderr, "open %s: %s\n", i2c_path, strerror(errno));
		return 4;
	}

	/* 2. open the demod i2c gate towards the tuner */
	uint8_t g;
	int gate_added = 0;
	if (i2c_rd(ifd, 0x68, 0x06, &g, 1) == 1 && !(g & 0x80)) {
		uint8_t w[2] = {0x06, (uint8_t)(g | 0x80)};
		i2c_wr(ifd, 0x68, w, 2);
		gate_added = 1;
	}

	FILE *fh = fopen(out, "w");
	if (!fh) {
		fprintf(stderr, "cannot write %s\n", out);
		return 5;
	}
	fprintf(fh, "if_khz,raw,pct\n");

	unsigned raw_min = 0xffff, raw_max = 0;
	int npts = 0;
	for (double f = start; f <= stop + 1e-9; f += step) {
		long fk = (long)(f * 1000 + 0.5);
		uint8_t b[5], r[2] = {0, 0};
		pll_bytes(fk, b);
		if (i2c_wr(ifd, 0x63, b, 5) < 0)
			continue;
		usleep(settle * 1000);
		if (i2c_rd(ifd, 0x68, 0x42, r, 2) != 2)
			continue;
		unsigned raw = (((unsigned)r[1] & 0x0f) << 8) | r[0];
		if (raw < raw_min) raw_min = raw;
		if (raw > raw_max) raw_max = raw;
		fprintf(fh, "%ld,%u,%d\n", fk, raw, raw_to_pct(raw));
		npts++;
	}
	fclose(fh);

	if (gate_added) {
		uint8_t w[2] = {0x06, g};
		i2c_wr(ifd, 0x68, w, 2);
	}
	close(ifd);
	close(ffd);
	fprintf(stderr, "# %d points, raw %u..%u -> %s\n", npts, raw_min, raw_max, out);
	return 0;
}
