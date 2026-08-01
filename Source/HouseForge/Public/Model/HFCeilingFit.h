// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Model/HFTypes.h"
#include "HFCeilingFit.generated.h"

/**
 * What a thing does when the ceiling above it moves.
 *
 * EVERY FIXTURE HAS AN ANSWER TO THIS, and until now none of them had one. A false ceiling was a
 * plane somebody chose and everything else in the room was set out against the structural slab, so
 * the two only agreed when the drop happened to be small. Deepening a ceiling did not move anything;
 * it buried whatever was in the way. Seven fittings in the reference flat stood 30 mm inside their
 * own ceilings the day the depth model changed, and every one of them validated.
 *
 * The answer is a property of the OBJECT, not of the drawing, which is why it is derived from the
 * fixture type rather than authored: a bought extract cannot be made shorter and a wardrobe cannot be
 * lifted off the floor, and no drawing has an opinion about either. See FHFCeilingFit::RuleFor.
 */
UENUM(BlueprintType)
enum class EHFCeilingFitRule : uint8
{
	/**
	 * Nothing near the ceiling, or nothing this mechanism is entitled to move.
	 *
	 * A bed, a socket, a balcony railing. Also the honest answer for a bought floor-standing
	 * appliance - a refrigerator neither shortens nor sinks into the slab, so a ceiling that comes
	 * down onto one is a fault to be reported rather than a fit to be resolved.
	 */
	Ignores,

	/**
	 * Its datum is the FINISHED SOFFIT rather than the structural slab, so it follows the ceiling
	 * both ways.
	 *
	 * FHFFixture::BaseZ on a ceiling-mounted fixture is already measured DOWN from the ceiling - see
	 * FHFFixture::IsCeilingMounted - and "the ceiling" meant the slab. A surface-mounted light 10 cm
	 * below a 300 slab is 10 cm below the slab and 38 cm INSIDE a 480 false ceiling. This is the rule
	 * that makes the datum mean what the field already says it means.
	 */
	HangsFromSoffit,

	/**
	 * Reaches the slab through the ceiling on a rod that lengthens to suit.
	 *
	 * The ceiling fan, and only the ceiling fan. Its mechanism already exists and is correct -
	 * FHFGenerators::CeilingSoffitDropAt answers how far, AHFFanActor::ApplyCeilingAbove lengthens
	 * the rod and drops the canopy onto the soffit to cover the hole cut for it. Named here so the
	 * dependency set is in ONE place rather than in one place plus a special case somebody has to
	 * remember; the fit itself leaves such a fixture's BaseZ and Height exactly as drawn.
	 */
	HangsOnARod,

	/**
	 * A bought object fixed high on a wall. Keeps its size and slides DOWN, only as far as it must.
	 *
	 * An extract, a split AC head, a storage geyser, a chimney, a pelmet. None of them can be made
	 * smaller and all of them have somewhere lower to go, so the ceiling wins and the fitting moves.
	 * Never upward: a drawing put it at a height for a reason, and a shallower ceiling is not a
	 * reason to raise it.
	 */
	Lowers,

	/**
	 * Made on site, standing on the floor or hung off the wall. Keeps its base and LOSES HEIGHT.
	 *
	 * A wardrobe, a loft, a tall unit, a run of wall cabinets. This is what a carpenter does: the
	 * carcass is cut to the room it is going into. Lowering one instead would either sink it into the
	 * floor or leave a gap under a unit fixed to a wall.
	 */
	Shortens
};

/** What actually happened when a fixture was fitted to the ceiling over it. */
UENUM(BlueprintType)
enum class EHFCeilingFitAction : uint8
{
	/** It already cleared. The overwhelmingly common case, and the one a build report stays quiet about. */
	Unchanged,

	/** Re-hung: its datum is the soffit and the soffit moved. */
	Rehung,

	/** Slid down to get its head under the soffit. */
	Lowered,

	/** Cut down to get its head under the soffit. */
	Shortened,

	/**
	 * IT DOES NOT FIT AND NOTHING HERE CAN MAKE IT.
	 *
	 * The fixture is left exactly as the drawing put it - moving it somewhere equally wrong helps
	 * nobody - and the shortfall is reported. That is the point of the outcome existing: a ceiling
	 * dropped onto a 1.8 m refrigerator is a design fault, and a silent fudge would hide it.
	 */
	Refused
};

/** Where a fixture ended up once the ceiling above it was taken into account. */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFCeilingFitResult
{
	GENERATED_BODY()

	/** Which rule was applied, from the fixture's type. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	EHFCeilingFitRule Rule = EHFCeilingFitRule::Ignores;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	EHFCeilingFitAction Action = EHFCeilingFitAction::Unchanged;

	/**
	 * The LOWEST finished soffit anywhere over this fixture's footprint, in the room's own datum.
	 *
	 * Over the footprint and not at the centre. A pelmet is 2.2 m long and a perimeter band is 45 cm
	 * wide, so a fitting routinely spans a level change - and the one that matters is the lowest, not
	 * whichever happens to be over the middle of it. Equal to the structural slab when nothing covers
	 * the fixture at all.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	double SoffitZ = 0.0;

	/** The resolved base, in the fixture's own datum - above the room floor, or below the soffit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	double BaseZ = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	double Height = 0.0;

	/** How far it moved or how much it lost, in spec units. Zero when it was already clear. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	double Adjustment = 0.0;

	/** How much it still could not give, when Refused. Zero otherwise. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	double Shortfall = 0.0;

	bool Moved() const { return Action != EHFCeilingFitAction::Unchanged; }
};

/**
 * Fitting everything in a room to the ceiling that ends up over it.
 *
 * ## Why this is recomputed and not declared
 *
 * A false ceiling in this plugin is DERIVED: a drawing names a template, the project's settings say
 * what that template's figures are, and the beams over the room decide the perimeter ring. Not one
 * of those three is a fact the drawing states, and all three can change without the spec being
 * touched - dragging a slider on the settings page re-stamps every templated ceiling in the flat.
 *
 * So a fixture height declared against a ceiling would be a copy of a number the user can change at
 * any moment, and it would be stale the instant they did. That is not a hypothetical: it is exactly
 * how the ceiling fan's rod came to build a rotor inside the plasterboard, and the fix then was to
 * stop storing the answer and start asking the question. This asks the same question for everything
 * else in the room.
 *
 * The spec therefore keeps saying what the DRAWING says - a pelmet over this window at 2350 - and the
 * level is built from what the BUILDING is. Nothing is written back into the spec, so there is no
 * second source of truth to drift, and re-running produces the same answer from the same inputs.
 *
 * The validator asks the same question of the spec's own numbers and warns, which makes it a check
 * on this resolver rather than a second authority: see CeilingLeavesNoRoomForFixture.
 *
 * ## Purity
 *
 * Every function here is a pure function of its arguments. No world, no actor, no settings - the
 * clearance arrives as a value from the composing layer, exactly as every generator's figures do.
 * See .claude/rules/04-conventions.md.
 *
 * ## Units
 *
 * Whatever the caller is working in. Room, ceilings, fixture and clearance must all be in the same
 * units, and every figure returned is in those units. AHFHouseActor works in centimetres because
 * SetSpec converts once at ingest; the validator works in the spec's declared units and scales its
 * clearance before calling.
 */
class HOUSEFORGE_API FHFCeilingFit
{
public:
	/**
	 * What a fixture of this type does when the ceiling above it moves.
	 *
	 * Derived from the type rather than authored, because it is a property of the object and not of
	 * the drawing: no plan states that an extract cannot be shortened. Anything unlisted Ignores,
	 * which is the safe answer - an unclassified fixture is left exactly where it was drawn and the
	 * validator still reports it if a ceiling lands on it.
	 */
	static EHFCeilingFitRule RuleFor(EHFFixtureType Type);

	/**
	 * The fixture's plan footprint sampled as a grid, corners included.
	 *
	 * A GRID AND NOT THE CORNERS ALONE. The soffit height is piecewise constant over zones that are
	 * insets of the room boundary, so a long fixture can straddle a band with all four of its corners
	 * on the same side of the level change - a run of wall units under a peripheral band, either end
	 * in the deep ring and the middle of it under the shallow part, or the reverse.
	 */
	static TArray<FVector2D> SamplePoints(const FHFFixture& Fixture);

	/**
	 * The lowest finished soffit anywhere over a fixture, given every ceiling in its room.
	 *
	 * Ceilings not covering the room are ignored, so a caller may hand over the whole spec's list.
	 * Returns the structural slab level when nothing covers the fixture.
	 */
	static double LowestSoffitZOver(const FHFFixture& Fixture, const FHFRoom& Room,
		const TArray<FHFFalseCeiling>& Ceilings);

	/**
	 * Where one fixture ends up.
	 *
	 * @param Clearance Gap left between the fixture's head and the finished soffit. Nothing is fixed
	 *        dead tight to a plastered ceiling, and a fitting that exactly touches one renders as two
	 *        faces in a plane - which is the flashing FHFStructuralCut exists to prevent.
	 *
	 * @param BuiltHeight Overall height of what is ACTUALLY BUILT here, when that is not the height
	 *        the drawing states. Zero or less means the drawn height stands, which is the answer for
	 *        almost everything.
	 *
	 *        THE DRAWN BOX IS NOT ALWAYS THE OBJECT, and a ceiling lands on the object. A plan marks
	 *        an extract at the fan that was bought - 250 x 100 - and the case built for it carries a
	 *        bezel sized to lap the CORNERS of the chase cored behind it, so it stands 316 tall. The
	 *        first version of this fitted the drawn box under the soffit and left 33 mm of real
	 *        bezel inside the plasterboard: the same defect this whole mechanism exists to fix,
	 *        arrived at one step later and found by rendering it.
	 *
	 *        Taken as CENTRED on the drawn box, because that is how such a fitting is placed - see
	 *        AHFFanActor::PlacementFor - and it is the only convention under which a symmetric bezel
	 *        round a drawn aperture means anything. Only Lowers uses it: a wardrobe is built to the
	 *        height it is drawn at, and a thing that hangs is placed from its datum rather than sized
	 *        about its centre.
	 */
	static FHFCeilingFitResult Fit(const FHFFixture& Fixture, const FHFRoom& Room,
		const TArray<FHFFalseCeiling>& Ceilings, double Clearance, double BuiltHeight = 0.0);

	/**
	 * Every fixture in a spec, resolved against the ceilings that end up over them.
	 *
	 * THE ONE CALL THE COMPOSING LAYER MAKES. Returns a parallel list in the same order, so anything
	 * downstream that reads a fixture reads the fitted one - the wall that cores an extract's duct,
	 * the actor that builds it, and the preview that draws it all take the same object, and cannot
	 * disagree about where it is.
	 *
	 * @param BuiltHeights Overall built height per fixture id, for the fittings whose built envelope
	 *        is not their drawn box. Supplied by the COMPOSING LAYER rather than worked out here,
	 *        exactly as the ceiling's rod hole and the wall's duct already are: how big a fan comes
	 *        out is a question for AHFFanActor::ParamsFor, which is the one place that knows what fan
	 *        ends up standing there. Null, or an id that is absent, means the drawn height stands.
	 *
	 * @param OutMoved Optional. One line per fixture that moved, for the build log.
	 */
	static TArray<FHFFixture> FitAll(const FHFHouseSpec& Spec, double Clearance,
		const TMap<FName, double>* BuiltHeights = nullptr, TArray<FString>* OutMoved = nullptr);

	/** One line describing what happened to a fixture, for a build report or a validator message. */
	static FString Describe(const FHFFixture& Fixture, const FHFCeilingFitResult& Result);
};
