#!/usr/bin/env python3
"""Extract Monuments of Mars graphics to PNG: the .IGS tile set, the level maps built into the
EXE, and the .CF1 / .CF2 screens (PCX files).

Usage:
  python extract_gfx.py tiles MARSn.IGS OUTDIR [--palette 0|1] [--bright]
  python extract_gfx.py levels UNPACKED.EXE MARSn.IGS OUTDIR [--palette 0|1] [--bright]
  python extract_gfx.py pcx FILE.CF1 OUT.png [--palette 0|1] [--bright]

Tile format (.IGS, read whole by LoadIGS into DS:0766; tile n is at n * 0x80):
  Each tile is 16x16 pixels, 2 bits per pixel, MSB = leftmost pixel, 128 bytes:
    bytes 0x00-0x3F: the 8 even scanlines (0, 2, ..., 14)   -> CGA bank 0 (B800:0000)
    bytes 0x40-0x7F: the 8 odd scanlines  (1, 3, ..., 15)   -> CGA bank 1 (B800:2000)
  Each scanline is 8 bytes, alternating mask and pixel bytes: m0 p0 m1 p1 m2 p2 m3 p3.
  The masked blitters (e.g. 1054:1c3f) draw dst = (dst & m) | p; the opaque ones
  (e.g. 1054:18a4) copy only the p bytes.

Levels: 20 per volume, 12 rows x 20 columns, one byte per cell, row-major, in their own segment
at offset 0x25 (2226:0025 in MARS1). DrawLevel (1054:31f3) draws byte b as tile b, except the
entity codes in ENTITY_CODES, which are spawn points rather than tiles.

Needs only the standard library; PNGs are written as 8-bit indexed images.
"""

import argparse
import os
import re
import struct
import sys
import zlib

TILE_BYTES = 0x80
LEVELS = 20
ROWS, COLS = 12, 20

# Map bytes that DrawLevel turns into entities instead of drawing (see docs/notes.md).
# 0x65 is the player's start.
ENTITY_CODES = {0x04, 0x06, 0x46, 0x49, 0x4A, 0x4B, 0x55, 0x65, 0x6E}

# CGA palettes, indexed [palette][bright]. Gameplay uses palette 0, low intensity: SetVideoMode
# (1054:0a36) does INT 10h AH=0Bh with BH=0 BL=0, then BH=1 BL=0. The other setter (1054:0aa7)
# selects palette 1 bright, used for the CF2 screen.
PALETTES = {
    (0, False): [(0, 0, 0), (0x00, 0xAA, 0x00), (0xAA, 0x00, 0x00), (0xAA, 0x55, 0x00)],
    (0, True): [(0, 0, 0), (0x55, 0xFF, 0x55), (0xFF, 0x55, 0x55), (0xFF, 0xFF, 0x55)],
    (1, False): [(0, 0, 0), (0x00, 0xAA, 0xAA), (0xAA, 0x00, 0xAA), (0xAA, 0xAA, 0xAA)],
    (1, True): [(0, 0, 0), (0x55, 0xFF, 0xFF), (0xFF, 0x55, 0xFF), (0xFF, 0xFF, 0xFF)],
}
GAP = 4  # palette index for grid lines in contact sheets
GAP_RGB = (0x30, 0x30, 0x30)
SEE_THROUGH = 5  # palette index for masked-out (transparent) pixels in tile sheets
SEE_THROUGH_RGB = (0x40, 0x40, 0x70)
ENTITY = 6  # palette index for entity-marker outlines in level renders
ENTITY_RGB = (0xFF, 0xFF, 0xFF)


def write_png(path, pixels, width, height, palette):
    """pixels: list of rows, each a bytes-like of palette indices."""
    raw = b"".join(b"\x00" + bytes(row) for row in pixels)

    def chunk(kind, data):
        c = kind + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)))
        f.write(chunk(b"PLTE", b"".join(bytes(c) for c in palette)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def scale(pixels, n):
    out = []
    for row in pixels:
        wide = bytes(p for p in row for _ in range(n))
        out.extend([wide] * n)
    return out


def unpack_2bpp(data, width, height):
    stride = (width * 2 + 7) // 8
    rows = []
    for y in range(height):
        line = data[y * stride:(y + 1) * stride]
        rows.append(bytes((line[x // 4] >> (6 - 2 * (x % 4))) & 3 for x in range(width)))
    return rows


# --- Tiles ----------------------------------------------------------------------------------

def decode_tile(t):
    """Return (pixels, see_through): 16 rows of 16 colour indices, and 16 rows of booleans
    that are True where the mask keeps the background (mask bits 11)."""
    pixels, see = [None] * 16, [None] * 16
    for bank in range(2):
        for r in range(8):
            line = t[bank * 0x40 + r * 8:bank * 0x40 + r * 8 + 8]
            mask, pix = line[0::2], line[1::2]
            y = r * 2 + bank
            pixels[y] = unpack_2bpp(pix, 16, 1)[0]
            see[y] = [m == 3 for m in unpack_2bpp(mask, 16, 1)[0]]
    return pixels, see


def load_tiles(igs_path):
    with open(igs_path, "rb") as f:
        data = f.read()
    n = len(data) // TILE_BYTES
    tiles = [decode_tile(data[i * TILE_BYTES:(i + 1) * TILE_BYTES]) for i in range(n)]
    return tiles, len(data) - n * TILE_BYTES


def extract_tiles(igs_path, outdir, palette, zoom):
    tiles, extra = load_tiles(igs_path)
    os.makedirs(outdir, exist_ok=True)
    per_row = 16
    cell = 17
    rows_n = (len(tiles) + per_row - 1) // per_row
    w, h = per_row * cell + 1, rows_n * cell + 1
    sheet = [bytearray([GAP] * w) for _ in range(h)]
    for i, (pix, see) in enumerate(tiles):
        x0, y0 = 1 + (i % per_row) * cell, 1 + (i // per_row) * cell
        for y in range(16):
            sheet[y0 + y][x0:x0 + 16] = bytes(SEE_THROUGH if s else p for p, s in zip(pix[y], see[y]))
    pal = palette + [GAP_RGB, SEE_THROUGH_RGB]
    path = os.path.join(outdir, "tiles_sheet.png")
    write_png(path, scale(sheet, zoom), w * zoom, h * zoom, pal)
    print(f"{igs_path}: {len(tiles)} tiles (00-{len(tiles) - 1:02X}), {extra} bytes left over")
    print(f"  wrote {path} (16 per row; blue-grey = see-through)")


# --- Levels ---------------------------------------------------------------------------------

# DrawLevel reads the map with: mov ax,LEVELSEG / mov es,ax / mov al,es:[bx+0025h]
LEVEL_READ = re.compile(rb"\xb8(..)\x8e\xc0\x26\x8a\x87\x25\x00", re.S)


def load_levels(exe_path):
    with open(exe_path, "rb") as f:
        d = f.read()
    if d[:2] != b"MZ" or d[0x1C:0x1E] == b"LZ":
        sys.exit(f"{exe_path}: expected an unpacked MZ executable (run tools/unlzexe.py first)")
    image = d[struct.unpack_from("<H", d, 8)[0] * 16:]
    segs = {struct.unpack("<H", m.group(1))[0] for m in LEVEL_READ.finditer(image)}
    if len(segs) != 1:
        sys.exit(f"{exe_path}: could not find a unique level segment (found {sorted(map(hex, segs))})")
    seg = segs.pop()
    base = seg * 16 + 0x25
    maps = [image[base + i * ROWS * COLS:base + (i + 1) * ROWS * COLS] for i in range(LEVELS)]
    return maps, seg


def render_level(m, tiles, entities):
    rows = [bytearray(COLS * 16) for _ in range(ROWS * 16)]
    for r in range(ROWS):
        for c in range(COLS):
            b = m[r * COLS + c]
            if b == 0 or b >= len(tiles):
                continue
            pix, _ = tiles[b]
            for y in range(16):
                rows[r * 16 + y][c * 16:c * 16 + 16] = pix[y]
            if entities and b in ENTITY_CODES:
                for i in range(16):  # outline the cell so spawn points stand out
                    rows[r * 16][c * 16 + i] = rows[r * 16 + 15][c * 16 + i] = ENTITY
                    rows[r * 16 + i][c * 16] = rows[r * 16 + i][c * 16 + 15] = ENTITY
    return rows


def extract_levels(exe_path, igs_path, outdir, palette, zoom, entities):
    maps, seg = load_levels(exe_path)
    tiles, _ = load_tiles(igs_path)
    os.makedirs(outdir, exist_ok=True)
    pal = palette + [GAP_RGB, SEE_THROUGH_RGB, ENTITY_RGB]
    for i, m in enumerate(maps):
        rows = render_level(m, tiles, entities)
        path = os.path.join(outdir, f"level{i + 1:02d}.png")
        write_png(path, scale(rows, zoom), COLS * 16 * zoom, ROWS * 16 * zoom, pal)
        with open(os.path.join(outdir, f"level{i + 1:02d}.txt"), "w") as f:
            for r in range(ROWS):
                f.write(" ".join(".." if b == 0 else f"{b:02X}" for b in m[r * COLS:(r + 1) * COLS]) + "\n")
    used = sorted({b for m in maps for b in m if b})
    print(f"{exe_path}: level segment {seg:04X} (Ghidra {seg + 0x1000:04X}):0025, {LEVELS} levels")
    print(f"  tile codes used: {' '.join(f'{b:02X}' for b in used)}")
    print(f"  wrote {outdir}/level01..{LEVELS:02d}.png and .txt")


# --- PCX ------------------------------------------------------------------------------------

def decode_pcx(data):
    (manuf, ver, enc, bpp, x0, y0, x1, y1) = struct.unpack_from("<4B4H", data, 0)
    planes, bpl = data[65], struct.unpack_from("<H", data, 66)[0]
    if manuf != 0x0A or enc != 1:
        raise ValueError("not an RLE PCX file")
    if bpp != 2 or planes != 1:
        raise ValueError(f"unsupported PCX layout: {bpp} bpp x {planes} planes")
    w, h = x1 - x0 + 1, y1 - y0 + 1
    out = bytearray()
    pos = 128
    need = bpl * h
    while len(out) < need and pos < len(data):
        b = data[pos]
        pos += 1
        if b >= 0xC0:
            out += bytes([data[pos]]) * (b & 0x3F)
            pos += 1
        else:
            out.append(b)
    if len(out) < need:
        raise ValueError(f"PCX data ended early ({len(out)} of {need} bytes)")
    rows = [unpack_2bpp(out[y * bpl:(y + 1) * bpl], w, 1)[0] for y in range(h)]
    # CGA palette info (ZSoft): byte 19 bit 6 = palette, bit 5 = intensity.
    pal_byte = data[19]
    info = {"version": ver, "size": f"{w}x{h}", "bytes_per_line": bpl,
            "cga_palette_byte": f"{pal_byte:#04x}", "header_palette": (pal_byte >> 6) & 1,
            "header_bright": bool(pal_byte & 0x20), "background": data[16] >> 4,
            "trailing_bytes": len(data) - pos}
    return w, h, rows, info


def extract_pcx(path, out_png, palette_arg, bright_arg, zoom):
    with open(path, "rb") as f:
        data = f.read()
    w, h, rows, info = decode_pcx(data)
    pal_n = info["header_palette"] if palette_arg is None else palette_arg
    bright = info["header_bright"] if bright_arg is None else bright_arg
    write_png(out_png, scale(rows, zoom), w * zoom, h * zoom, PALETTES[(pal_n, bright)])
    print(f"{path} -> {out_png} (palette {pal_n}{', bright' if bright else ''})")
    for k, v in info.items():
        print(f"  {k:18} {v}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    tp = sub.add_parser("tiles")
    tp.add_argument("igs")
    tp.add_argument("outdir")
    lp = sub.add_parser("levels")
    lp.add_argument("exe")
    lp.add_argument("igs")
    lp.add_argument("outdir")
    lp.add_argument("--no-entities", action="store_true", help="don't outline entity spawn cells")
    pp = sub.add_parser("pcx")
    pp.add_argument("pcx")
    pp.add_argument("out")
    for p in (tp, lp, pp):
        p.add_argument("--palette", type=int, choices=(0, 1), default=None,
                       help="CGA palette: 0 = green/red/brown (the game's), 1 = cyan/magenta/white"
                            " (pcx default: from the file header)")
        p.add_argument("--bright", action="store_true", default=None, help="use the high-intensity colours")
        p.add_argument("--zoom", type=int, default=None, help="pixel scale (default 4 tiles, 2 levels/pcx)")
    args = ap.parse_args()

    if args.cmd == "pcx":
        extract_pcx(args.pcx, args.out, args.palette, args.bright, args.zoom or 2)
        return
    palette = PALETTES[(args.palette or 0, bool(args.bright))]
    if args.cmd == "tiles":
        extract_tiles(args.igs, args.outdir, palette, args.zoom or 4)
    else:
        extract_levels(args.exe, args.igs, args.outdir, palette, args.zoom or 2, not args.no_entities)


if __name__ == "__main__":
    main()
