// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/HFArticulatedActor.h"
#include "Model/HFTypes.h"
#include "HFOpeningActor.generated.h"

/**
 * What sits inside an opening: a door leaf, or a window frame and its glazing.
 *
 * Articulated, because a real door opens. The leaf is a moving part hung on the jamb the drawing's
 * swing arc puts it on. A sliding door is two panels instead - one fixed, one running in its own
 * track - because a single leaf the width of the opening has nowhere to slide to but into the wall.
 * See .claude/rules/04-conventions.md. Window sashes are still fixed; they move in the retrofit
 * that follows the joinery kit.
 */
UCLASS()
class HOUSEFORGE_API AHFOpeningActor : public AHFArticulatedActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFOpening Opening;

	/** The wall this opening sits in, needed to place the leaf in the wall's plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFWall HostWall;

	/** Part id of a door's leaf, and of the running panel of a sliding door. */
	static const FName LeafPartId;

	/** Part id of the fixed panel of a sliding door. Present only on a sliding door. */
	static const FName FixedPanelPartId;

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};
