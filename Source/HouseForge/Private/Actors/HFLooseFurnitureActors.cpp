// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFLooseFurnitureActors.h"

#include "Model/HFBuildDefaults.h"

using namespace UE::Geometry;

// ---------------------------------------------------------------------------------------- sofa

void AHFSofaActor::ApplyProjectDefaults()
{
	// A SOFA IS BOUGHT, NOT BUILT ON SITE, so nothing on it comes off the joinery settings page - a
	// board thickness and a shadow-gap figure describe fitted carcassing and have no meaning on
	// upholstery. What a project DOES decide about a bought object is which one it buys, and that is
	// the whole of FHFSofaDefaults: the named design a drawing that named none gets, plus the two
	// figures a chaise is set out by. See UHFSettings::Sofa.
	Project = FHFBuildDefaults::FromProjectSettings().Sofa;
}

FHFSofaParams AHFSofaActor::ParamsFor(const FHFFixture& Fixture, const FHFSofaDefaults& Defaults)
{
	// WHICH SOFA, BEFORE HOW BIG. Every figure below the drawn box follows from the design, so the
	// design has to be resolved first - and resolving it is exactly what the kit is not allowed to do,
	// because Default means "ask the project" and a generator reads no settings object.
	const EHFSofaDesign Named = Fixture.Params.SofaDesign == EHFSofaDesign::Default
		? Defaults.DefaultDesign : Fixture.Params.SofaDesign;

	FHFSofaParams P = FHFUpholsteryKit::FiguresFor(Named);

	P.Width = Fixture.Footprint.X;
	P.Depth = Fixture.Footprint.Y;

	// THE DRAWN HEIGHT OF A SOFA IS THE TOP OF ITS BACK, unlike a bed, where it is the top of the
	// mattress. The difference is not arbitrary: a bed is dimensioned by the surface somebody sits on
	// because that figure has to agree with the nightstand beside it, and a sofa has nothing beside it
	// to agree with - what a plan states for a sofa is the tallest thing about it, so that a picture
	// or an AC head above can be set out clear of it.
	//
	// A drawing that states NO height keeps the design's own, which is what makes a design nameable
	// from a spec that is only a plan. What the drawn height never touches is the seat and the arm:
	// those are ergonomic and belong to the design - see FHFUpholsteryKit::FiguresFor.
	if (Fixture.Height > 0.0)
	{
		P.Height = Fixture.Height;
	}

	// The two figures a project decides about an L, and the hand, which the drawing does.
	P.ChaiseWidth = Defaults.ChaiseWidth;
	P.MinChaiseProjection = Defaults.MinChaiseProjection;
	P.bChaiseOnLeft = Fixture.Params.bChaiseOnLeft;

	// HOW MANY SEATS IS NOT SOMETHING THE DRAWING SAYS, and it is derived rather than assumed at
	// three. A seat cushion is 550-600 wide, everywhere, in every sofa anybody sells; so the seat
	// count is the clear width between the arms divided by that figure, which turns a 2100 sofa into
	// three seats and a 1500 one into two without either being stated.
	//
	// Derived from the SANITISED arm width rather than the default, because a narrow drawing clamps
	// the arms and a three-seater's worth of clear width would otherwise come out of a two-seater box.
	// On a sectional the clear width is what the chaise has LEFT of the straight run, so the same
	// 2100 box comes out as a two-seater plus a chaise rather than as a three-seater with a return
	// bolted to one of its cushions.
	const FHFSofaParams Clamped = FHFUpholsteryKit::SanitiseSofa(P);
	P.SeatCount = FMath::Clamp(FMath::RoundToInt(Clamped.InnerWidth() / 58.0), 1, 6);

	return FHFUpholsteryKit::SanitiseSofa(P);
}

void AHFSofaActor::ApplyFixture(const FHFFixture& Fixture)
{
	Sofa = ParamsFor(Fixture, Project);
}

FDynamicMesh3 AHFSofaActor::BuildMesh() const
{
	return FHFUpholsteryKit::BuildSofa(Sofa).Shell;
}

// --------------------------------------------------------------------------------------- table

void AHFTableActor::ApplyProjectDefaults()
{
	// Bought, like the sofa. See AHFSofaActor::ApplyProjectDefaults.
}

FHFTableParams AHFTableActor::ParamsFor(const FHFFixture& Fixture)
{
	FHFTableParams P;

	P.Width = Fixture.Footprint.X;
	P.Depth = Fixture.Footprint.Y;
	P.Height = Fixture.Height;

	if (Fixture.Type == EHFFixtureType::CoffeeTable)
	{
		// LIGHTER IN EVERY MEMBER, because a coffee table is seen from above and from a seat rather
		// than from standing height, and a dining table's 70 mm leg at 400 tall reads as a bench.
		P.LegSection = 6.0;
		P.LegInset = 6.0;

		// A shallow rail: there is no knee under a coffee table, so the apron is there to give the top
		// a shadow rather than to carry anything.
		P.ApronDepth = 4.0;
		P.ApronThickness = 2.0;
		P.ApronSetback = 1.2;

		// THE SHELF IS THE DIFFERENCE BETWEEN THE TWO OBJECTS. A coffee table with nothing between its
		// legs is a low dining table, and the flat has one of those already.
		P.ShelfTopZ = 12.0;
		P.ShelfThickness = 2.0;
	}
	else
	{
		// A four-seater. The apron is kept to 60 mm deliberately: at 100 the knee clearance under a
		// 750 top with a 30 top board falls to 620, which is 30 mm under what a knee needs, and
		// nothing in the drawing would ever have said so. See FHFTableParams::KneeClearance.
		P.LegSection = 7.0;
		P.LegInset = 8.0;
		P.ApronDepth = 6.0;
		P.ApronThickness = 2.2;
		P.ApronSetback = 1.5;
		P.ShelfTopZ = 0.0;
	}

	return FHFFrameKit::SanitiseTable(P);
}

void AHFTableActor::ApplyFixture(const FHFFixture& Fixture)
{
	Table = ParamsFor(Fixture);
}

FDynamicMesh3 AHFTableActor::BuildMesh() const
{
	return FHFFrameKit::BuildTable(Table).Shell;
}

// --------------------------------------------------------------------------------------- chair

void AHFChairActor::ApplyProjectDefaults()
{
	// Bought, like the sofa. See AHFSofaActor::ApplyProjectDefaults.
}

FHFChairParams AHFChairActor::ParamsFor(const FHFFixture& Fixture)
{
	FHFChairParams P;

	P.Width = Fixture.Footprint.X;
	P.Depth = Fixture.Footprint.Y;
	P.Height = Fixture.Height;

	return FHFFrameKit::SanitiseChair(P);
}

void AHFChairActor::ApplyFixture(const FHFFixture& Fixture)
{
	Chair = ParamsFor(Fixture);
}

FDynamicMesh3 AHFChairActor::BuildMesh() const
{
	return FHFFrameKit::BuildChair(Chair).Shell;
}
