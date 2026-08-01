// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/MeshTransforms.h"
#include "Geometry/HFGenerators.h"
#include "Geometry/HFMeshOps.h"
#include "Geometry/HFWardrobeKit.h"
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

	// A 4 cm leaf hung in a frame, as a volume rather than a triangle count: a 900 doorway gives a
	// 776 daylight opening between the frame's 62 mm faces, and the leaf laps 15 mm into the check on
	// each jamb and at the head, less half a centimetre of running clearance all round and a
	// centimetre of undercut at the floor. It is smaller than the hole, which is the whole point -
	// before the frame existed the leaf WAS the hole.
	constexpr double FrameInset = 6.2 - 1.5;
	const double ExpectedVolume =
		(90.0 - 2.0 * FrameInset - 1.0) * 4.0 * (210.0 - FrameInset - 0.5 - 1.0);
	TestNearlyEqual(TEXT("The leaf is a solid of the declared size"),
		TMeshQueries<FDynamicMesh3>::GetVolumeArea(Leaf.Mesh).X, ExpectedVolume, ExpectedVolume * 0.01);

	// Local space: the pivot is the origin, the leaf runs out along +X and up from the sill.
	const FAxisAlignedBox3d Local = Leaf.Mesh.GetBounds();
	TestTrue(TEXT("The leaf starts at its own pivot"), Local.Min.X >= -0.01 && Local.Min.X <= 1.0);
	TestNearlyEqual(TEXT("The leaf runs its width along local X"),
		Local.Max.X, 90.0 - 2.0 * FrameInset - 0.5, 0.01);
	TestTrue(TEXT("The leaf is undercut clear of the floor"), Local.Min.Z >= 0.5);
	TestTrue(TEXT("The leaf stops under the frame's head"), Local.Max.Z <= 210.0 - FrameInset);

	for (const int32 Tid : Leaf.Mesh.TriangleIndicesItr())
	{
		if (FHFMeshOps::RoleForGroup(Leaf.Mesh.GetTriangleGroup(Tid)) != EHFSurfaceRole::DoorLeaf)
		{
			AddError(TEXT("A door leaf triangle was emitted without the DoorLeaf surface role."));
			break;
		}
	}

	// Closed, the leaf fills the DAYLIGHT opening its frame leaves: the opening runs 155..245 along
	// the wall, and the leaf runs between the two checks inside that.
	const FAxisAlignedBox3d Closed = PosedBounds(Leaf, 0.0);
	TestNearlyEqual(TEXT("Closed, the leaf starts inside the near check"),
		Closed.Min.X, 155.0 + FrameInset + 0.5, 0.01);
	TestNearlyEqual(TEXT("Closed, the leaf reaches the far check"),
		Closed.Max.X, 245.0 - FrameInset - 0.5, 0.01);
	TestTrue(TEXT("Closed, the leaf lies in the plane of the wall"), Closed.Height() < 5.0);

	// And it is IN its frame rather than in the hole: flush with the frame's room-side face, which
	// stands a few millimetres proud of the wall it is fixed to.
	TestNearlyEqual(TEXT("Closed, the leaf finishes flush with the frame's face"),
		Closed.Max.Y, Wall.Thickness * 0.5 + 0.6, 0.01);

	// Open, the same leaf stands square to the wall on the inward side, hinged at the near jamb.
	const FAxisAlignedBox3d Open = PosedBounds(Leaf, 1.0);
	TestTrue(TEXT("Open, the leaf is square to the wall"), Open.Width() < 5.0);
	TestNearlyEqual(TEXT("Open, the leaf swings its own width off its hinge"),
		Open.Max.Y - Open.Min.Y, Local.Max.X - Local.Min.X, 0.01);
	TestTrue(TEXT("An inward-left door hinges at the near jamb"),
		FMath::Abs(Open.Min.X - (155.0 + FrameInset)) < 3.0);
	TestTrue(TEXT("Open, the opening is clear"), Open.Max.X < 245.0);

	// The other three swings, checked on which side of the wall the leaf ends up and which jamb it
	// hangs on. A door on the wrong jamb reads as correct in elevation and wrong in a walkthrough.
	struct FSwingCase
	{
		EHFSwing Swing;
		double TipSide;
		double ExpectedHingeX;
	};

	const FSwingCase Cases[] = {
		{ EHFSwing::InwardRight,   1.0,  245.0 - FrameInset },
		{ EHFSwing::OutwardLeft,  -1.0,  155.0 + FrameInset },
		{ EHFSwing::OutwardRight, -1.0,  245.0 - FrameInset },
	};

	// The leaf's own width off the hinge, plus the half wall the frame's face sits on: a leaf hung
	// in a frame pivots about the frame, not about the centreline of the masonry.
	const double ExpectedTipY = Local.Max.X + Wall.Thickness * 0.5 + 0.6;

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
		const double TipY = Case.TipSide > 0.0 ? Swung.Max.Y : Swung.Min.Y;

		TestNearlyEqual(TEXT("The leaf swings to the declared side of the wall"),
			TipY, Case.TipSide * ExpectedTipY, 0.5);
		TestTrue(TEXT("The leaf hangs on the declared jamb"),
			FMath::Abs(Swung.Center().X - Case.ExpectedHingeX) < 3.0);
	}

	// And the one-piece snapshot is the closed assembly: the leaf AND the frame it hangs in, which
	// is now more than the leaf. It has to contain the closed leaf exactly and reach past it to the
	// masonry the frame is buried in.
	const FDynamicMesh3 Snapshot = FHFGenerators::GenerateOpeningInfill(MakeDoorOpening(), Wall);
	const FAxisAlignedBox3d SnapshotBounds = Snapshot.GetBounds();

	TestTrue(TEXT("The closed snapshot is more than the leaf on its own"),
		TMeshQueries<FDynamicMesh3>::GetVolumeArea(Snapshot).X > ExpectedVolume * 1.05);
	TestTrue(TEXT("The closed snapshot contains the closed leaf"),
		SnapshotBounds.Contains(Closed.Min) && SnapshotBounds.Contains(Closed.Max));
	TestTrue(TEXT("The closed snapshot reaches the masonry on both jambs"),
		SnapshotBounds.Min.X <= 155.0 && SnapshotBounds.Max.X >= 245.0);

	return true;
}

/**
 * A sliding unit is two panels, and EITHER of them has somewhere to move to.
 *
 * One leaf the full width of the opening had nowhere to go: sliding it its own width drove 1.65 m
 * of it into the masonry beside the jamb, and in the reference flat straight through the next
 * window along. Half the opening each, on two tracks, is both what a sliding unit is and what
 * keeps every panel inside the reveal at every open amount.
 *
 * The panel that used to be furniture now runs too - see HouseForge.Articulation.SliderOpensBothWays
 * for the aperture that produces. What is measured here is that giving it a motion did not cost it
 * anything: it is still a glazed solid, still inside the reveal at every amount, and still on its
 * own track.
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
		[](const FHFMeshPart& P) { return P.PartId == FName(TEXT("LeafFar")); });
	const FHFMeshPart* Leaf = Parts.FindByPredicate(
		[](const FHFMeshPart& P) { return P.PartId == FName(TEXT("LeafNear")); });

	if (!TestNotNull(TEXT("It has a far panel"), Fixed) || !TestNotNull(TEXT("It has a near panel"), Leaf))
	{
		return false;
	}

	// BOTH SLIDE. Neither panel is furniture: a real two-track unit has gear on each leaf and you
	// push whichever one you want.
	TestTrue(TEXT("The far panel slides"), Fixed->Motion.Type == EHFMotionType::Slide);
	TestTrue(TEXT("The near panel slides"), Leaf->Motion.Type == EHFMotionType::Slide);
	TestTrue(TEXT("They run opposite ways"), Fixed->Motion.MaxTravelCm * Leaf->Motion.MaxTravelCm < 0.0);

	// An undrawn swing opens from the near jamb, matching the hinged case where an undrawn swing
	// hangs on the near jamb too.
	TestTrue(TEXT("One control runs the near panel by default"), Leaf->Motion.bMasterOpens);
	TestFalse(TEXT("and leaves the far one alone"), Fixed->Motion.bMasterOpens);

	// Both panels are solids in their own right, because a bake treats each as a mesh of its own.
	TestTrue(TEXT("The far panel is watertight"), FHFMeshOps::IsClosed(Fixed->Mesh));
	TestTrue(TEXT("The near panel is watertight"), FHFMeshOps::IsClosed(Leaf->Mesh));

	// Both panels are GLAZED, and the glass is a solid with thickness rather than a plane. A pane
	// modelled as a plane has area but no volume at all, and every bounds and triangle-count check
	// passes on one - which is how both balcony doors in the reference flat came to be opaque boards.
	for (const FHFMeshPart* Panel : { Fixed, Leaf })
	{
		TestTrue(TEXT("The panel is glazed with a solid, not a plane"),
			RoleVolume(Panel->Mesh, EHFSurfaceRole::Glass) > 0.0);
		TestTrue(TEXT("The panel has a sash section round its glass"),
			RoleVolume(Panel->Mesh, EHFSurfaceRole::WindowFrame) > 0.0);

		// The pane is 8 mm, not the window's 5: a door pane is a bigger, heavier sheet and it must
		// not inherit the window's figure just because the two share a builder.
		const FAxisAlignedBox3d Glass = RoleBounds(Panel->Mesh, EHFSurfaceRole::Glass);
		TestNearlyEqual(TEXT("The pane is a door's 8 mm rather than a window's 5"),
			Glass.Height(), 0.8, 0.001);

		// And it is held: a shoulder of section remains outside the glass on every edge, so the pane
		// is in the groove rather than standing through the sash into the reveal.
		const FAxisAlignedBox3d Sash = RoleBounds(Panel->Mesh, EHFSurfaceRole::WindowFrame);
		TestTrue(TEXT("The pane sits inside the stiles"),
			Glass.Min.X > Sash.Min.X && Glass.Max.X < Sash.Max.X);
		TestTrue(TEXT("The pane sits inside the rails"),
			Glass.Min.Z > Sash.Min.Z && Glass.Max.Z < Sash.Max.Z);
	}

	// Nothing about a sliding door is a door LEAF any more. It is a framed, glazed sash, and if it
	// carries the leaf role the material panel will paint it like a flush shutter.
	TestNearlyEqual(TEXT("A sliding door has no opaque leaf in it"),
		RoleVolume(Leaf->Mesh, EHFSurfaceRole::DoorLeaf), 0.0, 0.001);

	// The opening spans 155..245 along this wall. Every panel stays inside it at every open amount,
	// which is the property the old single leaf broke.
	constexpr double OpeningMin = 155.0;
	constexpr double OpeningMax = 245.0;

	const FAxisAlignedBox3d FixedBounds = PosedBounds(*Fixed, 0.0);
	TestTrue(TEXT("The far panel sits inside the opening"),
		FixedBounds.Min.X >= OpeningMin - 0.01 && FixedBounds.Max.X <= OpeningMax + 0.01);

	// BOTH panels are swept now, not just the one that used to move. A leaf given a travel it was
	// never checked against is a leaf that can run into the masonry, and the far one is set out from
	// the other jamb - so it is the one whose sign could be wrong without anything else noticing.
	for (const FHFMeshPart* Panel : { Leaf, Fixed })
	{
		for (double Alpha = 0.0; Alpha <= 1.0001; Alpha += 0.05)
		{
			const FAxisAlignedBox3d Posed = PosedBounds(*Panel, Alpha);
			if (Posed.Min.X < OpeningMin - 0.01 || Posed.Max.X > OpeningMax + 0.01)
			{
				AddError(FString::Printf(
					TEXT("At %.2f open, panel '%s' spans %.2f..%.2f and leaves the %.0f..%.0f opening; it is sliding into the wall."),
					Alpha, *Panel->PartId.ToString(), Posed.Min.X, Posed.Max.X, OpeningMin, OpeningMax));
				break;
			}
		}
	}

	const FAxisAlignedBox3d Closed = PosedBounds(*Leaf, 0.0);
	const FAxisAlignedBox3d Open = PosedBounds(*Leaf, 1.0);

	// Closed, the two panels together fill the CLEAR opening with no daylight between their meeting
	// stiles - they interlock rather than merely abut. Clear, not masonry: the outer frame eats a
	// face width off each jamb, and the panels fill what it leaves.
	constexpr double OuterFrameFace = 5.5;
	TestTrue(TEXT("Closed, the panels overlap at the meeting stile"), Closed.Max.X > FixedBounds.Min.X + 1.0);
	TestTrue(TEXT("Closed, the pair fills the clear opening"),
		Closed.Min.X <= OpeningMin + OuterFrameFace + 0.01
			&& FixedBounds.Max.X >= OpeningMax - OuterFrameFace - 0.01);

	// And the outer frame fills what is left, so nothing between the masonry and the glass is a gap.
	const FDynamicMesh3 Outer = FHFGenerators::GenerateOpeningFixedInfill(
		MakeDoorOpening(EHFOpeningKind::SlidingDoor, EHFSwing::None), Wall);
	const FAxisAlignedBox3d OuterBounds = Outer.GetBounds();

	TestTrue(TEXT("A sliding door has an outer frame"),
		RoleVolume(Outer, EHFSurfaceRole::WindowFrame) > 0.0);
	TestTrue(TEXT("The outer frame reaches both jambs"),
		OuterBounds.Min.X <= OpeningMin + 0.01 && OuterBounds.Max.X >= OpeningMax - 0.01);
	TestTrue(TEXT("The outer frame has tracks for the panels to run on"),
		RoleVolume(Outer, EHFSurfaceRole::MetalHardware) > 0.0);

	// The threshold: the one member a hinged door frame does not have. It stands on the floor and
	// the panels stand on it, which is why the glass starts above the sill rather than at it.
	TestNearlyEqual(TEXT("The threshold stands on the floor"), OuterBounds.Min.Z, 0.0, 0.01);
	TestTrue(TEXT("The panels stand on the threshold rather than on the floor"), Closed.Min.Z >= 2.99);

	// Open, the near panel has come to rest exactly over its partner, which is as far as it can
	// travel without leaving the reveal.
	TestNearlyEqual(TEXT("Open, the near panel stacks on the far one"), Open.Max.X, FixedBounds.Max.X, 0.01);

	// And the far panel does the mirror of it: run the other leaf and it comes to rest exactly over
	// the near one's shut position. Same rule, mirrored, which is the point of FHFSlidingSetOut.
	TestNearlyEqual(TEXT("Run the other way, the far panel stacks on the near one"),
		PosedBounds(*Fixed, 1.0).Min.X, Closed.Min.X, 0.01);
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
		[](const FHFMeshPart& P) { return P.PartId == FName(TEXT("SashFar")); });
	const FHFMeshPart* Sash = Parts.FindByPredicate(
		[](const FHFMeshPart& P) { return P.PartId == FName(TEXT("SashNear")); });

	if (!TestNotNull(TEXT("It has a far sash"), Fixed) || !TestNotNull(TEXT("It has a near sash"), Sash))
	{
		return false;
	}

	// Both sashes run. A two-track Domal unit has a roller carriage under each sash and a catch on
	// each meeting stile; which one you slide is which end of the window ends up open.
	TestTrue(TEXT("The far sash slides"), Fixed->Motion.Type == EHFMotionType::Slide);
	TestTrue(TEXT("The near sash slides"), Sash->Motion.Type == EHFMotionType::Slide);
	TestTrue(TEXT("They run opposite ways"), Fixed->Motion.MaxTravelCm * Sash->Motion.MaxTravelCm < 0.0);
	TestTrue(TEXT("It slides along the wall, not through it"),
		Sash->Motion.UnitAxis().Equals(FVector::XAxisVector, 0.0001));

	// Each sash is a solid in its own right, because a bake treats every part as a mesh of its own.
	TestTrue(TEXT("The far sash is watertight"), FHFMeshOps::IsClosed(Fixed->Mesh));
	TestTrue(TEXT("The near sash is watertight"), FHFMeshOps::IsClosed(Sash->Mesh));

	// Each carries its own catch, because either of them is the one somebody reaches for. A unit
	// with one handle is a unit half of which cannot be operated - invisible in a still of a shut
	// window, and the first thing anybody notices sliding it the other way.
	TestTrue(TEXT("Each sash carries its own catch"),
		RoleVolume(Fixed->Mesh, EHFSurfaceRole::MetalHardware) > 0.0
			&& RoleVolume(Sash->Mesh, EHFSurfaceRole::MetalHardware) > 0.0);

	// And they are on OPPOSITE stiles - both leaves meet in the middle, so the near sash's catch is
	// on its far edge and the far sash's on its near edge. Read off the geometry, because deriving
	// it inside the sash builder is exactly what would put both pulls at the same end.
	{
		const FAxisAlignedBox3d NearCatch = RoleBounds(Sash->Mesh, EHFSurfaceRole::MetalHardware);
		const FAxisAlignedBox3d FarCatch = RoleBounds(Fixed->Mesh, EHFSurfaceRole::MetalHardware);
		const FAxisAlignedBox3d NearSash = RoleBounds(Sash->Mesh, EHFSurfaceRole::WindowFrame);
		const FAxisAlignedBox3d FarSash = RoleBounds(Fixed->Mesh, EHFSurfaceRole::WindowFrame);

		TestTrue(TEXT("The near sash's catch is on its meeting stile"),
			NearCatch.Center().X > NearSash.Center().X);
		TestTrue(TEXT("The far sash's catch is on its meeting stile"),
			FarCatch.Center().X < FarSash.Center().X);
	}

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
	TestTrue(TEXT("The far sash sits inside the clear opening"),
		FixedBounds.Min.X >= ClearMin - 0.01 && FixedBounds.Max.X <= ClearMax + 0.01);

	// EITHER sash may be the one that runs, so both are swept - and each is swept against its
	// partner SHUT, which is the pose the partner is actually in while this one is running. The
	// interlock this proves is the one that matters: a leaf must never leave its reveal and must
	// never pass through the other leaf, whichever of the two is doing the moving.
	for (const FHFMeshPart* Runner : { Sash, Fixed })
	{
		const FHFMeshPart* Partner = Runner == Sash ? Fixed : Sash;
		const FAxisAlignedBox3d PartnerShut = PosedBounds(*Partner, 0.0);

		for (double Alpha = 0.0; Alpha <= 1.0001; Alpha += 0.05)
		{
			const FAxisAlignedBox3d Posed = PosedBounds(*Runner, Alpha);

			if (Posed.Min.X < ClearMin - 0.01 || Posed.Max.X > ClearMax + 0.01
				|| Posed.Min.Z < ClearBottom - 0.01 || Posed.Max.Z > ClearTop + 0.01)
			{
				AddError(FString::Printf(
					TEXT("At %.2f open, sash '%s' spans %.2f..%.2f along the wall and %.2f..%.2f up, and leaves the %.1f..%.1f x %.1f..%.1f clear opening; it is running into the frame."),
					Alpha, *Runner->PartId.ToString(), Posed.Min.X, Posed.Max.X, Posed.Min.Z, Posed.Max.Z,
					ClearMin, ClearMax, ClearBottom, ClearTop));
				break;
			}

			if (BoxesOverlap(Posed, PartnerShut))
			{
				AddError(FString::Printf(
					TEXT("At %.2f open, sash '%s' shares volume with its partner; they are not on separate tracks."),
					Alpha, *Runner->PartId.ToString()));
				break;
			}
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

	// Open, the near sash has come to rest exactly over its partner - as far as it can go without
	// leaving the reveal - and the aperture is at the NEAR jamb, which is the end its own bay was.
	TestNearlyEqual(TEXT("Open, the near sash stacks on the far one"), Open.Max.X, FixedBounds.Max.X, 0.01);
	TestTrue(TEXT("Open, roughly half the window is clear"),
		Open.Min.X - ClearMin >= (ClearMax - ClearMin) * 0.4);

	// Run the OTHER sash and the aperture is at the far jamb instead. Same rule mirrored, which is
	// the whole of what "open it both ways" means for a two-track unit.
	{
		const FAxisAlignedBox3d FarOpen = PosedBounds(*Fixed, 1.0);
		TestNearlyEqual(TEXT("Run the other way, the far sash stacks on the near one"),
			FarOpen.Min.X, Closed.Min.X, 0.01);
		TestTrue(TEXT("...and roughly half the window is clear at the FAR jamb"),
			ClearMax - FarOpen.Max.X >= (ClearMax - ClearMin) * 0.4);
	}

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

/**
 * A fan revolves, which is not the same thing as opening.
 *
 * The defect this exists for: a ceiling fan expressed as a hinge could not turn past 180 degrees,
 * because that is the clamp a hinge rightly carries. Three ceiling fans and two exhaust fans in the
 * reference flat need continuous revolution, and continuous revolution is not an amount between two
 * end stops - so it is its own motion with its own unbounded phase.
 *
 * Measured as ANGLE ACTUALLY SWEPT, summed along the path, rather than by reading the end pose. A
 * rotation is periodic: two and a half turns and half a turn land a blade in exactly the same place,
 * so any test that only looks at where the blade ended up cannot tell a fan that revolved from one
 * that never left its first turn.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSpinMotionTest, "HouseForge.Articulation.SpinMotion", HF_TEST_FLAGS)

bool FHFSpinMotionTest::RunTest(const FString& Parameters)
{
	// A 1200 mm ceiling fan on speed 5: three blades on a vertical axis, 300 rpm.
	FHFPartMotion Spin;
	Spin.Type = EHFMotionType::Spin;
	Spin.Axis = FVector::ZAxisVector;
	Spin.RevolutionsPerMinute = 300.0;

	const FVector BladeTip(60.0, 0.0, 0.0);

	TestTrue(TEXT("A fan is a moving part"), Spin.Moves());
	TestTrue(TEXT("A fan revolves"), Spin.Revolves());
	TestFalse(TEXT("A fan does not open"), Spin.Opens());

	// Stopped is exactly where the generator put it, like every other closed part.
	TestTrue(TEXT("A fan at phase 0 applies no offset at all"),
		Spin.SpinOffsetAt(0.0).Equals(FTransform::Identity));

	// A quarter turn is a quarter turn, in the direction the axis says.
	TestTrue(TEXT("A quarter turn puts the blade a quarter of the way round"),
		Spin.SpinOffsetAt(0.25).TransformPosition(BladeTip).Equals(FVector(0.0, 60.0, 0.0), 0.01));
	TestTrue(TEXT("A whole turn brings the blade back to where it started"),
		Spin.SpinOffsetAt(1.0).TransformPosition(BladeTip).Equals(BladeTip, 0.01));

	// ---------------------------------------------------------------- past 360 degrees, and on
	//
	// The defect, as a measurement. Ten revolutions swept in small steps: the angle actually turned
	// through has to come to ten turns' worth, which is 3600 degrees and twenty times the 180 a
	// hinge is clamped to. A phase that wrapped, saturated, or clamped fails this while still
	// putting the blade somewhere plausible.
	constexpr double Revolutions = 10.0;
	constexpr int32 Steps = 3600;

	double SweptDegrees = 0.0;
	FVector Previous = Spin.SpinOffsetAt(0.0).TransformPosition(BladeTip);

	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const double Turns = Revolutions * Step / Steps;
		const FVector Current = Spin.SpinOffsetAt(Turns).TransformPosition(BladeTip);

		SweptDegrees += FMath::RadiansToDegrees(
			FMath::Acos(FMath::Clamp(Previous.GetSafeNormal().Dot(Current.GetSafeNormal()), -1.0, 1.0)));

		// A revolution is a rotation about the axis: the blade keeps its radius and its height the
		// whole way round, however many turns in. A part translated into a convincing pose does not.
		TestNearlyEqual(TEXT("A revolving blade keeps its radius"), FVector2D(Current.X, Current.Y).Size(), 60.0, 0.001);
		TestNearlyEqual(TEXT("A revolving blade keeps its height"), Current.Z, 0.0, 0.001);

		Previous = Current;
	}

	TestNearlyEqual(TEXT("Ten revolutions really is ten revolutions of swept angle"),
		SweptDegrees, Revolutions * 360.0, 1.0);
	TestTrue(TEXT("A fan turns far past the 180 degrees a hinge is clamped to"), SweptDegrees > 180.0);

	// Both directions, and fractional phases beyond a turn, all of which a 0..1 amount cannot hold.
	TestTrue(TEXT("A phase past a full turn is where that many turns lands"),
		Spin.SpinOffsetAt(3.25).TransformPosition(BladeTip).Equals(FVector(0.0, 60.0, 0.0), 0.01));
	TestTrue(TEXT("A negative phase turns the other way"),
		Spin.SpinOffsetAt(-0.25).TransformPosition(BladeTip).Equals(FVector(0.0, -60.0, 0.0), 0.01));

	// The rate is the thing an artist knows about a fan: 300 rpm is five turns a second.
	TestNearlyEqual(TEXT("300 rpm turns five times a second"), Spin.TurnsInSeconds(1.0), 5.0, 1e-9);

	FHFPartMotion Exhaust;
	Exhaust.Type = EHFMotionType::Spin;
	Exhaust.Axis = FVector::YAxisVector;
	Exhaust.RevolutionsPerMinute = 1350.0;
	TestNearlyEqual(TEXT("An exhaust fan turns at its own rate"), Exhaust.TurnsInSeconds(2.0), 45.0, 1e-9);

	// An exhaust fan is set into a wall, so its axis is horizontal - and the phase turns it about
	// that axis rather than about a vertical one it does not have.
	TestTrue(TEXT("An exhaust fan revolves about the axis it was given"),
		Exhaust.SpinOffsetAt(0.25).TransformPosition(FVector(7.5, 0.0, 0.0)).Equals(FVector(0.0, 0.0, -7.5), 0.01));

	// ------------------------------------------------------------------- a fan is not an opening

	// No open amount can move a fan. This is what stops "open everything" from stopping every fan
	// in the flat at some arbitrary angle.
	TestTrue(TEXT("An open amount does not turn a fan"),
		Spin.OffsetAt(1.0).Equals(FTransform::Identity));
	TestTrue(TEXT("Not even a half-open one"), Spin.OffsetAt(0.5).Equals(FTransform::Identity));

	// And the pose comes from the phase, through the state, however the amount is set.
	FHFPartState Fan;
	Fan.PartId = TEXT("Fan");
	Fan.Motion = Spin;
	Fan.PivotTransform = FTransform(FRotator::ZeroRotator, FVector(300.0, 200.0, 280.0));
	Fan.SpinTurns = 7.25;

	TestTrue(TEXT("A fan is posed by its phase, about its own pivot"),
		Fan.CurrentPose().TransformPosition(BladeTip).Equals(FVector(300.0, 260.0, 280.0), 0.01));
	TestTrue(TEXT("An open amount cannot pose a fan at all"),
		Fan.PoseAt(1.0).TransformPosition(BladeTip).Equals(Fan.PoseAt(0.0).TransformPosition(BladeTip), 1e-9));

	// ------------------------------------------------------------ and a door is still a door
	//
	// The other half of the requirement. Whatever a fan needed, it must not have leaked into the
	// motion every door in the flat uses.
	FHFPartState Door;
	Door.PartId = TEXT("Leaf");
	Door.Motion.Type = EHFMotionType::Hinge;
	Door.Motion.Axis = FVector::ZAxisVector;
	Door.Motion.MaxAngleDegrees = 90.0;
	Door.SpinTurns = 5.0;
	Door.OpenAmount = 1.0;

	TestFalse(TEXT("A door does not revolve"), Door.Motion.Revolves());
	TestTrue(TEXT("A door opens"), Door.Motion.Opens());
	TestTrue(TEXT("A stray phase does not turn a door"),
		Door.CurrentPose().TransformPosition(FVector(100.0, 0.0, 0.0)).Equals(FVector(0.0, 100.0, 0.0), 0.01));

	return true;
}

/**
 * A part can be made to wait for another, and one master amount then respects the order.
 *
 * The composition this exists for is a drawer inside a wardrobe: its runners are screwed to the
 * carcass so it cannot be parented to the leaf, but it still cannot come out through one that is
 * shut. That is an ordering between two independent parts, and this is the ordering itself, tested
 * on the numbers. HouseForge.Joinery.InternalDrawerInterlock tests it on the geometry.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSequencedPartsTest, "HouseForge.Articulation.SequencedParts", HF_TEST_FLAGS)

bool FHFSequencedPartsTest::RunTest(const FString& Parameters)
{
	// Leaf, the drawer behind it, and the intermediate runner member geared to that drawer. Ordered
	// in the array so that no single pass in array order could resolve it: the runner comes first.
	auto MakeWardrobe = []()
	{
		TArray<FHFPartState> Parts;

		FHFPartState Runner;
		Runner.PartId = TEXT("DrawerRunner");
		Runner.Motion.Type = EHFMotionType::Slide;
		Runner.Motion.Axis = -FVector::YAxisVector;
		Runner.Motion.MaxTravelCm = 22.5;
		Runner.Motion.DrivenByPartId = TEXT("Drawer");
		Parts.Add(Runner);

		FHFPartState Drawer;
		Drawer.PartId = TEXT("Drawer");
		Drawer.Motion.Type = EHFMotionType::Slide;
		Drawer.Motion.Axis = -FVector::YAxisVector;
		Drawer.Motion.MaxTravelCm = 45.0;
		Drawer.Motion.SequencedAfterPartId = TEXT("Leaf");
		Drawer.Motion.SequenceThreshold = 0.5;
		Parts.Add(Drawer);

		FHFPartState Leaf;
		Leaf.PartId = TEXT("Leaf");
		Leaf.Motion.Type = EHFMotionType::Hinge;
		Leaf.Motion.Axis = FVector::ZAxisVector;
		Leaf.Motion.MaxAngleDegrees = -100.0;
		Parts.Add(Leaf);

		return Parts;
	};

	auto AmountOf = [](const TArray<FHFPartState>& Parts, const TCHAR* Id)
	{
		const FHFPartState* Part = Parts.FindByPredicate(
			[Id](const FHFPartState& P) { return P.PartId == FName(Id); });
		return Part != nullptr ? Part->OpenAmount : -1.0;
	};

	// Every part asked for the same amount, which is what MasterOpenAmount does.
	auto AtMaster = [&MakeWardrobe](double Master)
	{
		TArray<FHFPartState> Parts = MakeWardrobe();
		for (FHFPartState& Part : Parts)
		{
			Part.OpenAmount = Master;
		}
		FHFArticulation::ResolvePartAmounts(Parts);
		return Parts;
	};

	// Below the threshold the drawer has not moved at all, however far the master has been driven.
	for (const double Master : { 0.0, 0.1, 0.25, 0.4, 0.5 })
	{
		const TArray<FHFPartState> Parts = AtMaster(Master);

		TestNearlyEqual(TEXT("The leaf follows the master amount"), AmountOf(Parts, TEXT("Leaf")), Master, 1e-9);
		TestNearlyEqual(*FString::Printf(
				TEXT("At %.2f open the leaf is not yet clear, so the drawer is still shut"), Master),
			AmountOf(Parts, TEXT("Drawer")), 0.0, 1e-9);
		TestNearlyEqual(TEXT("...and its runner has stayed with it"),
			AmountOf(Parts, TEXT("DrawerRunner")), 0.0, 1e-9);
	}

	// Past it, the drawer comes out - proportionally, so it trails the leaf rather than jumping.
	{
		const TArray<FHFPartState> Parts = AtMaster(0.75);
		TestNearlyEqual(TEXT("Once the leaf is clear the drawer starts to come out"),
			AmountOf(Parts, TEXT("Drawer")), 0.5, 1e-9);
		TestNearlyEqual(TEXT("And the geared runner follows it in the same call"),
			AmountOf(Parts, TEXT("DrawerRunner")), 0.5, 1e-9);
	}

	{
		const TArray<FHFPartState> Parts = AtMaster(1.0);
		TestNearlyEqual(TEXT("Fully open, the leaf is fully open"), AmountOf(Parts, TEXT("Leaf")), 1.0, 1e-9);
		TestNearlyEqual(TEXT("Fully open, the drawer is all the way out"),
			AmountOf(Parts, TEXT("Drawer")), 1.0, 1e-9);
		TestNearlyEqual(TEXT("Fully open, so is its runner"),
			AmountOf(Parts, TEXT("DrawerRunner")), 1.0, 1e-9);
	}

	// The drawer never gets ahead of what the leaf allows, at any master amount at all.
	for (int32 Step = 0; Step <= 100; ++Step)
	{
		const double Master = Step / 100.0;
		const TArray<FHFPartState> Parts = AtMaster(Master);
		const double Drawer = AmountOf(Parts, TEXT("Drawer"));

		if (Drawer > AmountOf(Parts, TEXT("Leaf")) + 1e-9)
		{
			AddError(FString::Printf(
				TEXT("At master %.2f the drawer is %.3f open while its leaf is only %.3f - it is coming out through the shutter."),
				Master, Drawer, AmountOf(Parts, TEXT("Leaf"))));
			break;
		}
	}

	// Posing the drawer ON ITS OWN is capped the same way: the interlock is a property of the
	// assembly, not a special case of the master control.
	{
		TArray<FHFPartState> Parts = MakeWardrobe();
		Parts[1].OpenAmount = 1.0;   // the drawer, hauled all the way out
		Parts[2].OpenAmount = 0.0;   // with the leaf shut
		FHFArticulation::ResolvePartAmounts(Parts);

		TestNearlyEqual(TEXT("A drawer cannot be pulled out through a shut leaf"),
			AmountOf(Parts, TEXT("Drawer")), 0.0, 1e-9);
	}

	{
		TArray<FHFPartState> Parts = MakeWardrobe();
		Parts[1].OpenAmount = 1.0;
		Parts[2].OpenAmount = 0.6;   // the leaf just past its threshold
		FHFArticulation::ResolvePartAmounts(Parts);

		// 0.6 is a fifth of the way from the 0.5 threshold to fully open, so that is as far as the
		// drawer may travel however hard it is pulled.
		TestNearlyEqual(TEXT("A part-open leaf lets its drawer out only that far"),
			AmountOf(Parts, TEXT("Drawer")), 0.2, 1e-9);
		TestNearlyEqual(TEXT("The runner is geared to what the drawer actually did"),
			AmountOf(Parts, TEXT("DrawerRunner")), 0.2, 1e-9);
	}

	// A part that declares no ordering is not restricted by anything, which is what keeps every
	// fixture built before this existed behaving exactly as it did.
	{
		TArray<FHFPartState> Parts = MakeWardrobe();
		Parts[1].Motion.SequencedAfterPartId = NAME_None;
		for (FHFPartState& Part : Parts)
		{
			Part.OpenAmount = 0.3;
		}
		FHFArticulation::ResolvePartAmounts(Parts);

		TestNearlyEqual(TEXT("An unsequenced drawer opens as far as it was asked to"),
			AmountOf(Parts, TEXT("Drawer")), 0.3, 1e-9);
	}

	// A threshold of 1 is the strictest ordering there is: nothing until the leaf is fully open.
	{
		TArray<FHFPartState> Parts = MakeWardrobe();
		Parts[1].Motion.SequenceThreshold = 1.0;
		Parts[1].OpenAmount = 1.0;
		Parts[2].OpenAmount = 0.999;
		FHFArticulation::ResolvePartAmounts(Parts);
		TestNearlyEqual(TEXT("A threshold of 1 means not until it is all the way open"),
			AmountOf(Parts, TEXT("Drawer")), 0.0, 1e-9);

		Parts = MakeWardrobe();
		Parts[1].Motion.SequenceThreshold = 1.0;
		Parts[1].OpenAmount = 1.0;
		Parts[2].OpenAmount = 1.0;
		FHFArticulation::ResolvePartAmounts(Parts);
		TestNearlyEqual(TEXT("...and lets it straight out once it is"),
			AmountOf(Parts, TEXT("Drawer")), 1.0, 1e-9);
	}

	// An ordering naming a part that is not in the assembly is ignored rather than blocking
	// everything: freezing every drawer in a fixture over one bad name would be a second and more
	// confusing failure than the first.
	//
	// BUT IT IS NOT SILENT, and it used to be. Ignoring the ordering leaves the part completely
	// unconstrained, which is precisely the pre-interlock behaviour this whole mechanism exists to
	// remove - so a shutter part renamed without its drawer's ordering renamed with it would quietly
	// revert the physical-pose guarantee, with nothing anywhere saying so. Nothing validates a
	// declared dependency at generation either: AHFArticulatedActor::RegenerateParts checks empty
	// ids and duplicate ids and never that a named part exists. The report below is the only thing
	// that catches it, so the permissiveness and the diagnostic are asserted together.
	{
		TArray<FHFPartState> Parts = MakeWardrobe();
		Parts[1].Motion.SequencedAfterPartId = TEXT("NoSuchLeaf");
		Parts[1].OpenAmount = 1.0;

		TArray<FHFUnresolvedDependency> Unresolved;
		TestTrue(TEXT("An ordering naming a part that does not exist still resolves"),
			FHFArticulation::ResolvePartAmounts(Parts, nullptr, &Unresolved));
		TestNearlyEqual(TEXT("...and does not freeze the part that declared it"),
			AmountOf(Parts, TEXT("Drawer")), 1.0, 1e-9);

		if (TestEqual(TEXT("...but the dangling ordering is reported rather than swallowed"),
			Unresolved.Num(), 1))
		{
			TestEqual(TEXT("...naming the part that declared it"), Unresolved[0].PartId, FName(TEXT("Drawer")));
			TestEqual(TEXT("...and the id it could not find"), Unresolved[0].MissingPartId, FName(TEXT("NoSuchLeaf")));
			TestFalse(TEXT("...and saying it was an ordering, not a gearing"), Unresolved[0].bGearing);
		}
	}

	// The same for gearing, which fails the same way and is the other half of the pair.
	{
		TArray<FHFPartState> Parts = MakeWardrobe();
		// Its own sound ordering is taken off first, so the only thing capping the drawer here is
		// the dangling gearing - otherwise the shut leaf would hold it at 0 and the assertion below
		// would pass for the wrong reason.
		Parts[1].Motion.SequencedAfterPartId = NAME_None;
		Parts[1].Motion.DrivenByPartId = TEXT("NoSuchDriver");
		Parts[1].OpenAmount = 0.4;

		TArray<FHFUnresolvedDependency> Unresolved;
		TestTrue(TEXT("A gearing naming a part that does not exist still resolves"),
			FHFArticulation::ResolvePartAmounts(Parts, nullptr, &Unresolved));
		TestNearlyEqual(TEXT("...and leaves the part on its own amount"),
			AmountOf(Parts, TEXT("Drawer")), 0.4, 1e-9);

		if (TestEqual(TEXT("...and is reported too"), Unresolved.Num(), 1))
		{
			TestTrue(TEXT("...marked as a gearing"), Unresolved[0].bGearing);
		}
	}

	// A sound assembly reports nothing. Without this the two assertions above would pass just as
	// well against a function that reported every part it ever looked at.
	{
		TArray<FHFPartState> Parts = MakeWardrobe();
		TArray<FHFUnresolvedDependency> Unresolved;
		FHFArticulation::ResolvePartAmounts(Parts, nullptr, &Unresolved);
		TestEqual(TEXT("A wardrobe whose orderings all resolve reports nothing"), Unresolved.Num(), 0);
	}

	return true;
}

/**
 * A chain of geared parts resolves in one call, whatever order the parts are in.
 *
 * The gearing pass used to be a single sweep in array order, so A -> B -> C only worked if the
 * array happened to be in that order and otherwise lagged one call behind. That is invisible in a
 * still, shows up in motion as a part trailing its driver, and depends on nothing but the order a
 * generator emitted its parts in.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFGearedChainTest, "HouseForge.Articulation.GearedChainResolvesAtOnce", HF_TEST_FLAGS)

bool FHFGearedChainTest::RunTest(const FString& Parameters)
{
	// Deliberately in the WORST order: C is geared to B, B to A, and they are stored C, B, A. A
	// single pass in array order leaves C on last call's answer and B on this one's.
	auto MakeChain = []()
	{
		TArray<FHFPartState> Parts;

		for (const TCHAR* Id : { TEXT("C"), TEXT("B"), TEXT("A") })
		{
			FHFPartState Part;
			Part.PartId = Id;
			Part.Motion.Type = EHFMotionType::Slide;
			Part.Motion.Axis = FVector::XAxisVector;
			Part.Motion.MaxTravelCm = 10.0;
			Parts.Add(Part);
		}

		Parts[0].Motion.DrivenByPartId = TEXT("B");
		Parts[1].Motion.DrivenByPartId = TEXT("A");
		return Parts;
	};

	TArray<FHFPartState> Parts = MakeChain();
	Parts[2].OpenAmount = 1.0;   // A, the only part anybody drives

	TestTrue(TEXT("A chain of geared parts resolves"), FHFArticulation::ResolvePartAmounts(Parts));

	// ONE call, not two. Both of the parts downstream have to be there already.
	TestNearlyEqual(TEXT("The driver stands where it was put"), Parts[2].OpenAmount, 1.0, 1e-9);
	TestNearlyEqual(TEXT("The part geared to it followed in the same call"), Parts[1].OpenAmount, 1.0, 1e-9);
	TestNearlyEqual(TEXT("And so did the part geared to THAT one"), Parts[0].OpenAmount, 1.0, 1e-9);

	// Partway, so this cannot pass on the endpoints alone, and back again, so nothing ratchets.
	Parts = MakeChain();
	Parts[2].OpenAmount = 0.4;
	FHFArticulation::ResolvePartAmounts(Parts);
	TestNearlyEqual(TEXT("The whole chain follows partway too"), Parts[0].OpenAmount, 0.4, 1e-9);

	Parts[2].OpenAmount = 0.0;
	FHFArticulation::ResolvePartAmounts(Parts);
	TestNearlyEqual(TEXT("And all the way home again"), Parts[0].OpenAmount, 0.0, 1e-9);

	// A longer chain, to make sure one call is one call and not "two happens to be enough".
	{
		TArray<FHFPartState> Long;
		constexpr int32 Links = 12;

		for (int32 Index = Links - 1; Index >= 0; --Index)
		{
			FHFPartState Part;
			Part.PartId = FName(*FString::Printf(TEXT("Link%d"), Index));
			Part.Motion.Type = EHFMotionType::Slide;
			Part.Motion.Axis = FVector::XAxisVector;
			Part.Motion.MaxTravelCm = 5.0;
			if (Index > 0)
			{
				Part.Motion.DrivenByPartId = FName(*FString::Printf(TEXT("Link%d"), Index - 1));
			}
			Long.Add(Part);
		}

		Long.Last().OpenAmount = 1.0;   // Link0, at the far end of the array from everything it drives
		TestTrue(TEXT("A twelve-link chain resolves"), FHFArticulation::ResolvePartAmounts(Long));

		for (const FHFPartState& Part : Long)
		{
			TestNearlyEqual(*FString::Printf(TEXT("%s followed the chain in one call"), *Part.PartId.ToString()),
				Part.OpenAmount, 1.0, 1e-9);
		}
	}

	return true;
}

/**
 * Parts that depend on each other are refused, not iterated forever.
 *
 * Resolving to a fixed point is what makes a chain work in one call, and a cycle is what turns a
 * fixed-point solve into a hang. A generator that gears two parts to each other is a bug in the
 * generator; an editor that stops responding because of it is a much worse one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCircularArticulationTest,
	"HouseForge.Articulation.CircularRelationshipRefused", HF_TEST_FLAGS)

bool FHFCircularArticulationTest::RunTest(const FString& Parameters)
{
	auto MakePart = [](const TCHAR* Id)
	{
		FHFPartState Part;
		Part.PartId = Id;
		Part.Motion.Type = EHFMotionType::Slide;
		Part.Motion.Axis = FVector::XAxisVector;
		Part.Motion.MaxTravelCm = 10.0;
		return Part;
	};

	// Two parts geared to each other. If this hangs, it hangs the editor.
	{
		TArray<FHFPartState> Parts = { MakePart(TEXT("A")), MakePart(TEXT("B")) };
		Parts[0].Motion.DrivenByPartId = TEXT("B");
		Parts[1].Motion.DrivenByPartId = TEXT("A");
		Parts[0].OpenAmount = 0.25;
		Parts[1].OpenAmount = 0.75;

		TArray<FName> Cyclic;
		TestFalse(TEXT("A pair of parts geared to each other is refused"),
			FHFArticulation::ResolvePartAmounts(Parts, &Cyclic));
		TestEqual(TEXT("Both of them are named"), Cyclic.Num(), 2);

		// Refused means left alone, not zeroed: the amounts they were asked for are the best answer
		// available, and quietly shutting a fixture would look like a posing bug rather than a
		// generator one.
		TestNearlyEqual(TEXT("A keeps what it was asked for"), Parts[0].OpenAmount, 0.25, 1e-9);
		TestNearlyEqual(TEXT("B keeps what it was asked for"), Parts[1].OpenAmount, 0.75, 1e-9);
	}

	// A part naming itself, which no loop over back edges would see as a cycle unless it looked.
	{
		TArray<FHFPartState> Parts = { MakePart(TEXT("Solo")) };
		Parts[0].Motion.DrivenByPartId = TEXT("Solo");
		Parts[0].OpenAmount = 0.5;

		TArray<FName> Cyclic;
		TestFalse(TEXT("A part geared to itself is refused"),
			FHFArticulation::ResolvePartAmounts(Parts, &Cyclic));
		TestNearlyEqual(TEXT("...and keeps the amount it was asked for"), Parts[0].OpenAmount, 0.5, 1e-9);
	}

	// A longer loop, through the sequencing edge as well as the gearing one - both are dependencies
	// and either can close a circle.
	{
		TArray<FHFPartState> Parts = { MakePart(TEXT("X")), MakePart(TEXT("Y")), MakePart(TEXT("Z")) };
		Parts[0].Motion.DrivenByPartId = TEXT("Y");
		Parts[1].Motion.SequencedAfterPartId = TEXT("Z");
		Parts[1].Motion.SequenceThreshold = 0.5;
		Parts[2].Motion.DrivenByPartId = TEXT("X");

		TArray<FName> Cyclic;
		TestFalse(TEXT("A three-part loop is refused too"),
			FHFArticulation::ResolvePartAmounts(Parts, &Cyclic));
		TestEqual(TEXT("All three are named"), Cyclic.Num(), 3);
	}

	// And a cycle in one corner does not freeze the parts that are perfectly well defined. A
	// wardrobe with one bad relationship still has to open its other five shutters.
	{
		TArray<FHFPartState> Parts = {
			MakePart(TEXT("LoopA")), MakePart(TEXT("LoopB")), MakePart(TEXT("Good")), MakePart(TEXT("GearedToGood"))
		};
		Parts[0].Motion.DrivenByPartId = TEXT("LoopB");
		Parts[1].Motion.DrivenByPartId = TEXT("LoopA");
		Parts[3].Motion.DrivenByPartId = TEXT("Good");
		Parts[2].OpenAmount = 0.8;

		TArray<FName> Cyclic;
		TestFalse(TEXT("The cycle is still reported"), FHFArticulation::ResolvePartAmounts(Parts, &Cyclic));
		TestEqual(TEXT("Only the parts on the loop are named"), Cyclic.Num(), 2);
		TestNearlyEqual(TEXT("A sound part elsewhere in the fixture still resolves"),
			Parts[3].OpenAmount, 0.8, 1e-9);
	}

	// A part geared to one that is itself blocked by an ordering is NOT a cycle, and must resolve.
	{
		TArray<FHFPartState> Parts = { MakePart(TEXT("Runner")), MakePart(TEXT("Drawer")), MakePart(TEXT("Leaf")) };
		Parts[0].Motion.DrivenByPartId = TEXT("Drawer");
		Parts[1].Motion.SequencedAfterPartId = TEXT("Leaf");
		Parts[1].Motion.SequenceThreshold = 0.5;

		for (FHFPartState& Part : Parts)
		{
			Part.OpenAmount = 1.0;
		}

		TestTrue(TEXT("Gearing and sequencing together are not a cycle"),
			FHFArticulation::ResolvePartAmounts(Parts));
		TestNearlyEqual(TEXT("The drawer opens fully behind a fully open leaf"), Parts[1].OpenAmount, 1.0, 1e-9);
		TestNearlyEqual(TEXT("And its runner with it"), Parts[0].OpenAmount, 1.0, 1e-9);
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

	// An archway gets no frame either, and that is a decision rather than an oversight. A cased
	// opening in one of these flats is plastered masonry painted with the walls - a chowkhat is
	// there to hang a leaf on, and an archway has no leaf to hang.
	TestEqual(TEXT("An archway has no fixed geometry either"),
		FHFGenerators::GenerateOpeningFixedInfill(Arch, Wall).TriangleCount(), 0);

	// A door's LEAF is all that moves - but its frame does not, and the frame is fixed geometry.
	// This asserted the opposite while every door in the flat was a bare leaf in a bare hole.
	const FDynamicMesh3 DoorFrame = FHFGenerators::GenerateOpeningFixedInfill(MakeDoorOpening(), Wall);
	TestTrue(TEXT("A door's frame is fixed geometry"),
		TMeshQueries<FDynamicMesh3>::GetVolumeArea(DoorFrame).X > 0.0);
	TestTrue(TEXT("A door frame is watertight"), FHFMeshOps::IsClosed(DoorFrame));
	TestTrue(TEXT("Nothing in a door's fixed mesh is glass"),
		FMath::IsNearlyZero(RoleVolume(DoorFrame, EHFSurfaceRole::Glass), 0.001));

	return true;
}

/**
 * Every slider in the plugin is a PAIR, described symmetrically, with exactly one leading leaf.
 *
 * The three sliders - a sliding door, a sliding window, a sliding wardrobe run - are built by two
 * different pieces of code (FHFGenerators' BuildSlidingPair, and FHFWardrobeKit composing
 * FHFJoineryKit shutters), and the last time those two disagreed about how a slider works the
 * wardrobe built two runners and the door built one. That is the defect this exists to keep out on
 * the OTHER side: now that both leaves of every slider run, the thing that has to hold everywhere
 * is the description of the pair.
 *
 * Three properties, and each of them fails silently on its own:
 *
 *   - BOTH LEAVES RUN. Take the motion off one and the run opens from one end for ever, which is
 *     what was reported. Nothing else notices: the unit still opens.
 *   - EXACTLY ONE LEADS. Two leading leaves is the cancelling pose - both driven by one amount,
 *     tracks exchanged, nothing uncovered - and zero leading leaves is a fixture that ignores its
 *     own master control while every part reports a healthy travel.
 *   - THE PAIRING IS SYMMETRIC. A leaf whose alternate is unset, or names a part that is not there,
 *     is a leaf OpenRunFrom cannot shut. Open the run from that end and both leaves run out
 *     together, which is the cancelling pose arrived at by a different route.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSliderPairSymmetryTest,
	"HouseForge.Articulation.SliderPairsAreSymmetric", HF_TEST_FLAGS)

bool FHFSliderPairSymmetryTest::RunTest(const FString& Parameters)
{
	// Whatever produced them, a set of parts that contains sliding leaves has to describe them the
	// same way. Asked of the parts rather than of the code that made them, which is what lets one
	// assertion cover three builders.
	auto CheckPair = [this](const FString& What, const TArray<FHFMeshPart>& Parts)
	{
		TArray<const FHFMeshPart*> Leaves;
		for (const FHFMeshPart& Part : Parts)
		{
			if (Part.Motion.Type == EHFMotionType::Slide)
			{
				Leaves.Add(&Part);
			}
		}

		if (!TestEqual(*FString::Printf(TEXT("%s is two running leaves"), *What), Leaves.Num(), 2))
		{
			return;
		}

		int32 Leading = 0;
		for (const FHFMeshPart* Leaf : Leaves)
		{
			Leading += Leaf->Motion.bMasterOpens ? 1 : 0;

			TestTrue(*FString::Printf(TEXT("%s leaf '%s' has somewhere to run to"),
					*What, *Leaf->PartId.ToString()),
				FMath::Abs(Leaf->Motion.MaxTravelCm) > 1.0);
		}

		TestEqual(*FString::Printf(TEXT("%s has exactly one leaf a single control runs"), *What),
			Leading, 1);

		TestEqual(*FString::Printf(TEXT("%s: the first leaf names the second"), *What),
			Leaves[0]->Motion.AlternateToPartId, Leaves[1]->PartId);
		TestEqual(*FString::Printf(TEXT("%s: the second names the first back"), *What),
			Leaves[1]->Motion.AlternateToPartId, Leaves[0]->PartId);

		// They run OPPOSITE ways, which is what makes "which leaf you push" mean "which end opens".
		// Two leaves running the same way would both leave the same bay open and one of them would
		// leave the reveal doing it.
		TestTrue(*FString::Printf(TEXT("%s: the leaves run opposite ways"), *What),
			Leaves[0]->Motion.MaxTravelCm * Leaves[1]->Motion.MaxTravelCm < 0.0);
	};

	const FHFWall Wall = MakeHostWall();

	TArray<FHFMeshPart> DoorParts;
	FHFGenerators::BuildOpeningParts(
		MakeDoorOpening(EHFOpeningKind::SlidingDoor, EHFSwing::None), Wall, DoorParts);
	CheckPair(TEXT("A sliding door"), DoorParts);

	TArray<FHFMeshPart> WindowParts;
	FHFGenerators::BuildOpeningParts(MakeWindowOpening(EHFOpeningKind::SlidingWindow), Wall, WindowParts);
	CheckPair(TEXT("A sliding window"), WindowParts);

	// The wardrobe is built by a different piece of code entirely - FHFWardrobeKit composing
	// FHFJoineryKit shutters, rather than FHFGenerators' BuildSlidingPair - and the last time those
	// two disagreed about how a slider works, one of them built two runners and the other built one.
	// The same assertion is put to both.
	{
		FHFWardrobeParams Wardrobe;
		Wardrobe.Width = 240.0;
		Wardrobe.Depth = 60.0;
		Wardrobe.Height = 240.0;
		Wardrobe.BayCount = 4;
		Wardrobe.bHasLoft = false;
		Wardrobe.MotionKind = EHFShutterMotion::Sliding;

		CheckPair(TEXT("A sliding wardrobe run"), FHFWardrobeKit::Build(Wardrobe).Parts);
	}

	// The drawing's swing arc picks which end a single control opens from, and a slider carried that
	// field without ever reading it. Both hands are exercised because "the default happens to be
	// what the enum's zero value is" would pass the check above and mean nothing.
	for (const EHFSwing Swing : { EHFSwing::InwardLeft, EHFSwing::InwardRight })
	{
		TArray<FHFMeshPart> Handed;
		FHFGenerators::BuildOpeningParts(
			MakeDoorOpening(EHFOpeningKind::SlidingDoor, Swing), Wall, Handed);

		const FHFMeshPart* Leader = Handed.FindByPredicate(
			[](const FHFMeshPart& P) { return P.Motion.bMasterOpens; });

		if (TestNotNull(TEXT("A handed sliding door still has a leading panel"), Leader))
		{
			const FName Expected = Swing == EHFSwing::InwardRight ? TEXT("LeafFar") : TEXT("LeafNear");
			TestEqual(TEXT("The drawing's swing arc decides which end the unit opens from"),
				Leader->PartId, Expected);
		}
	}

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
