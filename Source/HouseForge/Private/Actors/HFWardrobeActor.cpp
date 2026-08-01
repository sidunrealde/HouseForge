// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFWardrobeActor.h"

#include "Model/HFBuildDefaults.h"
#include "Model/HFFixturePlacement.h"

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

	// Copied straight through, zero included, and that is the point: zero is the sentinel
	// FHFWardrobeKit::Sanitise resolves against the project's module width. Deriving it HERE instead
	// stamped a number onto the actor and froze it - the wardrobe was built at whatever the module
	// width happened to be that day, and a project that later changed the dial rebuilt every
	// wardrobe with its bay count unchanged. Resolving a sentinel in two places is how the two
	// answers drift; PlinthHeight below is the same rule.
	Wardrobe.BayCount = FMath::Max(Spec.ShutterCount, 0);

	Wardrobe.PlinthHeight = Spec.PlinthHeight;

	Wardrobe.bHasLoft = Spec.bHasLoft;
	Wardrobe.LoftHeight = Spec.LoftHeight;
	Wardrobe.ShelfCount = Spec.ShelfCount;
	Wardrobe.bHangingRail = Spec.bHasHangingRail;
	Wardrobe.HandleStyle = Spec.HandleStyle;
	Wardrobe.bGlassInsert = Spec.bHasGlassInsert;
	Wardrobe.CorniceHeight = Spec.CorniceHeight;

	// How the leaves move, read off the drawing rather than assumed. Until FHFFixtureParams carried
	// these, MotionKind and LoftMotionKind kept their SideHung defaults on every wardrobe the
	// pipeline ever built - so the sliding wardrobe the kit was written for, and the top-hung loft
	// flap beside it, were reachable only by hand-editing an actor after generation.
	//
	// Passed across separately rather than derived one from the other: a sliding body does not imply
	// a sliding loft, and Sanitise refuses that combination anyway.
	Wardrobe.MotionKind = Spec.ShutterMotion;
	Wardrobe.LoftMotionKind = Spec.LoftShutterMotion;

	// A wardrobe standing against one wall has both its ends on show, which is what an Indian bedroom
	// wardrobe is: a 2400 run in a 3600 room. An end dying into a return wall is a fitted wardrobe and
	// is set on the actor afterwards, because nothing on the fixture says which.
	Wardrobe.bLeftEndExposed = true;
	Wardrobe.bRightEndExposed = true;
}

FTransform AHFWardrobeActor::PlacementFor(const FHFFixture& Fixture, double FloorZ, const FHFWall* AnchorWall)
{
	// THE RULE MOVED, THE ANSWER DID NOT. This was the only correct implementation in the codebase of
	// "put the run's back against the anchor wall and turn it through half a turn if the drawing's
	// yaw pointed the front at the wall", and milestone 9 needs it for thirteen more types. Copied
	// once per type it would be thirteen chances to copy it slightly wrong, and every one of those
	// mistakes reads as correct in plan. Kept as a named entry point because a wardrobe's placement is
	// a thing tests and tools ask for by name.
	return FHFFixturePlacement::AgainstWall(Fixture, FloorZ, AnchorWall);
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
