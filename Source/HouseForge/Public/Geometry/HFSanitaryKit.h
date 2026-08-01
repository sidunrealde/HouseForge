// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Model/HFArticulation.h"
#include "Model/HFTypes.h"
#include "HFSanitaryKit.generated.h"

/**
 * A tap: a body, a spout that swings, and a lever that lifts.
 *
 * BOTH OF ITS MOVING PARTS ARE REAL ONES. A monobloc mixer's lever lifts to turn the water on and
 * swings side to side for temperature, and its spout swivels on the body so a bowl can be filled
 * from either side of a double sink. If a real one moves, the generated one moves - see
 * .claude/rules/04-conventions.md - and a tap is the smallest thing in the flat where that rule
 * still obviously applies.
 *
 * Revolved rather than boxed. A tap is the one fitting in a kitchen that is unambiguously round, and
 * a square one reads as a placeholder from across the room however well it is proportioned.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFTapParams
{
	GENERATED_BODY()

	/** Height of the body from the surface it is mounted on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double BodyHeight = 22.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double BodyRadius = 2.2;

	/** How far the spout reaches out over the bowl. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double SpoutReach = 20.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double SpoutRadius = 1.3;

	/** Length of the operating lever. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double LeverLength = 9.0;

	/** How far the lever lifts, fully open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double LeverLiftDegrees = 30.0;

	/** How far the spout swings to each side of centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double SpoutSwivelDegrees = 90.0;

	bool IsValid() const { return BodyHeight > 0.0 && BodyRadius > 0.0; }
};

/**
 * A sink: a rim, one or two bowls hanging below it, an optional drainer, and a tap.
 *
 * ## Frame
 *
 * Centimetres, origin at the CENTRE of the drawn footprint, and Z = 0 IS THE RIM - which is the
 * finished top of the counter it is set into. The bowls therefore hang at negative Z and the tap
 * stands at positive Z, which is exactly how a sink is dimensioned on site: everything is measured
 * from the worktop, because the worktop is the only level surface involved.
 *
 * The centre rather than a corner, because a sink is symmetric about its middle and so is the hole
 * it drops through - see FHFFixturePlacement::OnSurface.
 *
 * ## What moves and what does not
 *
 * The BOWL DOES NOT MOVE, and that is stated so it is not read as an oversight. Only the tap moves:
 * its lever lifts and its spout swivels, each on its own part with its own pivot.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFSinkParams
{
	GENERATED_BODY()

	/** Length of the rim, along +X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 80.0;

	/** Front to back, along +Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 45.0;

	/** Rim to the inside of the bowl's base. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BowlDepth = 20.0;

	/**
	 * Bowls across the width. Two is the standard Indian kitchen sink.
	 *
	 * A second bowl is not a second sink: both hang from one pressed rim, which is why this is a
	 * count here rather than two fixtures in the spec.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "1", ClampMax = "3"))
	int32 BowlCount = 2;

	/** Pressed stainless rim, 1.2 mm of steel folded to about 3. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double RimThickness = 0.3;

	/** Flat of rim between the bowls and around them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double RimWidth = 3.5;

	/** Radius the bowl's vertical corners are pressed to. A square-cornered bowl is not pressable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BowlCornerRadius = 3.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Tap")
	bool bHasTap = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Tap", meta = (ShowOnlyInnerProperties))
	FHFTapParams Tap;

	bool IsValid() const { return Width > 0.0 && Depth > 0.0 && BowlDepth > 0.0; }
};

/** A composed sink. Plain data carrying meshes by value, like every other build result. */
struct HOUSEFORGE_API FHFSinkBuild
{
	/** Rim, bowls and tap body, merged, in sink-local space. */
	UE::Geometry::FDynamicMesh3 Shell;

	/** The lever and the spout, each in its own local space with its pivot on the origin. */
	TArray<FHFMeshPart> Parts;

	/** Clear volume inside every bowl, for a caller that has to prove a bowl is hollow. */
	double BowlVolume = 0.0;

	FHFSinkParams Used;

	bool bValid = false;
};

/**
 * Sanitaryware: revolved and pressed forms rather than boxes.
 *
 * Pure - parameters in, meshes out, no world, no actor, no editor, no settings object. See
 * .claude/rules/04-conventions.md.
 */
class HOUSEFORGE_API FHFSanitaryKit
{
public:
	static FHFSinkParams SanitiseSink(const FHFSinkParams& Params);

	/** The whole sink: rim, bowls, tap, and a part for each of the tap's two movements. */
	static FHFSinkBuild BuildSink(const FHFSinkParams& Params);

	/** Part id of the tap's operating lever. */
	static FName TapLeverPartId() { return TEXT("TapLever"); }

	/** Part id of the swivelling spout. */
	static FName TapSpoutPartId() { return TEXT("TapSpout"); }
};
