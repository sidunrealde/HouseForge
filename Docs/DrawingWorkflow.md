# Reading a drawing into a level

The whole point of HouseForge: you hand it an interior drawing, Claude reads it, and a level comes
out that you can then edit.

## What is and is not on this path

**Houses are built from a spec read out of a drawing. There is no other way in.**

The reference 2BHK (`FHFSampleHouse`) is *not* a template the plugin builds from. It exists for two
reasons only: to produce the reference drawing set, and to give the tests a known-good house. The
committed `Reference/Specs/Sample2BHK.json` is **ground truth to diff against**, never build input
— the acceptance test is precisely that Claude reads the *PNGs* and rebuilds the spec from what it
sees. Reading the JSON would defeat the test entirely.

This is enforced, not just documented: `HouseForge.Architecture.SampleIsNotOnTheBuildPath` scans
every production source file and fails the merge gate if any of them reference the sample. A
"build the sample house" convenience button would quietly become the path everyone uses, so there
isn't one.

## Setup, once

1. **Tools > Start Unreal MCP Server.** Starts the engine's MCP server and writes `.mcp.json` into
   the project folder for Claude Code. To have it start with the editor, tick *Auto Start Server*
   under **Editor Preferences > Plugins > Model Context Protocol**.
2. **Run `Scripts\hf-drawings.ps1` once.** Provisions the local Python environment
   (`Scripts/.venv`, gitignored) that PDF import and the drawing generator need. Nothing is
   installed system-wide.

## The loop

**1. Import the drawings.** *Tools > Import Interior Drawings…* takes PNG and JPG as-is, and
rasterises PDF sheet sets to one PNG per page — which is how AutoCAD sets usually arrive. Files
land in `Reference/Drawings/<set>/`. Claude can also import them itself with the `ImportDrawings`
tool.

**2. Claude reads them.** `ListDrawings` reports what is available, `GetDrawingsPath` gives the
folder to open them from. Claude reads the images the way you would — plan sheets for the layout,
the reflected ceiling plan for false ceilings and beams, the electrical sheet for services,
elevations for joinery heights and shutter counts.

**3. Claude writes a House Spec** conforming to [`HouseSpecSchema.md`](HouseSpecSchema.md).
Every wall, room and fixture must come from something visible in a drawing — nothing invented.

**4. Validate before building.** `ValidateSpec` runs every rule in one pass and reports each
problem with the rule name and the offending numbers, so a misread can be corrected in one round
rather than one problem per round trip.

**5. Apply.** `ApplySpec` builds into a level. It **refuses** a spec with validation errors — half
a house would screenshot plausibly while the spec behind it is wrong, and the comparison in step 6
would silently pass.

**6. Capture and compare.** `CaptureTopDown` writes a top-down orthographic view under
`Saved/Screenshots`. Compare it against the source plan. This step is what closes the loop —
without it Claude is building blind and cannot tell whether what it read matches what it built.

**7. Correct.** `ListElements` to orient, `ModifyElement` to patch individual fields,
`DeleteElement` to remove something misread. Every change is re-validated and **rolled back if it
would break the spec**, so a bad edit cannot leave the level in a state the builder would choke on.
Deleting a wall takes its openings with it rather than leaving dangling references.

**8. Save.** `SaveSpec` writes the result to `Reference/Specs/`, so a level can always be traced
back to the spec and the drawing it came from.

## Units

**Read the units off the drawing before anything else.** They may be millimetres, centimetres,
metres, feet or inches — Indian residential sets are usually millimetres, but imperial sets exist
and the plugin supports both.

Look for a title block note, a dimension string's suffix, or a scale bar, and record where you
found it in `unitsSource`. Use the `ConvertLength` tool for imperial dimensions rather than
converting `12'-6"` in your head.

This matters more than it looks. A unit misread leaves the spec perfectly self-consistent — walls
meet, openings fit, areas are internally correct — and simply builds the house at the wrong scale.
No structural rule can catch that, which is why `ImplausibleScale` checks the total floor area
against what a dwelling actually measures, and names the unit that would have been right.

Conversion to Unreal centimetres happens exactly once, when the house actor takes the spec.
Everything after that — tools, preview, geometry — is in centimetres and never has to ask.

## Editing the result

Everything generated is a `UDynamicMeshComponent`, which means **Unreal's Modeling Tools work on it
directly** — sculpt, cut, bevel, weld, whatever the geometry needs. That is the point of using
dynamic meshes rather than baking static meshes up front.

There are two levels of editing, and they don't fight each other:

**Parametric.** Select a wall and change its thickness, height or openings in the details panel;
only that wall rebuilds. Each element actor owns its own parameter struct, which is why editing one
thing doesn't disturb the rest of the house.

**By hand.** Take the Modeling Tools to any element. The moment its mesh changes outside of
generation, the element is flagged `bArtistEdited` and **opts out of regeneration** — parameter
changes and whole-house rebuilds both leave it alone, and a house rebuild preserves the actor
rather than destroying and respawning it. Modelling work is never silently overwritten.

To go back, use **Revert To Generated** on the element (or untick `bArtistEdited`). That is the only
thing that discards hand edits, and it is explicit.

## What generates today

Walls with their openings boolean-cut out, floor slabs, skirting that stops at doorways, beams,
columns, door leaves, and window frames with glazing. Every triangle carries a surface-role
polygroup and real-world-scale UVs, so one UV tile is a fixed number of centimetres — that is what
lets the material panel express tiling in millimetres later.

Beams generate regardless of ceilings. A false ceiling does not create or delete a beam, it just
conceals it — so in a room with no false ceiling the beams are simply visible, which is correct.

`AHFHouseActor` also keeps the colour-coded wireframe preview, now off by default. It is still
useful when the meshes look wrong and you want to see what the spec actually says, and it is the
only thing that draws door swing arcs.
