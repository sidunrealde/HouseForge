// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
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
	 */
	void AdoptBakedMesh(int32 PartIndex, FName SourceComponentName, UStaticMesh* InBakedMesh, int32 AtRevision);

	/**
	 * Makes BakedParts match the current source components, destroying components for parts that no
	 * longer exist.
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

private:
	bool bWatching = false;

	void HandleMeshChanged();
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
