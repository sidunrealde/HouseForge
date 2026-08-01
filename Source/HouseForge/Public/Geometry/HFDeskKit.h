// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Geometry/HFJoineryKit.h"
#include "Model/HFArticulation.h"
#include "Model/HFBuildDefaults.h"
#include "HFDeskKit.generated.h"

/**
 * A study table: a top on a drawer pedestal at one end and a panel gable at the other.
 *
 * ## Why this is not a cased good
 *
 * FHFCasedGoodsKit builds a STACK OF BOXES, and every proportion in it is measured from the fact
 * that a carcass fills its footprint. A desk is the opposite object: the whole point of it is the
 * hole in the middle, because that is where somebody's knees go. Expressed as a cased good with one
 * bay of drawers and two empty bays it would come out as a 1200 sideboard with a chair pulled up to
 * it - correct in width, depth and height, and unusable.
 *
 * That hole is not only a look. It decides where the SKIRTING goes: a run of joinery scribed to the
 * plaster has its skirting cut out because there is nowhere for the board to run, whereas a desk has
 * 700 mm of clear wall under it and the skirting runs straight through. See
 * FHFSkirting::IsScribedJoinery, where a study table is deliberately not scribed, and SupportSetback
 * below, which is what keeps the legs off the board that now runs behind them.
 *
 * ## Frame
 *
 * Centimetres, in the desk's own local space, and the same datum every wall-backed run uses so that
 * FHFFixturePlacement::AgainstWall can place it: the origin is the front-left corner of the drawn
 * footprint on the floor, +X along the desk, +Y BACK towards the wall, +Z up.
 *
 * ## Construction figures live in Joinery
 *
 * As FHFCasedGoodsParams describes: what this desk MEASURES is here, and HOW joinery is built in this
 * project is in Joinery, which the composing layer resolves from the settings once and hands in.
 * Nothing in this file reaches for a settings object - see .claude/rules/04-conventions.md.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFDeskParams
{
	GENERATED_BODY()

	/** Length of the top, along +X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 120.0;

	/** Front edge of the top to the wall behind it, along +Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 55.0;

	/** Top of the working surface above the floor. 750 is writing height and is not negotiable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Height = 75.0;

	/** The worktop board: 25-30 mm of faced ply, thick enough to span the knee hole without a rail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double TopThickness = 2.5;

	/** Width of the drawer pedestal. 400 is what a pedestal is; anything less will not take a file. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double PedestalWidth = 40.0;

	/** Drawers in the pedestal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions",
		meta = (ClampMin = "0", ClampMax = "6"))
	int32 DrawerCount = 2;

	/** How much deeper the bottom front is than the top one. Nearly equal on a desk pedestal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "1.0"))
	double GradationRatio = 1.3;

	/** Puts the pedestal at the +X end instead of the -X end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions")
	bool bPedestalAtRightEnd = false;

	/** The panel gable carrying the other end of the top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double GableThickness = 1.8;

	/**
	 * Height of the modesty panel hung under the back edge of the top, between the two supports.
	 *
	 * Not decoration. It is what closes the back of a desk in elevation and gives the knee hole a
	 * shadow rather than a view straight through to the skirting; a desk without one reads as a
	 * trestle. Zero leaves it off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double ModestyPanelHeight = 25.0;

	// THERE IS NO PLINTH ON A DESK, and its absence is a decision rather than an omission. A toe kick
	// is what fitted wall-to-wall joinery has: it puts the carcass in its own shadow so a run of
	// cupboards appears to float, and it exists because you stand right up against a kitchen or a
	// wardrobe with your toes under it. Nobody stands against a desk - you sit at one, with a chair
	// and your own feet in the space a kick would have occupied - and a pedestal set up on a recessed
	// base reads as a base unit that has lost its worktop. Both supports therefore run to the floor.

	/**
	 * How far short of the drawn back plane the SUPPORTS stop, leaving the skirting to run behind.
	 *
	 * THE ONE FIGURE THAT KEEPS A DESK OFF A SKIRTING BOARD. A study table is not scribed joinery -
	 * the board runs on behind it - so its pedestal and its gable have to stand in front of that
	 * board, and a desk pushed flat against the plaster stands 18 mm inside it instead. Invisible in
	 * plan, invisible in any test of the desk on its own, and a permanent 18 mm interpenetration in
	 * the built room.
	 *
	 * The TOP is not set back: it runs the full drawn depth, over the skirting, which is what a
	 * worktop scribed to a wall actually does.
	 *
	 * Resolved by the composing layer from the project's skirting depth and handed in as a value, in
	 * the same way a chimney's duct length is - a generator may not go looking for the room it is in.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double SupportSetback = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Front")
	EHFHandleStyle HandleStyle = EHFHandleStyle::Bar;

	/** How joinery is built in this project. Filled in by the composing layer, never read here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Construction",
		meta = (ShowOnlyInnerProperties))
	FHFJoineryDefaults Joinery;

	/** Underside of the worktop, which is what both supports run up to. */
	double TopUnderZ() const { return FMath::Max(Height - TopThickness, 0.0); }

	/** How far back the supports actually reach. See SupportSetback. */
	double SupportDepth() const { return FMath::Max(Depth - SupportSetback, 0.0); }

	/**
	 * Clear height under the top, between the plinth and the worktop.
	 *
	 * The measurement that decides whether this is a desk. 650 is the figure a knee needs, and it is
	 * asserted rather than assumed - a pedestal generous enough to swallow the leg room is a
	 * sideboard whatever the drawing called it.
	 */
	double KneeClearance() const { return TopUnderZ(); }

	/** The drawn box IS the object: nothing on a desk stands above its own worktop. */
	double BuiltHeight() const { return Height; }

	bool IsValid() const
	{
		return Width > PedestalWidth + GableThickness && SupportDepth() > 0.0
			&& TopUnderZ() > 0.0 && TopThickness > 0.0;
	}
};

/**
 * A composed desk: the fixed shell, one part per drawer, and the sub-assemblies kept alongside.
 *
 * A plain struct rather than a USTRUCT because it carries meshes by value, exactly as
 * FHFCasedGoodsBuild does - and the sub-assemblies are here for the same reason: the KNEE HOLE is a
 * clearance BETWEEN the pedestal and the gable, and a clearance between two solids stops being
 * measurable the moment they are one mesh.
 */
struct HOUSEFORGE_API FHFDeskBuild
{
	/** Everything fixed, merged, in desk-local space. What the actor's BuildMesh returns. */
	UE::Geometry::FDynamicMesh3 Shell;

	/** Every drawer, each in its own local space with its pivot on the origin. */
	TArray<FHFMeshPart> Parts;

	UE::Geometry::FDynamicMesh3 Top;

	/** The pedestal carcass and its plinth. */
	UE::Geometry::FDynamicMesh3 Pedestal;

	/** The panel gable at the far end, and its plinth. */
	UE::Geometry::FDynamicMesh3 Gable;

	UE::Geometry::FDynamicMesh3 ModestyPanel;

	/** The carcass the pedestal was actually built from. */
	FHFCarcassParams PedestalCarcass;

	/** Where the pedestal's -X face lands, in desk space. */
	double PedestalX0 = 0.0;

	/** Where the gable's -X face lands, in desk space. */
	double GableX0 = 0.0;

	/** The parameters actually used, after clamping. */
	FHFDeskParams Used;

	bool bValid = false;
};

/**
 * Composing the joinery kit into a desk.
 *
 * Pure, like every other generator here: parameters in, meshes and parts out, no world, no actor, no
 * editor, no asset loading and no settings object - see .claude/rules/04-conventions.md.
 */
class HOUSEFORGE_API FHFDeskKit
{
public:
	/** The parameters actually used, clamped so the desk they describe can be built. */
	static FHFDeskParams Sanitise(const FHFDeskParams& Params);

	/**
	 * Top, pedestal, gable, modesty panel, and a part per drawer.
	 *
	 * @return A build with bValid false and empty meshes when the parameters describe no desk.
	 */
	static FHFDeskBuild Build(const FHFDeskParams& Params);

	/** Part ids are this with the index appended, top to bottom: Drawer0, Drawer1, ... */
	static FName DrawerPartIdPrefix() { return TEXT("Drawer"); }
};
