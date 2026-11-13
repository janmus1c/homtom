"""Build the stage-0 and stage-1 image tables (stock palette) from the current NOR dump.

stage 0: the NOR table 0x28000 up to and including the end marker, byte for byte (R1 black id 0 included).
stage 1: the same with id 263 (9x12, the "6" the bootloader draws over "GO 730" at (443,8) on every
         storage-mode frame) replaced by a solid block in the stock palette entry nearest homtom blue.
Usage: python3 stages.py NOR OUT_DIR
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "firmware"))
import norimage  # noqa: E402


def main(nor_path, out_dir):
    nor = Path(nor_path).read_bytes()
    info = norimage.parse(nor)
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    stock = nor[norimage.PALETTE_OFFSET:info["end"] + 8]
    for i in info["images"]:
        if i["id"] != 0:
            assert norimage.encode_rle(i["pixels"]) == i["blob"], i["id"]
    (out / "table-stage0.bin").write_bytes(stock)

    target = (0x18, 0xBC, 0xF2)
    blue = min(range(1, 256), key=lambda k: sum((a - b) ** 2 for a, b in zip(info["palette"][k], target)))
    # Splice: every other record keeps its exact NOR bytes (id 0 is no longer a canonical RLE stream
    # after the R1 black-logo write, so a full re-encode would change it).
    rec = next(i for i in info["images"] if i["id"] == 263)
    start = rec["offset"] - norimage.PALETTE_OFFSET
    end = start + 16 + len(rec["blob"])
    new = norimage.encode_record(263, rec["width"], rec["height"], bytes([blue]) * (rec["width"] * rec["height"]))
    s1 = stock[:start] + new + stock[end:]
    check = norimage.parse(b"\xff" * norimage.PALETTE_OFFSET + s1 + b"\xff" * 8)
    assert [i["id"] for i in check["images"]] == [i["id"] for i in info["images"]]
    (out / "table-stage1.bin").write_bytes(s1)
    print("stage0 %d bytes, stage1 %d bytes, id 263 colour index %d = %s" % (len(stock), len(s1), blue, info["palette"][blue]))


if __name__ == "__main__":
    main(*sys.argv[1:3])
