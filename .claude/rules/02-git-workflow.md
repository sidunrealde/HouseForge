# Rule 02 — Git workflow

Repository: `Plugins/HouseForge` → `https://github.com/sidunrealde/HouseForge.git`

## Branches

| Branch | Role |
|---|---|
| `main` | Stable. Only ever receives merges from `develop` at milestone boundaries. |
| `develop` | Integration. Receives merges from `feature/*` and only through the validation gate. |
| `feature/<name>` | All work. Branched off `develop`, merged back into `develop`. |

**Never commit directly to `main` or `develop`.** If you find yourself on either branch with
changes to make, stop and create a `feature/*` branch first.

## Commit discipline

- Commit at each working increment, not in one giant drop at the end.
- A commit should build. If it cannot build yet (mid-refactor), say so in the message body.
- Present-tense subject line, ≤72 chars, no trailing period.
- Reference the milestone in the body where relevant.

## Merging a feature branch

1. Run the validation gate (see [[03-validation-gate]]).
2. Only if it passes: `git checkout develop && git merge --no-ff feature/<name>`
3. The merge commit message records the gate evidence — build result and test counts.
4. Delete the feature branch after merge.

`--no-ff` is required. Feature history stays visible as a unit.

## Pushing

Pushing to the GitHub remote is an outward-facing action. **Ask the user before pushing**
any branch, including `main` and `develop`. Local commits and merges do not need to be asked about.

See also: [[01-scope]], [[03-validation-gate]]
