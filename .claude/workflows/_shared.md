# HouseForge milestone workflows

One workflow per remaining milestone. Each follows the same spine, because it is the shape that has
actually been catching bugs on this project:

1. **Research** — read the engine and the existing plugin source before designing anything. Every
   milestone so far has hit an API that did not behave as assumed (`FMeshBoolean` reporting failure
   on a clean cut, box projection atlasing UVs, `CreateNewMapForEditing` returning void).
2. **Design** — a plan that names real classes, headers and modules, with failure modes stated.
3. **Implement** — in pieces small enough that each can be built and tested.
4. **Adversarially verify** — reviewers whose job is to *refute* that it works, with distinct
   lenses. Geometry that looks right in a screenshot has been wrong three times now.

## Invoking

These live in the plugin so they are versioned with it. Claude Code discovers workflows from the
project root, so they are mirrored to `<project>/.claude/workflows/`. The plugin copy is the source
of truth; re-copy after editing.

```
Workflow({ name: 'hf-joinery-kit' })
Workflow({ scriptPath: '<plugin>/.claude/workflows/hf-joinery-kit.js' })
```

## Non-negotiables every workflow must respect

These come from `.claude/rules/` and have already been earned:

- **All changes inside `Plugins/HouseForge`.** The only exception is enabling plugins in
  `HouseBuilder.uproject`.
- **The validation gate.** `Scripts/hf-validate.ps1` must pass before any merge to `develop`;
  `Scripts/hf-merge.ps1` enforces it. Never merge around it.
- **Generators stay pure.** `(params) -> FDynamicMesh3`, no world or asset access, so they stay
  testable headlessly.
- **Every triangle carries a surface-role polygroup** and real-world-scale UVs, or the material
  panel cannot target it.
- **Artist edits are sacred.** Respect `bArtistEdited`; never regenerate over hand-modelled work.
- **Baking is reversible.** Keep the dynamic mesh; a bake is a toggle, not a replacement.
- **The sample house is not a build path.** `HouseForge.Architecture.SampleIsNotOnTheBuildPath`
  fails the gate if production code references it.
- **Assert on measurable properties**, not triangle counts: volume, watertightness, bounds, UV
  length over world length. Counts pass for the wrong reasons.
