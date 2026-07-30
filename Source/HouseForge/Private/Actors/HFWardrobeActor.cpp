// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFWardrobeActor.h"

#include "Model/HFBuildDefaults.h"

using namespace UE::Geometry;

void AHFWardrobeActor::ApplyProjectDefaults()
{
	// The composing layer's job, and the only line in this file that knows a settings object could
	// exist. By the time the generator runs, everything it needs is already on the actor.
	Wardrobe.Joinery = FHFBuildDefaults::FromProjectSettings().Joinery;
}

void AHFWardrobeActor::ApplyFixture(const FHFFixture& Fixture)
{
	const FHFFixtureParams& Spec = Fixture.Params;

	Wardrobe.Width = Fixture.Footprint.X;
	Wardrobe.Depth = Fixture.Footprint.Y;
	Wardrobe.Height = Fixture.Height;

	// A drawing that counted the shutters is believed. One that did not gets the run divided at the
	// project's module width, which is the figure a joiner would set it out at - and is one more
	// settings control that reaches geometry rather than sitting inert on the page.
	Wardrobe.BayCount = Spec.ShutterCount > 0
		? Spec.ShutterCount
		: FMath::Max(1, FMath::RoundToInt32(Wardrobe.Width / FMath::Max(Wardrobe.Joinery.ShutterModuleWidth, 1.0)));

	// Zero means the drawing did not say, so the project's figure stands. A stated one is honoured
	// even when it is unusual: a 60 mm kick under a wardrobe is a decision somebody made.
	Wardrobe.PlinthHeight = Spec.PlinthHeight > 0.0 ? Spec.PlinthHeight : Wardrobe.Joinery.PlinthHeight;

	Wardrobe.bHasLoft = Spec.bHasLoft;
	Wardrobe.LoftHeight = Spec.LoftHeight;
	Wardrobe.ShelfCount = Spec.ShelfCount;
	Wardrobe.bHangingRail = Spec.bHasHangingRail;
	Wardrobe.HandleStyle = Spec.HandleStyle;
	Wardrobe.bGlassInsert = Spec.bHasGlassInsert;
	Wardrobe.CorniceHeight = Spec.CorniceHeight;

	// A wardrobe standing against one wall has both its ends on show, which is what an Indian bedroom
	// wardrobe is: a 2400 run in a 3600 room. An end dying into a return wall is a fitted wardrobe and
	// is set on the actor afterwards, because nothing on the fixture says which.
	Wardrobe.bLeftEndExposed = true;
	Wardrobe.bRightEndExposed = true;
}

FTransform AHFWardrobeActor::PlacementFor(const FHFFixture& Fixture, double FloorZ, const FHFWall* AnchorWall)
{
	double Yaw = Fixture.RotationDegrees;

	if (AnchorWall != nullptr)
	{
		// Which way the back of the wardrobe looks at this yaw. Local +Y runs back into the unit, and
		// a yaw rotation takes it to (-sin, cos).
		const double Radians = FMath::DegreesToRadians(Yaw);
		const FVector2D Back(-FMath::Sin(Radians), FMath::Cos(Radians));

		// From the wall to the wardrobe. If the back is already pointing that way it is pointing away
		// from the wall, so the wardrobe is facing into it - and the whole run is turned round.
		const FVector2D OnWall = FMath::ClosestPointOnSegment2D(Fixture.Position, AnchorWall->Start, AnchorWall->End);
		const FVector2D ToFixture = Fixture.Position - OnWall;

		if (FVector2D::DotProduct(Back, ToFixture) > 0.0)
		{
			Yaw += 180.0;
		}
	}

	const FRotator Rotation(0.0, Yaw, 0.0);

	// The fixture is positioned by the CENTRE of its footprint and the wardrobe is built from its
	// front-left corner, so the corner is where the actor goes. Rotated with the run, or a turned
	// wardrobe lands half its own length away from where the drawing put it.
	const FVector ToCorner = Rotation.RotateVector(
		FVector(-Fixture.Footprint.X * 0.5, -Fixture.Footprint.Y * 0.5, 0.0));

	return FTransform(Rotation,
		FVector(Fixture.Position.X, Fixture.Position.Y, FloorZ + Fixture.BaseZ) + ToCorner);
}

// ------------------------------------------------------------------------------------ generation
//
// Two calls to Build per regeneration, one for the shell and one for the parts, because that is the
// shape of AHFElementActor's contract - BuildMesh and BuildParts are separate const hooks and
// neither may leave anything behind for the other. It is a regeneration, not a frame: a wardrobe is
// rebuilt when somebody edits a parameter, and the honesty of two pure calls is worth more here than
// a cache that has to be invalidated correctly.

FDynamicMesh3 AHFWardrobeActor::BuildMesh() const
{
	return FHFWardrobeKit::Build(Wardrobe).Shell;
}

void AHFWardrobeActor::BuildParts(TArray<FHFMeshPart>& OutParts) const
{
	FHFWardrobeBuild Built = FHFWardrobeKit::Build(Wardrobe);
	OutParts.Append(MoveTemp(Built.Parts));
}
