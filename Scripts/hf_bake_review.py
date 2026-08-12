"""Render the milestone-12 review package: what the bake does, seen rather than asserted.

Runs INSIDE a real editor (see hf-bake-review.ps1). Everything here needs a renderer that actually
draws and a Lumen that actually converges, so none of it can live in the automation suite - the gate
runs under -nullrhi, where nothing is drawn at all.

WHAT IT CAPTURES, AND WHY EACH ONE IS A MEASUREMENT RATHER THAN AN ILLUSTRATION

  interior      The same camera, same lighting, same exposure, live and baked. The point is not that
                they differ - it is WHICH WAY they differ. The live frame is BRIGHTER, because sky
                floods through walls Lumen cannot see. A reviewer who expects "broken looks broken"
                has to see that it does not.

  lumenscene    r.Lumen.Visualize 3 over the same two states, with a plain engine cube standing
                beside the flat as a control. A black frame proves nothing on its own - it could mean
                the visualisation never ran - so the cube is what turns "the flat is missing" from a
                guess into a reading.

  articulation  One articulated fixture, BAKED, at five open amounts. Rule 04: "a bake must not weld
                a chest of drawers into a block". Photographed from the baked static meshes, with the
                baked components' own world transforms emitted alongside, so the picture and the
                numbers can be checked against each other.

  override      Generated -> a Content Browser asset over it -> cleared. The third image is compared
                to the first PIXEL BY PIXEL by the compose step, because "clearing the override
                restores the generated mesh exactly" is a claim about exactness and the only honest
                way to show it is a difference of zero.

  guard         CaptureView on an unbaked flat, verbatim. The refusal is the product here.

Output: Saved/Review/bake/<name>.png plus review.json, composed afterwards by hf_bake_review_compose.py
(which needs numpy and PIL, and the editor's embedded python has neither).
"""

import json
import os
import time

import unreal

PLUGIN = r"D:/Projects/UnrealEngine/5.8/HouseBuilder/Plugins/HouseForge"
OUT_DIR = PLUGIN + "/Saved/Review/bake"
SHOT_DIR = r"D:/Projects/UnrealEngine/5.8/HouseBuilder/Saved/Screenshots/WindowsEditor"

# Same settle count as hf_lumen.py, and for the same reason: Lumen's screen probe gather accumulates
# over a history, so a frame captured cold measures noise rather than light.
SETTLE_FRAMES = 200

# Waiting on the FILE, not on a frame count. Baking the flat writes hundreds of assets and blocks the
# game thread for the best part of twenty seconds, during which the frame counter does not move and
# every HighResShot issued goes into a renderer that is not drawing. hf_lumen.py lost four of
# twenty-four images that way and still reported success.
SHOT_TIMEOUT_TICKS = 900

RESOLUTION = "1600x900"

# The asset stood in for a generated fixture. An engine primitive on purpose: the swap has to be
# unmistakable at a glance, and a tasteful replacement that looks a bit like a wardrobe would make a
# picture that proves nothing. What is being shown is that the override lands, fits and reverses -
# not that the asset library has good taste.
OVERRIDE_ASSET = "/Engine/BasicShapes/Cylinder.Cylinder"

# The open amounts the baked fixture is photographed at. Five, including both ends, because a
# two-position pair cannot tell a hinge from a teleport.
OPEN_AMOUNTS = [0.0, 0.25, 0.5, 0.75, 1.0]


def say(text):
    print(text, flush=True)
    unreal.log_warning("HFBAKEREVIEW " + text)


def emit(tag, payload):
    say("HFDATA {} {}".format(tag, json.dumps(payload)))


def world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def console(command):
    unreal.SystemLibrary.execute_console_command(world(), command)


def subsystem():
    return unreal.get_editor_subsystem(unreal.HFEditorSubsystem)


def split(returned):
    """(FHFOperationResult, out-param) from a UFUNCTION with one output parameter.

    Python binds a UFUNCTION with a return value and out params as a tuple, and one with neither as
    the bare value - so a signature gaining or losing an out param silently changes the shape of
    everything that calls it. Unpacked in one place rather than at every call site.
    """
    if isinstance(returned, tuple):
        return returned[0], (returned[1] if len(returned) > 1 else "")
    return returned, ""


def dynamic_components(actor):
    """Every live UDynamicMeshComponent on an element.

    AHFElementActor::GetMeshComponent is a plain inline getter rather than a UFUNCTION, so it is not
    reachable from here at all - the components are found by class instead.
    """
    return list(actor.get_components_by_class(unreal.DynamicMeshComponent))


def actors():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()


def house():
    for actor in actors():
        if actor.get_class().get_name() == "HFHouseActor":
            return actor
    return None


def elements():
    home = house()
    if home is None:
        return []
    return [e for e in home.get_editor_property("element_actors") if e]


def find_element(element_id):
    for element in elements():
        if str(element.get_editor_property("element_id")) == element_id:
            return element
    return None


def bounds_of(actor):
    return actor.get_actor_bounds(False)


def flat_bounds():
    """Union of the ELEMENT actors' bounds - never AHFHouseActor's own.

    The house actor is a coordinator that owns no primitive component, so its bounds are a degenerate
    box at the origin. hf_lumen.py's first run placed every camera 1.3 cm outside a 1 cm house and
    produced frames that looked like renders and measured nothing.
    """
    low = high = None
    for actor in actors():
        name = actor.get_class().get_name()
        if not name.startswith("HF") or name == "HFHouseActor":
            continue
        origin, extent = bounds_of(actor)
        if extent.x <= 0.0 and extent.y <= 0.0 and extent.z <= 0.0:
            continue
        lo = unreal.Vector(origin.x - extent.x, origin.y - extent.y, origin.z - extent.z)
        hi = unreal.Vector(origin.x + extent.x, origin.y + extent.y, origin.z + extent.z)
        if low is None:
            low, high = lo, hi
        else:
            low = unreal.Vector(min(low.x, lo.x), min(low.y, lo.y), min(low.z, lo.z))
            high = unreal.Vector(max(high.x, hi.x), max(high.y, hi.y), max(high.z, hi.z))

    if low is None:
        return unreal.Vector(0, 0, 0), unreal.Vector(0, 0, 0)

    origin = unreal.Vector((low.x + high.x) * 0.5, (low.y + high.y) * 0.5, (low.z + high.z) * 0.5)
    extent = unreal.Vector((high.x - low.x) * 0.5, (high.y - low.y) * 0.5, (high.z - low.z) * 0.5)
    return origin, extent


def baked_folder_size():
    """What the assets this bake wrote actually occupy, on disk, in the project's Content folder.

    Measured from the /Game path the house recorded rather than from a guess, and reported in the
    package because "what does the bake cost" is a disk question as much as a time one - these are
    user output written into somebody else's project (rule 01), and the number is what makes that a
    stated cost rather than a surprise.
    """
    home = house()
    if home is None:
        return None

    folder = str(home.get_editor_property("baked_asset_folder"))
    if not folder.startswith("/Game/"):
        return {"folder": folder, "files": None, "megabytes": None}

    root = os.path.join(
        r"D:/Projects/UnrealEngine/5.8/HouseBuilder/Content", folder[len("/Game/"):])

    total = 0
    files = 0
    for base, _dirs, names in os.walk(root):
        for name in names:
            try:
                total += os.path.getsize(os.path.join(base, name))
                files += 1
            except OSError:
                pass

    return {"folder": folder, "files": files, "megabytes": round(total / (1024.0 * 1024.0), 1)}


def zero_the_ambient_cubemap():
    """THE CONTROL, and without it none of the interior frames mean anything.

    FHFViewingLight pins an ambient cubemap at intensity 3 on an unbound PostProcessVolume so that a
    HouseForge capture of an unlit flat is not simply black. Left in, it lights the interior on its
    own and the live and baked frames come back looking nearly identical - which would "prove" the
    bake changes nothing.
    """
    changed = 0
    for actor in actors():
        if not isinstance(actor, unreal.PostProcessVolume):
            continue
        settings = actor.get_editor_property("settings")
        settings.set_editor_property("override_ambient_cubemap_intensity", True)
        settings.set_editor_property("ambient_cubemap_intensity", 0.0)
        actor.set_editor_property("settings", settings)
        changed += 1
    return changed


def place_control_cube(location):
    cube = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Cube.Cube")
    actor = unreal.EditorLevelLibrary.spawn_actor_from_object(cube, location, unreal.Rotator(0, 0, 0))
    if actor is None:
        return None
    actor.set_actor_scale3d(unreal.Vector(3.0, 3.0, 3.0))
    actor.set_actor_label("HFBakeReview_ControlCube")
    return actor


def look_at(location, target):
    direction = unreal.Vector(target.x - location.x, target.y - location.y, target.z - location.z)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
        location, direction.rotator())


# --------------------------------------------------------------------------------- reading state


def bake_state(element):
    """What one element is showing, read off the actor rather than inferred from the picture."""
    mode = element.get_editor_property("render_mode")
    parts = element.get_editor_property("baked_parts")
    return {
        "elementId": str(element.get_editor_property("element_id")),
        "class": element.get_class().get_name(),
        "renderMode": str(mode),
        "bakedParts": len(parts),
        "artistEdited": bool(element.get_editor_property("b_artist_edited")),
        "assetMissing": bool(element.get_editor_property("b_bake_asset_missing")),
    }


def baked_component_transforms(element):
    """Every baked component's world location, so the articulation can be checked numerically.

    A strip of five pictures showing a shutter in five places is suggestive. The same five positions
    read off the BAKED components' transforms is the measurement, and it is what distinguishes "the
    baked leaf moved" from "the live leaf moved behind a baked one that did not".
    """
    out = []
    for part in element.get_editor_property("baked_parts"):
        component = part.get_editor_property("component")
        if component is None:
            continue
        location = component.get_world_location()
        mesh = part.get_editor_property("baked_mesh")
        out.append({
            "source": str(part.get_editor_property("source_component_name")),
            "asset": mesh.get_path_name() if mesh else None,
            "world": [round(location.x, 3), round(location.y, 3), round(location.z, 3)],
            "collision": str(component.get_collision_enabled()),
            "visible": bool(component.is_visible()),
        })
    return out


def pick_articulated():
    """The fixture photographed opening. Chosen by what it HAS, never by name.

    Chosen on the PART LIST, because that is the only thing here that is actually reflected.
    FHFPartState exposes PartId, OpenAmount, SpinTurns and bArtistEdited - and no motion type at all,
    so a picker that filtered on one would silently match nothing and this whole section would go
    missing from the package with a warning nobody reads.

    A fixture is preferred over an opening on a tie: rule 04's example is a chest of drawers, and a
    wardrobe with four shutters makes the point a single door leaf makes weakly. Whether the parts
    really move is not guessed at here - it is MEASURED afterwards, from the baked components' own
    world transforms across the five open amounts, and the compose step fails if none of them moved.
    """
    best = None
    best_score = 0

    for element in elements():
        try:
            parts = element.get_editor_property("parts")
        except Exception:
            continue
        if not parts:
            continue

        ids = [str(p.get_editor_property("part_id")) for p in parts]

        # A fixture outranks an opening with the same part count, and any articulated element beats
        # none. Nothing here depends on the names.
        score = len(ids) * 2 + (1 if element.get_class().get_name() == "HFFixtureActor" else 0)
        if score > best_score:
            best = (element, ids)
            best_score = score

    return best


def pick_override_target():
    """A fixture to put an asset over: a free-standing one with a real footprint, not a wall plate."""
    best = None
    best_volume = 0.0

    for element in elements():
        if element.get_class().get_name() != "HFFixtureActor":
            continue
        origin, extent = bounds_of(element)
        volume = extent.x * extent.y * extent.z
        # Big enough to read in a frame, small enough to be a piece of furniture rather than joinery
        # running the length of a wall.
        if 8000.0 < volume < 4.0e7 and volume > best_volume:
            best = element
            best_volume = volume

    return best


# ------------------------------------------------------------------------------------- the run


class Run(object):
    """A state machine over slate ticks.

    Settling cannot be done with a sleep: the editor has to actually tick for Lumen to accumulate
    anything, so every step is "do a thing, then wait N frames".
    """

    def __init__(self):
        self.steps = []
        self.index = 0
        self.wait = 0
        self.handle = None
        self.written = []
        self.lost = []
        self.pending = None
        self.pending_ticks = 0
        self.data = {}

    # ------------------------------------------------------------------------------ step helpers

    def add(self, kind, payload=None):
        self.steps.append((kind, payload))

    def shot(self, name, settle=24):
        self.add("wait", settle)
        self.add("shot", name)
        self.add("await", name)

    # --------------------------------------------------------------------------------- the plan

    def plan(self, views, articulated, override_target):
        self.data["views"] = {name: {"eye": [e.x, e.y, e.z], "target": [t.x, t.y, t.z]}
                              for name, e, t in views}

        # ------------------------------------------------- 1 and 2: interior and Lumen, both states
        #
        # THE GUARD FIRST, WHILE THE FLAT IS STILL LIVE. It has to be caught in the state it refuses,
        # and the flat is only in that state once - before the first bake.
        self.add("guard")

        for state, baked in (("live", False), ("baked", True)):
            self.add("say", "=== {} ===".format(state))
            self.add("bake", baked)
            self.add("coverage", state)
            self.add("wait", SETTLE_FRAMES)

            for view_name, eye, target in views:
                self.add("camera", (eye, target))
                self.add("wait", SETTLE_FRAMES)

                self.add("cmd", "r.Lumen.Visualize 0")
                self.shot("interior__{}__{}__beauty".format(view_name, state))

                self.add("cmd", "r.Lumen.Visualize 3")
                self.shot("interior__{}__{}__lumenscene".format(view_name, state))

                self.add("cmd", "r.Lumen.Visualize 0")

        # ------------------------------------------------------ 3: a baked fixture, still opening
        #
        # The flat is left BAKED from the loop above, which is the state this has to be photographed
        # in - the whole claim is about baked geometry moving.
        if articulated is not None:
            element, moving = articulated
            self.add("say", "=== articulation: {} ({} parts) ===".format(
                element.get_editor_property("element_id"), len(moving)))
            self.add("camera_fixture", element)
            self.add("wait", SETTLE_FRAMES)

            for amount in OPEN_AMOUNTS:
                self.add("open", (element, amount))
                self.add("record_open", (element, amount))
                self.shot("articulation__open-{:03d}".format(int(amount * 100)), settle=30)

            self.add("open", (element, 0.0))

        # ---------------------------------------------------- 4: an override, applied and cleared
        if override_target is not None:
            self.add("say", "=== override: {} ===".format(
                override_target.get_editor_property("element_id")))

            # Back to live meshes for this one. The override is a runtime component swap and has
            # nothing to do with the bake; photographing it baked would conflate the two mechanisms
            # in the one picture meant to isolate the second.
            self.add("bake", False)
            self.add("camera_fixture", override_target)
            self.add("wait", SETTLE_FRAMES)

            self.add("record_mesh", (override_target, "before"))
            self.shot("override__1-generated", settle=30)

            self.add("override_apply", override_target)
            self.add("record_mesh", (override_target, "applied"))
            self.shot("override__2-asset", settle=30)

            self.add("override_clear", override_target)
            self.add("record_mesh", (override_target, "cleared"))
            self.shot("override__3-cleared", settle=30)

        self.add("done")

    # ------------------------------------------------------------------------------- the ticker

    def start(self):
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def tick(self, delta_seconds):
        if self.pending is not None:
            if os.path.exists(os.path.join(SHOT_DIR, self.pending + ".png")):
                self.pending = None
                self.pending_ticks = 0
                self.wait = 6
                return

            self.pending_ticks += 1
            if self.pending_ticks < SHOT_TIMEOUT_TICKS:
                return

            say("LOST SHOT {} - never written after {} ticks".format(self.pending, self.pending_ticks))
            self.lost.append(self.pending)
            self.pending = None
            self.pending_ticks = 0

        if self.wait > 0:
            self.wait -= 1
            return

        if self.index >= len(self.steps):
            return

        kind, payload = self.steps[self.index]
        self.index += 1

        try:
            self.step(kind, payload)
        except Exception as error:
            say("STEP {} FAILED: {}".format(kind, error))

    def step(self, kind, payload):
        if kind == "wait":
            self.wait = payload
        elif kind == "say":
            say(payload)
        elif kind == "cmd":
            console(payload)
        elif kind == "camera":
            look_at(payload[0], payload[1])
        elif kind == "camera_fixture":
            self.frame_fixture(payload)
        elif kind == "bake":
            # TIMED, because "what does baking the flat cost" is a question the milestone has to
            # answer with a number and nothing in FHFBakeService measures itself. Wall clock on the
            # game thread, which is the figure that matters: this is the interval in which the editor
            # does not respond.
            started = time.time()
            result, report = split(subsystem().set_house_render_mode(payload))
            elapsed = time.time() - started

            state = "bake" if payload else "unbake"
            self.data.setdefault("cost", {})[state] = {
                "seconds": round(elapsed, 2),
                "elements": len(elements()),
                "bakedParts": sum(len(e.get_editor_property("baked_parts")) for e in elements()),
                "disk": baked_folder_size(),
                "report": str(report)[:2000],
            }
            say("BAKE {} took {:.2f}s -> {} | {}".format(
                payload, elapsed, result.message[:200], str(report)[:300]))
        elif kind == "coverage":
            self.record_coverage(payload)
        elif kind == "guard":
            self.record_guard()
        elif kind == "open":
            element, amount = payload
            element.set_master_open_amount(amount)
        elif kind == "record_open":
            self.record_open(payload[0], payload[1])
        elif kind == "record_mesh":
            self.record_mesh(payload[0], payload[1])
        elif kind == "override_apply":
            self.apply_override(payload)
        elif kind == "override_clear":
            self.clear_override(payload)
        elif kind == "shot":
            stale = os.path.join(SHOT_DIR, payload + ".png")
            if os.path.exists(stale):
                os.remove(stale)
            console("HighResShot {} filename={}".format(RESOLUTION, payload))
            self.written.append(payload)
            say("SHOT {}".format(payload))
        elif kind == "await":
            self.pending = payload
            self.pending_ticks = 0
        elif kind == "done":
            self.finish()

    # ---------------------------------------------------------------------------- the recordings

    def frame_fixture(self, element):
        origin, extent = bounds_of(element)
        reach = max(extent.x, extent.y, extent.z) * 3.0 + 120.0
        eye = unreal.Vector(origin.x + reach * 0.7, origin.y + reach * 0.7, origin.z + reach * 0.45)
        look_at(eye, origin)

    def record_coverage(self, state):
        result, report = split(subsystem().check_lumen_coverage())
        self.data.setdefault("coverage", {})[state] = {
            "ok": bool(result.b_success),
            "report": str(report),
        }
        say("COVERAGE {} ok={} {}".format(state, result.b_success, str(report)[:400]))

    def record_guard(self):
        """CaptureView on the unbaked flat. The refusal IS the artefact."""
        origin, extent = flat_bounds()
        eye = unreal.Vector(origin.x, origin.y, origin.z + extent.z + 200.0)
        result, path = split(subsystem().capture_view("hf-bake-review-guard", 512, eye, origin, 70.0))

        self.data["guard"] = {
            "refused": not bool(result.b_success),
            "message": str(result.message),
            "path": str(path),
        }
        say("GUARD refused={} : {}".format(not result.b_success, result.message[:600]))

    def record_open(self, element, amount):
        self.data.setdefault("articulation", {}).setdefault("frames", []).append({
            "open": amount,
            "state": bake_state(element),
            "parts": baked_component_transforms(element),
        })

    def record_mesh(self, element, label):
        """A fingerprint of the LIVE dynamic mesh, at each of the three override steps.

        The pixel comparison in the compose step shows the picture comes back. This shows the mesh
        underneath it was never touched in the first place - which is the stronger claim, and the one
        rule 04 actually makes.
        """
        triangles = vertices = 0
        for component in dynamic_components(element):
            mesh = component.get_dynamic_mesh()
            if mesh is None:
                continue
            triangles += unreal.GeometryScript_MeshQueries.get_num_triangle_ids(mesh)
            vertices += unreal.GeometryScript_MeshQueries.get_num_vertex_ids(mesh)

        origin, extent = bounds_of(element)
        self.data.setdefault("override", {}).setdefault("steps", []).append({
            "step": label,
            "state": bake_state(element),
            "liveTriangles": triangles,
            "liveVertices": vertices,
            "worldExtent": [round(extent.x, 3), round(extent.y, 3), round(extent.z, 3)],
        })
        say("OVERRIDE {} tris={} verts={}".format(label, triangles, vertices))

    def apply_override(self, element):
        override = unreal.HFAssetOverride()
        override.set_editor_property("override_mesh", unreal.SoftObjectPath(OVERRIDE_ASSET))
        result, report = split(subsystem().apply_asset_to_elements(
            [str(element.get_editor_property("element_id"))], override))
        self.data.setdefault("override", {})["applyReport"] = str(report)
        self.data.setdefault("override", {})["asset"] = OVERRIDE_ASSET
        say("OVERRIDE APPLY ok={} {}".format(result.b_success, str(report)[:400]))

    def clear_override(self, element):
        result, report = split(subsystem().clear_asset_overrides(
            [str(element.get_editor_property("element_id"))]))
        self.data.setdefault("override", {})["clearReport"] = str(report)
        say("OVERRIDE CLEAR ok={} {}".format(result.b_success, str(report)[:400]))

    # ------------------------------------------------------------------------------------ finish

    def finish(self):
        self.data["written"] = self.written
        self.data["lost"] = self.lost
        self.data["shotDir"] = SHOT_DIR

        with open(os.path.join(OUT_DIR, "review.json"), "w", encoding="utf-8") as handle:
            json.dump(self.data, handle, indent=1, sort_keys=True)

        emit("WRITTEN", self.written)
        emit("LOST", self.lost)

        if self.lost:
            say("WARNING: {} shot(s) were never written. The set is incomplete.".format(len(self.lost)))

        say("HF DONE")

        if self.handle is not None:
            unreal.unregister_slate_post_tick_callback(self.handle)

        unreal.SystemLibrary.quit_editor()


def main():
    if not os.path.isdir(OUT_DIR):
        os.makedirs(OUT_DIR)

    spec_path = PLUGIN + "/Reference/Specs/Sample2BHK.json"
    with open(spec_path, "r", encoding="utf-8") as handle:
        result, _ = split(subsystem().apply_spec_json(handle.read(), ""))
    say("BUILD {}".format(result.message[:400]))

    if house() is None:
        say("NO HOUSE - nothing to show")
        unreal.SystemLibrary.quit_editor()
        return

    origin, extent = flat_bounds()
    emit("FLAT_BOUNDS", {"origin": [origin.x, origin.y, origin.z],
                         "extent": [extent.x, extent.y, extent.z]})

    # Metres across, or the bounds came off the wrong actor and every camera below is pointed at the
    # origin - which produces images that look like renders and measure nothing.
    if extent.x < 100.0 or extent.y < 100.0:
        say("ABORT: flat bounds are degenerate ({:.1f} x {:.1f} cm).".format(extent.x * 2, extent.y * 2))
        unreal.SystemLibrary.quit_editor()
        return

    zeroed = zero_the_ambient_cubemap()
    emit("AMBIENT_CUBEMAP_ZEROED", {"volumes": zeroed})
    if zeroed == 0:
        say("WARNING: no PostProcessVolume was zeroed. Every interior frame below may be the cubemap.")

    place_control_cube(unreal.Vector(origin.x - extent.x - 400.0, origin.y, 150.0))

    views = []

    # The master bedroom, looking AWAY from the window at the far wall. That wall receives no direct
    # sun, so what lands on it is bounce - and bounce is exactly what a flat outside the Lumen scene
    # does not have. Facing the window would measure the window.
    bedroom = find_element("R_MBed")
    if bedroom is not None:
        room_origin, room_extent = bounds_of(bedroom)
        views.append(("mbed",
                      unreal.Vector(room_origin.x, room_origin.y + room_extent.y - 60.0, 150.0),
                      unreal.Vector(room_origin.x, room_origin.y - room_extent.y, 140.0)))

    living = find_element("R_Living")
    if living is not None:
        room_origin, room_extent = bounds_of(living)
        views.append(("living",
                      unreal.Vector(room_origin.x + room_extent.x - 60.0, room_origin.y, 150.0),
                      unreal.Vector(room_origin.x - room_extent.x, room_origin.y, 140.0)))

    if not views:
        say("WARNING: neither R_MBed nor R_Living was found; falling back to an aerial view.")
        views.append(("aerial",
                      unreal.Vector(origin.x - extent.x * 1.3, origin.y - extent.y * 1.3,
                                    extent.z * 3.2 + 400.0),
                      unreal.Vector(origin.x, origin.y, origin.z)))

    articulated = pick_articulated()
    if articulated is None:
        say("WARNING: no articulated element with parts was found.")
    else:
        say("ARTICULATED {} with {} parts".format(
            articulated[0].get_editor_property("element_id"), len(articulated[1])))

    override_target = pick_override_target()
    if override_target is None:
        say("WARNING: no suitable fixture was found for the override demonstration.")
    else:
        say("OVERRIDE TARGET {}".format(override_target.get_editor_property("element_id")))

    console("r.ScreenPercentage 100")

    run = Run()
    run.plan(views, articulated, override_target)
    run.start()


main()
