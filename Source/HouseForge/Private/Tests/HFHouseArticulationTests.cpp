// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Geometry/HFGenerators.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFArticulation.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFSpecValidator.h"
#include "Model/HFTypes.h"
#include "Selections/MeshConnectedComponents.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Articulation checked against the house it has to move inside, rather than against one test wall.
 *
 * A leaf that swings correctly in isolation can still sweep through the partition next to it, and a
 * panel that slides its declared distance can still slide into masonry. Neither shows up in a
 * single-opening test, and neither shows up in a wireframe - only in a walkthrough, by which point
 * it is someone's screenshot. So the reference 2BHK is swept part by part, open amount by open
 * amount, against every wall, floor and ceiling in it.
 */
namespace HouseForgeSweep
{
	/** Penetration is only reported past this, so a leaf resting against a reveal is not a failure. */
	constexpr double Tolerance = 0.25;

	/**
	 * A piece of construction as a solid box on a plan line, with its openings as the holes they are.
	 *
	 * Walls, columns and downstand beams are all this shape, so they are all swept against the same
	 * way rather than each getting its own test that could be forgotten.
	 */
	struct FWallSolid
	{
		FName Id;
		FVector2D Start = FVector2D::ZeroVector;
		FVector2D Direction = FVector2D(1.0, 0.0);
		FVector2D Normal = FVector2D(0.0, 1.0);
		double Length = 0.0;
		double HalfThickness = 0.0;
		double BaseZ = 0.0;
		double TopZ = 0.0;
		TArray<FHFOpening> Openings;
	};

	/** A false ceiling or bulkhead as the plane a part must stay below. */
	struct FCeilingPlane
	{
		FName Id;
		TArray<FVector2D> Outline;
		double SoffitZ = 0.0;
	};

	TArray<FWallSolid> MakeWallSolids(const FHFHouseSpec& Spec)
	{
		TArray<FWallSolid> Solids;
		Solids.Reserve(Spec.Walls.Num());

		for (const FHFWall& Wall : Spec.Walls)
		{
			const double Length = Wall.Length();
			if (Length <= UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			FWallSolid Solid;
			Solid.Id = Wall.Id;
			Solid.Start = Wall.Start;
			Solid.Direction = (Wall.End - Wall.Start) / Length;
			Solid.Normal = FVector2D(-Solid.Direction.Y, Solid.Direction.X);
			Solid.Length = Length;
			Solid.HalfThickness = Wall.Thickness * 0.5;
			Solid.BaseZ = Wall.BaseZ;
			Solid.TopZ = Wall.BaseZ + Wall.Height;

			for (const FHFOpening& Opening : Spec.Openings)
			{
				if (Opening.WallId == Wall.Id && Opening.Width > 0.0 && Opening.Height > 0.0)
				{
					Solid.Openings.Add(Opening);
				}
			}

			Solids.Add(MoveTemp(Solid));
		}

		// Columns and downstand beams are masonry too. A door that clears every partition and then
		// swings into the column on the junction beside it is exactly as wrong, and it is the sort
		// of thing that only shows up once someone walks the flat.
		for (const FHFColumn& Column : Spec.Columns)
		{
			if (Column.Size.X <= 0.0 || Column.Size.Y <= 0.0 || Column.Height <= 0.0)
			{
				continue;
			}

			const double Radians = FMath::DegreesToRadians(Column.RotationDegrees);
			const FVector2D Direction(FMath::Cos(Radians), FMath::Sin(Radians));

			FWallSolid Solid;
			Solid.Id = Column.Id;
			Solid.Direction = Direction;
			Solid.Normal = FVector2D(-Direction.Y, Direction.X);
			Solid.Start = Column.Position - Direction * (Column.Size.X * 0.5);
			Solid.Length = Column.Size.X;
			Solid.HalfThickness = Column.Size.Y * 0.5;
			Solid.BaseZ = Column.BaseZ;
			Solid.TopZ = Column.BaseZ + Column.Height;
			Solids.Add(MoveTemp(Solid));
		}

		for (const FHFBeam& Beam : Spec.Beams)
		{
			const double Length = Beam.Length();
			if (Length <= UE_KINDA_SMALL_NUMBER || Beam.Width <= 0.0 || Beam.Depth <= 0.0)
			{
				continue;
			}

			FWallSolid Solid;
			Solid.Id = Beam.Id;
			Solid.Start = Beam.Start;
			Solid.Direction = (Beam.End - Beam.Start) / Length;
			Solid.Normal = FVector2D(-Solid.Direction.Y, Solid.Direction.X);
			Solid.Length = Length;
			Solid.HalfThickness = Beam.Width * 0.5;
			Solid.BaseZ = Beam.SoffitZ - Beam.Depth;
			Solid.TopZ = Beam.SoffitZ;
			Solids.Add(MoveTemp(Solid));
		}

		return Solids;
	}

	TArray<FCeilingPlane> MakeCeilingPlanes(const FHFHouseSpec& Spec)
	{
		TArray<FCeilingPlane> Planes;

		for (const FHFFalseCeiling& Ceiling : Spec.FalseCeilings)
		{
			if (Ceiling.Style == EHFCeilingStyle::None || Ceiling.Drop <= 0.0)
			{
				continue;
			}

			const FHFRoom* Room = Spec.FindRoom(Ceiling.RoomId);
			if (Room == nullptr)
			{
				continue;
			}

			// A peripheral band or a cove leaves the middle of the room open, so only a full drop or
			// a bulkhead is a plane the whole of its outline has to clear. Treating a cove as solid
			// would fail doors that pass under nothing at all.
			const bool bSolidOverItsOutline =
				Ceiling.Style == EHFCeilingStyle::FullDrop || Ceiling.Style == EHFCeilingStyle::Bulkhead;

			FCeilingPlane Plane;
			Plane.Id = Ceiling.Id;
			Plane.Outline = (Ceiling.ExplicitPolygon.Num() >= 3) ? Ceiling.ExplicitPolygon : Room->Boundary;
			Plane.SoffitZ = Room->FloorZ + Room->CeilingHeight - (bSolidOverItsOutline ? Ceiling.Drop : 0.0);

			if (Plane.Outline.Num() >= 3)
			{
				Planes.Add(MoveTemp(Plane));
			}
		}

		return Planes;
	}

	bool PolygonContains(const TArray<FVector2D>& Polygon, const FVector2D& Point)
	{
		bool bInside = false;
		const int32 Count = Polygon.Num();

		for (int32 i = 0, j = Count - 1; i < Count; j = i++)
		{
			const FVector2D& A = Polygon[i];
			const FVector2D& B = Polygon[j];

			if (((A.Y > Point.Y) != (B.Y > Point.Y)) &&
				(Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X))
			{
				bInside = !bInside;
			}
		}

		return bInside;
	}

	/** How far a world point lies inside a wall's masonry, in centimetres. Zero if it is clear. */
	double PenetrationInto(const FWallSolid& Wall, const FVector& Point)
	{
		const FVector2D Relative = FVector2D(Point.X, Point.Y) - Wall.Start;
		const double Along = FVector2D::DotProduct(Relative, Wall.Direction);
		const double Across = FVector2D::DotProduct(Relative, Wall.Normal);

		// Distance to the nearest face of the wall box. Negative on any axis means outside it.
		double Depth = Wall.HalfThickness - FMath::Abs(Across);
		Depth = FMath::Min(Depth, FMath::Min(Along, Wall.Length - Along));
		Depth = FMath::Min(Depth, FMath::Min(Point.Z - Wall.BaseZ, Wall.TopZ - Point.Z));

		if (Depth <= Tolerance)
		{
			return 0.0;
		}

		// An opening is a hole cut clean through, so a part inside one is inside nothing.
		for (const FHFOpening& Opening : Wall.Openings)
		{
			const double SillZ = Wall.BaseZ + Opening.SillHeight;

			if (Along >= Opening.OffsetAlongWall - Opening.Width * 0.5 - Tolerance &&
				Along <= Opening.OffsetAlongWall + Opening.Width * 0.5 + Tolerance &&
				Point.Z >= SillZ - Tolerance &&
				Point.Z <= SillZ + Opening.Height + Tolerance)
			{
				return 0.0;
			}
		}

		return Depth;
	}

	/**
	 * The boxes one part is built from, in the part's own local space.
	 *
	 * A part is not one box any more. A door leaf still is, but a window sash is a picture frame
	 * with a pane in it and a catch on its stile, and its bounding box is mostly the hole in the
	 * middle. Sampling that box would put lattice points in fresh air and call them part of the sash.
	 *
	 * Every piece a generator emits is a closed box appended into the part's mesh, and appending
	 * leaves them as separate connected components - so the components ARE the boxes. Their bounds
	 * cover the solid exactly, with no void between them and nothing outside them.
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

	/**
	 * Points covering a part's solid at a given open amount.
	 *
	 * A lattice over each of the part's boxes, so the inside is sampled as well as the corners.
	 * Sampling only the mesh vertices would miss a leaf that straddles a thin partition without
	 * either end being inside it.
	 */
	void SamplePart(const FHFMeshPart& Part, double OpenAmount, TArray<FVector>& OutPoints)
	{
		FHFPartState State;
		State.PivotTransform = Part.PivotTransform;
		State.Motion = Part.Motion;
		State.OpenAmount = OpenAmount;

		const FTransform Pose = State.CurrentPose();

		TArray<FAxisAlignedBox3d> Boxes;
		ComponentBoxes(Part.Mesh, Boxes);

		constexpr int32 StepsAlong = 12;
		constexpr int32 StepsAcross = 2;
		constexpr int32 StepsUp = 4;

		OutPoints.Reset();
		for (const FAxisAlignedBox3d& Local : Boxes)
		{
			for (int32 i = 0; i <= StepsAlong; ++i)
			{
				const double X = FMath::Lerp(Local.Min.X, Local.Max.X, double(i) / StepsAlong);
				for (int32 j = 0; j <= StepsAcross; ++j)
				{
					const double Y = FMath::Lerp(Local.Min.Y, Local.Max.Y, double(j) / StepsAcross);
					for (int32 k = 0; k <= StepsUp; ++k)
					{
						const double Z = FMath::Lerp(Local.Min.Z, Local.Max.Z, double(k) / StepsUp);
						OutPoints.Add(Pose.TransformPosition(FVector(X, Y, Z)));
					}
				}
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSampleHouseSweepTest, "HouseForge.Articulation.SampleHouseSweep", HF_TEST_FLAGS)

bool FHFSampleHouseSweepTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeSweep;

	FHFHouseSpec Spec = FHFSampleHouse::Make2BHK();
	FHFUnits::ConvertToCentimeters(Spec);

	const TArray<FWallSolid> Walls = MakeWallSolids(Spec);
	const TArray<FCeilingPlane> Ceilings = MakeCeilingPlanes(Spec);

	if (!TestTrue(TEXT("The sample house has walls to sweep against"), Walls.Num() > 0))
	{
		return false;
	}

	int32 MovingParts = 0;
	TArray<FVector> Points;

	for (const FHFOpening& Opening : Spec.Openings)
	{
		const FHFWall* Wall = Spec.FindWall(Opening.WallId);
		if (Wall == nullptr)
		{
			continue;
		}

		TArray<FHFMeshPart> Parts;
		FHFGenerators::BuildOpeningParts(Opening, *Wall, Parts);

		for (const FHFMeshPart& Part : Parts)
		{
			MovingParts += Part.Motion.Moves() ? 1 : 0;

			// The sampling above is only equivalent to the solid while every one of the part's pieces
			// is a box. Say so out loud rather than let a later piece be swept as a box it is not.
			TArray<FAxisAlignedBox3d> Boxes;
			ComponentBoxes(Part.Mesh, Boxes);

			double BoxedVolume = 0.0;
			for (const FAxisAlignedBox3d& Box : Boxes)
			{
				BoxedVolume += Box.Volume();
			}

			const double MeshVolume = TMeshQueries<FDynamicMesh3>::GetVolumeArea(Part.Mesh).X;
			if (!FMath::IsNearlyEqual(MeshVolume, BoxedVolume, FMath::Max(BoxedVolume, 1.0) * 0.05))
			{
				AddWarning(FString::Printf(
					TEXT("Part '%s' of opening '%s' has a piece that is not a box; the sweep samples that piece's bounding box, which is now only an approximation."),
					*Part.PartId.ToString(), *Opening.Id.ToString()));
			}

			// Everything the part is already inside before it moves. Measured per solid, because a
			// clash that is there at zero open is a defect in the plan - a doorway drawn across a
			// column, say - and belongs to whoever drew it. What this test owns is the part's
			// motion: moving it must not drive it into anything further than it already was.
			auto DepthsBySolid = [&](double OpenAmount)
			{
				SamplePart(Part, OpenAmount, Points);

				TMap<FName, double> Depths;
				for (const FVector& Point : Points)
				{
					for (const FWallSolid& Solid : Walls)
					{
						const double Depth = PenetrationInto(Solid, Point);
						if (Depth > 0.0)
						{
							double& Worst = Depths.FindOrAdd(Solid.Id);
							Worst = FMath::Max(Worst, Depth);
						}
					}
				}
				return Depths;
			};

			const TMap<FName, double> Closed = DepthsBySolid(0.0);

			for (const TPair<FName, double>& Clash : Closed)
			{
				AddWarning(FString::Printf(
					TEXT("Part '%s' of opening '%s' is %.1f cm inside the construction of '%s' before it moves at all. That is a clash in the plan, not in the articulation - the opening and '%s' overlap."),
					*Part.PartId.ToString(), *Opening.Id.ToString(), Clash.Value,
					*Clash.Key.ToString(), *Clash.Key.ToString()));
			}

			// A fixed part has one pose; a moving one is checked the whole way, because the worst
			// case is very often halfway rather than at either limit.
			const int32 Steps = Part.Motion.Moves() ? 20 : 0;

			for (int32 Step = 0; Step <= Steps; ++Step)
			{
				const double OpenAmount = Steps > 0 ? double(Step) / Steps : 0.0;
				const TMap<FName, double> Depths = DepthsBySolid(OpenAmount);

				bool bFailed = false;

				for (const TPair<FName, double>& Clash : Depths)
				{
					const double* Baseline = Closed.Find(Clash.Key);
					if (Clash.Value > (Baseline != nullptr ? *Baseline : 0.0) + Tolerance)
					{
						AddError(FString::Printf(
							TEXT("Part '%s' of opening '%s' swings %.2f cm into the construction of '%s' at %.2f open; closed it was %.2f cm in."),
							*Part.PartId.ToString(), *Opening.Id.ToString(), Clash.Value,
							*Clash.Key.ToString(), OpenAmount, Baseline != nullptr ? *Baseline : 0.0));
						bFailed = true;
						break;
					}
				}

				double WorstBelowFloor = 0.0;
				double WorstThroughCeiling = 0.0;
				FName WorstCeilingId;

				for (const FVector& Point : Points)
				{
					const FVector2D Plan(Point.X, Point.Y);

					for (const FHFRoom& Room : Spec.Rooms)
					{
						if (Room.Boundary.Num() >= 3 && PolygonContains(Room.Boundary, Plan))
						{
							WorstBelowFloor = FMath::Max(WorstBelowFloor, Room.FloorZ - Point.Z);
						}
					}

					for (const FCeilingPlane& Ceiling : Ceilings)
					{
						if (PolygonContains(Ceiling.Outline, Plan) &&
							Point.Z - Ceiling.SoffitZ > WorstThroughCeiling)
						{
							WorstThroughCeiling = Point.Z - Ceiling.SoffitZ;
							WorstCeilingId = Ceiling.Id;
						}
					}
				}

				if (WorstBelowFloor > Tolerance)
				{
					AddError(FString::Printf(
						TEXT("Part '%s' of opening '%s' is %.2f cm below the floor at %.2f open."),
						*Part.PartId.ToString(), *Opening.Id.ToString(), WorstBelowFloor, OpenAmount));
					bFailed = true;
				}

				if (WorstThroughCeiling > Tolerance)
				{
					AddError(FString::Printf(
						TEXT("Part '%s' of opening '%s' is %.2f cm through ceiling '%s' at %.2f open."),
						*Part.PartId.ToString(), *Opening.Id.ToString(), WorstThroughCeiling,
						*WorstCeilingId.ToString(), OpenAmount));
					bFailed = true;
				}

				if (bFailed)
				{
					break;
				}
			}
		}
	}

	// If nothing moved, the sweep above proved nothing. The reference flat has seven swing doors and
	// two sliding units, so anything much below that means parts stopped being generated.
	TestTrue(TEXT("The reference flat has moving parts to sweep"), MovingParts >= 9);

	return true;
}

/**
 * The geometry and the validator have to be describing the same door.
 *
 * SwingBlocked reasons about where a leaf ends up and warns when that is not inside any room. It is
 * worth nothing if the leaf the generator actually builds swings the other way - the spec would
 * validate clean and the flat would still have doors opening into masonry. So the swept leaf is
 * checked against the same room the validator reasoned about, for every door in the reference flat.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSampleHouseSwingAgreementTest,
	"HouseForge.Articulation.SwingsMatchTheValidator", HF_TEST_FLAGS)

bool FHFSampleHouseSwingAgreementTest::RunTest(const FString& Parameters)
{
	FHFHouseSpec Spec = FHFSampleHouse::Make2BHK();
	FHFUnits::ConvertToCentimeters(Spec);

	// Agreement is only meaningful against a validator that is currently happy. If the sample starts
	// tripping SwingBlocked, that has to be fixed in the plan before this test means anything.
	TestFalse(TEXT("No door in the reference flat trips SwingBlocked"),
		FHFSpecValidator::Validate(Spec).Contains(TEXT("SwingBlocked")));

	// The shipped section, which is what BuildOpeningParts below is called with. The frame is what
	// moved the hinge line off the masonry, so the arithmetic here has to know about it.
	const FHFOpeningBuildParams Params;

	int32 Checked = 0;

	for (const FHFOpening& Opening : Spec.Openings)
	{
		if (Opening.Kind != EHFOpeningKind::Door || Opening.Swing == EHFSwing::None)
		{
			continue;
		}

		const FHFWall* Wall = Spec.FindWall(Opening.WallId);
		if (Wall == nullptr || Wall->Length() <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D Direction = (Wall->End - Wall->Start) / Wall->Length();
		const FVector2D Normal(-Direction.Y, Direction.X);
		const FVector2D Centre = Wall->Start + Direction * Opening.OffsetAlongWall;

		// Exactly the reasoning HFSpecValidator does, repeated here rather than reused so that a
		// change to one has to be reconciled with the other instead of matching by construction.
		const double Side =
			(Opening.Swing == EHFSwing::InwardLeft || Opening.Swing == EHFSwing::InwardRight) ? 1.0 : -1.0;
		const FVector2D ValidatorTip = Centre + Normal * (Opening.Width * 0.9 * Side);

		const FHFRoom* ValidatorRoom = Spec.Rooms.FindByPredicate(
			[&ValidatorTip](const FHFRoom& Room)
			{
				return Room.Boundary.Num() >= 3 && Room.ContainsPoint(ValidatorTip);
			});

		TArray<FHFMeshPart> Parts;
		FHFGenerators::BuildOpeningParts(Opening, *Wall, Parts);

		if (Parts.Num() != 1)
		{
			AddError(FString::Printf(TEXT("Door '%s' did not produce a single leaf."), *Opening.Id.ToString()));
			continue;
		}

		const FHFMeshPart& Leaf = Parts[0];

		FHFPartState State;
		State.PivotTransform = Leaf.PivotTransform;
		State.Motion = Leaf.Motion;

		// The far edge of the leaf at the travel limit: the point the validator's arc stands in for.
		const FAxisAlignedBox3d Local = Leaf.Mesh.GetBounds();
		const FVector GeometryTip = State.PoseAt(1.0).TransformPosition(
			FVector(Local.Max.X, Local.Center().Y, Local.Center().Z));
		const FVector2D GeometryPlan(GeometryTip.X, GeometryTip.Y);

		const double Across = FVector2D::DotProduct(GeometryPlan - Centre, Normal);

		if (FMath::Sign(Across) != Side)
		{
			AddError(FString::Printf(
				TEXT("Door '%s' is declared to swing %s, but its leaf ends up %.1f cm on the other side of the wall. The geometry and SwingBlocked disagree about this door."),
				*Opening.Id.ToString(), Side > 0.0 ? TEXT("inward") : TEXT("outward"), Across));
			continue;
		}

		// Open means open: a leaf standing square to the wall projects into the room by its own
		// width off a hinge that is on the FRAME, so it reaches somewhere between the daylight
		// opening it closes and the masonry opening plus the half wall its frame face sits on.
		//
		// Not the full opening width any more, and that is the fix rather than a regression: a leaf
		// that measured exactly its masonry opening was a leaf hung in a hole with no frame in it.
		const double DaylightWidth = Opening.Width - Params.Door.FrameFace * 2.0;
		const double Reach = FMath::Abs(Across);

		TestTrue(
			*FString::Printf(TEXT("Door '%s' opens at least its daylight width off the wall (%.1f of %.1f)"),
				*Opening.Id.ToString(), Reach, DaylightWidth),
			Reach >= DaylightWidth);
		TestTrue(
			*FString::Printf(TEXT("Door '%s' does not overreach its opening"), *Opening.Id.ToString()),
			Reach <= Opening.Width + Wall->Thickness * 0.5);

		// The swing arc HFSpecValidator sweeps, and the one AHFHouseActor draws, are both struck from
		// the MASONRY jamb ON THE WALL CENTRELINE, with the opening width for a radius - which is the
		// arc an AutoCAD plan draws, and the reason both were left alone here.
		//
		// A leaf hung in a frame pivots about the frame's room-side face instead, so it reaches half
		// a wall further into the room than that arc allows for: 107.4 cm against 105 on the main
		// door in a 230 wall. The bound is therefore the opening width plus the half wall the frame
		// stands on, and the residual - about 2 cm on an external wall, 1 cm internally - is a
		// fixture band the plan arc does not reach. It is named here rather than papered over: the
		// rule already treats a 40 mm leaf as a line with no thickness, so this is inside its
		// existing resolution, and moving the drawn arc off the drawing's would be the worse trade.
		const FVector2D MasonryHinge = Centre +
			Direction * ((Opening.Swing == EHFSwing::InwardLeft || Opening.Swing == EHFSwing::OutwardLeft)
				? -Opening.Width * 0.5 : Opening.Width * 0.5);

		const double FromMasonryJamb = FVector2D::Distance(GeometryPlan, MasonryHinge);
		const double PlanArc = Opening.Width + Wall->Thickness * 0.5;

		if (FromMasonryJamb > PlanArc + 0.01)
		{
			AddError(FString::Printf(
				TEXT("The leaf of door '%s' reaches %.1f cm from its masonry jamb, past the %.1f cm the plan arc covers even allowing for its frame. The swing rule and the geometry no longer describe the same door."),
				*Opening.Id.ToString(), FromMasonryJamb, PlanArc));
		}

		// And the invariant that does hold exactly, whatever the wall: the leaf reaches its own
		// width off its own hinge. A leaf that lost length to the frame it hangs in without the
		// hinge moving with it would still satisfy every bound above.
		const FVector2D HingePlan(Leaf.PivotTransform.GetLocation().X, Leaf.PivotTransform.GetLocation().Y);
		TestNearlyEqual(
			*FString::Printf(TEXT("Door '%s' reaches its own leaf width off its own hinge"),
				*Opening.Id.ToString()),
			FVector2D::Distance(GeometryPlan, HingePlan), Local.Max.X, 0.05);

		const FHFRoom* GeometryRoom = Spec.Rooms.FindByPredicate(
			[&GeometryPlan](const FHFRoom& Room)
			{
				return Room.Boundary.Num() >= 3 && Room.ContainsPoint(GeometryPlan);
			});

		const FName ValidatorRoomId = ValidatorRoom != nullptr ? ValidatorRoom->Id : NAME_None;
		const FName GeometryRoomId = GeometryRoom != nullptr ? GeometryRoom->Id : NAME_None;

		if (ValidatorRoomId != GeometryRoomId)
		{
			AddError(FString::Printf(
				TEXT("Door '%s' opens into '%s' according to the validator but into '%s' according to the geometry."),
				*Opening.Id.ToString(), *ValidatorRoomId.ToString(), *GeometryRoomId.ToString()));
		}

		++Checked;
	}

	TestTrue(TEXT("Every swing door in the reference flat was checked"), Checked >= 7);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
