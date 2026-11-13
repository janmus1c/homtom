/* SKETCH ONLY, not compiled, not a patch. Linux 6.1 ASoC path for the GO 630 (Modena), rank 3 of 3 (needs a reboot per try).
 * Fits mach-homtom-go630.c; modelled on sound/soc/samsung/jive_wm8750.c:86-90 (dai_link, I2S, CBS_CFS = codec is clock slave) [V].
 *
 * Kconfig (homtom_defconfig):
 *   CONFIG_SND_SOC_SAMSUNG=y  CONFIG_SND_S3C2412_SOC_I2S=y  CONFIG_SND_SOC_WM8750=y  CONFIG_SPI_GPIO=y (now =m; the codec must exist at probe)
 *   (SND_S3C2412_SOC_I2S is a bare tristate in sound/soc/samsung/Kconfig:20, so a machine-driver Kconfig entry must `select` it) [V]
 *
 * Board file additions:
 *   1. spi-gpio master: sck GPB4, mosi GPB2, no miso, cs GPB3 (active low = CSB), mode 0, 16-bit word (reg<<9|data) MSB first [V] iomodena.h:79-81, spi.c:9-37
 *        static struct spi_gpio_platform_data homtom_spi = { .num_chipselect = 1 };  gpio descriptors via gpiod_lookup_table "spi_gpio"
 *        static struct spi_board_info homtom_codec_spi[] = { { .modalias = "wm8750", .max_speed_hz = 100000, .chip_select = 0, .mode = SPI_MODE_0 } };
 *      wm8750 SPI probe wants a regmap: write-only is fine, reads return cache [V] L/sound/soc/codecs/wm8750.c:765-775
 *      Do NOT request GPB0 (backlight) or GPB1 (BT_MODE).
 *   2. s3c2412-iis platform device at 0x55000000 (+0x100 size), clk aliases "iis" (PCLKCON bit 9) and "i2sclk" (6.1 has aliases "i2s-if"
 *      for SCLKCON bit 9, clk-s3c2443.c:173-174; add a NULL/"s3c2412-iis" alias) [V gap per HARDWARE_MAP 3.9]; DMA channel names from s3c2443_device_dma.
 *      s3c2412-i2s.c:65 calls clk_set_parent(i2sclk, mpll): mpll is not a parent of mux_i2s0 in 6.1, so it fails harmlessly [H]. Never let anything call
 *      clk_set_rate on epll: it feeds HSMMC1 (moviNAND), clk-s3c2443.c:64,107 [V].
 *   3. gpio_request: DAC_PWR_ON GPJ15 high via a gpio-regulator or fixed regulator supplying the codec "AVDD" (use gpio_request_one, NEVER a mask on GPJ10/14),
 *      AMP_ON GPG11 as the speaker widget, HEADPHONE_DETECT GPG3 (active low) as a snd_soc_jack_gpio.
 *
 * Machine driver (skeleton):
 *   static const struct snd_soc_dapm_widget homtom_widgets[] = { SND_SOC_DAPM_SPK("Speaker", homtom_amp_event), SND_SOC_DAPM_HP("Headphone", NULL) };
 *   homtom_amp_event(): gpio_set_value(S3C2410_GPG(11), SND_SOC_DAPM_EVENT_ON(event));     // [V] AMP_ON active high
 *   routes: "Speaker" <- "LOUT2","ROUT2" (differential, R26 |0x18 is the OUT2 power bits) ; "Headphone" <- "LOUT1","ROUT1"
 *   dai_link: cpu_dai_name "s3c2412-i2s", codec "wm8750.0-0000"? (spi: "spi0.0"), dai_fmt I2S|NB_NF|CBS_CFS.
 *   hw_params: snd_soc_dai_set_sysclk(codec_dai, 0, 12000000? / 256*rate, SND_SOC_CLOCK_IN); fs = 256 * rate. With the 12 MHz epllref path, rate is 46875 [H];
 *   a real 44.1/48 kHz needs EPLL (67.7/73.7 MHz in stock, iis-s3c2443.c:152-160) and must be arbitrated with HSMMC1's divider first [H].
 *   headset jack: snd_soc_card_jack_new + SND_SOC_JACK_HEADPHONE on GPG3, invert = 1.
 */
