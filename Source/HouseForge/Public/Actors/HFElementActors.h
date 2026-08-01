// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "GameFramework/Actor.h"
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

	/** Plan positions where skirting stops, so it does not run across a doorway. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	TArray<FVector2D> DoorwayCentres;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "1.0"))
	double DoorwayWidth = 100.0;

	/**
	 * How far the finished wall face stands in from each boundary edge, one entry per edge.
	 *
	 * A room boundary is a wall CENTRELINE, so the plaster is half a wall's thickness inside it.
	 * The skirting is laid against that face, and only the composing layer can see which wall is on
	 * which edge - so it works this out and puts the answer here, and the generator is handed it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	TArray<double> WallFaceInsets;

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
	 * The beams showing in this room, deepest first - what a perimeter bulkhead ring has to bury.
	 *
	 * CARRIED, NOT LOOKED UP. Which beams show in a room is a fact about the frame and the
	 * partitions under it, and only the composing layer can see either; this actor holds a room and
	 * a ceiling and nothing else. Without them, changing a ceiling figure on the settings page would
	 * re-stamp the band and silently drop the ring that was burying the beam - the exact defect the
	 * whole depth model exists to prevent, arrived at by turning a dial.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	TArray<FHFBeam> BeamsShowingInRoom;

	/**
	 * Re-resolve this ceiling's template against the project's current figures.
	 *
	 * The composing layer's job and the only line in this actor that knows a settings object can
	 * exist. Does nothing to a ceiling whose template is Custom, which is what keeps a hand-tuned
	 * one hand-tuned.
	 */
	void ApplyProjectDefaults();

	/** Where this ceiling's downlights are in the world, for whatever will light them. */
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
