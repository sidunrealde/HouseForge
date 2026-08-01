// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Model/HFTypes.h"
#include "HFSkirtingPlan.generated.h"

/** Why a skirting stops. Every break has one, and a break without one is a bug. */
UENUM(BlueprintType)
enum class EHFSkirtingBreakCause : uint8
{
	/**
	 * A door, a sliding door or an archway in a wall of THIS room.
	 *
	 * The floor runs through, so the skirting cannot: it stops at each jamb and picks up on the far
	 * side. A window does not do this however low its sill, because the wall under a window is still
	 * a wall - only an opening you walk through interrupts a skirting.
	 */
	Doorway,

	/**
	 * Built-in joinery scribed to this wall and meeting the floor.
	 *
	 * A wardrobe, a run of kitchen base units, a shoe rack. The carpenter cuts the skirting out where
	 * the carcass lands and scribes the plinth to the plaster; leaving it in puts a 18 mm board
	 * through the back of every one of them.
	 */
	Joinery
};

/** One stretch of one boundary edge where the skirting stops, and what stopped it. */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFSkirtingBreak
{
	GENERATED_BODY()

	/** Index of the boundary edge, running FHFRoom::Boundary[EdgeIndex] to the next point. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	int32 EdgeIndex = 0;

	/** Distance along the edge from Boundary[EdgeIndex], already clamped into the edge. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	double Start = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	double End = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	EHFSkirtingBreakCause Cause = EHFSkirtingBreakCause::Doorway;

	/** The FHFOpening or FHFFixture that caused it. This is what makes a gap explainable. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	FName SourceId;

	double Length() const { return FMath::Max(0.0, End - Start); }
};

/** One surviving stretch of skirting, as a distance range along its edge. */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFSkirtingRun
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	double Start = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	double End = 0.0;

	double Length() const { return FMath::Max(0.0, End - Start); }
};

/** One edge of a room's boundary, with the skirting that survives along it. */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFSkirtingEdge
{
	GENERATED_BODY()

	/** Boundary[i] and Boundary[i+1] - CENTRELINE positions, not the wall face. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	FVector2D Start = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	FVector2D End = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	double Length = 0.0;

	/**
	 * How far the finished wall face stands in from this edge - half the thickness of the thickest
	 * wall set out on it, and zero where no wall is.
	 *
	 * A ROOM BOUNDARY IS A CENTRELINE. Laid on the boundary itself a skirting is laid down the middle
	 * of the masonry, which is where all seven of this flat's skirtings were until the plaster was
	 * found: declared, generated, watertight, correctly tagged, and invisible.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	double FaceInset = 0.0;

	/** In order along the edge, with every break already taken out. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	TArray<FHFSkirtingRun> Runs;
};

/**
 * Where a room's skirting runs and where it stops.
 *
 * The whole answer for one room, worked out once and then merely built. Holding the runs rather than
 * the inputs is what makes the result assertable: the coverage test can ask this object whether the
 * perimeter is accounted for without generating a single triangle.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFSkirtingPlan
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	TArray<FHFSkirtingEdge> Edges;

	/** Every break, over every edge, with its cause. Ordered by edge and then along the edge. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	TArray<FHFSkirtingBreak> Breaks;

	/**
	 * Section depth off the plaster, in centimetres.
	 *
	 * Held on the plan so the runs and the geometry cannot disagree about it: the joinery test asks
	 * whether a carcass lands inside this band, and the generator builds a box this thick.
	 *
	 * THE HEIGHT IS NOT HERE, deliberately. It stays on FHFRoom, which is where the drawing states
	 * it and where the details panel edits it - a copy of it on the plan would be a second source of
	 * truth that goes stale the moment somebody types a new one, which is the mistake
	 * FHFCeilingFit was written to stop making.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.1"))
	double Depth = 1.8;

	/** Total length of the room's boundary. */
	double BoundaryLength() const;

	/** Total length actually skirted. */
	double CoveredLength() const;

	/**
	 * Total length taken out by breaks.
	 *
	 * Measured from the RUNS rather than by adding the breaks up, so two breaks that overlap - a door
	 * beside a wardrobe - are counted once. BoundaryLength() == CoveredLength() + BreakLength() is
	 * therefore an identity, and the thing a coverage test can lean on.
	 */
	double BreakLength() const { return BoundaryLength() - CoveredLength(); }
};

/** Section figures for a skirting, resolved by the composing layer and handed down. */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFSkirtingParams
{
	GENERATED_BODY()

	/**
	 * How far the section stands off the plaster, in centimetres.
	 *
	 * 18 mm is a standard Indian hardwood or MDF skirting. Cheap PVC is 12; a deep moulded profile in
	 * a formal room is 25.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", ClampMax = "6.0", UIMin = "1.0", UIMax = "3.0"))
	double Depth = 1.8;

	/**
	 * Extra taken off each side of a doorway beyond the masonry opening itself, in centimetres.
	 *
	 * The frame sits INSIDE the masonry opening - see FHFDoorParams::FrameFace - so a skirting cut to
	 * the opening already stops at the jamb. This is the setting-out slack on top, which also keeps
	 * the skirting's end grain out of the same plane as the frame's side.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "3.0"))
	double JambClearance = 1.0;
};

/**
 * Working out where a room's skirting runs.
 *
 * ## What this exists to fix
 *
 * Skirting was buried in the masonry until the plaster offset landed, so this is the first build in
 * which anybody could see where it actually runs - and it stopped dead in the middle of walls with no
 * opening anywhere near them. Measured over the reference flat, 71.5% of the boundary was skirted and
 * the gaps were in the wrong places. Four separate faults, all in the composing layer:
 *
 *   1. ONE GAP WIDTH FOR THE WHOLE FLAT. The room actor was handed the widest door in the spec - the
 *      1800 balcony slider - and every doorway was cut that wide. A 750 bathroom door took 1800 of
 *      skirting with it, 525 of solid wall on each side of the frame.
 *
 *   2. EVERY DOORWAY IN THE FLAT, IN EVERY ROOM. The loop ran over the whole spec's openings with no
 *      test that the wall was one of this room's. A door was matched to an edge if its centre came
 *      within 300 mm of the edge's line, so any door in any collinear wall anywhere cut a hole. The
 *      common bathroom had four such gaps and not one of them was its own door.
 *
 *   3. GAPS THAT OVERHUNG THE EDGE. A doorway up to a full gap-width BEFORE the start of an edge
 *      still cut into it, so a door in the next room round the corner shortened this one's run.
 *
 *   4. NOTHING KNEW ABOUT JOINERY. A wardrobe scribed to the wall had a skirting running through the
 *      back of it, which is the opposite error and equally wrong.
 *
 * ## Why it is a resolver and not a generator
 *
 * Which walls are on a room's edges, which openings are in those walls, and which fixtures stand
 * against them are all questions about the SPEC. A generator may not go looking - see
 * .claude/rules/04-conventions.md - so the composing layer asks them here, once, and hands the answer
 * to FHFGenerators::GenerateFloor as a value.
 *
 * ## Purity and units
 *
 * Every function is a pure function of its arguments: no world, no actor, no settings. Whatever units
 * the caller works in are the units of every figure returned, exactly as FHFCeilingFit does.
 */
class HOUSEFORGE_API FHFSkirting
{
public:
	/**
	 * True for an opening you walk through, which is the only kind that interrupts a skirting.
	 *
	 * A window does not, however low its sill - the wall under it is still a wall and its skirting
	 * runs straight past. The sill test is what separates a French casement from a window seat.
	 */
	static bool IsDoorway(const FHFOpening& Opening);

	/**
	 * True for built-in joinery that is scribed to the wall it backs onto.
	 *
	 * A property of the type, not of the drawing, exactly as EHFCeilingFitRule is: no plan states
	 * that a wardrobe is scribed and a sofa is pushed up against the skirting. Loose furniture is
	 * absent on purpose - a bed, a sofa or a nightstand stands IN FRONT of a skirting that carries on
	 * behind it, and cutting one out for a bed would be a fault nobody could see until they moved it.
	 */
	static bool IsScribedJoinery(EHFFixtureType Type);

	/** Every wall set out ON a given boundary edge: parallel, in the same line, and overlapping it. */
	static TArray<const FHFWall*> WallsOnEdge(const FVector2D& From, const FVector2D& To,
		const TArray<FHFWall>& Walls);

	/**
	 * The whole answer for one room.
	 *
	 * @param Fixtures May be the whole spec's list; anything in another room is ignored.
	 */
	static FHFSkirtingPlan For(const FHFRoom& Room, const TArray<FHFWall>& Walls,
		const TArray<FHFOpening>& Openings, const TArray<FHFFixture>& Fixtures,
		const FHFSkirtingParams& Params = FHFSkirtingParams());

	/** One line per break, for a build report. */
	static TArray<FString> Describe(const FHFRoom& Room, const FHFSkirtingPlan& Plan);
};
