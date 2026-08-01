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
	Joinery,

	/**
	 * A column standing proud of the wall behind it. NOT AN END - the skirting returns round it.
	 *
	 * The one cause that adds skirting rather than removing it, and the reason it is a break at all
	 * is that the straight run along the wall genuinely does stop: it turns out along the column's
	 * flank, crosses its face and comes back. See FHFSkirtingPlan::Returns, which carries the three
	 * lengths that do the turning. A break of this cause without them is a column with a bare face.
	 */
	Structure
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
 * One length of skirting that is not on the room boundary - a return round something standing
 * proud of a wall.
 *
 * Held as a plan segment rather than a distance along an edge, because that is exactly what it is
 * not: the near flank of a column runs at right angles to the wall the run came in along.
 *
 * ROOM ON THE LEFT, which is the same convention the boundary already uses - a room polygon winds
 * anticlockwise, so the left normal of each edge points into the room. The skirting is fixed to the
 * face this segment lies on and stands off it to the left, and that one rule is what lets the
 * generator build a return exactly as it builds a run.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFSkirtingReturn
{
	GENERATED_BODY()

	/** On the face the skirting is fixed to, with the room to the left of Start -> End. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	FVector2D Start = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	FVector2D End = FVector2D::ZeroVector;

	/** The column this goes round. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	FName SourceId;

	double Length() const { return FVector2D::Distance(Start, End); }
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

	/** The lengths that leave the boundary to go round a column. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	TArray<FHFSkirtingReturn> Returns;

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

	/** Total length of the returns. Extra skirting, off the boundary, so it is counted separately. */
	double ReturnLength() const;
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
 * A fifth was only ever going to be found by rendering it, and was: eighteen places in the
 * reference flat where a 450 x 230 column stands between 58 and 168 mm proud of the plaster it
 * sits in. The run went straight into the concrete and came out the far side, so from the room it
 * stopped dead at the column and the column's three exposed faces were bare - which is the same
 * picture as a missing length and reads exactly like one. A skirting RETURNS round a column: out
 * along the near flank, across the face, back along the far one. See FHFSkirtingPlan::Returns.
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
	 *
	 * BEING SCRIBED IS NOT ENOUGH TO CUT A SKIRTING. It says what a carpenter would do if the unit
	 * were there; whether it IS there is a separate question, and it was the difference between a
	 * correct rule and 710 cm of the reference flat's perimeter with the board deleted and bare
	 * plaster behind it. Eight types answer true here and one of them - Wardrobe - is the only one
	 * the composing layer builds today, so a shoe rack, a study table and two runs of kitchen base
	 * units each took their length of skirting away and put nothing in front of it. That is the
	 * user's "skirting stops abruptly in the middle", and it is why For takes BuiltFixtureIds: a
	 * break has to be justified by geometry that will exist, not by a row in a spec.
	 */
	static bool IsScribedJoinery(EHFFixtureType Type);

	/** Every wall set out ON a given boundary edge: parallel, in the same line, and overlapping it. */
	static TArray<const FHFWall*> WallsOnEdge(const FVector2D& From, const FVector2D& To,
		const TArray<FHFWall>& Walls);

	/**
	 * How far a column stands proud of a wall face, and over what stretch of it.
	 *
	 * Measured in the edge's own frame, which is exact for a rectangular column set out square to
	 * the wall it sits in - every column in this domain - and conservative for one turned at an
	 * angle, where it answers for the footprint's extent rather than its true section.
	 *
	 * @return False when the column does not reach past the plaster into this edge at all.
	 */
	static bool ColumnProjectsInto(const FHFColumn& Column, const FVector2D& From, const FVector2D& To,
		double FaceInset, double& OutProjection, double& OutFrom, double& OutTo);

	/**
	 * The whole answer for one room.
	 *
	 * @param Columns Structure standing in the wall faces. A column proud of the plaster does not
	 *        end a skirting - it makes it turn - so these produce breaks AND returns.
	 * @param Fixtures May be the whole spec's list; anything in another room is ignored.
	 * @param BuiltFixtureIds Which of those fixtures the composing layer will actually put geometry
	 *        in the level for. See the note below. Null means "no information", and then every
	 *        scribed type cuts, which is the right answer for a caller that is not building a house.
	 */
	static FHFSkirtingPlan For(const FHFRoom& Room, const TArray<FHFWall>& Walls,
		const TArray<FHFOpening>& Openings, const TArray<FHFColumn>& Columns,
		const TArray<FHFFixture>& Fixtures,
		const FHFSkirtingParams& Params = FHFSkirtingParams(),
		const TSet<FName>* BuiltFixtureIds = nullptr);

	/** One line per break, for a build report. */
	static TArray<FString> Describe(const FHFRoom& Room, const FHFSkirtingPlan& Plan);
};
