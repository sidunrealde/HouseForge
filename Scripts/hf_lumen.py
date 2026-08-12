"""Render the flat under four Lumen configurations and prove which of them Lumen can see.

Runs INSIDE a real editor (see hf-lumen.ps1), because none of this can be measured any other way:

  * A one-shot USceneCaptureComponent2D - which is what FHFSceneCapture uses, and what every other
    HouseForge screenshot goes through - runs no converged global illumination. It renders one frame
    with no history for the screen probe gather to accumulate into, and it compensates with an
    ambient cubemap. Measuring indirect light through it measures the cubemap.
  * So this drives the real level viewport, settles it for a couple of hundred frames after every
    state change so Lumen converges, and captures with HighResShot.
  * -nullrhi renders nothing at all, which is how the validation gate runs, which is why the
    automated half of this milestone (HouseForge.Lumen.*) asserts the MECHANISM instead of pixels.

THE ONE CONTROL THAT MATTERS. FHFViewingLight puts an ambient cubemap at intensity 3 on an unbound
PostProcessVolume. Left in, it lights the interior on its own and all four configurations come back
looking nearly identical - the experiment then "proves" Lumen works no matter what. It is zeroed
before anything is rendered, and the zeroing is reported so a reader can see it happened.

THE SECOND CONTROL. A plain engine cube stands beside the flat in every frame. A black Lumen Scene
visualisation proves nothing on its own - it could mean the visualisation never ran - so there has to
be something in the picture that is known to be in the Lumen scene. If the cube renders and the flat
does not, the instrument works and the geometry is absent.

Output: Saved/Review/lumen-baked/<config>__<view>__<pass>.png, measured afterwards by
hf_lumen_measure.py, which is a separate step because reading PNGs needs numpy and the editor's
python does not have it.
"""

import json
import os

import unreal

PLUGIN = r"D:/Projects/UnrealEngine/5.8/HouseBuilder/Plugins/HouseForge"
OUT_DIR = PLUGIN + "/Saved/Review/lumen-baked"

# Where HighResShot writes. Not configurable from the command, so the files are moved afterwards.
SHOT_DIR = r"D:/Projects/UnrealEngine/5.8/HouseBuilder/Saved/Screenshots/WindowsEditor"

# Frames to let Lumen converge after a state change. The screen probe gather accumulates over a
# history; captured cold, every configuration reads dark and the comparison is between two amounts of
# noise rather than between two amounts of light.
SETTLE_FRAMES = 200

# Ticks to keep asking whether HighResShot has written the file yet, before giving up on it.
#
# A FIXED FRAME WAIT IS NOT ENOUGH AND THE FIRST RUN PROVED IT. Baking the flat writes 410 static
# mesh assets and blocks the game thread for the best part of twenty seconds; the frame counter sat
# still at 762 for the whole of it. Every HighResShot requested in that window was issued into a
# renderer that was not producing frames, the twelve-frame wait expired without a single frame having
# been drawn, and four of the twenty-four images were simply never written. The run still reported
# success, and the measurement table quietly printed three configurations instead of four - which is
# the exact shape of failure this milestone is about, in the instrument rather than in the render.
#
# So the wait is now on the FILE, not on a frame count.
SHOT_TIMEOUT_TICKS = 900

RESOLUTION = "1600x900"

# id, bake?, hardware ray tracing?, description
CONFIGS = [
    ("A-live-software", False, 0, "live dynamic meshes, software tracing"),
    ("B-live-hardware", False, 1, "live dynamic meshes, hardware tracing (the shipped default)"),
    ("C-baked-software", True, 0, "baked static meshes, software tracing"),
    ("D-baked-hardware", True, 1, "baked static meshes, hardware tracing"),
]

# name, r.Lumen.Visualize value
PASSES = [
    ("beauty", 0),
    ("lumenscene", 3),
    ("surfcache", 5),
]


def say(text):
    print(text, flush=True)
    unreal.log_warning("HFLUMEN " + text)


def emit(tag, payload):
    say("HFDATA {} {}".format(tag, json.dumps(payload)))


def world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def console(command):
    unreal.SystemLibrary.execute_console_command(world(), command)


def subsystem():
    return unreal.get_editor_subsystem(unreal.HFEditorSubsystem)


def actors():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()


def zero_the_ambient_cubemap():
    """The control. Returns how many volumes were changed, so it can be reported rather than assumed."""
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
    """A plain engine static mesh, known to be in the Lumen scene, standing beside the flat."""
    cube = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Cube.Cube")
    actor = unreal.EditorLevelLibrary.spawn_actor_from_object(cube, location, unreal.Rotator(0, 0, 0))
    if actor is None:
        return None
    actor.set_actor_scale3d(unreal.Vector(3.0, 3.0, 3.0))
    actor.set_actor_label("HFLumen_ControlCube")
    return actor


def bounds_of(actor):
    origin, extent = actor.get_actor_bounds(False)
    return origin, extent


def flat_bounds():
    """The union of the ELEMENT actors' bounds.

    NOT AHFHouseActor's own bounds, which is the mistake the first run made. The house actor is a
    coordinator: the walls, rooms and fixtures are separate actors it holds references to, so it owns
    no primitive component of its own and get_actor_bounds returns a degenerate box at the origin.
    The reported extent was (1, 1, 1). Every aerial camera was then placed 1.3 cm outside a 1 cm
    house, which is to say inside the origin looking at nothing, and the frames came back saturated -
    whole-frame luminance 0.87 in all three configurations, identical to three decimal places, which
    is what "the camera is not looking at the building" reads like when it is not noticed.
    """
    low = None
    high = None

    for actor in actors():
        if not actor.get_class().get_name().startswith("HF"):
            continue
        if actor.get_class().get_name() == "HFHouseActor":
            continue

        origin, extent = bounds_of(actor)
        if extent.x <= 0.0 and extent.y <= 0.0 and extent.z <= 0.0:
            continue

        corner_low = unreal.Vector(origin.x - extent.x, origin.y - extent.y, origin.z - extent.z)
        corner_high = unreal.Vector(origin.x + extent.x, origin.y + extent.y, origin.z + extent.z)

        if low is None:
            low, high = corner_low, corner_high
        else:
            low = unreal.Vector(min(low.x, corner_low.x), min(low.y, corner_low.y), min(low.z, corner_low.z))
            high = unreal.Vector(max(high.x, corner_high.x), max(high.y, corner_high.y), max(high.z, corner_high.z))

    if low is None:
        return unreal.Vector(0, 0, 0), unreal.Vector(0, 0, 0)

    origin = unreal.Vector((low.x + high.x) * 0.5, (low.y + high.y) * 0.5, (low.z + high.z) * 0.5)
    extent = unreal.Vector((high.x - low.x) * 0.5, (high.y - low.y) * 0.5, (high.z - low.z) * 0.5)
    return origin, extent


def find_element(element_id):
    for actor in actors():
        if actor.get_class().get_name() == "HFHouseActor":
            for element in actor.get_editor_property("element_actors"):
                if element and str(element.get_editor_property("element_id")) == element_id:
                    return element
    return None


def house():
    for actor in actors():
        if actor.get_class().get_name() == "HFHouseActor":
            return actor
    return None


def look_at(location, target):
    direction = unreal.Vector(target.x - location.x, target.y - location.y, target.z - location.z)
    rotation = direction.rotator()
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(location, rotation)


class Run(object):
    """A state machine over slate ticks.

    Frame settling cannot be done with a sleep - the editor has to actually tick for Lumen to
    accumulate anything - so every step here is "do a thing, then wait N frames", driven from a post
    tick callback.
    """

    def __init__(self, views):
        self.views = views
        self.steps = []
        self.index = 0
        self.wait = 0
        self.handle = None
        self.written = []

        # The file this run is currently waiting on, and how long it has waited. Empty when not
        # waiting on one.
        self.pending = None
        self.pending_ticks = 0

        # Shots that were requested and never appeared. Reported at the end, loudly, because a
        # partial set measured without complaint is worse than no set at all.
        self.lost = []

        self.plan()

    # ------------------------------------------------------------------ building the step list

    def plan(self):
        for config_id, baked, hardware, description in CONFIGS:
            self.steps.append(("say", "=== {}: {}".format(config_id, description)))
            self.steps.append(("bake", baked))
            self.steps.append(("cmd", "r.Lumen.HardwareRayTracing {}".format(hardware)))

            # A change of tracing path rebuilds what Lumen holds, so this one settles longer.
            self.steps.append(("wait", SETTLE_FRAMES))

            for view_name, eye, target in self.views:
                self.steps.append(("camera", (eye, target)))
                self.steps.append(("wait", SETTLE_FRAMES))

                for pass_name, visualize in PASSES:
                    self.steps.append(("cmd", "r.Lumen.Visualize {}".format(visualize)))
                    self.steps.append(("wait", 24))
                    self.steps.append(("shot", "{}__{}__{}".format(config_id, view_name, pass_name)))
                    self.steps.append(("await", "{}__{}__{}".format(config_id, view_name, pass_name)))

                self.steps.append(("cmd", "r.Lumen.Visualize 0"))

        self.steps.append(("done", None))

    # ------------------------------------------------------------------------------- the ticker

    def start(self):
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def tick(self, delta_seconds):
        # Waiting on a file takes precedence over everything, because the thing being waited out is
        # a renderer that is not currently drawing and no number of ticks is a substitute for the
        # file existing.
        if self.pending is not None:
            if os.path.exists(os.path.join(SHOT_DIR, self.pending + ".png")):
                self.pending = None
                self.pending_ticks = 0
                # A few frames past the file appearing, so the write is complete rather than merely
                # started before the next configuration begins changing the scene under it.
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

        if kind == "wait":
            self.wait = payload
        elif kind == "say":
            say(payload)
        elif kind == "cmd":
            console(payload)
        elif kind == "camera":
            eye, target = payload
            look_at(eye, target)
        elif kind == "bake":
            result = subsystem().set_house_render_mode(payload)
            say("BAKE {} -> {}".format(payload, result))
        elif kind == "shot":
            # Removed first, so "the file exists" can only mean THIS shot landed. A leftover of the
            # same name from an earlier run would otherwise satisfy the wait instantly and be
            # collected as this configuration's evidence - the same trap hf-lumen.ps1 clears the
            # whole directory for, guarded again here so the python is safe to run on its own.
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

    def finish(self):
        emit("WRITTEN", self.written)
        emit("LOST", self.lost)
        emit("SHOTDIR", SHOT_DIR)

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
        say("BUILD {}".format(subsystem().apply_spec_json(handle.read(), "")))

    home = house()
    if home is None:
        say("NO HOUSE - nothing to measure")
        unreal.SystemLibrary.quit_editor()
        return

    origin, extent = flat_bounds()
    emit("HOUSE_BOUNDS", {"origin": [origin.x, origin.y, origin.z], "extent": [extent.x, extent.y, extent.z]})

    # A flat is metres across. Anything smaller than a room means the bounds were read off the wrong
    # actor, and every camera derived from them is pointing at the origin - which produces images
    # that look like renders and measure nothing.
    if extent.x < 100.0 or extent.y < 100.0:
        say("ABORT: flat bounds are degenerate ({:.1f} x {:.1f} cm). Cameras would be meaningless."
            .format(extent.x * 2.0, extent.y * 2.0))
        unreal.SystemLibrary.quit_editor()
        return

    zeroed = zero_the_ambient_cubemap()
    emit("AMBIENT_CUBEMAP_ZEROED", {"volumes": zeroed})
    if zeroed == 0:
        say("WARNING: no PostProcessVolume was found to zero. Every number below may be the cubemap.")

    cube = place_control_cube(unreal.Vector(origin.x - extent.x - 400.0, origin.y, 150.0))
    emit("CONTROL_CUBE", {"placed": cube is not None})

    # ------------------------------------------------------------------------------- the views
    #
    # Aerial: the whole flat from outside and above, with the control cube in frame. This is the shot
    # that settles membership - either the flat appears in the Lumen Scene visualisation or it does
    # not, and the cube proves the visualisation ran.
    aerial_eye = unreal.Vector(origin.x - extent.x * 1.3, origin.y - extent.y * 1.3, extent.z * 3.2 + 400.0)
    aerial_target = unreal.Vector(origin.x, origin.y, origin.z)

    views = [("aerial", aerial_eye, aerial_target)]

    # Master bedroom, looking AWAY from the window at the far wall. That wall is the measurement
    # surface: it receives no direct sun, so what lands on it is bounce, and bounce is exactly what a
    # flat outside the Lumen scene does not have. Facing the window instead would measure the window.
    bedroom = find_element("R_MBed")
    if bedroom is not None:
        room_origin, room_extent = bounds_of(bedroom)
        eye = unreal.Vector(room_origin.x, room_origin.y + room_extent.y - 60.0, 150.0)
        target = unreal.Vector(room_origin.x, room_origin.y - room_extent.y, 140.0)
        views.append(("mbed", eye, target))
        emit("MBED", {"origin": [room_origin.x, room_origin.y, room_origin.z],
                      "extent": [room_extent.x, room_extent.y, room_extent.z]})
    else:
        say("WARNING: R_MBed not found; the interior view is skipped.")

    # Real-time mode off would leave the viewport not ticking, and the settle loop would settle
    # nothing at all. Game view removes the editor's grids and icons from the picture.
    console("r.ScreenPercentage 100")

    Run(views).start()


main()
