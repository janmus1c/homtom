/* Host test for statusd_core.h: sample sentences, JSON and UI output asserted exactly. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "statusd_core.h"

static int fails;
#define ADC0 "\"adc\":{\"cycles\":0,\"bad_conv\":0,\"rejected\":[0,0,0],\"stale\":[0,0,0],\"ack\":0,\"no_touch\":0,\"timeout\":0,\"busy\":0,\"wait_ms\":0,\"wait_max_ms\":0},"
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define STREQ(a, b) do { if (strcmp((a), (b))) { printf("FAIL %s:%d\n  got:  %s\n  want: %s\n", __FILE__, __LINE__, (a), (b)); fails++; } } while (0)

/* canonical public NMEA examples */
static const char GGA[] = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
static const char RMC[] = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";

static void mk(char *dst, const char *body)	/* body without '$' and '*HH' */
{
	int x = 0;
	const char *p;

	for (p = body; *p; p++)
		x ^= (unsigned char)*p;
	sprintf(dst, "$%s*%02X", body, x);
}

static void adc_fill(int s[3][SD_ADC_N], int b, int r, int l)
{
	int i;

	for (i = 0; i < SD_ADC_N; i++) {
		s[0][i] = b;
		s[1][i] = r;
		s[2][i] = l;
	}
}

static int feed(struct sd_state *st, const char *s, u32 up)
{
	return sd_nmea_feed(st, s, (int)strlen(s), up);
}

int main(void)
{
	struct sd_state st;
	struct sd_lb lb;
	char buf[1024], s[160];
	int n, i, lines;
	const char *chunks[] = {"$GPGG", "A,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r", "\n$GPRMC,12", "3519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n", "garbage\r\n"};

	/* checksum helper agrees with the canonical examples */
	sd_init(&st);
	mk(s, "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
	STREQ(s, GGA);

	/* nothing known yet: all null, empty strings */
	st.uptime_s = 100;
	n = sd_json(&st, buf, sizeof(buf));
	STREQ(buf, "{\"uptime_s\":100,\"battery_raw\":null,\"battery_ref_raw\":null,\"charging\":null,\"external_power\":null,\"power_key\":null,\"battery_mv_est\":null,\"battery_pct_est\":null,\"light_raw\":null,\"gps_age_s\":null,\"gga\":\"\",\"rmc\":\"\",\"cpu_load\":null,\"loadavg_x100\":null,\"mem_free_kb\":null,\"mem_total_kb\":null,\"bt_up\":null,\"bt_links\":null,\"last_touch\":null," ADC0 "\"extra\":{}}");
	CHECK(n == (int)strlen(buf));

	/* accepted sentences */
	CHECK(feed(&st, GGA, 90) == 1);
	CHECK(feed(&st, RMC, 90) == 2);
	CHECK(st.quality == 1 && st.sats == 8 && st.fix_valid && st.last_fix_up == 90);
	CHECK(st.speed_x100 == 2240);
	STREQ(st.gga, GGA);
	STREQ(st.rmc, RMC);

	/* rejected: bad checksum, truncated, wrong start, unknown type, oversize */
	CHECK(feed(&st, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*48", 91) == 0);
	CHECK(feed(&st, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*4", 91) == 0);
	CHECK(feed(&st, "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47", 91) == 0);
	mk(s, "GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00");
	CHECK(feed(&st, s, 91) == 0);
	CHECK(st.last_fix_up == 90);
	memset(s, 'A', 150);
	s[150] = 0;
	CHECK(feed(&st, s, 91) == 0);

	/* no-fix GGA (quality 0) updates the sentence but not the fix age; sats 00 */
	mk(s, "GPGGA,123520,,,,,0,00,,,M,,M,,");
	CHECK(feed(&st, s, 95) == 1);
	CHECK(st.quality == 0 && st.sats == 0 && st.last_fix_up == 90);
	CHECK(strcmp(st.gga, GGA) != 0);

	/* RMC void status: speed unknown */
	mk(s, "GPRMC,123520,V,,,,,,,230394,,");
	CHECK(feed(&st, s, 95) == 2);
	CHECK(st.speed_x100 == -1);
	CHECK(feed(&st, RMC, 96) == 2);		/* valid again */
	CHECK(st.speed_x100 == 2240);

	/* GN talker accepted */
	mk(s, "GNGGA,123521,4807.038,N,01131.000,E,2,10,0.8,545.4,M,46.9,M,,");
	CHECK(feed(&st, s, 100) == 1);
	CHECK(st.quality == 2 && st.sats == 10 && st.last_fix_up == 100);

	/* quotes or backslashes never reach the JSON */
	mk(s, "GPGGA,12\"3\\519,,,,,1,05,,,M,,M,,");
	CHECK(feed(&st, s, 100) == 1);
	CHECK(strchr(st.gga, '"') == 0 && strchr(st.gga, '\\') == 0);

	/* battery estimate and full JSON */
	sd_init(&st);
	CHECK(feed(&st, GGA, 90) == 1);
	CHECK(feed(&st, RMC, 91) == 2);
	st.uptime_s = 100;
	st.bat = 793; st.ref = 500; st.light = 412;
	st.charging = 1; st.ext_power = 0; st.key = 0;
	sd_json(&st, buf, sizeof(buf));
	STREQ(buf, "{\"uptime_s\":100,\"battery_raw\":793,\"battery_ref_raw\":500,\"charging\":1,\"external_power\":0,\"power_key\":0,\"battery_mv_est\":3885,\"battery_pct_est\":65,\"light_raw\":412,\"gps_age_s\":10,"
		   "\"gga\":\"$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\",\"rmc\":\"$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\",\"cpu_load\":null,\"loadavg_x100\":null,\"mem_free_kb\":null,\"mem_total_kb\":null,\"bt_up\":null,\"bt_links\":null,\"last_touch\":null," ADC0 "\"extra\":{}}");

	/* clamps, unknown ref, light unknown, age never negative */
	{
		u32 mv, pct;

		st.bat = 1000; st.ref = 500;
		CHECK(sd_bat_est(&st, &mv, &pct) && mv == 4900 && pct == 100);
		st.bat = 400;
		CHECK(sd_bat_est(&st, &mv, &pct) && mv == 1960 && pct == 0);
		st.ref = 0;
		CHECK(!sd_bat_est(&st, &mv, &pct));
		st.ref = 500; st.light = -1; st.uptime_s = 50;
		sd_json(&st, buf, sizeof(buf));
		CHECK(strstr(buf, "\"light_raw\":null,\"gps_age_s\":0,") != 0);
	}

	/* output never overruns a small buffer and stays NUL terminated */
	for (i = 1; i < 300; i += 7) {
		char small[300];

		memset(small, 'X', sizeof(small));
		n = sd_json(&st, small, i);
		CHECK(n < i && small[n] == 0);
		memset(small, 'X', sizeof(small));
		n = sd_ui(&st, small, i);
		CHECK(n < i && small[n] == 0);
	}

	/* UI file */
	st.light = 412; st.bat = 793; st.ref = 500; st.uptime_s = 100;
	sd_ui(&st, buf, sizeof(buf));
	STREQ(buf, "battery_pct_est=65\ncharging=1\nexternal_power=0\nlight_raw=412\nuptime_s=100\n"
		   "gga=$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\n"
		   "rmc=$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\n"
		   "gps_fix=1\ngps_sats=8\ngps_speed_kn=22.4\n");
	sd_init(&st);
	st.uptime_s = 7;
	sd_ui(&st, buf, sizeof(buf));
	STREQ(buf, "uptime_s=7\n");

	/* line assembly over arbitrary chunks, CRLF, garbage, overlong line, resync on '$' */
	sd_init(&st);
	memset(&lb, 0, sizeof(lb));
	lines = 0;
	for (i = 0; i < 5; i++) {
		const char *c = chunks[i];

		for (; *c; c++)
			if (sd_lb_push(&lb, *c)) {
				lines++;
				sd_nmea_feed(&st, lb.buf, lb.n, 42);
				lb.n = 0;
			}
	}
	CHECK(lines == 3);			/* GGA, RMC, "garbage" (rejected by the parser) */
	STREQ(st.gga, GGA);
	STREQ(st.rmc, RMC);
	memset(&lb, 0, sizeof(lb));
	lines = 0;
	for (i = 0; i < 300; i++)
		lines += sd_lb_push(&lb, 'x');
	lines += sd_lb_push(&lb, '\n');
	CHECK(lines == 0);			/* overlong line dropped */
	for (n = 0; GGA[n]; n++)
		lines += sd_lb_push(&lb, GGA[n]);
	lines += sd_lb_push(&lb, '\n');
	CHECK(lines == 1 && lb.n == (int)strlen(GGA));

	/* system fields */
	{
		static const char LA[] = "0.93 0.58 12.26 1/53 1541\n";
		static const char MI[] = "MemTotal:          58140 kB\nMemFree:           46796 kB\nMemAvailable:      45672 kB\nBuffers:              72 kB\nCach";
		static const char MI_OLD[] = "MemTotal:          58140 kB\nMemFree:           46796 kB\nBuffers:              72 kB\n";

		sd_init(&st);
		st.uptime_s = 5;
		sd_parse_loadavg(&st, LA, (int)strlen(LA));
		CHECK(st.load_x100[0] == 93 && st.load_x100[1] == 58 && st.load_x100[2] == 1226);
		sd_parse_loadavg(&st, "0.9", 3);
		CHECK(st.load_x100[0] == -1);
		sd_parse_loadavg(&st, LA, (int)strlen(LA));
		sd_parse_meminfo(&st, MI, (int)strlen(MI));
		CHECK(st.mem_total_kb == 58140 && st.mem_free_kb == 45672);
		sd_parse_meminfo(&st, MI_OLD, (int)strlen(MI_OLD));
		CHECK(st.mem_total_kb == 58140 && st.mem_free_kb == 46796);
		sd_parse_meminfo(&st, "x", 1);
		CHECK(st.mem_total_kb == -1 && st.mem_free_kb == -1);
		sd_parse_meminfo(&st, MI, (int)strlen(MI));
		st.bt_up = st.bt_links = 0;
		sd_bt_entry(&st, ".");
		sd_bt_entry(&st, "hci0");
		sd_bt_entry(&st, "hci0:11");
		sd_bt_entry(&st, "hci0:12");
		sd_bt_entry(&st, "hci0:");
		sd_bt_entry(&st, "hci0:1x");
		sd_bt_entry(&st, "hci1:3");
		sd_bt_entry(&st, "hci");
		CHECK(st.bt_up == 1 && st.bt_links == 2);
		sd_parse_touch(&st, "120 80 down\n", 12, 1000, 1003);
		CHECK(st.touch_type == SD_TOUCH_DOWN && st.touch_x == 120 && st.touch_y == 80 && st.touch_age == 3);
		sd_json(&st, buf, sizeof(buf));
		CHECK(strstr(buf, "\"rmc\":\"\",\"cpu_load\":93,\"loadavg_x100\":[93,58,1226],\"mem_free_kb\":45672,\"mem_total_kb\":58140,"
			       "\"bt_up\":1,\"bt_links\":2,\"last_touch\":{\"x\":120,\"y\":80,\"type\":\"down\",\"age_s\":3}," ADC0 "\"extra\":{}}") != 0);
		sd_parse_touch(&st, "5 7 up", 6, 1000, 900);
		CHECK(st.touch_type == SD_TOUCH_UP && st.touch_age == 0);
		sd_parse_touch(&st, "5 7 move\n", 9, 1000, 1000);
		CHECK(st.touch_type == SD_TOUCH_MOVE);
		sd_parse_touch(&st, "-1000 -1000 up\n", 15, 1000, 1000);
		CHECK(st.touch_type == 0 && st.touch_x == -1);
		sd_parse_touch(&st, "5 7 upx", 7, 1000, 1000);
		CHECK(st.touch_type == 0);
		sd_parse_touch(&st, "", 0, 1000, 1000);
		CHECK(st.touch_type == 0);
		sd_json(&st, buf, sizeof(buf));
		CHECK(strstr(buf, "\"last_touch\":null," ADC0 "\"extra\":{}}") != 0);
		for (i = 1; i < 500; i += 7) {
			char small[500];

			memset(small, 'X', sizeof(small));
			n = sd_json(&st, small, i);
			CHECK(n < i && small[n] == 0);
		}
	}

	/* /tmp/ui.extra merge */
	{
		static const char EX[] =
			"gps_aid=OK 12:00\n"
			"bt_visible_s=275\n"
			"Bad_Key=1\n"			/* upper case */
			"bad-key=1\n"			/* '-' */
			"=novalue\n"			/* empty key */
			"noequals\n"
			"charging=0\n"			/* statusd's own key */
			"gps_aid=again\n"		/* repeat: first wins */
			"quote=a\"b\n"
			"slash=a\\b\n"
			"tab=a\tb\n"
			"empty=\n"
			"crlf=1\r\n"
			"long=012345678901234567890123456789012345678901234567890123456789\n"	/* 65 characters: too long */
			"ok64=01234567890123456789012345678901234567890123456789012345678\n"	/* exactly 64 */
			"k32_0123456789012345678901234567=1\n"	/* 32-char key */
			"mic_level=-38\n"
			"partial=1";			/* no newline: writer not done */
		static char big[4096];
		int k;

		sd_init(&st);
		CHECK(strlen("long=012345678901234567890123456789012345678901234567890123456789") == 65);
		CHECK(strlen("ok64=01234567890123456789012345678901234567890123456789012345678") == 64);
		sd_parse_extra(&st, EX, (int)strlen(EX));
		STREQ(st.extra, "gps_aid=OK 12:00\nbt_visible_s=275\nempty=\ncrlf=1\n"
				"ok64=01234567890123456789012345678901234567890123456789012345678\nmic_level=-38\n");
		CHECK(st.nextra == 6);
		st.uptime_s = 7;
		sd_ui(&st, buf, sizeof(buf));
		STREQ(buf, "uptime_s=7\ngps_aid=OK 12:00\nbt_visible_s=275\nempty=\ncrlf=1\n"
			   "ok64=01234567890123456789012345678901234567890123456789012345678\nmic_level=-38\n");
		sd_json(&st, buf, sizeof(buf));
		CHECK(strstr(buf, "\"last_touch\":null," ADC0 "\"extra\":{\"gps_aid\":\"OK 12:00\",\"bt_visible_s\":\"275\",\"empty\":\"\","
				  "\"crlf\":\"1\",\"ok64\":\"01234567890123456789012345678901234567890123456789012345678\","
				  "\"mic_level\":\"-38\"}}") != 0);
		/* missing or empty file clears the previous set */
		sd_parse_extra(&st, "", 0);
		CHECK(st.extra[0] == 0 && st.nextra == 0);
		/* key cap: 17 valid lines, the first 16 are taken */
		strcpy(big, "a=1\nb=2\nc=3\nd=4\ne=5\nf=6\ng=7\nh=8\ni=9\nj=10\nk=11\nl=12\nm=13\nn=14\no=15\np=16\nq=17\n");
		sd_parse_extra(&st, big, (int)strlen(big));
		CHECK(st.nextra == 16 && strstr(st.extra, "p=16\n") != 0 && strstr(st.extra, "q=") == 0);
		/* byte cap: 15 rejected 64-byte lines (statusd's own key), then a valid line that ends exactly at byte 1024
		 * (taken) or one byte later (it straddles the cap: ignored), and a valid line after the cap (ignored) */
		for (k = 0; k < 2; k++) {
			big[0] = 0;
			if (k)
				strcat(big, "\n");
			for (i = 0; i < 15; i++)
				sprintf(big + strlen(big), "charging=%054d\n", i);
			sprintf(big + strlen(big), "edge=%058d\n", 7);
			strcat(big, "after=1\n");
			CHECK(strlen(big) == (size_t)(1024 + k + 8));
			sd_parse_extra(&st, big, (int)strlen(big));
			CHECK(st.nextra == (k ? 0 : 1) && (k || strncmp(st.extra, "edge=", 5) == 0));
		}
		/* a 4 KB file of valid lines: only whole lines in the first 1 KB, at most 16 */
		big[0] = 0;
		for (k = 0; k < 90; k++)
			sprintf(big + strlen(big), "k%02d=%040d\n", k, k);
		sd_parse_extra(&st, big, (int)strlen(big));
		CHECK(st.nextra == 16 && strlen(st.extra) == 16 * 45);
		/* full JSON and UI with a maximal extra set never overrun the daemon's buffers or a small one */
		sd_init(&st);
		big[0] = 0;
		for (k = 0; k < 16; k++)
			sprintf(big + strlen(big), "key%02d=%057d\n", k, k);
		sd_parse_extra(&st, big, (int)strlen(big));
		CHECK(st.nextra == 16);
		feed(&st, GGA, 1);
		feed(&st, RMC, 1);
		st.bat = 793; st.ref = 500; st.light = 412; st.charging = st.ext_power = st.key = 1;
		{
			static char jb[2048], ub[1536];

			n = sd_json(&st, jb, sizeof(jb));
			CHECK(n < (int)sizeof(jb) - 1 && jb[n - 1] == '}' && jb[n - 2] == '}');
			n = sd_ui(&st, ub, sizeof(ub));
			CHECK(n < (int)sizeof(ub) - 1 && ub[n - 1] == '\n');
			CHECK(n < 2047);	/* the GUI reads 2047 bytes of the UI file */
		}
		for (i = 1; i < 2100; i += 13) {
			memset(big, 'X', sizeof(big));
			n = sd_json(&st, big, i);
			CHECK(n < i && big[n] == 0);
			memset(big, 'X', sizeof(big));
			n = sd_ui(&st, big, i);
			CHECK(n < i && big[n] == 0);
		}
	}

	/* ADC filter: median of 5, plausibility windows, hold-last-good, confirmation, staleness, percentage hysteresis */
	{
		u32 mv, pct, seed = 12345;
		int s[3][SD_ADC_N], k, bad_b = 0, bad_l = 0, bad_p = 0;
		static const int m1[5] = { 656, -1, 0, 657, 655 }, m2[5] = { -1, -1, -1, 5, 6 }, m3[2] = { 7, 3 };

		CHECK(sd_median(m1, 5, 3) == 655 && sd_median(m2, 5, 3) == -1 && sd_median(m3, 2, 1) == 3);

		sd_init(&st);
		CHECK(st.pct_hold == -1);
		for (k = 0; k < 3; k++) {
			adc_fill(s, 656, 380, 4);
			sd_adc_cycle(&st, s);
		}
		CHECK(st.bat == 656 && st.ref == 380 && st.light == 4 && st.adc_cycles == 3);
		CHECK(sd_bat_est(&st, &mv, &pct) && mv == 4229 && pct == 100);

		/* spikes inside one burst: the median removes them, nothing is rejected */
		adc_fill(s, 656, 380, 4);
		s[0][1] = 0;
		s[0][3] = 0;
		s[1][2] = 1023;
		s[2][0] = 1000;
		s[2][4] = 1003;
		sd_adc_cycle(&st, s);
		CHECK(st.bat == 656 && st.ref == 380 && st.light == 4);
		CHECK(st.ch[0].rejected == 0 && st.ch[1].rejected == 0 && st.ch[2].rejected == 0);

		/* a whole burst corrupted (the user's 0 % and light ~1000 flashes): rejected, last good value held */
		adc_fill(s, 0, 1023, 1000);
		sd_adc_cycle(&st, s);
		CHECK(st.bat == 656 && st.ref == 380 && st.light == 4);
		CHECK(st.ch[0].rejected == 1 && st.ch[1].rejected == 1 && st.ch[2].rejected == 1 && st.ch[0].stale == 1);
		CHECK(sd_bat_est(&st, &mv, &pct) && pct == 100);
		st.uptime_s = 9;
		sd_json(&st, buf, sizeof(buf));
		CHECK(strstr(buf, "\"battery_raw\":656,\"battery_ref_raw\":380,") != 0);
		CHECK(strstr(buf, "\"battery_pct_est\":100,\"light_raw\":4,") != 0);
		CHECK(strstr(buf, "\"adc\":{\"cycles\":5,\"bad_conv\":0,\"rejected\":[1,1,1],\"stale\":[1,1,1],") != 0);
		sd_ui(&st, buf, sizeof(buf));
		CHECK(strstr(buf, "battery_pct_est=100\n") != 0 && strstr(buf, "light_raw=4\n") != 0);

		/* a burst with three lost conversions has no median: rejected */
		adc_fill(s, 656, 380, 4);
		s[0][0] = s[0][2] = s[0][4] = -1;
		sd_adc_cycle(&st, s);
		CHECK(st.bat == 656 && st.ch[0].rejected == 2 && st.ch[0].stale == 2 && st.ch[1].stale == 0);

		/* 2000 cycles of glitch injection: up to two spikes (0, 1000 or 1023) per burst on every channel, every
		 * seventh burst entirely 0 / 1023 / ~1000, small noise otherwise: the reported values never move */
		for (k = 0; k < 2000; k++) {
			int c, i;

			adc_fill(s, 656, 380, 4);
			for (c = 0; c < 3; c++)
				for (i = 0; i < SD_ADC_N; i++) {
					seed = seed * 1103515245u + 12345u;
					s[c][i] += (int)((seed >> 16) % 3) - 1;
				}
			for (c = 0; c < 3; c++)
				for (i = 0; i < 2; i++) {
					seed = seed * 1103515245u + 12345u;
					if ((seed >> 16) & 1)
						s[c][(seed >> 20) % SD_ADC_N] = (seed >> 24) % 3 == 0 ? 0 : (seed >> 24) % 3 == 1 ? 1000 : 1023;
				}
			if (k % 7 == 3)
				adc_fill(s, 0, 1023, 998);
			sd_adc_cycle(&st, s);
			bad_b += st.bat < 654 || st.bat > 658;
			bad_l += st.light < 2 || st.light > 6 || st.ref < 378 || st.ref > 382;
			bad_p += !sd_bat_est(&st, &mv, &pct) || pct != 100;
		}
		CHECK(bad_b == 0 && bad_l == 0 && bad_p == 0);
		CHECK(st.ch[0].stale < 2 && st.ch[2].stale < 2);

		/* real battery step (charger plugged in, +44 counts): held twice, accepted on the third agreeing cycle */
		sd_init(&st);
		adc_fill(s, 656, 380, 4);
		sd_adc_cycle(&st, s);
		adc_fill(s, 700, 380, 4);
		sd_adc_cycle(&st, s);
		CHECK(st.bat == 656);
		sd_adc_cycle(&st, s);
		CHECK(st.bat == 656);
		sd_adc_cycle(&st, s);
		CHECK(st.bat == 700 && st.ch[0].stale == 0 && st.ch[0].rejected == 2);
		adc_fill(s, 702, 380, 4);
		sd_adc_cycle(&st, s);
		CHECK(st.bat == 702);

		/* real light step needs two agreeing cycles; disagreeing outliers never re-anchor */
		adc_fill(s, 702, 380, 400);
		sd_adc_cycle(&st, s);
		CHECK(st.light == 4);
		sd_adc_cycle(&st, s);
		CHECK(st.light == 400);
		adc_fill(s, 702, 380, 4);
		sd_adc_cycle(&st, s);
		CHECK(st.light == 400);
		adc_fill(s, 702, 380, 1000);
		sd_adc_cycle(&st, s);
		CHECK(st.light == 400);
		adc_fill(s, 702, 380, 600);
		sd_adc_cycle(&st, s);
		CHECK(st.light == 400);

		/* bounded staleness: the reference unpowered (0) for 12 cycles turns unknown, the estimate null; a plausible
		 * value afterwards is taken at once */
		sd_init(&st);
		adc_fill(s, 656, 380, 4);
		sd_adc_cycle(&st, s);
		adc_fill(s, 656, 0, 4);
		for (k = 0; k < SD_STALE_MAX - 1; k++)
			sd_adc_cycle(&st, s);
		CHECK(st.ref == 380 && st.ch[1].stale == SD_STALE_MAX - 1 && sd_bat_est(&st, &mv, &pct));
		sd_adc_cycle(&st, s);
		CHECK(st.ref == -1 && !sd_bat_est(&st, &mv, &pct) && st.pct_hold == -1);
		sd_adc_cycle(&st, s);
		CHECK(st.ref == -1 && st.ch[1].stale == SD_STALE_MAX);
		sd_json(&st, buf, sizeof(buf));
		CHECK(strstr(buf, "\"battery_ref_raw\":null,") != 0 && strstr(buf, "\"battery_pct_est\":null,") != 0);
		adc_fill(s, 656, 500, 4);
		sd_adc_cycle(&st, s);
		CHECK(st.ref == 500 && st.ch[1].stale == 0);

		/* percentage hysteresis (ref 500: bat 793 = 65 %, 795 = 66 %, 797 = 67 %) */
		sd_init(&st);
		adc_fill(s, 793, 500, 4);
		sd_adc_cycle(&st, s);
		CHECK(sd_bat_est(&st, &mv, &pct) && pct == 65);
		adc_fill(s, 795, 500, 4);
		sd_adc_cycle(&st, s);
		CHECK(sd_bat_raw(&st, &mv, &pct) && pct == 66 && sd_bat_est(&st, &mv, &pct) && pct == 65);
		adc_fill(s, 797, 500, 4);
		sd_adc_cycle(&st, s);
		CHECK(sd_bat_est(&st, &mv, &pct) && pct == 67);
		adc_fill(s, 795, 500, 4);
		sd_adc_cycle(&st, s);
		CHECK(sd_bat_est(&st, &mv, &pct) && pct == 67);
		adc_fill(s, 793, 500, 4);
		sd_adc_cycle(&st, s);
		CHECK(sd_bat_est(&st, &mv, &pct) && pct == 65);
		sd_ui(&st, buf, sizeof(buf));
		CHECK(strncmp(buf, "battery_pct_est=65\n", 19) == 0);

		/* request/acknowledgement file age */
		CHECK(sd_age_ms(100, 500000000, 98, 0) == 2500 && sd_arb_stale(2500));
		CHECK(sd_age_ms(100, 0, 99, 900000000) == 100 && !sd_arb_stale(100));
		CHECK(sd_age_ms(100, 0, 98, 0) == 2000 && !sd_arb_stale(2000));
		CHECK(sd_age_ms(100, 0, 103, 0) == -3000 && sd_arb_stale(-3000));
		CHECK(sd_arb_stale(sd_age_ms(4000000000u, 0, 5, 0)) && sd_arb_stale(sd_age_ms(5, 0, 4000000000u, 0)));
	}

	/* decimal parser */
	CHECK(sd_dec100("13.4", 4) == 1340 && sd_dec100("0.05", 4) == 5 && sd_dec100("7", 1) == 700 && sd_dec100("1.2x", 4) == -1 && sd_dec100("", 0) == -1);

	if (fails) {
		printf("%d failure(s)\n", fails);
		return 1;
	}
	printf("test_core: all checks passed\n");
	return 0;
}
