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
	void Regenerate();

	/** Throws away hand edits and rebuilds from the parameters. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "HouseForge")
	void RevertToGenerated();

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

private:
	/** Suppresses edit detection while we are the ones changing the mesh. */
	bool bGenerating = false;

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

/** What sits inside an opening: a door leaf, or a window frame and its glazing. */
UCLASS()
class HOUSEFORGE_API AHFOpeningActor : public AHFElementActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFOpening Opening;

	/** The wall this opening sits in, needed to place the leaf in the wall's plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFWall HostWall;

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
};
