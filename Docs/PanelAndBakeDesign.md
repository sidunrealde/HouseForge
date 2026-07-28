# Control panel and reversible bake - design

Produced by the `houseforge-panel-and-bake-design` workflow (11 agents) against the real
source, not against assumptions. It is the design input for the `hf-bake-and-assets` and
`hf-control-panel` milestones.

**Read the caveat in "How much to trust this" before treating the panel design as settled.**

---

# HouseForge Control Panel + Reversible Bake â€” Implementation Specification

Grounded in the actual source: `AHFElementActor` (single `UDynamicMeshComponent` root, `bArtistEdited`, `HandleMeshChanged`, `bGenerating` guard), `AHFHouseActor::BuildGeometry` (preserves only artist-edited elements), `UHFEditorSubsystem` (the single API), `UHFToolset` (thin wrapper), `FHFValidationResult`.

---

## 1. The panel, final shape

One nomad tab. **No workflow rail.** The judge's core criticism holds: three of five rail nodes are one-time, two are permanent activities, and Materials/Lighting/Assets are activities not stages. The panel is instead a **vertical stack of collapsible sections** whose visibility is derived from state, with the one-time sections disappearing for good once a house exists.

### Wireframe â€” main state (house built, correcting it)

```
+----------------------------------------------------------------+
| HouseForge                                            [.] [:]   |   .=setup  :=overflow
+----------------------------------------------------------------+
|  Sample2BHK    63 elements    Plan-01.png                       |
|  units mm (title block)   78.4 m2   [f=]      [ Re-validate ]   |
|  [ Capture Top-Down ]  [ Open last capture ]  [ Rebuild all ]   |
+----------------------------------------------------------------+
| v  ISSUES                                  1 error, 4 warnings  |
|    [X] OpeningExceedsWall  D2                                   |
|        spans 0..95 but wall W3 is only 90 long                  |
|    [!] MissingSwing        D4                                   |
|    [!] LowHeadroom         FC_Living                            |
|    [!] OverlappingFixtures WD_Bed1                              |
|    [!] ImplausibleScale    -                                    |
+----------------------------------------------------------------+
| [ search elements................ ]  [ All v ]  [ ! only ]      |
+----------------------------------------------------------------+
|  ELEMENT                              STATE        DISPLAY      |
| -------------------------------------------------------------- |
|  v Walls (12)                                        [-]        |
|      W1    external  4200 x 230       hand-edited    [#]        |
|      W2    external  3300 x 230                      [ ]        |
|      W3    internal  2100 x 115       [X] D2         [ ]        |
|      W4    internal  1800 x 115                      [#]!       |
|  v Rooms (6)                                         [#]        |
|      R_Living    Living    13.4 m2                   [#]        |
|      R_Kitchen   Kitchen    6.1 m2                   [#]        |
|  > Openings (14)                                     [-]        |
|  > Beams (5)                                         [ ]        |
|  > Columns (2)                                       [ ]        |
|  > Ceilings (4)                                      [ ]        |
|  > Fixtures (21)   spec only - no geometry yet       [o]        |
+----------------------------------------------------------------+
|  W3  internal wall            [ Details ]  [ Revert ]  [ Del ]  |
|  2100 x 115 x 3000 . 1 opening . generated . live               |
+----------------------------------------------------------------+
|  Display  All 63   [ Live | Baked ]   47/63 baked   ^ stale (3) |
+----------------------------------------------------------------+
|  ok  setup ready          ModifyElement W3  12:04:31            |
+----------------------------------------------------------------+

  [ ] live dynamic mesh     [#] showing baked mesh
  [#]! baked, stale         [-] mixed group      [o] no actor
```

### First run / setup broken â€” the setup card takes over the top

```
+----------------------------------------------------------------+
| HouseForge                                            [.] [:]   |
+----------------------------------------------------------------+
|  !!  HouseForge is not ready                                    |
|                                                                 |
|  [X] Python environment missing                                 |
|      PDF import needs Scripts/.venv (Pillow, PyMuPDF).          |
|      Nothing is installed system-wide.       [ Set up now ]     |
|  [X] MCP server not running                                     |
|      Claude cannot reach HouseForge until it is.                |
|                          [ Start server & write .mcp.json ]     |
|      [ ] Start automatically with the editor                    |
|  [v] MCP toolset registered   (13 tools)                        |
+----------------------------------------------------------------+
```

### No house in the level â€” the one-time card

```
+----------------------------------------------------------------+
|  1  DRAWINGS                                       3 sets       |
|     Sample2BHK  11 sheets   [img][img][img][img]  [folder]      |
|     Flat-402     4 sheets   [img][img]            [folder]      |
|     ..........................................................  |
|     |            Drop PNG, JPG or PDF sheets here            |  |
|     |        [ Import... ]   Set name [ Flat-402....... ]    |  |
|     ..........................................................  |
|                                                                 |
|  2  READ - this step happens in Claude, not here                |
|     [ Copy prompt ]  "Read the drawings in Sample2BHK and       |
|                       build the house."                         |
|     [ Spec schema ]  [ Workflow guide ]                         |
|                                                                 |
|  3  BUILD from a saved spec                                     |
|     Flat-402.json      3 Jul   [ Build ]                        |
|     (Sample2BHK.json is test ground truth, not build input)     |
+----------------------------------------------------------------+
|  ok  setup ready          ListDrawings  12:01:02                |
+----------------------------------------------------------------+
```

---

## 2. Sections â€” visible by default vs collapsed

| Section | Shown when | Default state |
|---|---|---|
| **Title bar** | always | visible. `[.]` opens setup popup (or is a red badge when a probe fails). `[:]` overflow: Show Drawings Folder, Show Specs Folder, Copy Spec JSON, Save Spec Asâ€¦, Length Converterâ€¦, Show Spec Wireframe (checkbox), Browse Baked Assets, Delete Orphan Baked Assets |
| **Setup card** | any probe fails | expanded, pinned above everything, everything below dimmed but still usable |
| **Get-started card** | `FindHouseActor() == nullptr` | expanded; the whole card vanishes once a house exists (it is not collapsed to a header â€” dead pixels) |
| **House summary** | house exists | visible, never collapsible. 3 lines |
| **Issues** | house exists | expanded when errors exist; collapsed to the header line when warnings only; the header alone reading "No issues" when clean |
| **Search + filter** | house exists | visible, empty |
| **Element tree** | house exists | Walls + Rooms expanded, everything else collapsed. Expansion persists per project in `EditorPerProjectUserSettings` |
| **Inspector strip** | exactly 1 row selected | visible; "3 elements selected" for multi; hidden on empty selection |
| **Bake bar** | house exists | always visible |
| **Footer** | always | one line: setup status + last MCP tool call & timestamp |

Two deliberate omissions from the winning design: the five-node rail (replaced by section presence) and the "Finish" stage card (Save Spec As lives in the overflow menu; it is one button, not a stage).

**Extension point.** Sections are constructed from `TArray<FHFPanelSection>` in `SHFHousePanel::Construct`, each `{ FName Id; TFunction<bool(const FHFPanelState&)> IsRelevant; TFunction<TSharedRef<SWidget>()> Build; }`. Materials/Lighting/Assets append entries later. When a third heavy section lands the stack gets long â€” at that point convert the same array into a mode strip. That is one change in `Construct`, not a re-layout of the sections. Reserving the code seam rather than pixels is the honest version of "no re-layout later"; three greyed "planned" stubs in the UI are not.

---

## 3. C++ classes to create

### Editor module â€” `.../Source/HouseForgeEditor/`

| Path | What it does |
|---|---|
| `Private/UI/HFPanelIds.h` | `HFPanelTabIds::HouseForgePanel` FName constant, shared by the spawner and the layout extender. |
| `Private/UI/FHFPanelState.h/.cpp` | Non-widget controller. Caches the spec snapshot, `FHFValidationResult`, the flattened `FHFElementRow` array, setup probe results, and the MCP heartbeat. Broadcasts `FSimpleMulticastDelegate OnChanged`. Every widget reads this; nothing else calls the subsystem for reads. Refreshes on `FEditorDelegates::MapChange`, `OnLevelActorAdded/Deleted`, `FCoreUObjectDelegates::OnObjectPropertyChanged` (filtered to HouseForge actors) and an explicit `RequestRefresh()`, all debounced through one `FTSTicker` tick. |
| `Private/UI/FHFElementRow.h` | Row model: `FName Id; FName Category; TWeakObjectPtr<AHFElementActor> Actor; FString Summary; bool bArtistEdited; EHFRenderMode RenderMode; bool bStale; EHFValidationSeverity WorstIssue; TArray<TSharedPtr<FHFElementRow>> Children;` â€” group headers are rows with children and no actor. |
| `Private/UI/SHFHousePanel.h/.cpp` | Tab root. Builds the section stack, owns `FHFPanelState` and `FUICommandList`, handles panel-wide file drop. |
| `Private/UI/SHFSetupCard.h/.cpp` | Three probes with a fix button each, plus the auto-start checkbox. |
| `Private/UI/SHFGetStartedCard.h/.cpp` | Drawing sets + thumbnails + `SDropTarget` + Import with a live Set name field + Copy prompt + saved-spec build list. |
| `Private/UI/SHFHouseSummary.h/.cpp` | Name/count/source, units line with the `[f=]` converter popup, Re-validate, Capture Top-Down, Open last capture, Rebuild all. |
| `Private/UI/SHFIssuesList.h/.cpp` | One `FHFValidationIssue` per row; click selects the offending actor via the same path the element list uses. |
| `Private/UI/SHFElementList.h/.cpp` | `SSearchBox` + `TTextFilter<FHFElementRow>` + category combo + errors-only toggle over an `STreeView` (two levels: group, element). Multi-select, context menu. |
| `Private/UI/SHFElementListRow.h/.cpp` | `SMultiColumnTableRow<TSharedPtr<FHFElementRow>>` â€” columns `Element`, `State`, `Display`. |
| `Private/UI/SHFInspectorStrip.h/.cpp` | Two read-only lines + Details / Revert / Delete buttons. |
| `Private/UI/SHFBakeBar.h/.cpp` | Scope label, `SSegmentedControl<EHFRenderMode>`, baked count hyperlink to the Content Browser folder, stale-rebake button. |
| `Private/UI/SHFLengthConverter.h/.cpp` | Small popup over `FHFUnits::ParseLengthToCentimeters`. ~60 lines; the only pure-convenience widget, kept because a unit misread is the failure mode that produces a self-consistent wrong house. |
| `Private/UI/FHFPanelCommands.h/.cpp` | `TCommands<FHFPanelCommands>`: SelectInViewport, FocusInViewport, RevertToGenerated, DeleteElement, ToggleBaked, RebakeStale, Revalidate. |
| `Private/Setup/FHFSetupProbe.h/.cpp` | `Probe()` returns `FHFSetupStatus { bVenvReady, bMcpRunning, bToolsetRegistered, FString Detail[3] }`. `ProvisionPython()` runs `hf-drawings.ps1 -ProvisionOnly` via `FMonitoredProcess`; `StartMcp()` runs the two Exec commands `StartMcpServer()` already runs; `SetMcpAutoStart(bool)` writes `bAutoStartServer` into `EditorPerProjectUserSettings` under `[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]` via `GConfig`. |
| `Private/Bake/FHFBakeService.h/.cpp` | The only code that creates assets. `Bake(AHFElementActor*, FString& OutError)`, `BakeMany(TArrayView<AHFElementActor*>, FHFBakeReport&)`, `FindOrphans(UWorld*)`, `DeleteOrphans()`. Converts `FDynamicMesh3` â†’ `FMeshDescription` â†’ `UStaticMesh::BuildFromMeshDescriptions`, creates the package under the house's `BakedAssetFolder`, stamps `UHFBakedMeshUserData`, registers with the asset registry, saves. |
| `Private/HFMcpHeartbeat.h/.cpp` | Records `{FString ToolName; FDateTime At;}` â€” every `UHFToolset` static sets it on entry. ~30 lines, and it is the only evidence the artist gets that Claude is working rather than hung. |

### Runtime module additions â€” `.../Source/HouseForge/`

Necessary and unavoidable; the bake *state* must live on the actor, which is runtime.

| Path | What it does |
|---|---|
| `Public/Actors/HFBakeTypes.h` | `EHFRenderMode { Dynamic, Baked }`, `FHFBakedPart`, `UHFBakedMeshUserData : UAssetUserData`, `FHFBakeHooks` (the delegate the editor module binds). |
| `Private/Actors/HFBakeTypes.cpp` | Definition of the static hook. |

### Modified files

- `Public/Actors/HFElementActors.h/.cpp` â€” bake state, `SetRenderMode`, `MeshRevision`.
- `Public/Actors/HFHouseActor.h/.cpp` â€” `BakedAssetFolder`, and `BuildGeometry` preserving baked elements (Â§4.6).
- `Public/HFEditorSubsystem.h/.cpp` â€” new API (Â§4.7).
- `Private/Toolset/HFToolset.h/.cpp` â€” `SetRenderMode`, `GetBakeReport`, heartbeat calls.
- `Private/HouseForgeEditor.cpp` â€” tab spawner, layout extension, `Tools > HouseForge > HouseForge Panel`, bind `FHFBakeHooks`.
- `HouseForgeEditor.Build.cs` â€” add `MeshDescription`, `StaticMeshDescription`, `MeshConversion`, `AssetRegistry`, `EditorWidgets` (for `SDropTarget`).

**Tab registration** (in a `UToolMenus::RegisterStartupCallback` handler, not raw `StartupModule`):

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

Plus a `Tools > HouseForge > HouseForge Panel` entry invoking the same tab. Two doors, one tab â€” users who found "Import Interior Drawings" under Tools will look there.

---

## 4. Reversible bake â€” final implementation instructions

### 4.1 Framing

The user-visible control is **`Display: Live | Baked`**, never "Bake"/"Convert"/"Flatten". Reversibility is communicated by the control's grammar, so no confirm dialog and no tooltip are needed to make it safe. "Bake" appears only where it means *the static mesh asset that was produced*.

### 4.2 State on `AHFElementActor`

```cpp
// HFBakeTypes.h
UENUM(BlueprintType) enum class EHFRenderMode : uint8 { Dynamic, Baked };

USTRUCT() struct FHFBakedPart
{
    UPROPERTY(VisibleAnywhere) FName SourceComponentName;              // "Mesh" for part 0
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMesh> BakedMesh;      // hard ref: cooker + reference viewer must see it
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Component;
    UPROPERTY(VisibleAnywhere) FSoftObjectPath BakedAssetPath;         // survives a force-delete so we can name what went missing
    UPROPERTY(VisibleAnywhere) int32 BakedAtMeshRevision = INDEX_NONE;
};
```

On the actor:

```cpp
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="HouseForge|Bake") EHFRenderMode RenderMode = EHFRenderMode::Dynamic;
UPROPERTY(VisibleAnywhere, Category="HouseForge|Bake")                 TArray<FHFBakedPart> BakedParts;
UPROPERTY(EditAnywhere,    Category="HouseForge|Bake")                 bool bAutoRebakeOnRegenerate = true;
UPROPERTY(EditAnywhere,    Category="HouseForge|Bake")                 bool bUnbakeOnHandEdit = true;
UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category="HouseForge|Bake") int32 MeshRevision = 0;
UPROPERTY(NonTransactional, VisibleAnywhere, AdvancedDisplay, Category="HouseForge|Bake") FGuid BakeOwnerGuid;
UPROPERTY(Transient, VisibleAnywhere, Category="HouseForge|Bake")      bool bBakeAssetMissing = false;
```

`bArtistEdited` semantics are **unchanged and orthogonal**. All four combinations {edited, generated} Ã— {baked, dynamic} must round-trip through save/load.

`BakeOwnerGuid` is `NonTransactional` on purpose: undoing a bake must not revert the guid, or the redo would fail to recognise its own asset and mint a duplicate.

### 4.3 Component model

Part 0's `UStaticMeshComponent` is a constructor default subobject (`CreateDefaultSubobject<UStaticMeshComponent>("BakedMesh_0")`, `SetupAttachment(Mesh)`, created hidden and non-colliding). Parts >0 are lazily created by `EnsurePartComponent(int32)` using `NewObject` + `AddInstanceComponent` + `RegisterComponent`.

Today every element has exactly one source component, so `BakedParts` has one entry â€” but the **shape is per-part from day one** because rule 04 says a bake must not weld a chest of drawers into a block. Walls and beams would never expose the flaw, so a single-mesh design would ship and the joinery milestone would then require rewriting the bake model rather than extending it. The cost of getting the shape right now is one `TArray` instead of one pointer.

### 4.4 The switch â€” one function, nowhere else touches visibility

```cpp
void AHFElementActor::ApplyRenderMode(EHFRenderMode Mode)
{
    const bool bBaked = (Mode == EHFRenderMode::Baked) && HasAllBakedAssets();
    bBakeAssetMissing = (Mode == EHFRenderMode::Baked) && !bBaked;

    Mesh->SetVisibility(!bBaked);                 // bPropagateToChildren defaults false - must stay false
    Mesh->SetHiddenInGame(bBaked);
    Mesh->SetCollisionEnabled(bBaked ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
    Mesh->SetIsEditable(!bBaked);

    for (FHFBakedPart& Part : BakedParts)
    {
        if (!Part.Component) continue;
        Part.Component->SetVisibility(bBaked);
        Part.Component->SetHiddenInGame(!bBaked);
        Part.Component->SetCollisionEnabled(bBaked ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    }
    RenderMode = bBaked ? EHFRenderMode::Baked : EHFRenderMode::Dynamic;
}
```

Collision switches with visibility. Leaving both on double-traces every wall, and leaves a complex-as-simple dynamic collision under a mesh the user thinks is the only thing there.

**The `FDynamicMesh3` is never read-modify-written, never cleared, never rebuilt by baking.** Bake touches component state and creates an asset; that is the whole reason unbake is instant and lossless.

### 4.5 Staleness

`MeshRevision` increments in `CommitMesh()` (unconditionally, including under the `bGenerating` guard) and in `HandleMeshChanged()`. `IsBakeStale()` is `Part.BakedMesh && Part.BakedAtMeshRevision != MeshRevision`. No mesh hashing â€” hashing 63 dynamic meshes to rediscover a fact the actor already knows is pure cost. A counter rather than a bool because it yields "baked 3 edits ago" and survives save/load and undo interleaving.

### 4.6 Interaction rules

- **`PostEditChangeProperty`** must intercept `RenderMode` by `GET_MEMBER_NAME_CHECKED` and call `SetRenderMode()` *before* the existing catch-all falls through to `Regenerate()` â€” otherwise flipping the toggle regenerates the element.
- **Bake bakes the current mesh.** A hand-edited wall bakes its sculpted form; unbaking restores that same sculpted form. `Regenerate()` still refuses artist-edited elements, baked or not.
- **`bUnbakeOnHandEdit` (default true).** In `HandleMeshChanged`, if `RenderMode == Baked`, switch to Dynamic. Without this the artist sculpts an invisible mesh, sees nothing change, and undoes work that actually applied.
- **`bAutoRebakeOnRegenerate` (default true).** After `CommitMesh` on a baked element, request a rebake through `FHFBakeHooks`. Otherwise a parameter edit leaves the viewport showing stale geometry and `CaptureTopDown` screenshots a house that no longer matches the spec â€” the same silent false-pass `ApplySpecJson`'s validation gate exists to prevent.
- **`AHFHouseActor::BuildGeometry` must preserve baked elements too.** Today only `bArtistEdited` survives. Change the preservation predicate to `Typed->bArtistEdited || Typed->RenderMode == EHFRenderMode::Baked`, refresh the preserved actor's parameter struct from the spec, regenerate it if not artist-edited, then rebake if `bRebakePreservedElementsOnRebuild`. Without this, a rebuild destroys baked actors and orphans every asset.
- **Undo.** Wrap panel-driven bakes in `FScopedTransaction` and `Modify()` the actor and its components. Asset creation is not transactional; undoing a bake leaves the asset on disk, which is correct â€” re-baking then costs nothing.
- **Asset load failure.** `PostLoad` calls `ReconcileBakeState()`: if `RenderMode == Baked` and any part's mesh is null, fall back to Dynamic and set `bBakeAssetMissing`. Never render nothing.

### 4.7 Routing â€” bake goes through the subsystem

The panel must not call `AHFElementActor::SetRenderMode` directly. Add to `UHFEditorSubsystem`:

```cpp
FHFOperationResult SetElementRenderMode(const FString& Category, const FString& ElementId, bool bBaked);
FHFOperationResult SetHouseRenderMode(bool bBaked, int32& OutChanged);
FHFOperationResult RebakeStale(int32& OutRebaked);
FHFOperationResult GetBakeReport(FString& OutReport) const;      // counts, stale ids, missing ids, folder size
FHFOperationResult LoadSpecFromFile(const FString& FileName, FString& OutSpecJson);  // finally calls FHFSpecSerializer::LoadFromFile
FHFOperationResult GetValidationReportForLevel(FHFValidationResult& OutResult) const;
FHFSetupStatus     GetSetupStatus() const;
```

and wrap `SetRenderMode` / `GetBakeReport` in `UHFToolset`. As designed by the winning angle, the panel's flagship feature would have been the one operation Claude could not perform, and the two surfaces would drift from day one.

`LoadSpecFromFile` is deliberately **not** exposed over MCP â€” a build-from-file tool is exactly the shortcut past drawing-reading that `HouseForge.Architecture.SampleIsNotOnTheBuildPath` guards. The panel's build list additionally excludes `Sample2BHK.json` with a visible footnote.

### 4.8 Asset production

`FHFBakeService` lives in the **editor** module because package creation and saving cannot be reached from the runtime module. The actor exposes only `AdoptBakedMesh(int32 PartIndex, UStaticMesh*, int32 AtRevision)` and `SetRenderMode()`. The actor's `CallInEditor` "Rebake Now" button and `bAutoRebakeOnRegenerate` reach the service through `FHFBakeHooks::BakeElement`, a static delegate declared in the runtime module and bound by `FHouseForgeEditorModule::StartupModule`. Unbound (cooked/runtime) it simply does nothing.

Assets go to `AHFHouseActor::BakedAssetFolder`, defaulting to `/Game/HouseForge/Baked/<LevelName>`, resolved and written back on first bake so a later level rename does not scatter assets. Under `/Game`, never plugin content â€” rule 01: generated output is user output.

`UHFBakedMeshUserData` stamps `{OwnerGuid, ElementId, ElementClassName, LevelPackageName, SourceMeshRevision, BakedAtUtc}`. `LevelPackageName` is what makes orphan deletion safe: a scan can only see the open level, and without it the scan would delete assets belonging to an unopened one.

---

## 5. Build order

Each step ships something usable on its own.

1. **Tab shell + `FHFPanelState` + house summary + footer heartbeat.** One dockable panel that says what house is in the level, its element count, its declared units and its source drawing, plus Re-validate and Capture Top-Down. Already more than exists today, and it establishes the state/refresh plumbing everything else hangs off.
2. **Issues list.** 49 validator rule sites currently reachable only as a text blob. Click-to-select the offending actor. This is the single highest value-per-line item in the whole spec.
3. **Element tree + search + inspector strip.** Read-only plus Select / Focus / Revert / Delete (routed to the cascading `DeleteElement`, not actor deletion). Now the panel is the correction loop.
4. **Setup card + probes.** Fixes the venv discoverability hole and turns the "go tick a box in Editor Preferences" dialog into a checkbox.
5. **Bake runtime.** `HFBakeTypes.h`, actor state, `ApplyRenderMode`, `MeshRevision`, `BuildGeometry` preservation. Testable headlessly before any UI exists.
6. **`FHFBakeService`** + subsystem routing + toolset wrappers.
7. **Bake bar + DISPLAY column.** The marquee feature lands last because it is the only part that needs all of 5 and 6 working first.
8. **Get-started card.** Import with a working `SetName` (fixing `HouseForgeEditor.cpp:52`, which hardcodes `FString()` so every interactive import is named after the first file), drop target, copy-prompt, build-from-saved-spec.

Steps 1â€“4 are roughly a week part-time and are useful immediately. 5â€“7 are the second week. Step 8 is last because the drawings folder is already reachable from the Tools menu, badly. Honest total: three weeks part-time, not one.

---

## 6. What to defer, and why

| Deferred | Why |
|---|---|
| **Group by Room** | Fixtures and false ceilings carry `RoomId`; walls, openings, beams and columns do not, so room grouping needs geometric containment plus an "Unassigned" bucket. With ~63 elements the category tree is browsable. Revisit the moment fixture generators land and a real 2BHK produces 100+ fixture rows â€” that is when "the kitchen is wrong" beats "wall W34 is wrong". |
| **Materials / Lighting / Asset-replacement sections** | Not built. One greyed line per unbuilt feature is fake UI. The section registry is the reservation. |
| **Per-property editing in the panel** | The Details panel already does it better â€” `ShowOnlyInnerProperties`, `ClampMin`, `CallInEditor` are all in place. The panel gets a `[Details]` button, not a reimplementation. |
| **Details-panel customisation for bake** | `UPROPERTY(EditAnywhere, Category="HouseForge|Bake")` on `RenderMode` gives the dropdown, `CallInEditor` gives Rebake Now. A customisation buys ordering only. |
| **Multi-part bake components** | The data shape is per-part now; `EnsurePartComponent` for parts >0 is written when joinery lands and there is a second part to create. |
| **Fixture rows doing anything but focus** | Fixtures have no actor class. Clicking a fixture row must **not** silently flip `bShowPreview`/`bShowFixtures` on `AHFHouseActor` â€” that is a selection gesture mutating saved actor state and changing what is rendered without being asked. "Show spec wireframe" goes in the overflow menu as a visible toggle. |
| **Thumbnail rendering of drawing sheets** | Needs an image-wrapper decode path and a cache. The get-started card ships with filename rows; thumbnails are a later polish pass. |
| **Undo of orphan deletion** | Asset deletion is not transactional. Deleting orphans always shows the list first and requires confirmation. |

---

## 7. Automation tests

All under `HouseForge.*` so the existing gate catches them. Runtime bake tests in `Source/HouseForge/Private/Tests/HFBakeTests.cpp`; the rest in `Source/HouseForgeEditor/Private/Tests/`.

**`HouseForge.Bake.*`** (`HFBakeTests.cpp`, runtime â€” no service, pure state)
- `DynamicMeshSurvivesBake` â€” bake, assert the `UDynamicMeshComponent`'s triangle count and vertex positions are byte-identical afterwards. The central claim of the whole feature.
- `UnbakeRestoresLiveMesh` â€” bake â†’ unbake â†’ mesh identical, dynamic visible, baked hidden.
- `BakeDoesNotSetArtistEdited` â€” `bGenerating`/hook guard holds; `bArtistEdited` still false after a bake.
- `ArtistEditedBakesSculptedForm` â€” hand-edit, bake, unbake; the edit is still there and `bArtistEdited` is still true.
- `HandEditWhileBakedUnbakes` â€” `bUnbakeOnHandEdit` fires and the artist sees their edit.
- `CollisionFollowsVisibility` â€” exactly one of the two components has collision enabled in each mode.
- `MeshRevisionMarksBakeStale` â€” regenerate after bake â‡’ `IsBakeStale()`.
- `MissingAssetFallsBackToDynamic` â€” null the baked mesh, `ReconcileBakeState()`, assert Dynamic + `bBakeAssetMissing`.
- `AllFourStateCombinationsRoundTrip` â€” {edited, generated} Ã— {baked, dynamic} through serialise/deserialise.

**`HouseForge.Editor.Bake.*`**
- `HouseRebuildPreservesBakedElements` â€” extends the existing `HFHouseRebuildPreservesEditsTest`: a baked, non-artist-edited element survives `BuildGeometry`, keeps its asset, and is not orphaned.
- `BakeCreatesStampedAsset` â€” asset exists at the expected path with `UHFBakedMeshUserData` carrying the right `ElementId` and `LevelPackageName`.
- `RebakeReusesSameAsset` â€” no duplicate package, `BakeOwnerGuid` unchanged.
- `OrphanScanIgnoresOtherLevels` â€” an asset stamped with a different `LevelPackageName` is never reported as an orphan.

**`HouseForge.Editor.Api.*`**
- `SetElementRenderModeRoutesThroughSubsystem` â€” the subsystem method exists, is `BlueprintCallable`, and `UHFToolset` has a matching wrapper. Guards against the panel growing a private path.
- `LoadSpecFromFileRoundTrips` â€” save then load then compare, finally exercising `FHFSpecSerializer::LoadFromFile`.
- `LoadSpecFromFileIsNotAnMcpTool` â€” reflection assert that `UHFToolset` has no `AICallable` function that builds from a file path. Extends the existing `SampleIsNotOnTheBuildPath` guard.

**`HouseForge.Editor.Panel.*`** (headless-safe; no `SLATE_TEST` harness needed)
- `PanelStateDerivesRowsFromSpec` â€” build the sample house in a temp world, construct `FHFPanelState`, assert row counts per category and that summaries match the spec.
- `PanelStateFlagsIssuesOnRows` â€” an element with a validation error carries `WorstIssue == Error`.
- `SetupProbeReportsMissingVenv` â€” probe against a temp path with no `.venv` returns `bVenvReady == false` with actionable detail text.
- `TabSpawnerIsRegistered` â€” `FGlobalTabmanager::Get()->HasTabSpawner(HFPanelTabIds::HouseForgePanel)`.

Widget rendering is not tested. Slate render tests are expensive to write and brittle; the value is in `FHFPanelState`, which is deliberately a plain non-widget class so it can be tested without a tab ever being spawned.

---

## 8. Trade-offs stated plainly

- **No workflow rail.** The rail communicated one-time progress well and permanent activity badly, and it cost a third of the panel's height forever. Section presence carries the same information for free. The loss is real: a first-time user no longer sees the whole workflow at a glance. The get-started card's numbered 1/2/3 layout is the compensation, and it disappears once it is no longer true.
- **Sections stack vertically.** This will get long. It is the right shape for two heavy sections and the wrong shape for five. The registry makes the eventual conversion cheap; it does not make it free.
- **Bake asset creation lives in the editor module, reached from the runtime actor through a delegate.** That indirection is ugly. The alternative â€” runtime module depending on `UnrealEd` â€” is worse.
- **Per-part bake data with one part.** Pure future-proofing cost today, paid because the milestone that exposes the flaw is the one that would make it expensive to fix.
- **`bAutoRebakeOnRegenerate` defaults on.** It makes parameter edits on baked elements slower. Off, the viewport lies about what the spec says, and `CaptureTopDown` â€” the tool Claude uses to check its own work â€” screenshots the lie.

---

## How much to trust this

The scoring phase was meant to judge three competing panel designs. Only design A reached
the judges - the design array was truncated mid-object, so B and C were never delivered.
All three judges said so unprompted. What follows is therefore a critique-and-amend of a
single candidate, not the result of a contest. The amendments are the valuable part; the
word "winner" is not.

### Lens: workflow-clarity

A - Workflow spine (state-derived rail + permanent Workbench). Build it, with the eight amendments in mustGraft. Caveat: it is also the only design that arrived intact - the prompt's design array is truncated mid-object inside design A's deliberateOmissions field, so designs 2 and 3 were never delivered and no comparative judgement was possible. If those two exist, re-run this scoring before committing; the recommendation below is a critique-and-amend of a single design, not a contest result.

### Lens: Scalability â€” will the panel survive ceilings, joinery, fixtures, materials, lighting and asset replacement without being rebuilt? Judged against the real source, not the design's claims about it. Secondary weight on cold-read clarity, capability coverage, bake-toggle safety, and one-developer Slate cost.

A â€” by default, not by contest. Designs B and C never arrived in my input (see warnings), so this is a single-candidate review. On its own merits A is worth building: the state-derived rail, the Details-panel delegation, the single subsystem code path and the 'Display: Live | Baked' framing are all correct calls that will still be correct after materials and lighting land. It must not be built as specified, though â€” the per-actor bake model and the fixed five-node spine are the two places it will need tearing up, and both are cheap to fix now and expensive to fix later.

### Lens: implementation-cost

A - Workflow spine (build it, but phased: ship the Workbench first and defer the rail)

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
