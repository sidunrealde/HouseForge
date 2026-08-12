# Lumen and the bake

Why baking stopped being an optimisation and became a prerequisite for every lit render, what
HouseForge does about it, and why a freshly generated house is nevertheless **not** baked.

Three code sites point here — `EHFLumenGuard`, `EHFBakeOnBuild` and
`UHFEditorSubsystem::ApplySpecJson` — so this is the decision record, not a summary of one.

---

## 1. The finding

**Lumen cannot see a `UDynamicMeshComponent`, and the resulting render is BRIGHTER than the
correct one.**

That second clause is the whole reason any of this is mechanical rather than left to judgement. A
broken render does not look broken. It looks bright, airy and cheerful, because the sky light that
should have been stopped by the walls is not stopped by anything.

Measured on the reference 2BHK, identical lighting, identical manual exposure, mean linearised
Rec.709 luminance (`Saved/Review/lumen/measurements.json`):

| Configuration | Whole frame | Ceiling | Wall | Blue/red |
|---|---|---|---|---|
| **A** live meshes, software tracing | **0.260** | 0.167 | 0.348 | **1.147** |
| **B** live meshes, hardware tracing (the shipped default) | 0.030 | 0.026 | 0.064 | 0.645 |
| **B2** live meshes, hardware + hit lighting | 0.048 | 0.057 | 0.085 | 0.551 |
| **C** baked, software tracing | 0.039 | 0.051 | 0.055 | 0.291 |
| **D** baked, hardware tracing | 0.083 | 0.116 | 0.135 | 0.615 |

**Read the brightness backwards.** A is three times brighter than the only fully correct
configuration and it is the broken one. It is also the only configuration whose blue/red ratio is
above 1.0 — it is lit by sky rather than by the flat's own warm fixtures, because to Lumen the flat
has no walls. That colour ratio is the cleanest single discriminator in the data set and it does not
depend on exposure at all.

## 2. The mechanism, at the line level

Four things must be true before a surface puts light into a room. A plain `UStaticMeshComponent`
gets all four for free; a dynamic mesh gets at most one.

1. **In the Lumen scene.** `FPrimitiveSceneProxy::UpdateVisibleInLumenScene`
   (`PrimitiveSceneProxy.cpp:1690-1733`) sets `bVisibleInLumenScene = AffectsDynamicIndirectLighting()
   && bCanBeTraced`, and `bCanBeTraced` branches on the tracing path — software wants a distance
   field representation, hardware wants a ray tracing representation.
2. **A distance field on the asset**, because the mesh card build is *chained off* it —
   `FDistanceFieldVolumeData::CacheDerivedData` calls `BeginCacheMeshCardRepresentation` only once the
   distance field is available (`DistanceFieldAtlas.cpp:296`, `:1050`). No distance field means no
   cards, on **both** tracing paths. "We use hardware ray tracing so we do not need distance fields"
   is the most commonly mis-stated fact about Lumen and it is wrong.
3. **Mesh cards.** `Proxy->GetMeshCardRepresentation()`, which for a static mesh comes off
   `RenderData->LODResources[0].CardRepresentationData`.
4. **Those cards captured** into the surface cache, via `EMeshPass::LumenCardCapture`, which iterates
   `PrimitiveSceneInfo->StaticMeshRelevances` (`LumenSceneCardCapture.cpp:775-860`).

A `UDynamicMeshComponent` fails at (2) by construction: `FBaseDynamicMeshSceneProxy` hardcodes
`bSupportsDistanceFieldRepresentation` and `bAffectDistanceFieldLighting` to false
(`BaseDynamicMeshSceneProxy.cpp:65-68`), `ComputeDistanceFieldForMesh` returns an empty pointer, and
`SetNewDistanceField` is `ensureMsgf(false, "Distance fields not supported")`. The API was formally
deprecated in 5.6. So **software tracing cannot see the flat at all** — configuration A.

Under hardware tracing it does better and it is still not enough. The proxy implements
`GetDynamicRayTracingInstances` and submits real triangles as a BLAS, so rays hit the walls and
occlusion is correct; the proxy even allocates six bounding-box cards in `UpdateLumenCardsFromBounds`.
But card *capture* needs static mesh batches, and `FBaseDynamicMeshSceneProxy::DrawStaticElements`
returns immediately unless `bPreferStaticDrawPath` (`:557-562`). HouseForge never calls
`SetMeshDrawPath`, so every element is on the DynamicDraw path. **The cards are allocated and never
filled** — which is exactly what configuration B measures: geometry present, radiance 0.030.

### The contingency that was considered and rejected

`UBaseDynamicMeshComponent::SetMeshDrawPath(EDynamicMeshDrawPath::StaticDraw)` makes the proxy emit
static batches with `bUseForMaterial = true`, which is precisely what
`FLumenCardMeshProcessor::AddMeshBatch` accepts. That would give dynamic meshes a genuinely captured
surface cache under hardware tracing, with no bake at all.

It is not the plan, for two reasons. The cards come from `UpdateLumenCardsFromBounds` — the bounding
box only, with a `TODO` in the engine saying "Implement a better method to set up the lumen cards" —
so a room-sized wall projects its whole surface onto six box faces. And the software path stays
broken regardless, because distance fields are still absent. It is a twenty-minute contingency if
baking ever slips, not a substitute.

Hit lighting (`r.Lumen.HardwareRayTracing.LightingMode 1`, configuration B2) is likewise a
**one-bounce** fix, not an alternative: the engine's own description says "Lumen Surface Cache will
still be used for secondary bounces", and a dynamic mesh has no usable surface cache. In an interior
lit by fixtures, second and third bounce is most of the illumination — which is what the numbers show,
0.048 against 0.083 from identical lighting.

**Baking is the only configuration that is correct on both tracing paths and gives multi-bounce GI.**

## 2a. The proof, through the real bake

Section 1 was measured against a stand-in bake written before `FHFBakeService` existed. Re-measured
through the shipped path — `SetHouseRenderMode`, 160 elements, 410 parts, 410 packages written — on
the same flat, same views, same controls:

| | Whole frame | Shadowed wall | Blue/red | Frame with no cards |
|---|---|---|---|---|
| **A** live, software | **0.662** | 0.687 | 0.925 | — |
| **B** live, hardware (the default) | 0.047 | 0.033 | 0.445 | **99.8%** |
| **C** BAKED, software | 0.040 | 0.046 | 0.087 | 0.0% |
| **D** BAKED, hardware | 0.104 | **0.073** | 0.319 | 0.1% |

Three separate things, each measured rather than inferred:

- **In the scene.** `CheckLumenCoverage` on the same run: unbaked **0 of 410 primitives radiant,
  410 absent, 0.0%** of cardable surface area. Baked: **314 radiant, 0 absent, 100.0%**, with 96
  correctly excluded as too small to card.
- **Cards placed.** 99.8% of the live interior frame is the engine's own "missing Surface Cache
  coverage" pink, against 0.1% baked — a factor of about 1600, and the one figure here that owes
  nothing to exposure.
- **Light arrives.** The shadowed wall is **2.20× brighter baked than live** on the same tracing
  path. The stand-in measured 2.11×. That agreement is worth more than either number.

And the deception reproduces exactly: **A is 6.4× brighter than D**, and it is the configuration in
which Lumen cannot see a single wall.

`Saved/Review/lumen-baked/index.md` has the images, including the aerial `r.Lumen.Visualize 3` row
where the flat is absent under A, a **black silhouette** under B — traced, radiating nothing — and
whole and white under C and D.

## 3. What HouseForge does about it

### The guard — `FHFLumenCoverage`

Walks every HouseForge element and decides, per drawn primitive, **whether it will put light into the
room**. Note that this measures *radiance*, not *membership*: under hardware tracing a dynamic mesh
satisfies `UpdateVisibleInLumenScene` outright, so a guard that only asked "is it in the Lumen scene"
would happily pass configuration B — the one measured at 0.030 against 0.083.

Verdicts, in order:

| Verdict | Meaning |
|---|---|
| `Radiant` | Static mesh, distance field intact, orthogonal transform, face over the card threshold. What a bake produces. |
| `TooSmallForCards` | Largest face under a 10 cm square. **Never counted as a fault** — see below. |
| `LiveDynamicMesh` | Not baked. The verdict this milestone exists for. |
| `NoDistanceField` | Asset built with `DistanceFieldResolutionScale` at zero, which kills the cards too. |
| `IndirectLightingOff` | `bAffectDynamicIndirectLighting` or `bAffectDistanceFieldLighting` cleared. |
| `NonOrthogonalTransform` | Sheared or zero-scaled — `AddMeshCardsFromBuildData` refuses to card it. |

It answers **without a renderer**, deliberately. The validation gate runs the whole suite under
`-nullrhi`, where there is no `FScene`, no `FLumenSceneData` and no distance field scene to count; a
guard that only worked while a frame was being drawn would be untested in the one run that gates a
merge, and the failure it exists to catch is the one nobody sees. Everything it reads — component
lighting flags and transform, the asset's build settings, the project cvars — is game-thread data the
renderer derives its own answer from, so the two agree by construction. `IsMatrixOrthogonal` is copied
from `LumenMeshCards.cpp` rather than approximated, so a marginal transform cannot land on two
different sides of the question.

It also checks the **project**, because two settings defeat a perfectly good bake and produce a render
indistinguishable from not having baked at all: `r.GenerateMeshDistanceFields` off, and
`r.MeshCardRepresentation` off.

#### Why `TooSmallForCards` is a verdict of its own

`FLumenSceneData::AddMeshCardsFromBuildData` requires `LargestFaceArea > MinFaceSurfaceArea`
(`LumenMeshCards.cpp:1004-1007`), the threshold being
`r.LumenScene.SurfaceCache.MeshCardsMinSize` squared — 10 cm × 10 cm by default. Measured on the
reference flat, **96 of 410 drawn primitives** are below it: handles, hinge knuckles, tap spouts.
Baking them harder does not help; the engine will not card them at any size of asset. A guard that
failed on every door handle in the building would be switched off within a day, so these are counted
and never held against the flat, and they sit in neither term of the coverage fraction — otherwise
there would be no value of that fraction that meant "correct".

### The refusal — wired into the capture path

`FHFSceneCapture::EnsureLumenCoverage` sits beside `EnsureMaterialsReady`, and for a stronger reason
than its neighbour. An uncompiled material draws checkerboard and `-nullrhi` draws black; both are
obvious the moment anyone looks. This failure draws a brighter, more attractive version of the wrong
answer, so there is nothing to notice and the check has to be mechanical.

`EHFLumenGuard` has three values. **`Refuse` is the default** and no pixel is written. `Warn` logs the
entire refusal at Warning and renders anyway — that is not an escape hatch, it says everything
`Refuse` would have said, and it exists because measuring the broken configuration requires being
allowed to render it. `Off` is correct for a plan: an orthographic section with the sky off has no
indirect light in it to get wrong.

On success the summary is logged **on the way past**, not only on failure, so a review package carries
the evidence that the flat was in the Lumen scene when the picture was taken. A number in the log
beside the image is the difference between "this render is trustworthy" and "this render was probably
fine".

---

## 4. The decision: a freshly generated house is **not** baked

`EHFBakeOnBuild::Never` is the default, and it was chosen against the obvious pull of "baking is now
required for a correct render, so bake".

### The counter-argument, stated properly

It is a good argument and it nearly won. Baking is now a correctness requirement rather than a
performance option. A user who generates a house and renders it gets a wrong picture unless something
bakes first. Every hour spent explaining that to somebody is an hour the default could have saved,
and defaults that require a follow-up step get skipped.

### Why it loses

1. **Generation is iterative.** The reference flat is ~160 elements and ~350 parts, and each part is
   one static mesh asset built, compiled and written to the project's Content folder. The usual next
   action after a build is another build — a corrected wall, a moved fixture, a different sofa design.
   Baking every one of those is a large cost paid mostly for output that is about to be discarded.

2. **Baking changes what the Modeling Tools edit.** Measured in
   `HouseForge.Bake.Probe.ToolTargetSelection`: with a static mesh present, `UToolTargetManager` hands
   a tool the **baked asset**, in every configuration where that component holds one — including
   unregistered, which is what the design originally proposed as the mitigation and which does not
   work. The fix in place is to clear the asset (`SetStaticMesh(nullptr)`) while the element is
   Dynamic, plus `SetIsEditable(false)` on the dynamic side while it is Baked. It holds. But a plugin
   whose stated premise is "generated geometry stays artist-editable" should not put every element in
   the building into that state without being asked.

3. **Assets are user output.** `.claude/rules/01-scope.md` is explicit that generated output belongs to
   the project rather than the plugin. Creating 350 assets in somebody else's `Content` folder as a
   silent side effect of "apply this spec" is a surprising write.

4. **The reason to auto-bake anyway no longer holds** — and this is the one that makes the other three
   safe. The argument for baking by default was that an unbaked render is *silently* wrong. It is no
   longer silent: the capture path refuses it, names the elements, and says what to do. Once a failure
   is loud, the default can be the one that is right for editing.

### How the choice is made visible rather than implicit

The rule is that the state is never inferred and never assumed. Four places say it:

- **The build result itself.** `ApplySpecJson` appends the render mode to the message it returns — the
  one text a caller is guaranteed to read is the result of the operation they just requested. Baked, it
  reports the bake and the resulting coverage; unbaked, it says so and says to bake before rendering.
- **The capture refuses**, with the elements named and the remedy given.
- **`CheckLumenCoverage`** exists as a tool in its own right, so the question can be asked at any time
  without rendering anything.
- **`Project Settings > Plugins > HouseForge Rendering`** holds both controls, filed alongside
  `UHFSettings` rather than in Editor Preferences — a render policy one artist has and another does not
  produces two different-looking renders of the same flat and no way to tell which is the honest one.

`EHFBakeOnBuild::Always` is there for the case the default is wrong for: an unattended pipeline whose
output is renders rather than edits, where paying the bake once at the end of a build is cheaper than
discovering a refusal at capture time.

---

## 5. Evidence

`Saved/*` is gitignored, so everything in the two directories below is a local build artefact. If it
is not there, regenerate it with `Scripts/hf-lumen.ps1` rather than looking for it in history.

- `Saved/Review/lumen/` — the original five-configuration comparison, with `index.md` naming each
  image. Its bake was a stand-in written before the real one existed.
- `Saved/Review/lumen-baked/` — the same views re-rendered through the **real** bake service, with
  captioned `COMPARISON__*.png` contact sheets and `measurements.json`. See `index.md` there, which
  also records the two defects in the instrument that were found and fixed in the course of taking
  the measurement — both of which produced output that looked like data.
- `Scripts/hf-lumen.ps1` and `Scripts/hf_lumen.py` — the instrument. It drives a real editor viewport
  under `-RenderOffScreen` and settles ~200 frames per state change, because `FHFSceneCapture` cannot
  answer this question: a one-shot `USceneCaptureComponent2D` runs no converged GI and compensates with
  an ambient cubemap, so measuring indirect light through it measures the cubemap. That cubemap is
  zeroed before every measurement, and the zeroing is reported rather than assumed — left in, all
  configurations come back nearly identical and the experiment "proves" Lumen works no matter what.
- `HouseForge.Lumen.*` — eight automation tests over the mechanism, run by the gate under `-nullrhi`.
