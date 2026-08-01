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

/** A composed frame. Plain data carrying meshes by value. */
struct HOUSEFORGE_API FHFFrameBuild
{
	UE::Geometry::FDynamicMesh3 Shell;
	TArray<FHFMeshPart> Parts;
	bool bValid = false;
};

class HOUSEFORGE_API FHFFrameKit
{
public:
	static FHFTowelRailParams SanitiseTowelRail(const FHFTowelRailParams& Params);

	/** Two flanges, two stems and the rail between them. Nothing moves; see FHFTowelRailParams. */
	static FHFFrameBuild BuildTowelRail(const FHFTowelRailParams& Params);
};
