// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/HFArticulatedActor.h"
#include "Model/HFTypes.h"
#include "HFOpeningActor.generated.h"

/**
 * What sits inside an opening: a door leaf, or a window frame with its sashes and glazing.
 *
 * Articulated, because a real door opens and so does a real window. The leaf is a moving part hung
 * on the jamb the drawing's swing arc puts it on. A sliding door is two panels instead - one fixed,
 * one running in its own track - because a single leaf the width of the opening has nowhere to
 * slide to but into the wall. A sliding window is the same arrangement one size down: two sashes on
 * two tracks, each glazed in its own rebate, with the operable one running. A ventilator is a
 * top-hung sash pivoting on its head.
 *
 * A fixed window and an archway have nothing that moves, and that is the answer rather than an
 * omission - see .claude/rules/04-conventions.md.
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

	/** Part id of the sash that moves: the running one of a sliding window, or a pivot ventilator's. */
	static const FName SashPartId;

	/** Part id of the fixed sash of a sliding window. */
	static const FName FixedSashPartId;

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};
