// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFCounterActor.h"

#include "Geometry/HFJoineryKit.h"
#include "Model/HFBuildDefaults.h"

using namespace UE::Geometry;

namespace
{
	/**
	 * How far the stone stands proud of the door faces beneath it.
	 *
	 * 20 mm, which is what a fitted worktop actually oversails by: enough to throw a drip clear of the
	 * shutter and to give the edge a shadow under it, and not so much that somebody walks into it.
	 */
	constexpr double OversailPastDoors = 2.0;

	/**
	 * The whole overhang: past the CARCASS front, which is the datum the slab is set out from.
	 *
	 * The carcass front plane is Y = 0 and the doors hang in front of it, so the slab has to clear
	 * the doors before it can oversail them. Asked of the same joinery figures the shutter is built
	 * from - a leaf thickness and its hinge clearance - rather than typed in here, because a project
	 * that changes its shutter thickness moves the door face and the stone has to follow it.
	 */
	double OverhangFor(const FHFJoineryDefaults& Joinery)
	{
		const FHFShutterParams Leaf = Joinery.Make<FHFShutterParams>();
		return Leaf.FaceOffset() + OversailPastDoors;
	}
}

bool AHFCounterActor::Builds(EHFFixtureType Type)
{
	return Type == EHFFixtureType::CounterTop;
}

double AHFCounterActor::RimLapFor(EHFFixtureType SetInType)
{
	switch (SetInType)
	{
	case EHFFixtureType::Hob:
		// A drop-in hob sits on a thin glass or steel flange. 10 mm a side is what turns the drawn
		// 580 x 500 appliance into the 560 x 480 cutout its template actually gives.
		return 1.0;

	default:
		// A top-mounted sink's pressed rim, and the safe answer for anything else set into stone.
		return FHFCounterKit::ApertureRimLap;
	}
}

void AHFCounterActor::ApplyProjectDefaults()
{
	// The composing layer's job, and the only line in this class that knows a settings object could
	// exist. By the time the generator runs, everything it needs is already on the actor.
	Counter.Overhang = OverhangFor(FHFBuildDefaults::FromProjectSettings().Joinery);
}

void AHFCounterActor::ApplyFixture(const FHFFixture& Fixture)
{
	Counter.Width = Fixture.Footprint.X;
	Counter.Depth = Fixture.Footprint.Y;

	// The drawn 40 is the whole build-up: 18-20 of granite on 18 of ply.
	Counter.Thickness = Fixture.Height;

	Counter.UpstandHeight = Fixture.Params.UpstandHeight;
}

FHFCounterParams AHFCounterActor::ParamsFor(const FHFFixture& Fixture)
{
	const FHFBuildDefaults Defaults = FHFBuildDefaults::FromProjectSettings();

	FHFCounterParams P;
	P.Overhang = OverhangFor(Defaults.Joinery);
	P.Width = Fixture.Footprint.X;
	P.Depth = Fixture.Footprint.Y;
	P.Thickness = Fixture.Height;
	P.UpstandHeight = Fixture.Params.UpstandHeight;

	return FHFCounterKit::Sanitise(P);
}

double AHFCounterActor::BuiltTopZ(const FHFFixture& Fixture)
{
	// The fixture's own base plus what actually gets built on it, rather than the drawn top. A sink
	// drawn at 690 is drawn there because somebody added up a 720 carcass and a 40 counter on a 100
	// plinth, and that sum goes stale the moment anybody changes the slab's thickness.
	return Fixture.BaseZ + ParamsFor(Fixture).TopZ();
}

FDynamicMesh3 AHFCounterActor::BuildMesh() const
{
	return FHFCounterKit::Build(Counter).Shell;
}
