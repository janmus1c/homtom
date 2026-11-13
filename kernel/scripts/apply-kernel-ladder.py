#!/usr/bin/env python3
"""Inject Homtom color checkpoints into arch/arm/kernel/head.S (and head-common.S)."""
from __future__ import annotations

import re
import sys
from pathlib import Path


def wrap_pair(tag: str, color_a: str, color_b: str, preserve: str) -> str:
    insn = f"\tHOMTOM_PAIR\t{color_a}, {color_b}, {tag}\n"
    if preserve == "r1":
        body = "\tmov\tr3, r1\n" + insn + "\tmov\tr1, r3\n"
    elif preserve == "r0r1":
        body = (
            "\tmov\tr3, r0\n"
            "\tmov\tip, r1\n"
            + insn
            + "\tmov\tr0, r3\n"
            "\tmov\tr1, ip\n"
        )
    else:
        body = insn
    return (
        f"\t/* homtom_cp_{tag}: color checkpoint */\n"
        f"{body}"
        f"\t/* end homtom_cp_{tag} */\n"
    )


def wrap_solid(tag: str, color: str, preserve: str, *, tpal_only: bool = False) -> str:
    macro = "HOMTOM_SOLID_TPAL" if tpal_only else "HOMTOM_SOLID"
    insn = f"\t{macro}\t{color}, {tag}\n"
    if preserve == "r1":
        body = "\tmov\tr3, r1\n" + insn + "\tmov\tr1, r3\n"
    elif preserve == "r0r1":
        body = (
            "\tmov\tr3, r0\n"
            "\tmov\tip, r1\n"
            + insn
            + "\tmov\tr0, r3\n"
            "\tmov\tr1, ip\n"
        )
    else:
        body = insn
    return (
        f"\t/* homtom_cp_{tag}: color checkpoint */\n"
        f"{body}"
        f"\t/* end homtom_cp_{tag} */\n"
    )


def macro_blob(macro_path: Path) -> str:
    macro_body = macro_path.read_text()
    if ".macro" in macro_body:
        macro_body = macro_body[macro_body.index(".macro") :]
    return (
        "\t/* begin homtom_kernel_ladder */\n"
        + macro_body
        + ("\n" if not macro_body.endswith("\n") else "")
        + "\t/* end homtom_kernel_ladder_macro */\n"
    )


def strip_prior(text: str) -> str:
    text = re.sub(
        r"\n\t/\* begin homtom_kernel_ladder \*/.*?\n\t/\* end homtom_kernel_ladder_macro \*/\n",
        "\n",
        text,
        flags=re.S,
    )
    text = re.sub(
        r"\n\t/\* homtom_cp_6[\w]+:.*?\n\t/\* end homtom_cp_6[\w]+ \*/\n",
        "\n",
        text,
        flags=re.S,
    )
    text = re.sub(
        r"\n\t/\* homtom_wb_drain \*/.*?\n\t/\* end homtom_wb_drain \*/\n",
        "\n",
        text,
        flags=re.S,
    )
    text = re.sub(
        r"\n\t/\* homtom_idmap_lcd \*/.*?\n\t/\* end homtom_idmap_lcd \*/\n",
        "\n",
        text,
        flags=re.S,
    )
    text = re.sub(
        r"\n\t/\* homtom_mmu_probe_pre \*/.*?\n\t/\* end homtom_mmu_probe_pre \*/\n",
        "\n",
        text,
        flags=re.S,
    )
    text = re.sub(
        r"\n\t/\* homtom_mmu_probe_post \*/.*?\n\t/\* end homtom_mmu_probe_post \*/\n",
        "\n",
        text,
        flags=re.S,
    )
    text = re.sub(
        r"\n\tbne\t1f\n\tb\t\.\n1:\n",
        "\n\tbeq\t__error_p\n",
        text,
    )
    text = re.sub(
        r"\n1:\n\tb\t__enable_mmu\n",
        "\n1:\tb\t__enable_mmu\n",
        text,
    )
    return text


IDMAP_LCD = """\
\t/* homtom_idmap_lcd */
\tldr\tr7, [r10, #PROCINFO_IO_MMUFLAGS]
\tmov\tr5, #0x4D000000
\tmov\tr5, r5, lsr #SECTION_SHIFT
\torr\tr3, r7, r5, lsl #SECTION_SHIFT
\torr\tr3, r3, #PMD_SECT_XN
\tstr\tr3, [r4, r5, lsl #PMD_ENTRY_ORDER]
\tmov\tr5, #0x4C000000
\torr\tr5, r5, #0x00800000
\tmov\tr5, r5, lsr #SECTION_SHIFT
\torr\tr3, r7, r5, lsl #SECTION_SHIFT
\torr\tr3, r3, #PMD_SECT_XN
\tstr\tr3, [r4, r5, lsl #PMD_ENTRY_ORDER]
\tmov\tr5, #0x4C000000
\tmov\tr5, r5, lsr #SECTION_SHIFT
\torr\tr3, r7, r5, lsl #SECTION_SHIFT
\torr\tr3, r3, #PMD_SECT_XN
\tstr\tr3, [r4, r5, lsl #PMD_ENTRY_ORDER]
\tmov\tr5, #0x56000000
\tmov\tr5, r5, lsr #SECTION_SHIFT
\torr\tr3, r7, r5, lsl #SECTION_SHIFT
\torr\tr3, r3, #PMD_SECT_XN
\tstr\tr3, [r4, r5, lsl #PMD_ENTRY_ORDER]
\tldr\tr7, [r10, #PROCINFO_MM_MMUFLAGS]
\t/* end homtom_idmap_lcd */
"""

WB_DRAIN = """\
\t/* homtom_wb_drain */
\tmov\tr3, #0
\tmcr\tp15, 0, r3, c7, c10, 4\t@ drain write buffer (ARMv4)
\tmcr\tp15, 0, r3, c8, c7, 0\t@ invalidate I+D TLBs
\t/* end homtom_wb_drain */
"""


def apply_head(head: Path, macro_path: Path) -> None:
    text = strip_prior(head.read_text())
    lines = text.splitlines(True)
    macro = macro_blob(macro_path)

    out: list[str] = []
    done = False
    for line in lines:
        if not done and line.strip() == "ENTRY(stext)":
            out.append(macro)
            done = True
        out.append(line)
    if not done:
        raise SystemExit("ENTRY(stext) not found")
    lines = out

    def after(pred, block: str) -> None:
        nonlocal lines
        out = []
        done = False
        for line in lines:
            out.append(line)
            if not done and pred(line):
                out.append(block)
                done = True
        if not done:
            raise SystemExit(f"insert_after failed: {block[:40]!r}")
        lines = out

    def before(pred, block: str) -> None:
        nonlocal lines
        out = []
        done = False
        for line in lines:
            if not done and pred(line):
                out.append(block)
                done = True
            out.append(line)
        if not done:
            raise SystemExit(f"insert_before failed: {block[:40]!r}")
        lines = out

    after(
        lambda l: bool(re.search(r"safe_svcmode_maskall\s+r9", l)),
        wrap_pair("6a_aqua", "0x00FFFF", "0x00FFFF", "r1"),
    )

    out = []
    done = False
    for line in lines:
        if not done and re.search(r"\bbeq\s+__error_p\b", line):
            out.append("\tbne\t1f\n")
            out.append(wrap_pair("6e_badcpu", "0xFF0000", "0x000000", "none"))
            out.append("\tb\t.\n")
            out.append("1:\n")
            out.append(wrap_pair("6a1_cpuok", "0xFFFFFF", "0x0000FF", "r1"))
            done = True
            continue
        out.append(line)
    if not done:
        raise SystemExit("6e/6a1 insert failed")
    lines = out

    after(
        lambda l: bool(re.search(r"\bbl\s+__vet_atags\b", l)),
        wrap_pair("6a2_atags", "0x00FF00", "0xFF00FF", "r1"),
    )
    after(
        lambda l: bool(re.search(r"\bbl\s+__create_page_tables\b", l)),
        wrap_pair("6a3_pagetbl", "0xFFFF00", "0xFF0000", "r1"),
    )

    out = []
    done = False
    for line in lines:
        if not done and re.match(r"1:\s+b\s+__enable_mmu\b", line):
            out.append("1:\n")
            out.append(wrap_pair("6b_mmu", "0x0000FF", "0xFFFFFF", "r0r1"))
            out.append("\tb\t__enable_mmu\n")
            done = True
            continue
        out.append(line)
    if not done:
        raise SystemExit("6b insert failed")
    lines = out

    after(
        lambda l: bool(re.match(r"__enable_mmu:", l)),
        wrap_pair("6c_enmmu", "0xFF80C0", "0x00FF00", "r0r1"),
    )
    before(
        lambda l: bool(re.search(r"\bb\s+__turn_mmu_on\b", l)),
        wrap_pair("6d_ttbr", "0x00FFFF", "0xFFFF00", "r0r1"),
    )

    # Identity-map LCD/CLK/GPIO so post-MMU TPAL paints work (VA==PA).
    out = []
    done = False
    for i, line in enumerate(lines):
        out.append(line)
        if (
            not done
            and re.search(r"\bblo\s+1b\b", line)
            and any("turn_mmu_on" in x for x in lines[max(0, i - 12) : i])
        ):
            out.append(IDMAP_LCD)
            done = True
    if not done:
        raise SystemExit("homtom_idmap_lcd insert failed")
    lines = out

    # MMU probe: FB lime under TPAL orange → after c1 clear overlays (no spin).
    # Post-MMU TPAL uses LCD identity map (VA==PA) until paging_init.
    # PROBE_PRE (FB lime fill) disabled: the bootloader FB lives at phys 0x30400000, inside the
    # decompressed 6.1 kernel image; the fill overwrote kernel text (see COLOR_LADDER.txt).
    PROBE_PRE = (
        "\t/* homtom_mmu_probe_pre */\n"
        "\t/* end homtom_mmu_probe_pre */\n"
    )
    # PROBE_POST (HOMTOM_PROBE_POST 6g_reveal) removed: it sat right after the MMU-enable mcr and clobbers r0/r1;
    # r0 (the CP15 control value) becomes cr_alignment in __mmap_switched, and alignment_trap rewrites c1 from it
    # on every user-mode exception entry => MMU switched off at the first user exception. See COLOR_LADDER.txt.
    PROBE_POST = (
        "\t/* homtom_mmu_probe_post */\n"
        "\t/* end homtom_mmu_probe_post */\n"
    )
    out = []
    done = False
    for line in lines:
        if not done and re.search(
            r"mcr\s+p15,\s*0,\s*r0,\s*c1,\s*c0,\s*0", line
        ):
            out.append(PROBE_PRE)
            out.append(WB_DRAIN)
            out.append(line)
            out.append(PROBE_POST)
            done = True
            continue
        out.append(line)
    if not done:
        raise SystemExit("mmu probe insert failed")

    head.write_text("".join(out))
    print(f"kernel color ladder applied to {head}")


def apply_mmap(common: Path, macro_path: Path) -> None:
    # head.S #includes head-common.S — macros are already defined there.
    del macro_path  # unused; keep argv compatible
    text = strip_prior(common.read_text())
    lines = text.splitlines(True)

    out: list[str] = []
    done = False
    for line in lines:
        out.append(line)
        if not done and re.match(r"__mmap_switched:", line):
            # r0=ctrl must survive for later save into r10.
            # Teal via LCD idmap (VA==PA) — proves virtual fetch of this code.
            # TPAL-only: do not re-arm TFT WIN overlay after MMU probe cleared it.
            out.append(wrap_solid("6h_mmap", "0x008080", "r0r1", tpal_only=True))
            done = True
    if not done:
        raise SystemExit("6h_mmap insert failed")
    lines = out

    # 6i — after bss/current/atags save, before start_kernel.
    # BLUE #0000FF — build id (sky #00BFFF was shared by skyred+mapfix).
    out = []
    done = False
    for line in lines:
        if not done and re.search(r"\bb\s+start_kernel\b", line):
            out.append(wrap_solid("6i_setup", "0x0000FF", "none", tpal_only=True))
            done = True
        out.append(line)
    if not done:
        raise SystemExit("6i_setup insert failed")

    common.write_text("".join(out))
    print(f"kernel color ladder applied to {common}")


def main() -> None:
    head = Path(sys.argv[1])
    macro_path = Path(sys.argv[2])
    apply_head(head, macro_path)
    if len(sys.argv) > 3:
        apply_mmap(Path(sys.argv[3]), macro_path)


if __name__ == "__main__":
    main()
