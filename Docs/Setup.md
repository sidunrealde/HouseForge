# Setting HouseForge up in a project

From nothing to a flat built out of a drawing. Follow it in order. Steps 3 and 7 are the ones that
catch people out, and both fail in the same misleading way — everything looks configured and
nothing works.

If you only want to know how to *use* it once it runs, that is
[`DrawingWorkflow.md`](DrawingWorkflow.md). This page is about getting it installed.

## What you need first

| | |
|---|---|
| **Unreal Engine 5.8** | Exactly 5.8. The plugin uses `UToolsetDefinition` and the engine's MCP server, neither of which exists in 5.7 or earlier. |
| **A C++ project** | HouseForge ships as source, so the project has to be able to compile it. See below if yours is Blueprint-only. |
| **Visual Studio 2022** (Windows) | With *Game development with C++*. On Mac, Xcode. |
| **Claude Code** | The plugin does not read drawings by itself. Claude does, over MCP. Without it you can still build a saved spec, but you cannot turn a new drawing into one. |
| **Python** | Only for PDF drawings and for regenerating the reference set. Step 5. |

**If your project is Blueprint-only**, add any C++ class to it once (*Tools > New C++ Class…*,
pick `None`, Create Class). Unreal converts the project and generates the solution. A
Blueprint-only project has no way to compile a source plugin, and the failure it gives you
instead — the plugin silently missing from the list — does not say so.

## 1. Put the plugin in the project

Clone or copy it so it lands here, with the `.uplugin` directly inside:

```
<YourProject>/
  <YourProject>.uproject
  Plugins/
    HouseForge/
      HouseForge.uplugin
      Source/
      Docs/
```

```bash
cd <YourProject>
mkdir -p Plugins
git clone https://github.com/sidunrealde/HouseForge.git Plugins/HouseForge
```

`Plugins/HouseForge` keeps its own git history, separate from the project's. That is deliberate —
the plugin is versioned, the project around it is yours.

## 2. Enable it

Right-click the `.uproject` → **Generate Visual Studio project files**, then open the project. If
Unreal offers to rebuild missing modules, say yes.

**Edit > Plugins**, search *HouseForge*, tick it, restart.

## 3. Enable the MCP server plugin — this one does not come for free

Still in **Edit > Plugins**, search **Model Context Protocol** and tick it. Restart.

This step is separate because HouseForge deliberately does not depend on the MCP plugin. It
registers itself with the `ToolsetRegistry` and the MCP plugin picks it up from there, which is
what keeps the two independently replaceable. The cost of that design is exactly this: enabling
HouseForge does *not* pull MCP in with it, and without MCP the plugin builds, loads, and gives you
no way to get a drawing into it.

Everything else cascades and you do not have to think about it — `GeometryScripting`,
`ToolsetRegistry` (which brings `PythonScriptPlugin`, `EditorScriptingUtilities` and `FileSandbox`),
and `MeshModelingToolset`.

One more worth ticking, though nothing breaks without it: **Modeling Tools Editor Mode**. It is
what lets an artist sculpt the generated geometry by hand, which is half the point of the plugin
generating dynamic meshes rather than static ones.

### Checking it took

**Tools > HouseForge Panel** should open a panel — under a *HouseForge* heading near the bottom of
the Tools menu. (**Window** carries the same tab under the Level Editor group; both routes open the
one panel rather than a second empty one.) If the entry is missing, the editor module did not
load — check the log for `LogHouseForgeEditor`.

The line to look for at startup is:

```
LogHouseForgeEditor: Registered the HouseForge MCP toolset.
```

If instead you see `ToolsetRegistry is unavailable; HouseForge will not be reachable over MCP`,
step 3 did not take.

## 4. Build

The editor will usually have built it for you on first load. To build by hand, or after pulling:

```powershell
& "<Engine>\Engine\Build\BatchFiles\Build.bat" <YourProject>Editor Win64 Development `
    -project="<full path>\<YourProject>.uproject" -waitmutex
```

Build **Development**. That is what the validation gate builds and tests, and a different
configuration is a different set of binaries — a fix built into one does not appear in the other.

To run the plugin's own test suite — worth doing once, to confirm the install rather than to test
your changes:

```powershell
Plugins\HouseForge\Scripts\hf-validate.ps1
```

It builds, then runs the `HouseForge.*` automation suite headless, and exits non-zero on any
failure. A clean install passes all of it.

## 5. Python, for PDF drawings

AutoCAD sets usually arrive as PDF, and a PDF has to be rasterised to images before Claude can read
it. Run this once:

```powershell
Plugins\HouseForge\Scripts\hf-drawings.ps1
```

It creates `Scripts/.venv` inside the plugin and installs Pillow into it. **Nothing is installed
system-wide** and the venv is gitignored.

Skip this and PNG and JPG drawings still import fine — only PDF needs it.

## 6. Start the MCP server and point Claude at it

**Tools > Start Unreal MCP Server**, in the same *HouseForge* section. This starts the server *and*
writes `.mcp.json`
into the project folder, which is the file Claude Code reads to find it.

To have it start with the editor instead of asking every time, tick *Auto Start Server* under
**Editor Preferences > Plugins > Model Context Protocol**.

## 7. Approve the server — once, and everyone hits this

From the project folder:

```bash
claude
```

It will ask whether to trust the `unreal-mcp` server it found in `.mcp.json`. **Approve it, then
`/exit`.** This is a one-time step per project and nothing works until you do it.

Claude Code treats a project-scoped `.mcp.json` as untrusted input — the file arrives with a
repository and can point anywhere — so it refuses to connect until a human says yes. Every other
signal says healthy while this is outstanding: the port is open, the config is correct, the editor
logged its listener. `claude mcp list` is what tells you the truth:

```
unreal-mcp: http://127.0.0.1:8000/mcp (HTTP) - ⏸ Pending approval (run `claude` to approve)
```

**Approval is stored per directory**, in `<folder>/.claude/settings.local.json`. Approving in some
other folder does nothing for this one, so do it in the project folder.

Then ask Claude to list its toolsets; `HouseForge` should be among them.

Write `.mcp.json` through the menu entry rather than by hand — the port and transport have to match
what the server actually bound, and a hand-written file that disagrees fails as "no tools found",
which reads like the plugin is broken.

## 8. Build something

Open **Tools > HouseForge Panel**. Three sections, in the order you use them:

1. **CLAUDE** — press *Check connection*. Green *Connected* means the whole chain works. Anything
   else names which link is broken and what to do about it.
2. **DRAWINGS** — drop the sheets in, or *Browse…*. Then **Generate**, and watch the trace.
3. **SURFACES** — what everything is made of, once it is built.

Generate hands the drawings to your own Claude Code, which reads them and builds the flat in this
editor. Importing drawings and re-materialling do not need Claude; only Generate does, and only
Generate is disabled without it.

With no drawing of your own to hand, the reference 2BHK set is already in
`Reference/Drawings/Sample2BHK/`. It is a real set — plan, furniture layout, reflected ceiling plan
and elevations — generated from a known spec, so what Claude builds from it can be diffed against
ground truth.

The rest of the loop — validate, apply, capture, compare, correct — is in
[`DrawingWorkflow.md`](DrawingWorkflow.md).

## When it does not work

**The plugin is not in the Plugins list.** The `.uplugin` is not directly inside
`Plugins/HouseForge/`. A nested `HouseForge/HouseForge/` from a clone is the usual cause.

**"Missing modules … would you like to rebuild?" then failure.** Build from the command line
(step 4) rather than from the dialog — the dialog swallows the compiler output, and the real error
is in it.

**The panel opens but every section is empty.** Expected with no level open and no house built.
SURFACES stays usable regardless, because finishes are assets rather than level state.

**Claude connects but has no HouseForge tools.** In order, and the first one is what it usually
is: has the server been approved (step 7 — `claude mcp list` says `Pending approval` if not), is
the server running (step 6), did `Registered the HouseForge MCP toolset` appear at startup
(step 3), was Claude started in the project folder.

**The panel says "Server not configured" and `.mcp.json` is nowhere.** The MCP plugin chooses
where to write it from the ENGINE's install kind, not the project: on an engine built from source
it writes to the workspace root instead of the project folder. Copy it into the project folder.
*Tools > Start Unreal MCP Server* now checks for this and says so rather than reporting success.

**PDF import says it cannot rasterise.** Step 5 has not been run, or it failed without a network.
`hf-drawings.ps1 -SvgOnly` skips the venv entirely, but then PDF import stays unavailable.

**A drawing imports but Claude builds it at the wrong scale.** Read the units off the drawing
rather than assuming — this is the one misread no structural rule can catch, because the spec comes
out perfectly self-consistent and merely the wrong size. `DrawingWorkflow.md` has the detail.

## What the artist actually needs to know

Three things, and none of them are in this file:

- Everything generated stays editable. Parametric in the details panel, by hand with the Modeling
  Tools, and the two do not fight — see *Editing the result* in
  [`DrawingWorkflow.md`](DrawingWorkflow.md).
- Hand edits are never silently overwritten. An element you sculpt opts out of regeneration, and
  **Revert To Generated** is the only thing that discards that work.
- Baking to static meshes is a toggle, not a conversion. The dynamic mesh stays alongside, and you
  can switch back and carry on editing. See [`LumenAndTheBake.md`](LumenAndTheBake.md) for why
  baking matters for lit renders.
