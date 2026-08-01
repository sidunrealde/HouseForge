// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshTransforms.h"
#include "Geometry/HFGenerators.h"
#include "Geometry/HFMeshOps.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFArticulation.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFTypes.h"
#include "Selections/MeshConnectedComponents.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Doors have frames, and the frames are where a frame goes.
 *
 * Nothing here existed while every door in the reference flat was a bare leaf hanging in a bare
 * masonry hole. That defect survived 201 passing tests and a full drawing-derived build, because
 * nothing anywhere asked whether a door had anything around it - the leaf was watertight, the right
 * size, in the right place, and swung correctly, and the doorway it swung in was a hole in a wall.
 *
 * So these ask the questions that would have caught it: is there fixed geometry at all, is it inside
 * the reveal rather than standing in the room, does it leave the leaf alone through the whole swing,
 * and can the material panel reach it.
 */
namespace
{
	/** A wall running along +X from the origin, so its normal is +Y and every box is axis-aligned. */
	FHFWall MakeWall(double Thickness)
	{
		FHFWall Wall;
		Wall.Id = TEXT("W_Frame");
		Wall.Start = FVector2D(0.0, 0.0);
		Wall.End = FVector2D(400.0, 0.0);
		Wall.Thickness = Thickness;
		Wall.Height = 300.0;
		return Wall;
	}

	FHFOpening MakeDoor(double Width, EHFSwing Swing, EHFOpeningKind Kind = EHFOpeningKind::Door)
	{
		FHFOpening Opening;
		Opening.Id = TEXT("D_Frame");
		Opening.WallId = TEXT("W_Frame");
		Opening.Kind = Kind;
		Opening.Width = Width;
		Opening.Height = 210.0;
		Opening.SillHeight = 0.0;
		Opening.OffsetAlongWall = 200.0;
		Opening.Swing = Swing;
		return Opening;
	}

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

	FAxisAlignedBox3d RoleBounds(const FDynamicMesh3& Mesh, EHFSurfaceRole Role)
	{
		FAxisAlignedBox3d Bounds = FAxisAlignedBox3d::Empty();
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
				Bounds.Contain(Mesh.GetVertex(Tri[i]));
			}
		}

		return Bounds;
	}

	/**
	 * The boxes a mesh is built from, as their own bounds.
	 *
	 * Every member a generator emits is a closed box appended into the mesh, and appending leaves
	 * them as separate connected components - so the components ARE the boxes, and on a wall running
	 * along +X they are axis aligned in world.
	 */
	void ComponentBoxes(const FDynamicMesh3& Mesh, TArray<FAxisAlignedBox3d>& OutBoxes)
	{
		OutBoxes.Reset();

		FMeshConnectedComponents Components(&Mesh);
		Components.FindConnectedTriangles();

		for (const FMeshConnectedComponents::FComponent& Component : Components)
		{
			FAxisAlignedBox3d Box = FAxisAlignedBox3d::Empty();
			for (const int32 Tid : Component.Indices)
			{
				FVector3d A, B, C;
				Mesh.GetTriVertices(Tid, A, B, C);
				Box.Contain(A);
				Box.Contain(B);
				Box.Contain(C);
			}
			OutBoxes.Add(Box);
		}
	}

	/** How far a point lies inside a box, in centimetres. Zero if it is outside or on the surface. */
	double PenetrationInto(const FAxisAlignedBox3d& Box, const FVector3d& Point)
	{
		const double X = FMath::Min(Point.X - Box.Min.X, Box.Max.X - Point.X);
		const double Y = FMath::Min(Point.Y - Box.Min.Y, Box.Max.Y - Point.Y);
		const double Z = FMath::Min(Point.Z - Box.Min.Z, Box.Max.Z - Point.Z);

		return FMath::Max(FMath::Min3(X, Y, Z), 0.0);
	}

	/** How far a point lies outside a box, in centimetres. Zero if it is inside or on the surface. */
	double ClearanceFrom(const FAxisAlignedBox3d& Box, const FVector3d& Point)
	{
		const FVector3d Outside(
			FMath::Max3(Box.Min.X - Point.X, 0.0, Point.X - Box.Max.X),
			FMath::Max3(Box.Min.Y - Point.Y, 0.0, Point.Y - Box.Max.Y),
			FMath::Max3(Box.Min.Z - Point.Z, 0.0, Point.Z - Box.Max.Z));

		return Outside.Length();
	}

	/**
	 * Points covering a part's solid at a given open amount.
	 *
	 * The leaf is one box, so a lattice over its local extent covers it - and a lattice rather than
	 * eight corners because a rotated box's corners can clear an obstruction its middle does not.
	 */
	void SampleLeaf(const FHFMeshPart& Part, double OpenAmount, TArray<FVector3d>& OutPoints)
	{
		OutPoints.Reset();

		FHFPartState State;
		State.PivotTransform = Part.PivotTransform;
		State.Motion = Part.Motion;
		State.OpenAmount = OpenAmount;

		const FTransform Pose = State.CurrentPose();
		const FAxisAlignedBox3d Local = Part.Mesh.GetBounds();

		constexpr int32 Along = 24;
		constexpr int32 Across = 4;
		constexpr int32 Up = 24;

		for (int32 i = 0; i <= Along; ++i)
		{
			const double X = FMath::Lerp(Local.Min.X, Local.Max.X, double(i) / Along);
			for (int32 j = 0; j <= Across; ++j)
			{
				const double Y = FMath::Lerp(Local.Min.Y, Local.Max.Y, double(j) / Across);
				for (int32 k = 0; k <= Up; ++k)
				{
					const double Z = FMath::Lerp(Local.Min.Z, Local.Max.Z, double(k) / Up);
					OutPoints.Add(FVector3d(Pose.TransformPosition(FVector(X, Y, Z))));
				}
			}
		}
	}
}

/**
 * Every kind of door, in every wall it is built into, comes with a frame in the reveal.
 *
 * The reference flat has three door widths, four swings and two wall thicknesses, and the defect was
 * present in all of them at once: not one door had a frame. So the assertion is on the combination
 * rather than on one representative door - a frame that appears only for an inward swing, or only in
 * a thin wall, is the same defect wearing a smaller hat.
 *
 * "In the reveal" is what makes it a frame rather than an object: it may bury itself in the masonry
 * around the opening, and it may stand a few millimetres proud of the wall face, but it may not
 * reach out into the room and it must never come out through the far side of the wall it is set in.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFDoorFrameInTheRevealTest,
	"HouseForge.Openings.EveryDoorKindGetsAFrameInItsReveal", HF_TEST_FLAGS)

bool FHFDoorFrameInTheRevealTest::RunTest(const FString& Parameters)
{
	const FHFOpeningBuildParams Params;
	const FHFDoorParams& Door = Params.Door;

	const EHFSwing Swings[] = {
		EHFSwing::InwardLeft, EHFSwing::InwardRight,
		EHFSwing::OutwardLeft, EHFSwing::OutwardRight, EHFSwing::None
	};

	int32 Checked = 0;

	// 750, 900 and 1050 are the flat's three doorways; 115 and 230 are its two walls. The same
	// section has to sit correctly in both, which is the whole reason the frame is not stretched to
	// the wall thickness.
	for (const double Thickness : { 11.5, 23.0 })
	{
		const FHFWall Wall = MakeWall(Thickness);

		for (const double Width : { 75.0, 90.0, 105.0 })
		{
			for (const EHFSwing Swing : Swings)
			{
				const FHFOpening Opening = MakeDoor(Width, Swing);
				const FDynamicMesh3 Frame = FHFGenerators::GenerateOpeningFixedInfill(Opening, Wall);

				const FString Which = FString::Printf(TEXT("%.0f door in a %.1f wall, swing %d"),
					Width, Thickness, int32(Swing));

				if (!TestTrue(*FString::Printf(TEXT("The %s has a frame at all"), *Which),
						TMeshQueries<FDynamicMesh3>::GetVolumeArea(Frame).X > 0.0))
				{
					continue;
				}

				TestTrue(*FString::Printf(TEXT("The frame of the %s is watertight"), *Which),
					FHFMeshOps::IsClosed(Frame));

				// Head and two jambs, each an L-section in two pieces: three members, six boxes. A
				// frame with a bottom member would be a window's, not a door's - the jambs of a
				// chowkhat run to the finished floor and the leaf is undercut over it.
				TArray<FAxisAlignedBox3d> Boxes;
				ComponentBoxes(Frame, Boxes);
				TestEqual(*FString::Printf(TEXT("The frame of the %s is head and two jambs"), *Which),
					Boxes.Num(), 6);

				const FAxisAlignedBox3d Bounds = Frame.GetBounds();
				const double HalfWidth = Width * 0.5;
				const double Centre = Opening.OffsetAlongWall;

				// Along the wall: it fills the opening and buries itself in the masonry beside it,
				// but no further than the embedment it declares.
				TestTrue(*FString::Printf(TEXT("The %s's frame reaches both jambs"), *Which),
					Bounds.Min.X <= Centre - HalfWidth && Bounds.Max.X >= Centre + HalfWidth);
				TestTrue(*FString::Printf(TEXT("The %s's frame stays in its own opening"), *Which),
					Bounds.Min.X >= Centre - HalfWidth - Door.FrameEmbed - 0.01
						&& Bounds.Max.X <= Centre + HalfWidth + Door.FrameEmbed + 0.01);

				// Up: from under the floor finish to into the lintel, and no higher.
				TestTrue(*FString::Printf(TEXT("The %s's frame stands on the floor"), *Which),
					Bounds.Min.Z <= 0.0 && Bounds.Min.Z >= -Door.FrameEmbed - 0.01);
				TestTrue(*FString::Printf(TEXT("The %s's frame reaches its head"), *Which),
					Bounds.Max.Z >= Opening.Height
						&& Bounds.Max.Z <= Opening.Height + Door.FrameEmbed + 0.01);

				// Across the wall, which is where a frame is either right or embarrassing. It is set
				// at the face the leaf swings TOWARDS, standing a few millimetres proud of it, and
				// its back is inside the masonry. A section stretched to fill a 230 reveal, or one
				// poking out the back of a 115 one, both fail here - and so does one built on the
				// wrong face, which would leave the leaf shutting against nothing.
				const double SwingSide =
					(Swing == EHFSwing::OutwardLeft || Swing == EHFSwing::OutwardRight) ? -1.0 : 1.0;

				const double ProudFace = SwingSide > 0.0 ? Bounds.Max.Y : Bounds.Min.Y;
				const double BuriedFace = SwingSide > 0.0 ? Bounds.Min.Y : Bounds.Max.Y;

				TestNearlyEqual(
					*FString::Printf(TEXT("The %s's frame stands proud of the face it is fixed to"), *Which),
					ProudFace, SwingSide * (Thickness * 0.5 + Door.FrameProud), 0.01);
				TestTrue(
					*FString::Printf(TEXT("The %s's frame does not come out the far face"), *Which),
					BuriedFace * SwingSide >= -Thickness * 0.5 - 0.01);
				TestTrue(*FString::Printf(TEXT("The %s's frame is no deeper than its section"), *Which),
					Bounds.Height() <= Door.FrameDepth + 0.01);

				// One section, both walls. The 230 wall gets the same 100 mm frame and keeps the
				// balance of its reveal as plastered masonry, which is what a real one does and is
				// why this is asserted rather than left to the eye.
				TestNearlyEqual(*FString::Printf(TEXT("The %s's frame is its own section deep"), *Which),
					Bounds.Height(), FMath::Min(Door.FrameDepth, Thickness + Door.FrameProud), 0.01);

				// And the material panel can reach it. A frame tagged with a role nothing maps to is
				// geometry nobody can ever finish, and it looks perfect in every screenshot.
				for (const int32 Tid : Frame.TriangleIndicesItr())
				{
					if (FHFMeshOps::RoleForGroup(Frame.GetTriangleGroup(Tid)) != EHFSurfaceRole::DoorLeaf)
					{
						AddError(FString::Printf(
							TEXT("The frame of the %s emitted a triangle carrying neither the door's role nor any other; the material panel cannot reach it."),
							*Which));
						break;
					}
				}

				++Checked;
			}
		}
	}

	TestEqual(TEXT("Every door width, swing and wall was checked"), Checked, 30);

	return true;
}

/**
 * A frame the leaf touches is worse than no frame at all.
 *
 * Two solids sharing a surface flicker through every frame of a walkthrough, and a leaf that passes
 * bodily through its own jamb on the way open is the kind of thing a still screenshot of a closed
 * door will never show. So the leaf is swept through its whole travel and every point of it tested
 * against every box of the frame - not just at nought and one, because the worst case for a hinge is
 * very often halfway.
 *
 * The check is the leaf against the FRAME. Whether the leaf clears the masonry is the house sweep's
 * question and it asks it against the whole flat.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFDoorFrameClearsItsLeafTest,
	"HouseForge.Openings.AFrameNeverTouchesTheLeafItCarries", HF_TEST_FLAGS)

bool FHFDoorFrameClearsItsLeafTest::RunTest(const FString& Parameters)
{
	const EHFSwing Swings[] = {
		EHFSwing::InwardLeft, EHFSwing::InwardRight,
		EHFSwing::OutwardLeft, EHFSwing::OutwardRight
	};

	TArray<FAxisAlignedBox3d> Boxes;
	TArray<FVector3d> Points;

	double WorstPenetration = 0.0;
	double TightestClearance = TNumericLimits<double>::Max();

	for (const double Thickness : { 11.5, 23.0 })
	{
		const FHFWall Wall = MakeWall(Thickness);

		for (const double Width : { 75.0, 90.0, 105.0 })
		{
			for (const EHFSwing Swing : Swings)
			{
				const FHFOpening Opening = MakeDoor(Width, Swing);

				ComponentBoxes(FHFGenerators::GenerateOpeningFixedInfill(Opening, Wall), Boxes);

				TArray<FHFMeshPart> Parts;
				FHFGenerators::BuildOpeningParts(Opening, Wall, Parts);

				if (!TestEqual(TEXT("The door produced a single leaf"), Parts.Num(), 1))
				{
					return false;
				}

				constexpr int32 Steps = 20;
				for (int32 Step = 0; Step <= Steps; ++Step)
				{
					const double OpenAmount = double(Step) / Steps;
					SampleLeaf(Parts[0], OpenAmount, Points);

					for (const FVector3d& Point : Points)
					{
						for (const FAxisAlignedBox3d& Box : Boxes)
						{
							const double Depth = PenetrationInto(Box, Point);
							if (Depth > WorstPenetration)
							{
								WorstPenetration = Depth;

								AddError(FString::Printf(
									TEXT("The leaf of a %.0f door in a %.1f wall is %.3f cm inside its own frame at %.2f open. A leaf may not touch the frame it hangs in, let alone pass through it."),
									Width, Thickness, Depth, OpenAmount));
							}

							TightestClearance = FMath::Min(TightestClearance, ClearanceFrom(Box, Point));
						}
					}
				}
			}
		}
	}

	TestTrue(TEXT("The leaf is never inside its frame at any open amount"), WorstPenetration <= 0.0);

	// And it does not merely avoid the frame, it clears it: the running clearance the section
	// declares is real, all the way round and all the way through the swing. Zero here would mean
	// two surfaces in contact, which is exactly the flicker this pass exists to remove.
	TestTrue(*FString::Printf(TEXT("The leaf keeps a running clearance from its frame (%.3f cm)"),
			TightestClearance),
		TightestClearance > 0.05);

	return true;
}

/**
 * A balcony door is glazed, and its glass is a solid.
 *
 * A pane modelled as a plane has area, bounds and triangles, and no volume whatsoever. Every check
 * that is not the volume integral passes on one - which is why the rule is stated as a volume here,
 * and why the two sliding doors in the reference flat could be opaque boards for as long as they
 * were without anything noticing.
 *
 * Asked of the reference flat's own openings rather than of a fixture built for the test, because
 * the defect was in the flat.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSlidingDoorIsGlazedTest,
	"HouseForge.Openings.ASlidingDoorIsGlazedAndItsGlassIsASolid", HF_TEST_FLAGS)

bool FHFSlidingDoorIsGlazedTest::RunTest(const FString& Parameters)
{
	FHFHouseSpec Spec = FHFSampleHouse::Make2BHK();
	FHFUnits::ConvertToCentimeters(Spec);

	const FHFOpeningBuildParams Params;
	int32 Checked = 0;

	for (const FHFOpening& Opening : Spec.Openings)
	{
		if (Opening.Kind != EHFOpeningKind::SlidingDoor)
		{
			continue;
		}

		const FHFWall* Wall = Spec.FindWall(Opening.WallId);
		if (!TestNotNull(TEXT("The sliding door has a wall to sit in"), Wall))
		{
			continue;
		}

		const FString Which = Opening.Id.ToString();

		TArray<FHFMeshPart> Parts;
		FHFGenerators::BuildOpeningParts(Opening, *Wall, Parts);

		if (!TestEqual(*FString::Printf(TEXT("'%s' is two panels"), *Which), Parts.Num(), 2))
		{
			continue;
		}

		const FHFMeshPart* Running = Parts.FindByPredicate(
			[](const FHFMeshPart& P) { return P.Motion.Type == EHFMotionType::Slide; });
		const FHFMeshPart* Standing = Parts.FindByPredicate(
			[](const FHFMeshPart& P) { return !P.Motion.Moves(); });

		if (!TestNotNull(*FString::Printf(TEXT("'%s' has a running panel"), *Which), Running)
			|| !TestNotNull(*FString::Printf(TEXT("'%s' has a fixed panel"), *Which), Standing))
		{
			continue;
		}

		for (const FHFMeshPart* Panel : { Standing, Running })
		{
			const double Glass = RoleVolume(Panel->Mesh, EHFSurfaceRole::Glass);

			// The whole defect, in one line: a solid pane, not a plane, and not an opaque board.
			TestTrue(*FString::Printf(TEXT("Panel '%s' of '%s' is glazed with a solid"),
					*Panel->PartId.ToString(), *Which),
				Glass > 0.0);
			TestTrue(*FString::Printf(TEXT("Panel '%s' of '%s' is not an opaque leaf"),
					*Panel->PartId.ToString(), *Which),
				FMath::IsNearlyZero(RoleVolume(Panel->Mesh, EHFSurfaceRole::DoorLeaf), 0.001));

			// A 1690 clear opening in two 860 panels, glazed to the groove: most of a balcony door
			// is glass, so a pane that is a sliver of its sash would pass "has volume" and still be
			// a boarded-up door with a porthole in it.
			const FAxisAlignedBox3d Pane = RoleBounds(Panel->Mesh, EHFSurfaceRole::Glass);
			const FAxisAlignedBox3d Sash = Panel->Mesh.GetBounds();

			TestTrue(*FString::Printf(TEXT("Most of panel '%s' of '%s' is glass"),
					*Panel->PartId.ToString(), *Which),
				Pane.Width() * Pane.Depth() > Sash.Width() * Sash.Depth() * 0.6);

			// 8 mm toughened, which is a door's pane and not the window path's 5.
			TestNearlyEqual(*FString::Printf(TEXT("Panel '%s' of '%s' is glazed at a door's thickness"),
					*Panel->PartId.ToString(), *Which),
				Pane.Height(), Params.SlidingDoor.GlassThickness, 0.001);

			TestTrue(*FString::Printf(TEXT("Panel '%s' of '%s' is watertight"),
					*Panel->PartId.ToString(), *Which),
				FHFMeshOps::IsClosed(Panel->Mesh));
		}

		// It still travels: half the clear opening less half the interlock, far edge to far edge, so
		// the running panel comes to rest exactly over its partner and inside the reveal.
		const FHFSlidingDoorParams Section =
			Params.SlidingDoor.Sanitised(Opening.Width, Opening.Height);
		const double Expected =
			Section.ClearWidth(Opening.Width) * 0.5 - Section.InterlockOverlap * 0.5;

		TestNearlyEqual(*FString::Printf(TEXT("The running panel of '%s' travels its declared distance"),
				*Which),
			Running->Motion.MaxTravelCm, Expected, 0.01);

		FHFPartState State;
		State.PivotTransform = Running->PivotTransform;
		State.Motion = Running->Motion;

		const FVector ClosedAt = State.PoseAt(0.0).TransformPosition(FVector::ZeroVector);
		const FVector OpenAt = State.PoseAt(1.0).TransformPosition(FVector::ZeroVector);

		TestNearlyEqual(*FString::Printf(TEXT("And it moves that far in the world for '%s'"), *Which),
			(OpenAt - ClosedAt).Size(), Expected, 0.01);

		// The outer frame: four sides including the threshold, which is the member a hinged frame
		// does not have, with the tracks the panels run on standing on it.
		const FDynamicMesh3 Outer = FHFGenerators::GenerateOpeningFixedInfill(Opening, *Wall);

		TestTrue(*FString::Printf(TEXT("'%s' has an outer frame the panels run in"), *Which),
			RoleVolume(Outer, EHFSurfaceRole::WindowFrame) > 0.0);
		TestTrue(*FString::Printf(TEXT("'%s' has tracks"), *Which),
			RoleVolume(Outer, EHFSurfaceRole::MetalHardware) > 0.0);
		TestTrue(*FString::Printf(TEXT("'%s' has no fixed pane behind its glazed panels"), *Which),
			FMath::IsNearlyZero(RoleVolume(Outer, EHFSurfaceRole::Glass), 0.001));

		// Every triangle of the frame is reachable by the material panel, and reachable as the
		// aluminium it is rather than as a door leaf.
		for (const int32 Tid : Outer.TriangleIndicesItr())
		{
			const EHFSurfaceRole Role = FHFMeshOps::RoleForGroup(Outer.GetTriangleGroup(Tid));
			if (Role != EHFSurfaceRole::WindowFrame && Role != EHFSurfaceRole::MetalHardware)
			{
				AddError(FString::Printf(
					TEXT("The outer frame of '%s' emitted a triangle with role %d, which is neither window frame nor metal hardware."),
					*Which, int32(Role)));
				break;
			}
		}

		const FAxisAlignedBox3d OuterBounds = Outer.GetBounds();
		TestNearlyEqual(*FString::Printf(TEXT("The threshold of '%s' stands on the floor"), *Which),
			OuterBounds.Min.Z, Wall->BaseZ + Opening.SillHeight, 0.01);
		TestNearlyEqual(*FString::Printf(TEXT("The frame of '%s' reaches its head"), *Which),
			OuterBounds.Max.Z, Wall->BaseZ + Opening.SillHeight + Opening.Height, 0.01);

		++Checked;
	}

	TestTrue(TEXT("Both balcony doors in the reference flat were checked"), Checked >= 2);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
