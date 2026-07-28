export const meta = {
  name: 'hf-photoreal-and-walkthrough',
  description: 'Everything needed to get photorealistic stills and a walkable flat out of a generation',
  whenToUse: 'HouseForge final quality milestone. Run after fixtures, materials and lighting exist.',
  phases: [
    { title: 'Gap analysis', detail: 'find what photorealism needs that is not there yet' },
    { title: 'Geometry quality', detail: 'bevels, normals, lightmap UVs, glass thickness' },
    { title: 'Time of day', detail: 'a sun and sky the user can drive, and interiors that respond' },
    { title: 'Render and walk', detail: 'cameras, Movie Render Queue, a walkthrough pawn' },
    { title: 'Verify', detail: 'render the reference flat and judge it honestly' },
  ],
}

const PLUGIN = 'd:/Projects/UnrealEngine/5.8/HouseBuilder/Plugins/HouseForge'
const ENGINE = 'd:/EpicGames/Engine/UE_5.8'

const RULES = `
Non-negotiable, from ${PLUGIN}/.claude/rules/:
- All changes inside the plugin. Branch off develop.
- Generators stay pure; articulation and lighting are the actors' job.
- Respect bArtistEdited and keep baking reversible.
- Anything that moves must move - doors, windows, drawers and shutters are articulated.
- Scripts/hf-validate.ps1 must pass before merging.

The target: photorealistic stills and a walkthrough, straight out of a generation. Judge output by
how it looks lit and in motion, not by a wireframe screenshot.

The user also wants to change the TIME OF DAY, so the sun, sky and interior lighting must all be
driven from one control rather than baked into fixed values.
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

phase('Gap analysis')

// The most valuable phase: the user said they may have missed things, so go and find them.
const gaps = await parallel([
  () => agent(
    `${RULES}

Audit ${PLUGIN}/Source for everything that stands between the current output and a photorealistic
render. Read the actual generators, actors and material code.

Look hard for the things people forget:
- Perfectly sharp edges. Every box in HFMeshOps has mathematically sharp corners, and a real edge
  has a chamfer that catches light. Confirm whether any bevelling exists.
- Normals: is there anything beyond a blanket QuickRecomputeOverlayNormals? Hard versus soft edges?
- UV channel 1 for lightmaps - does it exist at all?
- Glass: is it a plane or does it have thickness? Refraction needs thickness.
- Whether collision matches the visual mesh, including on articulated parts.
- Tessellation and shading artefacts on large flat surfaces.

Report each gap concretely, with the file and function it lives in, and how badly it would show in
a render. Be exhaustive - the user explicitly expects things to have been missed.`,
    { label: 'gaps:geometry', phase: 'Gap analysis', schema: {
      type: 'object',
      properties: {
        gaps: { type: 'array', items: { type: 'object', properties: {
          gap: { type: 'string' }, location: { type: 'string' }, renderImpact: { type: 'string' },
          severity: { type: 'string', enum: ['blocking', 'major', 'minor'] },
        }, required: ['gap', 'location', 'severity'] } },
      },
      required: ['gaps'],
    } }),

  () => agent(
    `${RULES}

Research in ${ENGINE} what UE 5.8 needs for a photorealistic interior, and check which of it the
project already has. Read ${PLUGIN}/../../Config/DefaultEngine.ini for what is already enabled -
Lumen, VSM, Substrate and ray tracing are known to be on.

Cover concretely, with class names and settings:
- Lumen for interiors: what actually matters, and the common causes of a dark or blotchy interior.
- Light portals at windows, and whether they still matter with Lumen.
- Physical light units - lumens, candelas, colour temperature - and IES profiles.
- Post-process: exposure metering, bloom, depth of field, colour grading.
- Reflections: Lumen reflections versus reflection captures, and glass and mirror handling.
- Nanite on generated static meshes: worth it, and what breaks.
- Movie Render Queue: the API to set up and trigger a render from editor code.

Flag anything that will look wrong by default rather than merely suboptimal.`,
    { label: 'gaps:rendering', phase: 'Gap analysis', schema: {
      type: 'object',
      properties: {
        findings: { type: 'array', items: { type: 'object', properties: {
          topic: { type: 'string' }, api: { type: 'string' }, setting: { type: 'string' }, whyItMatters: { type: 'string' },
        }, required: ['topic', 'whyItMatters'] } },
        wrongByDefault: { type: 'array', items: { type: 'string' } },
      },
      required: ['findings'],
    } }),

  () => agent(
    `${RULES}

Research what a walkthrough of a generated flat needs in UE 5.8, and check the project against it.

- A first-person pawn at a believable eye height with sensible movement speed, and why the default
  character feels wrong indoors.
- Interacting with articulated doors and drawers while walking - the geometry is articulated, so
  what is needed to actually open a door in play.
- Collision: what HouseForge currently generates (complex-as-simple on dynamic meshes) and whether
  that is adequate, plus how it behaves on moving parts.
- Navigation, if any.
- What breaks when a level is played versus viewed in the editor.

Read ${PLUGIN}/Source/HouseForge/Private/Actors/HFElementActors.cpp for the current collision
setup. Report concrete gaps.`,
    { label: 'gaps:walkthrough', phase: 'Gap analysis', schema: {
      type: 'object',
      properties: {
        gaps: { type: 'array', items: { type: 'string' } },
        recommendations: { type: 'array', items: { type: 'string' } },
      },
      required: ['gaps'],
    } }),
])

const [geometryGaps, renderGaps, walkGaps] = gaps
const blocking = (geometryGaps?.gaps ?? []).filter(g => g.severity === 'blocking')
log(`Gap analysis: ${geometryGaps?.gaps?.length ?? 0} geometry gaps (${blocking.length} blocking), ${renderGaps?.wrongByDefault?.length ?? 0} things wrong by default`)

const CONTEXT = `${RULES}

Geometry gaps found:
${JSON.stringify(geometryGaps ?? {}, null, 1).slice(0, 8000)}

Rendering research:
${JSON.stringify(renderGaps ?? {}, null, 1).slice(0, 8000)}

Walkthrough gaps:
${JSON.stringify(walkGaps ?? {}, null, 1).slice(0, 5000)}
`

phase('Geometry quality')

const geometry = await agent(
  `${CONTEXT}

Close the geometry gaps, on feature/render-quality.

The essential one is edge bevels. Add a bevel option to FHFMeshOps so generated geometry has a
small chamfer on its edges - a couple of millimetres is enough to catch a highlight and is the
single biggest difference between geometry that reads as real and geometry that reads as CG. It
must be a parameter, since a bevel on every edge of every element costs triangles.

Then, in order of what shows most in a render:
- Deliberate hard and soft normals rather than a blanket recompute.
- A second UV channel for lightmaps, so baked lighting stays an option alongside Lumen.
- Glass with real thickness so refraction and reflection behave.
- Collision that matches the visual mesh, including on articulated parts at any open amount.

Write tests for each: a bevelled box has more faces and no sharp dihedral above the threshold;
UV channel 1 exists and is non-overlapping; glass is a solid, not a plane.

Build and run the gate before reporting.`,
  { label: 'implement:geometry-quality', phase: 'Geometry quality', effort: 'high' })

phase('Time of day')

const timeOfDay = await agent(
  `${CONTEXT}

Geometry quality work:
${String(geometry).slice(0, 5000)}

Implement time-of-day control on feature/time-of-day.

One control - hour of day, plus date and a geographic location so the sun is in a plausible place
for the site - driving: sun angle and intensity, sky colour and atmosphere, and the interior
lighting response, so lamps come on toward evening and the exposure adapts.

It must be a live control an artist can scrub, visible in the editor viewport rather than only in
play, and it must be part of what the control panel exposes.

Consider whether to build on the engine's Sun Position Calculator and SkyAtmosphere, or a simpler
directional-light rig, and justify the choice. Whichever, the flat must look right at dawn, midday,
golden hour and night, with interior lights carrying the night case.

Write tests: the sun elevation for a known date, time and latitude matches the expected angle;
interior lights switch state across the threshold; scrubbing does not regenerate geometry.

Build and run the gate before reporting.`,
  { label: 'implement:time-of-day', phase: 'Time of day', effort: 'high' })

phase('Render and walk')

const output = await parallel([
  () => agent(
    `${CONTEXT}

Implement still rendering on feature/rendering.

- Camera placement: a way to set up interior cameras at believable heights and focal lengths, and
  something that suggests good views of a generated flat rather than making the user hunt.
- Movie Render Queue configured for interior stills: anti-aliasing, sample counts, warm-up frames
  so Lumen has converged, and output settings.
- A one-click "render this view" and "render all cameras" that produces images without the user
  learning MRQ.
- Renders must respect the current time of day.

Write tests where testable, and be honest about what can only be verified by looking.

Build and run the gate before reporting.`,
    { label: 'implement:rendering', phase: 'Render and walk', effort: 'high' }),

  () => agent(
    `${CONTEXT}

Implement the walkthrough on feature/walkthrough.

- A first-person pawn at a believable eye height, with movement speed and acceleration that feel
  right indoors rather than like a shooter.
- Interaction with articulated parts: look at a door, drawer or shutter and open it. The
  articulation framework already exists - drive OpenAmount, and animate rather than snapping.
- Collision that holds up: no walking through walls, closed doors block and open ones do not, and
  stairs or level changes if any exist.
- A PlayerStart placed somewhere sensible, in the foyer facing into the flat.

Write tests: the pawn spawns inside the flat and not inside geometry; a closed door blocks and an
open one does not; interaction changes OpenAmount.

Build and run the gate before reporting.`,
    { label: 'implement:walkthrough', phase: 'Render and walk', effort: 'high' }),
])

phase('Verify')

const verdicts = await parallel([
  { lens: 'photorealism', brief: 'Render the reference 2BHK at several times of day and judge the images honestly. What still reads as computer-generated? Sharp edges, flat materials, missing contact shadows, wrong light falloff, uniform surfaces, absent dust and wear. Be specific and unsparing - the user asked for photorealistic, not adequate.' },
  { lens: 'walkthrough-feel', brief: 'Walk the flat. Does it feel like a room? Eye height, movement speed, door interaction, collision snags, doors that open into you, anything that breaks immersion. Report what is wrong to be in, not what is wrong in the code.' },
  { lens: 'regressions', brief: 'Try to refute that this milestone broke earlier work. Do bevels break the boolean-cut openings? Does the second UV channel break the world-scale tiling the material panel depends on? Does time of day fight the cove strips? Does collision on articulated parts survive a rebuild? Run the full gate.' },
].map(({ lens, brief }) => () => agent(
  `${CONTEXT}

Geometry quality: ${String(geometry).slice(0, 4000)}
Time of day: ${String(timeOfDay).slice(0, 4000)}
Rendering and walkthrough:
${summarise(output, 4000)}

Evaluate through the lens of ${lens}.

${brief}

Where you find something fixable, fix it, rebuild and re-run the gate. Where you find something
that needs the user's judgement or assets that do not exist yet, say so plainly rather than
pretending it is done.`,
  { label: `verify:${lens}`, phase: 'Verify', effort: 'high', schema: {
    type: 'object',
    properties: {
      lens: { type: 'string' },
      issues: { type: 'array', items: { type: 'object', properties: {
        issue: { type: 'string' }, fixed: { type: 'boolean' }, needsUser: { type: 'boolean' },
      }, required: ['issue', 'fixed'] } },
      verdict: { type: 'string' },
    },
    required: ['lens', 'verdict'],
  } })))

return {
  blockingGeometryGaps: blocking.map(g => g.gap),
  wrongByDefault: renderGaps?.wrongByDefault ?? [],
  verdicts: verdicts.filter(Boolean).map(v => ({ lens: v.lens, verdict: v.verdict })),
  needsUser: verdicts.filter(Boolean).flatMap(v => (v.issues ?? []).filter(i => i.needsUser).map(i => i.issue)),
  unfixed: verdicts.filter(Boolean).flatMap(v => (v.issues ?? []).filter(i => !i.fixed && !i.needsUser).map(i => i.issue)),
}
