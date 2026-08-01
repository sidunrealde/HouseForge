// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/HFElementActors.h"
#include "Geometry/HFCounterKit.h"
#include "Model/HFTypes.h"
#include "HFCounterActor.generated.h"

/**
 * A worktop in the level: a slab on a run of base units, cut for whatever is set into it.
 *
 * AN ELEMENT ACTOR RATHER THAN AN ARTICULATED ONE, deliberately. A granite slab with an upstand has
 * no moving part, and the things set into it - a sink, a hob - are their own fixtures with their own
 * articulation. See .claude/rules/04-conventions.md: everything that moves must be able to, and
 * saying so about the counter means saying out loud that this one does not.
 *
 * ## Where the holes come from
 *
 * Not from here. A generator may not go looking for the rest of the house and neither may an actor,
 * so AHFHouseActor resolves which set-in fixtures land on which counter, converts each into this
 * counter's own frame, and puts the list on Counter.Apertures before the first generation - exactly
 * as a wall is handed the duct opening an extract needs cored through it.
 */
UCLASS()
class HOUSEFORGE_API AHFCounterActor : public AHFElementActor
{
	GENERATED_BODY()

public:
	/**
	 * Everything this worktop is, in centimetres, in its own local space.
	 *
	 * The actor's origin is the front-left corner of the DRAWN footprint at the underside of the
	 * slab - the same corner the run of base units below it is set out from.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFCounterParams Counter;

	/**
	 * Seeds the project's construction figures. Called by the composing layer, not by generation.
	 *
	 * THE OVERHANG IS RESOLVED HERE AND NOWHERE ELSE. Granite oversails the doors it covers, and the
	 * doors hang in front of the carcass by a figure that lives in the project's joinery settings -
	 * so a slab flush with the drawn footprint finishes BEHIND the doors, which is the one
	 * arrangement that exists nowhere. Resolved against the same figures the shutter is built from,
	 * in the composing layer, so the two cannot drift.
	 */
	void ApplyProjectDefaults();

	/** Reads a spec fixture into the parameters. Call after ApplyProjectDefaults. */
	void ApplyFixture(const FHFFixture& Fixture);

	/**
	 * The finished top of a counter standing at this fixture, above the room's floor.
	 *
	 * Static and public because the composing layer has to know where a sink's rim goes before either
	 * the sink or the counter has been built - and because the drawn BaseZ of a set-in fixture is
	 * stale the moment somebody changes the slab's thickness on the settings page. The same reason
	 * AHFFanActor::ParamsFor is static and public.
	 */
	static double BuiltTopZ(const FHFFixture& Fixture);

	/** What a counter of this type is before the drawing's own dimensions go on it. */
	static FHFCounterParams ParamsFor(const FHFFixture& Fixture);

	/**
	 * How far the cut edge is held back, per side, from an appliance dropped through it.
	 *
	 * A PROPERTY OF THE JOINT rather than of either party, which is why it is answered here and not
	 * on the appliance: the composing layer needs it before either the counter or the thing set into
	 * it has been built. A pressed steel sink rim laps about 15 mm; a hob's glass flange is thinner
	 * at 10, which is what makes a 580 x 500 hob a 560 x 480 cutout.
	 */
	static double RimLapFor(EHFFixtureType SetInType);

	/** True for the fixture types this actor builds. */
	static bool Builds(EHFFixtureType Type);

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
};
