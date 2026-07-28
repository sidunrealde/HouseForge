export const meta = {
  name: 'hf-bake-and-assets',
  description: 'Reversible bake to static meshes, and swapping generated fixtures for Content Browser assets',
  whenToUse: 'HouseForge milestone 12. Run after fixtures exist, since replacement targets them.',
  phases: [
    { title: 'Research', detail: 'asset creation APIs and the traps in them' },
    { title: 'Bake', detail: 'reversible bake, per element and in bulk' },
    { title: 'Replace', detail: 'per-instance override and a batch swap pass' },
    { title: 'Verify', detail: 'adversarially prove nothing can be silently lost' },
  ],
}

const PLUGIN = 'd:/Projects/UnrealEngine/5.8/HouseBuilder/Plugins/HouseForge'
const ENGINE = 'd:/EpicGames/Engine/UE_5.8'

const RULES = `
Non-negotiable, from ${PLUGIN}/.claude/rules/ - 04-conventions.md is explicit about both of these:

BAKING IS NON-DESTRUCTIVE AND REVERSIBLE. Baking an element to a static mesh must KEEP the dynamic
mesh alongside it. The whole workflow lives in the editor, so a bake is a rendering choice, not a
one-way door - an element can always be switched back and carry on being edited. Bake state is a
toggle, per element and in bulk, never a replacement.

ASSET OVERRIDES ARE NON-DESTRUCTIVE. Replacing a procedural fixture with a Content Browser asset
must never discard its parameter struct. Clearing the override restores the generated mesh exactly.

Also:
- All changes inside the plugin. Branch off develop.
- Respect bArtistEdited. A baked element that was hand-edited must keep those edits on unbake.
- Scripts/hf-validate.ps1 must pass before merging.
`

phase('Research')

const research = await agent(
  `${RULES}

Research in ${ENGINE} exactly how to, from editor code:
1. Convert an FDynamicMesh3 into a UStaticMesh asset. Look at UE::Geometry conversion helpers,
   FMeshDescription, UStaticMesh::BuildFromMeshDescriptions, and what GeometryScript's
   UGeometryScriptLibrary_CreateNewAssetFunctions offers - report which is actually appropriate
   here and why.
2. Create an asset package at a chosen path, mark it dirty, and save it.
3. Hold both a UDynamicMeshComponent and a UStaticMeshComponent on one actor and switch which is
   visible and which has collision.
4. Delete or replace a previously generated asset safely when an element is re-baked, without
   leaving orphans or breaking references.
5. Pick an asset from the Content Browser in a details panel and in a custom Slate panel
   (SObjectPropertyEntryBox, FAssetPickerConfig).

Be specific about traps. Asset creation from code is full of them - naming collisions, packages not
saving, GC of transient objects, assets created in the wrong content root for a plugin. Flag
anything you are unsure about rather than guessing.`,
  { label: 'research:assets', phase: 'Research', schema: {
    type: 'object',
    properties: {
      findings: { type: 'array', items: { type: 'object', properties: {
        topic: { type: 'string' }, api: { type: 'string' }, header: { type: 'string' }, module: { type: 'string' }, notes: { type: 'string' },
      }, required: ['topic', 'api'] } },
      traps: { type: 'array', items: { type: 'string' } },
      recommendedApproach: { type: 'string' },
      uncertainties: { type: 'array', items: { type: 'string' } },
    },
    required: ['findings', 'recommendedApproach'],
  } })

log(`Research done: ${research?.traps?.length ?? 0} traps flagged, ${research?.uncertainties?.length ?? 0} uncertainties`)

const CONTEXT = `${RULES}

Research findings:
${JSON.stringify(research ?? {}, null, 1).slice(0, 12000)}
`

phase('Bake')

const bake = await agent(
  `${CONTEXT}

Implement the reversible bake on feature/bake.

State on AHFElementActor: whether it is currently baked, the generated static mesh asset, and the
dynamic mesh retained underneath. Both components exist; baking switches which renders and which
carries collision. Nothing is destroyed.

Operations: Bake, Unbake, Rebake (after a parameter change), and bulk versions on AHFHouseActor
covering all elements or a selection.

Work through every interaction and make each one safe:
- Regenerate while baked: the dynamic mesh updates, and the bake goes stale. Decide whether it
  re-bakes automatically or is marked stale, and justify the choice.
- Hand-edit while baked: the edit lands on the dynamic mesh; bArtistEdited must still protect it.
- Unbake after hand-editing: the hand edits must survive.
- House rebuild with some elements baked: baked elements must be preserved exactly as
  bArtistEdited elements already are.
- Re-bake: the previous asset must not be orphaned.

Write tests for every one of those interactions. Each must prove that nothing is lost - that is the
entire point of the feature. Build and run the gate before reporting.`,
  { label: 'implement:bake', phase: 'Bake', effort: 'high' })

phase('Replace')

const replace = await agent(
  `${CONTEXT}

Bake landed:
${String(bake).slice(0, 5000)}

Implement asset replacement on feature/asset-replacement.

Per instance: AHFFixtureActor takes a TSoftObjectPtr<UStaticMesh> override plus a fit mode. Setting
it switches that fixture from procedural geometry to the asset; clearing it restores the generated
mesh exactly. The parameter struct is never discarded.

Fit modes, because a Content Browser asset will never match the generated footprint: stretch to
footprint, uniform scale to fit, native size anchored, native size centred. Plus pivot and anchor
correction - wall-backed fixtures align to their wall face, floor-standing ones to the floor plane -
and a rotation offset for assets authored facing a different axis.

Batch pass: a UHFAssetMappingTable data asset mapping EHFFixtureType to an asset, so a library built
once auto-applies to every future house. Applying it must be undoable and must report what it
changed.

Write tests: an override switches the mesh and clearing it restores the original; each fit mode
produces the transform it claims; a mapping table applies to all matching instances; reverting
restores every one. Build and run the gate before reporting.`,
  { label: 'implement:replace', phase: 'Replace', effort: 'high' })

phase('Verify')

// Distinct lenses: data loss is the failure mode that matters here, so most reviewers hunt for it.
const verdicts = await parallel([
  { lens: 'data-loss', brief: 'Hunt for any path where an artist could lose work: hand edits, parameters, or a previously baked asset. Construct concrete sequences of operations and check each one. This is the failure the whole feature exists to prevent.' },
  { lens: 'asset-hygiene', brief: 'Hunt for orphaned assets, naming collisions, packages that never save, assets created outside the plugin content root, and what happens when a level is closed without saving.' },
  { lens: 'reversibility', brief: 'Prove or refute that every operation is genuinely reversible. Bake then unbake then bake again. Override then clear. Apply a mapping table then revert. Does the result match the original exactly, or has something drifted?' },
].map(({ lens, brief }) => () => agent(
  `${CONTEXT}

Bake implementation:
${String(bake).slice(0, 6000)}

Asset replacement implementation:
${String(replace).slice(0, 6000)}

Try to REFUTE that this is safe, through the lens of ${lens}.

${brief}

Read the actual code rather than the reports. Default to "broken" when unsure - a false alarm costs
a few minutes, a missed data-loss bug costs someone's afternoon of modelling.

Write a test that demonstrates any defect you find, then fix it, rebuild and re-run the gate.`,
  { label: `refute:${lens}`, phase: 'Verify', effort: 'high', schema: {
    type: 'object',
    properties: {
      lens: { type: 'string' },
      defects: { type: 'array', items: { type: 'object', properties: {
        defect: { type: 'string' }, sequence: { type: 'string' }, fixed: { type: 'boolean' },
      }, required: ['defect', 'fixed'] } },
      verdict: { type: 'string', enum: ['safe', 'fixed', 'unsafe'] },
    },
    required: ['lens', 'verdict'],
  } })))

const unsafe = verdicts.filter(Boolean).filter(v => v.verdict === 'unsafe')
if (unsafe.length > 0) {
  log(`WARNING: ${unsafe.length} reviewer(s) still consider this unsafe`)
}

return {
  traps: research?.traps ?? [],
  uncertainties: research?.uncertainties ?? [],
  verdicts: verdicts.filter(Boolean).map(v => ({ lens: v.lens, verdict: v.verdict })),
  unfixedDefects: verdicts.filter(Boolean).flatMap(v => (v.defects ?? []).filter(d => !d.fixed)),
}
