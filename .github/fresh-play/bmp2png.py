"""The frames of fresh-play.yml's plays as png, with python's standard library only (the same on every runner).

    python3 -I bmp2png.py IN OUT

Converts every IN/NAME/*.bmp (SDL_SaveBMP's 24 or 32 bit pictures, uncompressed or with bit fields) into
OUT/NAME/*.png and prints one line per folder. Exit 1 when a picture could not be converted (the others still are).
"""

import pathlib
import struct
import sys
import zlib


def read_bmp(data):
    if data[:2] != b"BM":
        raise ValueError("not a bmp")
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height, _planes, bits, compression = struct.unpack_from("<iiHHI", data, 18)
    if bits not in (24, 32) or compression not in (0, 3):
        raise ValueError(f"{bits} bits, compression {compression}")
    masks = (0x00FF0000, 0x0000FF00, 0x000000FF)
    if compression == 3:   # the masks follow the 40 byte header, or are in a longer one at the same place
        masks = struct.unpack_from("<III", data, 14 + 40)
    flip = height > 0
    height = abs(height)
    step = bits // 8
    pitch = (width * step + 3) & ~3
    shifts = [(m & -m).bit_length() - 1 for m in masks]
    rows = []
    for y in range(height):
        src = offset + pitch * ((height - 1 - y) if flip else y)
        line = data[src:src + width * step]
        if bits == 24 and compression == 0:
            out = bytearray(width * 3)
            out[0::3] = line[2::3]
            out[1::3] = line[1::3]
            out[2::3] = line[0::3]
        else:
            out = bytearray(width * 3)
            for x in range(width):
                v = int.from_bytes(line[x * step:x * step + step], "little")
                for c in range(3):
                    out[x * 3 + c] = ((v & masks[c]) >> shifts[c]) & 0xFF
        rows.append(bytes(out))
    return width, height, rows


def write_png(path, width, height, rows):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + row for row in rows)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def main():
    src, dst = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    bad = 0
    for folder in sorted(p for p in src.iterdir() if p.is_dir()):
        done = 0
        for bmp in sorted(folder.glob("*.bmp")):
            out = dst / folder.name / (bmp.stem + ".png")
            out.parent.mkdir(parents=True, exist_ok=True)
            try:
                write_png(out, *read_bmp(bmp.read_bytes()))
                done += 1
            except (ValueError, struct.error) as e:
                print(f"{bmp}: {e}")
                bad += 1
        print(f"{folder.name}: {done} frames as png")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
