# Rule 04 — Code conventions

## Naming

All HouseForge types carry the `HF` prefix after the Unreal type letter:

- `AHFWallActor`, `UHFHouseSpec`, `FHFWall`, `EHFRoomType`, `SHFMaterialPanel`
- Automation tests are named `HouseForge.<Area>.<What>` so the whole suite matches `HouseForge.*`

## Module layout

```
Source/HouseForge/            Runtime — model, validation, geometry, actors, materials
  Public/Model/               USTRUCTs, UHFHouseSpec, HFSpecValidator
  Public/Actors/              AHF*Actor
  Private/Geometry/           pure mesh generators
  Private/Tests/              runtime automation tests

Source/HouseForgeEditor/      Editor — MCP toolset, subsystem, UI panels
  Private/Toolset/            UHFToolset (UToolsetDefinition)
  Private/UI/                 SHFMaterialPanel, SHFAssetReplacementPanel
  Private/Tests/              editor automation tests
```

## Units

**AutoCAD drawings are in millimetres. Unreal is in centimetres.**

Conversion happens exactly once, at spec ingest. Everything downstream of the spec is already in
Unreal centimetres — geometry code must never see millimetres. The spec declares its units
explicitly in a `Units` field; do not assume.

## Geometry generators stay pure

Every generator is a free function `(params) -> FDynamicMesh3`. No world access, no actor access,
no editor access, no asset loading. This is what makes them unit-testable without an editor or a
level, and it is not negotiable — if a generator needs world context, the context is passed in as
a parameter.

## Surface roles and polygroups

Every triangle a generator emits is assigned a polygroup tagged with a surface role (wall, floor
finish, ceiling soffit, cove interior, joinery carcass, shutter laminate, counter stone, glass,
metal hardware, skirting, door, window frame).

This is load-bearing: the material panel targets faces by role, and untagged geometry cannot be
re-materialled by the user. Generators also emit real-world-scale planar/box UVs so tiling
controls mean something.

## Generated geometry stays artist-editable

Elements are `UDynamicMeshComponent`s so Unreal's Modeling Tools can be taken to them after
generation. That is a requirement, not an implementation detail — do not bake to static meshes as
part of generation.

Anything that regenerates geometry **must** respect `AHFElementActor::bArtistEdited`. An element
flagged as hand-edited opts out of regeneration, and a house rebuild preserves the actor rather
than destroying and respawning it. Overwriting modelling work is a silent, unrecoverable loss, and
`RevertToGenerated` is the only thing allowed to discard it.

## The end goal is photoreal renders and a walkthrough

Everything generated is eventually lit, rendered and walked through. That sets a quality bar the
geometry has to meet, not just the materials:

- **No perfectly sharp edges.** A real edge has a small chamfer that catches light; a mathematically
  sharp one reads as CG under any lighting. Generated geometry needs an edge bevel option.
- **Normals must be controlled**, with hard and soft edges chosen deliberately rather than left to
  a blanket recompute.
- **A second UV channel** for lightmaps, so baked lighting stays an option alongside Lumen.
- **Glass needs thickness**, not a plane, or refraction and reflection look wrong.
- **Collision must match the visual mesh**, including on open doors, or a walkthrough passes
  through things.

Judge output by how it looks lit and in motion, not by how it reads in a wireframe screenshot.

## Anything that moves must be able to move

Doors swing, windows slide, drawers pull out, wardrobe shutters open. If a real one moves, the
generated one moves too.

That rules out merging a fixture into a single mesh. Every moving part is its own component with
its own pivot, a motion type (hinge or slide), an axis, travel limits, and an open amount driven as
a normalised 0..1 property. Baking a fixture bakes each part separately and keeps the articulation;
a bake must not weld a chest of drawers into a block.

Fixed parts of the same fixture may share one mesh. Only the moving parts need separating, so a
carcass stays a carcass and only its shutters and drawers are split out.

## Baking is non-destructive and reversible

Baking an element to a static mesh must **keep the dynamic mesh alongside it**. The whole workflow
lives in the editor, so baking is a rendering choice rather than a one-way door: an element can
always be switched back to its dynamic mesh and carry on being edited.

Bake state is a toggle, per element and in bulk — never a replacement. Do not discard the dynamic
mesh on bake, and do not offer a bake the user cannot undo.

## Actors own their data

Each generated element actor holds its own parameter struct as a `UPROPERTY(EditAnywhere)`.
`PostEditChangeProperty` calls `Regenerate()`. The house spec is the import/export format, **not**
a live second source of truth — do not introduce two-way sync between spec and actors.

## Asset overrides are non-destructive

Replacing a procedural fixture with a Content Browser asset never discards its parameter struct.
Clearing the override must restore the generated mesh exactly.

See also: [[01-scope]], [[03-validation-gate]]
