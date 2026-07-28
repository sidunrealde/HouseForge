# Rule 01 — Scope of changes

**All changes go inside `Plugins/HouseForge`.**

This plugin is a self-contained git repository (`https://github.com/sidunrealde/HouseForge.git`).
The surrounding `HouseBuilder` project is not version-controlled, so anything written outside the
plugin is invisible to history and will be lost.

## The one documented exception

`HouseBuilder.uproject` may be edited **only** to enable plugins HouseForge depends on:

| Plugin | Why |
|---|---|
| `GeometryScripting` | `GeometryScriptingCore` — the primitive and boolean operations the generators use |
| `ToolsetRegistry` | Where `UHFToolset` registers itself; MCP surfaces it from there |
| `ModelContextProtocol` | The engine's MCP server, which Claude connects to |
| `EditorToolset` | Reuses Epic's viewport capture and scene tools instead of reimplementing them |
| `PythonScriptPlugin` | Hard requirement of `ToolsetRegistry`; enabled explicitly rather than implicitly |

A project has to opt into its plugins; there is no way to do this from inside a plugin. Any change
to that file beyond the `Plugins` array needs explicit approval from the user first, and adding a
plugin not on this list means updating this table in the same commit.

## Explicitly out of scope without approval

- `Source/HouseBuilder/**` — the host project's game module
- `Config/**` — project ini files
- `Content/**` at project level (plugin content goes in `Plugins/HouseForge/Content`)
- The sibling `Archvis` project at `d:\Projects\UnrealEngine\5.8\Archvis` — **ignore it entirely**.
  It contains an older copy of this plugin. Do not read from it, copy from it, or reference it.

## Generated levels

Levels built by HouseForge are user output, not plugin source. They belong in the project's
`Content/` folder and are not committed to the plugin repo.

See also: [[02-git-workflow]], [[03-validation-gate]], [[04-conventions]]
