// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshTransforms.h"
#include "Geometry/HFGenerators.h"
#include "Geometry/HFMeshOps.h"
#include "Misc/AutomationTest.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two figures that are each inside their own clamp can still describe a unit that cannot be built.
 *
 * ClampMin and ClampMax bound one field at a time, which is all property metadata can do, and they
 * are also skipped entirely by a value loaded from an ini. Every rule that keeps an opening well
 * formed is a rule between two fields, so the reconciliation lives in the params struct and the
 * generators apply it before measuring anything.
 *
 * These assert the consequence in the geometry rather than the clamped number - a pane that stays
 * inside its sash, two sashes that do not occupy the same wall, a door that still has a leaf. A
 * test on the clamped double would pass just as well against a sanitiser that clamped the wrong
 * field.
 */
namespace
{
	FHFWall MakeWall()
	{
		FHFWall Wall;
		Wall.Id = TEXT("W_Sanity");
		Wall.Start = FVector2D(0.0, 0.0);
		Wall.End = FVector2D(400.0, 0.0);
		Wall.Thickness = 20.0;
		Wall.Height = 300.0;
		return Wall;
	}

	FHFOpening MakeSlidingWindow()
	{
		FHFOpening Opening;
		Opening.Id = TEXT("Win_Sanity");
		Opening.WallId = TEXT("W_Sanity");
		Opening.Kind = EHFOpeningKind::SlidingWindow;
		Opening.Width = 150.0;
		Opening.Height = 120.0;
		Opening.SillHeight = 90.0;
		Opening.OffsetAlongWall = 200.0;
		return Opening;
	}

	/** The reference flat's balcony door: 1800 x 2100, the size both of its sliding units are. */
	FHFOpening MakeSlidingDoor()
	{
		FHFOpening Opening;
		Opening.Id = TEXT("D_Sanity_Slide");
		Opening.WallId = TEXT("W_Sanity");
		Opening.Kind = EHFOpeningKind::SlidingDoor;
		Opening.Width = 180.0;
		Opening.Height = 210.0;
		Opening.OffsetAlongWall = 200.0;
		return Opening;
	}

	FHFOpening MakeDoor()
	{
		FHFOpening Opening;
		Opening.Id = TEXT("D_Sanity");
		Opening.WallId = TEXT("W_Sanity");
		Opening.Kind = EHFOpeningKind::Door;
		Opening.Width = 90.0;
		Opening.Height = 210.0;
		Opening.OffsetAlongWall = 200.0;
		Opening.Swing = EHFSwing::InwardLeft;
		return Opening;
	}

	/** Bounds of just the triangles carrying one surface role. */
	FAxisAlignedBox3d BoundsOfRole(const FDynamicMesh3& Mesh, EHFSurfaceRole Role)
	{
		FAxisAlignedBox3d Box = FAxisAlignedBox3d::Empty();
		const int32 Group = FHFMeshOps::GroupForRole(Role);

		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (Mesh.GetTriangleGroup(Tid) != Group)
			{
				continue;
			}

			const FIndex3i Tri = Mesh.GetTriangle(Tid);
			for (int32 i = 0; i < 3; ++i)
			{
				Box.Contain(Mesh.GetVertex(Tri[i]));
			}
		}

		return Box;
	}

	/** A part's mesh where the house puts it. */
	FDynamicMesh3 Posed(const FHFMeshPart& Part)
	{
		FDynamicMesh3 Mesh = Part.Mesh;
		MeshTransforms::ApplyTransform(Mesh, FTransformSRT3d(Part.PivotTransform), true);
		return Mesh;
	}
}

/**
 * A glazing groove cut deeper than the section it is cut into must not put the pane outside the sash.
 *
 * The rebate is subtracted from the sash's face width to get the pane's inset, so a rebate past the
 * face width makes the inset negative and the pane comes out LARGER than the sash holding it -
 * standing out through the outer frame and into the reveal as a sheet of glass floating in masonry.
 * Nothing about the mesh reports it: every box is closed, the volume is plausible, and it is a
 * rendering bug found only by looking at the window.
 *
 * Both figures here are individually inside their own clamps.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFOpeningDeepRebateKeepsPaneInSashTest,
	"HouseForge.Openings.ADeepRebateKeepsThePaneInsideItsSash", HF_TEST_FLAGS)

bool FHFOpeningDeepRebateKeepsPaneInSashTest::RunTest(const FString& Parameters)
{
	const FHFWall Wall = MakeWall();
	const FHFOpening Window = MakeSlidingWindow();

	FHFOpeningBuildParams Params;
	Params.SlidingWindow.SashFaceWidth = 4.0;

	// A 5 cm groove in a 4 cm section. Both accepted by their own clamps; together they are a sash
	// that cannot hold its own glass.
	Params.SlidingWindow.GlassRebate = 5.0;

	TArray<FHFMeshPart> Parts;
	FHFGenerators::BuildOpeningParts(Window, Wall, Parts, Params);

	if (!TestEqual(TEXT("The window still builds two sashes"), Parts.Num(), 2))
	{
		return false;
	}

	for (const FHFMeshPart& Part : Parts)
	{
		const FAxisAlignedBox3d Glass = BoundsOfRole(Part.Mesh, EHFSurfaceRole::Glass);
		const FAxisAlignedBox3d Frame = BoundsOfRole(Part.Mesh, EHFSurfaceRole::WindowFrame);

		if (!TestTrue(TEXT("The sash has a frame"), Frame.Volume() > 0.0)
			|| !TestTrue(TEXT("The sash has a pane"), Glass.Volume() > 0.0))
		{
			return false;
		}

		// The pane is held by the frame, so it cannot be wider or taller than it. Along the wall
		// (X) and up it (Z); the pane is deliberately thinner than the section on Y.
		TestTrue(TEXT("The pane does not run past the sash's near stile"), Glass.Min.X >= Frame.Min.X);
		TestTrue(TEXT("The pane does not run past the sash's far stile"), Glass.Max.X <= Frame.Max.X);
		TestTrue(TEXT("The pane does not run below the sash's bottom rail"), Glass.Min.Z >= Frame.Min.Z);
		TestTrue(TEXT("The pane does not run above the sash's top rail"), Glass.Max.Z <= Frame.Max.Z);

		// And a shoulder is left holding it, rather than the pane reaching the outer edge and
		// z-fighting the stile it is supposed to be housed in.
		TestTrue(TEXT("A shoulder of section remains outside the pane"),
			Glass.Min.X - Frame.Min.X > UE_KINDA_SMALL_NUMBER);
	}

	return true;
}

/**
 * Two sashes on two tracks have to be on two DIFFERENT tracks.
 *
 * The sashes sit at plus and minus half the track pitch, each half a section deep, so a pitch no
 * bigger than the sash depth puts them in the same band of the wall. The running sash then passes
 * bodily through the fixed one on its way open - and the closed window looks perfect, because at
 * rest the two sit side by side along the wall and only overlap once one of them moves.
 *
 * A 40 mm sash on a 10 mm pitch: both figures accepted on their own.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFOpeningSashesRunOnSeparateTracksTest,
	"HouseForge.Openings.TwoSashesNeverShareATrack", HF_TEST_FLAGS)

bool FHFOpeningSashesRunOnSeparateTracksTest::RunTest(const FString& Parameters)
{
	const FHFWall Wall = MakeWall();
	const FHFOpening Window = MakeSlidingWindow();

	FHFOpeningBuildParams Params;
	Params.SlidingWindow.SashDepth = 4.0;
	Params.SlidingWindow.TrackPitch = 1.0;

	TArray<FHFMeshPart> Parts;
	FHFGenerators::BuildOpeningParts(Window, Wall, Parts, Params);

	if (!TestEqual(TEXT("The window builds two sashes"), Parts.Num(), 2))
	{
		return false;
	}

	// The wall runs along X, so the tracks are separated along Y.
	const FAxisAlignedBox3d A = Posed(Parts[0]).GetBounds();
	const FAxisAlignedBox3d B = Posed(Parts[1]).GetBounds();

	const double Clearance = FMath::Max(A.Min.Y, B.Min.Y) - FMath::Min(A.Max.Y, B.Max.Y);

	TestTrue(TEXT("The two sashes occupy separate bands of the wall"), Clearance > 0.0);

	// And the running sash still has somewhere to run, or separating them achieved nothing.
	const FHFMeshPart* Runner = Parts.FindByPredicate(
		[](const FHFMeshPart& P) { return P.Motion.Type == EHFMotionType::Slide; });

	if (!TestNotNull(TEXT("One sash runs"), Runner))
	{
		return false;
	}

	TestTrue(TEXT("The running sash has travel"), Runner->Motion.MaxTravelCm > 0.0);

	// The sashes must also still fit inside the frame that houses them, which is the other half of
	// the same section closing.
	const FHFOpeningBuildParams Fixed = Params.Sanitised(Window.Width, Window.Height);
	TestTrue(TEXT("The outer frame is deep enough to contain both tracks"),
		Fixed.SlidingWindow.FrameDepth >= Fixed.SlidingWindow.TrackPitch + Fixed.SlidingWindow.SashDepth);

	return true;
}

/**
 * A leaf gap taken far enough does not thin a door leaf, it deletes it.
 *
 * The gap is subtracted from every side, and AppendBox declines a box with a non-positive extent -
 * so past half the opening the leaf is silently not there at all. A door with no leaf is a doorway,
 * and nothing in the level says which one was asked for.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFOpeningLeafSurvivesAnAbsurdGapTest,
	"HouseForge.Openings.ADoorKeepsItsLeafWhateverTheGap", HF_TEST_FLAGS)

bool FHFOpeningLeafSurvivesAnAbsurdGapTest::RunTest(const FString& Parameters)
{
	const FHFOpening Door = MakeDoor();

	FHFDoorParams Params;

	// 60 cm of gap on a 90 cm door. The likeliest way to arrive here is the oldest mistake in this
	// project: reading a 5 mm figure off a drawing and typing it where centimetres were wanted.
	Params.LeafFrameGap = 60.0;

	const FDynamicMesh3 Leaf = FHFGenerators::GenerateDoorLeaf(Door, 1.0, Params);
	const FAxisAlignedBox3d Bounds = Leaf.GetBounds();

	TestTrue(TEXT("The door still has a leaf"), Bounds.Volume() > 0.0);

	// And it is still a leaf inside the opening rather than one filling it. The leaf is generated in
	// its own space - X along the leaf, Y its thickness, Z its height - so the height is Depth().
	TestTrue(TEXT("The leaf is narrower than its opening"), Bounds.Width() < Door.Width);
	TestTrue(TEXT("The leaf is shorter than its opening"), Bounds.Depth() < Door.Height);

	return true;
}

/**
 * Sanitising must be a no-op on the figures the plugin ships.
 *
 * The whole reconciliation is worthless if it quietly moves the default section, because then every
 * project that never opened the settings page builds something other than what it built before.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFOpeningSanitiseLeavesShippedFiguresAloneTest,
	"HouseForge.Openings.SanitisingDoesNotDisturbTheShippedSection", HF_TEST_FLAGS)

bool FHFOpeningSanitiseLeavesShippedFiguresAloneTest::RunTest(const FString& Parameters)
{
	const FHFOpening Window = MakeSlidingWindow();

	const FHFOpeningBuildParams Shipped;
	const FHFOpeningBuildParams After = Shipped.Sanitised(Window.Width, Window.Height);

	TestEqual(TEXT("Sash depth"), After.SlidingWindow.SashDepth, Shipped.SlidingWindow.SashDepth);
	TestEqual(TEXT("Track pitch"), After.SlidingWindow.TrackPitch, Shipped.SlidingWindow.TrackPitch);
	TestEqual(TEXT("Frame depth"), After.SlidingWindow.FrameDepth, Shipped.SlidingWindow.FrameDepth);
	TestEqual(TEXT("Frame face"), After.SlidingWindow.FrameFace, Shipped.SlidingWindow.FrameFace);
	TestEqual(TEXT("Glass rebate"), After.SlidingWindow.GlassRebate, Shipped.SlidingWindow.GlassRebate);
	TestEqual(TEXT("Interlock overlap"),
		After.SlidingWindow.InterlockOverlap, Shipped.SlidingWindow.InterlockOverlap);

	TestEqual(TEXT("Ventilator frame face"), After.Ventilator.FrameFace, Shipped.Ventilator.FrameFace);
	TestEqual(TEXT("Ventilator glass rebate"), After.Ventilator.GlassRebate, Shipped.Ventilator.GlassRebate);
	TestEqual(TEXT("Ventilator open angle"),
		After.Ventilator.OpenAngleDegrees, Shipped.Ventilator.OpenAngleDegrees);

	TestEqual(TEXT("Door leaf thickness"), After.Door.LeafThickness, Shipped.Door.LeafThickness);
	TestEqual(TEXT("Door leaf gap"), After.Door.LeafFrameGap, Shipped.Door.LeafFrameGap);
	TestEqual(TEXT("Door frame depth"), After.Door.FrameDepth, Shipped.Door.FrameDepth);
	TestEqual(TEXT("Door frame face"), After.Door.FrameFace, Shipped.Door.FrameFace);
	TestEqual(TEXT("Door rebate stop"), After.Door.RebateStop, Shipped.Door.RebateStop);
	TestEqual(TEXT("Door leaf undercut"), After.Door.LeafUndercut, Shipped.Door.LeafUndercut);

	// The sliding door is sanitised against the opening it is IN, so it is asked about a balcony
	// door rather than about the window above. A 150 x 120 hole would clamp a 2.1 m door's section
	// on its way past and prove nothing about the section that ships.
	const FHFOpening BalconyDoor = MakeSlidingDoor();
	const FHFOpeningBuildParams AfterDoor = Shipped.Sanitised(BalconyDoor.Width, BalconyDoor.Height);

	TestEqual(TEXT("Sliding door frame depth"),
		AfterDoor.SlidingDoor.FrameDepth, Shipped.SlidingDoor.FrameDepth);
	TestEqual(TEXT("Sliding door frame face"),
		AfterDoor.SlidingDoor.FrameFace, Shipped.SlidingDoor.FrameFace);
	TestEqual(TEXT("Sliding door sash depth"),
		AfterDoor.SlidingDoor.SashDepth, Shipped.SlidingDoor.SashDepth);
	TestEqual(TEXT("Sliding door track pitch"),
		AfterDoor.SlidingDoor.TrackPitch, Shipped.SlidingDoor.TrackPitch);
	TestEqual(TEXT("Sliding door bottom rail"),
		AfterDoor.SlidingDoor.BottomRailWidth, Shipped.SlidingDoor.BottomRailWidth);
	TestEqual(TEXT("Sliding door interlock"),
		AfterDoor.SlidingDoor.InterlockOverlap, Shipped.SlidingDoor.InterlockOverlap);
	TestEqual(TEXT("Sliding door glass rebate"),
		AfterDoor.SlidingDoor.GlassRebate, Shipped.SlidingDoor.GlassRebate);
	TestEqual(TEXT("Sliding door threshold"),
		AfterDoor.SlidingDoor.ThresholdHeight, Shipped.SlidingDoor.ThresholdHeight);

	TestEqual(TEXT("Fixed window frame face"), After.FixedWindow.FrameFace, Shipped.FixedWindow.FrameFace);

	// The sliding door's section closes the same way the window's does. Two 40 mm sashes on a 46 mm
	// pitch pass one another with 6 mm to spare and occupy 86 mm of wall between them; the published
	// 92 mm frame is that plus its own web, so the sashes are inside the frame that houses them
	// rather than standing proud of it.
	TestEqual(TEXT("The shipped door pitch clears the shipped door sash"),
		Shipped.SlidingDoor.TrackPitch - Shipped.SlidingDoor.SashDepth, 0.6, 1e-9);
	TestTrue(TEXT("The shipped door frame contains both its sashes"),
		Shipped.SlidingDoor.FrameDepth >= Shipped.SlidingDoor.TrackPitch + Shipped.SlidingDoor.SashDepth);

	// The 3 mm running clearance the shipped section already has is exactly the floor enforced, so
	// the two must agree to the last decimal or the default has been moved by a rounding.
	TestEqual(TEXT("The shipped pitch clears the shipped sash by the enforced minimum"),
		Shipped.SlidingWindow.TrackPitch - Shipped.SlidingWindow.SashDepth, 0.3, 1e-9);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
