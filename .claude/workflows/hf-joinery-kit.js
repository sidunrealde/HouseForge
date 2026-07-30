export const meta = {
  name: 'hf-joinery-kit',
  description: 'Articulated joinery parts - shutters, drawers, handles, plinths, cornices, shelves - that actually open',
  whenToUse: 'HouseForge milestone 8. Run before hf-fixtures; every fixture composes from this kit.',
  phases: [
    { title: 'Articulation', detail: 'the movable-part framework everything else hangs off' },
    { title: 'Research', detail: 'real joinery proportions and existing generator conventions' },
    { title: 'Implement', detail: 'one part at a time, each built and tested' },
    { title: 'Retrofit', detail: 'make the existing door leaves and windows articulate too' },
    { title: 'Verify', detail: 'adversarial review, then composition and motion checks' },
  ],
}

const PLUGIN = 'd:/Projects/UnrealEngine/5.8/HouseBuilder/Plugins/HouseForge'
const ENGINE = 'd:/EpicGames/Engine/UE_5.8'

const RULES = `
Non-negotiable, from ${PLUGIN}/.claude/rules/:

ANYTHING THAT MOVES MUST BE ABLE TO MOVE. Doors swing, windows slide, drawers pull out, wardrobe
shutters open. If a real one moves, the generated one moves too. Every moving part is its own
component with its own pivot, a motion type, an axis, travel limits, and a normalised 0..1 open
amount. Fixed parts may share one mesh; only moving parts are separated.

Also:
- All changes inside the plugin. Branch off develop as feature/joinery-kit.
- Generators stay pure: (params) -> FDynamicMesh3, no world or asset access. Articulation is the
  actor's job; a generator returns a part's mesh in that part's own local space.
- Every triangle carries a surface-role polygroup and real-world-scale UVs.
- Respect bArtistEdited; never regenerate over hand-modelled work.
- Baking is reversible and must bake each part separately, keeping the articulation.
- Scripts/hf-validate.ps1 must pass before merging.
- Assert on measurable properties - volume, watertightness, bounds, and for moving parts the swept
  transform. Never on triangle counts; they pass for the wrong reasons.
`

// Budget per item, never across the array. Slicing a serialised array at a fixed length lets a
// long first item push every later one out of the prompt, silently - a design workflow did exactly
// that, delivered one of three designs to its judges, and produced a "winner" that was really the
// only candidate. Truncation should cost an item its own tail and say so.
const summarise = (items, perItem = 3000) => (items ?? [])
  .map((item, index) => {
    const text = typeof item === 'string' ? item : JSON.stringify(item ?? null, null, 1)
    if (!text) return `[${index + 1}] (empty - that agent returned nothing)`
    const body = text.length > perItem
      ? `${text.slice(0, perItem)}\n[...truncated, ${text.length - perItem} chars omitted]`
      : text
    return `[${index + 1}] ${body}`
  })
  .join('\n\n')

phase('Articulation')

// The framework has to exist first: getting it wrong means redoing every part.
const framework = await agent(
  `${RULES}

Design and implement the articulation framework. Everything else in this milestone depends on it,
so it is worth getting right before any joinery exists.

Read first: ${PLUGIN}/Source/HouseForge/Public/Actors/HFElementActors.h and its .cpp, to see how
elements currently hold a single UDynamicMeshComponent and how bArtistEdited protects it. Read
HFGenerators.h for the generator conventions. In ${ENGINE}, check how UDynamicMeshComponent behaves
as a child component with its own relative transform.

Design:
- A part descriptor: mesh, pivot transform, motion type (None, Hinge, Slide), axis, travel limits
  (angle in degrees for a hinge, distance in centimetres for a slide), and OpenAmount as 0..1.
- An AHFArticulatedActor base, deriving from AHFElementActor, holding a fixed-geometry component
  plus one component per moving part. Opening a part sets its relative transform from OpenAmount.
- How OpenAmount is exposed: per part in the details panel, plus an actor-level "open everything"
  for checking, and something an artist can drive.
- How this survives regeneration: parts are rebuilt, but a part the artist opened should stay open,
  and a hand-edited part must still be protected.
- How baking handles it - each part bakes separately, articulation preserved.

Implement it with tests: a hinge at OpenAmount 0 sits closed and at 1 sits at its limit; a slide
travels its declared distance; open amounts survive a regeneration; a hand-edited part is still
protected.

Build and run the gate before reporting. Report the exact API other parts must code against.`,
  { label: 'articulation:framework', phase: 'Articulation', effort: 'high' })

phase('Research')

const research = await agent(
  `${RULES}

Articulation framework now in place:
${String(framework).slice(0, 6000)}

Establish realistic proportions for Indian residential joinery, in millimetres: carcass board
thickness, shutter reveal gaps, plinth height and recess, drawer front graduation (real banks are
deeper at the bottom, not evenly divided), drawer travel and runner setback, cornice projection,
shelf spacing and thickness, hanging rail height and diameter, hinge positions and swing clearance.

For each moving part also establish its motion: which way a shutter is hinged and how far it opens,
how far a drawer pulls out relative to its depth, and what stops it.

Read FHFFixtureParams in Model/HFTypes.h - the kit must be driven by those fields. Flag any moving
part whose motion cannot be described by the existing parameters.`,
  { label: 'research:proportions', phase: 'Research', schema: {
    type: 'object',
    properties: {
      proportions: { type: 'array', items: { type: 'object', properties: {
        part: { type: 'string' }, dimension: { type: 'string' }, valueMm: { type: 'string' }, confidence: { type: 'string' },
      }, required: ['part', 'dimension', 'valueMm'] } },
      motions: { type: 'array', items: { type: 'object', properties: {
        part: { type: 'string' }, motionType: { type: 'string' }, pivot: { type: 'string' }, travel: { type: 'string' },
      }, required: ['part', 'motionType', 'travel'] } },
      paramGaps: { type: 'array', items: { type: 'string' } },
    },
    required: ['proportions', 'motions'],
  } })

phase('Implement')

const CONTEXT = `${RULES}

Articulation framework:
${String(framework).slice(0, 8000)}

Proportions and motions:
${JSON.stringify(research ?? {}, null, 1).slice(0, 10000)}

The kit lives in ${PLUGIN}/Source/HouseForge/Public/Geometry/HFJoineryKit.h and its .cpp. A
generator returns a part's mesh in the part's own local space, with its pivot at the origin, so the
actor can articulate it without unpicking a world transform.

After each part: build, run Scripts/hf-validate.ps1 -SkipBuild, fix failures before moving on.
`

const PARTS = [
  { key: 'shutter', moves: true, brief: 'A hinged shutter with a reveal gap around it and an optional glass insert. Hinged on its leading edge, opening to about 100 degrees. Reveal gaps are what stop a run of shutters reading as one slab.' },
  { key: 'drawer', moves: true, brief: 'A drawer - front, box and runners - that slides out. Banks are graduated, deeper at the bottom. Travel is most of the box depth, not all of it.' },
  { key: 'handle', moves: false, brief: 'Bar, knob, J-profile and handleless-groove, matching EHFHandleStyle. A J-profile is a recess in the shutter edge, not an applied part. Handles ride with the part they are on.' },
  { key: 'plinth', moves: false, brief: 'A recessed toe-kick under a floor-standing carcass - the recess is what makes it read as furniture rather than a box on the floor.' },
  { key: 'cornice', moves: false, brief: 'A moulding along the top of wall cabinets, projecting slightly proud of the shutter face.' },
  { key: 'shelfstack', moves: false, brief: 'Internal shelves and a hanging rail, visible once a shutter is open or through a glass insert.' },
]

const built = await pipeline(
  PARTS,
  part => agent(
    `${CONTEXT}

Implement the ${part.key} sub-generator.

${part.brief}

${part.moves
  ? 'This part MOVES. Return its mesh in local space with the pivot at the origin, and supply the part descriptor - motion type, axis, travel limits - so the actor can articulate it. Do not merge it into the carcass.'
  : 'This part does not move on its own. It may share the fixed geometry mesh, unless it is mounted on a moving part, in which case it must be generated in that part\'s local space so it travels with it.'}

Write tests in the HouseForge.Joinery category asserting watertightness, positive volume, bounds
matching the declared size, and that parameters genuinely change the output.
${part.moves ? 'Also assert the motion: closed at OpenAmount 0, at the declared limit at 1, and that the part does not intersect its carcass at any point in between.' : ''}

Build and run the tests before reporting.`,
    { label: `implement:${part.key}`, phase: 'Implement' }),

  (result, part) => agent(
    `${CONTEXT}

The ${part.key} sub-generator was just implemented:
${String(result).slice(0, 4000)}

Try to REFUTE that it is correct, reading the actual code rather than the report. Default to
"broken" when unsure. Look for:
- Negative volume, non-watertight output, or missing surface-role polygroups.
- Parameters accepted then ignored.
- Degenerate output at count 0 or 1, or division by zero.
- Proportions that would look wrong at real scale.
${part.moves ? '- A pivot in the wrong place, so the part swings or slides through its own carcass.\n- Travel limits that let it pass through an adjacent part or a wall.\n- A part that visually moves but whose collision does not follow it.' : '- A part mounted on a moving element that does not travel with it.'}

Fix genuine defects, rebuild, re-run the tests.`,
    { label: `refute:${part.key}`, phase: 'Verify', schema: {
      type: 'object',
      properties: {
        part: { type: 'string' },
        defects: { type: 'array', items: { type: 'string' } },
        verdict: { type: 'string', enum: ['sound', 'fixed', 'still-broken'] },
      },
      required: ['part', 'verdict'],
    } })
)

phase('Retrofit')

// Doors and windows were built as static leaves before this requirement existed.
const retrofit = await agent(
  `${CONTEXT}

Parts built:
${summarise(built, 1500)}

The existing door leaves and window glazing were generated as static geometry, before the
requirement that anything which moves must move. Retrofit them onto the articulation framework.

Read ${PLUGIN}/Source/HouseForge/Private/Geometry/HFGenerators.cpp - GenerateOpeningInfill - and
Public/Actors/HFElementActors.h - AHFOpeningActor.

- A hinged door leaf swings about the hinge stile, on the side and in the direction its EHFSwing
  says. The swing arc is already validated and drawn in the preview; the geometry must now match it.
- A sliding door slides along the wall by its own width.
- A sliding window sash slides; a casement sash swings.
- Frames, sills and mullions stay fixed.

Reuse the framework rather than inventing a second mechanism. Write tests: a door at OpenAmount 1
has swung to its declared angle about the correct edge, in the direction its swing says; a sliding
door has translated along the wall, not through it; and an open door does not intersect its own
frame.

Build and run the gate before reporting.`,
  { label: 'retrofit:openings', phase: 'Retrofit', effort: 'high' })

phase('Verify')

const review = await parallel([
  () => agent(
    `${CONTEXT}

Per-part results:
${summarise(built, 2000)}

Openings retrofit:
${String(retrofit).slice(0, 5000)}

Verify what per-part review cannot see: composition. Build a test wardrobe - carcass, shutters,
handles, plinth, shelves, hanging rail - and a test drawer bank, and confirm each assembles
coherently: no z-fighting coplanar faces, no parts floating clear, no interpenetration that would
show, handles riding correctly on the shutters they belong to.

Then open everything at once and confirm the assembly is still sound: shutters clear each other,
drawers clear the shutters, nothing passes through the carcass.

Write the composition test. Run the full gate and report honestly.`,
    { label: 'verify:composition', phase: 'Verify', effort: 'high' }),

  () => agent(
    `${CONTEXT}

Openings retrofit:
${String(retrofit).slice(0, 5000)}

Verify articulation across the whole reference 2BHK. Apply the sample spec, then sweep every
articulated part from closed to fully open and check:
- Nothing passes through a wall, floor or ceiling at any open amount.
- Door swings match the direction the drawings show, which the SwingBlocked validator rule already
  reasons about - the geometry must agree with the validator.
- Collision follows the visual mesh, so a walkthrough cannot walk through an open door.
- Open amounts survive a house rebuild and a level save and reload.

Report any part whose motion is wrong, and fix what you can.`,
    { label: 'verify:whole-house-motion', phase: 'Verify', effort: 'high' }),
])

return {
  parts: PARTS.map(p => p.key),
  verdicts: built.filter(Boolean).map(b => ({ part: b.part, verdict: b.verdict })),
  paramGaps: research?.paramGaps ?? [],
  review,
}
