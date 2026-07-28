# HouseForge

An Unreal Engine 5.8 editor plugin that turns AutoCAD interior drawings of Indian residential
units (2BHK / 3BHK) into fully modelled, editable Unreal levels.

Claude reads a drawing visually, produces a **House Spec** (validated JSON), and drives the editor
over MCP to build the geometry. Everything generated stays live and editable afterwards, and
placeholder furniture can be swapped for real assets once an asset library exists.

## Rules — read these first

@.claude/rules/01-scope.md
@.claude/rules/02-git-workflow.md
@.claude/rules/03-validation-gate.md
@.claude/rules/04-conventions.md

The short version:

1. **All changes go in `Plugins/HouseForge`.** Only exception: enabling plugins in `HouseBuilder.uproject`.
2. **`main` stable, `develop` integration, `feature/*` for all work.** Never commit to `main` or `develop` directly. Ask before pushing.
3. **No merge to `develop` until build and `HouseForge.*` tests pass.** Run `Scripts\hf-validate.ps1`.

## Architecture

```
Drawing (PNG/PDF) --Claude reads--> House Spec (JSON) --MCP--> Editor builds level
                                          ^                          |
                                          +------ export/read-back ---+

Built level --> [Material Panel]         tweak finishes, textures, UVs
            --> [Asset Replace Panel]    swap procedural fixtures for Content Browser assets
```

The House Spec is the contract between Claude's visual understanding and the geometry code.
Claude never issues raw geometry commands — it produces a spec and the plugin builds it. That
keeps generation deterministic, testable without an LLM, and re-runnable.

## Modules

- **`HouseForge`** (Runtime) — data model, validation, geometry generation, procedural actors, material library.
- **`HouseForgeEditor`** (Editor, `PostEngineInit`) — MCP toolset, editor subsystem, level creation, UI panels, screenshot capture.

## MCP

HouseForge exposes itself through UE 5.8's built-in Unreal MCP by registering a
`UToolsetDefinition` subclass with the `ToolsetRegistry` — it does not reference the MCP plugin
directly. Claude reaches it via the engine's `list_toolsets` → `describe_toolset` → `call_tool`
gateway.

Server settings live in `EditorPerProjectUserSettings` under
`[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]` (port 8000, `bAutoStartServer=True`).
Generate the Claude Code client config with the `ModelContextProtocol.GenerateClientConfig`
console command rather than hand-writing `.mcp.json`.

## Layout

```
Source/HouseForge/          runtime module
Source/HouseForgeEditor/    editor module
Scripts/                    hf-validate.ps1, hf-merge.ps1, gen_sample_drawings.py
Reference/Drawings/         input drawings (incl. the generated Sample2BHK set)
Reference/Specs/            house spec JSON
Docs/                       schema reference, workflow guide
```

## Environment

- Engine: `d:\EpicGames\Engine\UE_5.8`
- Host project: `d:\Projects\UnrealEngine\5.8\HouseBuilder`
- The sibling `Archvis` project holds an obsolete copy of this plugin — **ignore it entirely**.
