# House Spec schema

The House Spec is the contract between a drawing and the geometry. Reading a drawing produces one
of these; the validator checks it; the plugin builds it. No geometry command is ever issued
directly — which is what keeps generation deterministic, testable without an LLM, and re-runnable.

Canonical example: [`Reference/Specs/Sample2BHK.json`](../Reference/Specs/Sample2BHK.json), a
generated artifact of `FHFSampleHouse::Make2BHK()`. Regenerate it with the console command
`HouseForge.ExportSampleSpec`.

Defined in [`HFTypes.h`](../Source/HouseForge/Public/Model/HFTypes.h). Field names are camelCase.

---

## Conventions

**Units.** The spec declares its own units in `units` — `Millimeters` (what AutoCAD drawings almost
always use), `Centimeters`, or `Meters`. Every length in the file is in those units. Conversion to
Unreal centimetres happens exactly once at ingest, so never mix units within one spec.

**Coordinates.** Right-handed 2D plan, X to the east, Y to the north. Z is height above the floor.
Put the origin at a convenient corner of the plan and stay consistent.

**Ids.** Every element needs an id unique within its collection. Use readable ids (`W_South`,
`R_Kitchen`, `D_Main`) — they appear in validation messages and in the editor outliner.

**Room boundaries** are wound counter-clockwise and the closing edge is implicit. **Do not repeat
the first point at the end**; that is the single most common authoring mistake and the validator
rejects it, because a zero-length edge breaks the polygon offset used for false ceilings.

---

## Top level

| Field | Type | Notes |
|---|---|---|
| `schemaVersion` | int | `1` |
| `name` | string | Human name for the unit |
| `sourceDrawing` | string | Path of the drawing this was read from, for traceability |
| `units` | enum | `Millimeters` \| `Centimeters` \| `Meters` |
| `defaultWallThickness` | number | Fallback for walls that don't state one |
| `defaultWallHeight` | number | Fallback floor-to-slab height |
| `walls` | array | see below |
| `openings` | array | |
| `beams` | array | downstand beams |
| `columns` | array | |
| `rooms` | array | |
| `falseCeilings` | array | |
| `fixtures` | array | |

## `walls[]`

Described by **centreline**, not face lines — so changing `thickness` grows the wall symmetrically
without disturbing its neighbours' junctions.

| Field | Type | Notes |
|---|---|---|
| `id` | name | Referenced by openings and by fixture `anchorWallId` |
| `start`, `end` | `{x, y}` | Centreline endpoints |
| `thickness` | number | 230 external / 115 internal is typical in mm |
| `height` | number | 3000 typical in mm; use ~1100 for balcony parapets |
| `baseZ` | number | Non-zero for parapets and half-walls |
| `bIsExternal` | bool | External/structural walls |
| `surfaceRole` | enum | Defaults to `WallPaint` |

## `openings[]`

| Field | Type | Notes |
|---|---|---|
| `id` | name | |
| `wallId` | name | Host wall |
| `offsetAlongWall` | number | From the host wall's `start` to the opening's **centre**, measured along the centreline |
| `width`, `height` | number | |
| `sillHeight` | number | Bottom edge above the wall base. `0` for doors, ~900 for windows |
| `kind` | enum | `Door` \| `SlidingDoor` \| `Window` \| `SlidingWindow` \| `Archway` \| `Ventilator` |
| `swing` | enum | `None` \| `InwardLeft` \| `InwardRight` \| `OutwardLeft` \| `OutwardRight` |

`offsetAlongWall` is measured from `start`, so direction matters. If a wall runs north-to-south,
offsets count downward from its northern end.

## `beams[]`

RCC downstand beams. **These are the reason false ceilings exist in this domain** — a beam hanging
450 below the slab has to be boxed in, and the ceiling drop is chosen to clear the deepest beam
crossing the room. Modelled as first-class elements rather than fixtures because they are
structural: they cannot be moved to suit the furniture, and the ceiling works around them.

| Field | Type | Notes |
|---|---|---|
| `id` | name | |
| `start`, `end` | `{x, y}` | Centreline, usually following the wall below |
| `width` | number | 230 typical in mm |
| `depth` | number | How far the beam hangs **below the slab soffit** |
| `soffitZ` | number | Slab level the beam hangs from; matches the storey's ceiling height |
| `surfaceRole` | enum | Defaults to `Structure` |

A beam whose centreline runs along a room's boundary is concealed by the wall itself and is **not**
treated as crossing that room — only beams passing through the interior force a ceiling drop.

## `columns[]`

| Field | Type | Notes |
|---|---|---|
| `id` | name | |
| `position` | `{x, y}` | Centre in plan |
| `size` | `{x, y}` | Plan size before rotation; 230 x 450 typical |
| `rotationDegrees` | number | |
| `height`, `baseZ` | number | |
| `surfaceRole` | enum | Defaults to `Structure` |

## `rooms[]`

| Field | Type | Notes |
|---|---|---|
| `id`, `name` | name, string | `name` is the drawing label, e.g. "Master Bedroom" |
| `type` | enum | `Living`, `Dining`, `Kitchen`, `Utility`, `Bedroom`, `MasterBedroom`, `Bathroom`, `Toilet`, `Balcony`, `Foyer`, `Corridor`, `Study`, `Storage`, `Unknown` |
| `boundary` | array of `{x, y}` | CCW polygon, closing edge implicit, at least 3 points |
| `floorZ` | number | Floor level; drop balconies and bathrooms slightly if the drawing says so |
| `ceilingHeight` | number | Floor to **slab**, before any false ceiling drop |
| `floorRole` | enum | Surface role for the floor finish |
| `skirtingHeight` | number | `0` disables skirting — correct for bathrooms and balconies |

## `falseCeilings[]`

One per room that has one. Every drawing in this domain has several.

| Field | Type | Notes |
|---|---|---|
| `id`, `roomId` | name | |
| `style` | enum | see below |
| `drop` | number | How far below the slab the ceiling hangs |
| `bandWidth` | number | Width of the perimeter band; ignored by `FullDrop` |
| `cove` | object | `{ channelWidth, lipHeight, setback, bHasLedStrip }` |
| `explicitPolygon` | array of `{x, y}` | Overrides the room boundary. **Required for `Bulkhead`** |
| `lightPositions` | array of `{x, y}` | Recessed spotlights |

| Style | Meaning |
|---|---|
| `None` | Open to the slab |
| `Peripheral` | Dropped band around the perimeter, centre left at slab height |
| `FullDrop` | Whole room dropped uniformly — usual over wet areas, to hide plumbing |
| `Tray` | Stepped levels, inner region higher |
| `Cove` | Peripheral band with a recessed channel hiding an LED strip |
| `Bulkhead` | Localised drop over a wardrobe run, kitchen counter or corridor |

## `fixtures[]`

Joinery, furniture, sanitary ware and electrical fittings.

| Field | Type | Notes |
|---|---|---|
| `id`, `roomId` | name | |
| `type` | enum | see below |
| `label` | string | The drawing's own label, if any |
| `position` | `{x, y}` | Centre of the footprint |
| `rotationDegrees` | number | Yaw about the centre; `0` means `footprint.x` runs along +X |
| `footprint` | `{x, y}` | Width × depth **before** rotation |
| `height` | number | |
| `baseZ` | number | Underside above the room floor. Non-zero for wall cabinets and counters |
| `anchorWallId` | name | Wall this fixture backs onto. Optional but important — see below |
| `params` | object | see below |

### `type` — `EHFFixtureType`

| Group | Values |
|---|---|
| Joinery | `Wardrobe`, `LoftUnit`, `KitchenBaseCabinet`, `KitchenWallCabinet`, `KitchenTallUnit`, `CounterTop`, `TVUnit`, `StudyTable`, `Bookshelf`, `Vanity` |
| Appliances / sanitary | `Sink`, `Hob`, `Chimney`, `Refrigerator`, `WashingMachine`, `WC`, `Basin`, `Shower`, `ShowerPartition` |
| Loose furniture | `Bed`, `Nightstand`, `Sofa`, `Chair`, `DiningTable`, `CoffeeTable` |
| Electrical services | `PowerSocket`, `SwitchPlate`, `DistributionBoard`, `ACIndoorUnit`, `ACOutdoorUnit`, `Geyser`, `ExhaustFan`, `CeilingFan`, `LightFixture` |
| Architectural fittings | `ShoeRack`, `Pelmet`, `Mirror`, `TowelRail`, `Railing`, `WallNiche`, `Curtain` |

The electrical group is what the electrical layout sheet is drawn from, so tag those correctly —
a socket typed as `Unknown` will not appear on that sheet.

**Set `anchorWallId` whenever a fixture backs onto a wall.** Two things depend on it: asset
replacement aligns anchored fixtures to the wall face rather than the floor plane, and the
validator exempts them from the footprint check — room boundaries run along wall centrelines, so
an anchored wardrobe is *supposed* to cross the boundary.

### `params`

One flat bag rather than a per-type hierarchy, so it round-trips without custom converters. Fill
only what the fixture type cares about; each generator reads what applies and ignores the rest.

| Field | Applies to |
|---|---|
| `shutterCount`, `shelfCount`, `drawerCount` | Any joinery |
| `bHasLoft`, `loftHeight` | Wardrobes — a storage box above, standard in Indian bedrooms |
| `bHasHangingRail` | Wardrobes |
| `plinthHeight` | Any floor-standing carcass |
| `handleStyle` | `None` \| `Bar` \| `Knob` \| `JProfile` \| `HandlelessGroove` |
| `bHasGlassInsert` | Crockery units, display shutters |
| `upstandHeight` | Counters — backsplash. `0` disables |
| `corniceHeight` | Wall cabinets — top moulding. `0` disables |
| `diameter` | Ceiling fans (blade span), round lights |
| `gangCount` | Switch plates |

---

## Validation

`FHFSpecValidator::Validate` runs **every** rule in one pass rather than stopping at the first
failure, because a misread drawing usually has several problems and fixing them one per round-trip
is needlessly slow. Each issue carries a stable `code` and a message quoting the offending numbers.

**Errors** block generation. Representative rules: `NoWalls`, `NoRooms`, `DuplicateWallId`,
`MissingId`, `ZeroLengthWall`, `NonPositiveWallThickness`, `UnknownWallReference`,
`UnknownRoomReference`, `OpeningExceedsWall`, `OpeningExceedsWallHeight`,
`NonPositiveOpeningSize`, `UnclosedRoom`, `RepeatedClosingPoint`, `DegenerateRoom`,
`NonPositiveCeilingHeight`, `CeilingDropExceedsRoom`, `NonPositiveCeilingDrop`,
`MissingCeilingBand`, `BulkheadNeedsPolygon`, `FixtureOutsideRoom`, `NonPositiveFootprint`.

Structural rules: `ZeroLengthBeam`, `NonPositiveBeamSize`, `BeamDepthExceedsStorey`,
`NonPositiveColumnSize`, `NonPositiveColumnHeight`, `DuplicateBeamId`, `DuplicateColumnId`.
Openings sharing a wall and a height range raise `OpeningsOverlap`.

**Warnings** are worth reading but do not block: `DoorWithSill` (probably a mislabelled window),
`LowHeadroom` (under 2100 clear), `CeilingBelowDoorHead`, `CeilingDoesNotClearBeam`,
`BeamLowHeadroom`, `UnknownFixtureType`, `OverlappingFixtures`, `FixtureFootprintCrossesWall`.

`CeilingDoesNotClearBeam` is the one worth understanding. A peripheral or cove ceiling leaves the
centre of the room at slab height and so conceals nothing mid-span; it only escapes the warning
when a `Bulkhead` in the same room, deep enough to cover the beam, boxes it in. That pairing —
cove around the perimeter, bulkhead along the beam — is exactly how it is detailed in practice, and
is what the reference 2BHK's living room does.

Two overlap cases are deliberately **not** reported, because flagging them would make the rule
noise: fixtures stacked vertically with no shared height range (a wall cabinet over a counter), and
inset fittings in a cabinet run (a sink cut into a worktop, a basin over a vanity).

## Worked fragment

```json
{
  "schemaVersion": 1,
  "name": "Sample 2BHK",
  "units": "Millimeters",
  "defaultWallThickness": 115,
  "defaultWallHeight": 3000,
  "walls": [
    { "id": "W_South", "start": {"x": 0, "y": 0}, "end": {"x": 10800, "y": 0},
      "thickness": 230, "height": 3000, "bIsExternal": true, "surfaceRole": "WallPaint" }
  ],
  "openings": [
    { "id": "D_Balcony", "wallId": "W_South", "offsetAlongWall": 2100,
      "width": 1800, "height": 2100, "sillHeight": 0, "kind": "SlidingDoor", "swing": "None" }
  ],
  "rooms": [
    { "id": "R_Living", "name": "Living / Dining", "type": "Living",
      "boundary": [ {"x":0,"y":0}, {"x":6600,"y":0}, {"x":6600,"y":3600}, {"x":0,"y":3600} ],
      "ceilingHeight": 3000, "skirtingHeight": 100 }
  ],
  "falseCeilings": [
    { "id": "FC_Living", "roomId": "R_Living", "style": "Cove", "drop": 200, "bandWidth": 600,
      "cove": { "channelWidth": 80, "lipHeight": 50, "setback": 20, "bHasLedStrip": true },
      "lightPositions": [ {"x":1200,"y":1200}, {"x":5400,"y":2400} ] }
  ],
  "fixtures": [
    { "id": "F_TVUnit", "roomId": "R_Living", "type": "TVUnit", "label": "TV unit with drawers",
      "position": {"x":2100,"y":400}, "footprint": {"x":1800,"y":450}, "height": 600,
      "rotationDegrees": 0, "baseZ": 0, "anchorWallId": "W_South",
      "params": { "drawerCount": 3, "handleStyle": "HandlelessGroove", "plinthHeight": 80 } }
  ]
}
```
