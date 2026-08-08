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
 * A WC: a pedestal, a lofted pan, a close-coupled cistern, and a seat and lid that both lift.
 *
 * ## Frame
 *
 * Centimetres, origin at the CENTRE of the drawn footprint on the FLOOR, +Y running BACK towards the
 * wall, +X across. The centre rather than a corner because a WC is symmetric about its own
 * centreline and is set out from it on site - the soil connection is on that line, and so is
 * everything a plan dimensions about the fitting.
 *
 * ## What is approximated, said plainly
 *
 * The pan is a lofted ceramic form - a stack of rounded rectangles drawing in and forward from the
 * rim to the foot - and it is HOLLOW, with a real wall thickness and a floor. What it is not is a
 * trap: the waterway, the siphon and the standing water are one flat ceramic floor at the bottom of
 * the bowl. That is a deliberate simplification and it is invisible from anywhere a person stands,
 * because the only view of it is straight down into a bowl whose lid is usually shut. Modelling a
 * real S-trap would be a self-intersecting tube inside a solid nobody can see.
 *
 * ## What the drawing says and what is actually built
 *
 * The reference flat draws both WCs 380 x 600 x 400 standing on the floor, and labels them
 * "wall-hung". The DRAWN BOX is what is built to - a floor-standing close-coupled pan whose seat top
 * is at 400, which is the standard Indian figure - because a wall-hung pan is a fitting whose
 * cistern is buried in a duct that this plan does not have, and whose box would start at 200 rather
 * than at 0. A 400-high box standing on the floor describes one object and only one. The label is
 * noted rather than obeyed; see BuiltHeight for the other half of that, since the cistern stands
 * well above the drawn 400.
 *
 * ## What moves
 *
 * The lid, the seat, and the flush button. The seat is SEQUENCED AFTER the lid at a threshold of
 * zero, which makes its angle track the lid's exactly: a seat cannot rise through a closed lid, and
 * a lid can perfectly well be lifted on its own.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFWCParams
{
	GENERATED_BODY()

	/** Across the pan. 360-380 is every close-coupled WC sold in India. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 38.0;

	/** Overall projection from the wall, cistern included. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Projection = 60.0;

	/**
	 * Floor to the top of the SEAT. 400 in India, 450 where somebody has asked for comfort height.
	 *
	 * The figure a plan dimensions a WC by, and the one the pan is built down from rather than up to.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double SeatHeight = 40.0;

	/** Front to back of the cistern, taken out of the projection at the wall end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double CisternDepth = 18.0;

	/** Cistern lid above the seat. A close-coupled cistern stands about 380 over the pan. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double CisternHeight = 38.0;

	/** How far the cistern is narrower than the pan, each side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double CisternInset = 1.0;

	/** Ceramic wall of the pan. A vitreous china casting is 8-12 mm through. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double CeramicThickness = 1.0;

	/** Seat and lid board. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double SeatThickness = 1.6;

	/**
	 * How far the seat and lid swing up. 100 leans them back onto the cistern, which is where they stop.
	 *
	 * Past vertical on purpose: a seat that stops at 90 stands balanced on its hinge and falls the
	 * moment anything touches it, and every real seat is designed to go over centre and rest.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions",
		meta = (ClampMin = "0.0", ClampMax = "170.0"))
	double LidLiftDegrees = 100.0;

	/** How far the dual-flush plate presses in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double FlushButtonTravel = 0.8;

	/** Rim to the inside of the pan's floor - how deep the bowl actually is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BowlDepth = 22.0;

	/**
	 * Flat of the pan's rim, all round the opening. Not the same figure as the ceramic wall.
	 *
	 * A WC's rim is 35-45 mm across because it is a HOLLOW FLUSHING CHANNEL rather than the top edge
	 * of a wall, and it is what the seat lands on. Built at the wall thickness instead, the opening
	 * comes within 10 mm of the outside of the pan and the seat overhangs the bowl by nothing at all -
	 * a WC whose seat ring is visibly narrower than its hole.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double RimWidth = 3.5;

	/** Length of the pan itself: what is left of the projection once the cistern has had its share. */
	double PanLength() const { return Projection - FMath::Max(CisternDepth, 0.0); }

	/** Top of the pan's rim. The seat sits ON it, so the seat's top is SeatHeight exactly. */
	double RimZ() const { return SeatHeight - SeatThickness; }

	/**
	 * Overall height of what is BUILT, which is not what is drawn.
	 *
	 * A plan dimensions a WC to its seat, at 400, because that is the figure that has to agree with
	 * everything else in the room. The object standing there is 764 to the top of its cistern - the
	 * same "drawn box is not the object" the bed's headboard and the wall unit's cornice both are, and
	 * supplied for the same reason: FHFCeilingFit takes the built envelope.
	 */
	double BuiltHeight() const
	{
		return FMath::Max(RimZ() + FMath::Max(CisternHeight, 0.0), SeatHeight + SeatThickness);
	}

	bool IsValid() const { return Width > 0.0 && PanLength() > 0.0 && SeatHeight > 0.0; }
};

/** How a basin is carried. */
UENUM(BlueprintType)
enum class EHFBasinMount : uint8
{
	/**
	 * Standing on a counter with nothing of its own below: a vessel basin.
	 *
	 * What the master bathroom has. The vanity's stone is the surface, the basin sits on it, and the
	 * waste drops through a hole the composing layer does not have to cut for anything visible.
	 */
	CounterTop,

	/**
	 * Screwed to the wall on brackets, with a shroud over the trap: a half-pedestal basin.
	 *
	 * What the common bathroom has, because there is no vanity in it. Everything below the bowl is
	 * BELOW THE DRAWN BOX - the drawing dimensions the bowl and says nothing about what holds it up -
	 * so a basin built to the drawn box alone is a ceramic bowl floating at 800 with its waste pipe
	 * hanging in air. The shroud is what makes it a basin rather than a bowl.
	 */
	WallHung
};

/**
 * A wash basin: a lofted bowl on a tap ledge, with a tap on it and a waste through it.
 *
 * ## Frame
 *
 * Centimetres, origin at the CENTRE of the drawn footprint, and Z = 0 IS THE MOUNTING PLANE - the
 * finished top of the counter for a vessel basin, and the equivalent height for a wall-hung one. The
 * bowl therefore stands at positive Z in both cases and a shroud, where there is one, hangs below at
 * negative Z. One frame for both mounts is what lets the composing layer place either from the same
 * fixture without knowing which it has.
 *
 * ## The tap is on the basin, not behind it
 *
 * A tap ledge cast into the back of the basin, rather than a pillar tap standing on the counter
 * behind it. Both are real fittings; this one is the one that FITS. The master bathroom's basin is
 * 500 x 400 on a 900 x 500 vanity at the same centre, which leaves 50 mm of stone behind the bowl -
 * not enough for a tap of any kind, and a tap placed there anyway would stand through the splashback.
 * On the ledge it is inside the drawn footprint in both bathrooms.
 *
 * ## What moves
 *
 * The tap's lever and its spout. THE BOWL DOES NOT, and that is said out loud so it is not read as
 * an oversight - see the same note on FHFSinkParams.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFBasinParams
{
	GENERATED_BODY()

	/** Across the basin, along +X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 50.0;

	/** Front to back, along +Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 40.0;

	/** Mounting plane to the top of the rim. The drawn height of a vessel basin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Height = 18.0;

	/** How deep the water can stand: rim to the inside of the bowl's floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BowlDepth = 13.0;

	/** Flat of rim around the bowl at the front and sides. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double RimWidth = 4.0;

	/** Flat of rim at the BACK, where the tap stands. Wider than the rest, which is what a ledge is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double TapLedgeWidth = 9.0;

	/** Radius the bowl's plan corners are drawn to. A cast basin has no square corner anywhere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double CornerRadius = 8.0;

	/** Ceramic wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double CeramicThickness = 1.2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions")
	EHFBasinMount Mount = EHFBasinMount::CounterTop;

	/** How far a wall-hung basin's shroud and trap hang below the mounting plane. Ignored otherwise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double ShroudDrop = 26.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Tap")
	bool bHasTap = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Tap", meta = (ShowOnlyInnerProperties))
	FHFTapParams Tap;

	/** The lowest point of what is built, below the mounting plane. Zero or negative. */
	double BuiltBottomZ() const
	{
		return Mount == EHFBasinMount::WallHung ? -FMath::Max(ShroudDrop, 0.0) : 0.0;
	}

	/** Where the bowl's mouth stops and the tap ledge begins, in the basin's own frame. */
	double LedgeFrontY() const { return Depth * 0.5 - TapLedgeWidth; }

	/**
	 * Where the tap stands on the ledge - as far FORWARD on it as the bowl allows.
	 *
	 * NOT IN THE MIDDLE OF THE LEDGE, and the difference is the whole clearance a lever has. A
	 * monobloc's lever runs BACKWARDS from the top of its body, which is right on a kitchen sink
	 * standing in the middle of a worktop and wrong here: a basin's back is on the plaster, so a 70 mm
	 * lever on a tap centred in a 99 mm ledge ends 20 mm INSIDE the wall. Both of the flat's basins
	 * did, and nothing but a rendered elevation shows it - the fitting reads as correct from every
	 * angle except along the wall.
	 *
	 * Set forward, and the lever clamped to what is left behind it, a tap ledge does what a tap ledge
	 * is for.
	 */
	double TapBaseY() const { return LedgeFrontY() + Tap.BodyRadius * 1.6; }

	bool IsValid() const { return Width > 0.0 && Depth > 0.0 && Height > 0.0; }
};

/**
 * A shower: a mixer on the wall, a riser, an arm, a rose, a threshold and a floor gully.
 *
 * ## What the drawing declares, and what that means
 *
 * The reference flat marks each shower as a 900 x 900 AREA 2100 tall, not as a ShowerPartition. So
 * there is NO ENCLOSURE HERE and there is deliberately no glass door: the one big moving thing in a
 * shower is a door this spec does not have. What a wet area does have is a threshold that keeps the
 * water in and a gully that takes it away, and both are built - a shower reduced to a rose on a pipe
 * is a fitting hanging over an ordinary floor, and the floor is most of what makes the corner read
 * as a shower at all.
 *
 * ## Frame
 *
 * Centimetres, origin at the CENTRE of the drawn footprint on the FLOOR, +Y running BACK to the wall
 * the riser is fixed to.
 *
 * ## What moves
 *
 * The mixer's lever, and the rose on its ball joint.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFShowerParams
{
	GENERATED_BODY()

	/** Across the wet area, along +X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 90.0;

	/** Out from the wall, along +Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 90.0;

	/** Floor to the shower arm. The drawn height of the fitting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Height = 210.0;

	/** Centre of the mixer body above the floor. 1100 is where a bar mixer goes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double MixerHeight = 110.0;

	/** Distance between the mixer's two inlets - the width of the exposed bar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double MixerWidth = 19.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double MixerRadius = 2.4;

	/** Riser pipe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double RiserRadius = 1.2;

	/** How far the arm reaches out from the wall, which is what puts the rose over the standing area. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double ArmReach = 32.0;

	/** Overhead rose. 200 mm is the common one; 150 and 250 are both sold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double RoseDiameter = 20.0;

	/** How far the rose tilts each way on its ball. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions",
		meta = (ClampMin = "0.0", ClampMax = "60.0"))
	double RoseTiltDegrees = 25.0;

	/** How far the mixer's lever turns from shut to full. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions",
		meta = (ClampMin = "0.0", ClampMax = "170.0"))
	double LeverSweepDegrees = 90.0;

	/**
	 * A marble threshold along the open front of the wet area. Zero for none.
	 *
	 * The FRONT edge only, and that is a decision rather than an omission. A wet area is bounded on
	 * whichever sides happen to be open, the generator cannot know which those are, and a kerb run
	 * round all four sides would be a tray - a different fitting, and one that steps up out of a floor
	 * that is already there.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double ThresholdHeight = 2.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double ThresholdWidth = 5.0;

	/** Square floor gully, laid flush. Zero for none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double GullySize = 12.0;

	bool IsValid() const { return Width > 0.0 && Depth > 0.0 && Height > 0.0; }
};

/** A composed WC. Plain data carrying meshes by value, like every other build result. */
struct HOUSEFORGE_API FHFWCBuild
{
	/** Pedestal, pan and cistern, merged, in WC-local space. */
	UE::Geometry::FDynamicMesh3 Shell;

	/** Lid, seat and flush button, each in its own local space with its pivot on the origin. */
	TArray<FHFMeshPart> Parts;

	/** Clear volume inside the pan, for a caller that has to prove the bowl is hollow. */
	double BowlVolume = 0.0;

	/** Top of the pan's rim, which the seat sits on. */
	double RimZ = 0.0;

	/**
	 * How far the seat and lid actually swing, which is not always how far they were asked to.
	 *
	 * A CLOSE-COUPLED SEAT STOPS WHEN IT MEETS THE CISTERN, and that is a geometric fact rather than a
	 * setting. Asked for 100 degrees with an 18 cm cistern standing right behind the hinge, a seat
	 * sweeps its whole declared arc and spends the last eight degrees of it inside the cistern's front
	 * face - a movement that satisfies every assertion about travel while being impossible. So the
	 * request is clamped to the angle at which the leaf leans on the cistern, and the answer is
	 * reported rather than silently applied.
	 */
	double LidLiftDegrees = 0.0;

	FHFWCParams Used;

	bool bValid = false;
};

/** A composed basin. */
struct HOUSEFORGE_API FHFBasinBuild
{
	UE::Geometry::FDynamicMesh3 Shell;
	TArray<FHFMeshPart> Parts;

	double BowlVolume = 0.0;

	FHFBasinParams Used;

	bool bValid = false;
};

/** A composed shower. */
struct HOUSEFORGE_API FHFShowerBuild
{
	UE::Geometry::FDynamicMesh3 Shell;
	TArray<FHFMeshPart> Parts;

	/** Where the rose's spray face sits when it is not tilted, in shower-local space. */
	FVector RoseCentre = FVector::ZeroVector;

	FHFShowerParams Used;

	bool bValid = false;
};

/**
 * Sanitaryware: revolved, pressed and lofted forms rather than boxes.
 *
 * Pure - parameters in, meshes out, no world, no actor, no editor, no settings object. See
 * .claude/rules/04-conventions.md.
 */
class HOUSEFORGE_API FHFSanitaryKit
{
public:
	static FHFSinkParams SanitiseSink(const FHFSinkParams& Params);
	static FHFWCParams SanitiseWC(const FHFWCParams& Params);
	static FHFBasinParams SanitiseBasin(const FHFBasinParams& Params);
	static FHFShowerParams SanitiseShower(const FHFShowerParams& Params);

	/** The whole sink: rim, bowls, tap, and a part for each of the tap's two movements. */
	static FHFSinkBuild BuildSink(const FHFSinkParams& Params);

	/** The whole WC: pedestal, pan, cistern, and a part each for the lid, the seat and the button. */
	static FHFWCBuild BuildWC(const FHFWCParams& Params);

	/** The whole basin: bowl, ledge, waste, whatever carries it, and the tap's two parts. */
	static FHFBasinBuild BuildBasin(const FHFBasinParams& Params);

	/** The whole shower: gully, threshold, mixer, riser, arm and rose, with the lever and the tilt. */
	static FHFShowerBuild BuildShower(const FHFShowerParams& Params);

	/** Part id of the tap's operating lever. */
	static FName TapLeverPartId() { return TEXT("TapLever"); }

	/** Part id of the swivelling spout. */
	static FName TapSpoutPartId() { return TEXT("TapSpout"); }

	/** Part id of the WC's lid - the outer one, which lifts on its own. */
	static FName LidPartId() { return TEXT("Lid"); }

	/** Part id of the WC's seat, which may not rise past the lid above it. */
	static FName SeatPartId() { return TEXT("Seat"); }

	/** Part id of the WC's dual-flush plate. */
	static FName FlushButtonPartId() { return TEXT("FlushButton"); }

	/** Part id of the shower mixer's lever. */
	static FName MixerLeverPartId() { return TEXT("MixerLever"); }

	/** Part id of the shower rose, which tilts on its ball joint. */
	static FName RosePartId() { return TEXT("Rose"); }
};
