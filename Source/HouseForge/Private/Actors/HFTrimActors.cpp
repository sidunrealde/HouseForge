// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFTrimActors.h"

#include "Model/HFBuildDefaults.h"

using namespace UE::Geometry;

// ---------------------------------------------------------------------------------- the railing

void AHFRailingActor::ApplyProjectDefaults()
{
	// Nothing the project owns. A balustrade is fabricated to a code and to stock sections - 50 x 50
	// SHS, 16 mm bar, a 100 mm sphere - and not one of those is a figure this plugin's settings page
	// has any business changing. The line is here rather than absent so the seeding order in
	// AHFHouseActor is the same for every fixture type in the table.
}

FHFRailingParams AHFRailingActor::ParamsFor(const FHFFixture& Fixture)
{
	FHFRailingParams P;

	P.Width = Fixture.Footprint.X;
	P.Depth = Fixture.Footprint.Y;
	P.Height = Fixture.Height;

	// A SERVICE BALCONY GETS THE SAME GUARD AS A LIVING ONE. There is a temptation to make the short
	// 1800 run lighter, and it is the wrong instinct: the drop is the same drop.

	return FHFFrameKit::SanitiseRailing(P);
}

void AHFRailingActor::ApplyFixture(const FHFFixture& Fixture)
{
	const double Mount = Railing.MountBaseHeight;

	Railing = ParamsFor(Fixture);

	// Kept across the re-seed. ApplyMount is the composing layer's answer and it may have run first;
	// ParamsFor knows only the drawn box, so reading the fixture must not throw the parapet away.
	Railing.MountBaseHeight = Mount;
}

void AHFRailingActor::ApplyMount(double ParapetHeight)
{
	Railing.MountBaseHeight = FMath::Max(ParapetHeight, 0.0);
}

FDynamicMesh3 AHFRailingActor::BuildMesh() const
{
	return FHFFrameKit::BuildRailing(Railing).Shell;
}

// ----------------------------------------------------------------------------------- the pelmet

void AHFPelmetActor::ApplyProjectDefaults()
{
	// THE ONE FIGURE A PELMET SHARES WITH THE REST OF THE FLAT. It is made of the same board as the
	// wardrobes and the kitchen, by the same carpenter, so it reads the joinery default rather than
	// carrying a thickness of its own.
	Pelmet.BoardThickness = FHFBuildDefaults::FromProjectSettings().Joinery.CarcassBoardThickness;
}

FHFPelmetParams AHFPelmetActor::ParamsFor(const FHFFixture& Fixture)
{
	FHFPelmetParams P;
	P.BoardThickness = FHFBuildDefaults::FromProjectSettings().Joinery.CarcassBoardThickness;

	P.Width = Fixture.Footprint.X;
	P.Depth = Fixture.Footprint.Y;
	P.Height = Fixture.Height;

	return FHFWallPlateKit::SanitisePelmet(P);
}

void AHFPelmetActor::ApplyFixture(const FHFFixture& Fixture)
{
	const double Board = Pelmet.BoardThickness;

	Pelmet.Width = Fixture.Footprint.X;
	Pelmet.Depth = Fixture.Footprint.Y;
	Pelmet.Height = Fixture.Height;
	Pelmet.BoardThickness = Board;

	Pelmet = FHFWallPlateKit::SanitisePelmet(Pelmet);
}

FDynamicMesh3 AHFPelmetActor::BuildMesh() const
{
	return FHFWallPlateKit::BuildPelmet(Pelmet).Shell;
}
