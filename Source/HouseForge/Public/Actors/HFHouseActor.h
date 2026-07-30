// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Model/HFTypes.h"
#include "HFHouseActor.generated.h"

class ULineBatchComponent;

/**
 * The house in the level.
 *
 * Holds the spec that was read from a drawing and is the single thing the MCP tools and the editor
 * act on. Storing the spec on an actor rather than in a transient subsystem is what lets a level be
 * saved, reopened and read back - the round trip Claude relies on when correcting a misread plan.
 *
 * Until the geometry milestone lands this draws a wireframe rather than meshes. That is deliberate
 * scaffolding: it makes the read-drawing-to-level loop verifiable by screenshot now, and the mesh
 * generators replace it without changing anything the tools see.
 */
UCLASS(BlueprintType, HideCategories = (Rendering, Replication, Collision, HLOD, Physics, Networking, Input))
class HOUSEFORGE_API AHFHouseActor : public AActor
{
	GENERATED_BODY()

public:
	AHFHouseActor();

	/**
	 * The house, always in Unreal centimetres.
	 *
	 * SetSpec converts on the way in, so nothing reading this ever has to ask what units it is in.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	FHFHouseSpec Spec;

	/** Drawing this spec came from, carried through so a level can be traced back to its source. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	FString SourceDrawing;

	/** Replaces the spec, converting to centimetres, and redraws. */
	UFUNCTION(BlueprintCallable, Category = "HouseForge")
	void SetSpec(const FHFHouseSpec& InSpec);

	/** Redraws the wireframe preview from the current spec. */
	UFUNCTION(BlueprintCallable, Category = "HouseForge")
	void Rebuild();

	/**
	 * Spawns or replaces the element actors that make up the house: walls with their openings cut
	 * out, floors and skirting, beams, columns and opening infill.
	 *
	 * Called explicitly rather than from PostLoad. Spawning actors while the level is still
	 * loading is asking for trouble, and once a level is saved its element actors are already in
	 * it - rebuilding on load would duplicate them.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "HouseForge")
	void BuildGeometry();

	/** Destroys every element actor this house owns. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "HouseForge")
	void ClearGeometry();

	/**
	 * Takes the house's elements with it.
	 *
	 * UWorld::DestroyActor explicitly DETACHES attached children rather than destroying them, and
	 * does not touch owned actors either, so without this every wall, floor, ceiling and opening
	 * from the previous run is orphaned in the level - occupying the same space as the new one, and
	 * invisible in a top-down capture because the two houses are coincident. Owning the cleanup here
	 * rather than at the one call site also covers a user deleting the house in the outliner.
	 */
	virtual void Destroyed() override;

	/** Element actors generated from the spec, owned by this house. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	TArray<TObjectPtr<AActor>> ElementActors;

	/** Floor slab thickness used when generating rooms. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Geometry", meta = (ClampMin = "1.0"))
	double SlabThickness = 15.0;

	// -------------------------------------------------------------------- preview appearance

	/**
	 * Wireframe preview of the spec, drawn over the generated meshes.
	 *
	 * Off by default now that real geometry exists; still useful for seeing what the spec says
	 * when the meshes look wrong, and it is the only thing that draws door swing arcs.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Preview")
	bool bShowPreview = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Preview")
	bool bShowRooms = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Preview")
	bool bShowOpenings = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Preview")
	bool bShowStructure = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Preview")
	bool bShowFixtures = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Preview")
	bool bShowCeilings = true;

	virtual void PostLoad() override;
	virtual void PostRegisterAllComponents() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	UPROPERTY()
	TObjectPtr<ULineBatchComponent> Lines;

	void DrawWalls();
	void DrawOpenings();

	/** Leaf and sweep arc for a hinged door, so its direction is checkable from above. */
	void DrawSwing(const FHFOpening& Opening, const FHFWall& Wall,
		const FVector2D& Direction, const FVector2D& Normal,
		const FVector2D& Near, const FVector2D& Far, double BaseZ);
	void DrawRooms();
	void DrawStructure();
	void DrawFixtures();
	void DrawCeilings();

	/** Prism outline from a plan polygon extruded between two heights. */
	void DrawPrism(const TArray<FVector2D>& Polygon, double BottomZ, double TopZ,
		const FLinearColor& Color, float Thickness, bool bVerticals = true);
};
