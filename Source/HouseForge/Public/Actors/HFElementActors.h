// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "GameFramework/Actor.h"
#include "Geometry/HFRenderFinish.h"
#include "Model/HFSkirtingPlan.h"
#include "Model/HFTypes.h"
#include "HFElementActors.generated.h"

class UDynamicMeshComponent;

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

	UDynamicMeshComponent* GetMeshComponent() const { return Mesh; }

	virtual void PostInitializeComponents() override;

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
