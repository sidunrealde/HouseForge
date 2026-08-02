// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Model/HFTypes.h"
#include "HFUpholsteryKit.generated.h"

/**
 * Soft goods: a sofa, built as separate forms with real radii rather than as a box.
 *
 * ## Why this is its own kit
 *
 * Everything else in this catalogue is made of boards, tubes or pressed ceramic, and every one of
 * them is a flat-faced solid whose whole quality comes from the 1 mm chamfer FHFMeshOps puts on its
 * arrises. Upholstery is the one construction in the flat where that is useless, and the reason is
 * written into FHFBevelParams itself: Fabric's chamfer width is ZERO, deliberately, because "a
 * cushion has no arris to catch light". Correct - and it means a sofa built the way a wardrobe is
 * built comes out with mathematically perfect edges and reads as a cardboard box.
 *
 * A sofa arm is a 60-80 mm radius and a seat cushion is 40 mm. Those are not chamfers, they are the
 * shape of the object, so they are built: see FHFMeshOps::AppendSoftBox, which lofts a box out of
 * rounded-rectangle rings at a true radius. The facets land below FHFBevelParams::MinAngleDegrees,
 * so ComputeShadingNormals welds them smooth and the bevel pass correctly does nothing to them.
 *
 * ## Six forms, not one
 *
 * The same argument FHFBedKit makes about a bed, and harder: a sofa is the largest soft object in
 * the flat and the one the eye lands on from the front door. What makes it read is that its parts
 * are separately legible - the arms stand proud of the base, the cushions sit in a well between
 * them with shadow gaps between each pair, and the back cushions LEAN. A sofa drawn as one 2100 x
 * 900 x 800 solid is a shipping crate however exactly it matches the drawing.
 *
 *   Legs        JoineryCarcass   turned timber, and the only hard material on the object
 *   Base        Fabric           the upholstered plinth the cushions sit in
 *   Arms        Fabric           bolsters standing proud of the base on both sides
 *   Back        Fabric           the panel closing the back, above the arms
 *   Seat        Fabric           one cushion per seat, with a shadow gap between each pair
 *   Back cushions Fabric         one per seat, LEANING - see FHFSofaParams::BackRake
 *
 * ## What moves: nothing, and it is a decision
 *
 * .claude/rules/04-conventions.md asks that anything which moves in the real object moves here. On a
 * plain three-seater the honest answer is that nothing does. Cushions are REMOVABLE, which is not
 * the same as articulated - there is no hinge, no travel limit and no open amount that means
 * anything, and a "cushion lift" invented to satisfy the rule would be a control that lies about the
 * object. A RECLINER's footrest swings and a sofa-bed's back folds flat; the reference flat draws
 * neither. Stated here so the absence reads as an answer rather than an oversight.
 *
 * ## Frame
 *
 * Centimetres, in the sofa's own local space, and the same datum every wall-backed piece uses so
 * that FHFFixturePlacement::AgainstWall can place it: the origin is the front-left corner of the
 * drawn footprint on the floor, +X across the sofa, +Y BACK towards the wall, +Z up.
 *
 * The drawn box is the object. Depth is front to back OVERALL and Height is the top of the back, so
 * a sofa cannot grow out of the wall it stands against or over the picture above it.
 *
 * ## Purity
 *
 * Parameters in, meshes out. No world, no actor, no editor, no asset loading and no settings object
 * - see .claude/rules/04-conventions.md.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFSofaParams
{
	GENERATED_BODY()

	/** Overall width, arm to arm outside. 2100 is a three-seater; 1500 a two. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 210.0;

	/** Front of the arms to the back of the back panel, overall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 90.0;

	/** Top of the back panel above the floor. THE HEIGHT A DRAWING GIVES A SOFA. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Height = 80.0;

	/** Seats, and therefore cushions: one seat cushion and one back cushion each. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions",
		meta = (ClampMin = "1", ClampMax = "6"))
	int32 SeatCount = 3;

	/**
	 * Top of the seat cushion above the floor, uncompressed.
	 *
	 * 420-450 is a sofa; 450-460 is a dining chair. The two figures are close and they are not the
	 * same thing, because a sofa is sat back in and a chair is sat up at.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double SeatHeight = 43.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double SeatCushionThickness = 14.0;

	/** Top of the arms above the floor. Between the seat and the back, or it is not an arm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double ArmHeight = 62.0;

	/** Width of each arm in plan. 180-220 is upholstered over a frame; 60 is a metal-framed arm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double ArmWidth = 18.0;

	/** The upholstered panel closing the back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BackThickness = 14.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BackCushionThickness = 14.0;

	/**
	 * How far a back cushion rises above the seat it stands on.
	 *
	 * Its own dimension rather than "up to the back panel", because what shows ABOVE the cushions is
	 * the panel, and a sofa whose cushions reach the top of their own back has no panel visible at
	 * all - which is the one thing that would make the back read as a slab again.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BackCushionHeight = 31.0;

	/**
	 * How far the top of a back cushion leans back over its own height.
	 *
	 * THE SINGLE FIGURE THAT DECIDES WHETHER THIS READS AS SEATING. Upright, a back cushion is a slab
	 * standing on a seat and the sofa is three boxes in a row; leaned back it is somewhere to sit, and
	 * the lean is what puts a shadow under the cushion's own bottom edge. Built as a shear rather than
	 * a rotation - see FHFSoftBoxParams::RakeY - so the cushion still occupies an answerable box.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BackRake = 4.0;

	/**
	 * Gap between one cushion and the next, and between the outer cushions and the arms.
	 *
	 * Not a tolerance. It is the shadow line that makes three cushions read as three, and without it a
	 * three-seater's seat is one continuous slab 1740 mm long.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double CushionGap = 1.5;

	/** Clear height under the base. Light under a sofa is most of what makes it read as furniture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double LegHeight = 12.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double LegDiameter = 6.0;

	/** How far each leg stands in from the two faces of the corner it is under. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double LegInset = 9.0;

	/**
	 * How far the base is set in from the drawn box on the front and the two sides.
	 *
	 * The bed's argument, applied to a sofa: the arms and the cushions oversail the base, so the base
	 * lies in their shadow and the sofa's silhouette is its arms rather than one slab from the floor
	 * up. Small, because it is a shadow and not a ledge.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BaseInset = 2.0;

	/** Radius rolled onto an arm's top edge and its ends in plan. The biggest radius on the object. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Softness", meta = (ClampMin = "0.0"))
	double ArmRoll = 7.0;

	/** Radius rolled onto a cushion's edges. 40 mm is a filled cushion; 10 is a seat pad. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Softness", meta = (ClampMin = "0.0"))
	double CushionRoll = 4.0;

	/** Underside of the seat cushions, which is the top of the base. */
	double DeckZ() const { return FMath::Max(SeatHeight - SeatCushionThickness, 0.0); }

	/** Inside face of the left arm. */
	double InnerX0() const { return FMath::Min(ArmWidth, Width * 0.5); }

	/** Inside face of the right arm. */
	double InnerX1() const { return FMath::Max(Width - ArmWidth, Width * 0.5); }

	/** Clear width between the arms: what the cushions and their gaps have to divide up. */
	double InnerWidth() const { return FMath::Max(InnerX1() - InnerX0(), 0.0); }

	/** Front face of the back panel. */
	double BackFaceY() const { return FMath::Max(Depth - BackThickness, 0.0); }

	/** Front edge of the cushions, in front of the base so they oversail it. */
	double CushionFrontY() const { return BaseInset * 0.5; }

	/**
	 * Front edge of a back cushion at its BOTTOM, where it is furthest forward.
	 *
	 * Set out backwards from the panel: the cushion's TOP runs into the panel by one gap's worth, so
	 * that leaning it back opens a shadow under its bottom edge rather than a slot behind its top one.
	 */
	double BackCushionY0() const
	{
		return FMath::Max(BackFaceY() + CushionGap - BackRake - BackCushionThickness, CushionFrontY());
	}

	/** Top of the back cushions. What shows between here and Height is the panel. */
	double BackCushionTopZ() const { return SeatHeight + BackCushionHeight; }

	/** Width of one seat cushion, after the gap either side of every one has been taken out. */
	double SeatCushionWidth() const
	{
		const int32 Seats = FMath::Max(SeatCount, 1);
		return FMath::Max((InnerWidth() - CushionGap * (Seats + 1)) / Seats, 0.0);
	}

	/**
	 * Front to back of a seat cushion: what is left once the back cushion and its lean are taken out.
	 *
	 * The measurement that decides whether this is a sofa or a bench. 500 mm is the shallowest seat
	 * anybody sits back in; below about 450 it is a hall bench with a cushion on it.
	 */
	double SeatCushionDepth() const
	{
		return FMath::Max(BackCushionY0() - CushionGap - CushionFrontY(), 0.0);
	}

	/** The drawn box IS the object: nothing on a sofa stands above its own back. */
	double BuiltHeight() const { return Height; }

	bool IsValid() const
	{
		return Width > 0.0 && Depth > 0.0 && InnerWidth() > 0.0 && SeatCushionWidth() > 0.0
			&& SeatCushionDepth() > 0.0 && DeckZ() > LegHeight
			&& ArmHeight > SeatHeight && Height > ArmHeight;
	}
};

/**
 * A composed sofa: one merged shell, and every form kept alongside it.
 *
 * The sub-assemblies are here for the reason FHFBedBuild's are: a clearance BETWEEN two of them stops
 * being measurable the moment they are one mesh, and on a sofa nearly everything worth asserting is
 * such a clearance - the gap between one cushion and the next, the arm standing proud of the base,
 * the lean of a back cushion over its own footprint. Merged, none of those has an answer at all.
 *
 * A plain struct rather than a USTRUCT because it carries meshes by value.
 */
struct HOUSEFORGE_API FHFSofaBuild
{
	/** Everything, merged, in sofa-local space. What the actor's BuildMesh returns. */
	UE::Geometry::FDynamicMesh3 Shell;

	/** The four turned legs. */
	UE::Geometry::FDynamicMesh3 Legs;

	/** The upholstered plinth the cushions sit in. */
	UE::Geometry::FDynamicMesh3 Base;

	/** Left arm then right arm. */
	TArray<UE::Geometry::FDynamicMesh3> Arms;

	/** The panel closing the back, above the arms. */
	UE::Geometry::FDynamicMesh3 Back;

	/** One per seat, left to right. */
	TArray<UE::Geometry::FDynamicMesh3> SeatCushions;

	/** One per seat, left to right, each leaning back by BackRake. */
	TArray<UE::Geometry::FDynamicMesh3> BackCushions;

	/** The parameters actually used, after clamping. */
	FHFSofaParams Used;

	bool bValid = false;
};

/**
 * Building a sofa out of its six forms.
 *
 * Pure, like every other generator here: parameters in, meshes out, no world, no actor, no editor,
 * no asset loading and no settings object - see .claude/rules/04-conventions.md.
 */
class HOUSEFORGE_API FHFUpholsteryKit
{
public:
	/** The parameters actually used, clamped so the sofa they describe can be built. */
	static FHFSofaParams SanitiseSofa(const FHFSofaParams& Params);

	/**
	 * Legs, base, arms, back and two cushions per seat, merged and kept separately.
	 *
	 * @return A build with bValid false and empty meshes when the parameters describe no sofa. Never
	 *         a degenerate one - an empty mesh appends harmlessly, where a sliver carries through
	 *         every volume measurement taken afterwards.
	 */
	static FHFSofaBuild BuildSofa(const FHFSofaParams& Params);
};
