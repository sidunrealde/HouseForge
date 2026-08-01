// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Model/HFBuildDefaults.h"
#include "Model/HFTypes.h"
#include "HFCounterKit.generated.h"

/**
 * A hole through a counter, in the counter's own local frame.
 *
 * THE ONE PLACE A FIXTURE LEGITIMATELY KNOWS ABOUT ANOTHER FIXTURE, and it is a value rather than a
 * lookup. A sink and a hob are set INTO a counter, so the counter has to be cut for them - but a
 * generator may not go looking for the rest of the house, and a counter that searched for sinks
 * would be exactly that. So the composing layer resolves which set-in fixtures land on which
 * counter, converts each one into that counter's own frame, and hands the list over on the params.
 *
 * Identical in shape to the way a wall is handed the duct opening an extract needs cored through it
 * - see AHFFanActor::DuctOpeningFor - and for the same reason.
 *
 * ## The inset is the point
 *
 * The hole is NOT the appliance's footprint. A top-mounted sink and a drop-in hob both sit on a rim
 * that laps the cut edge: an 800 x 450 sink drops through a 770 x 420 hole and a 580 x 500 hob
 * through 560 x 480. Cutting the footprint itself leaves the appliance resting on nothing, with a
 * millimetre-wide slot of daylight all the way round it that reads instantly as wrong in any lit
 * shot. The composing layer applies the inset, because the inset is a property of the appliance.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFCounterAperture
{
	GENERATED_BODY()

	/** Centre of the hole in counter-local centimetres: +X along the run, +Y back into the counter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FVector2D Centre = FVector2D::ZeroVector;

	/** Size of the CUT, already inset from whatever appliance drops through it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FVector2D Size = FVector2D(50.0, 40.0);

	/** Which fixture asked for it, so a build report can say what each hole is for. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FName FixtureId;

	bool IsValid() const { return Size.X > 0.0 && Size.Y > 0.0; }
};

/** How the front edge of the slab is worked. */
UENUM(BlueprintType)
enum class EHFCounterEdge : uint8
{
	/** Sawn and polished square, with only the render chamfer on its arrises. */
	Square,

	/**
	 * The commonest worked granite edge in an Indian kitchen: the front face eased to a half-round.
	 *
	 * Approximated by a stepped profile rather than a true arc, because the swept section is what
	 * makes it cheap - see FHFMeshOps::AppendExtrudedSection.
	 */
	Bullnose,

	/**
	 * Square, with a drip groove routed into the underside a little back from the edge.
	 *
	 * What stops water running along the underside of the slab and down the shutter faces, and the
	 * detail that most makes a generated counter read as a real one: it is a hard shadow line
	 * running the whole length of the run, just under a highlight.
	 */
	DripGroove
};

/**
 * A worktop: a slab on a run of base units, with an upstand behind it and holes cut through it.
 *
 * ## Frame
 *
 * Centimetres, in the counter's own local space, and it is the SAME datum every cased good uses so
 * that a counter and the run under it are set out from one corner: the origin lies at the front-left
 * corner of the drawn footprint, +X runs along the run, +Y runs back towards the wall, +Z is up, and
 * Z = 0 is the UNDERSIDE of the slab.
 *
 * Y = 0 is the front of the DRAWN footprint, which is the front of the carcass beneath. The slab
 * itself starts in front of that, at Y = -Overhang, because granite oversails the doors it covers.
 *
 * ## What it is not
 *
 * Not a cased good. It has no carcass, no bays and no fronts, and nothing about it moves - the
 * things set INTO it are their own fixtures with their own articulation. See
 * .claude/rules/04-conventions.md: a slab of stone is one of the few things in this flat that
 * genuinely does not move, and that is stated rather than left as an omission.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFCounterParams
{
	GENERATED_BODY()

	/** Length of the run, along +X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 240.0;

	/** Front of the drawn footprint to the wall behind, along +Y. The overhang is extra. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 60.0;

	/**
	 * Total build-up: 18-20 of granite on 18 of ply, so 4 cm.
	 *
	 * Modelled as one solid because the ply is never on show - it is behind the edge profile on the
	 * front and buried in the carcass everywhere else.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Thickness = 4.0;

	/**
	 * How far the slab stands proud of the front of the drawn footprint.
	 *
	 * MEASURED PAST THE DOORS, NOT PAST THE CARCASS, and that is the whole difficulty of the figure.
	 * The carcass front plane is Y = 0 and its shutters hang in FRONT of it at negative Y, so a slab
	 * flush with Y = 0 finishes 20 mm BEHIND the doors it is supposed to cover - the one arrangement
	 * that exists nowhere and reads as wrong immediately, because the door tops catch the light where
	 * the stone should. Resolved by the composing layer against the same joinery figures the shutter
	 * is built from, so the two cannot drift.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Overhang = 2.5;

	/** Splashback standing on the slab at the wall. Zero for none. 10 cm is the Indian standard. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double UpstandHeight = 10.0;

	/** Thickness of the upstand, out from the wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double UpstandThickness = 2.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions")
	EHFCounterEdge Edge = EHFCounterEdge::DripGroove;

	/** Holes through the slab, already in this counter's frame and already inset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions")
	TArray<FHFCounterAperture> Apertures;

	/** Front face of the slab: in front of the drawn footprint by the overhang. Negative. */
	double FrontY() const { return -FMath::Max(Overhang, 0.0); }

	/** Overall depth of stone actually cut, overhang included. */
	double SlabDepth() const { return Depth - FrontY(); }

	/** Top of the slab, which is the working surface everything set into it is levelled to. */
	double TopZ() const { return FMath::Max(Thickness, 0.0); }

	bool IsValid() const { return Width > 0.0 && SlabDepth() > 0.0 && Thickness > 0.0; }
};

/** A composed worktop. Plain data, like every other build result in the kit. */
struct HOUSEFORGE_API FHFCounterBuild
{
	/** Slab, upstand and edge profile, merged, in counter-local space. */
	UE::Geometry::FDynamicMesh3 Shell;

	/** Holes that were actually cut, which is not always every hole that was asked for. */
	TArray<FHFCounterAperture> CutApertures;

	/** The parameters actually used, after clamping. */
	FHFCounterParams Used;

	bool bValid = false;
};

/**
 * Composing a worktop.
 *
 * Pure: parameters in, mesh out, no world, no actor, no editor, no settings object - see
 * .claude/rules/04-conventions.md.
 */
class HOUSEFORGE_API FHFCounterKit
{
public:
	/** The parameters actually used, clamped so the slab they describe can be cut. */
	static FHFCounterParams Sanitise(const FHFCounterParams& Params);

	/**
	 * The whole worktop: slab, edge, upstand, and every aperture cut through it.
	 *
	 * @return A build with bValid false and an empty mesh when the parameters describe no counter.
	 */
	static FHFCounterBuild Build(const FHFCounterParams& Params);

	/**
	 * How far a cut edge is held back from the appliance that drops through it, per side.
	 *
	 * 1.5 cm, which is the lap a pressed steel sink rim and a hob's glass flange both have. Kept here
	 * rather than on the appliance because it is a property of the JOINT between the two, and the
	 * composing layer needs it before either of them has been built.
	 */
	static constexpr double ApertureRimLap = 1.5;

	/**
	 * Stone left between a hole and the edge of the slab, or between two holes.
	 *
	 * Below about 5 cm a granite worktop cracks along the short grain at the corner of a cutout, which
	 * is why a sink is never cut hard against the front edge. An aperture that cannot keep this is
	 * refused rather than cut, and refusing is the honest answer: a slab with a hole through its front
	 * edge is not a worktop, and it would look exactly like one from above.
	 */
	static constexpr double MinApertureMargin = 5.0;
};
