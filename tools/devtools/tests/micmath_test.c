/* Host test of homtom-micmath.h against double-precision maths: cc -O1 -o /tmp/mm tests/micmath_test.c -lm && /tmp/mm */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
typedef unsigned int u32;
typedef unsigned short u16;
typedef short s16;
#include "../homtom-micmath.h"

static int bad, cases;
static double worst_l, worst_p;

static int ref_db(double v)
{
	double d = v <= 0 ? -90 : 20 * log10(v / 32768.0);
	d = d < -90 ? -90 : d > 0 ? 0 : d;
	return (int)floor(d + 0.5);
}

static void check(const char *name, const s16 *x, u32 n)
{
	double sum = 0, ss = 0, pk = 0;
	int lv, pkdb, rl, rp;
	u32 i;
	double el, ep;

	for (i = 0; i < n; i++)
		sum += x[i];
	sum /= n;
	for (i = 0; i < n; i++) {
		double d = x[i] - sum;
		ss += d * d;
		if (fabs(d) > pk)
			pk = fabs(d);
	}
	mm_window(x, n, &lv, &pkdb);
	rl = ref_db(sqrt(ss / n));
	rp = ref_db(pk);
	el = fabs(lv - (sqrt(ss / n) > 0 ? fmax(-90, fmin(0, 20 * log10(sqrt(ss / n) / 32768.0))) : -90));
	ep = fabs(pkdb - (pk > 0 ? fmax(-90, fmin(0, 20 * log10(pk / 32768.0))) : -90));
	if (el > 0.6 || ep > 0.6)
		printf("  %s: level %d (exact %.2f) peak %d (exact %.2f)\n", name, lv, 20 * log10(sqrt(ss / n) / 32768.0), pkdb, 20 * log10(pk / 32768.0));
	if (el > worst_l)
		worst_l = el;
	if (ep > worst_p)
		worst_p = ep;
	cases++;
	if (abs(lv - rl) > 1 || abs(pkdb - rp) > 1) {
		bad++;
		printf("FAIL %s: level %d (ref %d) peak %d (ref %d)\n", name, lv, rl, pkdb, rp);
	}
}

int main(void)
{
	static s16 x[16384];
	u32 n = 11719, i;
	int amp, dc;
	char nm[64];

	srand(1);
	for (amp = 1; amp <= 32767; amp = amp * 3 / 2 + 1)
		for (dc = -9000; dc <= 9000; dc += 4500) {
			for (i = 0; i < n; i++) {
				double v = dc + amp * sin(2 * M_PI * 440.0 * i / 46875.0);
				x[i] = (s16)fmax(-32768, fmin(32767, floor(v + 0.5)));
			}
			snprintf(nm, sizeof nm, "sine amp=%d dc=%d", amp, dc);
			check(nm, x, n);
			for (i = 0; i < n; i++) {
				double v = dc + amp * ((rand() / (double)RAND_MAX) * 2 - 1);
				x[i] = (s16)fmax(-32768, fmin(32767, floor(v + 0.5)));
			}
			snprintf(nm, sizeof nm, "noise amp=%d dc=%d", amp, dc);
			check(nm, x, n);
		}
	for (i = 0; i < n; i++)
		x[i] = 1234;
	check("pure dc", x, n);
	for (i = 0; i < n; i++)
		x[i] = i & 1 ? 32767 : -32768;
	check("full-scale square", x, n);
	for (i = 0; i < n; i++)
		x[i] = (s16)(i % 3 - 1);
	check("1 LSB", x, n);
	printf("%d cases, %d outside 1 dB, worst level error %.3f dB, worst peak error %.3f dB (before rounding to whole dB)\n", cases, bad, worst_l, worst_p);
	return bad != 0;
}
