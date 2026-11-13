"""Run the bootloader's own image lookup and draw code (0x30b06844 -> 0x30b0ac1c -> 0x30b067d8) under
Unicorn on a candidate image table, for every image of every group-16 scene, and compare each plotted
pixel with an independent Python decode (firmware/norimage.py).

Runs inside a container with unicorn 2.0.1 (see Dockerfile.emu). Usage:
  python3 emu-draw.py NOR BOOTLOADER_OUT TABLE_BIN|stock [TABLE_ADDR]

Memory model: the gunzipped bootloader at 0x30b00000, its .data copied to 0x30008000 as the C runtime
does (0x30b1a5f8), the NOR at 0. For a candidate table the harness does what the TTBL records do: table
at TABLE_ADDR, palette at 0x30009400, pointer 0x30008430 = TABLE_ADDR + 0x201. The plot hook stands in
for the function pointer at 0x30009398 (r0 = x, r1 = y, r2 = RGB565).
"""
import struct
import sys

from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_HOOK_MEM_WRITE, UC_MODE_ARM, Uc, UcError
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_SP

sys.path.insert(0, "/repo/firmware")
import norimage  # noqa: E402

BL_BASE, DATA_SRC, DATA_DST, DATA_LEN = 0x30B00000, 0x30B353A4, 0x30008000, 0xA28
DRAW, POINTER, PALETTE, PLOTFN, DECODE_BUF = 0x30B06844, 0x30008430, 0x30009400, 0x30009398, 0x30600000
STUB_PLOT, STUB_RET = 0x33F00000, 0x33F00010
SCENE_BASE, SCENE_SIZE = 0x28D94, 0x140
MAX_INSNS = 60_000_000


def scenes(bl):
    out = []
    for s in range(140):
        off = SCENE_BASE + s * SCENE_SIZE
        group, mode = struct.unpack_from("<II", bl, off)
        if group != 16:
            continue
        for k in range(26):
            i, x, y, _ = struct.unpack_from("<Ihhi", bl, off + 8 + k * 12)
            if i in (0xFFFFFFFF, 0xFFFFFFFE):
                break
            out.append((mode, i, x, y))
    return out + [(-1, 0, -1, -1), (-1, 230, 337, 8), (-1, 263, 443, 8)]


def main(nor_path, bl_path, table_arg, addr_arg=None):
    nor = open(nor_path, "rb").read()
    bl = open(bl_path, "rb").read()
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(0, 0x80000)
    uc.mem_write(0, nor)
    uc.mem_map(0x30000000, 0x04000000)
    uc.mem_write(BL_BASE, bl)
    uc.mem_write(DATA_DST, bl[DATA_SRC - BL_BASE:DATA_SRC - BL_BASE + DATA_LEN])
    if table_arg == "stock":
        table = nor[norimage.PALETTE_OFFSET:]
        uc.mem_write(PALETTE, nor[0x28000:0x28200])
        label = "stock NOR table (pointer 0x28201)"
    else:
        table = open(table_arg, "rb").read()
        addr = int(addr_arg, 0) if addr_arg else 0x30F00000
        uc.mem_write(addr, table)
        uc.mem_write(PALETTE, table[:512])
        uc.mem_write(POINTER, struct.pack("<I", addr + 0x201))
        label = "%s at 0x%08x" % (table_arg.split("/")[-1], addr)
    uc.mem_write(STUB_PLOT, struct.pack("<I", 0xE12FFF1E))  # bx lr
    uc.mem_write(0x3000811C + 4, struct.pack("<II", 480, 272))  # LCD size read by 0x30b05f9c/0x30b05fac for centring
    uc.mem_write(PLOTFN, struct.pack("<I", STUB_PLOT))
    pal565 = struct.unpack("<256H", bytes(uc.mem_read(PALETTE, 512)))
    try:
        ref = norimage.parse(b"\xff" * norimage.PALETTE_OFFSET + table[:len(table)] + b"\0" * 16)
        refimg = {i["id"]: i for i in ref["images"]}
    except ValueError as e:
        print("reference decode refuses this table (%s); reporting emulator behaviour only" % e)
        refimg = None

    state = dict(plots={}, insns=0, stray=[])

    def on_code(u, address, size, _):
        state["insns"] += 1
        if address == STUB_PLOT:
            x, y, c = u.reg_read(UC_ARM_REG_R0), u.reg_read(UC_ARM_REG_R1), u.reg_read(UC_ARM_REG_R2)
            state["plots"][(x, y)] = c
        elif address == STUB_RET or state["insns"] > MAX_INSNS:
            u.emu_stop()

    allowed = [(DECODE_BUF, DECODE_BUF + 480 * 272), (0x303F0000, 0x303F6000)]

    def on_write(u, access, address, size, value, _):
        if not any(a <= address < b for a, b in allowed):
            state["stray"].append(address)

    uc.hook_add(UC_HOOK_CODE, on_code)
    uc.hook_add(UC_HOOK_MEM_WRITE, on_write)

    seen, bad, total_px = set(), [], 0
    for mode, ident, x, y in scenes(bl):
        if ident in seen:
            continue
        seen.add(ident)
        state.update(plots={}, insns=0, stray=[])
        uc.reg_write(UC_ARM_REG_SP, 0x303F5800)
        uc.reg_write(UC_ARM_REG_LR, STUB_RET)
        uc.reg_write(UC_ARM_REG_R0, ident)
        uc.reg_write(UC_ARM_REG_R1, x)
        uc.reg_write(UC_ARM_REG_R2, y)
        try:
            uc.emu_start(DRAW, STUB_RET)
        except UcError as e:
            bad.append("id %d: CPU fault %s at pc 0x%08x" % (ident, e, uc.reg_read(UC_ARM_REG_PC)))
            continue
        if state["insns"] > MAX_INSNS:
            bad.append("id %d: no return after %d instructions (hang)" % (ident, MAX_INSNS))
            continue
        if refimg is None:
            print("id %3d: returned after %d insns, %d plots, %d stray writes" % (ident, state["insns"], len(state["plots"]), len(state["stray"])))
            if state["stray"]:
                bad.append("id %d: %d stray writes, first 0x%08x" % (ident, len(state["stray"]), state["stray"][0]))
            continue
        if ident not in refimg:
            if state["plots"]:
                bad.append("id %d: absent from table but plotted" % ident)
            continue
        im = refimg[ident]
        w, h = im["width"], im["height"]
        if x < 0:  # centred draw, as 0x30b06894-0x30b068b0 computes it
            x = ((479 - w) + (1 if 479 - w < 0 else 0)) >> 1
            y = ((271 - h) + (1 if 271 - h < 0 else 0)) >> 1
        mism = 0
        for yy in range(h):
            for xx in range(w):
                want = pal565[im["pixels"][yy * w + xx]]
                if state["plots"].get((x + xx, y + yy)) != want:
                    mism += 1
        total_px += w * h
        extra = len(state["plots"]) - w * h
        if mism or extra or state["stray"]:
            bad.append("id %d: %d pixel mismatches, %d extra plots, %d stray writes" % (ident, mism, extra, len(state["stray"])))
        print("id %3d %3dx%-3d at (%3d,%3d): %6d insns, %6d pixels, %s" % (
            ident, w, h, x, y, state["insns"], w * h, "match" if not mism and not extra and not state["stray"] else "MISMATCH"))
    print("%s: %d ids drawn, %d pixels compared, %s" % (label, len(seen), total_px, "ALL MATCH" if not bad else "PROBLEMS"))
    for b in bad:
        print("  " + b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
