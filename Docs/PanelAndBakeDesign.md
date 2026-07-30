# Control panel and reversible bake - design

Produced by the `houseforge-panel-and-bake-design` workflow (11 agents) against the real
source, not against assumptions. It is the design input for the `hf-bake-and-assets` and
`hf-control-panel` milestones.

---

# HouseForge Control Panel + Reversible Bake â€” implementation specification

Supersedes `Docs/PanelAndBakeDesign.md` (that doc was a single-candidate critique; this is the synthesis). Grounded in the real source: `AHFElementActor` (root `UDynamicMeshComponent`, `bArtistEdited`, `HandleMeshChanged`, `bGenerating` guard, `CommitMesh`), `AHFArticulatedActor` (per-part `UDynamicMeshComponent`s â€” `AHFOpeningActor` already ships one moving part today), `AHFHouseActor::BuildGeometry` (preserves only `ShouldPreserveOnRebuild()`), `UHFEditorSubsystem` (the single API), `FHFValidationResult` (`Code` / `ElementId` / `Message`).

Shape: **artist-station** â€” a flat vertical stack of sections over the editor's own selection, no workflow rail, no tree. Grafts applied: live readiness band, generation actions promoted to the top, concrete issue rows, provenance/units line, filter chips on a flat list, whole-house bulk bake inside the BAKE section.

---

## 1. Wireframe â€” main state (house in level, 5 actors selected in the viewport)

```
+= HouseForge ================================================[...]=+
| Sample2BHK      63 elements      mm -> cm (?)          [X]1 [!]4  |
| from Sample2BHK/Plan-01.png                                        |
| [ Re-validate ] [ Rebuild all ] [ Capture top-down ] [ Open last ] |
+--------------------------------------------------------------------+
| v ISSUES                                     1 error, 4 warnings   |
|   [X] OpeningExceedsWall   D2                                      |
|       spans 0..95 but wall W3 is only 90 long                      |
|   [!] MissingSwing         D4    door has no swing direction       |
|   [!] LowHeadroom          FC_Living   soffit 2.34 m under beam B2 |
|   [!] OverlappingFixtures  WD_Bed1                                 |
|   [!] ImplausibleScale     -     total floor area 412 m2           |
+--------------------------------------------------------------------+
| ROOMS   (Living)(M.Bed)(Bed 2)(Kitchen)(Bath)(Utility)(Foyer)      |
|         (Balcony)  (not in any room 3)                             |
+--------------------------------------------------------------------+
| [ find element............. ]  (Err)(Warn)(Edited)(Baked)(Stale)   |
|   W3    wall  internal 2100 x 115 x 3000   [X] D2         [#]      |
|   W4    wall  internal 1800 x 115 x 3000   * edited       [ ]      |
|   D2    door  900 x 2100  sill 0  in W3                   [#]!     |
|   R_Living  room  13.4 m2  h 3.00                         [#]      |
|                                    ... 12 of 63 shown  [Select all]|
+--------------------------------------------------------------------+
| SELECTED  R_Living floor + 4 walls        [Details] [Revert] [Del] |
+--------------------------------------------------------------------+
| v BAKE                                          47 / 63 baked      |
|   [ -o- ] Baked            mixed: 3 of 5 selected                  |
|   Dynamic meshes are kept. Switching back restores them exactly.   |
|   (!) 3 baked elements are stale     [ Rebake stale (3) ]          |
|   [ Bake all ]  [ All live ]      -> /Game/HouseForge/Baked/...    |
+--------------------------------------------------------------------+
| ok setup ready              last MCP: ModifyElement  12:04:31      |
+--------------------------------------------------------------------+

 [ ] live   [#] showing baked   [#]! baked but stale   [-] mixed
```

Two other states, same stack:

* **Readiness failing** â€” a red band pushes in *above* the house bar: `[X] Python environment missing â€” PDF import needs Scripts/.venv (Pillow, PyMuPDF). Nothing is installed system-wide. [Set up now]` / `[X] MCP server not running [Start server & write .mcp.json] [ ] start with the editor` / `[v] MCP toolset registered (13 tools)`. Everything below stays usable.
* **No house** â€” house bar, issues, rooms, find, selected and bake sections are all absent (not collapsed â€” absent). In their place: `1 DRAWINGS` (sets + counts + `[Import...]` with a working **Set name** field + `[Open folder]`), `2 READ â€” this step happens in Claude, not here [Copy prompt]`, `3 BUILD from a saved spec` (list of `Reference/Specs/*.json`, excluding `Sample2BHK.json` with a visible footnote that it is test ground truth).

---

## 2. Sections â€” default state

| Section | Present when | Default |
|---|---|---|
| Readiness band | any probe fails | expanded, pinned top. Collapses to the footer's `ok setup ready` when all three pass. Probes re-run on a 5 s ticker and on tab activate â€” the MCP server can stop mid-session |
| House bar | house exists | always visible, never collapsible. 2 lines. `mm -> cm` tooltip shows `FHFHouseSpec::UnitsSource` verbatim; the drawing path is a hyperlink that opens the sheet in the OS viewer |
| Action row | house exists | always visible. Re-validate / Rebuild all / Capture top-down / Open last capture. **Explicitly a temporary promotion** â€” these belong in `[...]` once the daily loop stops being read â†’ build â†’ spot misread â†’ rebuild |
| `[...]` menu | always | Import drawings (with Set name)â€¦, Open specâ€¦, Save spec asâ€¦, Copy spec JSON, Convert lengthâ€¦, Show drawings folder, Show specs folder, Show baked folder, Start MCP server, Show spec wireframe (checkbox â†’ `bShowPreview`), Delete orphan baked assetsâ€¦ |
| ISSUES | house exists | **expanded** when any error; **collapsed to its header** when warnings only; header reads `No issues` when clean. Never hidden â€” a clean house should say so |
| ROOMS | house exists and â‰¥1 room | visible. Chips wrap; `not in any room (N)` chip appears only when N > 0 |
| FIND | house exists | visible, empty text, all chips off. List renders only when text or a chip is active â€” an always-on 63-row list in a 400 px dock is the thing that made the tree designs unpleasant |
| SELECTED | â‰¥1 HouseForge actor selected | visible, 1 line. `3 elements selected` for multi. Greys in place on empty selection; does not collapse |
| BAKE | house exists | expanded |
| Footer | always | 1 line: setup status + last MCP tool name and time |
| SURFACES / ASSETS / LIGHT | never, this milestone | not built, not stubbed. Section registry is the reservation (Â§3) |

---

## 3. C++ to create â€” `Plugins/HouseForge/Source/HouseForgeEditor/`

| Path | What it does |
|---|---|
| `Private/UI/HFPanelIds.h` | `HFPanelTabIds::HouseForgePanel` FName, shared by spawner, layout extender and the Tools menu entry |
| `Private/UI/FHFPanelSection.h` | `struct FHFPanelSection { FName Id; TFunction<bool(const FHFPanelState&)> IsRelevant; TFunction<TSharedRef<SWidget>()> Build; }`. `SHFHousePanel::Construct` builds from a `TArray` of these â€” the seam materials/lighting/assets append to later |
| `Private/UI/FHFPanelState.h/.cpp` | Non-widget controller and the only read path. Caches: house weak ptr, spec snapshot, `FHFValidationResult`, flattened `TArray<TSharedPtr<FHFElementRow>>`, room chips, bake tallies, setup status, MCP heartbeat. `FSimpleMulticastDelegate OnChanged`. Refreshes on `FEditorDelegates::MapChange`, `OnLevelActorAdded/Deleted`, `FCoreUObjectDelegates::OnObjectPropertyChanged` (filtered to `AHF*`), `USelection::SelectionChangedEvent`, plus `RequestRefresh()`; all debounced through one `FTSTicker`. Testable headlessly â€” this is where the panel's logic lives |
| `Private/UI/HFElementRow.h` | `FHFElementRow { FName Id; FName Category; TWeakObjectPtr<AHFElementActor> Actor; FString Summary; bool bArtistEdited; EHFRenderMode RenderMode; bool bStale; EHFValidationSeverity WorstIssue; TArray<FName> Rooms; }` and `FHFRoomChip { FName RoomId; FString Label; EHFRoomType Type; TArray<TWeakObjectPtr<AHFElementActor>> Members; }` |
| `Private/UI/SHFHousePanel.h/.cpp` | Tab root. Owns `FHFPanelState`, the section array, `FUICommandList` |
| `Private/UI/SHFReadinessBand.h/.cpp` | Three probe rows, one fix button each, plus the MCP auto-start checkbox |
| `Private/UI/SHFHouseBar.h/.cpp` | Name / element count / units badge / provenance hyperlink / issue badge / action row / `[...]` menu |
| `Private/UI/SHFIssuesList.h/.cpp` | `SListView<TSharedPtr<FHFValidationIssue>>`. Row = severity glyph, `Code`, `ElementId`, then `Message` verbatim on line 2. Click selects the offending actor through the same path the find list uses; nothing else |
| `Private/UI/SHFRoomChips.h/.cpp` | `SWrapBox` of `SHFRoomChip` toggle buttons. Click selects that room's members, ctrl-click adds |
| `Private/UI/SHFFindList.h/.cpp` | `SSearchBox` + `TTextFilter<FHFElementRow>` + `SBasicFilterBar` chips (Errors, Warnings, Edited, Baked, Stale) over a flat `SListView`, multi-select, context menu (Select / Frame / Revert / Delete element and dependents), `[Select all N]` |
| `Private/UI/SHFFindRow.h/.cpp` | `SMultiColumnTableRow` â€” columns `Element`, `State`, `Display` |
| `Private/UI/SHFSelectionStrip.h/.cpp` | One-line readback in HouseForge vocabulary + `[Details]` (focuses `LevelEditorSelectionDetails`) / `[Revert]` / `[Delete]` |
| `Private/UI/SHFBakeSection.h/.cpp` | Tri-state `SCheckBox` (ToggleButton style) scoped to selection, scope caption, `N / M baked`, the permanent reassurance sentence, stale row, `[Bake all]` / `[All live]`, folder hyperlink |
| `Private/UI/SHFLengthConverter.h/.cpp` | ~60-line popup over `FHFUnits::ParseLengthToCentimeters`. Kept because a unit misread produces a self-consistent wrong house |
| `Private/UI/FHFPanelCommands.h/.cpp` | `TCommands`: SelectInViewport, FrameInViewport, RevertToGenerated, DeleteElement, ToggleBaked, RebakeStale, Revalidate |
| `Private/UI/FHFRoomMembership.h/.cpp` | Pure static: `Resolve(const FHFHouseSpec&, double Tolerance, TMap<FName,TArray<FName>>& OutRoomToElements, TArray<FName>& OutUnassigned)`. Exact for rooms/ceilings/fixtures (`RoomId` exists); **derived** for walls and openings by centreline-vs-polygon containment. Editor-side, no world access, unit-testable |
| `Private/Setup/FHFSetupProbe.h/.cpp` | `Probe() -> FHFSetupStatus { bVenvReady, bMcpRunning, bToolsetRegistered, FString Detail[3] }`; `ProvisionPython()` runs `hf-drawings.ps1 -ProvisionOnly` via `FMonitoredProcess`; `StartMcp()` runs the two Exec commands `StartMcpServer()` already runs; `SetMcpAutoStart(bool)` writes `bAutoStartServer` into `EditorPerProjectUserSettings` via `GConfig` |
| `Private/Bake/FHFBakeService.h/.cpp` | The only code that creates assets. `Bake(AHFElementActor*, FHFBakeReport&)`, `BakeMany(TArrayView<AHFElementActor*>, FHFBakeReport&)`, `RebakeStale(UWorld*, int32&)`, `FindOrphans(UWorld*, TArray<FAssetData>&)`, `DeleteOrphans(...)`. `FDynamicMesh3` â†’ `FMeshDescription` â†’ package under the house's `BakedAssetFolder` â†’ stamp `UHFBakedMeshUserData` â†’ asset registry â†’ save |
| `Private/HFMcpActivity.h/.cpp` | `{FString ToolName; FDateTime At;}` set on entry by every `UHFToolset` static. ~30 lines, and the only evidence the artist gets that Claude is working rather than hung |
| `Private/Tests/HFPanelStateTests.cpp`, `HFBakeServiceTests.cpp`, `HFRoomMembershipTests.cpp`, `HFEditorApiTests.cpp` | Â§6 |

**Runtime module additions** (unavoidable â€” bake *state* lives on the actor):
`Source/HouseForge/Public/Actors/HFBakeTypes.h` + `Private/Actors/HFBakeTypes.cpp` â€” `EHFRenderMode`, `FHFBakedPart`, `UHFBakedMeshUserData : UAssetUserData`, `FHFBakeHooks` (static delegate the editor module binds).
Modified: `HFElementActors.h/.cpp`, `HFArticulatedActor.h/.cpp` (expose `GetBakeSourceComponents()`), `HFHouseActor.h/.cpp`, `HFEditorSubsystem.h/.cpp`, `HFToolset.h/.cpp`, `HouseForgeEditor.cpp`, `HouseForgeEditor.Build.cs` (+`MeshDescription`, `StaticMeshDescription`, `MeshConversion`, `AssetRegistry`, `EditorWidgets`), `Scripts/hf-drawings.ps1` (+`-ProvisionOnly`).

**Tab registration** â€” inside the existing `UToolMenus::RegisterStartupCallback` handler, not raw `StartupModule`:

```cpp
FGlobalTabmanager::Get()->RegisterNomadTabSpawner(HFPanelTabIds::HouseForgePanel,
    FOnSpawnTab::CreateStatic(&SpawnHousePanelTab))
  .SetDisplayName(LOCTEXT("HouseForgeTab", "HouseForge"))
  .SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory())
  .SetMenuType(ETabSpawnerMenuType::Enabled);

// FLevelEditorModule::OnRegisterLayoutExtensions:
Extender.ExtendLayout(FTabId("LevelEditorSelectionDetails"), ELayoutExtensionPosition::After,
    FTabManager::FTab(FTabId(HFPanelTabIds::HouseForgePanel), ETabState::ClosedTab));
```

Plus `Tools > HouseForge > HouseForge Panel`, invoking the same tab id. Two doors, one tab.

**Selection model.** The panel owns no selection. Viewport â†’ panel: subscribe to `USelection::SelectionChangedEvent`, filter to `AHFElementActor`/`AHFHouseActor`, ignore anything else rather than blanking. Panel â†’ viewport: resolve rows to actors by `(UClass, ElementId)` â€” the same key `BuildGeometry`'s `Preserved` map already uses â€” then `BeginBatchSelectOperation` / `SelectNone(false,true)` / `SelectActor` per actor / `EndBatchSelectOperation` / `NoteSelectionChange`, inside one transaction. **Single click never moves the camera**; double-click / Enter / context-menu Frame does.

---

## 4. Reversible bake â€” final instructions

### 4.1 Framing (a standing rule, not a panel choice)

The control is **`Baked`**, a switch. No control anywhere in HouseForge may carry a word implying replacement â€” no *Flatten*, no *Convert to Static Mesh*, no *Bake and remove*. "Bake" appears only where it means *the static mesh asset that was produced*. The sentence **"Dynamic meshes are kept. Switching back restores them exactly."** sits permanently under the switch, at the point of action, not in a tooltip. No confirmation dialog: a control that needs one is the wrong control.

### 4.2 State â€” `HFBakeTypes.h`

```cpp
UENUM(BlueprintType) enum class EHFRenderMode : uint8 { Dynamic, Baked };

USTRUCT() struct FHFBakedPart
{
    UPROPERTY(VisibleAnywhere) FName SourceComponentName;           // NAME_None = the root Mesh
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMesh> BakedMesh;   // hard ref: cooker + reference viewer must see it
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Component;
    UPROPERTY(VisibleAnywhere) FSoftObjectPath BakedAssetPath;      // survives a force-delete, so we can name what went missing
    UPROPERTY(VisibleAnywhere) int32 BakedAtMeshRevision = INDEX_NONE;
};
```

On `AHFElementActor`:

```cpp
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="HouseForge|Bake") EHFRenderMode RenderMode = EHFRenderMode::Dynamic;
UPROPERTY(VisibleAnywhere,  Category="HouseForge|Bake") TArray<FHFBakedPart> BakedParts;
UPROPERTY(EditAnywhere,     Category="HouseForge|Bake") bool bAutoRebakeOnRegenerate = true;
UPROPERTY(EditAnywhere,     Category="HouseForge|Bake") bool bUnbakeOnHandEdit = true;
UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category="HouseForge|Bake") int32 MeshRevision = 0;
UPROPERTY(NonTransactional, VisibleAnywhere, AdvancedDisplay, Category="HouseForge|Bake") FGuid BakeOwnerGuid;
UPROPERTY(Transient, VisibleAnywhere, Category="HouseForge|Bake") bool bBakeAssetMissing = false;
```

`bArtistEdited` semantics are **unchanged and orthogonal**. All four of {edited, generated} Ã— {baked, dynamic} must round-trip through save/load.

`BakeOwnerGuid` is `NonTransactional` deliberately: undoing a bake must not revert the guid, or a redo fails to recognise its own asset and mints a duplicate.

**Per-part from day one is required, not speculative.** `AHFOpeningActor` already builds a moving door leaf as its own `UDynamicMeshComponent` (`AHFOpeningActor::BuildParts` â†’ `FHFGenerators::BuildOpeningParts`). A single-mesh bake would weld a door shut today, which rule 04 forbids outright.

### 4.3 Components

Part 0's `UStaticMeshComponent` is a constructor default subobject (`CreateDefaultSubobject<UStaticMeshComponent>("BakedMesh_0")`, `SetupAttachment(Mesh)`, created hidden and non-colliding). Parts > 0 are created by `EnsurePartComponent(int32)` with `NewObject` + `AddInstanceComponent` + `RegisterComponent`, attached to **that part's dynamic component** so it inherits the articulated pose for free.

### 4.4 The switch â€” the only place visibility or collision is touched

```cpp
void AHFElementActor::ApplyRenderMode(EHFRenderMode Mode)
{
    const bool bBaked = (Mode == EHFRenderMode::Baked) && HasAllBakedAssets();
    bBakeAssetMissing = (Mode == EHFRenderMode::Baked) && !bBaked;

    for (UDynamicMeshComponent* Src : GetBakeSourceComponents())
    {
        Src->SetVisibility(bBaked ? false : true);   // bPropagateToChildren stays false
        Src->SetHiddenInGame(bBaked);
        Src->SetCollisionEnabled(bBaked ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
        Src->SetIsEditable(!bBaked);
    }
    for (FHFBakedPart& P : BakedParts)
    {
        if (!P.Component) continue;
        if (bBaked && !P.Component->IsRegistered()) P.Component->RegisterComponent();
        P.Component->SetVisibility(bBaked);
        P.Component->SetHiddenInGame(!bBaked);
        P.Component->SetCollisionEnabled(bBaked ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
        if (!bBaked && P.Component->IsRegistered()) P.Component->UnregisterComponent();
    }
    RenderMode = bBaked ? EHFRenderMode::Baked : EHFRenderMode::Dynamic;
}
```

Two non-obvious requirements. **Collision switches with visibility** â€” leaving both on double-traces every wall and leaves complex-as-simple dynamic collision under a mesh the user believes is the only thing there. **The baked component is unregistered while Dynamic**, not merely hidden: a hidden-but-registered `UStaticMeshComponent` is a candidate `UStaticMeshComponentToolTarget`, and an artist starting a Modeling Tool on what they believe is the live wall must not silently edit a baked asset instead. Unregistering removes the candidate entirely. (In Baked mode the hazard reverses and is acceptable: the switch is visibly on, the dynamic mesh is untouched, and any edit to the baked asset is discarded by the next rebake â€” the panel says so in the stale row.)

**The `FDynamicMesh3` is never read, modified, cleared or rebuilt by baking.** Bake creates an asset and flips component state. That is the entire reason unbake is instant and lossless.

### 4.5 Staleness

`MeshRevision` increments in `CommitMesh()` (unconditionally, including under the `bGenerating` guard) and in `HandleMeshChanged()` / `HandlePartMeshChanged()`. `IsBakeStale()` is `any P: P.BakedMesh && P.BakedAtMeshRevision != MeshRevision`. One actor-level counter rather than one per part: editing a door leaf marks the frame stale too, which over-rebakes slightly, and buys a model simple enough to reason about. No mesh hashing â€” hashing 63 meshes to rediscover a fact the actor already knows is pure cost. A counter rather than a bool gives "baked 3 edits ago" and survives save/load and undo interleaving.

### 4.6 Interaction rules

* `PostEditChangeProperty` intercepts `RenderMode` by `GET_MEMBER_NAME_CHECKED` and calls `SetRenderMode()` **before** the existing catch-all falls through to `Regenerate()`. Without this, flipping the toggle regenerates the element.
* **Bake bakes what is on screen.** Never call `Regenerate()` first. A hand-edited wall bakes its sculpted form and unbakes back to that same sculpted form.
* `bUnbakeOnHandEdit` (default true): in `HandleMeshChanged`, if baked, switch to Dynamic. Otherwise the artist sculpts an invisible mesh, sees nothing change, and undoes work that actually applied.
* `bAutoRebakeOnRegenerate` (default true): after `CommitMesh` on a baked element, request a rebake through `FHFBakeHooks`. Off, a parameter edit leaves the viewport showing old geometry and `CaptureTopDown` screenshots a house that no longer matches the spec â€” the same silent false-pass `ApplySpecJson`'s validation gate exists to prevent.
* **`AHFHouseActor::BuildGeometry` must preserve baked elements.** Change the predicate to `Typed->ShouldPreserveOnRebuild() || Typed->RenderMode == EHFRenderMode::Baked`, then for a preserved-because-baked element: refresh its parameter struct from the spec, `Regenerate()` (it is not artist-edited), and rebake if `bRebakePreservedElementsOnRebuild`. Without this a rebuild destroys baked actors and orphans every asset.
* **Undo.** Panel-driven bakes run inside `FScopedTransaction` with `Modify()` on the actor and every touched component. Asset creation is not transactional; undoing a bake leaves the asset on disk, which is correct â€” re-baking then costs nothing.
* **`PostLoad` calls `ReconcileBakeState()`**: baked with any null part mesh â‡’ fall back to Dynamic and set `bBakeAssetMissing`. Never render nothing.

### 4.7 Routing â€” bake goes through the subsystem

The panel must not call `AHFElementActor::SetRenderMode` directly. Add to `UHFEditorSubsystem` (and wrap the first four in `UHFToolset`):

```cpp
FHFOperationResult SetElementRenderMode(const FString& Category, const FString& ElementId, bool bBaked);
FHFOperationResult SetHouseRenderMode(bool bBaked, int32& OutChanged);
FHFOperationResult RebakeStale(int32& OutRebaked);
FHFOperationResult GetBakeReport(FString& OutReport) const;
FHFOperationResult GetValidationReportForLevel(FHFValidationResult& OutResult) const;
FHFOperationResult LoadSpecFromFile(const FString& FileName, FString& OutSpecJson);  // finally calls FHFSpecSerializer::LoadFromFile
FHFSetupStatus     GetSetupStatus() const;
```

`LoadSpecFromFile` is deliberately **not** an MCP tool: a build-from-file tool is exactly the shortcut past drawing-reading that `HouseForge.Architecture.SampleIsNotOnTheBuildPath` guards. If the panel's flagship feature were the one thing Claude could not do, the two surfaces would drift from day one â€” hence everything else is wrapped.

### 4.8 Assets

`FHFBakeService` lives in the editor module (package creation is unreachable from runtime). The actor exposes only `AdoptBakedMesh(int32 PartIndex, UStaticMesh*, int32 AtRevision)` and `SetRenderMode()`, and reaches the service through `FHFBakeHooks::BakeElement`, a static delegate bound in `FHouseForgeEditorModule::StartupModule` and inert when unbound. Assets go to `AHFHouseActor::BakedAssetFolder`, defaulting to `/Game/HouseForge/Baked/<LevelName>` and written back on first bake so a later level rename does not scatter them â€” under `/Game`, never plugin content (rule 01: generated output is user output). `UHFBakedMeshUserData` stamps `{OwnerGuid, ElementId, ElementClassName, LevelPackageName, SourceMeshRevision, BakedAtUtc}`; `LevelPackageName` is what makes orphan deletion safe, because a scan can only see the open level.

---

## 5. Build order

Each step ships something usable alone.

1. **Tab shell + `FHFPanelState` + house bar + action row + footer.** A dockable panel naming the house, its element count, its declared units, its source drawing, with Re-validate / Rebuild all / Capture top-down. This alone makes *building and checking a house* reachable from the editor for the first time, and it establishes the state/refresh plumbing everything hangs off.
2. **ISSUES list.** 49 validator rule sites currently exist only as a text blob inside an MCP result. Click-to-select the offending actor. Highest value per line in the spec.
3. **FIND list + filter chips + SELECTED strip.** Read-only rows plus Select / Frame / Revert / Delete (routed to the cascading `DeleteElement`, never actor deletion). The panel is now the correction loop.
4. **Readiness band + probes + Import with a working Set name.** Closes the venv discoverability hole and fixes `HouseForgeEditor.cpp:52`, which hardcodes `FString()` so every interactive import is named after the first file.
5. **Bake runtime.** `HFBakeTypes.h`, per-part state, `ApplyRenderMode`, `MeshRevision`, `BuildGeometry` preservation, `ReconcileBakeState`. Fully testable headless with no UI and no asset creation (stub `AdoptBakedMesh` with a throwaway `UStaticMesh`).
6. **`FHFBakeService`** + subsystem routing + toolset wrappers + orphan scan.
7. **BAKE section UI.** The marquee feature lands seventh because it is the only part that needs 5 and 6 working first.
8. **ROOMS chips.** Last, because they are the only inferred thing in the panel and the find list already covers navigation; if the inference disappoints on a real plan, nothing above depends on it.

Steps 1â€“4 are the useful panel. 5â€“7 are the user's other request. 8 is optional. Honest estimate: three weeks part-time, not one.

---

## 6. Defer, and why

| Deferred | Why |
|---|---|
| SURFACES / ASSETS / LIGHT sections | The material library, asset override and lighting do not exist in `Source/`. Three greyed "planned" rows are fake UI. The `FHFPanelSection` array is the reservation; reserving a code seam is honest, reserving pixels is not |
| Per-property editing in the panel | The Details panel already does it better â€” `ShowOnlyInnerProperties`, `ClampMin`, `CallInEditor`, undo, multi-object edit. The panel gets a `[Details]` button, not a reimplementation that must be kept in sync with `FHFWall` forever |
| A spec JSON text editor | Rule 04: the spec is the import/export format, not a live second source of truth. `Copy spec JSON` in `[...]` is the whole surface |
| Element creation (Add Wall / Room / Fixture) | Houses come from drawings; `SampleIsNotOnTheBuildPath` enforces it. A creation UI is a plan editor â€” a different product, and it becomes the path everyone uses instead of reading the drawing |
| Drawing thumbnails | Needs an image-wrapper decode path and a cache. Filename rows first |
| Details-panel customisation for bake | `UPROPERTY(EditAnywhere, Category="HouseForge|Bake")` gives the dropdown, `CallInEditor` gives Rebake Now. A customisation buys ordering only |
| Cross-level orphan GC | A scan sees only the open level. Orphan deletion always lists candidates and requires confirmation, and is scoped to `LevelPackageName` matches |
| Undo of orphan deletion | Asset deletion is not transactional. Confirmation is the mitigation |
| `UHFHouseSpecAsset` factory | The class compiles and has zero references. Giving it a factory adds a second authoring path before anyone has asked for one |
| Fixture rows doing anything but focus | Fixtures have no actor class yet. A fixture row must **not** silently flip `bShowPreview`/`bShowFixtures` â€” that is a selection gesture mutating saved actor state. `Show spec wireframe` is a visible checkbox in `[...]` instead |

---

## 7. Automation tests

All named `HouseForge.*` so `hf-validate.ps1` catches them.

**`HouseForge.Bake.*`** â€” `Source/HouseForge/Private/Tests/HFBakeTests.cpp` (pure state, no service):
* `DynamicMeshSurvivesBake` â€” bake, then assert the `UDynamicMeshComponent`'s triangle count and every vertex position are identical. The central claim of the feature.
* `UnbakeRestoresLiveMesh` â€” bake â†’ unbake â†’ mesh identical, dynamic visible and registered, baked hidden and unregistered.
* `BakeDoesNotSetArtistEdited` â€” `bArtistEdited` still false after a bake.
* `ArtistEditedBakesSculptedForm` â€” hand-edit, bake, unbake; the edit is still there and `bArtistEdited` is still true.
* `HandEditWhileBakedUnbakes` â€” `bUnbakeOnHandEdit` fires.
* `CollisionFollowsVisibility` â€” exactly one of the two components has collision enabled, in each mode.
* `MeshRevisionMarksBakeStale` â€” regenerate after bake â‡’ `IsBakeStale()`.
* `MissingAssetFallsBackToDynamic` â€” null a part mesh, `ReconcileBakeState()`, assert Dynamic + `bBakeAssetMissing`.
* `AllFourStateCombinationsRoundTrip` â€” {edited, generated} Ã— {baked, dynamic} through serialise/deserialise.
* `ArticulatedBakeKeepsPartsSeparate` â€” bake an `AHFOpeningActor`, assert one `FHFBakedPart` per source component and that the leaf still moves with `SetPartOpenAmount`. Rule 04's "a bake must not weld a chest of drawers into a block", tested on the one articulated element that exists today.

**`HouseForge.Editor.Bake.*`**:
* `HouseRebuildPreservesBakedElements` â€” extends the existing rebuild-preserves-edits test: a baked, non-artist-edited element survives `BuildGeometry`, keeps its asset, is not orphaned.
* `BakeCreatesStampedAsset` â€” asset at the expected path with `UHFBakedMeshUserData` carrying the right `ElementId` and `LevelPackageName`.
* `RebakeReusesSameAsset` â€” no duplicate package, `BakeOwnerGuid` unchanged.
* `OrphanScanIgnoresOtherLevels` â€” an asset stamped with a different `LevelPackageName` is never reported.

**`HouseForge.Editor.Api.*`**:
* `SetElementRenderModeRoutesThroughSubsystem` â€” the subsystem method exists, is `BlueprintCallable`, and `UHFToolset` has a matching wrapper. Guards against the panel growing a private path.
* `LoadSpecFromFileRoundTrips` â€” finally exercises `FHFSpecSerializer::LoadFromFile`.
* `LoadSpecFromFileIsNotAnMcpTool` â€” reflection assert that no `AICallable` function on `UHFToolset` builds from a file path.

**`HouseForge.Editor.Panel.*`** (headless-safe; no Slate harness):
* `PanelStateDerivesRowsFromSpec` â€” build the sample house in a temp world, construct `FHFPanelState`, assert per-category row counts and summaries.
* `PanelStateFlagsIssuesOnRows` â€” an element with a validation error carries `WorstIssue == Error`.
* `PanelStateTracksBakeTallies` â€” `N / M baked` and the stale count match actor state.
* `SetupProbeReportsMissingVenv` â€” probe a temp path with no `.venv`, expect `bVenvReady == false` and actionable detail text.
* `TabSpawnerIsRegistered` â€” `FGlobalTabmanager::Get()->HasTabSpawner(HFPanelTabIds::HouseForgePanel)`.

**`HouseForge.Editor.Rooms.*`**:
* `MembershipIsExactForRoomIdElements` â€” ceilings and fixtures map by `RoomId`, never by geometry.
* `WallsOffPolygonLandInUnassigned` â€” a wall displaced past tolerance appears in the unassigned bucket, not in a wrong room.
* `SharedWallAppearsInBothRooms` â€” and each actor is selected once, not twice.

Widget rendering is not tested. Slate render tests are expensive and brittle; the value is in `FHFPanelState`, which is a plain non-widget class precisely so it can be tested without a tab ever being spawned.

---

## 8. Trade-offs, stated plainly

* **No workflow rail.** It reads beautifully on day 1 and is dead pixels on day 20, and its centre node â€” "Claude reads the drawing" â€” is a step the panel cannot perform. Section presence carries the same information for free. The loss is real: a first-time user no longer sees the whole workflow at a glance, and the no-house card's 1/2/3 layout is the only compensation.
* **The action row is a knowingly temporary graft.** Re-validate / Rebuild all / Capture belong in `[...]` for a finished product. They sit at the top because that is today's loop. Expect to demote them, and treat that as success, not churn.
* **Room chips are inferred for walls and openings.** `FHFWall` has no `RoomId` (HFTypes.h:195); only `FHFFalseCeiling` and `FHFFixture` carry one. Containment with a tolerance works on a clean orthogonal 2BHK and misfiles a wall Claude placed 4 cm off. Mitigations: a visible `not in any room (N)` chip, an exposed tolerance, and a wall belonging to every room it bounds. This is why chips are step 8 and the find list is step 3 â€” navigation must not depend on a guess.
* **Bake asset creation lives in the editor module and is reached from a runtime actor through a static delegate.** That indirection is ugly. The alternative â€” the runtime module depending on `UnrealEd` â€” is worse.
* **One `MeshRevision` per actor, not per part.** Editing a door leaf marks the frame stale and rebakes both. Wasted milliseconds, in exchange for a staleness rule one sentence long.
* **`bAutoRebakeOnRegenerate` defaults on.** It makes parameter edits on baked elements slower. Off, the viewport lies about what the spec says, and `CaptureTopDown` â€” the tool Claude uses to check its own work â€” screenshots the lie.
* **The sections stack vertically.** Right for four sections, wrong for seven. `FHFPanelSection` makes the eventual conversion to a mode strip one change in `Construct`; it does not make it free.
* **Unresolved until ten minutes in the actual editor** (settle during step 5, all have stated fallbacks): whether `UToolTargetManager` filters candidates by registration as assumed in Â§4.4; whether `UStaticMesh` async compilation needs an explicit `FinishCompilation` before collision assertions in a `-nullrhi` run; and whether polygroups survive `CommitMeshDescription` into a readable form â€” if not, surface-role targeting on baked meshes must go through material sections, which the material library should do anyway.

---

## The contest, and why this document was rewritten

The first run of this workflow serialised all three panel designs into one JSON blob and sliced
it at 24,000 characters. Design 1 filled the whole budget, so designs 2 and 3 never reached the
judges - and the cut landed mid-object, so even design 1 arrived as invalid JSON. The run still
returned a confident winner. That winner was simply the only candidate anyone had seen.

The judges caught it and said so unprompted; nothing in the workflow did. The fix budgets each
design separately (see `.claude/workflows/_shared.md`), and the workflow was re-run with the
audit and design phases replayed from cache.

**The verdict changed.** With all three designs actually visible, two of the three judges chose
design 3, the artist-station - a design that had been invisible the first time. The specification
above is the synthesis of the real contest, and it supersedes the earlier single-candidate one.

This is worth remembering when reading any other confident output from a fan-out: a winner is
only meaningful if the judges could see the field.

### Verdicts

**Lens: workflow-clarity**

Design 3 â€” artist-station

**Lens: Scalability â€” will the panel and bake model still be the right shape after false ceilings, the joinery kit, ~40 fixture types, a material library, lighting and asset replacement land? Judged against the real source on the current feature/joinery-kit branch, not against each design's claims about it. Secondary weight on cold-read clarity, capability coverage, bake safety, and one-developer Slate cost.**

Design 3 â€” artist-station. Build it, with the twelve grafts below. It is the only design whose cost is O(rooms) rather than O(elements), and the only one whose section structure maps 1:1 onto the remaining milestones instead of having to absorb them into a metaphor that does not fit.

**Lens: implementation-cost**

Design 1 â€” task-flow, with the workflow rail deleted and ten grafts applied

## Open questions on the bake, to settle in the editor

These are the things the design agent would not assert without checking. The first is the
dangerous one: it would silently break the artist-editable guarantee.

1. Modeling Tools target selection when one actor carries both a hidden UDynamicMeshComponent and a visible UStaticMeshComponent. I could not determine from the headers whether UToolTargetManager filters candidate components by visibility, so it is possible that starting a modelling tool on a baked element builds a UStaticMeshComponentToolTarget and edits the baked asset rather than the live mesh. The design mitigates it (SetIsEditable(false), bUnbakeOnHandEdit, a panel affordance) but this needs ten minutes in the actual editor to confirm. If it does target the static mesh component, the fix is to unregister BakedMeshComponent whenever RenderMode == Dynamic rather than merely hiding it.

2. Whether UStaticMesh::GetBodySetup() has a cooked triangle mesh available immediately after CreateStaticMeshAsset returns in a headless -nullrhi automation run. Static mesh builds are asynchronous (UStaticMesh implements IInterface_AsyncCompilation), so the collision trace assertions in HouseForge.Bake.BakedCollisionIsComplexAsSimple may need an explicit wait -- FStaticMeshCompilingManager::Get().FinishCompilation({SM}) is the likely call, but I have not verified its exact name and availability in 5.8. The CollisionTraceFlag assertion is safe regardless; only the trace half is in doubt.

3. What NewObject<UStaticMesh> actually does when an object of that name already exists in the package. This is why the design routes every repeat bake through CopyMeshToStaticMesh instead of CreateNewStaticMeshAssetFromMesh, which does no existence check before calling CreatePackage + NewObject. I am confident avoiding that path is correct; I am not confident about precisely how it misbehaves, so I did not write the failure mode as though I knew.

4. Whether the PolyTriGroups attribute that FDynamicMeshToMeshDescription writes (bSetPolyGroups defaults true) actually survives UStaticMesh::CommitMeshDescription and the subsequent build in a form that is readable later. If it does not, surface-role targeting on baked elements has to go entirely through material sections, which means the material library must assign role -> MaterialID on the dynamic mesh before any bake. That is the recommended approach anyway, so this is a question about a fallback, not the main path -- but it is worth resolving before the material panel is designed.

5. FGeometryScriptCopyMeshToAssetOptions::bUseBuildScale defaults to true. Our elements are all at identity transform with world-space geometry, so BuildScale should be 1.0 and this should be inert, but I have not traced what happens if a user scales an element actor and then re-bakes. Worth an explicit test once actor scaling is a supported operation.

6. Baked lighting. I set bGenerateLightmapUVs = true on creation because the planned lighting milestone might use it, but if the project commits to Lumen it is wasted build time on 150 meshes. This should be a setting on AHFHouseActor rather than hard-coded, and the default should be decided when the lighting milestone lands rather than now.

7. Whether the automation suite's headless editor has a valid GEditor for the transaction calls inside the GeometryScript asset functions. Scripts/hf-validate.ps1 runs UnrealEditor-Cmd with -unattended -nullrhi, which does have GEditor, so this should be fine -- but CreateNewStaticMeshAssetFromMeshLODs calls GEditor->BeginTransaction unguarded, and the recommended UE::AssetUtils::CreateStaticMeshAsset path does not, which is one more small reason to prefer the lower-level API.

## Capability inventory

58 user-facing capabilities exist or are planned. 39 of them cannot be reached
from any UI today - they are MCP-only, console-only, or not yet built. This is the list the
panel has to answer for:

- MCP toolset registration / availability
- Provision the local Python environment
- Regenerate the reference 2BHK drawing set
- Run the build + test validation gate
- Export the committed sample spec JSON
- Import into a named drawing set
- List available drawings
- Get the drawings folder path
- Get the specs folder path
- Convert a drawing dimension to centimetres
- Validate a House Spec without building
- See the validation report
- Build a house from spec JSON
- Read the level's house back as JSON
- Save the level's house to a spec file
- Load a saved spec file and rebuild it
- Author a spec as a Content Browser asset
- List what is in the level
- Patch one element's fields, validated with rollback
- Delete an element with dependent cascade
- Untick bArtistEdited to resume generation
- Collision on generated geometry
- Top-down orthographic capture of the house
- Surface-role polygroup tagging
- Real-world-scale UVs
- Watertightness / manifold check
- Create a house in the editor without a drawing
- Bake one element to a static mesh, keeping the dynamic mesh
- Unbake / switch an element back to its dynamic mesh
- Bulk bake / unbake (whole house or selection)
- Unified HouseForge panel
- False ceiling generation - 5 styles
- Cove channel and recessed spotlights
- Shared joinery kit (shutters, drawers, handles, plinths, cornices)
- Fixture generators (wardrobes, modular kitchen, beds, sanitary, fans, switch plates)
- Material library and material panel (textures, UVs, tiling per surface role)
- Lighting: real lights, cove strips, sky, PlayerStart
- Per-instance asset override for a fixture
- Batch asset swap pass from the Content Browser
