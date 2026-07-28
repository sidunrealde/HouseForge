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

Drawings are in millimetres; Unreal is centimetres. The spec declares its own units and conversion
happens exactly once, when the house actor takes the spec. Everything after that — tools, preview,
geometry — is in centimetres and never has to ask.

## Current state of the geometry

`AHFHouseActor` holds the spec and draws a colour-coded wireframe: walls, openings, rooms, beams,
columns, fixtures and false-ceiling soffits. That is deliberate scaffolding for this milestone — it
makes the read-to-level loop verifiable by screenshot now. The mesh generators replace it without
changing anything the tools see.

Beams generate regardless of ceilings. A false ceiling does not create or delete a beam, it just
conceals it — so in a room with no false ceiling the beams are simply visible, which is correct.
