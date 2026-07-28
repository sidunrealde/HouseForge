export const meta = {
  name: 'hf-control-panel',
  description: 'The artist-facing HouseForge panel that gathers every feature into one place',
  whenToUse: 'HouseForge milestone 13. Best run late, once the features it surfaces exist.',
  phases: [
    { title: 'Audit', detail: 'inventory everything the panel must reach' },
    { title: 'Skeleton', detail: 'dockable tab and the shell, useful before it is complete' },
    { title: 'Sections', detail: 'one section at a time, each usable on its own' },
    { title: 'Verify', detail: 'judge it as an artist would, then check it did not break anything' },
  ],
}

const PLUGIN = 'd:/Projects/UnrealEngine/5.8/HouseBuilder/Plugins/HouseForge'
const ENGINE = 'd:/EpicGames/Engine/UE_5.8'

const RULES = `
Non-negotiable, from ${PLUGIN}/.claude/rules/:
- All changes inside the plugin, in Source/HouseForgeEditor. Branch off develop as
  feature/control-panel.
- The panel calls UHFEditorSubsystem, the same API the MCP tools call. It holds no logic of its
  own, or the UI and the tools will drift apart.
- Respect bArtistEdited and keep baking reversible - the panel must make both obvious rather than
  letting someone destroy work by accident.
- Scripts/hf-validate.ps1 must pass before merging.

The user is a solo developer and artist. Favour a panel that is pleasant to use over one that
exposes everything. An unpleasant panel gets abandoned and the plugin gets driven by MCP alone.
`

phase('Audit')

const audit = await parallel([
  () => agent(
    `${RULES}

Read all of ${PLUGIN}/Source and ${PLUGIN}/Docs and produce a complete inventory of user-facing
capabilities: everything a person could want to trigger, see or adjust.

For each: what it does, where it currently lives (Tools menu, details panel, MCP tool, console
command, or nowhere), and whether it is currently unreachable from the UI.

Group them by when an artist would actually use them - setting up, generating, correcting,
finishing, exporting - rather than by which class implements them.`,
    { label: 'audit:capabilities', phase: 'Audit', schema: {
      type: 'object',
      properties: {
        groups: { type: 'array', items: { type: 'object', properties: {
          stage: { type: 'string' },
          capabilities: { type: 'array', items: { type: 'string' } },
        }, required: ['stage', 'capabilities'] } },
        unreachable: { type: 'array', items: { type: 'string' } },
      },
      required: ['groups'],
    } }),

  () => agent(
    `${RULES}

Research in ${ENGINE} the Slate and editor APIs for a plugin tool panel, with real examples from
engine source to copy patterns from:
- Registering a dockable nomad tab and putting it in the Tools menu.
- SListView and STreeView for element lists, SSearchBox, SExpandableArea, SSegmentedControl.
- Embedding an IDetailsView so the panel can show a selected element's parameters without
  hand-writing widgets for every property.
- SObjectPropertyEntryBox and FAssetPickerConfig for Content Browser asset pickers.
- Reflecting editor selection into a panel and driving selection from it.
- Progress and cancellation for long operations, and toast notifications for results.

Name two or three engine plugins whose panels are good references, and say what to copy from each.`,
    { label: 'audit:slate', phase: 'Audit', schema: {
      type: 'object',
      properties: {
        findings: { type: 'array', items: { type: 'object', properties: {
          topic: { type: 'string' }, api: { type: 'string' }, header: { type: 'string' }, module: { type: 'string' },
        }, required: ['topic', 'api'] } },
        references: { type: 'array', items: { type: 'string' } },
        modulesNeeded: { type: 'array', items: { type: 'string' } },
      },
      required: ['findings'],
    } }),
])

const [caps, slate] = audit

const CONTEXT = `${RULES}

Capability inventory:
${JSON.stringify(caps ?? {}, null, 1).slice(0, 10000)}

Slate research:
${JSON.stringify(slate ?? {}, null, 1).slice(0, 8000)}
`

phase('Skeleton')

// Build order matters: the panel must be useful before it is finished.
const skeleton = await agent(
  `${CONTEXT}

Build the panel shell on feature/control-panel, so it is useful from the first commit.

- A dockable tab under Tools, remembering its layout.
- The shell: a header showing the current house - its name, source drawing, room and element counts,
  and whether the spec validates - plus a body that sections plug into.
- An empty state that tells a first-time user what to do, since the first thing they will see is a
  level with no house in it.
- The first working section: Drawings. List what is in Reference/Drawings, import more, and show
  which drawing the current house came from.

Wire it to UHFEditorSubsystem. Write tests where the editor allows: the tab registers and spawns,
the panel reports the house state correctly, and the empty state appears with no house present.

Build and run the gate before reporting.`,
  { label: 'implement:skeleton', phase: 'Skeleton', effort: 'high' })

phase('Sections')

const SECTIONS = [
  { key: 'build', brief: 'Generate and correct: validate the current spec and show the report inline with each issue readable, apply a spec, rebuild geometry, and capture a top-down view. Validation errors should be the most visible thing when they exist.' },
  { key: 'elements', brief: 'A browsable tree of the house - rooms containing their walls, openings, ceilings and fixtures - reflecting editor selection both ways, with an embedded details view for whatever is selected. This is where an artist lives while correcting a misread drawing.' },
  { key: 'materials', brief: 'Per surface role: base colour, texture maps, tiling in millimetres, projection mode. Changes apply live to every element using that role. Needs the materials milestone to have landed.' },
  { key: 'finishing', brief: 'Bake and unbake, per element and in bulk, with bake state obvious at a glance; asset replacement per instance and by mapping table; and revert, which must be prominent and unambiguous since it is the only way back from a hand edit.' },
  { key: 'presentation', brief: 'Time of day as a scrubbable control, lighting presets, cameras and render actions. Needs the photoreal milestone to have landed.' },
]

const sections = await pipeline(
  SECTIONS,
  section => agent(
    `${CONTEXT}

Panel shell:
${String(skeleton).slice(0, 6000)}

Implement the ${section.key} section.

${section.brief}

It must be usable on its own - someone should be able to do this one job without the other sections
existing. Call UHFEditorSubsystem for everything; add a subsystem method if one is missing rather
than putting logic in the widget.

If this section depends on a milestone that has not landed yet, build what you can and degrade
gracefully with a clear message rather than a broken control.

Build and run the gate before reporting.`,
    { label: `implement:${section.key}`, phase: 'Sections' }),

  (result, section) => agent(
    `${CONTEXT}

The ${section.key} section was just implemented:
${String(result).slice(0, 4000)}

Review it as an artist who has not read any documentation. Read the actual widget code.

- Is it obvious what each control does without a tooltip?
- Can anything here destroy work without a clear warning - regenerating over a hand edit, reverting,
  applying a mapping table?
- Does anything block the editor while it runs, with no progress or cancel?
- Is anything here that should not be, that would be better left to the details panel?
- Does it fail gracefully with no house, an invalid spec, or a missing dependency?

Fix what you find. Report anything needing the user's judgement rather than guessing.`,
    { label: `review:${section.key}`, phase: 'Verify', schema: {
      type: 'object',
      properties: {
        section: { type: 'string' },
        issues: { type: 'array', items: { type: 'string' } },
        fixed: { type: 'array', items: { type: 'string' } },
        needsUser: { type: 'array', items: { type: 'string' } },
      },
      required: ['section'],
    } })
)

phase('Verify')

const verdicts = await parallel([
  () => agent(
    `${CONTEXT}

Sections built:
${JSON.stringify(sections.filter(Boolean), null, 1).slice(0, 10000)}

Judge the panel as a whole, which per-section review cannot.

Walk the three things a first-time user should manage without documentation: import a drawing and
see it listed; build a house and see whether it validated; select a wall and change its thickness.
If any of those needs explaining, the panel has failed and you should say so.

Then check it scales: with the reference 2BHK's full element count, is the tree usable? Does the
panel stay responsive?

Fix what you can. Be blunt about what is still unpleasant.`,
    { label: 'verify:whole-panel', phase: 'Verify', effort: 'high' }),

  () => agent(
    `${CONTEXT}

Try to refute that this milestone broke anything. The panel calls the same subsystem the MCP tools
call, so a change made for the UI could easily have changed tool behaviour.

- Do all the MCP tools still work? Exercise them.
- Did any subsystem method change signature or semantics?
- Does the panel hold state that could go stale when a spec is applied from MCP instead of the UI?
- Run the full validation gate.

Fix regressions. Report the gate result honestly.`,
    { label: 'verify:regressions', phase: 'Verify', effort: 'high' }),
])

return {
  unreachableBefore: caps?.unreachable ?? [],
  sections: SECTIONS.map(s => s.key),
  needsUser: sections.filter(Boolean).flatMap(s => s.needsUser ?? []),
  verdicts,
}
