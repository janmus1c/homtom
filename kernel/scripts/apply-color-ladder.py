#!/usr/bin/env python3
"""Inject Homtom boot color checkpoints into arch/arm/boot/compressed/head.S."""
from __future__ import annotations

import re
import sys
from pathlib import Path


def wrap(tag: str, color: str) -> str:
    return (
        f"\t/* homtom_cp_{tag}: color checkpoint */\n"
        f"\tHOMTOM_SOLID\t{color}, {tag}\n"
        f"\t/* end homtom_cp_{tag} */\n"
    )


def strip_prior(text: str) -> str:
    patterns = [
        r"\n\t/\* homtom_early_cyan:.*?\n\t/\* end homtom_early_cyan \*/\n",
        r"\n\t/\* homtom_early_magenta:.*?\n\t/\* end homtom_early_magenta \*/\n",
        r"\n\t/\* begin homtom_color_ladder \*/.*?\n\t/\* end homtom_color_ladder_macro \*/\n",
        r"\n\t/\* homtom_cp_\w+:.*?\n\t/\* end homtom_cp_\w+ \*/\n",
    ]
    for pat in patterns:
        text = re.sub(pat, "\n", text, flags=re.S)
    return text


def insert_after(lines: list[str], pred, block: str) -> list[str]:
    out: list[str] = []
    done = False
    for line in lines:
        out.append(line)
        if not done and pred(line):
            out.append(block)
            done = True
    if not done:
        raise SystemExit(f"insert_after failed for block:\n{block[:80]}")
    return out


def insert_before(lines: list[str], pred, block: str) -> list[str]:
    out: list[str] = []
    done = False
    for line in lines:
        if not done and pred(line):
            out.append(block)
            done = True
        out.append(line)
    if not done:
        raise SystemExit(f"insert_before failed for block:\n{block[:80]}")
    return out


def main() -> None:
    head = Path(sys.argv[1])
    macro_path = Path(sys.argv[2]) if len(sys.argv) > 2 else head.parent / "homtom-solid-macro.S"
    text = strip_prior(head.read_text())
    lines = text.splitlines(True)

    macro_body = macro_path.read_text()
    # Drop the leading C-style file banner; keep .macro definition.
    if ".macro" in macro_body:
        macro_body = macro_body[macro_body.index(".macro") :]
    macro = (
        "\t/* begin homtom_color_ladder */\n"
        + macro_body
        + ("\n" if not macro_body.endswith("\n") else "")
        + "\t/* end homtom_color_ladder_macro */\n"
    )
    lines = insert_after(lines, lambda l: "efi-header.S" in l, macro)

    # 1 cyan — zImage entry after spsr save
    lines = insert_after(
        lines,
        lambda l: "spsr_cxsf, r9" in l and "Save the CPU boot mode" in l,
        wrap("1_cyan", "0x18BCF2"),
    )

    # 2 blue — restart: path after add sp,sp,r0 / add r6,r6,r0
    out: list[str] = []
    done = False
    i = 0
    while i < len(lines):
        line = lines[i]
        out.append(line)
        if not done and line.lstrip().startswith("restart:"):
            i += 1
            while i < len(lines):
                out.append(lines[i])
                if re.match(r"add\s+r6,\s*r6,\s*r0\s*$", lines[i].strip()):
                    out.append(wrap("2_blue", "0x0000FF"))
                    done = True
                    break
                i += 1
        i += 1
    if not done:
        raise SystemExit("checkpoint 2_blue (restart stack) not found")
    lines = out

    def is_bl(line: str, target: str) -> bool:
        return re.match(rf"bl\s+{re.escape(target)}\s*$", line.strip()) is not None

    # 3 green — before decompress *argument setup* (must not clobber r0–r3
    # after mov r0,r4 / mov r1,sp / … / mov r3,r7).
    lines = insert_before(
        lines,
        lambda l: re.match(r"mov\s+r0,\s*r4\s*$", l.strip()) is not None
        and True,
        wrap("3_green", "0x00FF00"),
    )
    # The first mov r0,r4 in the file may be unrelated — prefer the one
    # immediately followed (within a few lines) by bl decompress_kernel.
    text = "".join(lines)
    # Drop every 3_green, then reinsert at the correct mov r0,r4.
    text = re.sub(
        r"\n\t/\* homtom_cp_3_green:.*?\n\t/\* end homtom_cp_3_green \*/\n",
        "\n",
        text,
        flags=re.S,
    )
    lines = text.splitlines(True)
    out = []
    done = False
    i = 0
    while i < len(lines):
        if (
            not done
            and re.match(r"mov\s+r0,\s*r4\s*$", lines[i].strip())
        ):
            window = "".join(lines[i : i + 8])
            if re.search(r"bl\s+decompress_kernel", window):
                out.append(wrap("3_green", "0x00FF00"))
                done = True
        out.append(lines[i])
        i += 1
    if not done:
        raise SystemExit("checkpoint 3_green (pre-decompress mov r0,r4) not found")
    lines = out
    # 4 yellow — after decompress
    lines = insert_after(
        lines,
        lambda l: is_bl(l, "decompress_kernel"),
        wrap("4_yellow", "0xFFFF00"),
    )

    # 5 magenta — after the cache_off that follows decompress
    out = []
    seen_decomp = False
    done = False
    for line in lines:
        out.append(line)
        if is_bl(line, "decompress_kernel"):
            seen_decomp = True
        if seen_decomp and not done and is_bl(line, "cache_off"):
            out.append(wrap("5_magenta", "0xFF00FF"))
            done = True
    if not done:
        raise SystemExit("checkpoint 5_magenta not found")
    lines = out

    # 6 red — enter kernel
    lines = insert_after(
        lines,
        lambda l: l.strip() == "__enter_kernel:",
        wrap("6_red", "0xFF0000"),
    )

    head.write_text("".join(lines))
    print(f"color ladder applied to {head}")


if __name__ == "__main__":
    main()
