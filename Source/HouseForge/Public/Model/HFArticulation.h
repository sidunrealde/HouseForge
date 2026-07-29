// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "HFArticulation.generated.h"

/**
 * How a part moves.
 *
 * Deliberately only the two motions real joinery uses. A door swings, a drawer pulls out, a
 * wardrobe shutter swings, a sliding sash slides - everything in this domain is a hinge or a
 * slide, and nothing is served by making the framework general enough to express motions no
 * cabinet has.
 */
UENUM(BlueprintType)
enum class EHFMotionType : uint8
{
	/** Fixed. A part with no motion still gets its own component if it needs its own material. */
	None,
	/** Rotates about Axis, through the part's own origin. */
	Hinge,
	/** Translates along Axis. */
	Slide
};

/**
 * The motion one part is capable of.
 *
 * Axis and travel are expressed in the part's own local space, whose origin is the pivot - the
 * hinge line for a hinge, any point on the line of travel for a slide. That is what lets a
 * generator stay pure: it returns a part's mesh in that part's own space, and knows nothing about
 * where in the house the part ends up.
 *
 * Travel is signed. The sign is the direction of opening, so a left-hung and a right-hung shutter
 * differ only by the sign of MaxAngleDegrees rather than by a separate handedness flag.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFPartMotion
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFMotionType Type = EHFMotionType::None;

	/** Axis of rotation or direction of travel, in the part's local space. Normalised on use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FVector Axis = FVector::ZAxisVector;

	/** Rotation at OpenAmount 1, in degrees. Signed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (EditCondition = "Type == EHFMotionType::Hinge", ClampMin = "-180.0", ClampMax = "180.0"))
	double MaxAngleDegrees = 90.0;

	/** Travel at OpenAmount 1, in centimetres. Signed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (EditCondition = "Type == EHFMotionType::Slide"))
	double MaxTravelCm = 0.0;

	/**
	 * The part this one is geared to, if any. Empty for a part that moves on its own.
	 *
	 * A driven part takes the driver's open amount rather than carrying one of its own; the gearing
	 * is already expressed by its own travel. The case this exists for is the intermediate member of
	 * a full-extension drawer runner, which travels exactly half as far as the drawer it carries -
	 * and which cannot be left to a separate open amount, because a drawer pulled out while its
	 * intermediate stayed behind is a drawer hanging on nothing.
	 *
	 * It is a real motion, not a bookkeeping trick: the member slides, so it is its own component
	 * with its own pivot and travel limit, per .claude/rules/04-conventions.md.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	FName DrivenByPartId;

	bool Moves() const { return Type != EHFMotionType::None; }

	/** Unit axis, falling back to +Z rather than producing a degenerate rotation. */
	FVector UnitAxis() const;

	/**
	 * The offset a given open amount applies, in the part's own local space.
	 *
	 * Identity at 0, so a closed part sits exactly where its generator put it and the closed pose
	 * of an assembly is bit-identical to the same assembly generated as one fixed mesh.
	 */
	FTransform OffsetAt(double OpenAmount) const;

	/** Where a point given in the part's local space ends up at that open amount, still local. */
	FVector SweptLocalPoint(const FVector& LocalPoint, double OpenAmount) const;
};

/**
 * Live state of one moving part on an actor.
 *
 * Split from the generated description on purpose. Everything the generator decides - pivot,
 * motion, travel - is rebuilt on every regeneration, while OpenAmount and the hand-edited flag are
 * user state and survive it. A wardrobe whose shutters were left open to be photographed must not
 * snap shut because its carcass depth changed.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFPartState
{
	GENERATED_BODY()

	/** Stable across regeneration; this is how a part is matched back to itself. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	FName PartId;

	/** 0 fully closed, 1 at the travel limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "HouseForge",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	double OpenAmount = 0.0;

	/**
	 * True once this part's mesh has been modified outside of generation.
	 *
	 * Per part rather than per actor: hand-detailing one shutter must not freeze the other five,
	 * and it must not be silently overwritten when they rebuild.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	bool bArtistEdited = false;

	/** Closed pose, relative to the actor. Regenerated, shown so a pivot can be checked. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	FTransform PivotTransform;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	FHFPartMotion Motion;

	/** Relative transform this part's component should carry at the current open amount. */
	FTransform CurrentPose() const { return PoseAt(OpenAmount); }

	/** Relative transform this part's component would carry at a given open amount. */
	FTransform PoseAt(double InOpenAmount) const;
};

/**
 * How an articulated element was posed, lifted clear of the actor holding it.
 *
 * A house rebuild respawns its elements, so a pose held only on the actor dies with it and every
 * door in the flat slams shut. Posing is user state in exactly the way a hand edit is - someone
 * opened those doors on purpose, usually to photograph them - so it is captured by part id before
 * the rebuild and put back after it.
 */
struct HOUSEFORGE_API FHFPartPoses
{
	/** Only parts posed away from closed; a closed part has nothing worth carrying. */
	TMap<FName, double> OpenAmountsByPartId;

	double MasterOpenAmount = 0.0;

	bool IsEmpty() const { return OpenAmountsByPartId.IsEmpty() && MasterOpenAmount <= 0.0; }
};

/**
 * One moving part as produced by generation: a mesh plus where it hangs and how it moves.
 *
 * A plain struct rather than a USTRUCT because it carries an FDynamicMesh3 by value. It is still
 * pure data with no world or asset access, so a generator can hand back a whole articulated kit
 * without ever touching an actor - articulation is the actor's job, but describing it is not.
 *
 * Mesh is in the part's own local space with the origin on the pivot.
 */
struct HOUSEFORGE_API FHFMeshPart
{
	/** Unique within one actor. Survives regeneration and is what open amounts key off. */
	FName PartId;

	/** The part's geometry, in part-local space. Roles and UVs already applied. */
	UE::Geometry::FDynamicMesh3 Mesh;

	/** Where the part's origin sits, relative to the actor, when closed. */
	FTransform PivotTransform = FTransform::Identity;

	FHFPartMotion Motion;

	/** Open amount a freshly generated part starts at. Almost always 0. */
	double DefaultOpenAmount = 0.0;
};
