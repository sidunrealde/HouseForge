// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFLooseFurnitureActors.h"

using namespace UE::Geometry;

// ---------------------------------------------------------------------------------------- sofa

void AHFSofaActor::ApplyProjectDefaults()
{
	// A SOFA IS BOUGHT, NOT BUILT ON SITE, so nothing on it comes off the joinery settings page - a
	// board thickness and a shadow-gap figure describe fitted carcassing and have no meaning on
	// upholstery. The hook exists so the composing layer can treat every fixture the same way and so
	// there is somewhere obvious for a future furniture catalogue to be read from. The same reason
	// AHFBedActor and AHFSinkActor have one.
}

FHFSofaParams AHFSofaActor::ParamsFor(const FHFFixture& Fixture)
{
	FHFSofaParams P;

	P.Width = Fixture.Footprint.X;
	P.Depth = Fixture.Footprint.Y;

	// THE DRAWN HEIGHT OF A SOFA IS THE TOP OF ITS BACK, unlike a bed, where it is the top of the
	// mattress. The difference is not arbitrary: a bed is dimensioned by the surface somebody sits on
	// because that figure has to agree with the nightstand beside it, and a sofa has nothing beside it
	// to agree with - what a plan states for a sofa is the tallest thing about it, so that a picture
	// or an AC head above can be set out clear of it.
	P.Height = Fixture.Height;

	// HOW MANY SEATS IS NOT SOMETHING THE DRAWING SAYS, and it is derived rather than assumed at
	// three. A seat cushion is 550-600 wide, everywhere, in every sofa anybody sells; so the seat
	// count is the clear width between the arms divided by that figure, which turns a 2100 sofa into
	// three seats and a 1500 one into two without either being stated.
	//
	// Derived from the SANITISED arm width rather than the default, because a narrow drawing clamps
	// the arms and a three-seater's worth of clear width would otherwise come out of a two-seater box.
	const FHFSofaParams Clamped = FHFUpholsteryKit::SanitiseSofa(P);
	P.SeatCount = FMath::Clamp(FMath::RoundToInt(Clamped.InnerWidth() / 58.0), 1, 6);

	return FHFUpholsteryKit::SanitiseSofa(P);
}

void AHFSofaActor::ApplyFixture(const FHFFixture& Fixture)
{
	Sofa = ParamsFor(Fixture);
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
