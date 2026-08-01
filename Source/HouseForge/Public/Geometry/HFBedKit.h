// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Model/HFTypes.h"
#include "HFBedKit.generated.h"

/**
 * A bed: a headboard, a frame and a mattress.
 *
 * ## Three objects, not one box
 *
 * THIS IS THE WHOLE POINT OF THE KIT. A bed drawn as a single 1800 x 2000 x 600 solid is the most
 * conspicuous placeholder a bedroom can contain: it is the largest thing in the room, it is what the
 * eye lands on from the doorway, and one undifferentiated block of one material reads as a shipping
 * crate no matter how exactly it matches the drawing. What makes a bed look like a bed is that three
 * different materials meet on it - a polished carcass, a laminate or upholstered headboard, and a
 * soft mattress - and that they meet at edges the light can find.
 *
 * So this emits three surface roles and never merges them into one:
 *
 *   JoineryCarcass    the frame: the box the mattress lies on, and the plinth it stands on
 *   ShutterLaminate   the headboard panel
 *   Fabric            the mattress, and the upholstered pad inset into the headboard
 *
 * ## The mattress overhangs the frame, and that is what makes it read as a mattress
 *
 * FrameInset sets the box in from the mattress on the three open sides, so the mattress oversails it
 * and throws a continuous shadow line along the whole length of the bed. Built flush the two become
 * one silhouette and the mattress stops being separately legible from more than a metre away -
 * correct in every dimension, and a box. The inset is small, 25 mm, because it is a shadow and not a
 * ledge; larger and the bed starts to look like a mattress balanced on a pallet.
 *
 * ## Frame
 *
 * Centimetres, in the bed's own local space, and the same datum every wall-backed run in this plugin
 * uses so that FHFFixturePlacement::AgainstWall can place it: the origin is the front-left corner of
 * the drawn footprint on the floor, +X across the bed, +Y BACK towards the head, +Z up.
 *
 * The drawn box is the object. Depth is head to foot OVERALL, headboard included, so the mattress is
 * Depth less the headboard's thickness rather than the drawing's figure plus a headboard hanging over
 * the back of it - which would push the head of every bed in the flat through the wall behind it.
 *
 * ## What moves: nothing, deliberately
 *
 * .claude/rules/04-conventions.md says anything that moves must be able to. A bed is one of the few
 * things in this catalogue where the honest answer is that nothing does. A STORAGE bed's deck lifts
 * on gas struts and would need a hinge, a pivot and a travel limit like any other opening - but the
 * reference flat draws a plain double bed, not a storage one, and building a lid that opens onto a
 * hollow that is not modelled would be a mechanism invented to satisfy a rule rather than to match an
 * object. Stated here so that "the bed does not move" reads as a decision and not as an oversight.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFBedParams
{
	GENERATED_BODY()

	/** Mattress width. 180 king, 150 queen, 90 single - the Indian sizes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 180.0;

	/** Foot to the back of the headboard, overall. The mattress is this less HeadboardThickness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 200.0;

	/**
	 * Top of the mattress above the floor. THE HEIGHT A DRAWING GIVES A BED.
	 *
	 * Not the headboard's height, which is the tallest thing here and the obvious thing to confuse it
	 * with. A plan dimensions a bed by the surface somebody sits on, because that is the figure that
	 * has to agree with the nightstand beside it - 550 of nightstand under a 600 mattress top is a
	 * table you can reach a glass down onto, and the two numbers only mean anything measured from the
	 * same datum.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double MattressTopZ = 60.0;

	/** A domestic mattress. 200 mm is a spring or a foam mattress; anything under 100 is a pad. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double MattressThickness = 20.0;

	/** Top of the headboard above the floor. 1000-1100 is a bed you can sit up against. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double HeadboardHeight = 105.0;

	/** Finished headboard panel, upholstery included. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double HeadboardThickness = 6.0;

	/**
	 * How far the frame is set in from the mattress on the foot and the two sides.
	 *
	 * The shadow line that separates a mattress from the box under it. See the header - this is the
	 * single figure that decides whether a bed reads as a bed or as a crate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double FrameInset = 2.5;

	/** Recessed base the frame stands on, so the box does not sit flat on the floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double PlinthHeight = 8.0;

	/** How far the plinth is set in behind the frame above it - the toe shadow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double PlinthRecess = 3.0;

	/** An upholstered pad inset into the headboard's front face, in Fabric. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Construction")
	bool bUpholsteredHeadboard = true;

	/** Border of bare panel left round the pad. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Construction",
		meta = (EditCondition = "bUpholsteredHeadboard", ClampMin = "0.0"))
	double UpholsteryMargin = 7.0;

	/** How far the pad stands off the panel. It is stuffed, so it is proud rather than flush. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Construction",
		meta = (EditCondition = "bUpholsteredHeadboard", ClampMin = "0.0"))
	double UpholsteryProud = 1.5;

	/** Front face of the headboard: where the mattress stops and the panel starts. */
	double HeadboardFaceY() const { return FMath::Max(Depth - HeadboardThickness, 0.0); }

	/** Foot to head of the mattress itself, which is the drawn depth less the headboard. */
	double MattressLength() const { return HeadboardFaceY(); }

	/** Top of the frame box, which is the underside of the mattress. */
	double DeckTopZ() const { return FMath::Max(MattressTopZ - MattressThickness, 0.0); }

	/**
	 * Overall height of what is actually built.
	 *
	 * THE DRAWN BOX IS NOT THE OBJECT HERE, and by a long way: a bed drawn 600 high stands 1050 with
	 * its headboard on. Nothing in the reference flat has a ceiling anywhere near either figure, but
	 * FHFCeilingFit takes a built envelope rather than a drawn one precisely so that the answer does
	 * not depend on the room happening to be tall - see FHFCeilingFit::Fit.
	 */
	double BuiltHeight() const { return FMath::Max(MattressTopZ, HeadboardHeight); }

	bool IsValid() const
	{
		return Width > 0.0 && MattressLength() > 0.0 && MattressThickness > 0.0
			&& DeckTopZ() > PlinthHeight;
	}
};

/**
 * A composed bed: one fixed shell, and each of the three surfaces kept alongside it.
 *
 * The sub-assemblies are here for the same reason FHFCasedGoodsBuild keeps its carcasses: a
 * clearance BETWEEN two of them stops being measurable once they are one mesh, and the overhang of
 * the mattress over the frame is exactly such a clearance. Merged, "does the mattress oversail the
 * frame" has no answer at all.
 *
 * A plain struct rather than a USTRUCT because it carries meshes by value.
 */
struct HOUSEFORGE_API FHFBedBuild
{
	/** Everything, merged, in bed-local space. What the actor's BuildMesh returns. */
	UE::Geometry::FDynamicMesh3 Shell;

	/** The box the mattress lies on, and the plinth under it. */
	UE::Geometry::FDynamicMesh3 Frame;

	/** The headboard panel and its upholstered pad. */
	UE::Geometry::FDynamicMesh3 Headboard;

	UE::Geometry::FDynamicMesh3 Mattress;

	/** The parameters actually used, after clamping. */
	FHFBedParams Used;

	bool bValid = false;
};

/**
 * Building a bed out of its three parts.
 *
 * Pure, like every other generator here: parameters in, meshes out, no world, no actor, no editor,
 * no asset loading and no settings object - see .claude/rules/04-conventions.md.
 */
class HOUSEFORGE_API FHFBedKit
{
public:
	/** The parameters actually used, clamped so the bed they describe can be built. */
	static FHFBedParams Sanitise(const FHFBedParams& Params);

	/**
	 * Frame, headboard and mattress, merged and kept separately.
	 *
	 * @return A build with bValid false and empty meshes when the parameters describe no bed. Never
	 *         a degenerate one - an empty mesh appends harmlessly, where a sliver carries through
	 *         every volume measurement taken afterwards.
	 */
	static FHFBedBuild Build(const FHFBedParams& Params);
};
