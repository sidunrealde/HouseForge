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
 * swing arc puts it on, and a sliding door's leaf slides instead - see
 * .claude/rules/04-conventions.md. Window sashes are still fixed; they move in the retrofit that
 * follows the joinery kit.
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

	/** Part id of a door or sliding door's leaf. */
	static const FName LeafPartId;

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};
