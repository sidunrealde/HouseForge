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

## Actors own their data

Each generated element actor holds its own parameter struct as a `UPROPERTY(EditAnywhere)`.
`PostEditChangeProperty` calls `Regenerate()`. The house spec is the import/export format, **not**
a live second source of truth — do not introduce two-way sync between spec and actors.

## Asset overrides are non-destructive

Replacing a procedural fixture with a Content Browser asset never discards its parameter struct.
Clearing the override must restore the generated mesh exactly.

See also: [[01-scope]], [[03-validation-gate]]
