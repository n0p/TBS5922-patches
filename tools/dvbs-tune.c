/*
 * dvbs-tune - minimal DVB-S/S2 tuning probe using the DVBv5 API.
 *
 * Usage:
 *   dvbs-tune [-a N] -f FREQ_KHZ -s SR_KSPS [-p V|H] [-b L|H] [-d S|S2]
 *             [-w TIMEOUT_MS]
 *
 * Prints:
 *   freq=<khz> sr=<ksps> lock=<0|1> status=0x.. str=<rel> cnr=<raw> took=<ms> rc=<errno>
 * Exit: 0 = lock, 1 = no lock, >1 = error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <linux/dvb/frontend.h>

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void add_prop(struct dtv_property *p, int *n, uint32_t cmd, uint32_t val)
{
	p[*n].cmd = cmd;
	p[*n].u.data = val;
	(*n)++;
}

int main(int argc, char **argv)
{
	int adapter = 0, opt;
	long freq_khz = 0, sr_ksps = 0, timeout_ms = 400;
	int pol_v = 1, band_high = 0, dvbs2 = 1;
	char path[128];

	while ((opt = getopt(argc, argv, "a:f:s:p:b:d:w:")) != -1) {
		switch (opt) {
		case 'a': adapter = atoi(optarg); break;
		case 'f': freq_khz = atol(optarg); break;
		case 's': sr_ksps = atol(optarg); break;
		case 'p': pol_v = (optarg[0] == 'V' || optarg[0] == 'v'); break;
		case 'b': band_high = (optarg[0] == 'H' || optarg[0] == 'h'); break;
		case 'd': dvbs2 = (optarg[0] != '1'); break;
		case 'w': timeout_ms = atol(optarg); break;
		default:
			fprintf(stderr, "bad option\n");
			return 2;
		}
	}
	if (!freq_khz || !sr_ksps) {
		fprintf(stderr, "need -f FREQ_KHZ -s SR_KSPS\n");
		return 2;
	}

	snprintf(path, sizeof(path), "/dev/dvb/adapter%d/frontend0", adapter);
	int fd = open(path, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", path, strerror(errno));
		return 3;
	}

	struct dtv_property p[12];
	struct dtv_properties cmd;
	int n = 0;

	add_prop(p, &n, DTV_CLEAR, 0);
	add_prop(p, &n, DTV_DELIVERY_SYSTEM, dvbs2 ? SYS_DVBS2 : SYS_DVBS);
	add_prop(p, &n, DTV_FREQUENCY, (uint32_t)freq_khz);
	add_prop(p, &n, DTV_SYMBOL_RATE, (uint32_t)(sr_ksps * 1000));
	add_prop(p, &n, DTV_INVERSION, INVERSION_AUTO);
	add_prop(p, &n, DTV_INNER_FEC, FEC_AUTO);
	if (dvbs2)
		add_prop(p, &n, DTV_MODULATION, PSK_8);
	add_prop(p, &n, DTV_VOLTAGE, pol_v ? SEC_VOLTAGE_13 : SEC_VOLTAGE_18);
	add_prop(p, &n, DTV_TONE, band_high ? SEC_TONE_ON : SEC_TONE_OFF);
	add_prop(p, &n, DTV_TUNE, 0);
	cmd.num = n;
	cmd.props = p;

	long t0 = now_ms();
	int rc = ioctl(fd, FE_SET_PROPERTY, &cmd);
	int set_errno = rc < 0 ? errno : 0;
	long set_ms = now_ms() - t0;

	int locked = 0, max_str = -1, best_cnr = -1, last_st = 0;

	do {
		fe_status_t st = 0;
		if (ioctl(fd, FE_READ_STATUS, &st) == 0) {
			last_st = (int)st;
			if (st & FE_HAS_LOCK)
				locked = 1;
		}
		struct dtv_property q[2];
		struct dtv_properties qc;
		memset(q, 0, sizeof(q));
		q[0].cmd = DTV_STAT_SIGNAL_STRENGTH;
		q[1].cmd = DTV_STAT_CNR;
		qc.num = 2;
		qc.props = q;
		if (ioctl(fd, FE_GET_PROPERTY, &qc) >= 0) {
			if (q[0].u.st.len >= 1) {
				int v = (int)q[0].u.st.stat[0].uvalue;
				if (v > max_str)
					max_str = v;
			}
			if (q[1].u.st.len >= 1 && q[1].u.st.stat[0].scale == FE_SCALE_DECIBEL) {
				int v = (int)q[1].u.st.stat[0].svalue;
				if (v > best_cnr)
					best_cnr = v;
			}
		}
		if (locked)
			break;
		if (now_ms() - t0 >= timeout_ms)
			break;
		long rem = timeout_ms - (now_ms() - t0);
		usleep((rem < 40 ? rem : 40) * 1000);
	} while (1);

	/* ask the demod what it actually measured (get_frontend) */
	long msr = -1;
	int ds = -1, mod = -1;
	if (locked) {
		struct dvb_frontend_parameters legp;
		memset(&legp, 0, sizeof(legp));
		if (ioctl(fd, FE_GET_FRONTEND, &legp) == 0) {
			struct dtv_property q2[3];
			struct dtv_properties qc2;
			memset(q2, 0, sizeof(q2));
			q2[0].cmd = DTV_SYMBOL_RATE;
			q2[1].cmd = DTV_DELIVERY_SYSTEM;
			q2[2].cmd = DTV_MODULATION;
			qc2.num = 3;
			qc2.props = q2;
			if (ioctl(fd, FE_GET_PROPERTY, &qc2) >= 0) {
				msr = (long)(q2[0].u.data / 1000);
				ds = (int)q2[1].u.data;
				mod = (int)q2[2].u.data;
			}
			else
				msr = (long)(legp.u.qpsk.symbol_rate / 1000);
		} else {
			msr = (long)(legp.u.qpsk.symbol_rate / 1000);
		}
	}

	printf("freq=%ld sr=%ld lock=%d status=0x%02x str=%d cnr=%d msr=%ld ds=%d mod=%d set=%ld took=%ld rc=%d errno=%d\n",
	       freq_khz, sr_ksps, locked, last_st & 0xff, max_str, best_cnr, msr,
	       ds, mod, set_ms, now_ms() - t0, rc, set_errno);
	close(fd);
	return locked ? 0 : 1;
}
