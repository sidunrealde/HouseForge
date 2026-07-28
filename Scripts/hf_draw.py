"""Minimal 2D drawing canvas with SVG and PNG backends.

Primitives are recorded once and rendered twice: to SVG, which is text and therefore diffable and
the checked-in source of truth, and to PNG, which is what Claude actually reads back. Writing to
one canvas rather than two renderers is what keeps the two outputs from drifting.

SVG needs no third-party package at all. PNG needs Pillow, which Scripts/hf-drawings.ps1 installs
into a local Scripts/.venv so nothing lands in the system Python.
"""

from __future__ import annotations

import math
import xml.sax.saxutils as sax
from dataclasses import dataclass, field
from typing import Sequence

# CAD-ish palette. Deliberately near-monochrome: these are line drawings, and the geometry has to
# read at a glance rather than compete with colour.
BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
WALL_FILL = (35, 35, 35)
GREY = (120, 120, 120)
LIGHT = (185, 185, 185)
FURNITURE = (70, 70, 70)
DIM = (90, 90, 90)
ACCENT = (0, 90, 170)
CEILING = (140, 40, 130)


def _hex(c: tuple[int, int, int]) -> str:
    return "#%02x%02x%02x" % c


@dataclass
class _Op:
    kind: str
    data: dict = field(default_factory=dict)


class Canvas:
    """Records drawing primitives, then renders them to SVG and PNG."""

    def __init__(self, width: int, height: int, title: str = ""):
        self.width = width
        self.height = height
        self.title = title
        self.ops: list[_Op] = []

    # ------------------------------------------------------------------ primitives

    def line(self, p1, p2, width=1.0, color=BLACK, dash=None):
        self.ops.append(_Op("line", dict(p1=p1, p2=p2, width=width, color=color, dash=dash)))

    def polyline(self, pts: Sequence, width=1.0, color=BLACK, dash=None, close=False):
        pts = list(pts)
        if close and pts:
            pts = pts + [pts[0]]
        for a, b in zip(pts, pts[1:]):
            self.line(a, b, width, color, dash)

    def polygon(self, pts: Sequence, fill=None, stroke=BLACK, width=1.0, dash=None):
        self.ops.append(_Op("polygon", dict(pts=list(pts), fill=fill, stroke=stroke,
                                            width=width, dash=dash)))

    def rect(self, x, y, w, h, fill=None, stroke=BLACK, width=1.0, dash=None):
        self.polygon([(x, y), (x + w, y), (x + w, y + h), (x, y + h)],
                     fill=fill, stroke=stroke, width=width, dash=dash)

    def circle(self, center, radius, fill=None, stroke=BLACK, width=1.0, dash=None):
        self.ops.append(_Op("circle", dict(c=center, r=radius, fill=fill, stroke=stroke,
                                           width=width, dash=dash)))

    def arc(self, center, radius, start_deg, end_deg, width=1.0, color=BLACK, dash=None):
        self.ops.append(_Op("arc", dict(c=center, r=radius, a0=start_deg, a1=end_deg,
                                        width=width, color=color, dash=dash)))

    def text(self, pos, s, size=12, color=BLACK, anchor="mm", rotate=0.0, bold=False):
        """anchor is a two-char Pillow-style code: horizontal l/m/r, vertical t/m/b."""
        self.ops.append(_Op("text", dict(pos=pos, s=str(s), size=size, color=color,
                                         anchor=anchor, rotate=rotate, bold=bold)))

    # ------------------------------------------------------------------------ SVG

    def to_svg(self) -> str:
        out = [
            f'<?xml version="1.0" encoding="UTF-8"?>',
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.width}" height="{self.height}" '
            f'viewBox="0 0 {self.width} {self.height}">',
            f'<title>{sax.escape(self.title)}</title>',
            f'<rect width="{self.width}" height="{self.height}" fill="#ffffff"/>',
        ]

        def dash_attr(d):
            return f' stroke-dasharray="{d}"' if d else ""

        for op in self.ops:
            d = op.data
            if op.kind == "line":
                (x1, y1), (x2, y2) = d["p1"], d["p2"]
                out.append(
                    f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" '
                    f'stroke="{_hex(d["color"])}" stroke-width="{d["width"]:.2f}"'
                    f'{dash_attr(d["dash"])} stroke-linecap="round"/>')

            elif op.kind == "polygon":
                pts = " ".join(f"{x:.2f},{y:.2f}" for x, y in d["pts"])
                fill = _hex(d["fill"]) if d["fill"] else "none"
                stroke = _hex(d["stroke"]) if d["stroke"] else "none"
                out.append(
                    f'<polygon points="{pts}" fill="{fill}" stroke="{stroke}" '
                    f'stroke-width="{d["width"]:.2f}"{dash_attr(d["dash"])}/>')

            elif op.kind == "circle":
                cx, cy = d["c"]
                fill = _hex(d["fill"]) if d["fill"] else "none"
                stroke = _hex(d["stroke"]) if d["stroke"] else "none"
                out.append(
                    f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{d["r"]:.2f}" fill="{fill}" '
                    f'stroke="{stroke}" stroke-width="{d["width"]:.2f}"{dash_attr(d["dash"])}/>')

            elif op.kind == "arc":
                cx, cy = d["c"]
                r = d["r"]
                a0, a1 = math.radians(d["a0"]), math.radians(d["a1"])
                x0, y0 = cx + r * math.cos(a0), cy + r * math.sin(a0)
                x1, y1 = cx + r * math.cos(a1), cy + r * math.sin(a1)
                large = 1 if abs(d["a1"] - d["a0"]) > 180 else 0
                sweep = 1 if d["a1"] > d["a0"] else 0
                out.append(
                    f'<path d="M {x0:.2f} {y0:.2f} A {r:.2f} {r:.2f} 0 {large} {sweep} '
                    f'{x1:.2f} {y1:.2f}" fill="none" stroke="{_hex(d["color"])}" '
                    f'stroke-width="{d["width"]:.2f}"{dash_attr(d["dash"])}/>')

            elif op.kind == "text":
                x, y = d["pos"]
                ax = {"l": "start", "m": "middle", "r": "end"}[d["anchor"][0]]
                # SVG's dominant-baseline maps onto Pillow's vertical anchor codes.
                ay = {"t": "hanging", "m": "central", "b": "auto"}[d["anchor"][1]]
                transform = f' transform="rotate({-d["rotate"]:.2f} {x:.2f} {y:.2f})"' if d["rotate"] else ""
                weight = ' font-weight="bold"' if d["bold"] else ""
                out.append(
                    f'<text x="{x:.2f}" y="{y:.2f}" font-family="DejaVu Sans, Arial, sans-serif" '
                    f'font-size="{d["size"]:.2f}" fill="{_hex(d["color"])}" text-anchor="{ax}" '
                    f'dominant-baseline="{ay}"{weight}{transform}>{sax.escape(d["s"])}</text>')

        out.append("</svg>")
        return "\n".join(out)

    # ------------------------------------------------------------------------ PNG

    def to_png(self, path, supersample: int = 2):
        """Renders via Pillow. Supersampled then downscaled, because Pillow has no antialiasing
        for lines and unsmoothed CAD line work is genuinely hard to read."""
        from PIL import Image, ImageDraw

        s = supersample
        img = Image.new("RGB", (self.width * s, self.height * s), WHITE)
        draw = ImageDraw.Draw(img)

        def sc(p):
            return (p[0] * s, p[1] * s)

        def dashed_segments(p1, p2, pattern):
            """Pillow has no dash support, so long dashes are emitted as separate segments."""
            on, off = pattern
            x1, y1 = p1
            x2, y2 = p2
            total = math.hypot(x2 - x1, y2 - y1)
            if total <= 0:
                return
            ux, uy = (x2 - x1) / total, (y2 - y1) / total
            pos = 0.0
            while pos < total:
                seg = min(on, total - pos)
                yield ((x1 + ux * pos, y1 + uy * pos),
                       (x1 + ux * (pos + seg), y1 + uy * (pos + seg)))
                pos += on + off

        def parse_dash(d):
            if not d:
                return None
            parts = [float(v) for v in str(d).replace(",", " ").split()]
            if len(parts) == 1:
                parts = [parts[0], parts[0]]
            return parts[0], parts[1]

        def stroke_line(p1, p2, color, width, dash):
            w = max(1, int(round(width * s)))
            pattern = parse_dash(dash)
            if pattern:
                for a, b in dashed_segments(p1, p2, pattern):
                    draw.line([sc(a), sc(b)], fill=color, width=w)
            else:
                draw.line([sc(p1), sc(p2)], fill=color, width=w)

        for op in self.ops:
            d = op.data
            if op.kind == "line":
                stroke_line(d["p1"], d["p2"], d["color"], d["width"], d["dash"])

            elif op.kind == "polygon":
                pts = [sc(p) for p in d["pts"]]
                if d["fill"]:
                    draw.polygon(pts, fill=d["fill"])
                if d["stroke"]:
                    closed = d["pts"] + [d["pts"][0]]
                    for a, b in zip(closed, closed[1:]):
                        stroke_line(a, b, d["stroke"], d["width"], d["dash"])

            elif op.kind == "circle":
                cx, cy = d["c"]
                r = d["r"]
                box = [sc((cx - r, cy - r)), sc((cx + r, cy + r))]
                draw.ellipse(box, fill=d["fill"],
                             outline=d["stroke"] if d["stroke"] else None,
                             width=max(1, int(round(d["width"] * s))))

            elif op.kind == "arc":
                cx, cy = d["c"]
                r = d["r"]
                box = [sc((cx - r, cy - r)), sc((cx + r, cy + r))]
                draw.arc(box, d["a0"], d["a1"], fill=d["color"],
                         width=max(1, int(round(d["width"] * s))))

            elif op.kind == "text":
                _draw_text(draw, img, d, s, sc)

        if s > 1:
            img = img.resize((self.width, self.height), Image.LANCZOS)
        img.save(path)

    def save(self, svg_path, png_path):
        with open(svg_path, "w", encoding="utf-8") as f:
            f.write(self.to_svg())
        self.to_png(png_path)


# --------------------------------------------------------------------------------- text support

_FONT_CACHE: dict[tuple[int, bool], object] = {}

_FONT_CANDIDATES = [
    ("C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/arialbd.ttf"),
    ("C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/segoeuib.ttf"),
    ("DejaVuSans.ttf", "DejaVuSans-Bold.ttf"),
]


def _font(size: int, bold: bool):
    key = (size, bold)
    if key in _FONT_CACHE:
        return _FONT_CACHE[key]

    from PIL import ImageFont

    for regular, heavy in _FONT_CANDIDATES:
        try:
            _FONT_CACHE[key] = ImageFont.truetype(heavy if bold else regular, size)
            return _FONT_CACHE[key]
        except OSError:
            continue

    # Every candidate missing is survivable - the drawing still reads, the labels are just plain.
    _FONT_CACHE[key] = ImageFont.load_default()
    return _FONT_CACHE[key]


def _draw_text(draw, img, d, s, sc):
    from PIL import Image, ImageDraw

    font = _font(max(6, int(round(d["size"] * s))), d["bold"])

    if not d["rotate"]:
        draw.text(sc(d["pos"]), d["s"], fill=d["color"], font=font, anchor=d["anchor"])
        return

    # Pillow cannot rotate text in place, so it is rendered to its own transparent layer, rotated
    # about its anchor, and composited back. Used for vertical dimension strings.
    box = draw.textbbox((0, 0), d["s"], font=font, anchor="lt")
    tw, th = max(1, box[2] - box[0]), max(1, box[3] - box[1])
    pad = max(tw, th)
    layer = Image.new("RGBA", (tw + 2 * pad, th + 2 * pad), (0, 0, 0, 0))
    ImageDraw.Draw(layer).text((pad, pad), d["s"], fill=d["color"] + (255,), font=font, anchor="lt")

    rotated = layer.rotate(d["rotate"], resample=Image.BICUBIC, expand=False)

    ax, ay = d["anchor"]
    ox = {"l": 0.0, "m": 0.5, "r": 1.0}[ax] * tw
    oy = {"t": 0.0, "m": 0.5, "b": 1.0}[ay] * th

    theta = math.radians(d["rotate"])
    # Offset from the layer centre to the requested anchor point, rotated with the text.
    dx = (pad + ox) - layer.width / 2.0
    dy = (pad + oy) - layer.height / 2.0
    rx = dx * math.cos(theta) + dy * math.sin(theta)
    ry = -dx * math.sin(theta) + dy * math.cos(theta)

    px, py = sc(d["pos"])
    img.paste(rotated,
              (int(round(px - layer.width / 2.0 - rx)), int(round(py - layer.height / 2.0 - ry))),
              rotated)
