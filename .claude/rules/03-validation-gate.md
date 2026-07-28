# Rule 03 — Validation gate

**No merge into `develop` until the build succeeds and the HouseForge automation tests pass.**

This is a hard gate, not a guideline. It is enforced by a script, and the evidence goes into the
merge commit message so the history shows what was actually verified.

## Running the gate

From anywhere:

```powershell
Plugins\HouseForge\Scripts\hf-validate.ps1
```

It does two things, in order, and stops at the first failure:

1. **Build** — `HouseBuilderEditor Win64 Development` via UnrealBuildTool.
2. **Test** — the `HouseForge.*` automation suite, headless:
   ```
   UnrealEditor-Cmd HouseBuilder.uproject
     -ExecCmds="Automation RunTests HouseForge;Quit"
     -unattended -nopause -nullrhi -nosplash
     -testexit="Automation Test Queue Empty"
   ```

Exit code 0 means the gate passed. Anything else blocks the merge.

## Merging through the gate

Use the wrapper so the gate cannot be skipped by accident:

```powershell
Plugins\HouseForge\Scripts\hf-merge.ps1 -Feature feature/<name>
```

It runs `hf-validate.ps1`, and only on success performs the `--no-ff` merge into `develop` with
the gate results embedded in the commit message.

## What must be tested

Every feature branch adds tests for what it introduced. A branch that adds no tests needs an
explicit reason stated in its merge commit. Minimum expectations by area:

- **Data model** — JSON round-trip; every validator rule exercised with a deliberately-bad spec.
- **Geometry** — watertightness, bounds match declared dimensions, surface-role polygroups present.
- **Editor** — end-to-end build of the sample 2BHK asserting actor counts and room areas.

Tests live in `Private/Tests/` of the module they cover, under the `HouseForge.*` category.

See also: [[02-git-workflow]], [[04-conventions]]
