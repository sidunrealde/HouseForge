"""Compose the milestone-12 review package, and assert the two things it claims about pixels.

Runs OUTSIDE the editor: reading PNGs needs numpy and PIL and the editor's embedded python has
neither. hf_bake_review.py renders the frames and writes review.json beside them; this turns those
into side-by-side sheets, measures them, and writes index.md.

IT ASSERTS, and exits non-zero when it fails. Two of the package's claims are claims about pixels,
and a review package that merely displays them is a review package that can be wrong quietly:

  1. THE BROKEN CONFIGURATION IS BRIGHTER. Live-mesh interiors must measure brighter than baked ones
     under the same lighting and the same exposure. This is the whole reason the bake had to be
     guarded rather than recommended - if it ever stops being true, the guard's justification has
     changed and somebody needs to know before the next person reads the index and believes it.

  2. CLEARING AN OVERRIDE RESTORES THE GENERATED MESH EXACTLY. The 'generated' and 'cleared' frames
     are the same camera on the same fixture with an asset swap in between, so if the restore is
     exact the two images are IDENTICAL. Not similar - identical. Any non-zero difference is either a
     restore that is not exact or a renderer that is not deterministic, and both are worth stopping
     for.
"""

import json
import os
import sys

import numpy
from PIL import Image, ImageDraw, ImageFont


# Rec.709. The images are sRGB-encoded, so they are linearised first - averaging sRGB bytes measures
# an encoding, not an amount of light, and would flatten exactly the differences being looked for.
LUMA = (0.2126, 0.7152, 0.0722)


def linear_luminance(path):
    pixels = numpy.asarray(Image.open(path).convert("RGB"), dtype=numpy.float64) / 255.0
    linear = numpy.where(pixels <= 0.04045, pixels / 12.92, ((pixels + 0.055) / 1.055) ** 2.4)
    return float((linear * LUMA).sum(axis=2).mean())


def label(image, text, subtext=""):
    """A caption bar under a frame. Every tile in this package carries its own numbers.

    A four-up sheet whose tiles are told apart by the order they were pasted in is a sheet that gets
    misread the first time somebody crops one out of it.
    """
    bar = 64 if subtext else 40
    out = Image.new("RGB", (image.width, image.height + bar), (16, 16, 18))
    out.paste(image, (0, 0))

    draw = ImageDraw.Draw(out)
    try:
        font = ImageFont.truetype("arial.ttf", 20)
        small = ImageFont.truetype("arial.ttf", 15)
    except OSError:
        font = small = ImageFont.load_default()

    draw.text((12, image.height + 8), text, fill=(240, 240, 240), font=font)
    if subtext:
        draw.text((12, image.height + 34), subtext, fill=(170, 170, 175), font=small)
    return out


def strip(paths_and_captions, out_path, scale=1.0):
    """Tiles in a row, at a common height. Returns False if nothing was available to compose."""
    tiles = []
    for path, caption, sub in paths_and_captions:
        if not os.path.exists(path):
            continue
        image = Image.open(path).convert("RGB")
        if scale != 1.0:
            image = image.resize((int(image.width * scale), int(image.height * scale)), Image.LANCZOS)
        tiles.append(label(image, caption, sub))

    if not tiles:
        return False

    width = sum(t.width for t in tiles)
    height = max(t.height for t in tiles)
    sheet = Image.new("RGB", (width, height), (16, 16, 18))
    x = 0
    for tile in tiles:
        sheet.paste(tile, (x, 0))
        x += tile.width
    sheet.save(out_path)
    return True


def text_panel(lines, out_path, width=1600):
    """A message rendered as an image, so the package is self-contained.

    Used for the guard. The refusal text IS the artefact here - there is no picture of a capture that
    did not happen - and a reviewer looking through a folder of PNGs should not have to open a JSON
    file to find the one thing the folder is about.
    """
    try:
        font = ImageFont.truetype("consola.ttf", 19)
        head = ImageFont.truetype("arialbd.ttf", 26)
    except OSError:
        font = head = ImageFont.load_default()

    line_height = 27
    height = 90 + line_height * len(lines)
    image = Image.new("RGB", (width, height), (18, 12, 12))
    draw = ImageDraw.Draw(image)

    draw.rectangle([0, 0, width, 56], fill=(120, 24, 24))
    draw.text((20, 14), "THE GUARD FIRING - capture_view on an unbaked flat", fill=(255, 240, 240), font=head)

    y = 76
    for line in lines:
        draw.text((20, y), line, fill=(235, 225, 225), font=font)
        y += line_height

    image.save(out_path)


def wrap(text, width=140):
    out = []
    for paragraph in str(text).replace("\r", "").split("\n"):
        if not paragraph:
            out.append("")
            continue
        line = ""
        for word in paragraph.split(" "):
            if len(line) + len(word) + 1 > width:
                out.append(line)
                line = word
            else:
                line = (line + " " + word) if line else word
        out.append(line)
    return out


def write_index(out_dir, review, measured, failures):
    """The index, GENERATED from the measurements rather than written beside them.

    Every number below is interpolated from measurements.json. A hand-written index drifts from its
    own evidence the first time the package is regenerated and nobody re-reads the prose - which is
    the same class of failure as a green gate that measured nothing, one level out.
    """
    cost = review.get("cost", {}).get("bake", {})
    lines = []
    add = lines.append

    add("# The reversible bake, seen")
    add("")
    add("Rendered by `Scripts/hf-bake-review.ps1`, which drives a real editor viewport and then")
    add("measures what it drew. `Saved/*` is gitignored, so everything here is a build artefact - if")
    add("the images are missing, run the script rather than looking for them in history.")
    add("")
    if failures:
        add("**THIS PACKAGE DID NOT PASS ITS OWN ASSERTIONS.** " + str(len(failures)) + " failure(s):")
        add("")
        for failure in failures:
            add("* " + failure)
        add("")
    else:
        add("Every claim this package makes about pixels was measured, and held.")
        add("")

    # ------------------------------------------------------------------ 1: the interiors
    add("## 1. The same interior, live and baked")
    add("")
    add("`SIDEBYSIDE__interior__<view>__beauty.png`. Same camera, same lights, same manual exposure.")
    add("The ambient cubemap `FHFViewingLight` pins was zeroed first, and the zeroing is reported in")
    add("`review.json` - left in, it lights the room on its own and both states come back looking the")
    add("same, which would 'prove' the bake changes nothing.")
    add("")
    add("| View | Live (dynamic meshes) | Baked (static meshes) | Live / baked |")
    add("|---|---:|---:|---:|")
    for view, m in sorted(measured.get("interior", {}).items()):
        add("| `{}` | **{:.4f}** | {:.4f} | **{:.2f}x** |".format(
            view, m["liveLuminance"], m["bakedLuminance"], m["liveOverBaked"]))
    add("")
    add("Mean linearised Rec.709 luminance over the whole frame.")
    add("")
    add("**The broken one is the bright one.** That is the entire reason this needed a guard rather")
    add("than a note in the documentation. A render of an unbaked flat is not dim, or obviously")
    add("missing something - it is a clean, evenly-lit room, lit by sky pouring through walls Lumen")
    add("cannot see. There is no version of 'look at it and check' that survives this, which is why")
    add("`FHFLumenCoverage` refuses the capture instead of warning about it.")
    add("")

    # ------------------------------------------------------------------ 2: the Lumen scene
    add("## 2. What Lumen can see")
    add("")
    add("`SIDEBYSIDE__interior__<view>__lumenscene.png` - `r.Lumen.Visualize 3` over the same two")
    add("states. A plain engine `Cube` stands beside the flat in every frame as the control: a black")
    add("Lumen scene proves nothing on its own, because it could equally mean the visualisation never")
    add("ran. The cube is what turns 'the flat is absent' into a reading rather than a guess.")
    add("")
    for state in ("live", "baked"):
        report = review.get("coverage", {}).get(state, {})
        if report:
            add("* **{}** - `CheckLumenCoverage`: {}".format(state, report.get("report", "").strip()))
    add("")

    # ------------------------------------------------------------------ 3: articulation
    art = measured.get("articulation")
    if art:
        add("## 3. A baked fixture that still opens")
        add("")
        add("`STRIP__articulation.png` - one articulated fixture, **baked**, at "
            + ", ".join("{:.0f}%".format(a * 100) for a in art["openAmounts"]) + " open.")
        add("")
        add("Rule 04: *a bake must not weld a chest of drawers into a block.* The pictures show")
        add("movement; the numbers show it is the **baked** geometry moving and not a live part")
        add("behind a welded one. Of **{}** baked component(s) on this fixture, **{}** changed world".format(
            art["bakedComponents"], art["bakedComponentsThatMoved"]))
        add("position across the five open amounts - read off the baked `UStaticMeshComponent`")
        add("transforms themselves, in `review.json`.")
        add("")

    # ------------------------------------------------------------------ 4: the override
    over = measured.get("override")
    if over:
        add("## 4. An asset override, applied and reversed")
        add("")
        add("`STRIP__override.png` - generated, then `{}` over it, then cleared.".format(over.get("asset")))
        add("")
        add("| | Measured |")
        add("|---|---|")
        add("| The override actually changed the picture | worst channel moved by **{}** |".format(
            over["maxChannelDiff_generatedVsAsset"]))
        add("| Clearing it restored the picture | **{}** pixel(s) differ from the generated frame |".format(
            over["differingPixels_generatedVsCleared"]))
        add("| The live dynamic mesh across all three steps | {} triangles |".format(
            " / ".join(str(t) for t in over["liveMeshTriangles"])))
        add("")
        add("Rule 04 says *clearing the override must restore the generated mesh exactly*, and")
        add("exactly is a word with a test attached: the first and third frames are compared pixel by")
        add("pixel and any difference at all fails this script. The triangle count is the stronger")
        add("claim underneath it - nothing in the override path reads or writes an `FDynamicMesh3`, so")
        add("the restore is exact by construction rather than by repair.")
        add("")

    # ------------------------------------------------------------------ 5: the guard
    add("## 5. The guard firing")
    add("")
    add("`guard__refused.png` - `capture_view` called on the flat while it was still unbaked, and its")
    add("refusal verbatim. This is the only thing standing between a model driving the editor and a")
    add("confidently wrong render, so the package shows it working rather than describing it.")
    add("")

    # ------------------------------------------------------------------ 6: what it cost
    if cost:
        add("## 6. What the bake cost")
        add("")
        add("| | |")
        add("|---|---:|")
        add("| Elements | {} |".format(cost.get("elements")))
        add("| Baked parts (one static mesh asset each) | {} |".format(cost.get("bakedParts")))
        add("| Wall clock, whole-flat bake | **{:.1f} s** |".format(cost.get("seconds", 0.0)))
        if cost.get("bakedParts"):
            add("| Per part | {:.0f} ms |".format(
                cost.get("seconds", 0.0) * 1000.0 / cost["bakedParts"]))
        disk = cost.get("disk") or {}
        if disk.get("megabytes") is not None:
            add("| On disk, in the project's `Content` | **{} MB** across {} file(s) |".format(
                disk["megabytes"], disk["files"]))
            add("| Written to | `{}` |".format(disk["folder"]))
        add("")
        add("Wall clock on the game thread, which is the figure that matters: this is the interval in")
        add("which the editor does not respond. `SetRenderModeMany` runs it under an `FScopedSlowTask`")
        add("with a cancel button, so it is a progress bar rather than a hang.")
        add("")

    add("## Files")
    add("")
    add("* `interior__<view>__<state>__beauty.png` / `__lumenscene.png` - the raw frames")
    add("* `SIDEBYSIDE__*` - the pairs, captioned with their own numbers")
    add("* `articulation__open-NNN.png`, `STRIP__articulation.png`")
    add("* `override__1-generated.png` / `__2-asset.png` / `__3-cleared.png`, `STRIP__override.png`")
    add("* `guard__refused.png`")
    add("* `review.json` - everything the editor pass recorded, including the baked transforms")
    add("* `measurements.json` - everything this script measured")
    add("")

    with open(os.path.join(out_dir, "index.md"), "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")


def main(out_dir):
    review_path = os.path.join(out_dir, "review.json")
    if not os.path.exists(review_path):
        print("FAIL: no review.json in {} - the editor pass did not complete.".format(out_dir))
        return 1

    with open(review_path, "r", encoding="utf-8") as handle:
        review = json.load(handle)

    failures = []
    measured = {}

    def png(name):
        return os.path.join(out_dir, name + ".png")

    # ---------------------------------------------------- 1: the interiors, and which way they differ
    views = sorted(review.get("views", {}).keys())
    for view in views:
        live = png("interior__{}__live__beauty".format(view))
        baked = png("interior__{}__baked__beauty".format(view))
        if not (os.path.exists(live) and os.path.exists(baked)):
            failures.append("interior pair for '{}' is incomplete".format(view))
            continue

        live_l = linear_luminance(live)
        baked_l = linear_luminance(baked)
        measured.setdefault("interior", {})[view] = {
            "liveLuminance": round(live_l, 6),
            "bakedLuminance": round(baked_l, 6),
            "liveOverBaked": round(live_l / baked_l, 3) if baked_l > 0 else None,
        }

        strip([
            (live, "LIVE dynamic meshes - WRONG",
             "whole-frame luminance {:.4f}   sky through walls Lumen cannot see".format(live_l)),
            (baked, "BAKED static meshes - CORRECT",
             "whole-frame luminance {:.4f}   lit by the flat's own fixtures".format(baked_l)),
        ], png("SIDEBYSIDE__interior__{}__beauty".format(view)), scale=0.62)

        strip([
            (png("interior__{}__live__lumenscene".format(view)), "LIVE - what Lumen can see",
             "the control cube renders; the flat does not"),
            (png("interior__{}__baked__lumenscene".format(view)), "BAKED - what Lumen can see",
             "the flat is in the scene"),
        ], png("SIDEBYSIDE__interior__{}__lumenscene".format(view)), scale=0.62)

        # THE ASSERTION. Not "they differ" - the DIRECTION. A broken render that looked broken would
        # need no guard at all; the guard exists because it looks better.
        if not live_l > baked_l:
            failures.append(
                "interior '{}': the live frame measured {:.4f} and the baked one {:.4f}. The whole "
                "premise of the guard is that the BROKEN configuration is brighter. If that has "
                "stopped being true, the index below is telling a reader something that is no longer "
                "so.".format(view, live_l, baked_l))

    # ------------------------------------------------- 2: a baked fixture that still opens
    articulation = review.get("articulation", {}).get("frames", [])
    if articulation:
        tiles = []
        for frame in articulation:
            amount = frame["open"]
            parts = frame.get("parts", [])
            tiles.append((
                png("articulation__open-{:03d}".format(int(amount * 100))),
                "open {:.0f}%".format(amount * 100),
                "{} baked part(s), {} showing".format(
                    frame["state"]["bakedParts"], frame["state"]["renderMode"].split(".")[-1]),
            ))
        strip(tiles, png("STRIP__articulation"), scale=0.42)

        # The pictures show movement; these numbers show it is the BAKED components that moved. A
        # baked part whose world position never changes across the five frames is a welded one.
        travel = {}
        for frame in articulation:
            for part in frame.get("parts", []):
                travel.setdefault(part["source"], []).append(part["world"])

        moved = 0
        for source, positions in travel.items():
            spread = max(
                max(abs(a[i] - b[i]) for i in range(3))
                for a in positions for b in positions)
            if spread > 0.5:
                moved += 1
        measured["articulation"] = {
            "bakedComponents": len(travel),
            "bakedComponentsThatMoved": moved,
            "openAmounts": [f["open"] for f in articulation],
        }

        if moved == 0:
            failures.append(
                "not one baked component changed world position across {} open amounts. Rule 04: "
                "'a bake must not weld a chest of drawers into a block'.".format(len(articulation)))
    else:
        failures.append("no articulation frames were captured")

    # ------------------------------------------- 3: the override, applied and exactly reversed
    before = png("override__1-generated")
    applied = png("override__2-asset")
    cleared = png("override__3-cleared")

    if all(os.path.exists(p) for p in (before, applied, cleared)):
        a = numpy.asarray(Image.open(before).convert("RGB"), dtype=numpy.int32)
        b = numpy.asarray(Image.open(applied).convert("RGB"), dtype=numpy.int32)
        c = numpy.asarray(Image.open(cleared).convert("RGB"), dtype=numpy.int32)

        changed = int(numpy.abs(a - b).max())
        restored = int(numpy.abs(a - c).max())
        differing = int((numpy.abs(a - c).sum(axis=2) > 0).sum())

        measured["override"] = {
            "asset": review.get("override", {}).get("asset"),
            "maxChannelDiff_generatedVsAsset": changed,
            "maxChannelDiff_generatedVsCleared": restored,
            "differingPixels_generatedVsCleared": differing,
            "liveMeshTriangles": [s["liveTriangles"] for s in review.get("override", {}).get("steps", [])],
        }

        strip([
            (before, "1  generated", "the procedural fixture"),
            (applied, "2  asset override", str(review.get("override", {}).get("asset", ""))),
            (cleared, "3  cleared",
             "identical to 1" if restored == 0 else "{} pixel(s) differ from 1".format(differing)),
        ], png("STRIP__override"), scale=0.42)

        # THE ASSERTION. Rule 04 says "clearing the override must restore the generated mesh exactly",
        # and exactly is a word with a test attached.
        if changed == 0:
            failures.append(
                "the override changed nothing on screen - the 'generated' and 'asset' frames are "
                "identical, so this sequence demonstrates nothing about the override at all.")
        if restored != 0:
            failures.append(
                "clearing the override did NOT restore the picture exactly: {} pixel(s) differ, worst "
                "channel by {}. Rule 04: 'Clearing the override must restore the generated mesh "
                "exactly.'".format(differing, restored))

        # And the mesh underneath, which is the stronger claim: nothing in the override path reads or
        # writes an FDynamicMesh3, so the triangle count must not move at any step.
        counts = {s["liveTriangles"] for s in review.get("override", {}).get("steps", [])}
        if len(counts) > 1:
            failures.append(
                "the live dynamic mesh changed triangle count across the override sequence ({}). The "
                "override is supposed to touch visibility and nothing else.".format(sorted(counts)))
    else:
        failures.append("the override sequence is incomplete")

    # --------------------------------------------------------------------- 4: the guard, verbatim
    guard = review.get("guard", {})
    if guard:
        lines = wrap(guard.get("message", ""))
        text_panel(
            ["refused: {}".format(guard.get("refused")), ""] + lines,
            png("guard__refused"))

        if not guard.get("refused"):
            failures.append(
                "capture_view on the UNBAKED flat succeeded. The guard is the only thing standing "
                "between a model and a confidently-wrong render, and it did not fire.")
        measured["guard"] = {"refused": bool(guard.get("refused"))}
    else:
        failures.append("the guard was never exercised")

    # ---------------------------------------------------------------------------------- coverage
    measured["coverage"] = review.get("coverage", {})
    measured["lostShots"] = review.get("lost", [])
    if review.get("lost"):
        failures.append("{} shot(s) were never written; the set is incomplete".format(len(review["lost"])))

    with open(os.path.join(out_dir, "measurements.json"), "w", encoding="utf-8") as handle:
        json.dump(measured, handle, indent=1, sort_keys=True)

    write_index(out_dir, review, measured, failures)

    print(json.dumps(measured, indent=1, sort_keys=True))

    if failures:
        print("")
        for failure in failures:
            print("FAIL: " + failure)
        return 1

    print("\nEvery claim this package makes about pixels was measured, and held.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "."))
