"""TTBL ttsystem container and newc cpio helpers for homtom builds."""
import gzip
import struct

MAGIC = b"TTBL"


def sections(image):
    if image[:4] != MAGIC:
        raise ValueError("not a TTBL image")
    out, offset = [], 4
    while True:
        length, address = struct.unpack_from("<II", image, offset)
        if length == 0:
            return out, address
        out.append((address, image[offset + 8:offset + 8 + length]))
        offset += 24 + length


def read_cpio(data):
    entries, offset = [], 0
    while True:
        header = data[offset:offset + 110]
        if header[:6] != b"070701":
            raise ValueError("bad cpio magic at %d" % offset)
        fields = [int(header[6 + 8 * i:14 + 8 * i], 16) for i in range(13)]
        ino, mode, uid, gid, nlink, mtime, size, dmaj, dmin, rmaj, rmin, namesize, _ = fields
        name_start = offset + 110
        name = data[name_start:name_start + namesize - 1].decode()
        body_start = (name_start + namesize + 3) & ~3
        body = data[body_start:body_start + size]
        offset = (body_start + size + 3) & ~3
        if name == "TRAILER!!!":
            return entries
        entries.append(dict(name=name, mode=mode, uid=uid, gid=gid, mtime=mtime,
                            rmaj=rmaj, rmin=rmin, data=body))


def write_cpio(entries):
    out = bytearray()

    def add(ino, e):
        name = e["name"].encode() + b"\0"
        fields = [ino, e["mode"], e.get("uid", 0), e.get("gid", 0), 1, e.get("mtime", 0),
                  len(e["data"]), 0, 0, e.get("rmaj", 0), e.get("rmin", 0), len(name), 0]
        out.extend(b"070701" + b"".join(b"%08X" % f for f in fields) + name)
        out.extend(b"\0" * (-len(out) % 4))
        out.extend(e["data"])
        out.extend(b"\0" * (-len(out) % 4))

    for ino, e in enumerate(entries, 300000):
        add(ino, e)
    add(0, dict(name="TRAILER!!!", mode=0, data=b""))
    out.extend(b"\0" * (-len(out) % 512))
    return bytes(out)


def initramfs_entries(image):
    secs, _ = sections(image)
    initramfs = next(data for address, data in secs if address == 0x31000000)
    return read_cpio(gzip.decompress(initramfs))
