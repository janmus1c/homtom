#!/bin/sh
# Apply in-repo homtom board support onto a Linux 6.1.y tree (default: .runtime/linux-6.1).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TREE=${1:-$ROOT/.runtime/linux-6.1}
H=$ROOT/kernel/homtom

[ -f "$TREE/Makefile" ] || { echo "no kernel tree at $TREE"; exit 1; }
ver=$(awk '/^VERSION /{v=$3} /^PATCHLEVEL /{p=$3} /^SUBLEVEL /{s=$3} END{print v"."p"."s}' "$TREE/Makefile")
case $ver in
6.1.*) ;;
*) echo "warn: expected 6.1.y, got $ver" ;;
esac

cp "$H/mach-homtom-go630.c" "$TREE/arch/arm/mach-s3c/mach-homtom-go630.c"
cp "$H/homtom_defconfig" "$TREE/arch/arm/configs/homtom_defconfig"

# mach-types: ATAG 605 = historical MACH_TOMTOMGO (OpenTom / TomTom bootloader)
if ! grep -q 'MACH_TOMTOMGO' "$TREE/arch/arm/tools/mach-types"; then
	printf '%s\n' 'tomtomgo		MACH_TOMTOMGO		TOMTOMGO		605' \
		>> "$TREE/arch/arm/tools/mach-types"
fi

# Refresh MACH_TOMTOMGO board Kconfig (insert or replace existing block)
if grep -q 'config MACH_TOMTOMGO' "$TREE/arch/arm/mach-s3c/Kconfig.s3c24xx"; then
	awk '
		BEGIN { skip=0 }
		/^config MACH_TOMTOMGO$/ {
			while ((getline line < "'"$H"'/Kconfig.board") > 0) print line
			close("'"$H"'/Kconfig.board")
			skip=1
			next
		}
		skip && /^config / { skip=0 }
		skip && /^comment / { skip=0 }
		skip && /^end(if|menu)/ { skip=0 }
		skip { next }
		{ print }
	' "$TREE/arch/arm/mach-s3c/Kconfig.s3c24xx" > "$TREE/arch/arm/mach-s3c/Kconfig.s3c24xx.new"
	mv "$TREE/arch/arm/mach-s3c/Kconfig.s3c24xx.new" "$TREE/arch/arm/mach-s3c/Kconfig.s3c24xx"
else
	awk '
		/^comment "S3C2443 Boards"/ {
			print
			print ""
			while ((getline line < "'"$H"'/Kconfig.board") > 0) print line
			close("'"$H"'/Kconfig.board")
			next
		}
		{ print }
	' "$TREE/arch/arm/mach-s3c/Kconfig.s3c24xx" > "$TREE/arch/arm/mach-s3c/Kconfig.s3c24xx.new"
	mv "$TREE/arch/arm/mach-s3c/Kconfig.s3c24xx.new" "$TREE/arch/arm/mach-s3c/Kconfig.s3c24xx"
fi

# Makefile
if ! grep -q 'mach-homtom-go630' "$TREE/arch/arm/mach-s3c/Makefile.s3c24xx"; then
	printf '%s\n' 'obj-$(CONFIG_MACH_TOMTOMGO)	+= mach-homtom-go630.o' \
		>> "$TREE/arch/arm/mach-s3c/Makefile.s3c24xx"
fi

# Mainline FB_S3C help text names S3C2443, but depends omitted it — enable for GO 630.
FBK="$TREE/drivers/video/fbdev/Kconfig"
if grep -q 'depends on (CPU_S3C2416 || ARCH_S3C64XX) || COMPILE_TEST' "$FBK"; then
	sed -i.bak \
		's/depends on (CPU_S3C2416 || ARCH_S3C64XX) || COMPILE_TEST/depends on (CPU_S3C2443 || CPU_S3C2416 || ARCH_S3C64XX) || COMPILE_TEST/' \
		"$FBK"
	rm -f "$FBK.bak"
fi

# GO 630 GSTATUS1 is 0x32443000; mainline lists only 0x32443001 with
# idmask 0xffffffff → Unknown CPU panic inside s3c24xx_init_io.
S3C24XX_C="$TREE/arch/arm/mach-s3c/s3c24xx.c"
if [ -f "$S3C24XX_C" ] && grep -q '0x32443001' "$S3C24XX_C"; then
	python3 - "$S3C24XX_C" <<'PY'
import pathlib, re, sys
p = pathlib.Path(sys.argv[1])
t = p.read_text()
n, c = re.subn(
    r"(\.idcode\s*=\s*0x32443001,\s*\n\s*\.idmask\s*=\s*)0xffffffff",
    r"\g<1>0xfffffffe",
    t,
    count=1,
)
if c:
    p.write_text(n)
    print("s3c2443 idmask widened for GSTATUS1 0x32443000")
PY
fi

# Boot color ladder (see kernel/homtom/COLOR_LADDER.txt).
MACRO="$H/homtom-solid-macro.S"
HEAD="$TREE/arch/arm/boot/compressed/head.S"
KHEAD="$TREE/arch/arm/kernel/head.S"
if [ -f "$MACRO" ] && [ -f "$HEAD" ]; then
	cp "$MACRO" "$TREE/arch/arm/boot/compressed/homtom-solid-macro.S"
	python3 "$ROOT/kernel/scripts/apply-color-ladder.py" "$HEAD" "$MACRO"
fi
KCOMMON="$TREE/arch/arm/kernel/head-common.S"
if [ -f "$MACRO" ] && [ -f "$KHEAD" ]; then
	python3 "$ROOT/kernel/scripts/apply-kernel-ladder.py" "$KHEAD" "$MACRO" "$KCOMMON"
fi

# PA==VA LCD marks while early idmap still lives (cleared in prepare_page_table).
# Physical BLUE hold ⇒ died after 6i, before paging_init orange — bisect here.
python3 - "$TREE" <<'PY'
import pathlib, re, sys

tree = pathlib.Path(sys.argv[1])

def strip_mark(t: str, name: str) -> str:
    return re.sub(
        rf"\n\t/\* {name}:.*?/\* end {name} \*/\n",
        "\n",
        t,
        flags=re.S,
    )

# Debug only, like the asm ladder and the board marks: compiled out unless built with -DHOMTOM_COLOR_LADDER
# (each mark is a full-screen colour plus a busy-wait pause).
def tpal_block(name: str, rgb_en: str, pause: str = "0x00800000") -> str:
    return (
        f"\t/* {name}: PA==VA TPAL {rgb_en} */\n"
        "#ifdef HOMTOM_COLOR_LADDER\n"
        "\t{\n"
        "\t\tvoid __iomem *tpal = (void __iomem *)0x4D000050UL;\n"
        f"\t\tvolatile unsigned n = {pause};\n"
        f"\t\twritel_relaxed({rgb_en}, tpal);\n"
        "\t\twhile (n--)\n"
        "\t\t\t;\n"
        "\t}\n"
        "#endif\n"
        f"\t/* end {name} */\n"
    )

# start_kernel entry — WHITE #FFFFFF
main = tree / "init/main.c"
t = strip_mark(main.read_text(), "homtom_sk_enter")
needle = "void start_kernel(void)\n{\n\tchar *command_line;\n"
if needle not in t:
    raise SystemExit("start_kernel insert point missing")
mark = (
    "void start_kernel(void)\n{\n"
    + tpal_block("homtom_sk_enter", "0x01FFFFFF")
    + "\tchar *command_line;\n"
)
main.write_text(t.replace(needle, mark, 1))
print("inserted homtom_sk_enter in start_kernel")

# setup_arch: after machine_desc assigned — CYAN #00FFFF
# and immediately before paging_init — YELLOW #FFFF00
setup = tree / "arch/arm/kernel/setup.c"
t = strip_mark(setup.read_text(), "homtom_mdesc_ok")
t = strip_mark(t, "homtom_pre_paging")
t = strip_mark(t, "homtom_sp_ok")
t = strip_mark(t, "homtom_fdt_ok")
n0 = "\tsetup_processor();\n"
if t.count(n0) != 1:
    raise SystemExit("setup_processor() call not unique")
t = t.replace(n0, n0 + tpal_block("homtom_sp_ok", "0x01FF00FF"), 1)
nf = "\tif (!mdesc)\n\t\tmdesc = setup_machine_tags(atags_vaddr, __machine_arch_type);\n"
if t.count(nf) != 1:
    raise SystemExit("setup_machine_tags call not unique")
t = t.replace(nf, tpal_block("homtom_fdt_ok", "0x0100FF00") + nf, 1)
n1 = "\tmachine_desc = mdesc;\n\tmachine_name = mdesc->name;\n"
if n1 not in t:
    raise SystemExit("machine_desc assign insert point missing")
t = t.replace(
    n1,
    n1 + tpal_block("homtom_mdesc_ok", "0x0100FFFF"),
    1,
)
n2 = "\tpaging_init(mdesc);\n"
if t.count(n2) != 1:
    raise SystemExit("paging_init call not unique")
t = t.replace(n2, tpal_block("homtom_pre_paging", "0x01FFFF00") + n2, 1)
setup.write_text(t)
print("inserted homtom_mdesc_ok + homtom_pre_paging in setup_arch")

# setup_machine_tags: split match vs atag handling (after physical GREEN on setuparch).
atags = tree / "arch/arm/kernel/atags_parse.c"
import tarfile
tarball = tree.parent / "linux-6.1.191.tar.xz"
if tarball.exists():
    with tarfile.open(tarball) as tf:
        t = tf.extractfile("linux-6.1.191/arch/arm/kernel/atags_parse.c").read().decode()
else:
    t = atags.read_text()
for m in ("homtom_mt_nomatch", "homtom_mt_match", "homtom_mt_tags", "homtom_mt_parsed"):
    t = strip_mark(t, m)
def once(t, needle, repl):
    if t.count(needle) != 1:
        raise SystemExit(f"atags_parse insert point missing: {needle!r}")
    return t.replace(needle, repl, 1)
BITDUMP = (
    "\t/* homtom_mt_nomatch: TomTom bootloader passes r1=0 (measured 2026-11-06); force 605 */\n"
    "\tmachine_nr = MACH_TYPE_TOMTOMGO;\n"
    "\t__machine_arch_type = MACH_TYPE_TOMTOMGO;\n"
    "\tfor_each_machine_desc(p)\n"
    "\t\tif (machine_nr == p->nr) {\n"
    "\t\t\tmdesc = p;\n"
    "\t\t\tbreak;\n"
    "\t\t}\n"
    "\t/* end homtom_mt_nomatch */\n"
)
t = once(t, "\tif (!mdesc)\n\t\treturn NULL;\n",
    "\tif (!mdesc) {\n" + BITDUMP
    + "\t\tif (!mdesc)\n\t\t\treturn NULL;\n\t}\n" + tpal_block("homtom_mt_match", "0x018000FF"))
t = once(t, "\tif (mdesc->fixup)\n\t\tmdesc->fixup(tags, &from);\n",
    tpal_block("homtom_mt_tags", "0x01FF80C0") + "\tif (mdesc->fixup)\n\t\tmdesc->fixup(tags, &from);\n")
t = once(t, "\t/* parse_early_param needs a boot_command_line */\n",
    tpal_block("homtom_mt_parsed", "0x01008080") + "\t/* parse_early_param needs a boot_command_line */\n")
t = t.replace("#include <linux/init.h>\n", "#include <linux/init.h>\n#include <linux/io.h>\n#include <asm/mach-types.h>\n", 1)
atags.write_text(t)
print("inserted homtom_mt_* marks in setup_machine_tags")

# paging_init entry — ORANGE #FF8000 (before prepare_page_table)
mmu = tree / "arch/arm/mm/mmu.c"
with tarfile.open(tarball) as tf:
    t = tf.extractfile("linux-6.1.191/arch/arm/mm/mmu.c").read().decode()
t = once(t, "\tfor (addr = 0; addr < MODULES_VADDR; addr += PMD_SIZE)\n\t\tpmd_clear(pmd_off_k(addr));\n#endif\n",
    "\tfor (addr = 0; addr < MODULES_VADDR; addr += PMD_SIZE) {\n"
    "\t\t/* homtom: keep LCD PA==VA section (0x4D000000) alive for color marks */\n"
    "\t\tif (addr >= 0x4D000000UL && addr < 0x4D100000UL)\n\t\t\tcontinue;\n"
    "\t\tif (addr == 0x00000000UL) {\n" + tpal_block("homtom_pt_pre0", "0x01FF00FF") + "\t\t}\n"
    + "\t\tpmd_clear(pmd_off_k(addr));\n"
    + "\t\tif (addr == 0x00000000UL) {\n" + tpal_block("homtom_pt_a0", "0x0100FFFF") + "\t\t}\n"
    + "\t\tif (addr == 0x2E000000UL) {\n" + tpal_block("homtom_pt_a1", "0x0100FF00") + "\t\t}\n"
    + "\t\tif (addr == 0x34000000UL) {\n" + tpal_block("homtom_pt_a2", "0x01FFFFFF") + "\t\t}\n"
    + "\t\tif (addr == 0x50000000UL) {\n" + tpal_block("homtom_pt_a3", "0x010000FF") + "\t\t}\n"
    + "\t}\n#endif\n"
    + tpal_block("homtom_pt_loop1", "0x0180FF80"))
t = once(t, "\tfor ( ; addr < PAGE_OFFSET; addr += PMD_SIZE)\n\t\tpmd_clear(pmd_off_k(addr));\n",
    "\tfor ( ; addr < PAGE_OFFSET; addr += PMD_SIZE)\n\t\tpmd_clear(pmd_off_k(addr));\n"
    + tpal_block("homtom_pt_loop2", "0x01FF8080"))
t = once(t, "static __init void prepare_page_table(void)\n{\n\tunsigned long addr;\n\tphys_addr_t end;\n",
    "static __init void prepare_page_table(void)\n{\n\tunsigned long addr;\n\tphys_addr_t end;\n"
    + tpal_block("homtom_pt_entry", "0x01FFFF00"))
t = once(t, "\tprepare_page_table();\n\tmap_lowmem();\n",
    tpal_block("homtom_pre_prepare", "0x01FF1493", "0x01000000")
    + "\tprepare_page_table();\n"
    + tpal_block("homtom_pp_done", "0x0180FF00")
    + "\tmap_lowmem();\n"
    + tpal_block("homtom_ml_done", "0x01804000"))
t = once(t, "\tearly_fixmap_shutdown();\n\tdevicemaps_init(mdesc);\n",
    tpal_block("homtom_mk_done", "0x01808080")
    + "\tearly_fixmap_shutdown();\n\tdevicemaps_init(mdesc);\n"
    + tpal_block("homtom_dm_done", "0x01000080"))
t = once(t, "\tbootmem_init();\n\n\tempty_zero_page",
    tpal_block("homtom_pre_bootmem", "0x01800000") + "\tbootmem_init();\n"
    + tpal_block("homtom_bm_done", "0x01FFC000") + "\n\tempty_zero_page")
mmu.write_text(t)
print("inserted homtom_pre_prepare before prepare_page_table()")
PY

# initcall index display: before each initcall, draw its global index as 10 binary squares
# (white=1, black=0, MSB left, red bar = left edge) on the LCD framebuffer. See kernel/scripts/initcall-table.py.
python3 - "$TREE" <<'PY'
import pathlib, re, sys
tree = pathlib.Path(sys.argv[1])
main = tree / "init/main.c"
t = main.read_text()
t = re.sub(r"\n/\* homtom_il_begin \*/.*?/\* homtom_il_end \*/\n", "\n", t, flags=re.S)
t = re.sub(r"\n\t/\* homtom_il_begin \*/.*?/\* homtom_il_end \*/\n", "\n", t, flags=re.S)
HELP = r"""
/* homtom_il_begin */
unsigned int homtom_ic_idx;
extern void homtom_dbg_stage(unsigned int n);
static void __iomem *homtom_ic_fb __initdata;
static void __init homtom_ic_fill(unsigned int x0, unsigned int y0, unsigned int w,
				  unsigned int h, u16 c)
{
	unsigned int x, y;

	for (y = y0; y < y0 + h; y++)
		for (x = x0; x < x0 + w; x++)
			((u16 *)homtom_ic_fb)[y * 480 + x] = c;
}
static void __init homtom_ic_show(unsigned int idx)
{
	void __iomem *lcd = (void __iomem *)0xf7500000UL;
	int i;

	if (1)
		return;

	__raw_writel(0, lcd + 0x50);
	if (!homtom_ic_fb) {
		unsigned long phys = (unsigned long)__raw_readl(lcd + 0x14) << 1;

		if (phys < 0x30000000UL || phys >= 0x34000000UL - 480 * 272 * 2)
			return;
		homtom_ic_fb = (void __iomem *)phys_to_virt(phys);
		homtom_ic_fill(0, 0, 480, 272, 0x87E0);
	}
	homtom_ic_fill(6, 104, 10, 48, 0xF800);
	for (i = 9; i >= 0; i--)
		homtom_ic_fill(24 + (9 - i) * 44, 110, 36, 36,
			       (idx >> i) & 1 ? 0xFFFF : 0x0000);
	for (i = 9; i >= 0; i--)
		homtom_ic_fill(24 + (9 - i) * 44, 156, 36, 20,
			       (idx >> i) & 1 ? 0x0000 : 0xFFFF);
	__cpuc_flush_dcache_area((void *)homtom_ic_fb + 104 * 480 * 2, 78 * 480 * 2);
}
/* homtom_il_end */
"""
needle = "static void __init do_initcall_level(int level, char *command_line)\n"
if t.count(needle) != 1:
    raise SystemExit("do_initcall_level not unique")
t = t.replace(needle, HELP + "\n" + needle, 1)
n2 = "\tfor (fn = initcall_levels[level]; fn < initcall_levels[level+1]; fn++)\n\t\tdo_one_initcall(initcall_from_entry(fn));\n"
patched = "\tfor (fn = initcall_levels[level]; fn < initcall_levels[level+1]; fn++) {\n\t\tdo_one_initcall(initcall_from_entry(fn));\n\t}\n"
t = t.replace(patched, n2)
t = re.sub(r"\tfor \(fn = initcall_levels\[level\]; fn < initcall_levels\[level\+1\]; fn\+\+\) \{\n\t\t/\* homtom_il_begin \*/.*?\n\t\tdo_one_initcall\(initcall_from_entry\(fn\)\);\n\t\}\n", lambda m: n2, t, flags=re.S)
if t.count(n2) != 1:
    raise SystemExit("initcall loop not found")
t = t.replace(n2,
    "\tfor (fn = initcall_levels[level]; fn < initcall_levels[level+1]; fn++) {\n"
    "\t\t/* homtom_il_begin */\n\t\thomtom_ic_show(homtom_ic_idx++);\n\t\t/* homtom_il_end */\n"
    "\t\tdo_one_initcall(initcall_from_entry(fn));\n\t}\n", 1)
for inc in ("<linux/io.h>", "<asm/cacheflush.h>", "<asm/memory.h>"):
    if "#include " + inc not in t:
        t = t.replace("#include <linux/init.h>\n", "#include <linux/init.h>\n#include " + inc + "\n", 1)
main.write_text(t)
print("inserted homtom initcall index display")
PY

# s3c-hsudc: 6.1 clk_enable() without prepare is a no-op warning; prepare+enable the bus clock.
HSUDC="$TREE/drivers/usb/gadget/udc/s3c-hsudc.c"
if grep -q '^	clk_enable(hsudc->uclk);' "$HSUDC"; then
	python3 - "$HSUDC" <<'PY'
import pathlib, sys
p = pathlib.Path(sys.argv[1])
t = p.read_text()
t = t.replace("\tclk_enable(hsudc->uclk);\n", "\tret = clk_prepare_enable(hsudc->uclk);\n\tif (ret) {\n\t\tdev_err(dev, \"failed to enable usb-device clock\\n\");\n\t\tgoto err_res;\n\t}\n", 1)
p.write_text(t)
print("s3c-hsudc: clk_prepare_enable")
PY
fi

# Crash/heartbeat display: oops/panic PC+LR as 32-bit rows (y=196/216), tick heartbeat square (top right).
python3 - "$TREE" <<'PY'
import pathlib, re, sys
tree = pathlib.Path(sys.argv[1])
def strip(t):
    t = re.sub(r"\n/\* homtom_dbg_begin \*/.*?/\* homtom_dbg_end \*/\n", "\n", t, flags=re.S)
    t = re.sub(r"[ \t]*/\* homtom_dbg_begin \*/.*?/\* homtom_dbg_end \*/\n", "", t, flags=re.S)
    return t
def once(t, needle, repl):
    if t.count(needle) != 1:
        raise SystemExit(f"homtom_dbg insert point missing: {needle!r}")
    return t.replace(needle, repl, 1)

HELP = r"""
/* homtom_dbg_begin */
#define HOMTOM_DBG_DRAW 0
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <asm/cputype.h>
#include <asm/cp15.h>
#include <asm/domain.h>
#include <asm/system_info.h>
#include <asm/irq_regs.h>
#include <asm/memory.h>
#include <asm/cacheflush.h>
extern unsigned int homtom_ic_idx;
static u16 *homtom_dbg_fbva(void)
{
	void __iomem *lcd = (void __iomem *)0xf7500000UL;
	unsigned long phys = (unsigned long)__raw_readl(lcd + 0x14) << 1;

	if (phys < 0x30000000UL || phys >= 0x34000000UL - 480 * 272 * 2)
		return NULL;
	return (u16 *)phys_to_virt(phys);
}
static void homtom_dbg_fill(u16 *fb, unsigned int x0, unsigned int y0,
			    unsigned int w, unsigned int h, u16 c)
{
	unsigned int x, y;

	for (y = y0; y < y0 + h && y < 272; y++)
		for (x = x0; x < x0 + w && x < 480; x++)
			fb[y * 480 + x] = c;
}
static const u16 homtom_dbg_font[16] = {
	0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7252,
	0x7BEF, 0x7BCF, 0x2BED, 0x6BAE, 0x3923, 0x6B6E, 0x79A7, 0x79A4
};
static void homtom_dbg_hex(u16 *fb, unsigned int x0, unsigned int y0, u32 v)
{
	int d, r, c;

	for (d = 0; d < 8; d++) {
		unsigned int g = homtom_dbg_font[(v >> (28 - 4 * d)) & 15];

		for (r = 0; r < 5; r++)
			for (c = 0; c < 3; c++)
				homtom_dbg_fill(fb, x0 + d * 12 + c * 3,
						y0 + r * 3, 3, 3,
						(g >> (14 - (r * 3 + c))) & 1 ?
						0xFFFF : 0x0000);
	}
}
static void homtom_dbg_word(u16 *fb, unsigned int row, unsigned int col, u32 v)
{
	homtom_dbg_hex(fb, 12 + col * 108, 4 + row * 18, v);
}
void homtom_dbg_dump(struct pt_regs *regs, unsigned long addr, unsigned long fsr)
{
	static int done = !HOMTOM_DBG_DRAW;
	u16 *fb;
	u32 *sp;
	unsigned int i;

	if (done)
		return;
	done = 1;
	fb = homtom_dbg_fbva();
	if (!fb)
		return;
	__raw_writel(0, (void __iomem *)0xf7500050UL);
	homtom_dbg_fill(fb, 0, 0, 480, 272, 0x0000);
	homtom_dbg_word(fb, 0, 0, addr);
	homtom_dbg_word(fb, 0, 1, fsr);
	homtom_dbg_word(fb, 0, 2, regs->ARM_pc);
	homtom_dbg_word(fb, 0, 3, regs->ARM_lr);
	homtom_dbg_word(fb, 1, 0, regs->ARM_sp);
	homtom_dbg_word(fb, 1, 1, regs->ARM_cpsr);
	homtom_dbg_word(fb, 1, 2, current->pid);
	homtom_dbg_word(fb, 1, 3, preempt_count());
	for (i = 0; i < 4; i++) {
		homtom_dbg_word(fb, 2, i, regs->uregs[i]);
		homtom_dbg_word(fb, 3, i, regs->uregs[4 + i]);
		homtom_dbg_word(fb, 4, i, regs->uregs[8 + i]);
	}
	sp = (u32 *)regs->ARM_sp;
	{
		void __iomem *lcd = (void __iomem *)0xf7500000UL;

		homtom_dbg_word(fb, 14, 0, (unsigned long)__raw_readl(lcd + 0x14) << 1);
		homtom_dbg_word(fb, 14, 1, __raw_readl(lcd + 0x14));
		homtom_dbg_word(fb, 14, 2, __raw_readl(lcd + 0x18));
		homtom_dbg_word(fb, 14, 3, __raw_readl(lcd + 0x1c));
	}
	for (i = 0; i < 36 && (unsigned long)sp >= 0xC0000000UL; i++) {
		u32 w;

		if (copy_from_kernel_nofault(&w, sp + i, 4))
			break;
		homtom_dbg_word(fb, 5 + i / 4, i % 4, w);
	}
	__cpuc_flush_dcache_area((void *)fb, 480 * 272 * 2);
}
void homtom_dbg_panic(unsigned long caller)
{
	u16 *fb;

	if (!HOMTOM_DBG_DRAW)
		return;
	fb = homtom_dbg_fbva();

	if (!fb)
		return;
	homtom_dbg_fill(fb, 456, 248, 24, 24, 0x001F);
	homtom_dbg_hex(fb, 356, 254, caller);
	__cpuc_flush_dcache_area((void *)fb + 248 * 480 * 2, 24 * 480 * 2);
}
void homtom_exc_dbg(int which)
{
	static unsigned int cnt[6];
	static const char *const nm[] = { "ret_to_user", "pabt_usr", "dabt_usr", "irq_usr", "und_usr", "swi" };
	struct pt_regs *r = task_pt_regs(current);

	if (which < 0 || which > 5 || cnt[which] >= 4)
		return;
	if (which == 0 && current->pid != 1)
		return;
	cnt[which]++;
	printk(KERN_INFO "homtom: %s #%u pid %d pc %08lx psr %08lx\n", nm[which], cnt[which], current->pid,
	       r->ARM_pc, r->ARM_cpsr);
	if (which == 0 && cnt[0] == 1) {
		u32 w[4];
		int i, bad = 0;

		for (i = 0; i < 4; i++)
			if (get_user(w[i], (u32 __user *)(0x100b8 + 4 * i)))
				bad = 1;
		printk(KERN_INFO "homtom: user text @100b8 %s %08x %08x %08x %08x\n", bad ? "UNREADABLE" : "=", w[0], w[1], w[2], w[3]);
		printk(KERN_INFO "homtom: cr_alignment %08lx live cr %08x domain %08x\n", cr_alignment, get_cr(), get_domain());
		__cpuc_flush_kern_all();
		printk(KERN_INFO "homtom: caches flushed before user return\n");

	}
}
void homtom_dbg_stage(unsigned int n)
{
	u16 *fb;

	printk(KERN_INFO "homtom: stage %#x jiffies %lu\n", n, jiffies);
	if (n == 0x10B) {
		printk(KERN_INFO "homtom: cpuid %08x cr %08x hwcap %08lx domain %08x vbar_high %d\n",
		       read_cpuid_id(), get_cr(), elf_hwcap, get_domain(), vectors_high());
		{
			void __iomem *irq = (void __iomem *)0xF6000000UL;

			printk(KERN_INFO "homtom: irq INTMOD %08x SRCPND %08x INTPND %08x INTMSK %08x SUBSRCPND %08x INTSUBMSK %08x\n",
			       __raw_readl(irq + 0x04), __raw_readl(irq + 0x00), __raw_readl(irq + 0x10),
			       __raw_readl(irq + 0x08), __raw_readl(irq + 0x18), __raw_readl(irq + 0x1c));
		}
	}
	if (!HOMTOM_DBG_DRAW)
		return;
	fb = homtom_dbg_fbva();

	if (!fb)
		return;
	{
		void __iomem *irq = (void __iomem *)0xF6000000UL;

		homtom_dbg_word(fb, 12, 0, n);
		homtom_dbg_word(fb, 12, 1, (u32)jiffies);
		homtom_dbg_word(fb, 12, 2, __raw_readl(irq + 0x00) & ~__raw_readl(irq + 0x08));	/* SRCPND & ~INTMSK */
		homtom_dbg_word(fb, 12, 3, __raw_readl(irq + 0x08));	/* INTMSK */
	}
	__cpuc_flush_dcache_area((void *)fb + 216 * 480 * 2, 20 * 480 * 2);
}
void homtom_dbg_hb(void)
{
	static unsigned int c;

	if (!HOMTOM_DBG_DRAW)
		return;
	static int dumped;
	struct pt_regs *r;
	u16 *fb;

	if (++c % 4)
		return;
	fb = homtom_dbg_fbva();
	if (!fb || dumped)
		return;
	r = get_irq_regs();
	homtom_dbg_fill(fb, 440, 10, 32, 32, (c / 4) & 1 ? 0xFFFF : 0x001F);
	homtom_dbg_word(fb, 0, 0, (u32)jiffies);
	homtom_dbg_word(fb, 0, 1, r ? r->ARM_pc : 0);
	homtom_dbg_word(fb, 0, 2, r ? r->ARM_lr : 0);
	homtom_dbg_word(fb, 0, 3, homtom_ic_idx);
	{
		void __iomem *irq = (void __iomem *)0xF6000000UL;
		void __iomem *wdt = (void __iomem *)0xF6400000UL;

		homtom_dbg_word(fb, 1, 0, __raw_readl(wdt + 0x00));	/* WTCON */
		homtom_dbg_word(fb, 1, 1, __raw_readl(wdt + 0x08));	/* WTCNT */
		homtom_dbg_word(fb, 1, 2, __raw_readl(irq + 0x00));	/* SRCPND */
		homtom_dbg_word(fb, 1, 3, __raw_readl(irq + 0x10));	/* INTPND */
		homtom_dbg_word(fb, 2, 0, __raw_readl(irq + 0x08));	/* INTMSK */
		homtom_dbg_word(fb, 2, 1, __raw_readl(irq + 0x18));	/* SUBSRCPND */
		homtom_dbg_word(fb, 2, 2, __raw_readl(irq + 0x1c));	/* INTSUBMSK */
		homtom_dbg_word(fb, 2, 3, r ? r->ARM_cpsr : 0);
		if (r && r->ARM_pc >= 0xC0008000UL) {
			unsigned int k;

			for (k = 0; k < 4; k++) {
				u32 w = 0xDEADDEAD;

				copy_from_kernel_nofault(&w, (u32 *)(r->ARM_pc - 4 + 4 * k), 4);
				homtom_dbg_word(fb, 3, k, w);
			}
		}
	}
	__cpuc_flush_dcache_area((void *)fb, 80 * 480 * 2);
}
/* homtom_dbg_end */
"""
setup = tree / "arch/arm/kernel/setup.c"
t = strip(setup.read_text())
t = once(t, '#include "atags.h"\n', '#include "atags.h"\n' + HELP)
setup.write_text(t)

traps = tree / "arch/arm/kernel/traps.c"
t = strip(traps.read_text())
t = once(t, "void die(const char *str, struct pt_regs *regs, int err)\n{\n",
    "/* homtom_dbg_begin */\nextern void homtom_dbg_dump(struct pt_regs *regs, unsigned long addr, unsigned long fsr);\n/* homtom_dbg_end */\n"
    "void die(const char *str, struct pt_regs *regs, int err)\n{\n")
t = once(t, "\tif (!user_mode(regs))\n\t\tbug_type = report_bug(regs->ARM_pc, regs);\n\tif (bug_type != BUG_TRAP_TYPE_NONE)\n\t\tstr = \"Oops - BUG\";\n",
    "\t/* homtom_dbg_begin */\n\thomtom_dbg_dump(regs, 0, 0);\n\t/* homtom_dbg_end */\n"
    "\tif (!user_mode(regs))\n\t\tbug_type = report_bug(regs->ARM_pc, regs);\n\tif (bug_type != BUG_TRAP_TYPE_NONE)\n\t\tstr = \"Oops - BUG\";\n")
traps.write_text(t)

panic = tree / "kernel/panic.c"
t = strip(panic.read_text())
t = once(t, "\tbool _crash_kexec_post_notifiers = crash_kexec_post_notifiers;\n\n",
    "\tbool _crash_kexec_post_notifiers = crash_kexec_post_notifiers;\n\n"
    "\t/* homtom_dbg_begin */\n\t{\n\t\textern void homtom_dbg_panic(unsigned long caller);\n"
    "\t\thomtom_dbg_panic((unsigned long)__builtin_return_address(0));\n\t}\n\t/* homtom_dbg_end */\n")
panic.write_text(t)

core = tree / "kernel/sched/core.c"
t = strip(core.read_text())
t = once(t, "void scheduler_tick(void)\n{\n\tint cpu = smp_processor_id();\n",
    "void scheduler_tick(void)\n{\n\tint cpu = smp_processor_id();\n"
    "\t/* homtom_dbg_begin */\n\t{\n\t\textern void homtom_dbg_hb(void);\n\t\thomtom_dbg_hb();\n\t}\n\t/* homtom_dbg_end */\n")
core.write_text(t)

fault = tree / "arch/arm/mm/fault.c"
t = strip(fault.read_text())
t = re.sub(r"(\treturn;\n)\n+(\t/\*\n\t \* No handler)", r"\1\n\2", t)
t = once(t, "\tif (fixup_exception(regs))\n\t\treturn;\n\n\t/*\n\t * No handler, we'll have to terminate things with extreme prejudice.\n",
    "\tif (fixup_exception(regs))\n\t\treturn;\n\n"
    "\t/* homtom_dbg_begin */\n\t{\n\t\textern void homtom_dbg_dump(struct pt_regs *regs, unsigned long addr, unsigned long fsr);\n"
    "\t\thomtom_dbg_dump(regs, addr, fsr);\n\t}\n\t/* homtom_dbg_end */\n\n"
    "\t/*\n\t * No handler, we'll have to terminate things with extreme prejudice.\n")
fault.write_text(t)
print("inserted homtom crash/heartbeat display")
PY

# kernel_init stage markers (rows y=220): 0x101 after do_basic_setup ... 0x10B before run_init_process.
python3 - "$TREE" <<'PY'
import pathlib, re, sys
tree = pathlib.Path(sys.argv[1])
main = tree / "init/main.c"
t = main.read_text()
t = re.sub(r"[ \t]*/\* homtom_ks_begin \*/.*?/\* homtom_ks_end \*/\n", "", t, flags=re.S)
def mark(n):
    return "\t/* homtom_ks_begin */ homtom_dbg_stage(0x%X); /* homtom_ks_end */\n" % n
def once(t, needle, repl):
    if t.count(needle) != 1:
        raise SystemExit(f"kernel_init stage insert point missing: {needle!r}")
    return t.replace(needle, repl, 1)
t = once(t, "\tdo_basic_setup();\n", "\tdo_basic_setup();\n" + mark(0x101))
t = once(t, "\twait_for_initramfs();\n\tconsole_on_rootfs();\n",
         "\twait_for_initramfs();\n" + mark(0x102) + "\tconsole_on_rootfs();\n" + mark(0x103))
t = once(t, "\tkernel_init_freeable();\n\t/* need to finish all async __init code before freeing the memory */\n\tasync_synchronize_full();\n",
         "\tkernel_init_freeable();\n" + mark(0x106) + "\t/* need to finish all async __init code before freeing the memory */\n\tasync_synchronize_full();\n" + mark(0x107))
t = once(t, "\texit_boot_config();\n\tfree_initmem();\n\tmark_readonly();\n",
         "\texit_boot_config();\n" + mark(0x108) + "\tfree_initmem();\n" + mark(0x109) + "\tmark_readonly();\n" + mark(0x10A))
t = once(t, "\tdo_sysctl_args();\n\n\tif (ramdisk_execute_command) {\n",
         "\tdo_sysctl_args();\n" + mark(0x10B) + "\n\tif (ramdisk_execute_command) {\n")
main.write_text(t)
print("inserted kernel_init stage markers")
PY

# free_initmem: poisoning/freeing init memory kills the tick on this board (measured: stage 0x108 reached, 0x109 never).
# Something still references .init* after boot. Keep init memory until the reference is found (costs <1 MB RAM).
python3 - "$TREE" <<'PY'
import pathlib, sys, tarfile
tree = pathlib.Path(sys.argv[1])
tarball = tree.parent / "linux-6.1.191.tar.xz"
init = tree / "arch/arm/mm/init.c"
if tarball.exists():
    with tarfile.open(tarball) as tf:
        t = tf.extractfile("linux-6.1.191/arch/arm/mm/init.c").read().decode()
else:
    t = init.read_text()
old = "void free_initmem(void)\n{\n\tfix_kernmem_perms();\n\n\tpoison_init_mem(__init_begin, __init_end - __init_begin);\n\tif (!machine_is_integrator() && !machine_is_cintegrator())\n\t\tfree_initmem_default(-1);\n}\n"
if t.count(old) != 1:
    raise SystemExit("free_initmem body not found")
new = ("/* homtom: keep init memory (poison/free killed the tick; see COLOR_LADDER.txt) */\n"
       "extern void homtom_dbg_stage(unsigned int n);\n"
       "void free_initmem(void)\n{\n\thomtom_dbg_stage(0x120);\n\treturn;\n}\n")
init.write_text(t.replace(old, new, 1))
print("free_initmem: keep init memory")
PY

# LCD text console (kernel/homtom/homtom-fbtext.c): printk to the framebuffer, registered right after paging_init().
cp "$H/homtom-fbtext.c" "$TREE/arch/arm/kernel/homtom-fbtext.c"
cp "$H/homtom-splash.fb" "$TREE/arch/arm/kernel/homtom-splash.fb"
grep -q homtom-splash "$TREE/arch/arm/kernel/Makefile" ||
	printf '%s\n' '$(obj)/homtom-fbtext.o: $(src)/homtom-splash.fb' >> "$TREE/arch/arm/kernel/Makefile"
grep -q homtom-fbtext "$TREE/arch/arm/kernel/Makefile" ||
	printf '%s\n' 'obj-$(CONFIG_MACH_TOMTOMGO)	+= homtom-fbtext.o' >> "$TREE/arch/arm/kernel/Makefile"
python3 - "$TREE" <<'PY'
import pathlib, re, sys
setup = pathlib.Path(sys.argv[1]) / "arch/arm/kernel/setup.c"
t = setup.read_text()
t = re.sub(r"[ \t]*/\* homtom_fbtext_begin \*/.*?/\* homtom_fbtext_end \*/\n", "", t, flags=re.S)
needle = "\tpaging_init(mdesc);\n"
if t.count(needle) != 1:
    raise SystemExit("paging_init call not unique for fbtext")
t = t.replace(needle, needle + "\t/* homtom_fbtext_begin */\n\t{\n\t\textern void homtom_fbtext_init(void);\n\t\thomtom_fbtext_init();\n\t}\n\t/* homtom_fbtext_end */\n", 1)
setup.write_text(t)
print("registered homtom fbtext console after paging_init")
PY

# exec-path / user-fault tracing (user-mode entry hangs the machine; see COLOR_LADDER.txt ladder-idle/ladder-trace).
python3 - "$TREE" <<'PY'
import pathlib, re, sys
tree = pathlib.Path(sys.argv[1])
def strip(t):
    return re.sub(r"[ \t]*/\* homtom_tr_begin \*/.*?/\* homtom_tr_end \*/\n", "", t, flags=re.S)
def once(t, needle, repl):
    if t.count(needle) != 1:
        raise SystemExit(f"trace insert point missing: {needle!r}")
    return t.replace(needle, repl, 1)
elf = tree / "fs/binfmt_elf.c"
t = strip(elf.read_text())
t = once(t, "\tSTART_THREAD(elf_ex, regs, elf_entry, bprm->p);\n",
    "\t/* homtom_tr_begin */ { static int homtom_e1; if (homtom_e1++ < 3) pr_info(\"homtom: elf %s entry %lx sp %lx\\n\", bprm->filename, (unsigned long)elf_entry, (unsigned long)bprm->p); } /* homtom_tr_end */\n"
    "\tSTART_THREAD(elf_ex, regs, elf_entry, bprm->p);\n"
    "\t/* homtom_tr_begin */ { static int homtom_e2; if (homtom_e2++ < 3) pr_info(\"homtom: elf start_thread done pc %lx sp %lx cpsr %lx\\n\", regs->ARM_pc, regs->ARM_sp, regs->ARM_cpsr); } /* homtom_tr_end */\n")
elf.write_text(t)
fault = tree / "arch/arm/mm/fault.c"
t = strip(fault.read_text())
t = once(t, "\tif (kprobe_page_fault(regs, fsr))\n\t\treturn 0;\n",
    "\tif (kprobe_page_fault(regs, fsr))\n\t\treturn 0;\n"
    "\t/* homtom_tr_begin */\n\tif (user_mode(regs)) {\n\t\tstatic int homtom_uf;\n\n\t\tif (homtom_uf++ < 60)\n"
    "\t\t\tpr_info(\"homtom: user fault addr %08lx fsr %03x pc %08lx lr %08lx\\n\", addr, fsr, regs->ARM_pc, regs->ARM_lr);\n\t}\n\t/* homtom_tr_end */\n")
fault.write_text(t)
print("inserted exec/user-fault trace")
PY

# user-mode entry/exit asm tracing (first 4 events each): ret_to_user, pabt_usr, dabt_usr, irq_usr.
python3 - "$TREE" <<'PY'
import pathlib, re, sys
tree = pathlib.Path(sys.argv[1])
def strip(t):
    return re.sub(r"[ \t]*@ homtom_ex_begin.*?@ homtom_ex_end\n", "", t, flags=re.S)
def once(t, needle, repl):
    if t.count(needle) != 1:
        raise SystemExit(f"asm trace insert point missing: {needle!r}")
    return t.replace(needle, repl, 1)
def hook(n):
    return "\t@ homtom_ex_begin\n\tmov\tr0, #%d\n\tbl\thomtom_exc_dbg\n\t@ homtom_ex_end\n" % n
armv = tree / "arch/arm/kernel/entry-armv.S"
t = strip(armv.read_text())
t = once(t, "__dabt_usr:\n\tusr_entry uaccess=0\n", "__dabt_usr:\n\tusr_entry uaccess=0\n" + hook(2))
t = once(t, "__irq_usr:\n\tusr_entry\n", "__irq_usr:\n\tusr_entry\n" + hook(3))
t = once(t, "__pabt_usr:\n\tusr_entry\n", "__pabt_usr:\n\tusr_entry\n" + hook(1))
t = once(t, "__und_usr:\n\tusr_entry uaccess=0\n", "__und_usr:\n\tusr_entry uaccess=0\n" + hook(4))
armv.write_text(t)
com = tree / "arch/arm/kernel/entry-common.S"
t = strip(com.read_text())
t = once(t, "\tarch_ret_to_user r1\n\tct_user_enter save = 0\n\n\trestore_user_regs fast = 0, offset = 0\nENDPROC(ret_to_user_from_irq)",
         "\tarch_ret_to_user r1\n\tct_user_enter save = 0\n" + hook(0) + "\n\trestore_user_regs fast = 0, offset = 0\nENDPROC(ret_to_user_from_irq)")
t = once(t, "\treload_current r10, ip\n\tzero_fp\n\talignment_trap r10, ip, cr_alignment\n\tasm_trace_hardirqs_on save=0\n\tenable_irq_notrace\n",
    "\treload_current r10, ip\n\tzero_fp\n" + hook(5) + "\t@ homtom_ex_begin\n\tldmia\tsp, {r0 - r3}\n\tldr\tlr, [sp, #S_PC]\n\t@ homtom_ex_end\n\talignment_trap r10, ip, cr_alignment\n\tasm_trace_hardirqs_on save=0\n\tenable_irq_notrace\n")
com.write_text(t)
print("inserted user-mode asm trace hooks")
PY

cp "$H/homtom-fiq.S" "$TREE/arch/arm/kernel/homtom-fiq.S"
grep -q homtom-fiq "$TREE/arch/arm/kernel/Makefile" ||
	printf '%s\n' 'obj-$(CONFIG_MACH_TOMTOMGO)	+= homtom-fiq.o' >> "$TREE/arch/arm/kernel/Makefile"

# USB gadget debug: compile ECM function, u_ether and composite with -DDEBUG so DBG() (notify connect/speed, queue errors) reach the console.
for mk in "$TREE/drivers/usb/gadget/function/Makefile:CFLAGS_f_ecm.o" "$TREE/drivers/usb/gadget/function/Makefile:CFLAGS_u_ether.o" "$TREE/drivers/usb/gadget/Makefile:CFLAGS_composite.o"; do
	f=${mk%%:*}; v=${mk#*:}
	grep -q "^$v " "$f" || printf '%s += -DDEBUG\n' "$v" >> "$f"
done

# s3c-hsudc: trace the interrupt-IN (ECM notify) endpoint: immediate write vs queued-waiting-for-TX_SUCCESS.
python3 - "$TREE" <<'PY'
import pathlib, re, sys
f = pathlib.Path(sys.argv[1]) / "drivers/usb/gadget/udc/s3c-hsudc.c"
t = f.read_text()
t = re.sub(r"[ \t]*/\* homtom_hs_begin \*/.*?/\* homtom_hs_end \*/\n", "", t, flags=re.S)
def once(t, needle, repl):
    if t.count(needle) != 1:
        raise SystemExit(f"hsudc insert point missing: {needle!r}")
    return t.replace(needle, repl, 1)
t = once(t, "\t\t\tcsr = readl(hsudc->regs + offset);\n\t\t\tif (!(csr & S3C_ESR_TX_SUCCESS) &&\n",
    "\t\t\tcsr = readl(hsudc->regs + offset);\n"
    "\t\t\t/* homtom_hs_begin */ if (ep_index(hsep) && hsep->ep.desc && usb_endpoint_xfer_int(hsep->ep.desc)) pr_info(\"homtom: hsudc int-in ep%d queue csr %08x len %u\\n\", ep_index(hsep), csr, _req->length); /* homtom_hs_end */\n"
    "\t\t\tif (!(csr & S3C_ESR_TX_SUCCESS) &&\n")
t = once(t, "\tif (hsreq)\n\t\tlist_add_tail(&hsreq->queue, &hsep->queue);\n",
    "\t/* homtom_hs_begin */ if (ep_index(hsep) && hsep->ep.desc && usb_endpoint_xfer_int(hsep->ep.desc) && ep_is_in(hsep)) pr_info(\"homtom: hsudc int-in %s\\n\", hsreq ? \"QUEUED waiting for TX_SUCCESS irq\" : \"written to FIFO\"); /* homtom_hs_end */\n"
    "\tif (hsreq)\n\t\tlist_add_tail(&hsreq->queue, &hsep->queue);\n")
f.write_text(t)
print("inserted hsudc int-in trace")
PY

echo "applied homtom board to $TREE ($ver)"
