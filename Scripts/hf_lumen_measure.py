"""Measure the Lumen comparison renders, and lay them out side by side.

Run outside the editor - reading PNGs needs numpy, and the editor's embedded python does not have it.

WHY THE LUMINANCE IS LINEARISED. The images are sRGB. Averaging sRGB bytes and calling the result
brightness overstates the dark images badly, because the transfer curve gives away most of its code
values to the bottom of the range: two configurations differing by 3x in light can differ by 1.5x in
mean byte value. Every figure here is converted to linear before it is averaged, then reported as a
Rec.709 luminance in 0..1.

WHAT THE BLUE/RED RATIO IS FOR. It is the cleanest single discriminator in the whole data set, and it
does not depend on exposure at all. A flat Lumen cannot see is lit by unoccluded SKY, which is blue;
a flat it can see is lit by its own warm fixtures. The broken configuration is the only one that
comes back above 1.0, and it is also the BRIGHTEST - which is the whole reason this milestone exists.

Regions are fractions of the frame rather than pixel rectangles, so they survive a change of
resolution and can be read off the contact sheet:

  ceiling  the top 5-20% of the frame
  wall     the left 3-15% of the frame, vertically centred
"""

import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

VIEWS = ("aerial", "mbed")

# The interior view is the one that MEASURES anything. The aerial frame contains sky, so its exposure
# is set by the sky and its numbers say nothing about bounce; it earns its place as the Lumen Scene
# visualisation - "is the flat in there at all" - and not as a photometer.
MEASURED_VIEW = "mbed"

# How much brighter the shadowed wall must be with the flat in the Lumen scene than without it, on
# the same tracing path. Measured 2.11x on the stand-in bake and 2.20x on the real one; 1.5x is a
# floor well clear of both, chosen so this fails on a REGRESSION rather than on noise.
MIN_BAKED_WALL_GAIN = 1.5

CONFIG_ORDER = ["A-live-software", "B-live-hardware", "C-baked-software", "D-baked-hardware"]

CONFIG_LABEL = {
    "A-live-software": "A  live meshes, software tracing",
    "B-live-hardware": "B  live meshes, hardware tracing (shipped default)",
    "C-baked-software": "C  BAKED, software tracing",
    "D-baked-hardware": "D  BAKED, hardware tracing",
}

REGIONS = {
    # name: (x0, x1, y0, y1) as fractions of width and height
    "whole_frame": (0.0, 1.0, 0.0, 1.0),
    "ceiling": (0.20, 0.80, 0.05, 0.20),
    "wall_left": (0.03, 0.15, 0.35, 0.65),
}


def linearise(srgb):
    """sRGB 0..1 -> linear 0..1, the exact piecewise transfer function, not a 2.2 power."""
    return np.where(srgb <= 0.04045, srgb / 12.92, ((srgb + 0.055) / 1.055) ** 2.4)


def load_linear(path):
    image = Image.open(path).convert("RGB")
    return linearise(np.asarray(image, dtype=np.float64) / 255.0)


def crop(array, region):
    height, width = array.shape[0], array.shape[1]
    x0, x1, y0, y1 = region
    return array[int(y0 * height):int(y1 * height), int(x0 * width):int(x1 * width), :]


def luminance(rgb):
    """Rec.709 luminance of already-linear data."""
    return float(0.2126 * rgb[..., 0].mean() + 0.7152 * rgb[..., 1].mean() + 0.0722 * rgb[..., 2].mean())


def pink_fraction(path):
    """Share of the frame the engine has painted 'missing Surface Cache coverage'.

    THE DIRECT MEASUREMENT OF "ARE THERE CARDS", and the only one here that does not depend on
    exposure at all. r.Lumen.Visualize 5 legends itself "Pink - missing Surface Cache coverage", so
    counting pink counts surfaces that are in the Lumen scene and contribute no radiance - which is
    exactly configuration B, the one that occludes correctly and lights nothing.

    Measured on the reference flat, interior view: 0.998 live against 0.0006 baked, a factor of about
    1600. Nothing else in this file separates two configurations that cleanly.

    Read on sRGB bytes rather than linearised, deliberately: this is a flat false-colour overlay the
    renderer draws at a fixed value, not a photometric quantity, and linearising it would only move
    the threshold about.

    ONE CAVEAT, AND IT IS WHY THE ASSERTION ONLY USES THE HARDWARE PAIR. Under software tracing a
    dynamic mesh is not in the Lumen scene at all, so there is no surface for the visualisation to
    report coverage for and the frame comes back white - pink 0.000, which reads like full coverage
    and is the opposite. Zero pink means "covered" only when the geometry is known to be traced.
    """
    rgb = np.asarray(Image.open(path).convert("RGB"), dtype=np.float64) / 255.0
    red, green, blue = rgb[..., 0], rgb[..., 1], rgb[..., 2]

    pink = (red > 0.35) & (blue > 0.35) & (green < 0.6 * np.minimum(red, blue))
    return round(float(pink.mean()), 4)


def measure(path):
    linear = load_linear(path)

    out = {}
    for name, region in REGIONS.items():
        out[name] = round(luminance(crop(linear, region)), 6)

    red = linear[..., 0].mean()
    blue = linear[..., 2].mean()
    out["blue_over_red"] = round(float(blue / red) if red > 1e-9 else 0.0, 4)

    return out


def contact_sheet(directory, view, which, out_path, results):
    """The four configurations of one pass, in a row, so the difference is looked at rather than read.

    Labelled, because an unlabelled row of four renders is the one artefact of this whole exercise
    most likely to be looked at later by somebody who does not have the table to hand - and the
    brightest tile in the row is the WRONG one, which is not a thing to leave to memory.
    """
    tiles = []
    for config in CONFIG_ORDER:
        path = os.path.join(directory, "{}__{}__{}.png".format(config, view, which))
        if os.path.exists(path):
            tiles.append((config, Image.open(path).convert("RGB")))

    if not tiles:
        return None

    width = 800
    band = 46
    scaled = [(config, tile.resize((width, int(tile.height * width / tile.width)))) for config, tile in tiles]
    height = max(tile.height for _, tile in scaled)

    sheet = Image.new("RGB", (width * len(scaled), height + band), (16, 16, 16))
    draw = ImageDraw.Draw(sheet)

    for index, (config, tile) in enumerate(scaled):
        x = index * width
        sheet.paste(tile, (x, band))

        row = results.get(config, {}).get(view, {})
        caption = CONFIG_LABEL.get(config, config)
        if row:
            caption += "   frame {:.3f}  wall {:.3f}  blue/red {:.2f}".format(
                row["whole_frame"], row["wall_left"], row["blue_over_red"])

        # Baked configurations in white, live ones in amber: the row then reads as two states rather
        # than four images, and the amber ones are the ones not to trust.
        colour = (255, 255, 255) if "baked" in config else (255, 176, 64)
        draw.text((x + 12, 14), caption, fill=colour)

    sheet.save(out_path)
    return out_path


def main():
    directory = sys.argv[1] if len(sys.argv) > 1 else "."

    results = {}
    for name in sorted(os.listdir(directory)):
        if not name.endswith("__beauty.png"):
            continue
        config, view, _ = name[:-4].split("__")
        row = measure(os.path.join(directory, name))

        surfcache = os.path.join(directory, "{}__{}__surfcache.png".format(config, view))
        row["uncached_fraction"] = pink_fraction(surfcache) if os.path.exists(surfcache) else None

        results.setdefault(config, {})[view] = row

    with open(os.path.join(directory, "measurements.json"), "w", encoding="utf-8") as handle:
        json.dump(results, handle, indent=1, sort_keys=True)

    for view in VIEWS:
        for which in ("beauty", "lumenscene"):
            sheet = contact_sheet(directory, view, which,
                                  os.path.join(directory, "COMPARISON__{}__{}.png".format(view, which)),
                                  results)
            if sheet:
                print("contact sheet: {}".format(sheet))

    # The table, printed, because the point of the exercise is the comparison and not the file.
    for view in VIEWS:
        present = [c for c in CONFIG_ORDER if c in results and view in results[c]]
        if not present:
            continue

        print("")
        print("{}   {:>12} {:>10} {:>10} {:>10} {:>12}".format(
            view.upper().ljust(50), "whole", "ceiling", "wall", "blue/red", "no cards"))
        for config in present:
            row = results[config][view]
            uncached = row.get("uncached_fraction")
            print("{}   {:>12.4f} {:>10.4f} {:>10.4f} {:>10.3f} {:>12}".format(
                CONFIG_LABEL.get(config, config).ljust(50),
                row["whole_frame"], row["ceiling"], row["wall_left"], row["blue_over_red"],
                "-" if uncached is None else "{:.1%}".format(uncached)))

    return verdict(results)


def verdict(results):
    """The assertions, printed as PASS/FAIL lines, returning a process exit code.

    THIS IS WHERE THE THIRD OF THE MILESTONE'S THREE TESTS LIVES, and it is deliberately not in the
    HouseForge.* automation suite. The other two - a baked flat reports coverage, an unbaked one does
    not and the capture refuses - assert a MECHANISM and run happily under -nullrhi, which is how the
    gate runs. This one asserts LIGHT, and there is no honest way to measure light in that process:
    -nullrhi draws nothing at all, and the one instrument the suite does have, FHFSceneCapture, is a
    one-shot USceneCaptureComponent2D that runs no converged global illumination and compensates with
    an ambient cubemap. A test written on top of it would report a confident number measured off the
    cubemap and would pass whether or not Lumen could see a single wall. That is a worse outcome than
    not having the test, so the assertion lives with the instrument that can actually take it.
    """
    failures = []
    print("")

    missing = [(config, view) for config in CONFIG_ORDER for view in VIEWS
               if view not in results.get(config, {})]
    if missing:
        failures.append("incomplete set: {}".format(", ".join("{}/{}".format(c, v) for c, v in missing)))
        print("FAIL  the set is incomplete - {} image(s) missing. Nothing below is a comparison of "
              "four configurations.".format(len(missing)))
    else:
        print("PASS  all {} configurations rendered in both views.".format(len(CONFIG_ORDER)))

    baked = results.get("D-baked-hardware", {}).get(MEASURED_VIEW)
    live = results.get("B-live-hardware", {}).get(MEASURED_VIEW)

    # THE ASSERTION. The left wall of the master bedroom view receives no direct light - the camera
    # faces away from the window - so what lands on it is bounce, and bounce is precisely what a flat
    # outside the Lumen scene does not have. Same tracing path on both sides, so the bake is the only
    # variable.
    if baked and live and live["wall_left"] > 1e-9:
        gain = baked["wall_left"] / live["wall_left"]
        if gain >= MIN_BAKED_WALL_GAIN:
            print("PASS  shadowed wall is {:.2f}x brighter baked than live ({:.4f} against {:.4f}), "
                  "same hardware tracing. Floor {:.2f}x.".format(
                      gain, baked["wall_left"], live["wall_left"], MIN_BAKED_WALL_GAIN))
        else:
            failures.append("shadowed wall gain {:.2f}x below {:.2f}x".format(gain, MIN_BAKED_WALL_GAIN))
            print("FAIL  shadowed wall is only {:.2f}x brighter baked than live. The bake is not "
                  "putting light into the room.".format(gain))
    else:
        failures.append("no shadowed-wall measurement")
        print("FAIL  the shadowed wall could not be measured in both configurations.")

    # CARDS PLACED, measured. The luminance assertion above says light arrived; this says WHY, and it
    # is the half that cannot be explained away by exposure or by a lucky camera.
    if baked and live and baked.get("uncached_fraction") is not None and live.get("uncached_fraction") is not None:
        if live["uncached_fraction"] > 0.5 and baked["uncached_fraction"] < 0.05:
            print("PASS  surface cache coverage: {:.1%} of the live frame has no cards, against "
                  "{:.1%} baked.".format(live["uncached_fraction"], baked["uncached_fraction"]))
        else:
            failures.append("surface cache coverage live {:.1%} / baked {:.1%}".format(
                live["uncached_fraction"], baked["uncached_fraction"]))
            print("FAIL  surface cache coverage did not separate the two configurations: {:.1%} of "
                  "the live frame uncached, {:.1%} baked.".format(
                      live["uncached_fraction"], baked["uncached_fraction"]))
    else:
        failures.append("no surface cache measurement")
        print("FAIL  the surface cache pass could not be measured in both configurations.")

    # THE DECEPTION, recorded as data rather than left in prose. The whole reason the guard is
    # mechanical is that the broken configuration is the brighter one; if that ever stopped being
    # true the argument for refusing would need rewriting, and this is where it would show up.
    broken = results.get("A-live-software", {}).get(MEASURED_VIEW)
    if broken and baked:
        ratio = broken["whole_frame"] / baked["whole_frame"] if baked["whole_frame"] > 1e-9 else 0.0
        if ratio > 1.0:
            print("PASS  the BROKEN configuration is {:.1f}x brighter than the correct one "
                  "({:.4f} against {:.4f}) - which is why this is not left to the eye.".format(
                      ratio, broken["whole_frame"], baked["whole_frame"]))
        else:
            print("NOTE  the broken configuration is no longer the brighter one ({:.4f} against "
                  "{:.4f}). Docs/LumenAndTheBake.md argues from the opposite - check it.".format(
                      broken["whole_frame"], baked["whole_frame"]))

    if failures:
        print("")
        print("FAILED: {}".format("; ".join(failures)))
        return 1

    return 0


sys.exit(main())
