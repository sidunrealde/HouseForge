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

	/** Redraws the preview from the current spec. */
	UFUNCTION(BlueprintCallable, Category = "HouseForge")
	void Rebuild();

	// -------------------------------------------------------------------- preview appearance

	/** Wireframe preview of the spec. Replaced by real meshes in the geometry milestone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Preview")
	bool bShowPreview = true;

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
	void DrawRooms();
	void DrawStructure();
	void DrawFixtures();
	void DrawCeilings();

	/** Prism outline from a plan polygon extruded between two heights. */
	void DrawPrism(const TArray<FVector2D>& Polygon, double BottomZ, double TopZ,
		const FLinearColor& Color, float Thickness, bool bVerticals = true);
};
