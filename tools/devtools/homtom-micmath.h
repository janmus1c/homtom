/* Level maths of homtom-snd2 --micmeter. Freestanding: no libc, no libgcc division (the only 64-bit operation is an
 * addition). Needs u32 and s16 from the includer. Host test: tools/devtools/tests/micmath_test.c.
 *
 * log2 of an integer in Q10 from the position of the top bit and a 16-segment linear interpolation of log2(1 + i/16);
 * the interpolation error is below 0.001 octave (0.003 dB), the whole chain stays within 0.1 dB. */
#define MM_FLOOR_DB (-90)

static u32 mm_div(u32 n, u32 d)
{
	u32 q = 0, bit = 1;

	if (!d)
		return 0;
	while (d < n && !(d & 0x80000000u)) {
		d <<= 1;
		bit <<= 1;
	}
	while (bit) {
		if (n >= d) {
			n -= d;
			q |= bit;
		}
		d >>= 1;
		bit >>= 1;
	}
	return q;
}

/* log2(v) * 1024 for v >= 1 */
static int mm_log2q(u32 v)
{
	static const u16 t[17] = { 0, 90, 174, 254, 330, 402, 470, 536, 599, 659, 717, 773, 827, 879, 929, 977, 1024 };
	int e = 31;
	u32 f, i, fr;

	while (!(v >> e))
		e--;
	f = e >= 4 ? (v >> (e - 4)) & 15 : (v << (4 - e)) & 15;
	/* 8 fractional bits below the top 4 */
	fr = e >= 12 ? (v >> (e - 12)) & 255 : (v << (12 - e)) & 255;
	i = f;
	return e * 1024 + t[i] + (int)(((u32)(t[i + 1] - t[i]) * fr) >> 8);
}

/* whole dB, rounded to nearest, floored at MM_FLOOR_DB, never above 0; centi is dB * 100 */
static int mm_dbround(int centi)
{
	u32 m;

	if (centi >= 0)
		return 0;
	m = mm_div((u32)(-centi) + 50, 100);
	return m > 90 ? MM_FLOOR_DB : -(int)m;
}

/* 20*log10(p / 32768) in centi-dB */
static int mm_amp_centi(u32 p)
{
	if (!p)
		return MM_FLOOR_DB * 100;
	return ((mm_log2q(p) - 15 * 1024) * 602) >> 10;
}

/* One window of n samples (n >= 1): subtract the window mean (the codec's DC offset), then
 * *level = RMS in dBFS (20*log10(rms/32768): a full-scale square wave is 0 dB, a full-scale sine -3 dB), *peak = largest |x - mean| in dBFS; whole dB, floor -90. */
static void mm_window(const s16 *x, u32 n, int *level, int *peak)
{
	int sum = 0, mean, neg;
	u32 i, pk = 0, d2, hi, lo, k, m;
	unsigned long long ss = 0;

	for (i = 0; i < n; i++)
		sum += x[i];
	neg = sum < 0;
	m = mm_div((u32)(neg ? -sum : sum) + n / 2, n);
	mean = neg ? -(int)m : (int)m;
	for (i = 0; i < n; i++) {
		int d = x[i] - mean;
		u32 a = (u32)(d < 0 ? -d : d);

		pk = a > pk ? a : pk;
		d2 = a * a;
		ss += d2;
	}
	hi = (u32)(ss >> 32);
	lo = (u32)ss;
	k = 0;
	while (hi) {
		lo = (lo >> 1) | (hi << 31);
		hi >>= 1;
		k++;
	}
	/* mean square = ss / n, taken as a difference of logarithms so that nothing is truncated; 2^30 = 32768^2 */
	if (!lo)
		*level = MM_FLOOR_DB;
	else
		*level = mm_dbround((((mm_log2q(lo) + (int)k * 1024 - mm_log2q(n)) - 30 * 1024) * 301) >> 10);
	*peak = mm_dbround(mm_amp_centi(pk));
}
