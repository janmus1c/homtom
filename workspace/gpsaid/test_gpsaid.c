/* Host test for tools/devtools/gpsaid.c (clang on macOS): cc -O1 -o out/test_gpsaid test_gpsaid.c && out/test_gpsaid
 * Checks the pure parts against independent references: SiRF checksum (manual example and stock sirfreset routine),
 * NMEA checksum, fixed-point LLA->ECEF against double precision, GPS week/TOW, MID 128 layout and the stock MID 129. */
#define GPSAID_NO_MAIN
#include "../../tools/devtools/gpsaid.c"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails;

#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void ref_ecef(double lat, double lon, double h, double *x, double *y, double *z)
{
	double a = 6378137.0, f = 1 / 298.257223563, e2 = f * (2 - f), p = lat * M_PI / 180, l = lon * M_PI / 180;
	double n = a / sqrt(1 - e2 * sin(p) * sin(p));

	*x = (n + h) * cos(p) * cos(l);
	*y = (n + h) * cos(p) * sin(l);
	*z = (n * (1 - e2) + h) * sin(p);
}

/* Stock /bin/sirfreset checksum routine at 0xfc38: 16-bit running sum, then the low 15 bits. */
static u32 stock_checksum(const u8 *p, u32 n)
{
	unsigned short s = 0;
	u32 i;

	for (i = 0; i < n; i++)
		s = (unsigned short)(s + p[i]);
	return s & 0x7fff;
}

static u32 lcg = 12345;

static u32 rnd(void)
{
	lcg = lcg * 1103515245u + 12345u;
	return lcg >> 1;
}

int main(void)
{
	static const u8 ex128[25] = {
		0x80, 0xff, 0xd7, 0x00, 0xf9, 0xff, 0xbe, 0x52, 0x66, 0x00, 0x3a, 0xc5, 0x7a,
		0x00, 0x01, 0x24, 0xf8, 0x00, 0x83, 0xd6, 0x00, 0x03, 0x9c, 0x0c, 0x33
	};
	struct buf o = { { 0 }, 0 };
	u8 f[64], p[25];
	u32 i, w, t, n;
	double maxerr = 0;
	struct aid a;
	s32 v;

	CHECK(sirf_checksum(ex128, 25) == 0x0a91, "manual MID128 example checksum");
	for (i = 0; i < 2000; i++) {
		u8 b[400];
		u32 k, len = rnd() % 400;

		for (k = 0; k < len; k++)
			b[k] = (u8)rnd();
		CHECK(sirf_checksum(b, len) == stock_checksum(b, len), "checksum vs stock routine len %u", len);
	}

	n = sirf_frame(mid129_stock, 24, f);
	CHECK(n == 32 && f[0] == 0xa0 && f[1] == 0xa2 && f[2] == 0 && f[3] == 24 && f[30] == 0xb0 && f[31] == 0xb3, "frame");
	printf("MID129 stock frame:");
	for (i = 0; i < n; i++)
		printf(" %02x", f[i]);
	printf("\n");

	nmea_finish(&o, "PSRF100,0,9600,8,1,0");
	CHECK(!strcmp(o.b, "$PSRF100,0,9600,8,1,0*0C\r\n"), "PSRF100 manual example: %s", o.b);
	psrf100_sirf4800(&o);
	printf("switch sentence: %s", o.b);

	CHECK(parse_fixed("51.4778", 6, &v) == 0 && v == 51477800, "parse lat");
	CHECK(parse_fixed("-0.0000005", 6, &v) == 0 && v == -1, "parse rounding %d", v);
	CHECK(parse_fixed("-121.97232", 6, &v) == 0 && v == -121972320, "parse lon");
	CHECK(parse_fixed("180", 6, &v) == 0 && v == 180000000, "parse 180");
	CHECK(parse_fixed("1.2.3", 6, &v) != 0 && parse_fixed("", 6, &v) != 0 && parse_fixed("x", 0, &v) != 0, "parse junk");
	CHECK(parse_fixed("1790000000", 0, &v) == 0 && v == 1790000000, "parse time");

	for (i = 0; i < 20000; i++) {
		s32 lat = (s32)(rnd() % 180000001u) - 90000000, lon = (s32)(rnd() % 360000001u) - 180000000;
		s32 alt = (s32)(rnd() % 9501u) - 500, x, y, z;
		double rx, ry, rz, e;

		lla_to_ecef(lat, lon, alt, &x, &y, &z);
		ref_ecef(lat / 1e6, lon / 1e6, alt, &rx, &ry, &rz);
		e = fmax(fabs(x - rx), fmax(fabs(y - ry), fabs(z - rz)));
		if (e > maxerr)
			maxerr = e;
	}
	printf("ECEF max abs error over 20000 random points: %.2f m\n", maxerr);
	CHECK(maxerr < 3.0, "ECEF error %.2f", maxerr);

	CHECK(gps_time(315964800u, 0, 0, &w, &t) == 0 && w == 0 && t == 0, "GPS epoch");
	CHECK(gps_time(1700000000u, 18, 0, &w, &t) == 0 && w == 2288 && t == 252818, "2023 week %u tow %u", w, t);
	CHECK(gps_time(1700000000u, 18, -1024, &w, &t) == 0 && w == 1264, "week offset");

	a.lat_udeg = 37387511;
	a.lon_udeg = -121972320;
	a.alt_m = 0;
	lla_to_ecef(a.lat_udeg, a.lon_udeg, 0, &a.x, &a.y, &a.z);
	a.drift_hz = 75000;
	a.tow_s = 86400;
	a.week = 924;
	a.channels = 12;
	a.hot = 0;
	mid128(&a, p);
	CHECK(p[0] == 0x80 && p[23] == 12 && p[24] == 0x83, "mid128 header/tail");
	CHECK(!memcmp(p + 13, ex128 + 13, 10), "mid128 drift/tow/week/channels match manual example layout");
	printf("MID128 at SiRF example location: ecef %d %d %d (manual example -2686727 -4304282 3851642)\n", a.x, a.y, a.z);

	a.lat_udeg = 51477800;
	a.lon_udeg = -1;
	a.alt_m = 46;
	a.drift_hz = 0;
	a.tow_s = 137618;
	a.week = 2437;
	psrf104(&a, &o);
	printf("PSRF104 dummy: %s", o.b);
	CHECK(!strncmp(o.b, "$PSRF104,51.477800,-0.000001,46,0,137618,2437,12,3*", 51), "PSRF104 text %s", o.b);

	printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
	return fails != 0;
}
