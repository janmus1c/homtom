#!/bin/busybox sh
# Read-only register snapshot for the GO 630 (S3C2443). Needs /bin/devmem; performs reads only.
# Decodes the input pins documented in docs/PERIPHERALS.md. Output is plain text for an artifact.
rd() { devmem "$1" | cut -d" " -f2; }
hx() { echo $(( 0x$1 )); }
bit() { echo $(( ($1 >> $2) & 1 )); }

echo "== GPIO CON/DAT (B C D E F G H J L)"
for p in B:10:14 C:20:24 D:30:34 E:40:44 F:50:54 G:60:64 H:70:74 J:D0:D4 L:F0:F4; do
	n=${p%%:*}; r=${p#*:}; c=${r%%:*}; d=${r##*:}
	echo "GP${n}CON=$(rd 0x560000$c) GP${n}DAT=$(rd 0x560000$d)"
done

echo "== clocks and power"
echo "HCLKCON=$(rd 0x4C000030) PCLKCON=$(rd 0x4C000034) SCLKCON=$(rd 0x4C000038) CLKDIV0=$(rd 0x4C000024)"
echo "PWRCFG=$(rd 0x4C000060) PHYPWR=$(rd 0x4C000084) UCLKCON=$(rd 0x4C00008C)"
echo "INFORM0=$(rd 0x4C000070) INFORM1=$(rd 0x4C000074) INFORM2=$(rd 0x4C000078) INFORM3=$(rd 0x4C00007C)"

echo "== PWM timer 0 (backlight)"
echo "TCFG0=$(rd 0x51000000) TCFG1=$(rd 0x51000004) TCON=$(rd 0x51000008) TCNTB0=$(rd 0x5100000C) TCMPB0=$(rd 0x51000010)"

echo "== RTC (BCD values meaningful only if RTCCON bit 0 = 1)"
echo "RTCCON=$(rd 0x57000040) TICNT=$(rd 0x57000044) RTCALM=$(rd 0x57000050) INTP=$(rd 0x57000030)"
echo "BCDSEC=$(rd 0x57000070) MIN=$(rd 0x57000074) HOUR=$(rd 0x57000078) DATE=$(rd 0x5700007C) MON=$(rd 0x57000084) YEAR=$(rd 0x57000088)"

echo "== decoded inputs (1 = pin high)"
gpc=$(hx $(rd 0x56000024)); gpf=$(hx $(rd 0x56000054)); gpg=$(hx $(rd 0x56000064)); gpj=$(hx $(rd 0x560000D4))
echo "LCD_ID GPC7=$(bit $gpc 7)          (1 Sharp LQ043T1, 0 Samsung LTE430WQ)"
echo "ON_OFF GPF0=$(bit $gpf 0)          (0 pressed)"
echo "CD_SD GPF1=$(bit $gpf 1)           (0 card present)"
echo "ACPWR GPF2=$(bit $gpf 2)           (0 external power)"
echo "LINEIN GPF3=$(bit $gpf 3)          (0 plugged)"
echo "BATT_TEMP_OVER GPF4=$(bit $gpf 4)  (input high = charging allowed)"
echo "LOW_DC_VCC GPF6=$(bit $gpf 6)      (0 asserted)"
echo "WP_SD GPF7=$(bit $gpf 7)           (1 protected, sense unverified)"
echo "GPS_1PPS GPG0=$(bit $gpg 0)"
echo "CHARGING GPG1=$(bit $gpg 1)        (0 charging)"
echo "HEADPHONE GPG3=$(bit $gpg 3)       (0 plugged)"
echo "USB_PWR_BYPASS GPG5=$(bit $gpg 5) PWR_MODE GPG6=$(bit $gpg 6) AMP_ON GPG11=$(bit $gpg 11)"
echo "EN_DR_PWR GPJ0=$(bit $gpj 0) LX_EN GPJ1=$(bit $gpj 1) EN_FM_PWR GPJ4=$(bit $gpj 4) GPS_ON GPJ6=$(bit $gpj 6) SD_PWR GPJ11=$(bit $gpj 11) DAC_PWR GPJ15=$(bit $gpj 15)"
echo "(GPJ10 PWR_RST=$(bit $gpj 10) must stay 0; GPJ14 movinand power=$(bit $gpj 14) must stay 0)"
