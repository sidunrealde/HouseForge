"""Generates the reference 2BHK drawing set from the house spec.

The drawings and the ground truth come from the same file - Reference/Specs/Sample2BHK.json,
itself generated from FHFSampleHouse::Make2BHK() - so a drawing can never describe a house the
plugin would not build. That is what makes this set usable as an acceptance test: Claude reads the
PNGs like a real drawing, rebuilds a spec, and the result is diffable against known truth.

Sheets:
    01  blank layout            walls, door swings, window symbols, dimensions, room schedule
    02  furniture layout        plan symbols for every fixture
    03  reflected ceiling plan  false ceiling regions, coves, lights, fans, switch plates
    04+ wall elevations         one sheet per room, elevations A/B/C/D side by side

Run via Scripts/hf-drawings.ps1, which provisions the local venv Pillow needs.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from hf_draw import (ACCENT, BLACK, CEILING, Canvas, DIM, FURNITURE, GREY, LIGHT,
                     WALL_FILL, WHITE)

# A3 landscape at 150 dpi. Large enough that a 1200 wide wardrobe still shows its shutter lines.
SHEET_W, SHEET_H = 2480, 1754
FRAME = 30
INNER = 48
TITLE_W, TITLE_H = 720, 210

PLAN_AREA = (130, 150, SHEET_W - 130, SHEET_H - 290)

DASH_HIDDEN = "10 7"
DASH_CEILING = "16 8"
DASH_FINE = "5 5"

# Ceiling-mounted items belong on the reflected ceiling plan, not the furniture layout.
CEILING_MOUNTED = ("CeilingFan", "LightFixture")

# Everything the electrical sheet is responsible for. Kept in one place so the furniture layout
# and the electrical layout cannot disagree about which is which.
ELECTRICAL_TYPES = ("PowerSocket", "SwitchPlate", "DistributionBoard", "ACIndoorUnit",
                    "ACOutdoorUnit", "Geyser", "ExhaustFan", "CeilingFan", "LightFixture")


# ------------------------------------------------------------------------------- spec accessors

def load_spec(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def pt(d):
    return (d["x"], d["y"])


def wall_by_id(spec, wall_id):
    for w in spec["walls"]:
        if w["id"] == wall_id:
            return w
    return None


def room_bounds(room):
    xs = [p["x"] for p in room["boundary"]]
    ys = [p["y"] for p in room["boundary"]]
    return min(xs), min(ys), max(xs), max(ys)


def wall_axis(wall):
    """'h' for a wall running east-west, 'v' for north-south, None for anything skew."""
    (x1, y1), (x2, y2) = pt(wall["start"]), pt(wall["end"])
    if abs(y2 - y1) < 1e-6:
        return "h"
    if abs(x2 - x1) < 1e-6:
        return "v"
    return None


def wall_polygon(wall):
    """The wall's plan footprint: centreline expanded by half its thickness each side."""
    (x1, y1), (x2, y2) = pt(wall["start"]), pt(wall["end"])
    dx, dy = x2 - x1, y2 - y1
    length = math.hypot(dx, dy)
    if length < 1e-9:
        return []
    nx, ny = -dy / length, dx / length
    h = wall["thickness"] / 2.0
    return [(x1 + nx * h, y1 + ny * h), (x2 + nx * h, y2 + ny * h),
            (x2 - nx * h, y2 - ny * h), (x1 - nx * h, y1 - ny * h)]


def opening_frame(wall, opening):
    """Corner points of the hole an opening punches through its wall, in plan."""
    (x1, y1), (x2, y2) = pt(wall["start"]), pt(wall["end"])
    dx, dy = x2 - x1, y2 - y1
    length = math.hypot(dx, dy)
    ux, uy = dx / length, dy / length
    nx, ny = -uy, ux

    c = opening["offsetAlongWall"]
    hw = opening["width"] / 2.0
    ht = wall["thickness"] / 2.0 + 2.0     # slight overshoot so the white gap fully clears the fill

    a = (x1 + ux * (c - hw), y1 + uy * (c - hw))
    b = (x1 + ux * (c + hw), y1 + uy * (c + hw))
    return [(a[0] + nx * ht, a[1] + ny * ht), (b[0] + nx * ht, b[1] + ny * ht),
            (b[0] - nx * ht, b[1] - ny * ht), (a[0] - nx * ht, a[1] - ny * ht)], (ux, uy), (nx, ny), a, b


# ------------------------------------------------------------------------------------ plan view

class PlanView:
    """Maps plan millimetres to sheet pixels, flipping Y so north is up."""

    def __init__(self, spec, area=PLAN_AREA, pad_mm=900.0):
        xs, ys = [], []
        for w in spec["walls"]:
            for p in wall_polygon(w):
                xs.append(p[0])
                ys.append(p[1])
        for r in spec["rooms"]:
            for p in r["boundary"]:
                xs.append(p["x"])
                ys.append(p["y"])

        self.min_x, self.max_x = min(xs) - pad_mm, max(xs) + pad_mm
        self.min_y, self.max_y = min(ys) - pad_mm, max(ys) + pad_mm

        ax0, ay0, ax1, ay1 = area
        span_x = self.max_x - self.min_x
        span_y = self.max_y - self.min_y
        self.scale = min((ax1 - ax0) / span_x, (ay1 - ay0) / span_y)

        # Centre the plan in its area rather than pinning it to a corner.
        used_w = span_x * self.scale
        used_h = span_y * self.scale
        self.ox = ax0 + ((ax1 - ax0) - used_w) / 2.0
        self.oy = ay0 + ((ay1 - ay0) - used_h) / 2.0
        self.area = area

    def __call__(self, p):
        x, y = (p["x"], p["y"]) if isinstance(p, dict) else p
        return (self.ox + (x - self.min_x) * self.scale,
                self.oy + (self.max_y - y) * self.scale)

    def s(self, mm):
        return mm * self.scale


# ---------------------------------------------------------------------------------- sheet frame

def sheet_frame(c, sheet_no, sheet_title, spec, total_sheets):
    c.rect(FRAME, FRAME, SHEET_W - 2 * FRAME, SHEET_H - 2 * FRAME, stroke=BLACK, width=2.5)
    c.rect(INNER, INNER, SHEET_W - 2 * INNER, SHEET_H - 2 * INNER, stroke=BLACK, width=1.0)

    tx = SHEET_W - INNER - TITLE_W
    ty = SHEET_H - INNER - TITLE_H
    c.rect(tx, ty, TITLE_W, TITLE_H, fill=WHITE, stroke=BLACK, width=2.0)
    c.line((tx, ty + 58), (tx + TITLE_W, ty + 58), width=1.0)
    c.line((tx, ty + 118), (tx + TITLE_W, ty + 118), width=1.0)
    c.line((tx + 470, ty + 118), (tx + 470, ty + TITLE_H), width=1.0)

    c.text((tx + 18, ty + 29), "HOUSEFORGE  /  INTERIOR DESIGN SET", size=20, anchor="lm", bold=True)
    c.text((tx + 18, ty + 88), spec.get("name", "Residence"), size=26, anchor="lm", bold=True)
    c.text((tx + 18, ty + 140), "DRAWING", size=13, anchor="lm", color=GREY)
    c.text((tx + 18, ty + 172), sheet_title.upper(), size=19, anchor="lm", bold=True)
    c.text((tx + 488, ty + 140), "SHEET", size=13, anchor="lm", color=GREY)
    c.text((tx + 488, ty + 172), f"{sheet_no:02d} / {total_sheets:02d}", size=19, anchor="lm", bold=True)

    c.text((INNER + 22, INNER + 34), sheet_title.upper(), size=30, anchor="lm", bold=True)
    c.text((INNER + 22, INNER + 70), f"ALL DIMENSIONS IN {spec.get('units', 'Millimeters').upper()}",
           size=15, anchor="lm", color=GREY)


def north_arrow(c, x, y, r=34):
    c.circle((x, y), r, stroke=BLACK, width=1.6)
    c.polygon([(x, y - r + 5), (x - 11, y + r - 12), (x, y + r - 21), (x + 11, y + r - 12)],
              fill=BLACK, stroke=BLACK, width=1.0)
    c.text((x, y + r + 20), "N", size=18, anchor="mm", bold=True)


def scale_bar(c, view, x, y, length_mm=2000):
    px = view.s(length_mm)
    c.line((x, y), (x + px, y), width=2.0)
    for i in range(5):
        seg = px / 4.0
        if i < 4 and i % 2 == 0:
            c.rect(x + i * seg, y - 7, seg, 7, fill=BLACK, stroke=BLACK, width=0.5)
        c.line((x + i * seg, y - 12), (x + i * seg, y + 6), width=1.2)
    c.text((x, y + 26), "0", size=13, anchor="mm", color=DIM)
    c.text((x + px, y + 26), f"{length_mm}", size=13, anchor="mm", color=DIM)
    c.text((x + px / 2.0, y - 26), "SCALE", size=12, anchor="mm", color=GREY)


# ----------------------------------------------------------------------------------- dimensions

def dim_chain(c, view, values, fixed, axis, offset_px, label_size=15):
    """A dimension chain along one axis. `values` are the plan coordinates of each tick."""
    values = sorted(set(round(v, 3) for v in values))
    if len(values) < 2:
        return

    for i, (a, b) in enumerate(zip(values, values[1:])):
        if axis == "x":
            pa = view((a, fixed))
            pb = view((b, fixed))
            y = pa[1] + offset_px
            c.line((pa[0], y), (pb[0], y), width=1.2, color=DIM)
            for px in (pa[0], pb[0]):
                c.line((px, pa[1]), (px, y + 9), width=0.8, color=LIGHT)
                c.line((px - 6, y + 6), (px + 6, y - 6), width=1.4, color=DIM)
            if pb[0] - pa[0] > 34:
                c.text(((pa[0] + pb[0]) / 2.0, y - 15), f"{int(round(b - a))}",
                       size=label_size, anchor="mm", color=DIM)
        else:
            pa = view((fixed, a))
            pb = view((fixed, b))
            x = pa[0] - offset_px
            c.line((x, pa[1]), (x, pb[1]), width=1.2, color=DIM)
            for py in (pa[1], pb[1]):
                c.line((pa[0], py), (x - 9, py), width=0.8, color=LIGHT)
                c.line((x - 6, py + 6), (x + 6, py - 6), width=1.4, color=DIM)
            if abs(pb[1] - pa[1]) > 34:
                c.text((x - 15, (pa[1] + pb[1]) / 2.0), f"{int(round(b - a))}",
                       size=label_size, anchor="mm", color=DIM, rotate=90)


def grid_dimensions(c, spec, view):
    """Dimension chains built from the wall grid, plus an overall run outside them."""
    xs, ys = set(), set()
    for w in spec["walls"]:
        axis = wall_axis(w)
        if axis == "v":
            xs.add(round(w["start"]["x"], 3))
        elif axis == "h":
            ys.add(round(w["start"]["y"], 3))

    all_x = [p[0] for w in spec["walls"] for p in wall_polygon(w)]
    all_y = [p[1] for w in spec["walls"] for p in wall_polygon(w)]
    lo_x, hi_x = min(all_x), max(all_x)
    lo_y, hi_y = min(all_y), max(all_y)

    dim_chain(c, view, xs, lo_y, "x", 66)
    dim_chain(c, view, [min(xs), max(xs)], lo_y, "x", 132, label_size=17)
    dim_chain(c, view, ys, lo_x, "y", 66)
    dim_chain(c, view, [min(ys), max(ys)], lo_x, "y", 132, label_size=17)


# ---------------------------------------------------------------------------------- plan shells

def draw_walls(c, spec, view, fill=WALL_FILL):
    for w in spec["walls"]:
        poly = [view(p) for p in wall_polygon(w)]
        if poly:
            c.polygon(poly, fill=fill, stroke=BLACK, width=1.2)


def draw_openings(c, spec, view, show_swings=True):
    for o in spec["openings"]:
        wall = wall_by_id(spec, o["wallId"])
        if wall is None:
            continue

        frame, (ux, uy), (nx, ny), a, b = opening_frame(wall, o)
        kind = o["kind"]

        # Ventilators sit high in the wall, so in plan they are shown hidden rather than cut open.
        if kind == "Ventilator":
            c.polygon([view(p) for p in frame], fill=None, stroke=GREY, width=1.2, dash=DASH_HIDDEN)
            continue

        # Punch the hole, then redraw the reveals so the wall still reads as closed at each jamb.
        c.polygon([view(p) for p in frame], fill=WHITE, stroke=None)
        ht = wall["thickness"] / 2.0
        for endpoint in (a, b):
            c.line(view((endpoint[0] + nx * ht, endpoint[1] + ny * ht)),
                   view((endpoint[0] - nx * ht, endpoint[1] - ny * ht)), width=1.4)

        if kind in ("Window", "SlidingWindow"):
            for f in (0.28, 0.5, 0.72):
                off = (f - 0.5) * wall["thickness"]
                c.line(view((a[0] + nx * off, a[1] + ny * off)),
                       view((b[0] + nx * off, b[1] + ny * off)),
                       width=1.1, color=BLACK)

        elif kind == "SlidingDoor":
            # Two overlapping leaves, offset to opposite faces of the frame.
            q = wall["thickness"] * 0.22
            mid = ((a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0)
            c.line(view((a[0] + nx * q, a[1] + ny * q)),
                   view((mid[0] + ux * 60 + nx * q, mid[1] + uy * 60 + ny * q)), width=3.0)
            c.line(view((b[0] - nx * q, b[1] - ny * q)),
                   view((mid[0] - ux * 60 - nx * q, mid[1] - uy * 60 - ny * q)), width=3.0)

        elif kind == "Archway":
            c.line(view(a), view(b), width=1.0, color=GREY, dash=DASH_FINE)

        else:
            # Hinged door: leaf drawn at 90 degrees with its swing arc, as on a real plan.
            width = o["width"]
            swing = o.get("swing", "None")
            hinge = a if "Left" in swing else b
            inward = -1.0 if "Outward" in swing else 1.0
            leaf_end = (hinge[0] + nx * width * inward, hinge[1] + ny * width * inward)
            c.line(view(hinge), view(leaf_end), width=3.0)

            if show_swings:
                start_ang = math.degrees(math.atan2(-(leaf_end[1] - hinge[1]),
                                                    leaf_end[0] - hinge[0]))
                other = b if hinge is a else a
                end_ang = math.degrees(math.atan2(-(other[1] - hinge[1]), other[0] - hinge[0]))
                lo, hi = sorted((start_ang % 360, end_ang % 360))
                if hi - lo > 180:
                    lo, hi = hi, lo + 360
                c.arc(view(hinge), view.s(width), lo, hi, width=1.0, color=GREY, dash=DASH_FINE)


def column_polygon(col):
    cx, cy = col["position"]["x"], col["position"]["y"]
    hw, hd = col["size"]["x"] / 2.0, col["size"]["y"] / 2.0
    rot = col.get("rotationDegrees", 0.0)
    return [_rot(cx + u * hw, cy + v * hd, cx, cy, rot)
            for u, v in ((-1, -1), (1, -1), (1, 1), (-1, 1))]


def draw_columns(c, spec, view):
    """Columns are drawn over the walls and cross-hatched, the way a structural plan marks them."""
    for col in spec.get("columns", []):
        poly = [view(p) for p in column_polygon(col)]
        c.polygon(poly, fill=(70, 70, 70), stroke=BLACK, width=1.4)
        # Diagonal ticks read as concrete even at small scale.
        c.line(poly[0], poly[2], width=0.9, color=WHITE)
        c.line(poly[1], poly[3], width=0.9, color=WHITE)


def draw_beams(c, spec, view, label=True):
    """Beams are above the cut plane, so they are hidden line - long dash, as on a real RCP."""
    for beam in spec.get("beams", []):
        (x1, y1), (x2, y2) = pt(beam["start"]), pt(beam["end"])
        dx, dy = x2 - x1, y2 - y1
        length = math.hypot(dx, dy)
        if length < 1e-9:
            continue
        nx, ny = -dy / length, dx / length
        h = beam["width"] / 2.0

        for side in (-1.0, 1.0):
            c.line(view((x1 + nx * h * side, y1 + ny * h * side)),
                   view((x2 + nx * h * side, y2 + ny * h * side)),
                   width=1.3, color=ACCENT, dash=DASH_CEILING)

        if label:
            mx, my = view(((x1 + x2) / 2.0, (y1 + y2) / 2.0))
            angle = math.degrees(math.atan2(dy, dx))
            if angle > 90:
                angle -= 180
            elif angle < -90:
                angle += 180
            c.text((mx, my), f"BEAM {int(round(beam['width']))}x{int(round(beam['depth']))}",
                   size=10, anchor="mm", color=ACCENT, rotate=angle)


def draw_room_labels(c, spec, view, with_area=True):
    for r in spec["rooms"]:
        x0, y0, x1, y1 = room_bounds(r)
        cx, cy = view(((x0 + x1) / 2.0, (y0 + y1) / 2.0))
        name = r["name"].upper()
        c.text((cx, cy - 13), name, size=17, anchor="mm", bold=True)
        if with_area:
            area = abs(_polygon_area([pt(p) for p in r["boundary"]])) / 1_000_000.0
            c.text((cx, cy + 12), f"{area:.2f} SQM", size=14, anchor="mm", color=DIM)
            c.text((cx, cy + 33), f"{(x1 - x0):.0f} x {(y1 - y0):.0f}", size=13,
                   anchor="mm", color=LIGHT)


def _polygon_area(points):
    total = 0.0
    for i in range(len(points)):
        x1, y1 = points[i]
        x2, y2 = points[(i + 1) % len(points)]
        total += x1 * y2 - x2 * y1
    return total / 2.0


# ------------------------------------------------------------------------ fixture plan symbols

def _rot(px, py, cx, cy, deg):
    t = math.radians(deg)
    dx, dy = px - cx, py - cy
    return (cx + dx * math.cos(t) - dy * math.sin(t),
            cy + dx * math.sin(t) + dy * math.cos(t))


class FixtureFrame:
    """Local frame of a fixture footprint, so symbols can be drawn in easy local coordinates."""

    def __init__(self, fixture, view):
        self.cx, self.cy = fixture["position"]["x"], fixture["position"]["y"]
        self.w = fixture["footprint"]["x"]
        self.d = fixture["footprint"]["y"]
        self.rot = fixture.get("rotationDegrees", 0.0)
        self.view = view

    def p(self, u, v):
        """u,v in -0.5..0.5 across the footprint."""
        px = self.cx + u * self.w
        py = self.cy + v * self.d
        return self.view(_rot(px, py, self.cx, self.cy, self.rot))

    def outline(self):
        return [self.p(-0.5, -0.5), self.p(0.5, -0.5), self.p(0.5, 0.5), self.p(-0.5, 0.5)]


def _divisions(c, f, count, along_width=True, color=FURNITURE, width=1.0, dash=None):
    if count <= 1:
        return
    for i in range(1, count):
        t = -0.5 + i / count
        if along_width:
            c.line(f.p(t, -0.5), f.p(t, 0.5), width=width, color=color, dash=dash)
        else:
            c.line(f.p(-0.5, t), f.p(0.5, t), width=width, color=color, dash=dash)


def draw_fixture_plan(c, fx, view):
    kind = fx["type"]
    f = FixtureFrame(fx, view)
    params = fx.get("params", {}) or {}
    outline = f.outline()

    def box(dash=None, w=1.4):
        c.polygon(outline, fill=None, stroke=FURNITURE, width=w, dash=dash)

    if kind in ("Wardrobe", "LoftUnit", "Bookshelf", "TVUnit", "StudyTable", "Vanity",
                "KitchenBaseCabinet", "KitchenTallUnit"):
        box()
        n = max(params.get("shutterCount", 0), params.get("drawerCount", 0), 2)
        # Shutters always divide the long face of a run, whichever footprint axis that is. Keying
        # off the geometry rather than the authoring convention keeps the symbol right even when a
        # spec transposes width and depth.
        along = f.w >= f.d
        _divisions(c, f, n, along_width=along)
        # Cross-diagonals are the conventional plan mark for a hinged storage unit.
        if kind in ("Wardrobe", "KitchenTallUnit"):
            for i in range(n):
                t0 = -0.5 + i / n
                t1 = -0.5 + (i + 1) / n
                tm = (t0 + t1) / 2.0
                if along:
                    c.line(f.p(t0, 0.5), f.p(tm, -0.5), width=0.7, color=LIGHT)
                    c.line(f.p(t1, 0.5), f.p(tm, -0.5), width=0.7, color=LIGHT)
                else:
                    c.line(f.p(0.5, t0), f.p(-0.5, tm), width=0.7, color=LIGHT)
                    c.line(f.p(0.5, t1), f.p(-0.5, tm), width=0.7, color=LIGHT)

    elif kind == "KitchenWallCabinet":
        box(dash=DASH_HIDDEN)     # above eye level, so hidden line
        _divisions(c, f, max(params.get("shutterCount", 2), 2),
                   along_width=f.w >= f.d, dash=DASH_HIDDEN, color=GREY)

    elif kind == "CounterTop":
        box(w=1.8)

    elif kind == "Sink":
        box()
        for u in (-0.25, 0.25):
            c.polygon([f.p(u - 0.2, -0.32), f.p(u + 0.2, -0.32), f.p(u + 0.2, 0.28), f.p(u - 0.2, 0.28)],
                      stroke=FURNITURE, width=1.0)
        c.circle(f.p(0.0, -0.42), max(3.0, view.s(45)), stroke=FURNITURE, width=1.0)

    elif kind == "Hob":
        box()
        for u, v in ((-0.24, -0.24), (0.24, -0.24), (-0.24, 0.24), (0.24, 0.24)):
            c.circle(f.p(u, v), max(3.0, view.s(80)), stroke=FURNITURE, width=1.0)

    elif kind == "Chimney":
        box(dash=DASH_HIDDEN)
        c.line(f.p(-0.5, -0.5), f.p(0.5, 0.5), width=0.8, color=GREY, dash=DASH_HIDDEN)

    elif kind in ("Refrigerator", "WashingMachine"):
        box()
        c.circle(f.p(0.0, 0.0), max(4.0, view.s(min(f.w, f.d) * 0.28)), stroke=FURNITURE, width=1.0)
        c.text(f.p(0.0, -0.62), "REF" if kind == "Refrigerator" else "W/M", size=11,
               anchor="mm", color=GREY)

    elif kind == "Bed":
        box()
        # Headboard, then two pillows, then the turned-down sheet line.
        c.polygon([f.p(-0.5, -0.5), f.p(0.5, -0.5), f.p(0.5, -0.42), f.p(-0.5, -0.42)],
                  fill=LIGHT, stroke=FURNITURE, width=1.0)
        for u in (-0.24, 0.24):
            c.polygon([f.p(u - 0.2, -0.38), f.p(u + 0.2, -0.38), f.p(u + 0.2, -0.2), f.p(u - 0.2, -0.2)],
                      stroke=FURNITURE, width=1.0)
        c.line(f.p(-0.5, 0.06), f.p(0.5, 0.06), width=1.0, color=FURNITURE)

    elif kind == "Nightstand":
        box()
        c.line(f.p(-0.5, -0.5), f.p(0.5, 0.5), width=0.7, color=LIGHT)
        c.line(f.p(0.5, -0.5), f.p(-0.5, 0.5), width=0.7, color=LIGHT)

    elif kind == "Sofa":
        box()
        c.polygon([f.p(-0.5, -0.5), f.p(0.5, -0.5), f.p(0.5, -0.26), f.p(-0.5, -0.26)],
                  stroke=FURNITURE, width=1.0)        # back
        for u in (-0.5, 0.5):
            s = 0.12 if u < 0 else -0.12
            c.polygon([f.p(u, -0.5), f.p(u + s, -0.5), f.p(u + s, 0.5), f.p(u, 0.5)],
                      stroke=FURNITURE, width=1.0)    # arms
        _divisions(c, f, 3)

    elif kind in ("CoffeeTable", "DiningTable"):
        box()
        if kind == "DiningTable":
            # Chairs around the long sides.
            for u in (-0.28, 0.28):
                for v, sign in ((-0.5, -1.0), (0.5, 1.0)):
                    c.polygon([f.p(u - 0.16, v + sign * 0.06), f.p(u + 0.16, v + sign * 0.06),
                               f.p(u + 0.16, v + sign * 0.34), f.p(u - 0.16, v + sign * 0.34)],
                              stroke=GREY, width=1.0)

    elif kind == "Chair":
        box()

    elif kind == "WC":
        # Cistern against the wall, bowl in front.
        c.polygon([f.p(-0.5, -0.5), f.p(0.5, -0.5), f.p(0.5, -0.24), f.p(-0.5, -0.24)],
                  stroke=FURNITURE, width=1.2)
        c.circle(f.p(0.0, 0.14), max(4.0, view.s(min(f.w, f.d) * 0.36)), stroke=FURNITURE, width=1.2)

    elif kind == "Basin":
        c.circle(f.p(0.0, 0.0), max(4.0, view.s(min(f.w, f.d) * 0.45)), stroke=FURNITURE, width=1.2)
        c.circle(f.p(0.0, -0.36), max(2.5, view.s(35)), stroke=FURNITURE, width=1.0)

    elif kind in ("Shower", "ShowerPartition"):
        box(dash=DASH_FINE)
        c.line(f.p(-0.5, -0.5), f.p(0.5, 0.5), width=0.9, color=LIGHT)
        c.line(f.p(0.5, -0.5), f.p(-0.5, 0.5), width=0.9, color=LIGHT)
        c.circle(f.p(0.0, 0.0), max(3.0, view.s(60)), stroke=FURNITURE, width=1.0)

    elif kind == "CeilingFan":
        r = max(6.0, view.s(params.get("diameter", 1200) / 2.0))
        cp = f.p(0.0, 0.0)
        c.circle(cp, r, stroke=CEILING, width=1.2, dash=DASH_CEILING)
        for a in (90, 210, 330):
            t = math.radians(a)
            c.line(cp, (cp[0] + r * math.cos(t), cp[1] - r * math.sin(t)), width=1.6, color=CEILING)
        c.circle(cp, max(2.5, r * 0.12), fill=CEILING, stroke=CEILING, width=1.0)

    elif kind == "LightFixture":
        cp = f.p(0.0, 0.0)
        r = max(4.0, view.s(120))
        c.circle(cp, r, stroke=CEILING, width=1.2)
        c.line((cp[0] - r, cp[1]), (cp[0] + r, cp[1]), width=1.0, color=CEILING)
        c.line((cp[0], cp[1] - r), (cp[0], cp[1] + r), width=1.0, color=CEILING)

    elif kind == "SwitchPlate":
        cp = f.p(0.0, 0.0)
        r = max(4.0, view.s(140))
        c.circle(cp, r, fill=WHITE, stroke=ACCENT, width=1.4)
        c.line(cp, (cp[0] + r * 1.7, cp[1] - r * 1.7), width=1.2, color=ACCENT)
        c.text((cp[0] + r * 1.9, cp[1] - r * 2.1), f"S{params.get('gangCount', '')}",
               size=11, anchor="lm", color=ACCENT)

    elif kind == "Curtain":
        c.polyline([f.p(-0.5 + i / 12.0, 0.4 * math.sin(i * 1.4)) for i in range(13)],
                   width=1.2, color=GREY)

    # ------------------------------------------------------------------ electrical services
    elif kind == "PowerSocket":
        # Conventional plan symbol: a half-disc on the wall with a stem.
        cp = f.p(0.0, 0.0)
        r = max(4.0, view.s(150))
        c.circle(cp, r, fill=WHITE, stroke=ACCENT, width=1.4)
        c.line((cp[0] - r, cp[1]), (cp[0] + r, cp[1]), width=1.2, color=ACCENT)
        c.line((cp[0], cp[1]), (cp[0], cp[1] - r * 1.9), width=1.2, color=ACCENT)
        c.text((cp[0] + r * 1.4, cp[1] + r * 1.4), str(params.get("gangCount", 2)),
               size=10, anchor="lm", color=ACCENT)

    elif kind == "DistributionBoard":
        c.polygon(outline, fill=WHITE, stroke=ACCENT, width=1.8)
        cp = f.p(0.0, 0.0)
        c.text((cp[0], cp[1]), "DB", size=12, anchor="mm", bold=True, color=ACCENT)

    elif kind == "ACIndoorUnit":
        box(w=1.6)
        for v in (-0.15, 0.15):
            c.line(f.p(-0.45, v), f.p(0.45, v), width=0.8, color=LIGHT)
        c.text(f.p(0.0, -0.9), "AC", size=11, anchor="mm", color=ACCENT)

    elif kind == "ACOutdoorUnit":
        box(w=1.6)
        c.circle(f.p(0.0, 0.0), max(4.0, view.s(min(f.w, f.d) * 0.35)), stroke=FURNITURE, width=1.2)
        c.text(f.p(0.0, -0.85), "ODU", size=10, anchor="mm", color=GREY)

    elif kind == "Geyser":
        c.circle(f.p(0.0, 0.0), max(5.0, view.s(min(f.w, f.d) * 0.5)),
                 fill=WHITE, stroke=FURNITURE, width=1.4)
        c.text(f.p(0.0, 0.0), "G", size=11, anchor="mm", bold=True, color=FURNITURE)

    elif kind == "ExhaustFan":
        cp = f.p(0.0, 0.0)
        r = max(4.0, view.s(180))
        c.circle(cp, r, fill=WHITE, stroke=ACCENT, width=1.3)
        for a in (45, 135, 225, 315):
            t = math.radians(a)
            c.line(cp, (cp[0] + r * math.cos(t), cp[1] - r * math.sin(t)), width=1.0, color=ACCENT)
        c.text((cp[0] + r * 1.5, cp[1]), "EF", size=10, anchor="lm", color=ACCENT)

    # ---------------------------------------------------------------- architectural fittings
    elif kind == "ShoeRack":
        box()
        _divisions(c, f, max(params.get("shutterCount", 2), 2), along_width=f.w >= f.d)

    elif kind == "Pelmet":
        box(dash=DASH_HIDDEN, w=1.0)

    elif kind == "Mirror":
        c.polygon(outline, fill=(232, 240, 245), stroke=FURNITURE, width=1.2)
        # Diagonal streaks are the usual mark for glass in plan.
        for t in (-0.3, 0.0, 0.3):
            c.line(f.p(t - 0.12, -0.5), f.p(t + 0.12, 0.5), width=0.7, color=LIGHT)

    elif kind == "TowelRail":
        c.line(f.p(-0.5, 0.0), f.p(0.5, 0.0), width=2.0, color=FURNITURE)
        for u in (-0.5, 0.5):
            c.line(f.p(u, -0.5), f.p(u, 0.5), width=1.2, color=FURNITURE)

    elif kind == "Railing":
        c.line(f.p(-0.5, 0.0), f.p(0.5, 0.0), width=1.6, color=FURNITURE)
        n = max(4, int(f.w / 300.0))
        for i in range(n + 1):
            u = -0.5 + i / n
            c.line(f.p(u, -0.4), f.p(u, 0.4), width=0.8, color=GREY)

    elif kind == "WallNiche":
        box(dash=DASH_HIDDEN, w=1.0)
        _divisions(c, f, max(params.get("shelfCount", 2), 2),
                   along_width=f.w >= f.d, dash=DASH_HIDDEN, color=GREY)

    else:
        box(dash=DASH_FINE)


# --------------------------------------------------------------------------------- sheet 1 & 2

def sheet_layout(spec, view, with_furniture, sheet_no, total):
    title = "Furniture Layout Plan" if with_furniture else "Blank Layout Plan"
    c = Canvas(SHEET_W, SHEET_H, f"{spec.get('name')} - {title}")
    sheet_frame(c, sheet_no, title, spec, total)

    draw_walls(c, spec, view)
    draw_openings(c, spec, view)
    draw_columns(c, spec, view)

    if with_furniture:
        for fx in spec["fixtures"]:
            if fx["type"] in CEILING_MOUNTED:
                continue
            draw_fixture_plan(c, fx, view)

    draw_room_labels(c, spec, view, with_area=not with_furniture)
    grid_dimensions(c, spec, view)

    north_arrow(c, PLAN_AREA[2] - 40, PLAN_AREA[1] + 46)
    scale_bar(c, view, INNER + 40, SHEET_H - INNER - 66)

    if with_furniture:
        legend(c, spec, INNER + 40, PLAN_AREA[1] + 20)
    else:
        opening_schedule(c, spec, INNER + 40, PLAN_AREA[1] + 20)

    return c


def opening_schedule(c, spec, x, y):
    """Door and window schedule, the way a real layout sheet carries one."""
    groups = {}
    for o in spec["openings"]:
        key = (o["kind"], int(o["width"]), int(o["height"]), int(o.get("sillHeight", 0)))
        groups.setdefault(key, []).append(o["id"])

    c.text((x, y), "DOOR / WINDOW SCHEDULE", size=15, anchor="lt", bold=True)
    c.line((x, y + 24), (x + 430, y + 24), width=1.2)
    c.text((x, y + 34), "TYPE", size=12, anchor="lt", color=GREY)
    c.text((x + 150, y + 34), "W x H", size=12, anchor="lt", color=GREY)
    c.text((x + 285, y + 34), "SILL", size=12, anchor="lt", color=GREY)
    c.text((x + 355, y + 34), "QTY", size=12, anchor="lt", color=GREY)
    c.line((x, y + 54), (x + 430, y + 54), width=0.8, color=LIGHT)

    row = y + 64
    for (kind, w, h, sill), ids in sorted(groups.items()):
        c.text((x, row), kind, size=13, anchor="lt")
        c.text((x + 150, row), f"{w} x {h}", size=13, anchor="lt")
        c.text((x + 285, row), str(sill), size=13, anchor="lt")
        c.text((x + 355, row), str(len(ids)), size=13, anchor="lt")
        row += 26
    c.line((x, row + 2), (x + 430, row + 2), width=1.2)


def spaced(kind):
    return "".join((" " + ch) if ch.isupper() else ch for ch in kind).strip().upper()


def legend(c, spec, x, y):
    counts = {}
    for fx in spec["fixtures"]:
        if fx["type"] in CEILING_MOUNTED:
            continue
        counts[fx["type"]] = counts.get(fx["type"], 0) + 1

    c.text((x, y), "FURNITURE / JOINERY SCHEDULE", size=15, anchor="lt", bold=True)
    c.line((x, y + 24), (x + 360, y + 24), width=1.2)

    row = y + 36
    for kind, n in sorted(counts.items()):
        c.text((x, row), spaced(kind), size=11, anchor="lt")
        c.text((x + 320, row), f"x{n}", size=11, anchor="lt", color=DIM)
        row += 19


# ----------------------------------------------------------------------------------- sheet 3

def sheet_ceiling(spec, view, sheet_no, total):
    title = "Reflected Ceiling Plan"
    c = Canvas(SHEET_W, SHEET_H, f"{spec.get('name')} - {title}")
    sheet_frame(c, sheet_no, title, spec, total)

    # Walls in outline only: on an RCP the ceiling is the subject, the shell is reference.
    draw_walls(c, spec, view, fill=(225, 225, 225))
    draw_openings(c, spec, view, show_swings=False)
    draw_columns(c, spec, view)

    # Beams first, so the ceiling regions that conceal them read as being in front.
    draw_beams(c, spec, view)

    rooms = {r["id"]: r for r in spec["rooms"]}
    labelled: set[str] = set()

    for fc in spec["falseCeilings"]:
        style = fc["style"]
        if style == "None":
            continue

        poly = [pt(p) for p in fc["explicitPolygon"]] if fc.get("explicitPolygon") else None
        room = rooms.get(fc["roomId"])
        if poly is None:
            if room is None:
                continue
            poly = [pt(p) for p in room["boundary"]]

        c.polygon([view(p) for p in poly], fill=None, stroke=CEILING, width=2.0)

        if style in ("Peripheral", "Cove", "Tray"):
            inner = _inset_rect(poly, fc.get("bandWidth", 0.0))
            if inner:
                c.polygon([view(p) for p in inner], fill=None, stroke=CEILING,
                          width=1.4, dash=DASH_CEILING)
            if style == "Cove":
                cove = _inset_rect(poly, fc.get("bandWidth", 0.0) - fc.get("cove", {}).get("setback", 0.0))
                if cove:
                    c.polygon([view(p) for p in cove], fill=None, stroke=ACCENT, width=2.4)

        if room is not None:
            labelled.add(room["id"])
            # The label block sits in the upper part of the room rather than dead centre, which is
            # where the ceiling fan symbol lives.
            x0, y0, x1, y1 = room_bounds(room)
            cx = view(((x0 + x1) / 2.0, y1))[0]
            top_y = view(((x0 + x1) / 2.0, y1))[1]
            bot_y = view(((x0 + x1) / 2.0, y0))[1]
            ly = top_y + (bot_y - top_y) * 0.2

            drop = int(round(fc.get("drop", 0)))
            label = "".join((" " + ch) if ch.isupper() else ch for ch in style).strip().upper()
            c.text((cx, ly), room["name"].upper(), size=15, anchor="mm", bold=True)
            c.text((cx, ly + 21), label, size=13, anchor="mm", bold=True, color=CEILING)
            c.text((cx, ly + 40), f"DROP {drop}", size=12, anchor="mm", color=DIM)

        for lp in fc.get("lightPositions", []):
            p = view(pt(lp))
            r = max(4.0, view.s(110))
            c.circle(p, r, fill=WHITE, stroke=CEILING, width=1.4)
            c.line((p[0] - r, p[1]), (p[0] + r, p[1]), width=1.0, color=CEILING)
            c.line((p[0], p[1] - r), (p[0], p[1] + r), width=1.0, color=CEILING)

    for fx in spec["fixtures"]:
        if fx["type"] in ("CeilingFan", "LightFixture", "SwitchPlate"):
            draw_fixture_plan(c, fx, view)

    # Rooms open to the slab still need naming, but carry no ceiling data to stack under it.
    for r in spec["rooms"]:
        if r["id"] in labelled:
            continue
        x0, y0, x1, y1 = room_bounds(r)
        cx, cy = view(((x0 + x1) / 2.0, (y0 + y1) / 2.0))
        c.text((cx, cy), r["name"].upper(), size=15, anchor="mm", bold=True)

    north_arrow(c, PLAN_AREA[2] - 40, PLAN_AREA[1] + 46)
    scale_bar(c, view, INNER + 40, SHEET_H - INNER - 66)
    ceiling_legend(c, INNER + 40, PLAN_AREA[1] + 20)
    return c


def sheet_electrical(spec, view, sheet_no, total):
    """Electrical layout: points, switching and services, on a deliberately faint shell."""
    title = "Electrical Layout Plan"
    c = Canvas(SHEET_W, SHEET_H, f"{spec.get('name')} - {title}")
    sheet_frame(c, sheet_no, title, spec, total)

    # The shell is reference here; the services are the subject.
    draw_walls(c, spec, view, fill=(225, 225, 225))
    draw_openings(c, spec, view, show_swings=False)
    draw_columns(c, spec, view)

    for fx in spec["fixtures"]:
        if fx["type"] in ELECTRICAL_TYPES:
            draw_fixture_plan(c, fx, view)

    # Recessed lights are recorded on the ceilings, not as fixtures, but they are electrical
    # points and belong on this sheet too.
    for fc in spec.get("falseCeilings", []):
        for lp in fc.get("lightPositions", []):
            p = view(pt(lp))
            r = max(4.0, view.s(110))
            c.circle(p, r, fill=WHITE, stroke=CEILING, width=1.3)
            c.line((p[0] - r, p[1]), (p[0] + r, p[1]), width=1.0, color=CEILING)
            c.line((p[0], p[1] - r), (p[0], p[1] + r), width=1.0, color=CEILING)

    for r in spec["rooms"]:
        x0, y0, x1, y1 = room_bounds(r)
        cx, cy = view(((x0 + x1) / 2.0, (y0 + y1) / 2.0))
        c.text((cx, cy), r["name"].upper(), size=14, anchor="mm", bold=True, color=GREY)

    north_arrow(c, PLAN_AREA[2] - 40, PLAN_AREA[1] + 46)
    scale_bar(c, view, INNER + 40, SHEET_H - INNER - 66)
    electrical_legend(c, spec, INNER + 40, PLAN_AREA[1] + 20)
    return c


def electrical_legend(c, spec, x, y):
    c.text((x, y), "ELECTRICAL LEGEND", size=15, anchor="lt", bold=True)
    c.line((x, y + 24), (x + 380, y + 24), width=1.2)

    def socket(yy):
        c.circle((x + 20, yy), 8, fill=WHITE, stroke=ACCENT, width=1.4)
        c.line((x + 12, yy), (x + 28, yy), width=1.2, color=ACCENT)
        c.line((x + 20, yy), (x + 20, yy - 15), width=1.2, color=ACCENT)

    def exhaust(yy):
        c.circle((x + 20, yy), 8, fill=WHITE, stroke=ACCENT, width=1.3)
        for a in (45, 135, 225, 315):
            t = math.radians(a)
            c.line((x + 20, yy), (x + 20 + 8 * math.cos(t), yy - 8 * math.sin(t)),
                   width=1.0, color=ACCENT)

    def fan(yy):
        c.circle((x + 20, yy), 11, stroke=CEILING, width=1.2, dash=DASH_CEILING)
        for a in (90, 210, 330):
            t = math.radians(a)
            c.line((x + 20, yy), (x + 20 + 11 * math.cos(t), yy - 11 * math.sin(t)),
                   width=1.6, color=CEILING)

    def light(yy):
        c.circle((x + 20, yy), 8, fill=WHITE, stroke=CEILING, width=1.3)
        c.line((x + 12, yy), (x + 28, yy), width=1.0, color=CEILING)
        c.line((x + 20, yy - 8), (x + 20, yy + 8), width=1.0, color=CEILING)

    rows = [
        ("Power socket (gang count noted)", socket),
        ("Switch plate", lambda yy: c.circle((x + 20, yy), 8, fill=WHITE, stroke=ACCENT, width=1.4)),
        ("Distribution board", lambda yy: (c.rect(x + 6, yy - 8, 28, 16, fill=WHITE, stroke=ACCENT, width=1.6),
                                           c.text((x + 20, yy), "DB", size=9, anchor="mm", bold=True, color=ACCENT))),
        ("Split AC indoor unit", lambda yy: c.rect(x + 4, yy - 6, 32, 12, stroke=FURNITURE, width=1.5)),
        ("AC outdoor unit", lambda yy: (c.rect(x + 4, yy - 8, 32, 16, stroke=FURNITURE, width=1.5),
                                        c.circle((x + 20, yy), 5, stroke=FURNITURE, width=1.0))),
        ("Water heater", lambda yy: (c.circle((x + 20, yy), 9, fill=WHITE, stroke=FURNITURE, width=1.4),
                                     c.text((x + 20, yy), "G", size=10, anchor="mm", bold=True, color=FURNITURE))),
        ("Exhaust fan", exhaust),
        ("Ceiling fan point", fan),
        ("Recessed light point", light),
    ]

    row = y + 46
    for label, draw_symbol in rows:
        draw_symbol(row)
        c.text((x + 46, row), label.upper(), size=11, anchor="lm")
        row += 28

    # Point count per room, which is what an electrician actually works from.
    counts = {}
    for fx in spec["fixtures"]:
        if fx["type"] in ELECTRICAL_TYPES:
            counts[fx["type"]] = counts.get(fx["type"], 0) + 1

    light_points = sum(len(fc.get("lightPositions", [])) for fc in spec.get("falseCeilings", []))
    if light_points:
        counts["RecessedLight"] = light_points

    row += 16
    c.text((x, row), "POINT SCHEDULE", size=14, anchor="lt", bold=True)
    c.line((x, row + 22), (x + 380, row + 22), width=1.2)
    row += 34
    for kind, n in sorted(counts.items()):
        c.text((x, row), spaced(kind), size=11, anchor="lt")
        c.text((x + 340, row), f"x{n}", size=11, anchor="lt", color=DIM)
        row += 19


def _inset_rect(poly, amount):
    """Insets an axis-aligned polygon by its bounding box. Enough for the rectangular rooms in
    this plan; true polygon offset lives in the C++ generator, where concave rooms matter."""
    if amount <= 0:
        return None
    xs = [p[0] for p in poly]
    ys = [p[1] for p in poly]
    x0, x1 = min(xs) + amount, max(xs) - amount
    y0, y1 = min(ys) + amount, max(ys) - amount
    if x1 <= x0 or y1 <= y0:
        return None
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def ceiling_legend(c, x, y):
    c.text((x, y), "CEILING LEGEND", size=15, anchor="lt", bold=True)
    c.line((x, y + 24), (x + 360, y + 24), width=1.2)

    rows = [
        ("Ceiling outline", lambda yy: c.line((x, yy), (x + 46, yy), width=2.0, color=CEILING)),
        ("Inner band edge", lambda yy: c.line((x, yy), (x + 46, yy), width=1.4, color=CEILING, dash=DASH_CEILING)),
        ("Cove / LED channel", lambda yy: c.line((x, yy), (x + 46, yy), width=2.4, color=ACCENT)),
        ("Recessed light", lambda yy: (c.circle((x + 23, yy), 8, fill=WHITE, stroke=CEILING, width=1.4),
                                       c.line((x + 15, yy), (x + 31, yy), width=1.0, color=CEILING),
                                       c.line((x + 23, yy - 8), (x + 23, yy + 8), width=1.0, color=CEILING))),
        ("Ceiling fan", lambda yy: (c.circle((x + 23, yy), 11, stroke=CEILING, width=1.2, dash=DASH_CEILING),
                                    c.line((x + 23, yy), (x + 23, yy - 11), width=1.6, color=CEILING),
                                    c.line((x + 23, yy), (x + 13, yy + 6), width=1.6, color=CEILING),
                                    c.line((x + 23, yy), (x + 33, yy + 6), width=1.6, color=CEILING))),
        ("Switch plate", lambda yy: c.circle((x + 23, yy), 8, fill=WHITE, stroke=ACCENT, width=1.4)),
        ("Beam over (width x depth)", lambda yy: (c.line((x, yy - 4), (x + 46, yy - 4), width=1.3, color=ACCENT, dash=DASH_CEILING),
                                                  c.line((x, yy + 4), (x + 46, yy + 4), width=1.3, color=ACCENT, dash=DASH_CEILING))),
        ("Column", lambda yy: (c.rect(x + 12, yy - 8, 22, 16, fill=(70, 70, 70), stroke=BLACK, width=1.2),
                               c.line((x + 12, yy - 8), (x + 34, yy + 8), width=0.9, color=WHITE),
                               c.line((x + 34, yy - 8), (x + 12, yy + 8), width=0.9, color=WHITE))),
    ]

    row = y + 46
    for label, draw_symbol in rows:
        draw_symbol(row)
        c.text((x + 66, row), label.upper(), size=12, anchor="lm")
        row += 30


# ----------------------------------------------------------------------------------- elevations

WALL_LETTERS = ["A", "B", "C", "D"]


def room_wall_segments(room):
    """The four sides of a room, as (letter, direction, fixed coord, range, outward normal)."""
    x0, y0, x1, y1 = room_bounds(room)
    return [
        ("A", "h", y0, (x0, x1), (0.0, -1.0)),    # looking north at the south wall
        ("B", "v", x1, (y0, y1), (1.0, 0.0)),     # looking west at the east wall
        ("C", "h", y1, (x1, x0), (0.0, 1.0)),     # looking south at the north wall
        ("D", "v", x0, (y1, y0), (-1.0, 0.0)),    # looking east at the west wall
    ]


def openings_on_segment(spec, seg, tol=200.0):
    """Openings hosted on a wall collinear with this room side and overlapping its span."""
    letter, axis, fixed, (r0, r1), _ = seg
    lo, hi = min(r0, r1), max(r0, r1)
    found = []

    for o in spec["openings"]:
        wall = wall_by_id(spec, o["wallId"])
        if wall is None or wall_axis(wall) != axis:
            continue

        wfixed = wall["start"]["y"] if axis == "h" else wall["start"]["x"]
        if abs(wfixed - fixed) > tol:
            continue

        # Position of the opening centre along the shared axis.
        (sx, sy), (ex, ey) = pt(wall["start"]), pt(wall["end"])
        length = math.hypot(ex - sx, ey - sy)
        t = o["offsetAlongWall"] / length
        cx = sx + (ex - sx) * t
        cy = sy + (ey - sy) * t
        along = cx if axis == "h" else cy

        if lo - o["width"] / 2.0 <= along <= hi + o["width"] / 2.0:
            found.append((along, o))

    return found


def fixtures_on_segment(spec, room, seg, depth=1400.0):
    """Fixtures standing against this side of the room, nearest wall first."""
    letter, axis, fixed, (r0, r1), (nx, ny) = seg
    lo, hi = min(r0, r1), max(r0, r1)
    found = []

    for fx in spec["fixtures"]:
        if fx["roomId"] != room["id"] or fx["type"] in CEILING_MOUNTED:
            continue

        px, py = fx["position"]["x"], fx["position"]["y"]
        along = px if axis == "h" else py
        across = py if axis == "h" else px
        distance = abs(across - fixed)

        if distance > depth or not (lo - 300.0 <= along <= hi + 300.0):
            continue

        # Footprint extent along the elevation, accounting for rotation.
        rot = fx.get("rotationDegrees", 0.0)
        w, d = fx["footprint"]["x"], fx["footprint"]["y"]
        t = math.radians(rot)
        extent = abs(w * math.cos(t)) + abs(d * math.sin(t)) if axis == "h" else \
                 abs(w * math.sin(t)) + abs(d * math.cos(t))

        found.append((distance, along, extent, fx))

    found.sort(key=lambda item: -item[0])   # far items first, so near ones draw over them
    return found


def draw_fixture_elevation(c, fx, x0, x1, floor_y, px_per_mm):
    """A fixture seen face-on, with the joinery detail that makes it readable as what it is."""
    params = fx.get("params", {}) or {}
    kind = fx["type"]

    base = floor_y - fx.get("baseZ", 0.0) * px_per_mm
    top = base - fx["height"] * px_per_mm
    w = x1 - x0
    if w <= 1 or abs(base - top) < 1:
        return

    def panel(a, b, t, bo, dash=None, color=FURNITURE, width=1.2):
        c.polygon([(a, t), (b, t), (b, bo), (a, bo)], stroke=color, width=width, dash=dash)

    if kind in ("Wardrobe", "KitchenTallUnit", "Bookshelf", "TVUnit", "Vanity",
                "KitchenBaseCabinet", "KitchenWallCabinet", "StudyTable", "LoftUnit"):
        plinth = params.get("plinthHeight", 0.0) * px_per_mm
        cornice = params.get("corniceHeight", 0.0) * px_per_mm
        carcass_bottom = base - plinth
        carcass_top = top + cornice

        if plinth > 0.5:
            # Recessed toe-kick: inset so it reads as set back from the shutter face.
            c.polygon([(x0 + 4, carcass_bottom), (x1 - 4, carcass_bottom),
                       (x1 - 4, base), (x0 + 4, base)], fill=(238, 238, 238),
                      stroke=GREY, width=1.0)
        if cornice > 0.5:
            panel(x0 - 3, x1 + 3, top, carcass_top, color=FURNITURE, width=1.4)

        panel(x0, x1, carcass_top, carcass_bottom, width=1.6)

        shutters = params.get("shutterCount", 0)
        drawers = params.get("drawerCount", 0)
        body_top, body_bottom = carcass_top, carcass_bottom

        if drawers > 0:
            # Drawer bank occupies the lower part of the carcass.
            bank = min(0.55, 0.18 * drawers) * (body_bottom - body_top)
            bank_top = body_bottom - bank
            for i in range(drawers):
                dt = bank_top + (bank / drawers) * i
                db = bank_top + (bank / drawers) * (i + 1)
                panel(x0 + 3, x1 - 3, dt + 2, db - 2, width=1.0)
                _handle(c, params, (x0 + x1) / 2.0, (dt + db) / 2.0, w * 0.3, horizontal=True)
            body_bottom = bank_top

        if shutters > 0:
            step = w / shutters
            for i in range(shutters):
                a = x0 + step * i
                b = a + step
                panel(a + 3, b - 3, body_top + 2, body_bottom - 2, width=1.0)
                if params.get("bHasGlassInsert"):
                    panel(a + 12, b - 12, body_top + 12, body_bottom - 12,
                          color=LIGHT, width=0.8, dash=DASH_FINE)
                # Handles sit on the leading edge of each leaf, alternating like a real pair.
                hx = b - 14 if i % 2 == 0 else a + 14
                _handle(c, params, hx, (body_top + body_bottom) / 2.0, 0, horizontal=False)

        if params.get("shelfCount", 0) and shutters == 0:
            n = params["shelfCount"]
            for i in range(1, n + 1):
                sy = body_top + (body_bottom - body_top) * i / (n + 1)
                c.line((x0 + 4, sy), (x1 - 4, sy), width=0.9, color=LIGHT)

        if params.get("bHasLoft"):
            loft_h = params.get("loftHeight", 500.0) * px_per_mm
            panel(x0, x1, top - loft_h, top, width=1.4)
            _divisions_px(c, x0, x1, top - loft_h, top, max(shutters, 2))
            c.text(((x0 + x1) / 2.0, top - loft_h / 2.0), "LOFT", size=11, anchor="mm", color=GREY)

    elif kind == "CounterTop":
        panel(x0, x1, top, base, width=1.8)
        up = params.get("upstandHeight", 0.0) * px_per_mm
        if up > 0.5:
            panel(x0, x1, top - up, top, color=GREY, width=1.0)

    elif kind == "Bed":
        head = top - fx["height"] * px_per_mm * 0.55
        panel(x0, x1, top, base, width=1.4)
        panel(x0 + w * 0.06, x1 - w * 0.06, head, top, width=1.2)   # headboard
        c.line((x0, top + (base - top) * 0.35), (x1, top + (base - top) * 0.35),
               width=1.0, color=LIGHT)

    elif kind in ("Sofa", "CoffeeTable", "DiningTable", "Chair", "Nightstand"):
        panel(x0, x1, top, base, width=1.3)

    elif kind == "WC":
        panel(x0 + w * 0.2, x1 - w * 0.2, top, base, width=1.3)
        panel(x0, x1, top - fx["height"] * px_per_mm * 0.9, top, color=GREY, width=1.0)

    elif kind == "Basin":
        panel(x0, x1, top, base, width=1.3)
        c.line((x0, (top + base) / 2.0), (x1, (top + base) / 2.0), width=0.9, color=LIGHT)

    elif kind in ("Shower", "ShowerPartition"):
        panel(x0, x1, top, base, dash=DASH_FINE, color=LIGHT, width=1.0)
        c.circle(((x0 + x1) / 2.0, top + 14), 9, stroke=FURNITURE, width=1.2)

    elif kind in ("Refrigerator", "WashingMachine", "Chimney", "Hob", "Sink"):
        panel(x0, x1, top, base, width=1.3)
        if kind in ("Refrigerator", "WashingMachine"):
            c.circle(((x0 + x1) / 2.0, (top + base) / 2.0), min(w, abs(base - top)) * 0.22,
                     stroke=FURNITURE, width=1.0)

    elif kind in ("SwitchPlate", "PowerSocket", "DistributionBoard"):
        panel(x0, x1, top, base, color=ACCENT, width=1.4)
        gangs = params.get("gangCount", 0)
        if gangs > 1 and (x1 - x0) > 12:
            _divisions_px(c, x0, x1, top, base, gangs)

    elif kind == "ACIndoorUnit":
        panel(x0, x1, top, base, width=1.6)
        for t in (0.35, 0.65):
            yy = top + (base - top) * t
            c.line((x0 + 4, yy), (x1 - 4, yy), width=0.9, color=LIGHT)

    elif kind == "ACOutdoorUnit":
        panel(x0, x1, top, base, width=1.5)
        c.circle(((x0 + x1) / 2.0, (top + base) / 2.0),
                 min(x1 - x0, abs(base - top)) * 0.3, stroke=FURNITURE, width=1.1)

    elif kind == "Geyser":
        panel(x0, x1, top, base, width=1.4)
        c.text(((x0 + x1) / 2.0, (top + base) / 2.0), "G", size=11, anchor="mm",
               bold=True, color=FURNITURE)

    elif kind == "ExhaustFan":
        c.circle(((x0 + x1) / 2.0, (top + base) / 2.0),
                 min(x1 - x0, abs(base - top)) * 0.45, fill=WHITE, stroke=ACCENT, width=1.3)

    elif kind == "Mirror":
        c.polygon([(x0, top), (x1, top), (x1, base), (x0, base)],
                  fill=(232, 240, 245), stroke=FURNITURE, width=1.3)
        for t in (0.2, 0.5, 0.8):
            xx = x0 + (x1 - x0) * t
            c.line((xx - 10, base), (xx + 10, top), width=0.8, color=LIGHT)

    elif kind == "TowelRail":
        c.line((x0, (top + base) / 2.0), (x1, (top + base) / 2.0), width=2.2, color=FURNITURE)
        for xx in (x0, x1):
            c.line((xx, top), (xx, base), width=1.2, color=FURNITURE)

    elif kind == "Railing":
        # Balusters between a top rail and the parapet coping.
        c.line((x0, top), (x1, top), width=2.0, color=FURNITURE)
        n = max(4, int((x1 - x0) / 22))
        for i in range(n + 1):
            xx = x0 + (x1 - x0) * i / n
            c.line((xx, top), (xx, base), width=1.0, color=GREY)

    elif kind == "Pelmet":
        panel(x0, x1, top, base, width=1.3)
        c.line((x0, base), (x1, base), width=1.0, color=GREY)

    elif kind in ("ShoeRack", "WallNiche"):
        panel(x0, x1, top, base, width=1.3)
        shelves = max(params.get("shelfCount", 2), 2)
        for i in range(1, shelves + 1):
            yy = top + (base - top) * i / (shelves + 1)
            c.line((x0 + 4, yy), (x1 - 4, yy), width=0.9, color=LIGHT)

    else:
        panel(x0, x1, top, base, dash=DASH_FINE, color=LIGHT, width=1.0)


def _divisions_px(c, x0, x1, top, bottom, n):
    if n <= 1:
        return
    step = (x1 - x0) / n
    for i in range(1, n):
        c.line((x0 + step * i, top), (x0 + step * i, bottom), width=0.9, color=FURNITURE)


def _handle(c, params, x, y, span, horizontal):
    style = params.get("handleStyle", "Bar")
    if style in ("None", "HandlelessGroove"):
        if style == "HandlelessGroove":
            c.line((x - 9, y), (x + 9, y), width=1.6, color=GREY)
        return
    if style == "Knob":
        c.circle((x, y), 3.6, fill=FURNITURE, stroke=FURNITURE, width=1.0)
    elif style == "JProfile":
        c.line((x - 6, y - 7), (x - 6, y + 7), width=2.0, color=FURNITURE)
    else:   # Bar
        if horizontal:
            c.line((x - max(10.0, span / 2.0), y), (x + max(10.0, span / 2.0), y),
                   width=2.4, color=FURNITURE)
        else:
            c.line((x, y - 16), (x, y + 16), width=2.4, color=FURNITURE)


def sheet_elevations(spec, room, sheet_no, total):
    title = f"Elevations - {room['name']}"
    c = Canvas(SHEET_W, SHEET_H, f"{spec.get('name')} - {title}")
    sheet_frame(c, sheet_no, title, spec, total)

    segments = room_wall_segments(room)
    ceiling_h = room.get("ceilingHeight", 3000.0)

    fc = next((f for f in spec["falseCeilings"]
               if f["roomId"] == room["id"] and f["style"] != "None"), None)

    # 2x2 grid of elevation frames.
    gx0, gy0 = 150, 190
    gw = (SHEET_W - 300) / 2.0
    gh = (SHEET_H - 190 - 320) / 2.0

    widest = max(abs(seg[3][1] - seg[3][0]) for seg in segments)
    px_per_mm = min((gw - 220) / widest, (gh - 170) / (ceiling_h * 1.25))

    for i, seg in enumerate(segments):
        letter, axis, fixed, (r0, r1), _ = seg
        cell_x = gx0 + (i % 2) * gw
        cell_y = gy0 + (i // 2) * gh

        span = abs(r1 - r0)
        ew = span * px_per_mm
        eh = ceiling_h * px_per_mm

        x0 = cell_x + (gw - ew) / 2.0
        floor_y = cell_y + (gh - eh) / 2.0 + eh
        ceil_y = floor_y - eh

        # Wall face, floor line, slab line.
        c.rect(x0, ceil_y, ew, eh, fill=(250, 250, 250), stroke=BLACK, width=1.6)
        c.line((x0 - 34, floor_y), (x0 + ew + 34, floor_y), width=3.0)
        c.line((x0 - 20, ceil_y), (x0 + ew + 20, ceil_y), width=1.4, color=GREY)

        if fc is not None:
            fy = ceil_y + fc["drop"] * px_per_mm
            c.line((x0, fy), (x0 + ew, fy), width=1.8, color=CEILING, dash=DASH_CEILING)
            c.text((x0 + ew + 8, fy), f"FC {int(round(ceiling_h - fc['drop']))}",
                   size=11, anchor="lm", color=CEILING)

        # Skirting.
        skirt = room.get("skirtingHeight", 0.0)
        if skirt > 0:
            c.rect(x0, floor_y - skirt * px_per_mm, ew, skirt * px_per_mm,
                   fill=(235, 235, 235), stroke=GREY, width=1.0)

        # Elevation runs left-to-right as seen; r0 is the left-hand end.
        def to_px(along):
            t = (along - r0) / (r1 - r0) if r1 != r0 else 0.0
            return x0 + t * ew

        for distance, along, extent, fx in fixtures_on_segment(spec, room, seg):
            a, b = sorted((to_px(along - extent / 2.0), to_px(along + extent / 2.0)))
            a = max(a, x0)
            b = min(b, x0 + ew)
            if b - a > 2:
                draw_fixture_elevation(c, fx, a, b, floor_y, px_per_mm)

        for along, o in openings_on_segment(spec, seg):
            a, b = sorted((to_px(along - o["width"] / 2.0), to_px(along + o["width"] / 2.0)))
            a, b = max(a, x0), min(b, x0 + ew)
            if b - a <= 2:
                continue
            sill = o.get("sillHeight", 0.0)
            oy1 = floor_y - sill * px_per_mm
            oy0 = oy1 - o["height"] * px_per_mm
            c.rect(a, oy0, b - a, oy1 - oy0, fill=WHITE, stroke=BLACK, width=1.8)
            if o["kind"] in ("Window", "SlidingWindow", "Ventilator"):
                c.line(((a + b) / 2.0, oy0), ((a + b) / 2.0, oy1), width=1.2)
                c.line((a, (oy0 + oy1) / 2.0), (b, (oy0 + oy1) / 2.0), width=1.0, color=LIGHT)
            else:
                c.rect(a + 7, oy0 + 7, (b - a) - 14, (oy1 - oy0) - 14, stroke=GREY, width=1.0)
                c.circle((b - 18, (oy0 + oy1) / 2.0), 3.4, fill=FURNITURE, stroke=FURNITURE, width=1.0)
            c.text(((a + b) / 2.0, oy0 - 14), o["id"], size=11, anchor="mm", color=DIM)

        # Width below, height at the side.
        dy = floor_y + 40
        c.line((x0, dy), (x0 + ew, dy), width=1.2, color=DIM)
        c.line((x0, floor_y), (x0, dy + 8), width=0.8, color=LIGHT)
        c.line((x0 + ew, floor_y), (x0 + ew, dy + 8), width=0.8, color=LIGHT)
        c.text((x0 + ew / 2.0, dy - 14), f"{int(round(span))}", size=14, anchor="mm", color=DIM)

        dx = x0 - 44
        c.line((dx, ceil_y), (dx, floor_y), width=1.2, color=DIM)
        c.text((dx - 14, (ceil_y + floor_y) / 2.0), f"{int(round(ceiling_h))}",
               size=14, anchor="mm", color=DIM, rotate=90)

        facing = {"A": "LOOKING NORTH", "B": "LOOKING WEST",
                  "C": "LOOKING SOUTH", "D": "LOOKING EAST"}[letter]
        c.text((cell_x + gw / 2.0, cell_y + gh - 54), f"ELEVATION {letter}",
               size=19, anchor="mm", bold=True)
        c.text((cell_x + gw / 2.0, cell_y + gh - 28), facing, size=12, anchor="mm", color=GREY)

    x0r, y0r, x1r, y1r = room_bounds(room)
    area = abs(_polygon_area([pt(p) for p in room["boundary"]])) / 1_000_000.0
    c.text((INNER + 40, SHEET_H - INNER - 74),
           f"{room['name'].upper()}   {int(round(x1r - x0r))} x {int(round(y1r - y0r))}   "
           f"{area:.2f} SQM   CEILING {int(round(ceiling_h))}",
           size=15, anchor="lm")

    return c


# ----------------------------------------------------------------------------------------- main

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    plugin = os.path.dirname(here)

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--spec", default=os.path.join(plugin, "Reference", "Specs", "Sample2BHK.json"))
    ap.add_argument("--out", default=os.path.join(plugin, "Reference", "Drawings", "Sample2BHK"))
    ap.add_argument("--svg-only", action="store_true",
                    help="Skip PNG rendering (useful when Pillow is unavailable).")
    args = ap.parse_args()

    spec = load_spec(args.spec)
    os.makedirs(args.out, exist_ok=True)

    view = PlanView(spec)

    # One elevation sheet per room worth drawing. Circulation and service spaces with no joinery
    # would produce four blank walls, so they are skipped rather than padding the set.
    elevation_rooms = [r for r in spec["rooms"]
                       if r["type"] in ("Living", "Dining", "Kitchen", "Bedroom", "MasterBedroom",
                                        "Bathroom", "Foyer")]
    total = 4 + len(elevation_rooms)

    sheets = [
        ("01-blank-layout", lambda n: sheet_layout(spec, view, False, n, total)),
        ("02-furniture-layout", lambda n: sheet_layout(spec, view, True, n, total)),
        ("03-reflected-ceiling-plan", lambda n: sheet_ceiling(spec, view, n, total)),
        ("04-electrical-layout", lambda n: sheet_electrical(spec, view, n, total)),
    ]
    for i, room in enumerate(elevation_rooms):
        slug = room["name"].lower().replace(" / ", "-").replace(" ", "-")
        sheets.append((f"{5 + i:02d}-elevations-{slug}",
                       (lambda r: (lambda n: sheet_elevations(spec, r, n, total)))(room)))

    for i, (name, build) in enumerate(sheets, start=1):
        canvas = build(i)
        svg_path = os.path.join(args.out, name + ".svg")
        with open(svg_path, "w", encoding="utf-8") as f:
            f.write(canvas.to_svg())

        if args.svg_only:
            print(f"  {name}.svg")
        else:
            png_path = os.path.join(args.out, name + ".png")
            canvas.to_png(png_path)
            print(f"  {name}.svg + .png")

    print(f"\n{len(sheets)} sheets written to {args.out}")


if __name__ == "__main__":
    main()
