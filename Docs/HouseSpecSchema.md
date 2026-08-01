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

**Units — read them, never assume them.** The spec declares its own units in `units`:
`Millimeters`, `Centimeters`, `Meters`, `Feet`, `Inches`. Every length in the file is in those
units, and conversion to Unreal centimetres happens exactly once at ingest — never mix units within
one spec.

A unit misread is the most dangerous mistake possible here, because it leaves the spec **perfectly
self-consistent**: every wall still meets, every opening still fits, every room area is internally
correct. The house is simply built at the wrong scale, and no structural rule can detect it. So:

- **Find the units on the drawing** — a title block note ("ALL DIMENSIONS IN MILLIMETERS"), a
  dimension string's suffix, or a scale bar — and record where you found it in **`unitsSource`**.
  The validator warns when that field is blank, because a blank one means the units were guessed.
- **Use `ConvertLength` for imperial.** Converting `12'-6"` by hand is exactly the arithmetic that
  goes wrong quietly. The tool accepts `12'-6"`, `12' 6"`, `12.5'`, `78"`, and metric forms with or
  without a suffix.
- The `ImplausibleScale` rule is the backstop: it checks the total floor area is that of a real
  dwelling, and when it isn't, it tells you which unit *would* have been right.

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
| `units` | enum | `Millimeters` \| `Centimeters` \| `Meters` \| `Feet` \| `Inches` |
| `unitsSource` | string | **Where the units were read from.** Warned about if blank |
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

**`kind` decides what moves**, so it is not cosmetic:

| Kind | What is built | What moves |
|---|---|---|
| `Door` | One leaf | Hinged on the jamb the `swing` names |
| `SlidingDoor` | Two panels on two tracks | One panel, until its far edge meets the fixed one's |
| `Window` | Frame, mullion if wide, fixed glazing | Nothing |
| `SlidingWindow` | Two-track frame with sill tracks, two glazed sashes | One sash, the same way a sliding door's panel does |
| `Archway` | A hole, and nothing else | Nothing |
| `Ventilator` | Frame and one top-hung sash | The sash, pivoting on its head |

A `SlidingWindow` is the default for a habitable-room window in these flats — a 27 mm two-track
aluminium unit. Use `Window` only where the glazing genuinely is fixed. A `Ventilator` is built as
a top-hung pivot sash; where one is genuinely a fixed louvre, `Window` describes it honestly.

An opening too small to divide falls back to fixed glazing and says so in the log, rather than
producing sashes too narrow to be anything.

**Read the swing arc.** Every hinged door on a plan is drawn with a quarter-circle showing which
side it is hinged and which way it opens. `Left`/`Right` picks the hinge end (the near or far end
of the opening as measured from the host wall's `start`); `Inward`/`Outward` picks the side the
leaf sweeps into. A hinged door with `swing: None` raises `MissingSwing`, and a leaf that sweeps
into solid construction raises `SwingBlocked`. The preview draws the leaf and its arc, so a door
hung on the wrong side is visible in a top-down capture rather than silently wrong.

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

### Name a template; do not tune six numbers

**`template` is the field to write.** It names a DESIGN, and the project's False Ceilings settings
decide what that design means here — band width, drop, cove section, downlight pitch, and how big a
ring is needed to bury a beam. Everything below it is then filled in before anything validates or
builds, so what is committed and what a validator reads are still plain numbers.

| Template | What you get |
|---|---|
| `Custom` | Nothing is stamped. The figures below stand exactly as written. |
| `PlainBand` | A shallow perimeter band with a run of recessed downlights in it |
| `Cove` | The same band with a trough at its inner edge throwing light UP onto the slab |
| `SteppedTray` | Two levels: an outer band and a shallower step inside it |
| `FramedPanel` | A frame band round a centre panel that sits HIGHER than the frame, cove between |

Write `Custom` only for a ceiling whose figures are genuinely particular — a wet area's full drop, or
a bulkhead following its own polygon. A `Custom` ceiling gets no beam ring and no downlight run laid
out for it, so both have to be written by hand.

### The figures

| Field | Type | Notes |
|---|---|---|
| `id`, `roomId` | name | |
| `template` | enum | see above. Prefer this to writing the figures |
| `style` | enum | see below |
| `drop` | number | How far below the slab the ceiling hangs. **100–200 is the range**, not 500 |
| `bandWidth` | number | Width of the perimeter band; ignored by `FullDrop` |
| `innerDrop` | number | Drop of the step inside the band, for `Tray`. `0` means half the outer drop |
| `centrePanelDrop` | number | A panel filling the centre, this far below the slab. Must be **less** than `drop` — it is recessed above the band, not hung below it. `0`: no panel |
| `perimeterBulkheadWidth` | number | A deeper ring round the outside, boxing in the beams that run round the room |
| `perimeterBulkheadDrop` | number | Drop of that ring. Deeper than `drop`, or no ring is built |
| `cove` | object | `{ channelWidth, lipHeight, setback, bHasLedStrip, stripWidth, stripHeight, stripSetback }` |
| `downlight` | object | `{ cutoutDiameter, flangeDiameter, flangeProjection, bodyDepth, bRecessed }` |
| `explicitPolygon` | array of `{x, y}` | Overrides the room boundary. **Required for `Bulkhead`** |
| `lightPositions` | array of `{x, y}` | Recessed downlights, bored through the soffit where they fit |

| Style | Meaning |
|---|---|
| `None` | Open to the slab |
| `Peripheral` | Dropped band around the perimeter, centre left at slab height |
| `FullDrop` | Whole room dropped uniformly — usual over wet areas, to hide plumbing |
| `Tray` | Stepped levels, inner region higher |
| `Cove` | Peripheral band with a recessed channel hiding an LED strip |
| `Bulkhead` | Localised drop over a wardrobe run, kitchen counter or corridor |

### A beam is boxed in where it runs; it does not deepen the room

A ceiling shallower than a beam crossing its room leaves the beam hanging through the finished
soffit, and `CeilingDoesNotClearBeam` says so. **The answer is not a deeper ceiling.** Nobody drops
twenty-four square metres of living room by half a metre to hide one beam, and doing it destroys the
design — a cove at the bottom of a 500 well is a 6:1 trough that absorbs its own light.

Two remedies the rule accepts, both positional:

- **A perimeter bulkhead ring**, for a beam running along a wall line — which in these layouts is
  every beam. A 230 beam over a 115 partition stands 57.5 proud of the plaster on both faces, and a
  ring 300 wide dropping 480 buries it while everything further in stays at 150. A templated ceiling
  derives its own ring from the beams; a `Custom` one has to state it.
- **A separate `Bulkhead` ceiling** with its own polygon, for a beam crossing the open interior of a
  room. It has to be over the beam along the beam's whole run inside that room.

### Everything in the room answers to the ceiling, and it is resolved at build time

**State the height the drawing states. Do not adjust anything for the ceiling.** A pelmet goes in at
the 2350 the drawing marks it at, even if the finished soffit over it is going to be at 2520.

A false ceiling here is *derived*: a drawing names a template, the project's settings say what that
template's figures are, and the beams over the room decide the perimeter ring. None of the three is
something a drawing states, and all three change when somebody drags a slider. So a fixture height
worked out against a ceiling would be a copy of a number that goes stale the moment they do.

`FHFCeilingFit` therefore resolves it at build time, from the soffit that actually ends up over each
fixture's **whole footprint** — not over its centre, because a 2.2 m run crosses a 300 ring and a 450
band without its middle leaving one of them. What gives depends on what the thing is:

| Fixture | What happens |
|---|---|
| `CeilingFan` | Its rod lengthens and its canopy drops onto the soffit; it hangs from the slab |
| `LightFixture` | `baseZ` is a drop below the *finished soffit* rather than below the slab |
| `ExhaustFan`, `ACIndoorUnit`, `Geyser`, `Chimney`, `Pelmet`, `Curtain` | Keeps its size, slides **down** only as far as it must, never up |
| `Wardrobe`, `LoftUnit`, `KitchenWallCabinet`, `KitchenTallUnit`, `Bookshelf` | Keeps its base, is **cut shorter** — what a carpenter does |
| everything else | Left exactly as drawn |

Nothing is written back into the spec, so the file keeps saying what the drawing says. When nothing
can be made to fit, the fitting is built as drawn and `CeilingLeavesNoRoomForFixture` says so — a
silent fudge would hide a design fault.

An extract that has to drop takes the duct cored through its wall with it, and the size the fit works
against is what actually gets **built**: an extract's bezel laps the corners of that chase, so a fan
drawn 250 stands 316 tall.

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

`ImplausibleScale` is an error and is the guard against a unit misread — see the units note above.

**Warnings** are worth reading but do not block: `MissingUnitsSource`, `ImplausibleCeilingHeight`,
`ImplausibleWallThickness`, `ImplausibleDoorSize`, `MissingSwing`, `SwingBlocked`,
`DoorWithSill` (probably a mislabelled window),
`LowHeadroom` (under 2100 clear), `CeilingBelowDoorHead`, `CeilingDoesNotClearBeam`,
`CeilingLeavesNoRoomForFixture`, `BeamLowHeadroom`, `UnknownFixtureType`, `OverlappingFixtures`,
`FixtureFootprintCrossesWall`.

`CeilingBelowDoorHead` covers windows and ventilators as well as doors, and asks positionally: an
opening is cut into a wall, and the wall line is exactly where a perimeter bulkhead ring hangs
lowest. A shallow band away from the wall really does sit clear of a door head, and the same
question answers that too.

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
