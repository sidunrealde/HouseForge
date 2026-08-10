// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/HFAssetOverrideTypes.h"
#include "Actors/HFBakeTypes.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "GameFramework/Actor.h"
#include "Geometry/HFRenderFinish.h"
#include "Model/HFSkirtingPlan.h"
#include "Model/HFTypes.h"
#include "HFElementActors.generated.h"

class UDynamicMeshComponent;
class ULightComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Lifts a dynamic mesh component's editable flag for the duration of OUR OWN write, and puts it back.
 *
 * WITHOUT THIS A BAKED ELEMENT CANNOT REGENERATE AT ALL, and it fails silently.
 * AHFElementActor::ApplyRenderMode marks the dynamic component non-editable in Baked mode, which is
 * doing real work - UDynamicMeshComponentToolTargetFactory::CanBuildTarget tests IsEditable()
 * explicitly, so it is what drops the live mesh out of the Modeling Tools' candidate list and gets
 * the count down to the one candidate a single-selection tool requires. But
 * UDynamicMeshComponent::SetMesh checks the same flag (DynamicMeshComponent.cpp:186) and refuses
 * with an ensure, so generation - which goes through SetMesh - is refused too.
 *
 * The symptom is nasty: MeshRevision still bumps, the re-bake still runs, and the element comes out
 * "baked and current" holding the PREVIOUS plan's geometry. The viewport, and any capture taken of
 * it, then quietly disagree with the spec. Found by
 * HouseForge.Bake.RegenerateWhileBakedRebakes, not by reading.
 *
 * The same shape as AHFElementActor::bGenerating: a narrow, scoped statement that this particular
 * write is the plugin's own.
 */
struct HOUSEFORGE_API FHFEditableWriteScope
{
	explicit FHFEditableWriteScope(UDynamicMeshComponent* InComponent);
	~FHFEditableWriteScope();

	FHFEditableWriteScope(const FHFEditableWriteScope&) = delete;
	FHFEditableWriteScope& operator=(const FHFEditableWriteScope&) = delete;

private:
	UDynamicMeshComponent* Component = nullptr;
	bool bWasEditable = true;
};

/**
 * Base for every generated element.
 *
 * Each element actor owns its own parameter struct and regenerates its mesh when that struct
 * changes. The house spec is the import and export format, not a live second source of truth -
 * see .claude/rules/04-conventions.md. That is what makes the level directly editable: change a
 * wall's thickness in the details panel and only that wall rebuilds.
 */
UCLASS(Abstract, HideCategories = (Replication, Networking, Input, HLOD))
class HOUSEFORGE_API AHFElementActor : public AActor
{
	GENERATED_BODY()

public:
	AHFElementActor();

	/**
	 * Rebuilds this element's mesh from its current parameters.
	 *
	 * Does nothing once the mesh has been edited by hand, unless forced. Regenerating over an
	 * artist's work would silently destroy it, and it is the sort of loss that is only noticed
	 * long afterwards.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "HouseForge")
	virtual void Regenerate();

	/** Throws away hand edits and rebuilds from the parameters. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "HouseForge")
	virtual void RevertToGenerated();

	/**
	 * True when this element carries work a house-level rebuild must not throw away.
	 *
	 * The house rebuild preserves an actor rather than destroying and respawning it when this
	 * returns true. Virtual because an element can hold hand-edited work somewhere other than its
	 * own root mesh - an articulated element carries a flag per moving part, and respawning the
	 * actor would destroy those just as surely.
	 */
	virtual bool ShouldPreserveOnRebuild() const { return bArtistEdited; }

	/**
	 * True once the mesh has been modified outside of generation - by the Modeling Tools, or any
	 * other editor that touches the dynamic mesh.
	 *
	 * These are UDynamicMeshComponents precisely so an artist can sculpt, cut and detail them
	 * with Unreal's modelling tools after generation. This flag is what makes that safe: an
	 * edited element opts out of regeneration and keeps whatever was modelled.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	bool bArtistEdited = false;

	/** Spec element this actor was generated from, so a rebuild can match it back up. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	FName ElementId;

	/**
	 * Which kind of fixture this actor was built for, or Unknown for a wall, room, beam or column.
	 *
	 * STAMPED AT SPAWN, and it has to be, because the actor class is not the answer. Five fixture
	 * types - a kitchen base unit, a TV console, a bedside unit, a shoe rack and a vanity - all
	 * become AHFCasedGoodsActor, which is the whole return on the cased goods kit and is exactly
	 * what makes the class useless as a key. A mapping table that put a shoe rack where every TV
	 * console should be would be applying itself correctly to the wrong things.
	 *
	 * Read by the asset replacement pass and by the panel's grouping, and by nothing that generates
	 * geometry: what this element IS lives in its parameter struct, and this is only the label the
	 * drawing gave it.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	EHFFixtureType SourceFixtureType = EHFFixtureType::Unknown;

	/**
	 * What is done to this element's geometry on the way to the component: chamfers, UVs, lightmap.
	 *
	 * Lives on the actor rather than in the generators because a generator is a pure function of its
	 * parameters and the bevel is not idempotent - see FHFRenderFinish. It is editable per element
	 * because the cost is real: chamfering an arris is triangles, and the answer for a wall the
	 * camera walks past is not the answer for a service duct nobody ever sees.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Render")
	FHFRenderFinish RenderFinish;

	/**
	 * Volumes this element's geometry was built around, so the chamfer knows where the plaster runs on.
	 *
	 * NOT A RENDER SETTING, which is why it is here and not on FHFRenderFinish: it is a fact about
	 * where this element sits in the building, and the settings page re-seeds RenderFinish wholesale.
	 * A wall butting into another wall ends in that wall's face and the surface continues straight
	 * through; chamfered anyway, the junction is scored with a 3 mm V-notch. See
	 * FHFMeshOps::BevelConvexEdges.
	 *
	 * Empty is the honest answer for an element standing on its own, and then every convex arris is
	 * a real one.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge|Render")
	TArray<FHFStructuralCut> FlushVolumes;

	UDynamicMeshComponent* GetMeshComponent() const { return Mesh; }

	// ================================================================================== the bake
	//
	// A SWITCH, NEVER A REPLACEMENT. Everything below exists to make one sentence true: "Dynamic
	// meshes are kept. Switching back restores them exactly." Nothing here reads, writes, clears or
	// rebuilds the FDynamicMesh3 - which is the entire reason unbake is instant and lossless, and
	// the reason there is no confirmation dialog anywhere near it.

	/**
	 * Which of this element's two representations is currently drawing.
	 *
	 * Editable in the details panel, and PostEditChangeProperty routes it to SetRenderMode instead of
	 * letting the catch-all regenerate the element - flipping a render switch must not rebuild
	 * geometry.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HouseForge|Bake")
	EHFRenderMode RenderMode = EHFRenderMode::Dynamic;

	/** One per source component, index-parallel to GetBakeSourceComponents(). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge|Bake")
	TArray<FHFBakedPart> BakedParts;

	/**
	 * Re-bake automatically when the geometry changes underneath a baked element.
	 *
	 * On by default. Off, a parameter edit leaves the viewport drawing the OLD baked geometry while
	 * the spec says something else, and CaptureTopDown - the tool Claude uses to check its own work -
	 * photographs the lie. That is the same silent false pass the validation gate exists to prevent.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Bake")
	bool bAutoRebakeOnRegenerate = true;

	/**
	 * Switch back to Dynamic the moment somebody hand-edits the mesh of a baked element.
	 *
	 * On by default, and it is a usability guard rather than a safety one. The safety comes from
	 * ApplyRenderMode clearing the baked component's UStaticMesh while Dynamic; this is what stops an
	 * artist sculpting a mesh nobody can see, watching nothing change, and undoing work that in fact
	 * applied perfectly.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Bake")
	bool bUnbakeOnHandEdit = true;

	/**
	 * Bumped every time this element's geometry changes, by generation or by hand.
	 *
	 * A counter rather than a bool: it survives save/load and undo interleaving, and it can say
	 * "baked three edits ago". One per ACTOR rather than one per part - editing a door leaf marks the
	 * frame stale too, which over-bakes slightly and buys a staleness rule that is one sentence long.
	 * No mesh hashing: hashing sixty meshes to rediscover something the actor already knows is pure
	 * cost.
	 */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = "HouseForge|Bake")
	int32 MeshRevision = 0;

	/**
	 * Identity of this element as the owner of its baked assets.
	 *
	 * NonTransactional deliberately. Undoing a bake must not revert the guid, or the redo fails to
	 * recognise the asset it just made and mints a duplicate beside it.
	 */
	UPROPERTY(NonTransactional, VisibleAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = "HouseForge|Bake")
	FGuid BakeOwnerGuid;

	/**
	 * Set when this element wanted to be Baked and its asset was not there.
	 *
	 * Transient because it is a fact about this session, not about the level. NEVER RENDER NOTHING is
	 * the rule it serves: a missing asset falls back to Dynamic and says so, rather than leaving a
	 * hole in the flat where a wall used to be.
	 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge|Bake")
	bool bBakeAssetMissing = false;

	/**
	 * Every dynamic mesh component of this element, in bake order. The root first.
	 *
	 * The one place the bake asks what an element is made of, so an articulated fixture answers with
	 * its moving parts and a wall answers with itself. Overridden rather than inspected by the bake
	 * service, because only the actor knows which of its components are geometry.
	 */
	virtual void GetBakeSourceComponents(TArray<UDynamicMeshComponent*>& OutComponents) const;

	/**
	 * Switches which representation draws. The whole of the user-facing bake, minus asset creation.
	 *
	 * Baking first if needed is NOT done here: a switch to Baked with no assets falls back to Dynamic
	 * and sets bBakeAssetMissing. Asking for a bake is FHFBakeService's job, reached through
	 * FHFBakeHooks.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Bake")
	void SetRenderMode(EHFRenderMode Mode);

	/** Every part has an asset to draw. False on an element that has never been baked. */
	UFUNCTION(BlueprintPure, Category = "HouseForge|Bake")
	bool HasAllBakedAssets() const;

	/** Any baked asset was made from geometry older than what the dynamic mesh holds now. */
	UFUNCTION(BlueprintPure, Category = "HouseForge|Bake")
	bool IsBakeStale() const;

	/** True when at least one part carries an asset, whichever mode is showing. */
	UFUNCTION(BlueprintPure, Category = "HouseForge|Bake")
	bool HasAnyBakedAsset() const;

	/**
	 * Takes ownership of a freshly created asset for one source component.
	 *
	 * The only door the editor's bake service has into this actor. It creates the component if there
	 * is not one yet, points it at the asset, and records the revision the asset was made from.
	 *
	 * @param PartIndex           index into GetBakeSourceComponents()
	 * @param SourceComponentName NAME_None for the root mesh
	 * @param InBakedMesh         the asset, or null to drop this part's bake
	 * @param AtRevision          MeshRevision the asset was built from
	 * @param bSourceWasEmpty     the source held no triangles, so producing nothing was correct
	 * @param ContentHash         fingerprint of the asset as written; see FHFBakedPart::BakedContentHash
	 */
	void AdoptBakedMesh(int32 PartIndex, FName SourceComponentName, UStaticMesh* InBakedMesh, int32 AtRevision,
		bool bSourceWasEmpty = false, int64 ContentHash = 0);

	/** True when any part's baked asset has been written to since this element baked it. */
	UFUNCTION(BlueprintPure, Category = "HouseForge|Bake")
	bool HasHandEditedBakedAsset() const;

	/**
	 * THE WAY BACK FROM A SCULPT THAT LANDED IN A BAKED ASSET.
	 *
	 * In Baked mode the Modeling Tools target the baked UStaticMesh - measured, and correct for the
	 * engine to do, because the live mesh is deliberately not editable while it is invisible. So an
	 * artist's work can end up in the asset rather than in the FDynamicMesh3, where nothing regenerates
	 * it and the next re-bake used to flatten it. FHFBakeService now refuses that overwrite; this is
	 * how the work gets home.
	 *
	 * Takes the asset's geometry into the source component AS A HAND EDIT: bArtistEdited is set, the
	 * element goes back to Dynamic, and it stops regenerating - which is exactly what would have
	 * happened had the sculpt landed on the live mesh in the first place. Re-baking afterwards is then
	 * an ordinary bake of the sculpted form.
	 *
	 * @param PartIndex index into GetBakeSourceComponents()
	 * @param NewMesh   the asset's geometry, already converted
	 */
	void AdoptHandEditedMesh(int32 PartIndex, UE::Geometry::FDynamicMesh3&& NewMesh);

	/**
	 * Makes BakedParts match the current source components, destroying components for parts that no
	 * longer exist.
	 *
	 * MATCHES, rather than truncating to length. A part can disappear from the MIDDLE of a fixture -
	 * narrowing a wardrobe by one bay drops a body leaf while every loft leaf above it stays - and
	 * trimming the tail instead would leave every part after it holding the previous part's asset.
	 * Baked components are matched to their sources by attachment first and by recorded name second,
	 * and the attachment of a survivor is repaired on the way past.
	 *
	 * A wardrobe that loses a drawer loses that drawer's baked component here; the ASSET is left on
	 * disk and becomes an orphan, which is a thing the orphan scan can offer to delete with the user
	 * looking at it. Deleting assets silently from a regeneration path is not something this plugin
	 * does.
	 *
	 * @param OutOrphaned paths of assets whose part went away
	 * @return how many parts were dropped
	 */
	int32 SyncBakedPartsToSources(TArray<FSoftObjectPath>* OutOrphaned = nullptr);

	/**
	 * Settles bake state against what actually exists, and never leaves the element invisible.
	 *
	 * Called from PostLoad, so a level whose baked assets were deleted, moved or force-deleted while
	 * it was closed comes back drawing its dynamic mesh with bBakeAssetMissing set, rather than
	 * coming back as a hole in the flat.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Bake")
	void ReconcileBakeState();

	/** Ensures BakeOwnerGuid is set. Idempotent. */
	const FGuid& EnsureBakeOwnerGuid();

	/**
	 * If this element is baked, stale and set to auto-rebake, asks the bake service to redo it.
	 *
	 * Called at the end of every generation path rather than from CommitMesh, so one regeneration of
	 * an articulated fixture costs one bake of the whole fixture and not one per part.
	 */
	void FlushPendingRebake();

	// ============================================================================ the asset override
	//
	// A SECOND SWITCH, ON THE SAME PRINCIPLE AS THE BAKE. .claude/rules/04-conventions.md: "Replacing
	// a procedural fixture with a Content Browser asset never discards its parameter struct. Clearing
	// the override must restore the generated mesh exactly." Nothing below reads, writes, clears or
	// rebuilds the FDynamicMesh3, and nothing below calls Regenerate, CommitMesh or RevertToGenerated.
	// The generated mesh comes back exactly because it never went anywhere.

	/** The Content Browser asset standing in for this element, if any. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Asset",
		meta = (ShowOnlyInnerProperties))
	FHFAssetOverride AssetOverride;

	/**
	 * What the last fit worked out. Transient - a session fact, recomputed on every apply.
	 *
	 * Held so the panel can report `stretched 1.34x, 12 cm of slack in depth` on a row without
	 * re-solving, and so the numbers a user saw on the preview are demonstrably the numbers that
	 * were applied rather than a second computation that agrees by inspection.
	 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge|Asset")
	FHFAssetFitResult LastAssetFit;

	/** True when an asset is currently standing in for this element's generated geometry. */
	UFUNCTION(BlueprintPure, Category = "HouseForge|Asset")
	bool HasAssetOverride() const;

	/** True when the override was placed by a mapping table rather than chosen for this instance. */
	UFUNCTION(BlueprintPure, Category = "HouseForge|Asset")
	bool HasTableAssetOverride() const;

	/**
	 * Puts a Content Browser asset in front of the generated geometry.
	 *
	 * Loads the soft pointer, fits the asset into the box the GENERATED MESH occupies, and switches
	 * which components draw. Returns the fit so the caller can report what it cost; an unset override
	 * is the same call as ClearAssetOverride.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Asset")
	FHFAssetFitResult SetAssetOverride(const FHFAssetOverride& InOverride);

	/**
	 * THE WAY BACK. Restores the generated mesh exactly.
	 *
	 * Clears the component's asset rather than merely hiding it - see the tool-target measurement in
	 * ApplyRenderMode, which applies here for the same reason and with the same consequence.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Asset")
	void ClearAssetOverride();

	/**
	 * Works out what an override WOULD do, changing nothing.
	 *
	 * What the panel's preview calls, so the mismatch between a vendor's model and the drawing is
	 * something the user sees before it lands rather than after.
	 */
	FHFAssetFitResult PreviewAssetFit(const FHFAssetOverride& InOverride) const;

	/**
	 * The box this element's generated geometry occupies, in the ACTOR'S OWN LOCAL SPACE.
	 *
	 * The target an override is fitted into, and the reason the fit does not have to know which of
	 * FHFFixturePlacement's five datums put this actor where it is: whatever datum that was, the
	 * generated geometry is already in the right place relative to the actor, so this box is right by
	 * construction. Taken over the same components the bake takes - GetBakeSourceComponents - so a
	 * wardrobe's box includes its shutters and a door's includes its leaf.
	 *
	 * An articulated element held OPEN measures wider than one held shut, because the box is of what
	 * is there rather than of what was drawn. Elements are generated shut, so this only bites on a
	 * fixture the user has posed open and then overridden, and the preview shows the number.
	 */
	UFUNCTION(BlueprintPure, Category = "HouseForge|Asset")
	FBox GetGeneratedLocalBounds() const;

	/** The component drawing the override asset, or null. */
	UStaticMeshComponent* GetAssetOverrideComponent() const { return AssetOverrideComponent; }

	virtual void PostInitializeComponents() override;
	virtual void PostLoad() override;

	/**
	 * Arms hand-edit detection on an element that came back from a saved level.
	 *
	 * PostInitializeComponents never runs in an editor world, so without this the protection
	 * bArtistEdited exists to give is inactive in exactly the case that matters.
	 */
	virtual void PostRegisterAllComponents() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	TObjectPtr<UDynamicMeshComponent> Mesh;

	/** Subclass hook: produce this element's mesh from its parameters. */
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const { return UE::Geometry::FDynamicMesh3(); }

	/** Pushes a generated mesh into the component and turns on collision. */
	void CommitMesh(UE::Geometry::FDynamicMesh3&& Generated);

	/**
	 * Records that this element's geometry is not what it was.
	 *
	 * Bumps MeshRevision, which is the whole of the staleness model. Called from CommitMesh, from
	 * hand-edit detection, and from the articulated actor when it writes a part mesh - a shutter
	 * regenerating is a geometry change even though the root mesh never moved.
	 */
	void MarkMeshRevisionChanged();

	/** Applies the mode to the components. The only place visibility or collision is touched. */
	void ApplyRenderMode(EHFRenderMode Mode);

	/** Starts watching the component so external edits set bArtistEdited. */
	void WatchForEdits();

	/**
	 * Suppresses edit detection while we are the ones changing a mesh.
	 *
	 * Protected rather than private because a subclass with more than one mesh component has to
	 * write those under the same guard, or generating a part would mark it as hand-edited and it
	 * would never regenerate again.
	 */
	bool bGenerating = false;

	/** Creates, or finds, the static mesh component that stands in for one source component. */
	UStaticMeshComponent* EnsureBakedComponent(int32 PartIndex, UDynamicMeshComponent* Source);

	/**
	 * The one component an asset override draws through. Attached to the root, created on first use.
	 *
	 * Held on the actor rather than made and destroyed with the override so that clearing an override
	 * and setting another does not churn components - and, more importantly, so the component's
	 * lifetime is not the thing that decides whether the override is active. What decides that is
	 * whether it is holding a UStaticMesh, which is the same rule the bake arrived at by measurement.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge|Asset")
	TObjectPtr<UStaticMeshComponent> AssetOverrideComponent;

	/**
	 * What each source component blocked before the override took its collision away.
	 *
	 * THE SAME INVARIANT AS FHFBakedPart::SourceCollisionEnabled, and it exists because the override
	 * hit exactly the failure that field was written to prevent. ApplyRenderMode returns early for an
	 * element that has never been baked - `if (BakedParts.IsEmpty() && !bBaked)` - so on an
	 * un-baked fixture there was nothing at all to put the collision back, and clearing an override
	 * left every part of it passable. Visible only as a walkthrough falling through the furniture,
	 * with nothing logged. Found by HouseForge.Editor.Assets.CollisionFollowsTheOverride.
	 *
	 * Recorded per source and only from a component that is not already reading NoCollision, for the
	 * reason FHFBakedPart spells out: NoCollision is never something a generator declares, so the
	 * guard is the exact complement of our own write. Without it, re-applying an override over an
	 * active one would record the suppression as the thing to restore.
	 *
	 * SAVED, not transient, and that is load-bearing rather than tidy. An overridden element is saved
	 * with its sources already suppressed, so a transient record would be re-taken from those
	 * suppressed components on the next load - recording "blocks nothing" as the thing to restore.
	 * Reverting after a save-and-reload would then hand back a fixture that is visible, editable,
	 * correct-looking and completely passable, with nothing logged. Exactly the failure
	 * FHFBakedPart::SourceCollisionEnabled is saved to avoid, reached by a different road.
	 */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "HouseForge|Asset")
	TArray<TEnumAsByte<ECollisionEnabled::Type>> PreOverrideCollision;

	/** True while this override is the thing holding the source components' collision off. */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "HouseForge|Asset")
	bool bOverrideSuppressedCollision = false;

	/**
	 * Makes the components agree with AssetOverride, whatever the render mode says.
	 *
	 * Called at the end of ApplyRenderMode, which is called at the end of every generation path - so
	 * a regeneration, a bake, an unbake and a whole-house rebuild all leave an overridden element
	 * still showing its override rather than flickering back to geometry the user replaced.
	 */
	void RefreshAssetOverride();

private:
	bool bWatching = false;

	void HandleMeshChanged();

	/** The whole of ApplyRenderMode except the override reconciliation that always follows it. */
	void ApplyRenderModeToComponents(EHFRenderMode Mode);
};

/** A wall, with its openings already cut out. */
UCLASS()
class HOUSEFORGE_API AHFWallActor : public AHFElementActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFWall Wall;

	/**
	 * Beams and columns passing through this wall.
	 *
	 * The RCC frame goes up first and the blockwork infills around it, so the wall is not built
	 * where these are. Held on the actor rather than looked up, for the same reason the openings
	 * are: a wall owns everything it needs to rebuild itself when its thickness is edited, and a
	 * generator may not go looking for the rest of the house. See FHFStructuralCut.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	TArray<FHFStructuralCut> Structure;

	/** The openings cut into this wall. Held here so the wall owns everything it needs to rebuild. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	TArray<FHFOpening> Openings;

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
};

/** A room's floor slab and skirting. */
UCLASS()
class HOUSEFORGE_API AHFRoomActor : public AHFElementActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFRoom Room;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "1.0"))
	double SlabThickness = 15.0;

	/**
	 * Where the skirting runs, where it stops and why - the whole answer for this room.
	 *
	 * Resolved by FHFSkirting::For in the composing layer, because every question behind it is a
	 * question about the SPEC: which walls are set out on this room's edges, which openings are in
	 * those walls, and which joinery is scribed to them. A generator may not go looking - see
	 * .claude/rules/04-conventions.md - and the three fields this replaced were the composing layer
	 * trying to answer all three with a bag of points and one number, which is how a 750 bathroom
	 * door came to remove 1800 of skirting and how the common bathroom collected four gaps for doors
	 * in other people's rooms.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFSkirtingPlan Skirting;

	/**
	 * Also emit the structural slab soffit over this room.
	 *
	 * Without it, looking up in the middle of a room with a peripheral ceiling shows open sky
	 * where the structure above should be.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	bool bGenerateCeilingSlab = true;

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
};

/** A false ceiling over one room. */
UCLASS()
class HOUSEFORGE_API AHFCeilingActor : public AHFElementActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFFalseCeiling Ceiling;

	/** The room this ceiling covers, needed for its boundary and structural height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFRoom Room;

	/** Ceiling fans whose drop rods must pass through this ceiling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	TArray<FVector2D> FanDrops;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double FanDropRadius = 8.0;

	/**
	 * Where this ceiling's downlights are in the world, for whatever will light them.
	 *
	 * DELIBERATELY NO ApplyProjectDefaults HERE, unlike every other actor the settings page reaches.
	 * A ceiling figure does not change one element in place: it changes what hangs between a ceiling
	 * fan and the room, so the ceiling and the fans under it have to be re-seeded together or a
	 * deeper ceiling swallows the rotor. Only the house holds both, so re-seeding is
	 * AHFHouseActor::ApplyProjectSettingsToCeilings and there is no second way in.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge")
	TArray<FVector> DownlightPositions() const;

	/**
	 * Puts real lights in the cove trough and up each downlight can.
	 *
	 * WITHOUT THIS A COVE IS A PAINTED LINE. The strip is concealed from every camera in the flat by
	 * construction - that is the whole point of the detail - so what a person is meant to see is the
	 * WASH it throws on the slab above, and nothing was throwing one. DownlightPositions has
	 * returned the aperture rather than the plan dot since it was written, precisely so a light
	 * could be parented there, and until now it had no caller at all.
	 *
	 * Idempotent: the lights are destroyed and rebuilt, so regenerating a ceiling or re-seeding it
	 * from the settings page does not accumulate them.
	 *
	 * Rebuilt by Regenerate, so a ceiling whose design changes takes its lighting with it.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "HouseForge")
	int32 RebuildLights();

	/** Off leaves the fittings modelled but dark, which is what the flat did before this existed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Lighting")
	bool bBuildLights = true;

	/** Lumens per recessed downlight. A 3-inch COB is 600 to 900. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Lighting", meta = (ClampMin = "0.0"))
	double DownlightLumens = 700.0;

	/** Lumens per metre of cove strip. A domestic warm-white COB tape is 700 to 1200. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Lighting", meta = (ClampMin = "0.0"))
	double CoveLumensPerMetre = 900.0;

	/** Colour temperature of both, in kelvin. 3000 is the warm white these flats are lit with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Lighting", meta = (ClampMin = "1700.0", ClampMax = "12000.0"))
	double LightTemperatureKelvin = 3000.0;

	/** The lights this ceiling owns, so a rebuild can take them away again. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge|Lighting")
	TArray<TObjectPtr<ULightComponent>> Lights;

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
};

/** A downstand beam. Generated whether or not a false ceiling later conceals it. */
UCLASS()
class HOUSEFORGE_API AHFBeamActor : public AHFElementActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFBeam Beam;

	/** Columns this beam lands on, and any beam that runs through it. See FHFStructuralCut. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	TArray<FHFStructuralCut> Structure;

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
};

/** A column. */
UCLASS()
class HOUSEFORGE_API AHFColumnActor : public AHFElementActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFColumn Column;

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
};

// AHFOpeningActor lives in Actors/HFOpeningActor.h: a door leaf moves, so it derives from
// AHFArticulatedActor, which in turn derives from the base declared here.
