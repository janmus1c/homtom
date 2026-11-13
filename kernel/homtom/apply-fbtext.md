# Integrating homtom-fbtext.c (Linux 6.1.191, homtom GO 630)

## 1. Where to call `homtom_fbtext_init()`

Directly after `paging_init(mdesc);` in `setup_arch()`, `arch/arm/kernel/setup.c`
(line 1366 in the patched tree):

```c
	/* end homtom_pre_paging */
	paging_init(mdesc);
	/* homtom_fbtext_begin */
	{
		extern void homtom_fbtext_init(void);
		homtom_fbtext_init();
	}
	/* homtom_fbtext_end */
	kasan_init();
```

Why this is the earliest valid point (verified in `.runtime/linux-6.1`):

- `setup.c:1351` `arm_memblock_init(mdesc)` runs `mdesc->reserve` = `homtom_reserve()`,
  which points LCDSADDR1 at 0x33F00000. That happens before paging_init.
- `mm/mmu.c:1850` `paging_init()` calls `map_lowmem()` (lowmem, so `phys_to_virt(0x33F00000)`
  is mapped), then `mmu.c:1909` `devicemaps_init(mdesc)`.
- `mmu.c:1540-1541` inside devicemaps_init: `if (mdesc->map_io) mdesc->map_io();` →
  `homtom_map_io()` runs `iotable_init(homtom_lcd_iodesc, ...)`, creating the 0xF7500000 LCD mapping.
- devicemaps_init ends with `local_flush_tlb_all(); flush_cache_all();`.

Before paging_init, the 0xF7500000 mapping does not exist (homtom code itself still writes
PA 0x4D000050). A `console_initcall` would also work but runs later (console_init() in
start_kernel, after setup_arch); there is no reason to wait.

`register_console()` takes `console_lock()` and `mutex_lock(&syslog_lock)`. That is the same
thing earlycon does from `parse_early_param()` inside setup_arch, so calling it here is fine
(inference from code: might_sleep checks are skipped while `system_state == SYSTEM_BOOTING`).

## 2. Build wiring

Following the existing apply-homtom.sh pattern (copy + append once):

```sh
cp "$H/homtom-fbtext.c" "$TREE/arch/arm/kernel/homtom-fbtext.c"
grep -q homtom-fbtext "$TREE/arch/arm/kernel/Makefile" ||
	printf '%s\n' 'obj-$(CONFIG_MACH_TOMTOMGO)	+= homtom-fbtext.o' >> "$TREE/arch/arm/kernel/Makefile"
```

`obj-y` also works; `obj-$(CONFIG_MACH_TOMTOMGO)` keeps it board-only. Putting the file next to
`mach-homtom-go630.c` in `arch/arm/mach-s3c/` works identically; the relative
`#include "../../../lib/fonts/..."` stays valid because both directories are three levels deep.

## 3. Config

- **Fonts: nothing to enable.** The current `.config` has no `CONFIG_FONT_*` at all:
  `FONT_SUPPORT` is a hidden symbol selected only by console drivers
  (`FRAMEBUFFER_CONSOLE` is off), and `FONT_8x8` additionally `depends on FRAMEBUFFER_CONSOLE || STI_CONSOLE`.
  The file therefore compiles its own copy of `lib/fonts/font_8x8.c` (renamed to
  `homtom_fbtext_font`, 2 KB) when `CONFIG_FONT_8x8` is not built in, and uses the shared
  `font_vga_8x8` when it is. No duplicate symbol in either case.
- **No `console=` needed.** The console is registered with `CON_ENABLED` preset, so
  `try_enable_preferred_console(newcon, false)` accepts it even if a `console=ttySAC0` is ever added.
  (6.1 has no `CON_PRINTK`; replay of the existing log is `CON_PRINTBUFFER`, which is set.)
- **Required cmdline change.** `CONFIG_CMDLINE` (forced) contains `loglevel=0 quiet`.
  With console_loglevel 0, `suppress_message_printing()` drops *every* record, so the boot log
  would never appear. Oops/panic text still would (`oops_begin()` and `panic()` call
  `console_verbose()`). Replace `loglevel=0 quiet` with `ignore_loglevel` (or `loglevel=8`) in
  `kernel/homtom/homtom_defconfig`. Add `initcall_debug` to get initcall names as text in place
  of the index squares.

## 4. Layout and the existing debug drawings

The console defaults to the whole screen (60x34 at 8x8). Existing code that draws into it:

| Helper | Pixels | Effect on console |
|---|---|---|
| `homtom_ic_show` (init/main.c) | y=104..175, every initcall | overwrites rows 13-21 constantly |
| `homtom_dbg_hb` (scheduler_tick) | y=4..72 + square x=440..471 y=10..41 | overwrites rows 0-9 every 4 ticks |
| `homtom_dbg_stage` | y=220..234 | rows 27-29 |
| `homtom_dbg_dump` (die, fault) | full-screen black + hex | wipes the console just before the oops text is printed |
| `homtom_dbg_panic` | x=356..479, y=248..271 | bottom-right of rows 31-33 |

**Recommendation:** console owns the full screen; remove `homtom_ic_show`, `homtom_dbg_hb`,
`homtom_dbg_stage` drawing and the `homtom_dbg_dump`/`homtom_dbg_panic` calls (or make them
no-ops when `homtom_fbtext_init` succeeded). Their information is printed as text anyway:
initcalls via `initcall_debug`, stages via a `pr_emerg()`, the oops register dump by `__die()`.
Keep the heartbeat only if you need it while the console is silent: move it to a 4x4 block
inside x=476..479 and accept that it overprints the last pixel column of column 59.

Alternative if you want to keep the old drawings for one more run: build with
`-DHOMTOM_FBTEXT_Y0=184` (console in y=184..271, 11 rows at 8x8) and drop only
`homtom_dbg_stage` and `homtom_dbg_panic`, which sit inside that band. `homtom_dbg_dump`
still wipes the whole screen at oops time.

## 5. Knobs (compile-time `-D` or edit the defaults)

- `HOMTOM_FBTEXT_FONT_H` 8 (default, 60x34) or 16 (60x17).
- `HOMTOM_FBTEXT_Y0` first pixel row the console owns (default 0).
- `HOMTOM_FBTEXT_FREEZE_ON_OOPS` 1 (default): on the first write with `oops_in_progress`
  set, keep the last 2 lines, clear the rest and stop drawing when full, so the oops head
  stays on screen. Later messages, including the panic line, are dropped.
