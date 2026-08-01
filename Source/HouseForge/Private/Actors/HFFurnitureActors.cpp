// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFFurnitureActors.h"

#include "Model/HFBuildDefaults.h"

using namespace UE::Geometry;

namespace
{
	/**
	 * Scribe clearance left between a fixture standing on the floor and the skirting behind it.
	 *
	 * A gap rather than a touch, and a small one. Two faces in the same plane is the coplanar
	 * flashing FHFStructuralCut exists to prevent everywhere else in this plugin, and a desk's gable
	 * dead against a skirting board is precisely that - two large flat surfaces meeting exactly, which
	 * renders as a flickering seam at floor level right where the eye follows the wall round.
	 */
	constexpr double SkirtingScribeGap = 0.2;
}

// ----------------------------------------------------------------------------------------- bed

void AHFBedActor::ApplyProjectDefaults()
{
	// A BED IS BOUGHT, NOT BUILT ON SITE, so nothing on it comes off the joinery settings page - a
	// mattress is whatever the manufacturer made it. The hook exists so the composing layer can treat
	// every fixture the same way, and so there is somewhere obvious for a future furniture catalogue
	// to be read from. The same reason AHFSinkActor has one.
}

FHFBedParams AHFBedActor::ParamsFor(const FHFFixture& Fixture)
{
	FHFBedParams P;

	// The drawn footprint is the MATTRESS's width and the bed's OVERALL depth. Both of the sizes in
	// this flat - 1800 x 2000 and 1500 x 2000 - are Indian mattress sizes exactly, which is what a
	// drawing marks a bed with; the headboard is then built inside that depth rather than added
	// behind it, or the head of every bed in the flat would stand in the wall it is against.
	P.Width = Fixture.Footprint.X;
	P.Depth = Fixture.Footprint.Y;

	// THE DRAWN HEIGHT OF A BED IS THE TOP OF ITS MATTRESS. Not the headboard, which is the tallest
	// thing on it and the obvious thing to confuse it with: a plan dimensions the surface somebody
	// sits on, because that is the figure that has to agree with the nightstand beside it.
	P.MattressTopZ = Fixture.Height;

	return FHFBedKit::Sanitise(P);
}

void AHFBedActor::ApplyFixture(const FHFFixture& Fixture)
{
	Bed = ParamsFor(Fixture);
}

FDynamicMesh3 AHFBedActor::BuildMesh() const
{
	return FHFBedKit::Build(Bed).Shell;
}

// ---------------------------------------------------------------------------------------- desk

void AHFDeskActor::ApplyProjectDefaults()
{
	const FHFBuildDefaults Defaults = FHFBuildDefaults::FromProjectSettings();

	Desk.Joinery = Defaults.Joinery;

	// AND THE SKIRTING, which is the figure that makes this hook more than boilerplate. A study table
	// is not scribed joinery - FHFSkirting::IsScribedJoinery says so, because the board runs on
	// through the 700 mm of clear wall under the knee hole - so the supports have to stand IN FRONT of
	// that board. Left at zero they stand 18 mm inside it: invisible in plan, invisible in every test
	// of the desk on its own, and a permanent interpenetration in the built room.
	//
	// Resolved here because only the composing layer may read a settings object, and handed to the
	// generator as a plain dimension - see .claude/rules/04-conventions.md.
	Desk.SupportSetback = Defaults.Skirting.Depth + SkirtingScribeGap;
}

FHFDeskParams AHFDeskActor::ParamsFor(const FHFFixture& Fixture)
{
	FHFDeskParams P;
	P.Joinery = FHFBuildDefaults::FromProjectSettings().Joinery;

	P.Width = Fixture.Footprint.X;
	P.Depth = Fixture.Footprint.Y;
	P.Height = Fixture.Height;

	// Two, because that is what a desk pedestal has and a drawing that marked none has said nothing
	// rather than asked for a pedestal with no fronts on it. The same fallback the nightstand takes.
	P.DrawerCount = Fixture.Params.DrawerCount > 0 ? Fixture.Params.DrawerCount : 2;
	P.HandleStyle = Fixture.Params.HandleStyle;

	// WHICH END THE PEDESTAL GOES AT IS NOT SOMETHING THE DRAWING SAYS, and the honest answer is a
	// fixed one rather than a guess dressed up as a rule. Both ends of this desk are open floor, so
	// unlike a kitchen drawer bank - which has to be put at the end that is not blocked by the return
	// run, see AHFCasedGoodsActor::bBankAtRunStart - there is nothing here for the composing layer to
	// measure. Left at the -X end, and settable on the actor by anybody who wants it at the other.
	P.bPedestalAtRightEnd = false;

	return FHFDeskKit::Sanitise(P);
}

void AHFDeskActor::ApplyFixture(const FHFFixture& Fixture)
{
	// Everything except the two figures ApplyProjectDefaults resolved from the project, which have to
	// survive this call: the drawing has no opinion about how thick a board is or where a skirting
	// board runs, and re-reading it must not throw either of them away.
	const FHFJoineryDefaults Joinery = Desk.Joinery;
	const double Setback = Desk.SupportSetback;

	Desk = ParamsFor(Fixture);

	Desk.Joinery = Joinery;
	Desk.SupportSetback = Setback;
	Desk = FHFDeskKit::Sanitise(Desk);
}

// Two calls to Build per regeneration, one for the shell and one for the parts, because that is the
// shape of AHFElementActor's contract - BuildMesh and BuildParts are separate const hooks and neither
// may leave anything behind for the other.

FDynamicMesh3 AHFDeskActor::BuildMesh() const
{
	return FHFDeskKit::Build(Desk).Shell;
}

void AHFDeskActor::BuildParts(TArray<FHFMeshPart>& OutParts) const
{
	FHFDeskBuild Built = FHFDeskKit::Build(Desk);
	OutParts.Append(MoveTemp(Built.Parts));
}
