#!/usr/bin/env python3
"""Draws the Script Studio's icons into indra/newview/skins/default/textures/studio/.

The studio's kinds of symbol -- function, event, constant and the rest --
its toolbar, its tabs and its explorer want small marks, white on a clear
ground with the shape in the alpha, as the viewer's other icons are, so
that a skin's colour tints them. They are drawn here from a few shapes --
discs, rings, rounded boxes, strokes, polygons -- laid out in a 16 by 16
grid, rasterised with the distance to each shape at sixteen samples a
pixel, and written as grey-and-alpha PNGs with nothing but the standard
library, so that a change to one is a change to a few lines here rather
than a paint session. Run it and commit what it writes:

    python3 scripts/content_tools/generate_studio_icons.py
"""

import math
import struct
import sys
import zlib
from pathlib import Path

SIZE = 16
SAMPLES = 4  # per axis: sixteen a pixel


# --- shapes: each answers how far a point is inside it (negative) ------------


def disc(cx, cy, r):
    return lambda x, y: math.hypot(x - cx, y - cy) - r


def ring(cx, cy, r, w):
    return lambda x, y: abs(math.hypot(x - cx, y - cy) - r) - w / 2


def box(x0, y0, x1, y1, radius=0.0):
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    hx, hy = (x1 - x0) / 2 - radius, (y1 - y0) / 2 - radius

    def sdf(x, y):
        dx, dy = abs(x - cx) - hx, abs(y - cy) - hy
        outside = math.hypot(max(dx, 0), max(dy, 0))
        inside = min(max(dx, dy), 0)
        return outside + inside - radius

    return sdf


def frame(x0, y0, x1, y1, w, radius=0.0):
    inner = box(x0, y0, x1, y1, radius)
    return lambda x, y: abs(inner(x, y)) - w / 2


def stroke(ax, ay, bx, by, w):
    def sdf(x, y):
        px, py = x - ax, y - ay
        vx, vy = bx - ax, by - ay
        length = vx * vx + vy * vy
        t = 0.0 if length == 0 else max(0.0, min(1.0, (px * vx + py * vy) / length))
        return math.hypot(px - t * vx, py - t * vy) - w / 2

    return sdf


def polygon(points):
    def sdf(x, y):
        n = len(points)
        d = float("inf")
        inside = False
        j = n - 1
        for i in range(n):
            ax, ay = points[i]
            bx, by = points[j]
            d = min(d, stroke(ax, ay, bx, by, 0)(x, y))
            if (ay > y) != (by > y) and x < (bx - ax) * (y - ay) / (by - ay) + ax:
                inside = not inside
            j = i
        return -d if inside else d

    return sdf


def union(*shapes):
    return lambda x, y: min(s(x, y) for s in shapes)


def cut(shape, hole):
    return lambda x, y: max(shape(x, y), -hole(x, y))


# --- the icons -------------------------------------------------------------------


def cube():
    # A box seen from a corner: a hexagon with the three edges that meet
    # in the middle.
    c = 8.0
    r = 6.0
    corners = [(c + r * math.cos(math.radians(a)), c + r * math.sin(math.radians(a))) for a in range(-90, 270, 60)]
    outline = union(*[stroke(*corners[i], *corners[(i + 1) % 6], 1.4) for i in range(6)])
    inner = union(stroke(c, c, *corners[1], 1.2), stroke(c, c, *corners[3], 1.2), stroke(c, c, *corners[5], 1.2))
    return union(outline, inner)


def bolt():
    return polygon([(9.5, 1), (4, 9), (7.5, 9), (6, 15), (12, 6.5), (8.5, 6.5), (10.5, 1)])


def target():
    return union(ring(8, 8, 5.5, 1.4), disc(8, 8, 2.2))


def tag():
    # A label with a hole, pointing left.
    body = polygon([(1.5, 8), (6, 2.5), (14, 2.5), (14, 13.5), (6, 13.5)])
    return cut(body, disc(6.5, 8, 1.4))


def tee():
    return union(box(2.5, 2.5, 13.5, 5, 0.6), box(6.75, 4, 9.25, 13.5, 0.6))


def outlined_box():
    return frame(2.5, 3, 13.5, 13, 1.4, 1.5)


def dotted_box():
    return union(frame(2.5, 3, 13.5, 13, 1.4, 1.5), disc(8, 8, 1.8))


def field():
    return union(frame(2, 2, 14, 14, 1.2, 1.5), box(7, 7, 12.5, 12.5, 0.8))


def flag():
    pole = stroke(4, 2, 4, 14.5, 1.5)
    cloth = polygon([(4.5, 2.5), (13, 5), (4.5, 7.5)])
    return union(pole, cloth)


def page(lines=()):
    # A sheet with a folded corner, and lines on it where asked.
    sheet = union(polygon([(3.5, 1.5), (9.5, 1.5), (12.5, 4.5), (12.5, 14.5), (3.5, 14.5)]))
    outline = lambda x, y: abs(sheet(x, y)) - 0.7
    fold = union(stroke(9.5, 1.5, 9.5, 4.5, 1.2), stroke(9.5, 4.5, 12.5, 4.5, 1.2))
    shapes = [outline, fold]
    for y in lines:
        shapes.append(stroke(5.5, y, 10.5, y, 1.2))
    return union(*shapes)


def dots():
    return union(disc(3.5, 8, 1.6), disc(8, 8, 1.6), disc(12.5, 8, 1.6))


def warning():
    shell = polygon([(8, 1.5), (15, 14), (1, 14)])
    body = lambda x, y: abs(shell(x, y)) - 0.7
    mark = union(stroke(8, 5.5, 8, 9.5, 1.5), disc(8, 12, 0.9))
    return union(body, mark)


def module():
    return union(frame(1.5, 1.5, 10.5, 10.5, 1.3, 1.0), frame(5.5, 5.5, 14.5, 14.5, 1.3, 1.0))


def magnifier():
    glass = ring(6.5, 6.5, 4.2, 1.6)
    handle = stroke(9.8, 9.8, 14, 14, 2.2)
    return union(glass, handle)


def format_lines():
    return union(stroke(2, 3, 14, 3, 1.6), stroke(6, 6.5, 14, 6.5, 1.6), stroke(6, 10, 14, 10, 1.6), stroke(2, 13.5, 14, 13.5, 1.6))


def floppies():
    # Two disks, one behind the other: save all.
    def floppy(x0, y0):
        shell = frame(x0, y0, x0 + 9, y0 + 9, 1.3, 1.0)
        label = box(x0 + 2.5, y0 + 5.5, x0 + 6.5, y0 + 9, 0.3)
        slot = box(x0 + 3, y0, x0 + 6, y0 + 3, 0.3)
        return union(shell, label, slot)

    back = floppy(5.5, 1.5)
    front = floppy(1.5, 5.5)
    return union(cut(back, box(1.5, 5.5, 10.5, 14.5)), front)


def braces():
    # { } for the preprocessed view.
    left = union(stroke(6, 2, 4.5, 2, 1.4), stroke(4.5, 2, 4.5, 7, 1.4), stroke(4.5, 7, 3, 8, 1.4), stroke(3, 8, 4.5, 9, 1.4),
                 stroke(4.5, 9, 4.5, 14, 1.4), stroke(4.5, 14, 6, 14, 1.4))
    right = union(stroke(10, 2, 11.5, 2, 1.4), stroke(11.5, 2, 11.5, 7, 1.4), stroke(11.5, 7, 13, 8, 1.4), stroke(13, 8, 11.5, 9, 1.4),
                  stroke(11.5, 9, 11.5, 14, 1.4), stroke(11.5, 14, 10, 14, 1.4))
    return union(left, right)


def prim():
    # A smaller cube, the object explorer's prim beside the linkset's box.
    c = 8.0
    r = 5.0
    corners = [(c + r * math.cos(math.radians(a)), c + r * math.sin(math.radians(a))) for a in range(-90, 270, 60)]
    body = polygon(corners)
    inner = union(stroke(c, c, *corners[1], 1.0), stroke(c, c, *corners[3], 1.0), stroke(c, c, *corners[5], 1.0))
    return union(lambda x, y: abs(body(x, y)) - 0.7, inner)


def gear():
    # A ring of eight teeth around a hole: the .luaurc in an explorer.
    teeth = [polygon([(8 + 7.5 * math.cos(math.radians(a - 8)), 8 + 7.5 * math.sin(math.radians(a - 8))),
                      (8 + 7.5 * math.cos(math.radians(a + 8)), 8 + 7.5 * math.sin(math.radians(a + 8))),
                      (8 + 5 * math.cos(math.radians(a + 14)), 8 + 5 * math.sin(math.radians(a + 14))),
                      (8 + 5 * math.cos(math.radians(a - 14)), 8 + 5 * math.sin(math.radians(a - 14)))])
             for a in range(0, 360, 45)]
    return cut(union(disc(8, 8, 5.2), *teeth), disc(8, 8, 2.2))


ICONS = {
    # The kinds of symbol, on the completion list and in the outline.
    "Symbol_Function": cube,
    "Symbol_Event": bolt,
    "Symbol_Constant": target,
    "Symbol_Keyword": tag,
    "Symbol_Type": tee,
    "Symbol_Variable": outlined_box,
    "Symbol_Parameter": dotted_box,
    "Symbol_Field": field,
    "Symbol_Label": flag,
    "Symbol_Snippet": lambda: page((7.5, 10.5)),
    "Symbol_Word": dots,
    "Symbol_Deprecated": warning,
    "Symbol_Module": module,
    # The toolbar.
    "Studio_SaveAll": floppies,
    "Studio_Find": magnifier,
    "Studio_Format": format_lines,
    "Studio_Expanded": braces,
    # The tabs and the explorer.
    "Studio_File": lambda: page(()),
    "Studio_Prim": prim,
    "Studio_Config": gear,
}


# --- rasterising and writing -------------------------------------------------------


def raster(shape):
    rows = []
    for py in range(SIZE):
        row = bytearray()
        for px in range(SIZE):
            covered = 0
            for sy in range(SAMPLES):
                for sx in range(SAMPLES):
                    x = px + (sx + 0.5) / SAMPLES
                    y = py + (sy + 0.5) / SAMPLES
                    if shape(x, y) <= 0:
                        covered += 1
            alpha = round(255 * covered / (SAMPLES * SAMPLES))
            row += bytes((255, alpha))
        rows.append(bytes(row))
    return rows


def png(rows):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 4, 0, 0, 0)  # 8-bit grey with alpha
    data = b"".join(b"\x00" + row for row in rows)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(data, 9)) + chunk(b"IEND", b"")


def main(argv):
    out = Path(__file__).resolve().parents[2] / "indra" / "newview" / "skins" / "default" / "textures" / "studio"
    out.mkdir(parents=True, exist_ok=True)
    for name, make in ICONS.items():
        path = out / (name + ".png")
        path.write_bytes(png(raster(make())))
        print(path.relative_to(Path(__file__).resolve().parents[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
