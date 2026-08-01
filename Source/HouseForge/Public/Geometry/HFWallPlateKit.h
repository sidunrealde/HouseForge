// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Model/HFArticulation.h"
#include "Model/HFTypes.h"
#include "HFWallPlateKit.generated.h"

/**
 * Thin things fixed flat to a finished wall face.
 *
 * ## Why this is a kit and not a mirror
 *
 * A mirror is the first of nineteen instances that are all the same construction problem: something
 * a few centimetres thick, screwed to plaster, whose whole reading is its EDGE. A socket, a switch
 * plate, a distribution board and a pelmet are the rest of them, and they share the two things that
 * are hard about the type - a back that must land exactly on the wall face rather than near it, and
 * a front whose profile is the only thing that separates a real fitting from a decal.
 *
 * The mirror is what it starts with because the sanitary group needs it. See
 * FHFFixturePlacement::OnWallFace for the other half of the problem, which is not this kit's.
 *
 * Pure - parameters in, meshes out, no world, no actor, no editor, no settings object. See
 * .claude/rules/04-conventions.md.
 */

/**
 * A bathroom mirror: a bevelled glass plate on a backing board.
 *
 * ## Frame
 *
 * Centimetres, origin at the CENTRE of the drawn footprint in plan, at the BOTTOM of the plate,
 * +Y running BACK into the wall. Z = 0 is the bottom edge, so a mirror placed at BaseZ 1000 has its
 * bottom edge exactly there - which is the figure a drawing gives and the one that has to agree with
 * the basin below it.
 *
 * ## The bevel is the whole fitting
 *
 * A frameless mirror is a rectangle of silvered glass, and there is nothing about a rectangle of
 * silvered glass for light to catch: rendered as a flat plate it is a grey panel that reflects the
 * room and reads as a hole in the wall. What makes a real one read is the 15-20 mm polished BEVEL
 * round its edge, which is a second surface at a shallow angle and therefore a bright line all the
 * way round under any lighting at all. So the glass is a lofted solid - a smaller front face over a
 * full-size back one - rather than a box.
 *
 * A 30 mm build-up is a frameless mirror. There is no room in it for a frame and none is built; a
 * mirror CABINET would be a different fixture with a door, and this spec does not declare one.
 *
 * ## What moves
 *
 * Nothing, and it is said out loud. A mirror cabinet's door opens; a 600 x 30 x 800 plate on a wall
 * has no moving part, and inventing one would be a mechanism built to satisfy a rule rather than to
 * match an object. See .claude/rules/04-conventions.md.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFMirrorParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 60.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Height = 80.0;

	/** Whole build-up, wall face to the front of the glass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 3.0;

	/** Silvered plate. 5-6 mm is what a mirror this size is cut from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double GlassThickness = 0.6;

	/** The polished band round the edge, measured across the face. Zero for a plain cut edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BevelWidth = 1.8;

	/**
	 * Backing board the glass is bonded to: WHATEVER THE DEPTH HAS LEFT, and not a figure of its own.
	 *
	 * A drawing gives a mirror one dimension in this direction - 30 mm - and that figure is the whole
	 * build-up standing off the plaster, because it is the only part of it anybody can measure. Given
	 * its own thickness the backing has no reason to add up to that: a 12 mm board behind 6 mm of
	 * glass leaves the fitting 12 mm shy of its drawn front, which is a mirror hanging in a recess it
	 * does not have.
	 */
	double BackingThickness() const { return FMath::Max(Depth - GlassThickness, 0.0); }

	bool IsValid() const { return Width > 0.0 && Height > 0.0 && GlassThickness > 0.0; }
};

/** A composed wall plate. Plain data carrying meshes by value. */
struct HOUSEFORGE_API FHFWallPlateBuild
{
	UE::Geometry::FDynamicMesh3 Shell;
	TArray<FHFMeshPart> Parts;
	bool bValid = false;
};

class HOUSEFORGE_API FHFWallPlateKit
{
public:
	static FHFMirrorParams SanitiseMirror(const FHFMirrorParams& Params);

	/** Backing board and bevelled glass. Nothing moves; see FHFMirrorParams. */
	static FHFWallPlateBuild BuildMirror(const FHFMirrorParams& Params);
};
