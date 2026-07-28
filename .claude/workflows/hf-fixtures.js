export const meta = {
  name: 'hf-fixtures',
  description: 'Generate every fixture type, composed from the joinery kit',
  whenToUse: 'HouseForge milestone 9. Requires hf-joinery-kit to have landed first.',
  phases: [
    { title: 'Plan', detail: 'group the fixture types and establish real dimensions' },
    { title: 'Implement', detail: 'one group at a time, each built and tested' },
    { title: 'Verify', detail: 'adversarial review per group, then the whole sample house' },
  ],
}

const PLUGIN = 'd:/Projects/UnrealEngine/5.8/HouseBuilder/Plugins/HouseForge'

const RULES = `
Non-negotiable, from ${PLUGIN}/.claude/rules/:
- All changes inside the plugin. Branch off develop as feature/fixtures.
- Generators are pure: (params) -> FDynamicMesh3. Compose from FHFJoineryKit; do not re-implement
  shutters, drawers, handles, plinths or cornices.
- Every triangle carries a surface-role polygroup and real-world-scale UVs.
- Fixture actors must respect bArtistEdited and never regenerate over hand-modelled work.
- Each fixture keeps a TSoftObjectPtr mesh override slot, unused for now - that is the seam the
  asset-replacement milestone plugs into, and retrofitting it later would touch every generator.
- Scripts/hf-validate.ps1 must pass before merging.
- Assert on volume, watertightness and bounds. Never on triangle counts.
`

// Grouped so related fixtures share reasoning and reviewers see them together.
const GROUPS = [
  { key: 'wardrobes', types: 'Wardrobe, LoftUnit, Bookshelf, ShoeRack, WallNiche',
    brief: 'Carcass, shutters with reveals, handles, internal shelves, hanging rail, plinth, optional loft above. The loft is standard in Indian bedrooms and its proportions matter.' },
  { key: 'kitchen', types: 'KitchenBaseCabinet, KitchenWallCabinet, KitchenTallUnit, CounterTop, Sink, Hob, Chimney',
    brief: 'A base run with drawer banks and a toe-kick, a counter with upstand and cutouts for sink and hob, wall units with a cornice. The counter cutouts must be real holes, not decals.' },
  { key: 'beds', types: 'Bed, Nightstand, TVUnit, StudyTable, DiningTable, Chair, CoffeeTable, Sofa',
    brief: 'Loose furniture. Blocky massing is wrong here - a bed needs a headboard, mattress and visible base; a sofa needs back, arms and seat cushions.' },
  { key: 'sanitary', types: 'WC, Basin, Shower, ShowerPartition, Vanity, Mirror, TowelRail, Geyser',
    brief: 'Wall-hung WC with cistern, counter basin, shower tray with a glass partition, vanity with shutters. Sanitary ware reads as wrong very quickly if proportions are off.' },
  { key: 'services', types: 'CeilingFan, LightFixture, SwitchPlate, PowerSocket, DistributionBoard, ACIndoorUnit, ACOutdoorUnit, ExhaustFan, Refrigerator, WashingMachine, Railing, Pelmet, Curtain',
    brief: 'Electrical fittings and the remaining architectural fittings. A ceiling fan needs a drop rod, motor housing and three blades; a railing needs balusters, not a solid panel.' },
]

phase('Plan')

const plan = await agent(
  `${RULES}

Read ${PLUGIN}/Source/HouseForge/Public/Geometry/HFJoineryKit.h, HFGenerators.h and HFMeshOps.h,
plus EHFFixtureType and FHFFixtureParams in Model/HFTypes.h. Read HFSampleHouse.cpp to see the
dimensions the reference 2BHK actually uses for each fixture - those are the sizes the generators
will be asked for.

For every fixture type in EHFFixtureType, establish realistic dimensions in millimetres and which
joinery-kit parts it composes from. Flag any type whose parameters in FHFFixtureParams are
insufficient to generate it properly, since that would need a model change.

Also decide how AHFFixtureActor should be structured: it must carry the fixture struct, respect
bArtistEdited, and expose an unused TSoftObjectPtr mesh override for the later asset-replacement
milestone.`,
  { label: 'plan:fixtures', phase: 'Plan', schema: {
    type: 'object',
    properties: {
      fixtures: { type: 'array', items: { type: 'object', properties: {
        type: { type: 'string' }, dimensionsMm: { type: 'string' },
        composedFrom: { type: 'string' }, paramsSufficient: { type: 'boolean' }, notes: { type: 'string' },
      }, required: ['type', 'dimensionsMm', 'composedFrom'] } },
      actorDesign: { type: 'string' },
      modelChangesNeeded: { type: 'array', items: { type: 'string' } },
    },
    required: ['fixtures', 'actorDesign'],
  } })

log(`Planned ${plan?.fixtures?.length ?? 0} fixture types; ${plan?.modelChangesNeeded?.length ?? 0} model changes flagged`)

phase('Implement')

const CONTEXT = `${RULES}

Plan to build against:
${JSON.stringify(plan ?? {}, null, 1).slice(0, 14000)}

Work on feature/fixtures. Generators go in
${PLUGIN}/Source/HouseForge/Public/Geometry/HFFixtureGenerators.h and its .cpp; the actor in
Public/Actors/. After each group: build, run Scripts/hf-validate.ps1 -SkipBuild, fix failures before
moving on.
`

const groups = await pipeline(
  GROUPS,
  group => agent(
    `${CONTEXT}

Implement the ${group.key} fixture generators: ${group.types}

${group.brief}

Compose from FHFJoineryKit wherever a part already exists. Write automation tests in the
HouseForge.Fixtures category asserting watertightness, positive volume, bounds matching the
declared footprint and height, and that parameters genuinely change the output.

Build and run the tests before reporting.`,
    { label: `implement:${group.key}`, phase: 'Implement' }),

  (result, group) => parallel([
    () => agent(
      `${CONTEXT}

The ${group.key} generators were just implemented:
${String(result).slice(0, 4000)}

Try to REFUTE their correctness, reading the actual code. Default to "broken" when unsure. Look for:
- Negative volume or non-watertight output.
- Parameters accepted then ignored - a shutterCount that changes nothing.
- Fixtures that would interpenetrate their own parts, or float above the floor.
- Missing surface-role polygroups.
- Fixtures ignoring baseZ, so wall cabinets sit on the floor.

Fix genuine defects, rebuild, re-run tests.`,
      { label: `refute:${group.key}`, phase: 'Verify', schema: {
        type: 'object',
        properties: {
          group: { type: 'string' },
          defects: { type: 'array', items: { type: 'string' } },
          verdict: { type: 'string', enum: ['sound', 'fixed', 'still-broken'] },
        },
        required: ['group', 'verdict'],
      } }),

    () => agent(
      `${CONTEXT}

The ${group.key} generators were just implemented. Judge them on PLAUSIBILITY rather than
correctness: at real scale, in a real flat, would these read as the thing they claim to be?

Check proportions against the dimensions in HFSampleHouse.cpp. Call out anything that would look
obviously generated - uniform divisions where real joinery is graduated, missing detail that the
eye expects, parts at the wrong height for human use.

Report concrete proportion fixes. Apply the ones you are confident about.`,
      { label: `plausibility:${group.key}`, phase: 'Verify', schema: {
        type: 'object',
        properties: {
          group: { type: 'string' },
          issues: { type: 'array', items: { type: 'string' } },
          applied: { type: 'array', items: { type: 'string' } },
        },
        required: ['group'],
      } }),
  ])
)

phase('Verify')

const wholeHouse = await agent(
  `${CONTEXT}

Per-group results:
${JSON.stringify(groups.flat().filter(Boolean), null, 1).slice(0, 12000)}

Now verify what per-group review cannot: the whole reference 2BHK with every fixture generated.

- Wire fixtures into AHFHouseActor::BuildGeometry so the sample house spawns them all.
- Write an editor test that applies the reference spec and asserts every fixture produced geometry,
  none is empty, and none has negative volume.
- Check fixtures against their rooms: nothing intersecting a wall it should sit against, nothing
  floating, nothing below the floor.

Then run the full validation gate and report honestly, including anything still failing.`,
  { label: 'verify:whole-house', phase: 'Verify', effort: 'high' })

return {
  groups: GROUPS.map(g => g.key),
  verdicts: groups.flat().filter(Boolean).filter(r => r.verdict).map(r => ({ group: r.group, verdict: r.verdict })),
  modelChangesNeeded: plan?.modelChangesNeeded ?? [],
  wholeHouse,
}
