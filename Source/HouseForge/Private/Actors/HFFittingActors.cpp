// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFFittingActors.h"

using namespace UE::Geometry;

// ---------------------------------------------------------------------------------------- sink

void AHFSinkActor::ApplyProjectDefaults()
{
	// Nothing on a bought sink comes off the joinery settings page - a pressed steel bowl is what the
	// manufacturer made, not what this project builds. The hook exists so the composing layer can
	// treat every fixture the same way, and so there is somewhere obvious for a future sanitaryware
	// catalogue to be read from.
}

void AHFSinkActor::ApplyFixture(const FHFFixture& Fixture)
{
	Sink.Width = Fixture.Footprint.X;
	Sink.Depth = Fixture.Footprint.Y;

	// The drawn height of a sink is its BOWL DEPTH measured down from the rim, which is why the
	// fixture's BaseZ plus its height comes out at the counter top rather than above it. A sink is
	// dimensioned from the worktop down, because the worktop is the only level surface involved.
	Sink.BowlDepth = Fixture.Height;

	// A UTILITY SINK IS ONE BOWL AND A KITCHEN SINK IS TWO, and the drawing says which by how wide it
	// is: nothing narrower than about 700 has room for two bowls that a pan will fit in. Read off the
	// width rather than declared, because no drawing of a flat this size marks a bowl count.
	Sink.BowlCount = Fixture.Footprint.X >= 70.0 ? 2 : 1;
}

FDynamicMesh3 AHFSinkActor::BuildMesh() const
{
	return FHFSanitaryKit::BuildSink(Sink).Shell;
}

void AHFSinkActor::BuildParts(TArray<FHFMeshPart>& OutParts) const
{
	FHFSinkBuild Built = FHFSanitaryKit::BuildSink(Sink);
	OutParts.Append(MoveTemp(Built.Parts));
}

// ----------------------------------------------------------------------------------------- hob

void AHFHobActor::ApplyProjectDefaults()
{
	// As the sink: a hob is bought, not built.
}

void AHFHobActor::ApplyFixture(const FHFFixture& Fixture)
{
	Hob.Width = Fixture.Footprint.X;
	Hob.Depth = Fixture.Footprint.Y;

	// THE DRAWN HEIGHT IS THE WHOLE APPLIANCE, top of the grates to the bottom of the body, and it
	// straddles the stone. Split so that what stands above the counter is the glass and its pan
	// supports and everything else drops through the cutout - which is the only arrangement that
	// puts the cooking surface at the counter height the drawing actually specified.
	const double Overall = FMath::Max(Fixture.Height, Hob.GlassThickness);
	const double Above = FMath::Min(Hob.GlassThickness + Hob.GrateHeight, Overall);

	Hob.BodyDepth = Overall - Above;
}

double AHFHobActor::CookingSurfaceAboveStone(const FHFFixture& Fixture)
{
	// The same split ApplyFixture makes, read off the kit's own figures rather than off a built actor.
	// A hob drawn shallower than its own glass and grates is a drawing mistake, and the honest answer
	// there is the whole of what was drawn.
	const FHFHobParams Defaults;
	return FMath::Min(Defaults.GlassThickness + Defaults.GrateHeight,
		FMath::Max(Fixture.Height, Defaults.GlassThickness));
}

FDynamicMesh3 AHFHobActor::BuildMesh() const
{
	return FHFApplianceKit::BuildHob(Hob).Shell;
}

void AHFHobActor::BuildParts(TArray<FHFMeshPart>& OutParts) const
{
	FHFApplianceBuild Built = FHFApplianceKit::BuildHob(Hob);
	OutParts.Append(MoveTemp(Built.Parts));
}

// ------------------------------------------------------------------------------------- chimney

void AHFChimneyActor::ApplyProjectDefaults()
{
	// As above. The one figure a chimney takes from the rest of the house is its duct length, and
	// that arrives through ApplyCeilingAbove rather than from any settings page.
}

FHFChimneyParams AHFChimneyActor::ParamsFor(const FHFFixture& Fixture)
{
	FHFChimneyParams P;
	P.Width = Fixture.Footprint.X;
	P.Depth = Fixture.Footprint.Y;
	P.CanopyHeight = Fixture.Height;

	// The taper is most of the canopy on a pyramidal hood: a short straight skirt carrying the
	// filter, and the slope above it doing the work of the silhouette.
	P.TaperHeight = P.CanopyHeight * 0.55;

	return FHFApplianceKit::SanitiseChimney(P);
}

void AHFChimneyActor::ApplyFixture(const FHFFixture& Fixture)
{
	Chimney = ParamsFor(Fixture);
}

void AHFChimneyActor::ApplyCeilingAbove(double SoffitZAboveCanopyTop)
{
	// ASSIGNED, NOT ACCUMULATED - and that is the difference between this and a ceiling fan's rod.
	// The fan's ApplyCeilingAbove adds to a project figure, which is why calling it twice hangs the
	// fan a ceiling lower each time and why the composing layer has to re-seed a fan from scratch.
	// A duct has no compiled-in length to add to: it is entirely the gap between the canopy and the
	// soffit, so setting it is idempotent and a second call cannot make it wrong.
	Chimney.DuctLength = FMath::Max(SoffitZAboveCanopyTop, 0.0);
}

FDynamicMesh3 AHFChimneyActor::BuildMesh() const
{
	return FHFApplianceKit::BuildChimney(Chimney).Shell;
}

void AHFChimneyActor::BuildParts(TArray<FHFMeshPart>& OutParts) const
{
	FHFApplianceBuild Built = FHFApplianceKit::BuildChimney(Chimney);
	OutParts.Append(MoveTemp(Built.Parts));
}
