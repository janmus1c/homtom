"""Build and run the norwrite harness, then check its outputs with the Python model and the repo's decoder.

Run: python3 run_harness.py [OUTDIR]   (default: a fresh temporary directory). Needs a host C compiler.

Steps:
1. gen_plan.py --check: tools/devtools/norwrite_plan.h equals the generator output.
2. Build `harness` (norwrite.c unchanged, NORWRITE_HOST) and `harness-erase` (adds NORWRITE_ERASE_EXPERIMENT).
3. harness r1: clean run with transcript, rerun, power cuts, kills, DQ5, stuck busy, F0-while-busy,
   preemption, wrong ID, protected sector, foreign images, bit flips, SA0 not protected, single-instance lock
   (live, empty, garbage, stale, own pid, not creatable), clock failure; bus monitor on every write.
4. Replay the clean transcript into a fresh Python model (en29lv400bb.py): every read must match and the final
   image must hash to 87dc81ca...
5. Fuzz transcripts from the C model replayed into the Python model: every read must match.
6. Decode every distinct intermediate image region with firmware/norimage.py.
7. harness-erase: per-sector interrupted-erase images and the R3 cut sweep; classify damage by region.
"""

import hashlib
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MODEL = os.path.dirname(HERE)
ROOT = os.path.abspath(os.path.join(MODEL, "..", ".."))
sys.path.insert(0, MODEL)
sys.path.insert(0, os.path.join(ROOT, "firmware"))

import norimage  # noqa: E402
from en29lv400bb import EN29LV400BB  # noqa: E402

STOCK = os.path.join(ROOT, "artifacts", "nor-analysis", "n1-n2-20261107", "nor-n1-linux61.bin")
HOMTOM = os.path.join(ROOT, "artifacts", "homtom-nor", "mtd0-homtom.bin")
DEVTOOLS = os.path.join(ROOT, "tools", "devtools")
TARGET_SHA = "87dc81ca967579b424e3e48d1e680685549ee0d73d433d2cfd00c5c4611a384e"
REGION_LO, REGION_HI = 0x28000, 0x40000

REGIONS = [  # (name, first byte, last byte)
    ("SA0 vectors + device info", 0x00000, 0x03FFF),
    ("SA1-SA2 (0xFF)", 0x04000, 0x07FFF),
    ("stage 1 code", 0x08000, 0x0B293),
    ("gzip stream", 0x0B294, 0x247DC),
    ("SA5 gap (0xFF)", 0x247DD, 0x27FFF),
    ("palette + records 0/218/219", 0x28000, 0x2FFFF),
    ("records 219-221 + version string", 0x30000, 0x3FFFF),
    ("ngffs SA7 header", 0x40000, 0x4FFFF),
    ("ngffs SA8 settings.dat", 0x50000, 0x5FFFF),
    ("ngffs SA9 header", 0x60000, 0x6FFFF),
    ("ngffs SA10 header", 0x70000, 0x7FFFF),
]

failures = []


def fail(msg):
    failures.append(msg)
    print("FAIL:", msg)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def build(out):
    cc = os.environ.get("CC", "cc")
    common = [cc, "-O2", "-Wall", "-Wextra", "-I", DEVTOOLS, os.path.join(HERE, "harness.c"),
              os.path.join(HERE, "nor_model.c")]
    subprocess.run(common + ["-o", os.path.join(out, "harness")], check=True)
    subprocess.run(common + ["-DNORWRITE_ERASE_EXPERIMENT", "-DNW_ERASE_SECTORS=0,1,2,3,4,5,6,7,8,9,10",
                             "-o", os.path.join(out, "harness-erase")], check=True)


def replay(path, img, label):
    """Replay a transcript into a fresh Python model; return the model. Fails on the first read mismatch."""
    params = {}
    with open(path) as f:
        first = f.readline()
        if "erase_us=" in first:
            params["erase_us"] = float(first.split("erase_us=")[1].split()[0])
        c = EN29LV400BB(img, **params)
        reads = writes = 0
        for n, line in enumerate(f, 2):
            op, a, *rest = line.split()
            if op == "R":
                got = c.read(int(a, 16))
                reads += 1
                if got != int(rest[0], 16):
                    fail("%s: line %d read %s: Python %04x, C %s" % (label, n, a, got, rest[0]))
                    return c, reads, writes
            elif op == "W":
                c.write(int(a, 16), int(rest[0], 16))
                writes += 1
            elif op == "T":
                c.wait(float(a))
    return c, reads, writes


def decode_snapshots(path, stock):
    base = norimage.parse(stock)
    ref = [(im["id"], im["width"], im["height"]) for im in base["images"]]
    size = REGION_HI - REGION_LO
    n = 0
    with open(path, "rb") as f:
        while True:
            region = f.read(size)
            if not region:
                break
            n += 1
            img = stock[:REGION_LO] + region + stock[REGION_HI:]
            try:
                p = norimage.parse(img)
            except Exception as e:  # noqa: BLE001
                fail("snapshot %d does not decode: %s" % (n, e))
                continue
            if [(im["id"], im["width"], im["height"]) for im in p["images"]] != ref or p["end"] != base["end"]:
                fail("snapshot %d: image chain differs" % n)
                continue
            if p["palette565"] != base["palette565"]:
                fail("snapshot %d: palette changed" % n)
            for k, (a, b) in enumerate(zip(p["images"], base["images"])):
                if k == 0:
                    if any(x & ~y & 0xFF for x, y in zip(a["pixels"], b["pixels"])):
                        fail("snapshot %d: record 0 pixel gained bits" % n)
                elif a["pixels"] != b["pixels"]:
                    fail("snapshot %d: record %d changed" % (n, k))
    return n, len(ref)


def classify(img, stock):
    return [name for name, lo, hi in REGIONS if img[lo:hi + 1] != stock[lo:hi + 1]]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else tempfile.mkdtemp(prefix="norwrite-harness-")
    os.makedirs(out, exist_ok=True)
    print("output directory:", out)
    with open(STOCK, "rb") as f:
        stock = f.read()
    with open(HOMTOM, "rb") as f:
        homtom = f.read()

    subprocess.run([sys.executable, os.path.join(MODEL, "gen_plan.py"), "--check"], check=True, cwd=MODEL)
    build(out)

    print("\n== R1 suite (C harness, writer source unchanged) ==")
    r = subprocess.run([os.path.join(out, "harness"), "r1", STOCK, out], capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr[-4000:])
    if r.returncode:
        fail("harness r1 exit %d" % r.returncode)

    print("\n== replay of the clean transcript into the Python model ==")
    c, reads, writes = replay(os.path.join(out, "clean.tr"), stock, "clean replay")
    got = sha(c.image())
    print("replayed %d reads (all matched the C model) and %d writes; Python image sha256 %s" % (reads, writes, got))
    if got != TARGET_SHA:
        fail("replayed image is not the R1 target")
    if 0x80 in c.commands_seen or 0x30 in c.commands_seen:
        fail("replay saw an erase command")
    print("commands seen by the Python model after unlock:", sorted("%02X" % x for x in c.commands_seen))

    print("\n== C model vs Python model, random bus traffic ==")
    total = 0
    for seed in range(1, 9):
        path = os.path.join(out, "fuzz-%d.tr" % seed)
        subprocess.run([os.path.join(out, "harness"), "fuzz", STOCK, path, str(seed), "3000"], check=True,
                       capture_output=True)
        _, reads, writes = replay(path, stock, "fuzz %d" % seed)
        total += reads + writes
    print("8 fuzz transcripts, %d bus ops, reads compared one by one" % total)

    print("\n== repo decoder (firmware/norimage.py) on every distinct intermediate state ==")
    n, nimg = decode_snapshots(os.path.join(out, "snapshots.bin"), stock)
    print("%d distinct region states decoded: %d records each, ids and sizes unchanged, record 0 only lost bits, "
          "records 1-23 and the palette unchanged" % (n, nimg))

    print("\n== R2/R3 erase analysis (host-only NORWRITE_ERASE_EXPERIMENT build) ==")
    r = subprocess.run([os.path.join(out, "harness-erase"), "erase", STOCK, HOMTOM, out], capture_output=True,
                       text=True)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr[-2000:])
    if r.returncode:
        fail("harness erase exit %d" % r.returncode)
    size = 0x80000
    with open(os.path.join(out, "erase-cut.bin"), "rb") as f:
        data = f.read()
    print("sector | cut at 10% / 50% / 90% of typical erase time: damaged regions | uninterrupted")
    for s in range(11):
        imgs = [data[(4 * s + k) * size:(4 * s + k + 1) * size] for k in range(4)]
        cuts = [classify(i, stock) for i in imgs[:3]]
        names = sorted(set(sum(cuts, [])))
        print("SA%-2d | %s | %s" % (s, "; ".join(names) or "none", "; ".join(classify(imgs[3], stock)) or "none"))

    print("\nharness summary: %d failures" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
