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

	/** A window in the same wall: 150 wide, 135 tall, on a 90 sill - the flat's living-room size. */
	FHFOpening MakeWindowOpening(EHFOpeningKind Kind, double Width = 150.0, double Height = 135.0,
		double Sill = 90.0, EHFSwing Swing = EHFSwing::None)
	{
		FHFOpening Window;
		Window.Id = TEXT("Win1");
		Window.WallId = TEXT("W1");
		Window.OffsetAlongWall = 200.0;
		Window.Width = Width;
		Window.Height = Height;
		Window.SillHeight = Sill;
		Window.Kind = Kind;
		Window.Swing = Swing;
		return Window;
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

	/**
	 * Volume of only the triangles carrying one surface role.
	 *
	 * The divergence theorem over a subset, which is exact as long as that subset is itself closed -
	 * and every role in a sash is its own closed box or boxes. It is how "the glass is a solid and
	 * not a plane" gets measured: a pane modelled as a plane has an area but no volume at all, and
	 * every bounds and triangle-count check passes on one.
	 *
	 * Negated to match the winding these meshes actually carry. Not assumed: RoleVolumeSumsToTheMesh
	 * below asserts that summing every role reproduces TMeshQueries' figure for the whole mesh, so a
	 * convention change would fail loudly rather than silently flip every measurement's sign.
	 */
	double RoleVolume(const FDynamicMesh3& Mesh, EHFSurfaceRole Role)
	{
		double Volume = 0.0;
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (FHFMeshOps::RoleForGroup(Mesh.GetTriangleGroup(Tid)) != Role)
			{
				continue;
			}

			FVector3d A, B, C;
			Mesh.GetTriVertices(Tid, A, B, C);
			Volume -= A.Dot(B.Cross(C)) / 6.0;
		}
		return Volume;
	}

	/** Every role in the mesh, summed. Must come to what TMeshQueries says the whole mesh is. */
	double RoleVolumeSumsToTheMesh(const FDynamicMesh3& Mesh)
	{
		double Volume = 0.0;
		for (int32 Index = 0; Index <= int32(EHFSurfaceRole::Structure); ++Index)
		{
			Volume += RoleVolume(Mesh, EHFSurfaceRole(Index));
		}
		return Volume;
	}

	/** Bounds of only the triangles carrying one surface role. */
	FAxisAlignedBox3d RoleBounds(const FDynamicMesh3& Mesh, EHFSurfaceRole Role)
	{
		FAxisAlignedBox3d Bounds = FAxisAlignedBox3d::Empty();
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (FHFMeshOps::RoleForGroup(Mesh.GetTriangleGroup(Tid)) != Role)
			{
				continue;
			}

			FVector3d A, B, C;
			Mesh.GetTriVertices(Tid, A, B, C);
			Bounds.Contain(A);
			Bounds.Contain(B);
			Bounds.Contain(C);
		}
		return Bounds;
	}

	/** Where a point given in a part's local space ends up, in the actor, at a given open amount. */
	FVector PosedPoint(const FHFMeshPart& Part, const FVector& LocalPoint, double OpenAmount)
	{
		FHFPartState State;
		State.PivotTransform = Part.PivotTransform;
		State.Motion = Part.Motion;
		State.OpenAmount = OpenAmount;

		return State.CurrentPose().TransformPosition(LocalPoint);
	}

	/** True when two boxes share volume - not merely touch, which two things in a track do. */
	bool BoxesOverlap(const FAxisAlignedBox3d& A, const FAxisAlignedBox3d& B, double Tolerance = 0.01)
	{
		return A.Min.X < B.Max.X - Tolerance && B.Min.X < A.Max.X - Tolerance
			&& A.Min.Y < B.Max.Y - Tolerance && B.Min.Y < A.Max.Y - Tolerance
			&& A.Min.Z < B.Max.Z - Tolerance && B.Min.Z < A.Max.Z - Tolerance;
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

/**
 * A sliding unit is two panels, and the one that moves has somewhere to move to.
 *
 * One leaf the full width of the opening had nowhere to go: sliding it its own width drove 1.65 m
 * of it into the masonry beside the jamb, and in the reference flat straight through the next
 * window along. Half the opening each, on two tracks, is both what a sliding unit is and what
 * keeps every panel inside the reveal at every open amount.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSlidingDoorPartTest, "HouseForge.Articulation.SlidingDoorPart", HF_TEST_FLAGS)

bool FHFSlidingDoorPartTest::RunTest(const FString& Parameters)
{
	const FHFWall Wall = MakeHostWall();

	TArray<FHFMeshPart> Parts;
	FHFGenerators::BuildOpeningParts(MakeDoorOpening(EHFOpeningKind::SlidingDoor, EHFSwing::None), Wall, Parts);

	if (!TestEqual(TEXT("A sliding door is two panels"), Parts.Num(), 2))
	{
		return false;
	}

	const FHFMeshPart* Fixed = Parts.FindByPredicate(
		[](const FHFMeshPart& P) { return P.PartId == FName(TEXT("PanelFixed")); });
	const FHFMeshPart* Leaf = Parts.FindByPredicate(
		[](const FHFMeshPart& P) { return P.PartId == FName(TEXT("Leaf")); });

	if (!TestNotNull(TEXT("It has a fixed panel"), Fixed) || !TestNotNull(TEXT("It has a running panel"), Leaf))
	{
		return false;
	}

	TestTrue(TEXT("The fixed panel does not move"), !Fixed->Motion.Moves());
	TestTrue(TEXT("The running panel slides"), Leaf->Motion.Type == EHFMotionType::Slide);

	// Both panels are solids in their own right, because a bake treats each as a mesh of its own.
	TestTrue(TEXT("The fixed panel is watertight"), FHFMeshOps::IsClosed(Fixed->Mesh));
	TestTrue(TEXT("The running panel is watertight"), FHFMeshOps::IsClosed(Leaf->Mesh));

	// The opening spans 155..245 along this wall. Every panel stays inside it at every open amount,
	// which is the property the old single leaf broke.
	constexpr double OpeningMin = 155.0;
	constexpr double OpeningMax = 245.0;

	const FAxisAlignedBox3d FixedBounds = PosedBounds(*Fixed, 0.0);
	TestTrue(TEXT("The fixed panel sits inside the opening"),
		FixedBounds.Min.X >= OpeningMin - 0.01 && FixedBounds.Max.X <= OpeningMax + 0.01);

	for (double Alpha = 0.0; Alpha <= 1.0001; Alpha += 0.05)
	{
		const FAxisAlignedBox3d Posed = PosedBounds(*Leaf, Alpha);
		if (Posed.Min.X < OpeningMin - 0.01 || Posed.Max.X > OpeningMax + 0.01)
		{
			AddError(FString::Printf(
				TEXT("At %.2f open, the running panel spans %.2f..%.2f and leaves the %.0f..%.0f opening; it is sliding into the wall."),
				Alpha, Posed.Min.X, Posed.Max.X, OpeningMin, OpeningMax));
			break;
		}
	}

	const FAxisAlignedBox3d Closed = PosedBounds(*Leaf, 0.0);
	const FAxisAlignedBox3d Open = PosedBounds(*Leaf, 1.0);

	// Closed, the two panels together fill the opening with no daylight between their meeting
	// stiles - they interlock rather than merely abut.
	TestTrue(TEXT("Closed, the panels overlap at the meeting stile"), Closed.Max.X > FixedBounds.Min.X + 1.0);
	TestTrue(TEXT("Closed, the pair fills the opening"),
		Closed.Min.X <= OpeningMin + 1.0 && FixedBounds.Max.X >= OpeningMax - 1.0);

	// Open, the running panel has come to rest exactly over its fixed partner, which is as far as
	// it can travel without leaving the reveal.
	TestNearlyEqual(TEXT("Open, the running panel stacks on the fixed one"), Open.Max.X, FixedBounds.Max.X, 0.01);
	TestTrue(TEXT("Open, roughly half the opening is clear"),
		Open.Min.X - OpeningMin >= (OpeningMax - OpeningMin) * 0.4);

	// It slides: the swept distance is along the wall, with nothing else disturbed.
	TestNearlyEqual(TEXT("It travels its declared distance"),
		Open.Center().X - Closed.Center().X, Leaf->Motion.MaxTravelCm, 0.01);
	TestNearlyEqual(TEXT("It does not drift off the wall"), Open.Center().Y, Closed.Center().Y, 0.01);
	TestNearlyEqual(TEXT("It does not change height"), Open.Center().Z, Closed.Center().Z, 0.01);
	TestNearlyEqual(TEXT("It stays the same size"), Open.Volume(), Closed.Volume(), 0.01);

	// The two panels run in different tracks, or they would pass through one another.
	TestTrue(TEXT("The panels are on separate tracks"),
		Closed.Min.Y >= FixedBounds.Max.Y - 0.01 || Closed.Max.Y <= FixedBounds.Min.Y + 0.01);

	// And both tracks still fit inside a 20 cm wall.
	const double AcrossMin = FMath::Min(Closed.Min.Y, FixedBounds.Min.Y);
	const double AcrossMax = FMath::Max(Closed.Max.Y, FixedBounds.Max.Y);
	TestTrue(TEXT("The whole unit fits within the wall thickness"),
		AcrossMin >= -Wall.Thickness * 0.5 && AcrossMax <= Wall.Thickness * 0.5);

	return true;
}

/**
 * A sliding window is two sashes, and the one that runs has somewhere to run to.
 *
 * The same shape of answer as the sliding door, deliberately: each sash takes half the clear
 * opening and the running one travels until its far edge meets the fixed one's. A single sash the
 * width of the opening would slide into the masonry beside the jamb, which is exactly the failure
 * the balcony doors were rebuilt to avoid.
 *
 * Two tracks, not three. The third track is the flyscreen option and deepens the frame from 65 to
 * 92.5 mm; what is built here is the 27 mm two-track Domal section a flat of this class ships with.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSlidingWindowSashTest, "HouseForge.Articulation.SlidingWindowSashes", HF_TEST_FLAGS)

bool FHFSlidingWindowSashTest::RunTest(const FString& Parameters)
{
	const FHFWall Wall = MakeHostWall();
	const FHFOpening Window = MakeWindowOpening(EHFOpeningKind::SlidingWindow);

	TArray<FHFMeshPart> Parts;
	FHFGenerators::BuildOpeningParts(Window, Wall, Parts);

	if (!TestEqual(TEXT("A sliding window is two sashes"), Parts.Num(), 2))
	{
		return false;
	}

	const FHFMeshPart* Fixed = Parts.FindByPredicate(
		[](const FHFMeshPart& P) { return P.PartId == FName(TEXT("SashFixed")); });
	const FHFMeshPart* Sash = Parts.FindByPredicate(
		[](const FHFMeshPart& P) { return P.PartId == FName(TEXT("Sash")); });

	if (!TestNotNull(TEXT("It has a fixed sash"), Fixed) || !TestNotNull(TEXT("It has a running sash"), Sash))
	{
		return false;
	}

	TestTrue(TEXT("The fixed sash does not move"), !Fixed->Motion.Moves());
	TestTrue(TEXT("The operable sash slides"), Sash->Motion.Type == EHFMotionType::Slide);
	TestTrue(TEXT("It slides along the wall, not through it"),
		Sash->Motion.UnitAxis().Equals(FVector::XAxisVector, 0.0001));

	// Each sash is a solid in its own right, because a bake treats every part as a mesh of its own.
	TestTrue(TEXT("The fixed sash is watertight"), FHFMeshOps::IsClosed(Fixed->Mesh));
	TestTrue(TEXT("The running sash is watertight"), FHFMeshOps::IsClosed(Sash->Mesh));

	// Every triangle carries a role, and all three of the ones a window is made of are present. An
	// untagged triangle can never be re-materialled, and the failure is invisible in a screenshot.
	for (const FHFMeshPart* Part : { Fixed, Sash })
	{
		for (const int32 Tid : Part->Mesh.TriangleIndicesItr())
		{
			const EHFSurfaceRole Role = FHFMeshOps::RoleForGroup(Part->Mesh.GetTriangleGroup(Tid));
			if (Role != EHFSurfaceRole::WindowFrame && Role != EHFSurfaceRole::Glass
				&& Role != EHFSurfaceRole::MetalHardware)
			{
				AddError(FString::Printf(TEXT("Sash '%s' emitted a triangle with role %d, which is none of window frame, glass or metal hardware."),
					*Part->PartId.ToString(), int32(Role)));
				break;
			}
		}
	}

	TestTrue(TEXT("The operable sash carries its catch in metal"),
		RoleVolume(Sash->Mesh, EHFSurfaceRole::MetalHardware) > 0.0);

	// Pins the sign convention the per-role measurements below depend on.
	TestNearlyEqual(TEXT("The roles account for the whole sash"),
		RoleVolumeSumsToTheMesh(Sash->Mesh),
		TMeshQueries<FDynamicMesh3>::GetVolumeArea(Sash->Mesh).X, 0.01);

	// The glazing is a SOLID pane sitting in the sash's rebate, not a plane. Refraction and
	// reflection are wrong without thickness, and a plane has none while passing every bounds check.
	//
	// 5 mm toughened, engaged 9 mm into an 18 mm groove all round: the pane is the sash's clear
	// opening plus that engagement, which is 65.55 x 119.8 on this sash.
	const double GlassVolume = RoleVolume(Sash->Mesh, EHFSurfaceRole::Glass);
	const double ExpectedGlass = 65.55 * 119.8 * 0.5;
	TestNearlyEqual(TEXT("The glass is a solid of the declared pane size"),
		GlassVolume, ExpectedGlass, ExpectedGlass * 0.01);

	const FAxisAlignedBox3d Glass = RoleBounds(Sash->Mesh, EHFSurfaceRole::Glass);
	TestNearlyEqual(TEXT("The pane is 5 mm thick, not a plane"), Glass.Height(), 0.5, 0.001);

	// The sash frame itself, as a volume rather than a triangle count: two stiles full height with
	// the rails let in between them, in 40 x 27 mm section.
	const double ExpectedFrame = 2.0 * (4.0 * 2.7 * 126.0) + 2.0 * (63.75 * 2.7 * 4.0);
	TestNearlyEqual(TEXT("The sash is a real section, not a slab"),
		RoleVolume(Sash->Mesh, EHFSurfaceRole::WindowFrame), ExpectedFrame, ExpectedFrame * 0.01);

	// The opening spans 125..275 along this wall, and its clear opening 129.5..270.5 inside the
	// outer frame. Every sash stays inside THAT at every open amount - not merely inside the reveal,
	// because a sash overlapping the frame it runs in is a sash ploughing through its own jamb.
	constexpr double ClearMin = 129.5;
	constexpr double ClearMax = 270.5;
	constexpr double ClearBottom = 94.5;
	constexpr double ClearTop = 220.5;

	const FAxisAlignedBox3d FixedBounds = PosedBounds(*Fixed, 0.0);
	TestTrue(TEXT("The fixed sash sits inside the clear opening"),
		FixedBounds.Min.X >= ClearMin - 0.01 && FixedBounds.Max.X <= ClearMax + 0.01);

	for (double Alpha = 0.0; Alpha <= 1.0001; Alpha += 0.05)
	{
		const FAxisAlignedBox3d Posed = PosedBounds(*Sash, Alpha);

		if (Posed.Min.X < ClearMin - 0.01 || Posed.Max.X > ClearMax + 0.01
			|| Posed.Min.Z < ClearBottom - 0.01 || Posed.Max.Z > ClearTop + 0.01)
		{
			AddError(FString::Printf(
				TEXT("At %.2f open, the running sash spans %.2f..%.2f along the wall and %.2f..%.2f up, and leaves the %.1f..%.1f x %.1f..%.1f clear opening; it is running into the frame."),
				Alpha, Posed.Min.X, Posed.Max.X, Posed.Min.Z, Posed.Max.Z,
				ClearMin, ClearMax, ClearBottom, ClearTop));
			break;
		}

		if (BoxesOverlap(Posed, FixedBounds))
		{
			AddError(FString::Printf(
				TEXT("At %.2f open, the running sash shares volume with the fixed one; they are not on separate tracks."),
				Alpha));
			break;
		}
	}

	const FAxisAlignedBox3d Closed = PosedBounds(*Sash, 0.0);
	const FAxisAlignedBox3d Open = PosedBounds(*Sash, 1.0);

	// Closed, the two sashes interlock rather than merely abut: 25 mm of meeting stile, which is
	// what stops daylight showing between them.
	TestNearlyEqual(TEXT("Closed, the meeting stiles interlock by their declared overlap"),
		Closed.Max.X - FixedBounds.Min.X, 2.5, 0.05);
	TestTrue(TEXT("Closed, the pair fills the clear opening"),
		Closed.Min.X <= ClearMin + 0.01 && FixedBounds.Max.X >= ClearMax - 0.01);

	// Open, the running sash has come to rest exactly over its fixed partner - as far as it can go
	// without leaving the reveal.
	TestNearlyEqual(TEXT("Open, the running sash stacks on the fixed one"), Open.Max.X, FixedBounds.Max.X, 0.01);
	TestTrue(TEXT("Open, roughly half the window is clear"),
		Open.Min.X - ClearMin >= (ClearMax - ClearMin) * 0.4);

	// It slides: the declared travel, along the wall, with nothing else disturbed.
	TestNearlyEqual(TEXT("It travels its declared distance"),
		Open.Center().X - Closed.Center().X, Sash->Motion.MaxTravelCm, 0.01);
	TestNearlyEqual(TEXT("It does not drift off the wall"), Open.Center().Y, Closed.Center().Y, 0.01);
	TestNearlyEqual(TEXT("It does not change height"), Open.Center().Z, Closed.Center().Z, 0.01);
	TestNearlyEqual(TEXT("It stays the same size"), Open.Volume(), Closed.Volume(), 0.01);

	// Two tracks: the sashes never share a plane, or they would pass through one another.
	TestTrue(TEXT("The sashes are on separate tracks"),
		Closed.Min.Y >= FixedBounds.Max.Y - 0.01 || Closed.Max.Y <= FixedBounds.Min.Y + 0.01);

	// And the whole unit, catch included, still fits inside a 20 cm wall.
	const double AcrossMin = FMath::Min(Closed.Min.Y, FixedBounds.Min.Y);
	const double AcrossMax = FMath::Max(Closed.Max.Y, FixedBounds.Max.Y);
	TestTrue(TEXT("The whole unit fits within the wall thickness"),
		AcrossMin >= -Wall.Thickness * 0.5 && AcrossMax <= Wall.Thickness * 0.5);

	// The glazing lives in the sashes and nowhere else. A fixed pane left behind the closed sash
	// would double the glass and make an open window render exactly like a shut one.
	const FDynamicMesh3 FixedInfill = FHFGenerators::GenerateOpeningFixedInfill(Window, Wall);
	TestNearlyEqual(TEXT("The frame carries no glazing of its own"),
		RoleVolume(FixedInfill, EHFSurfaceRole::Glass), 0.0, 0.01);
	TestTrue(TEXT("The frame carries the tracks the sashes run on"),
		RoleVolume(FixedInfill, EHFSurfaceRole::MetalHardware) > 0.0);

	const FDynamicMesh3 Snapshot = FHFGenerators::GenerateOpeningInfill(Window, Wall);
	TestNearlyEqual(TEXT("The closed snapshot glazes the window exactly once"),
		RoleVolume(Snapshot, EHFSurfaceRole::Glass), GlassVolume * 2.0, GlassVolume * 0.02);

	// An opening too small to divide is honestly fixed glazing, and the frame glazes it - the two
	// answers must agree or the result is a framed hole that photographs as an open window.
	const FHFOpening Tiny = MakeWindowOpening(EHFOpeningKind::SlidingWindow, 40.0, 40.0, 150.0);
	TArray<FHFMeshPart> NoParts;
	AddExpectedMessagePlain(TEXT("too small to divide into two sashes"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1);
	FHFGenerators::BuildOpeningParts(Tiny, Wall, NoParts);
	TestEqual(TEXT("A sliding window too small for two sashes has none"), NoParts.Num(), 0);
	TestTrue(TEXT("...and is glazed by its frame instead"),
		RoleVolume(FHFGenerators::GenerateOpeningFixedInfill(Tiny, Wall), EHFSurfaceRole::Glass) > 0.0);

	return true;
}

/**
 * A ventilator's top-hung sash pivots on its head.
 *
 * A ventilator can be a fixed louvre, and a fixed louvre genuinely does not move - the rule is that
 * anything which moves must be able to move, not that everything must. A top-hung pivot sash is the
 * other half of the category and it does move, so this is the one that gets built.
 *
 * The property that matters is which way the hinge takes it. Rotating about the head, the corner
 * nearest the axis sweeps a quarter circle of the sash's own thickness; hung on the wrong face that
 * corner goes UP, and the sash disappears into the lintel as it opens - invisible in elevation, and
 * exactly the failure the door leaf was rehung to avoid.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFVentilatorSashTest, "HouseForge.Articulation.VentilatorSash", HF_TEST_FLAGS)

bool FHFVentilatorSashTest::RunTest(const FString& Parameters)
{
	const FHFWall Wall = MakeHostWall();

	// The reference flat's ventilator: 60 x 45, sitting on a bathroom door head at 210.
	const FHFOpening Vent = MakeWindowOpening(EHFOpeningKind::Ventilator, 60.0, 45.0, 210.0);

	TArray<FHFMeshPart> Parts;
	FHFGenerators::BuildOpeningParts(Vent, Wall, Parts);

	if (!TestEqual(TEXT("A ventilator is one sash"), Parts.Num(), 1))
	{
		return false;
	}

	const FHFMeshPart& Sash = Parts[0];
	TestEqual(TEXT("The sash carries a stable id"), Sash.PartId, FName(TEXT("Sash")));
	TestTrue(TEXT("A ventilator sash hinges"), Sash.Motion.Type == EHFMotionType::Hinge);

	// Top-hung: the axis is horizontal and runs ALONG the wall. A vertical axis here would be a
	// casement, and a cross-wall axis would be geometry no ventilator has.
	TestTrue(TEXT("It pivots about a horizontal axis along the wall"),
		Sash.Motion.UnitAxis().Equals(FVector::XAxisVector, 0.0001));
	TestNearlyEqual(TEXT("It opens the angle a stay allows"),
		FMath::Abs(Sash.Motion.MaxAngleDegrees), 30.0, 0.01);

	TestTrue(TEXT("The sash is watertight"), FHFMeshOps::IsClosed(Sash.Mesh));
	TestTrue(TEXT("The sash faces outward"), TMeshQueries<FDynamicMesh3>::GetVolumeArea(Sash.Mesh).X > 0.0);

	// Real glass again, in the sash rather than in the frame behind it.
	const double GlassVolume = RoleVolume(Sash.Mesh, EHFSurfaceRole::Glass);
	const double ExpectedGlass = 48.2 * 33.2 * 0.4;
	TestNearlyEqual(TEXT("The ventilator's glass is a solid pane"), GlassVolume, ExpectedGlass, ExpectedGlass * 0.01);
	TestNearlyEqual(TEXT("The pane is 4 mm thick, not a plane"),
		RoleBounds(Sash.Mesh, EHFSurfaceRole::Glass).Height(), 0.4, 0.001);
	TestTrue(TEXT("It has a pull to open it by"),
		RoleVolume(Sash.Mesh, EHFSurfaceRole::MetalHardware) > 0.0);

	TestNearlyEqual(TEXT("The frame carries no glazing of its own"),
		RoleVolume(FHFGenerators::GenerateOpeningFixedInfill(Vent, Wall), EHFSurfaceRole::Glass), 0.0, 0.01);

	// The hinge line is the head of the clear opening: 3.5 in from the near jamb at 170, and 3.5
	// down from the head at 255.
	const FVector HingeAtMidSpan = Sash.PivotTransform.TransformPosition(FVector(26.5, 0.0, 0.0));
	TestTrue(TEXT("The hinge runs along the head of the clear opening"),
		HingeAtMidSpan.Equals(FVector(200.0, 0.0, 251.5), 0.01));

	// A point ON the axis cannot move, at any open amount. That is the difference between a pivot
	// and a part that was merely translated into a convincing-looking pose.
	for (double Alpha = 0.0; Alpha <= 1.0001; Alpha += 0.25)
	{
		TestTrue(TEXT("The hinge line itself never moves"),
			Sash.Motion.SweptLocalPoint(FVector(26.5, 0.0, 0.0), Alpha).Equals(FVector(26.5, 0.0, 0.0), 0.001));
	}

	// The bottom edge, at the limit: 38 cm below a hinge turned 30 degrees is 19 out and 32.9 down.
	const FVector BottomEdgeLocal(26.5, 0.0, -38.0);
	const FVector BottomEdgeOpen = PosedPoint(Sash, BottomEdgeLocal, 1.0);

	TestNearlyEqual(TEXT("Open, the bottom edge swings out along the wall normal"), BottomEdgeOpen.Y, 19.0, 0.05);
	TestNearlyEqual(TEXT("Open, the bottom edge lifts as it swings"), BottomEdgeOpen.Z, 218.591, 0.05);
	TestNearlyEqual(TEXT("It does not travel along the wall at all"), BottomEdgeOpen.X, 200.0, 0.001);

	// And nothing on the sash ever rises above the hinge line, which is what says it is hung on the
	// right face. Hung on the other one it would drive into the lintel as it opened.
	for (double Alpha = 0.0; Alpha <= 1.0001; Alpha += 0.05)
	{
		const FAxisAlignedBox3d Posed = PosedBounds(Sash, Alpha);
		if (Posed.Max.Z > 251.5 + 0.01)
		{
			AddError(FString::Printf(
				TEXT("At %.2f open, the ventilator sash reaches Z %.2f, above its %.1f hinge line; it is swinging into the lintel."),
				Alpha, Posed.Max.Z, 251.5));
			break;
		}
		if (Posed.Min.X < 173.5 - 0.01 || Posed.Max.X > 226.5 + 0.01)
		{
			AddError(FString::Printf(
				TEXT("At %.2f open, the ventilator sash spans %.2f..%.2f along the wall and leaves its 173.5..226.5 clear opening."),
				Alpha, Posed.Min.X, Posed.Max.X));
			break;
		}
	}

	// Closed, it sits in the frame rather than in the room.
	const FAxisAlignedBox3d Shut = PosedBounds(Sash, 0.0);
	TestTrue(TEXT("Closed, the sash lies in the plane of the wall"), Shut.Height() < 6.0);
	TestTrue(TEXT("Closed, the sash is inside the wall"),
		Shut.Min.Y >= -Wall.Thickness * 0.5 && Shut.Max.Y <= Wall.Thickness * 0.5);

	// The other hand: an outward swing turns it around, and the bottom edge goes the other way.
	TArray<FHFMeshPart> Outward;
	FHFGenerators::BuildOpeningParts(
		MakeWindowOpening(EHFOpeningKind::Ventilator, 60.0, 45.0, 210.0, EHFSwing::OutwardLeft), Wall, Outward);

	if (TestEqual(TEXT("The outward variant is one sash too"), Outward.Num(), 1))
	{
		const FVector OutwardEdge = PosedPoint(Outward[0], BottomEdgeLocal, 1.0);
		TestNearlyEqual(TEXT("An outward ventilator swings against the wall normal"), OutwardEdge.Y, -19.0, 0.05);
		TestTrue(TEXT("...and still never rises above its hinge"),
			PosedBounds(Outward[0], 1.0).Max.Z <= 251.5 + 0.01);
	}

	return true;
}

/** Things that do not move must not pretend to. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFixedOpeningPartsTest, "HouseForge.Articulation.FixedOpenings", HF_TEST_FLAGS)

bool FHFFixedOpeningPartsTest::RunTest(const FString& Parameters)
{
	const FHFWall Wall = MakeHostWall();

	const FHFOpening Window = MakeWindowOpening(EHFOpeningKind::Window);

	TArray<FHFMeshPart> Parts;
	FHFGenerators::BuildOpeningParts(Window, Wall, Parts);
	TestEqual(TEXT("A fixed window has nothing that moves"), Parts.Num(), 0);

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
