"""Driving HouseForge headlessly: build a spec, measure the level, render it, and look.

Imported by throwaway scripts run through Scripts/hf-view.ps1. Nothing here is on the build path;
it exists so that "render it and look" is a repeatable command rather than a session's worth of
rediscovery.

    import sys
    sys.path.insert(0, r"<plugin>/Scripts")
    import hf_view as hf

    hf.build()
    hf.shoot("mine.png", (500.0, 300.0, 160.0), (120.0, 60.0, 250.0))
    hf.emit("SOMETHING", {"measured": 1})
    hf.done()

Everything printed goes through say(), which writes to stdout AND logs a warning - a commandlet's
log swallows Display-level Python output, which is a long half hour to work out from an empty log.
"""

import json

import unreal

PLUGIN = r"D:/Projects/UnrealEngine/5.8/HouseBuilder/Plugins/HouseForge"
SPEC = PLUGIN + "/Reference/Specs/Sample2BHK.json"
SHOTS = PLUGIN + "/Saved/Screenshots"


def say(text):
    print(text, flush=True)
    unreal.log_warning(text)


def subsystem():
    return unreal.get_editor_subsystem(unreal.HFEditorSubsystem)


def build(spec_path=SPEC, level=""):
    """Builds a spec into the current level and returns the operation result."""
    with open(spec_path, "r", encoding="utf-8") as handle:
        text = handle.read()
    result = subsystem().apply_spec_json(text, level)
    say("HF BUILD: {}".format(result))
    return result


def all_actors():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()


def house():
    """The house actor. FindHouseActor is not a UFUNCTION, so this iterates the level."""
    for actor in all_actors():
        if actor and actor.get_class().get_name() == "HFHouseActor":
            return actor
    return None


def elements():
    """Every element actor the house owns."""
    out = []
    owner = house()
    if owner is None:
        return out
    for actor in owner.get_editor_property("element_actors"):
        if actor:
            out.append(actor)
    return out


def shoot(name, loc, look, res=1400, fov=70.0):
    """An offscreen perspective capture into Saved/Screenshots. Coordinates in centimetres."""
    out = subsystem().capture_view(name, res, unreal.Vector(*loc), unreal.Vector(*look), fov)
    say("HF SHOT {} -> {}".format(name, out))
    return out


def emit(tag, payload):
    """One machine-readable line the runner greps out of the log."""
    say("HFDATA {} {}".format(tag, json.dumps(payload)))


def done():
    say("HF DONE")
