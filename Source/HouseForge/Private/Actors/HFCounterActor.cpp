// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFCounterActor.h"

#include "Geometry/HFJoineryKit.h"
#include "HouseForge.h"
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
	const FHFCounterBuild Build = FHFCounterKit::Build(Counter);

	// ------------------------------------------------------ a hole that was not cut has to be said
	//
	// FHFCounterKit REFUSES a cutout that would leave less than 50 mm of stone anywhere round it,
	// because granite cracks from the corner of one, and refusing is the right answer. What is not
	// the right answer is refusing in silence.
	//
	// The reference flat's hob was drawn on its counter's centreline, which put the cut 10 mm too
	// close to the upstand. The slab came back whole - flawless from above, which is the only angle
	// anybody looks at a worktop from - and the composing layer went on to stand the hob at the
	// counter's finished top exactly as though the hole were there. SEVEN AND A HALF LITRES OF
	// APPLIANCE INSIDE SOLID STONE, and not one thing in the build said anything at all.
	//
	// Nothing downstream can find this. The counter is watertight, the right size and correctly
	// wound; the hob is watertight, the right size and in the right place ON its counter. It is only
	// wrong as a pair, and the pair is this layer's business.
	for (const FHFCounterAperture& Asked : Counter.Apertures)
	{
		const bool bCut = Build.CutApertures.ContainsByPredicate(
			[&Asked](const FHFCounterAperture& Made) { return Made.FixtureId == Asked.FixtureId; });

		if (!bCut)
		{
			UE_LOG(LogHouseForge, Warning,
				TEXT("HouseForge counter '%s': the %.0f x %.0f cutout for '%s' at (%.1f, %.1f) was refused - it would leave less than %.0f cm of stone round it - so the slab is solid there and whatever is set into it is standing in granite. Move the fitting, or give the counter more depth."),
				*ElementId.ToString(), Asked.Size.X, Asked.Size.Y, *Asked.FixtureId.ToString(),
				Asked.Centre.X, Asked.Centre.Y, FHFCounterKit::MinApertureMargin);
		}
	}

	return Build.Shell;
}

bool AHFCounterActor::EveryApertureWasCut() const
{
	const FHFCounterBuild Build = FHFCounterKit::Build(Counter);

	for (const FHFCounterAperture& Asked : Counter.Apertures)
	{
		if (!Build.CutApertures.ContainsByPredicate(
			[&Asked](const FHFCounterAperture& Made) { return Made.FixtureId == Asked.FixtureId; }))
		{
			return false;
		}
	}

	return true;
}
