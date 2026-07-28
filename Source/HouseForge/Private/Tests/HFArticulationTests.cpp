// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/MeshTransforms.h"
#include "Geometry/HFGenerators.h"
#include "Geometry/HFMeshOps.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFArticulation.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	/** A 400 cm wall running along +X from the origin, so its normal is +Y. */
	FHFWall MakeHostWall()
	{
		FHFWall Wall;
		Wall.Id = TEXT("W1");
		Wall.Start = FVector2D(0.0, 0.0);
		Wall.End = FVector2D(400.0, 0.0);
		Wall.Thickness = 20.0;
		Wall.Height = 300.0;
		return Wall;
	}

	FHFOpening MakeDoorOpening(EHFOpeningKind Kind = EHFOpeningKind::Door,
		EHFSwing Swing = EHFSwing::InwardLeft)
	{
		FHFOpening Door;
		Door.Id = TEXT("D1");
		Door.WallId = TEXT("W1");
		Door.OffsetAlongWall = 200.0;
		Door.Width = 90.0;
		Door.Height = 210.0;
		Door.Kind = Kind;
		Door.Swing = Swing;
		return Door;
	}

	/** Bounds of a part's mesh once it has been posed at a given open amount. */
	FAxisAlignedBox3d PosedBounds(const FHFMeshPart& Part, double OpenAmount)
	{
		FHFPartState State;
		State.PivotTransform = Part.PivotTransform;
		State.Motion = Part.Motion;
		State.OpenAmount = OpenAmount;

		FDynamicMesh3 Posed = Part.Mesh;
		MeshTransforms::ApplyTransform(Posed, FTransformSRT3d(State.CurrentPose()), true);
		return Posed.GetBounds();
	}
}

/**
 * The hinge maths, which every swinging thing in the plugin depends on.
 *
 * Asserted on where a known point actually ends up, not on the transform's internals: a quaternion
 * that looks plausible and rotates the wrong way passes any test that only reads it back.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFHingeMotionTest, "HouseForge.Articulation.HingeMotion", HF_TEST_FLAGS)

bool FHFHingeMotionTest::RunTest(const FString& Parameters)
{
	FHFPartMotion Hinge;
	Hinge.Type = EHFMotionType::Hinge;
	Hinge.Axis = FVector::ZAxisVector;
	Hinge.MaxAngleDegrees = 90.0;

	const FVector Tip(100.0, 0.0, 0.0);

	// Closed means exactly where the generator put it, not merely near it: the closed pose of an
	// assembly has to be identical to the same assembly generated as one fixed mesh.
	TestTrue(TEXT("A hinge at 0 applies no offset at all"), Hinge.OffsetAt(0.0).Equals(FTransform::Identity));
	TestTrue(TEXT("A closed hinge leaves a point where it was"),
		Hinge.SweptLocalPoint(Tip, 0.0).Equals(Tip, 0.01));

	// At the limit, a point 100 out on +X has swung a quarter turn onto +Y.
	TestTrue(TEXT("A hinge at 1 sits at its declared limit"),
		Hinge.SweptLocalPoint(Tip, 1.0).Equals(FVector(0.0, 100.0, 0.0), 0.01));

	// Halfway is halfway in angle, which is what makes a slider read linearly.
	const FVector Half = Hinge.SweptLocalPoint(Tip, 0.5);
	TestNearlyEqual(TEXT("Half open is 45 degrees"),
		FMath::RadiansToDegrees(FMath::Atan2(Half.Y, Half.X)), 45.0, 0.01);

	// A hinge is a rotation, so the radius is invariant at every open amount. A pivot placed at the
	// mesh centre instead of the hinge line would fail this without changing the end pose much.
	for (double Alpha = 0.0; Alpha <= 1.0; Alpha += 0.25)
	{
		TestNearlyEqual(TEXT("A hinge preserves distance from the pivot"),
			Hinge.SweptLocalPoint(Tip, Alpha).Size(), 100.0, 0.01);
	}

	// The sign of the travel is the direction of swing; that is the whole handedness mechanism.
	FHFPartMotion Mirrored = Hinge;
	Mirrored.MaxAngleDegrees = -90.0;
	TestTrue(TEXT("Negative travel swings the other way"),
		Mirrored.SweptLocalPoint(Tip, 1.0).Equals(FVector(0.0, -100.0, 0.0), 0.01));

	// Out-of-range open amounts must clamp rather than extrapolate a shutter through its carcass.
	TestTrue(TEXT("Open amounts above 1 clamp to the limit"),
		Hinge.SweptLocalPoint(Tip, 4.0).Equals(Hinge.SweptLocalPoint(Tip, 1.0), 0.01));
	TestTrue(TEXT("Open amounts below 0 clamp to closed"),
		Hinge.SweptLocalPoint(Tip, -4.0).Equals(Tip, 0.01));

	// A part with no motion cannot be moved by any open amount.
	FHFPartMotion Fixed;
	TestFalse(TEXT("A part with no motion does not move"), Fixed.Moves());
	TestTrue(TEXT("A fixed part ignores its open amount"),
		Fixed.SweptLocalPoint(Tip, 1.0).Equals(Tip, 0.01));

	// The pose is the offset composed with the pivot, in that order. Getting it backwards rotates
	// the part about the actor origin instead of about its own hinge, which looks like a bug in
	// the pivot rather than in the composition.
	FHFPartState State;
	State.Motion = Hinge;
	State.PivotTransform = FTransform(FRotator::ZeroRotator, FVector(300.0, 50.0, 0.0));

	TestTrue(TEXT("A closed part sits on its pivot"),
		State.PoseAt(0.0).TransformPosition(Tip).Equals(FVector(400.0, 50.0, 0.0), 0.01));
	TestTrue(TEXT("An open part rotates about its own pivot, not the actor origin"),
		State.PoseAt(1.0).TransformPosition(Tip).Equals(FVector(300.0, 150.0, 0.0), 0.01));

	return true;
}

/** The slide maths: travel is a distance in centimetres, and it has to be exactly that distance. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSlideMotionTest, "HouseForge.Articulation.SlideMotion", HF_TEST_FLAGS)

bool FHFSlideMotionTest::RunTest(const FString& Parameters)
{
	FHFPartMotion Slide;
	Slide.Type = EHFMotionType::Slide;
	Slide.Axis = FVector::XAxisVector;
	Slide.MaxTravelCm = 45.0;

	const FVector Front(10.0, 20.0, 30.0);

	TestTrue(TEXT("A slide at 0 applies no offset at all"), Slide.OffsetAt(0.0).Equals(FTransform::Identity));
	TestTrue(TEXT("A closed slide leaves a point where it was"),
		Slide.SweptLocalPoint(Front, 0.0).Equals(Front, 0.01));

	// The declared distance, travelled exactly. A drawer that comes out 44 cm instead of 45 is a
	// drawer whose runner length was never really honoured.
	TestNearlyEqual(TEXT("A slide at 1 travels its declared distance"),
		(Slide.SweptLocalPoint(Front, 1.0) - Front).Size(), 45.0, 0.001);
	TestTrue(TEXT("A slide travels along its axis and nowhere else"),
		Slide.SweptLocalPoint(Front, 1.0).Equals(Front + FVector(45.0, 0.0, 0.0), 0.01));

	TestNearlyEqual(TEXT("Travel is linear in the open amount"),
		(Slide.SweptLocalPoint(Front, 0.5) - Front).Size(), 22.5, 0.001);

	// A slide does not rotate, so orientation is untouched at every open amount.
	TestTrue(TEXT("A slide introduces no rotation"),
		Slide.OffsetAt(1.0).GetRotation().Equals(FQuat::Identity, 0.0001));

	// An unnormalised axis must still travel the declared distance, or the travel figure would
	// silently mean something different for every part.
	FHFPartMotion Unnormalised = Slide;
	Unnormalised.Axis = FVector(0.0, 7.0, 0.0);
	TestNearlyEqual(TEXT("Travel is in centimetres regardless of axis length"),
		(Unnormalised.SweptLocalPoint(Front, 1.0) - Front).Size(), 45.0, 0.001);

	// Travel expressed in the part's local space, then placed by a rotated pivot: the world travel
	// follows the pivot's orientation, which is what puts a drawer's pull along its own runners.
	FHFPartState State;
	State.Motion = Slide;
	State.PivotTransform = FTransform(FRotator(0.0, 90.0, 0.0), FVector(100.0, 100.0, 0.0));

	const FVector Closed = State.PoseAt(0.0).TransformPosition(FVector::ZeroVector);
	const FVector Open = State.PoseAt(1.0).TransformPosition(FVector::ZeroVector);
	TestTrue(TEXT("A rotated pivot slides along its own axis"), Open.Equals(Closed + FVector(0.0, 45.0, 0.0), 0.01));

	return true;
}

/**
 * A door leaf, as the framework's first real moving part.
 *
 * The leaf mesh has to be a solid in its own local space, because everything downstream - baking,
 * collision, materials - treats a part as a mesh in its own right rather than as a slice of a
 * bigger one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFDoorLeafPartTest, "HouseForge.Articulation.DoorLeafPart", HF_TEST_FLAGS)

bool FHFDoorLeafPartTest::RunTest(const FString& Parameters)
{
	const FHFWall Wall = MakeHostWall();

	TArray<FHFMeshPart> Parts;
	FHFGenerators::BuildOpeningParts(MakeDoorOpening(), Wall, Parts);

	if (!TestEqual(TEXT("A door produces exactly one moving part"), Parts.Num(), 1))
	{
		return false;
	}

	const FHFMeshPart& Leaf = Parts[0];
	TestEqual(TEXT("The leaf carries a stable id"), Leaf.PartId, FName(TEXT("Leaf")));
	TestTrue(TEXT("A door leaf hinges"), Leaf.Motion.Type == EHFMotionType::Hinge);
	TestNearlyEqual(TEXT("It swings a right angle"), FMath::Abs(Leaf.Motion.MaxAngleDegrees), 90.0, 0.01);
	TestNearlyEqual(TEXT("A freshly generated leaf starts closed"), Leaf.DefaultOpenAmount, 0.0, 0.0001);

	TestTrue(TEXT("The leaf is watertight"), FHFMeshOps::IsClosed(Leaf.Mesh));
	TestTrue(TEXT("The leaf faces outward"), TMeshQueries<FDynamicMesh3>::GetVolumeArea(Leaf.Mesh).X > 0.0);

	// A 4 cm leaf inset half a centimetre all round, as a volume rather than a triangle count.
	const double ExpectedVolume = (90.0 - 1.0) * 4.0 * (210.0 - 1.0);
	TestNearlyEqual(TEXT("The leaf is a solid of the declared size"),
		TMeshQueries<FDynamicMesh3>::GetVolumeArea(Leaf.Mesh).X, ExpectedVolume, ExpectedVolume * 0.01);

	// Local space: the pivot is the origin, the leaf runs out along +X and up from the sill.
	const FAxisAlignedBox3d Local = Leaf.Mesh.GetBounds();
	TestTrue(TEXT("The leaf starts at its own pivot"), Local.Min.X >= -0.01 && Local.Min.X <= 1.0);
	TestNearlyEqual(TEXT("The leaf runs its width along local X"), Local.Max.X, 89.5, 0.01);
	TestTrue(TEXT("The leaf sits on the sill in local Z"), Local.Min.Z >= -0.01);

	for (const int32 Tid : Leaf.Mesh.TriangleIndicesItr())
	{
		if (FHFMeshOps::RoleForGroup(Leaf.Mesh.GetTriangleGroup(Tid)) != EHFSurfaceRole::DoorLeaf)
		{
			AddError(TEXT("A door leaf triangle was emitted without the DoorLeaf surface role."));
			break;
		}
	}

	// Closed, the leaf fills the opening it belongs to: 155..245 along the wall, 0..210 up.
	const FAxisAlignedBox3d Closed = PosedBounds(Leaf, 0.0);
	TestNearlyEqual(TEXT("Closed, the leaf spans the opening"), Closed.Min.X, 155.5, 0.01);
	TestNearlyEqual(TEXT("Closed, the leaf reaches the far jamb"), Closed.Max.X, 244.5, 0.01);
	TestTrue(TEXT("Closed, the leaf lies in the plane of the wall"), Closed.Height() < 5.0);

	// Open, the same leaf stands square to the wall on the inward side, hinged at the near jamb.
	const FAxisAlignedBox3d Open = PosedBounds(Leaf, 1.0);
	TestTrue(TEXT("Open, the leaf is square to the wall"), Open.Width() < 5.0);
	TestNearlyEqual(TEXT("Open, the leaf swings its full width off the wall"), Open.Max.Y, 89.5, 0.01);
	TestTrue(TEXT("An inward-left door hinges at the near jamb"), FMath::Abs(Open.Min.X - 155.0) < 3.0);
	TestTrue(TEXT("Open, the opening is clear"), Open.Max.X < 245.0);

	// The other three swings, checked on which side of the wall the leaf ends up and which jamb it
	// hangs on. A door on the wrong jamb reads as correct in elevation and wrong in a walkthrough.
	struct FSwingCase
	{
		EHFSwing Swing;
		double ExpectedTipY;
		double ExpectedHingeX;
	};

	const FSwingCase Cases[] = {
		{ EHFSwing::InwardRight,   89.5,  245.0 },
		{ EHFSwing::OutwardLeft,  -89.5,  155.0 },
		{ EHFSwing::OutwardRight, -89.5,  245.0 },
	};

	for (const FSwingCase& Case : Cases)
	{
		TArray<FHFMeshPart> SwingParts;
		FHFGenerators::BuildOpeningParts(MakeDoorOpening(EHFOpeningKind::Door, Case.Swing), Wall, SwingParts);
		if (SwingParts.Num() != 1)
		{
			AddError(TEXT("A swing variant failed to produce a leaf."));
			continue;
		}

		const FAxisAlignedBox3d Swung = PosedBounds(SwingParts[0], 1.0);
		const double TipY = Case.ExpectedTipY > 0.0 ? Swung.Max.Y : Swung.Min.Y;

		TestNearlyEqual(TEXT("The leaf swings to the declared side of the wall"), TipY, Case.ExpectedTipY, 0.5);
		TestTrue(TEXT("The leaf hangs on the declared jamb"),
			FMath::Abs(Swung.Center().X - Case.ExpectedHingeX) < 3.0);
	}

	// And the one-piece snapshot must still be the closed assembly, so nothing that consumed the
	// old infill sees a behaviour change.
	const FDynamicMesh3 Snapshot = FHFGenerators::GenerateOpeningInfill(MakeDoorOpening(), Wall);
	TestNearlyEqual(TEXT("The closed snapshot has the volume of the leaf"),
		TMeshQueries<FDynamicMesh3>::GetVolumeArea(Snapshot).X, ExpectedVolume, ExpectedVolume * 0.01);
	TestTrue(TEXT("The closed snapshot matches the closed pose"),
		Snapshot.GetBounds().Min.Equals(Closed.Min, 0.01) && Snapshot.GetBounds().Max.Equals(Closed.Max, 0.01));

	return true;
}

/** A sliding door slides its own width clear of the opening, and nothing else moves. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSlidingDoorPartTest, "HouseForge.Articulation.SlidingDoorPart", HF_TEST_FLAGS)

bool FHFSlidingDoorPartTest::RunTest(const FString& Parameters)
{
	const FHFWall Wall = MakeHostWall();

	TArray<FHFMeshPart> Parts;
	FHFGenerators::BuildOpeningParts(MakeDoorOpening(EHFOpeningKind::SlidingDoor, EHFSwing::None), Wall, Parts);

	if (!TestEqual(TEXT("A sliding door produces one moving part"), Parts.Num(), 1))
	{
		return false;
	}

	const FHFMeshPart& Leaf = Parts[0];
	TestTrue(TEXT("A sliding door slides"), Leaf.Motion.Type == EHFMotionType::Slide);
	TestNearlyEqual(TEXT("It travels its own width"), FMath::Abs(Leaf.Motion.MaxTravelCm), 90.0, 0.01);

	const FAxisAlignedBox3d Closed = PosedBounds(Leaf, 0.0);
	const FAxisAlignedBox3d Open = PosedBounds(Leaf, 1.0);

	// The measurable property is the swept distance, along the wall, with nothing else changed.
	TestNearlyEqual(TEXT("The leaf travels exactly its declared distance"),
		Open.Center().X - Closed.Center().X, 90.0, 0.01);
	TestNearlyEqual(TEXT("It does not drift off the wall"), Open.Center().Y, Closed.Center().Y, 0.01);
	TestNearlyEqual(TEXT("It does not change height"), Open.Center().Z, Closed.Center().Z, 0.01);
	TestNearlyEqual(TEXT("It stays the same size"), Open.Volume(), Closed.Volume(), 0.01);

	// Fully open means the opening is genuinely clear, not merely mostly clear.
	TestTrue(TEXT("Open, the leaf is clear of the opening"), Open.Min.X >= 244.5 - 0.01);

	return true;
}

/** Things that do not move must not pretend to. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFixedOpeningPartsTest, "HouseForge.Articulation.FixedOpenings", HF_TEST_FLAGS)

bool FHFFixedOpeningPartsTest::RunTest(const FString& Parameters)
{
	const FHFWall Wall = MakeHostWall();

	FHFOpening Window = MakeDoorOpening(EHFOpeningKind::Window, EHFSwing::None);
	Window.SillHeight = 90.0;
	Window.Height = 135.0;
	Window.Width = 150.0;

	TArray<FHFMeshPart> Parts;
	FHFGenerators::BuildOpeningParts(Window, Wall, Parts);
	TestEqual(TEXT("A window has no moving parts yet"), Parts.Num(), 0);

	// Its frame and glazing are fixed geometry, so they must all be in the fixed mesh.
	const FDynamicMesh3 Fixed = FHFGenerators::GenerateOpeningFixedInfill(Window, Wall);
	const FDynamicMesh3 Whole = FHFGenerators::GenerateOpeningInfill(Window, Wall);
	TestNearlyEqual(TEXT("A window's infill is entirely fixed"),
		TMeshQueries<FDynamicMesh3>::GetVolumeArea(Fixed).X,
		TMeshQueries<FDynamicMesh3>::GetVolumeArea(Whole).X, 0.01);

	FHFOpening Arch = MakeDoorOpening(EHFOpeningKind::Archway, EHFSwing::None);
	Parts.Reset();
	FHFGenerators::BuildOpeningParts(Arch, Wall, Parts);
	TestEqual(TEXT("An archway has no moving parts"), Parts.Num(), 0);
	TestEqual(TEXT("An archway has no fixed geometry either"),
		FHFGenerators::GenerateOpeningFixedInfill(Arch, Wall).TriangleCount(), 0);

	// A door contributes nothing to the fixed mesh: all of it moves.
	TestEqual(TEXT("A door's infill is entirely a moving part"),
		FHFGenerators::GenerateOpeningFixedInfill(MakeDoorOpening(), Wall).TriangleCount(), 0);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
