// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Model/HFArticulation.h"
#include "Model/HFTypes.h"
#include "HFFrameKit.generated.h"

/**
 * Assemblies of thin members: tubes and sections on brackets, posts and legs.
 *
 * ## Why this is a kit and not a towel rail
 *
 * A towel rail is the first of seven instances of one construction problem: an object whose whole
 * mass is a handful of members 10 to 40 mm across, where every dimension that matters is a CENTRE
 * LINE and every joint is a member dying into another member's surface. The balcony railings, the
 * dining table and the coffee table are the rest of them.
 *
 * They share the failure mode too. A thin member is small enough that the render finish can lose it
 * entirely - see FHFRenderFinish::MinFeatureFactor - and small enough that a joint modelled as two
 * solids TOUCHING rather than overlapping is a visible seam at any distance. So members here always
 * run INTO what they land on rather than up to it.
 *
 * Pure - parameters in, meshes out, no world, no actor, no editor, no settings object. See
 * .claude/rules/04-conventions.md.
 */

/**
 * A towel rail: a tube on two wall brackets.
 *
 * ## Frame
 *
 * Centimetres, origin at the CENTRE of the drawn footprint in plan, at the BOTTOM of the drawn box,
 * +Y running BACK into the wall.
 *
 * ## What moves
 *
 * Nothing, and it is stated rather than omitted. A SWING-ARM rail exists and it swings; the
 * reference flat draws a 500 x 40 straight rail, which is two brackets and a tube and has no moving
 * part at all. See .claude/rules/04-conventions.md - the rule is that a thing which moves in the
 * real object moves here, not that every object must be given something to move.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFTowelRailParams
{
	GENERATED_BODY()

	/** Overall length along the wall, bracket to bracket outside. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 50.0;

	/** How far the rail stands off the wall, wall face to the front of the tube. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 4.0;

	/** Height of the drawn box: the bracket's own height, not the rail's diameter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Height = 6.0;

	/** The tube itself. 18-20 mm is a towel rail; 25 is a grab rail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double RailDiameter = 1.9;

	/** Diameter of the flange screwed to the wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double FlangeDiameter = 4.6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double FlangeThickness = 0.8;

	bool IsValid() const { return Width > 0.0 && Depth > 0.0 && RailDiameter > 0.0; }
};

/**
 * A table: a top on four legs, with a rail under it and optionally a shelf between them.
 *
 * ## Frame
 *
 * Centimetres, origin at the CENTRE of the drawn footprint in plan, on the floor - the datum
 * FHFFixturePlacement::FreeStanding places loose furniture by, because a table has no back and no
 * set-out corner and a drawing marks one by where it sits.
 *
 * ## Two materials, and why that matters more than the joinery does
 *
 * The top is ShutterLaminate and everything under it is JoineryCarcass. That is what a table is - a
 * faced top on a solid frame - and it is also the only thing separating a dining table from a
 * 1400 x 800 block at 750: from standing height you see the top, its edge, and a line of shadow
 * under it, and if the top is the same surface as the legs there is no line.
 *
 * ## Knee room is the measurement, not the height
 *
 * A dining table is 750 to the top and that figure is on the drawing. What decides whether anybody
 * can sit at it is TopUnderZ less the apron, which is not on the drawing at all - a 100 mm rail
 * under a 30 mm top leaves 620, which is 30 mm under what a knee needs. It is asserted rather than
 * assumed, in the same way FHFDeskParams::KneeClearance is.
 *
 * ## What moves: nothing, deliberately
 *
 * An EXTENSION table's leaf slides and a nest of tables pulls apart. The reference flat draws
 * neither: a 1400 x 800 four-seater and a 1100 x 600 coffee table, both with fixed tops and no
 * drawer declared on either. See .claude/rules/04-conventions.md - the rule is that a thing which
 * moves in the real object moves here.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFTableParams
{
	GENERATED_BODY()

	/** Length of the top, along +X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 140.0;

	/** Width of the top, along +Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 80.0;

	/** Top of the working surface above the floor. 750 dining, 400-450 coffee. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Height = 75.0;

	/** The top board. 25-30 is a faced ply top; 18 is a laminate one and looks it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double TopThickness = 3.0;

	/** Square section of each leg. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double LegSection = 7.0;

	/** How far a leg's outer face stands in from the edge of the top above it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double LegInset = 8.0;

	/**
	 * Depth of the rail running round under the top, between the legs. Zero leaves it off.
	 *
	 * It is what stops a table reading as a slab balanced on four sticks, and it is also the thing
	 * that eats knee room - see KneeClearance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double ApronDepth = 6.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double ApronThickness = 2.2;

	/** How far the apron is set back inside the legs' outer faces - the shadow that separates them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double ApronSetback = 1.5;

	/**
	 * Top of a lower shelf between the legs, above the floor. Zero leaves it off.
	 *
	 * A coffee table has one and a dining table does not, and that is the whole difference between
	 * the two objects once the proportions are set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double ShelfTopZ = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double ShelfThickness = 2.0;

	/** Radius rolled onto the top's edges. A pencil round is 2-3 mm; a softened edge is 8-10. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Softness", meta = (ClampMin = "0.0"))
	double EdgeRoll = 0.8;

	/** Radius rolled onto a leg's arrises. Small, and the reason a leg does not read as a stick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Softness", meta = (ClampMin = "0.0"))
	double LegRoll = 0.8;

	/** Underside of the top, which is what the legs and the apron run up to. */
	double TopUnderZ() const { return FMath::Max(Height - TopThickness, 0.0); }

	/**
	 * Clear height under the apron. THE MEASUREMENT THAT DECIDES WHETHER ANYBODY CAN SIT AT IT.
	 *
	 * 650 is what a knee needs at a dining table and 620 is what a rail one section too deep leaves.
	 * Neither figure is on a drawing, which is why this is asserted rather than assumed.
	 */
	double KneeClearance() const { return FMath::Max(TopUnderZ() - ApronDepth, 0.0); }

	/** Centre-to-centre span of the legs across the table. */
	double LegSpanX() const { return FMath::Max(Width - 2.0 * LegInset - LegSection, 0.0); }

	double LegSpanY() const { return FMath::Max(Depth - 2.0 * LegInset - LegSection, 0.0); }

	/** The drawn box IS the object: nothing on a table stands above its own top. */
	double BuiltHeight() const { return Height; }

	bool IsValid() const
	{
		return Width > 0.0 && Depth > 0.0 && TopThickness > 0.0 && TopUnderZ() > 0.0
			&& LegSection > 0.0 && LegSpanX() > 0.0 && LegSpanY() > 0.0;
	}
};

/**
 * A dining chair: four legs, a seat with a pad on it, and a raked back between two stiles.
 *
 * ## Why the chairs exist at all
 *
 * The reference drawing marks a four-seater dining table and no chairs, which is normal - a plan
 * shows the table because the table is what has to fit, and takes the chairs as read. Built that
 * way, the flat has a dining table nobody can sit at, and the question the room actually has to
 * answer - can a chair be pulled out without hitting the sofa - has nothing in it to ask about.
 * So the seating the table implies is declared, and the clearance round it is measured.
 *
 * ## Frame
 *
 * Centimetres, origin at the CENTRE of the drawn footprint in plan, on the floor. Local +Y runs
 * BACK, so a chair at zero yaw faces -Y and FHFFixturePlacement::FreeStanding turns it to face
 * whichever side of the table it is on.
 *
 * ## The rake is the chair
 *
 * A back that stands vertically is a bench end. The two rear stiles run straight to the seat and
 * lean back above it, and the drawn depth is the envelope of the whole lean - so a chair pulled out
 * from a table sweeps exactly the footprint it was declared with, which is what makes the clearance
 * check in the living room mean anything.
 *
 * ## What moves: nothing
 *
 * A chair is MOVED, which is not the same as articulated: it has no hinge, no travel limit and no
 * part that opens. Pulling one out is a fact about where it stands, so it is measured as a clearance
 * in the composing layer rather than modelled as an articulation here.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFChairParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 45.0;

	/** Front of the front legs to the back of the stiles at their top, overall. Includes the rake. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 48.0;

	/** Top of the back. 850-950 is a dining chair; below 800 it is a low-back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Height = 85.0;

	/** Top of the seat pad above the floor. 450 is the figure, and it is not a matter of taste. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double SeatHeight = 45.0;

	/** The seat board under the pad. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double SeatThickness = 3.0;

	/** The upholstered pad on the seat board, in Fabric. Zero leaves a bare timber seat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double CushionThickness = 5.0;

	/** Square section of the legs and the back stiles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double LegSection = 4.0;

	/** How far a leg's outer face stands in from the edge of the seat above it, across the chair. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double LegInset = 1.5;

	/** How far the top of the back leans back, measured from the seat. See the header. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BackRake = 4.0;

	/** Clear gap between the seat and the bottom of the back rest. What makes a back read as a back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BackRestGap = 15.0;

	/** How far the top of the back rest stops below the top of the stiles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BackRestReveal = 4.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BackRestThickness = 2.5;

	/** Radius rolled onto the seat pad. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Softness", meta = (ClampMin = "0.0"))
	double CushionRoll = 2.0;

	/** Radius rolled onto the timber arrises. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Softness", meta = (ClampMin = "0.0"))
	double TimberRoll = 0.6;

	/** Front to back of the seat itself: the drawn depth less what the lean takes. */
	double SeatDepth() const { return FMath::Max(Depth - BackRake, 0.0); }

	/** Underside of the seat board, which is where the legs stop. */
	double SeatUnderZ() const { return FMath::Max(SeatHeight - CushionThickness - SeatThickness, 0.0); }

	/** The drawn box IS the object: the top of the stiles is the top of the chair. */
	double BuiltHeight() const { return Height; }

	bool IsValid() const
	{
		return Width > 2.0 * LegSection && SeatDepth() > 2.0 * LegSection
			&& SeatUnderZ() > 0.0 && Height > SeatHeight + BackRestGap + BackRestReveal;
	}
};

/** A composed frame. Plain data carrying meshes by value. */
struct HOUSEFORGE_API FHFFrameBuild
{
	UE::Geometry::FDynamicMesh3 Shell;
	TArray<FHFMeshPart> Parts;
	bool bValid = false;
};

/**
 * A composed table, with the top kept apart from what holds it up.
 *
 * Separate for the reason every build struct here keeps its sub-assemblies: the clearance UNDER the
 * top is the measurement that decides whether the thing is a table, and a clearance between two
 * solids stops being answerable once they are one mesh.
 */
struct HOUSEFORGE_API FHFTableBuild
{
	UE::Geometry::FDynamicMesh3 Shell;

	/** The faced top, in ShutterLaminate. */
	UE::Geometry::FDynamicMesh3 Top;

	/** Four legs, in JoineryCarcass. */
	UE::Geometry::FDynamicMesh3 Legs;

	/** The rail running round under the top. Empty when ApronDepth is zero. */
	UE::Geometry::FDynamicMesh3 Apron;

	/** The lower shelf. Empty when ShelfTopZ is zero. */
	UE::Geometry::FDynamicMesh3 Shelf;

	FHFTableParams Used;
	bool bValid = false;
};

/** A composed chair, with the pad kept apart from the frame it sits on. */
struct HOUSEFORGE_API FHFChairBuild
{
	UE::Geometry::FDynamicMesh3 Shell;

	/** Legs, stiles and the seat board, in JoineryCarcass. */
	UE::Geometry::FDynamicMesh3 Frame;

	/** The upholstered pad, in Fabric. */
	UE::Geometry::FDynamicMesh3 Cushion;

	/** The raked panel between the stiles, in ShutterLaminate. */
	UE::Geometry::FDynamicMesh3 BackRest;

	FHFChairParams Used;
	bool bValid = false;
};

class HOUSEFORGE_API FHFFrameKit
{
public:
	static FHFTowelRailParams SanitiseTowelRail(const FHFTowelRailParams& Params);

	/** Two flanges, two stems and the rail between them. Nothing moves; see FHFTowelRailParams. */
	static FHFFrameBuild BuildTowelRail(const FHFTowelRailParams& Params);

	static FHFTableParams SanitiseTable(const FHFTableParams& Params);

	/**
	 * Top, four legs, an apron and an optional lower shelf.
	 *
	 * @return A build with bValid false and empty meshes when the parameters describe no table.
	 */
	static FHFTableBuild BuildTable(const FHFTableParams& Params);

	static FHFChairParams SanitiseChair(const FHFChairParams& Params);

	/**
	 * Four legs, a seat board with a pad on it, and a raked back between the rear stiles.
	 *
	 * @return A build with bValid false and empty meshes when the parameters describe no chair.
	 */
	static FHFChairBuild BuildChair(const FHFChairParams& Params);
};
