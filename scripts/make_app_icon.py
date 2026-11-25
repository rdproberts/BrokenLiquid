"""Generate resources/app/BrokenLiquid.ico (16-256 px) with no third-party packages.

The logo: a liquid droplet on a navy tile, split by a crack - smooth chrome on the left,
faceted cyan-edged shards on the right ("broken liquid"). Re-run after changing the design:

    python scripts/make_app_icon.py
"""
import math
import struct
import zlib
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "resources" / "app"
SIZES = [16, 24, 32, 48, 64, 128, 256]

NAVY_TOP = (0x13, 0x2A, 0x45)
NAVY_BOT = (0x0A, 0x14, 0x24)
CYAN = (0x22, 0xC3, 0xE6)
CYAN_LIGHT = (0x8F, 0xE9, 0xF8)

TIP = (0.5, 0.13)
BODY = (0.5, 0.60)
R = 0.265
SHIFT = (0.035, -0.02)  # right half is displaced like a broken piece
CRACK = [(0.13, 0.502), (0.30, 0.484), (0.44, 0.518), (0.58, 0.488), (0.72, 0.520), (0.88, 0.495)]


def lerp(a, b, t):
    t = max(0.0, min(1.0, t))
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(len(a)))


def in_droplet(x, y):
    dx, dy = x - BODY[0], y - BODY[1]
    if dx * dx + dy * dy <= R * R:
        return True
    ax, ay = BODY[0] - TIP[0], BODY[1] - TIP[1]
    d = math.hypot(ax, ay)
    px, py = x - TIP[0], y - TIP[1]
    along = (px * ax + py * ay) / d
    if along < 0:
        return False
    half = math.asin(R / d)
    tangent_len = math.sqrt(d * d - R * R)
    if along > tangent_len * math.cos(half):
        return False
    perp = abs(px * ay - py * ax) / d
    return perp <= along * math.tan(half)


def crack_x(y):
    pts = CRACK
    if y <= pts[0][0]:
        return pts[0][1]
    for (y0, x0), (y1, x1) in zip(pts, pts[1:]):
        if y0 <= y <= y1:
            return x0 + (x1 - x0) * (y - y0) / (y1 - y0)
    return pts[-1][1]


def in_tile(x, y):
    r = 0.2
    cx = min(max(x, r), 1 - r)
    cy = min(max(y, r), 1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def facet_shade(x, y, size):
    # Triangular lattice facets with a deterministic per-facet brightness.
    s = 0.085
    u, v = x / s, y / s
    i, j = math.floor(u), math.floor(v)
    fu, fv = u - i, v - j
    upper = fu + fv < 1.0
    h = (i * 73856093) ^ (j * 19349663) ^ (1 if upper else 0) * 83492791
    rnd = ((h & 0xFFFF) / 0xFFFF) - 0.5
    light = 0.80 - 0.9 * (x - 0.5) - 0.6 * (y - 0.35) + rnd * 0.35
    edge = min(fu, fv, abs(1.0 - fu - fv)) < (0.06 if size >= 64 else 0.0)
    return light, edge


def sample(x, y, size):
    if not in_tile(x, y):
        return None
    col = lerp(NAVY_TOP, NAVY_BOT, y)
    # glow behind droplet
    g = math.hypot(x - 0.5, y - 0.52)
    col = lerp(col, CYAN, max(0.0, 0.22 - g * 0.45))

    cx = crack_x(y)
    line_w = 0.012 if size >= 48 else 0.022
    # left half (fluid chrome)
    if in_droplet(x, y) and x <= cx:
        hx, hy = x - 0.40, y - 0.50
        t = math.hypot(hx, hy) / 0.33
        col = lerp((0xF4, 0xFA, 0xFF), (0x8D, 0xA8, 0xC2), t * 1.4)
        col = lerp(col, (0x14, 0x28, 0x42), (t - 0.55) * 1.6)
        if abs(x - cx) < line_w:
            col = lerp(col, CYAN_LIGHT, 0.9)
        return col
    # right half (faceted shards), shifted
    rx, ry = x - SHIFT[0], y - SHIFT[1]
    if in_droplet(rx, ry) and rx > crack_x(ry):
        light, edge = facet_shade(rx, ry, size)
        col = lerp((0x1E, 0x38, 0x57), (0xD4, 0xE2, 0xEE), light)
        if edge:
            col = lerp(col, CYAN, 0.75)
        if abs(rx - crack_x(ry)) < line_w:
            col = lerp(col, CYAN_LIGHT, 0.9)
        return col
    return col


def render(size):
    ss = 4 if size <= 64 else 3
    rows = []
    for py in range(size):
        row = bytearray()
        for px in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(ss):
                for sx in range(ss):
                    x = (px + (sx + 0.5) / ss) / size
                    y = (py + (sy + 0.5) / ss) / size
                    c = sample(x, y, size)
                    if c is not None:
                        acc[0] += c[0]
                        acc[1] += c[1]
                        acc[2] += c[2]
                        acc[3] += 255
            n = ss * ss
            a = acc[3] / n
            if a > 0:
                cov = acc[3] / 255
                row += bytes((int(acc[0] / cov), int(acc[1] / cov), int(acc[2] / cov), int(a)))
            else:
                row += b"\x00\x00\x00\x00"
        rows.append(bytes(row))
    return rows


def png_bytes(size, rows):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    images = []
    for s in SIZES:
        data = png_bytes(s, render(s))
        images.append((s, data))
        if s == 256:
            (OUT / "BrokenLiquid-256.png").write_bytes(data)
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries, blobs = b"", b""
    for s, data in images:
        dim = 0 if s >= 256 else s
        entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
        blobs += data
    (OUT / "BrokenLiquid.ico").write_bytes(header + entries + blobs)
    print("wrote", OUT / "BrokenLiquid.ico")


if __name__ == "__main__":
    main()
