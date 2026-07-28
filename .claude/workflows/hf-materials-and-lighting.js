export const meta = {
  name: 'hf-materials-and-lighting',
  description: 'Material library with per-role tiling control, and lighting that makes the flat walkable',
  whenToUse: 'HouseForge milestones 10 and 11. Run after fixtures, so every surface role exists to be assigned.',
  phases: [
    { title: 'Research', detail: 'material instance and lighting APIs, and the roles in play' },
    { title: 'Materials', detail: 'library, placeholder materials, per-role assignment' },
    { title: 'Lighting', detail: 'real lights from fixtures, cove strips, sky, player start' },
    { title: 'Verify', detail: 'adversarial review and a walkable flat' },
  ],
}

const PLUGIN = 'd:/Projects/UnrealEngine/5.8/HouseBuilder/Plugins/HouseForge'
const ENGINE = 'd:/EpicGames/Engine/UE_5.8'

const RULES = `
Non-negotiable, from ${PLUGIN}/.claude/rules/:
- All changes inside the plugin. Branch off develop as feature/materials then feature/lighting.
- Every generated triangle already carries an EHFSurfaceRole polygroup and real-world-scale UVs
  where one UV tile is a fixed number of centimetres. That is what makes tiling expressible in
  millimetres - do not break it.
- Respect bArtistEdited: re-materialling must not regenerate geometry.
- Scripts/hf-validate.ps1 must pass before merging.
`

phase('Research')

const research = await parallel([
  () => agent(
    `${RULES}

Research in ${ENGINE} the APIs for: creating UMaterialInstanceDynamic and
UMaterialInstanceConstant from editor code; assigning materials per polygroup on a
UDynamicMeshComponent (look at UDynamicMeshComponent::SetMaterial, ConfigureMaterialSet, and how
polygroups map to material indices); and creating a material asset programmatically.

Critically: establish exactly how a UDynamicMeshComponent maps triangle polygroups to material
slots, because HouseForge tags every triangle with a surface role and needs each role to take its
own material. If that mapping does not exist as assumed, say so plainly and propose the real
mechanism.

Report exact class names, headers, modules.`,
    { label: 'research:materials', phase: 'Research', schema: {
      type: 'object',
      properties: {
        findings: { type: 'array', items: { type: 'object', properties: {
          topic: { type: 'string' }, api: { type: 'string' }, header: { type: 'string' }, module: { type: 'string' },
        }, required: ['topic', 'api'] } },
        polygroupToMaterialMechanism: { type: 'string' },
        traps: { type: 'array', items: { type: 'string' } },
      },
      required: ['findings', 'polygroupToMaterialMechanism'],
    } }),

  () => agent(
    `${RULES}

Research in ${ENGINE}: spawning and configuring lights from editor code (UPointLightComponent,
USpotLightComponent, URectLightComponent), a sky and exposure setup that suits an interior
(SkyLight, DirectionalLight, SkyAtmosphere, PostProcessVolume exposure settings), and spawning a
PlayerStart.

Also: what is actually needed for Lumen to light an interior acceptably without baking, given the
project already has Lumen and VSM enabled. And how to make a thin emissive strip read as cove
lighting.

Report exact classes, headers, modules, and the settings that matter.`,
    { label: 'research:lighting', phase: 'Research', schema: {
      type: 'object',
      properties: {
        findings: { type: 'array', items: { type: 'object', properties: {
          topic: { type: 'string' }, api: { type: 'string' }, header: { type: 'string' }, module: { type: 'string' },
        }, required: ['topic', 'api'] } },
        interiorLumenNotes: { type: 'string' },
        coveStripApproach: { type: 'string' },
      },
      required: ['findings'],
    } }),
])

const [matApi, lightApi] = research

phase('Materials')

const CONTEXT = `${RULES}

Material API research:
${JSON.stringify(matApi ?? {}, null, 1).slice(0, 8000)}

Lighting API research:
${JSON.stringify(lightApi ?? {}, null, 1).slice(0, 8000)}
`

const materials = await agent(
  `${CONTEXT}

Build the material system on feature/materials.

1. UHFMaterialLibrary, a UDataAsset mapping every EHFSurfaceRole to a parameterised material:
   base colour, texture maps (base colour, normal, roughness, metallic, AO), roughness and metallic
   scalars, UV tiling, offset and rotation, projection mode (UV-mapped versus world-aligned
   triplanar), and a real-world tile size in millimetres from which the UV scale is computed.
2. Placeholder materials for each role, created as assets in the plugin's Content folder so a
   generated house is readable immediately rather than default-grey.
3. Assignment at generation time: each element's polygroups take the material for their role.
4. Live update: changing a library value updates every actor using that role, without regenerating
   geometry - which would destroy hand edits.

Write tests: every role resolves to a material; changing tile size changes the computed UV scale
proportionally; re-materialling leaves geometry and bArtistEdited untouched.

Build and run the gate before reporting.`,
  { label: 'implement:materials', phase: 'Materials', effort: 'high' })

phase('Lighting')

const lighting = await agent(
  `${CONTEXT}

Materials landed:
${String(materials).slice(0, 5000)}

Build lighting on feature/lighting.

1. Fixtures of type LightFixture and CeilingFan spawn real light components; recessed light
   positions recorded on false ceilings become spot lights aimed downward.
2. Cove ceilings get an emissive strip along their channel - that is the entire point of a cove,
   and it should read as indirect light on the soffit above.
3. A sky and exposure setup suitable for an interior, so the flat is viewable rather than black.
4. A PlayerStart in the foyer, and collision on generated geometry, so Play works immediately.

Intensities must be plausible for a domestic interior in lumens or candelas, not arbitrary numbers.

Write tests: a spec with light positions produces that many lights; a cove produces an emissive
strip; a PlayerStart exists and sits inside a room rather than in a wall.

Build and run the gate before reporting.`,
  { label: 'implement:lighting', phase: 'Lighting', effort: 'high' })

phase('Verify')

const verdicts = await parallel(['correctness', 'artist-workflow', 'performance'].map(lens => () => agent(
  `${CONTEXT}

Materials implementation:
${String(materials).slice(0, 6000)}

Lighting implementation:
${String(lighting).slice(0, 6000)}

Try to REFUTE that this works, through the lens of ${lens}. Read the actual code. Default to
"broken" when unsure.

For correctness: does every role really get its own material, or do polygroups collapse to one slot?
For artist-workflow: can tiling genuinely be set in millimetres and does it match reality? Does
changing a material destroy hand edits or force a regeneration?
For performance: how many lights does the reference 2BHK spawn, and is that sane? Are dynamic mesh
components with per-polygroup materials going to cost a draw call each?

Fix genuine defects, rebuild, re-run the gate.`,
  { label: `refute:${lens}`, phase: 'Verify', schema: {
    type: 'object',
    properties: {
      lens: { type: 'string' },
      defects: { type: 'array', items: { type: 'object', properties: {
        defect: { type: 'string' }, severity: { type: 'string' }, fixed: { type: 'boolean' },
      }, required: ['defect', 'fixed'] } },
      verdict: { type: 'string', enum: ['sound', 'fixed', 'still-broken'] },
    },
    required: ['lens', 'verdict'],
  } })))

return {
  polygroupMechanism: matApi?.polygroupToMaterialMechanism,
  verdicts: verdicts.filter(Boolean).map(v => ({ lens: v.lens, verdict: v.verdict })),
  unfixed: verdicts.filter(Boolean).flatMap(v => (v.defects ?? []).filter(d => !d.fixed).map(d => d.defect)),
}
