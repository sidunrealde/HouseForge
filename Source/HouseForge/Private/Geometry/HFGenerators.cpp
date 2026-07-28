// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFGenerators.h"

#include "Geometry/HFMeshOps.h"

using namespace UE::Geometry;

namespace
{
	/** How far an opening cutter overshoots the wall faces, in centimetres. */
	constexpr double CutterOvershoot = 5.0;

	/** Door leaf and window frame thicknesses. */
	constexpr double DoorLeafThickness = 4.0;
	constexpr double WindowFrameDepth = 6.0;
	constexpr double WindowFrameWidth = 5.0;
	constexpr double GlassThickness = 0.8;

	struct FWallFrame
	{
		FVector2D Direction = FVector2D(1.0, 0.0);
		FVector2D Normal = FVector2D(0.0, 1.0);
		double Length = 0.0;
		double YawDegrees = 0.0;
		bool bValid = false;
	};

	FWallFrame MakeWallFrame(const FVector2D& Start, const FVector2D& End)
	{
		FWallFrame Frame;
		Frame.Length = FVector2D::Distance(Start, End);
		if (Frame.Length <= UE_KINDA_SMALL_NUMBER)
		{
			return Frame;
		}

		Frame.Direction = (End - Start) / Frame.Length;
		Frame.Normal = FVector2D(-Frame.Direction.Y, Frame.Direction.X);
		Frame.YawDegrees = FMath::RadiansToDegrees(FMath::Atan2(Frame.Direction.Y, Frame.Direction.X));
		Frame.bValid = true;
		return Frame;
	}
}

FVector2D FHFGenerators::OpeningCentre(const FHFOpening& Opening, const FHFWall& Wall)
{
	const FWallFrame Frame = MakeWallFrame(Wall.Start, Wall.End);
	if (!Frame.bValid)
	{
		return Wall.Start;
	}
	return Wall.Start + Frame.Direction * Opening.OffsetAlongWall;
}

FDynamicMesh3 FHFGenerators::GenerateWall(const FHFWall& Wall, const TArray<FHFOpening>& OpeningsInWall)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	const FWallFrame Frame = MakeWallFrame(Wall.Start, Wall.End);
	if (!Frame.bValid || Wall.Thickness <= 0.0 || Wall.Height <= 0.0)
	{
		return Mesh;
	}

	const FVector2D Midpoint = (Wall.Start + Wall.End) * 0.5;
	const FVector3d Centre(Midpoint.X, Midpoint.Y, Wall.BaseZ + Wall.Height * 0.5);
	const FVector3d Extents(Frame.Length * 0.5, Wall.Thickness * 0.5, Wall.Height * 0.5);

	FHFMeshOps::AppendBox(Mesh, Centre, Extents, Frame.YawDegrees, Wall.SurfaceRole);

	for (const FHFOpening& Opening : OpeningsInWall)
	{
		if (Opening.Width <= 0.0 || Opening.Height <= 0.0)
		{
			continue;
		}

		const FVector2D OpeningPlan = Wall.Start + Frame.Direction * Opening.OffsetAlongWall;
		const double CentreZ = Wall.BaseZ + Opening.SillHeight + Opening.Height * 0.5;

		FDynamicMesh3 Cutter;
		FHFMeshOps::InitialiseMesh(Cutter);

		// Overshoot the wall faces. A cutter flush with the surface leaves coplanar faces that the
		// boolean has to resolve, and it frequently resolves them badly - stray slivers, or no cut
		// at all.
		FHFMeshOps::AppendBox(Cutter,
			FVector3d(OpeningPlan.X, OpeningPlan.Y, CentreZ),
			FVector3d(Opening.Width * 0.5, Wall.Thickness * 0.5 + CutterOvershoot, Opening.Height * 0.5),
			Frame.YawDegrees, Wall.SurfaceRole);

		FHFMeshOps::SubtractInPlace(Mesh, Cutter);
	}

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

FDynamicMesh3 FHFGenerators::GenerateFloor(const FHFRoom& Room, double SlabThickness,
	const TArray<FVector2D>& SkirtingGaps, double GapWidth)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	if (Room.Boundary.Num() < 3 || SlabThickness <= 0.0)
	{
		return Mesh;
	}

	// The slab sits below the finished floor level, so FloorZ stays the walkable surface.
	FHFMeshOps::AppendPrism(Mesh, Room.Boundary, Room.FloorZ - SlabThickness, Room.FloorZ, Room.FloorRole);

	if (Room.SkirtingHeight > 0.0)
	{
		constexpr double SkirtingDepth = 1.8;
		const int32 Count = Room.Boundary.Num();

		for (int32 i = 0; i < Count; ++i)
		{
			const FVector2D& A = Room.Boundary[i];
			const FVector2D& B = Room.Boundary[(i + 1) % Count];

			const FWallFrame Edge = MakeWallFrame(A, B);
			if (!Edge.bValid)
			{
				continue;
			}

			// Walk the edge, skipping the stretch in front of each doorway. Running skirting
			// straight across an opening is one of the most obvious tells that geometry was
			// generated rather than modelled.
			TArray<TPair<double, double>> Gaps;
			for (const FVector2D& Gap : SkirtingGaps)
			{
				const FVector2D ToGap = Gap - A;
				const double Along = FVector2D::DotProduct(ToGap, Edge.Direction);
				const double Across = FMath::Abs(FVector2D::DotProduct(ToGap, Edge.Normal));

				if (Across < 30.0 && Along > -GapWidth && Along < Edge.Length + GapWidth)
				{
					Gaps.Add({ Along - GapWidth * 0.5, Along + GapWidth * 0.5 });
				}
			}
			Gaps.Sort([](const TPair<double, double>& L, const TPair<double, double>& R) { return L.Key < R.Key; });

			double Cursor = 0.0;
			auto EmitRun = [&](double From, double To)
			{
				const double RunLength = To - From;
				if (RunLength <= 1.0)
				{
					return;
				}

				const FVector2D RunCentre = A + Edge.Direction * ((From + To) * 0.5);
				// Inset so the skirting sits against the wall face rather than through it.
				const FVector2D Offset = Edge.Normal * (SkirtingDepth * 0.5);

				FHFMeshOps::AppendBox(Mesh,
					FVector3d(RunCentre.X + Offset.X, RunCentre.Y + Offset.Y, Room.FloorZ + Room.SkirtingHeight * 0.5),
					FVector3d(RunLength * 0.5, SkirtingDepth * 0.5, Room.SkirtingHeight * 0.5),
					Edge.YawDegrees, EHFSurfaceRole::Skirting);
			};

			for (const TPair<double, double>& Gap : Gaps)
			{
				EmitRun(Cursor, FMath::Min(Gap.Key, Edge.Length));
				Cursor = FMath::Max(Cursor, Gap.Value);
			}
			EmitRun(Cursor, Edge.Length);
		}
	}

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

FDynamicMesh3 FHFGenerators::GenerateBeam(const FHFBeam& Beam)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	const FWallFrame Frame = MakeWallFrame(Beam.Start, Beam.End);
	if (!Frame.bValid || Beam.Width <= 0.0 || Beam.Depth <= 0.0)
	{
		return Mesh;
	}

	const FVector2D Midpoint = (Beam.Start + Beam.End) * 0.5;

	// Beams hang down from the slab soffit, so they occupy ClearHeight..SoffitZ.
	FHFMeshOps::AppendBox(Mesh,
		FVector3d(Midpoint.X, Midpoint.Y, Beam.SoffitZ - Beam.Depth * 0.5),
		FVector3d(Frame.Length * 0.5, Beam.Width * 0.5, Beam.Depth * 0.5),
		Frame.YawDegrees, Beam.SurfaceRole);

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

FDynamicMesh3 FHFGenerators::GenerateColumn(const FHFColumn& Column)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	if (Column.Size.X <= 0.0 || Column.Size.Y <= 0.0 || Column.Height <= 0.0)
	{
		return Mesh;
	}

	FHFMeshOps::AppendBox(Mesh,
		FVector3d(Column.Position.X, Column.Position.Y, Column.BaseZ + Column.Height * 0.5),
		FVector3d(Column.Size.X * 0.5, Column.Size.Y * 0.5, Column.Height * 0.5),
		Column.RotationDegrees, Column.SurfaceRole);

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

FDynamicMesh3 FHFGenerators::GenerateOpeningInfill(const FHFOpening& Opening, const FHFWall& Wall)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	const FWallFrame Frame = MakeWallFrame(Wall.Start, Wall.End);
	if (!Frame.bValid || Opening.Width <= 0.0 || Opening.Height <= 0.0)
	{
		return Mesh;
	}

	// An archway is a hole and nothing else.
	if (Opening.Kind == EHFOpeningKind::Archway)
	{
		return Mesh;
	}

	const FVector2D Plan = Wall.Start + Frame.Direction * Opening.OffsetAlongWall;
	const double SillZ = Wall.BaseZ + Opening.SillHeight;
	const double CentreZ = SillZ + Opening.Height * 0.5;

	const bool bIsDoor = Opening.Kind == EHFOpeningKind::Door || Opening.Kind == EHFOpeningKind::SlidingDoor;

	if (bIsDoor)
	{
		// The leaf, sitting in the plane of the wall. It is drawn closed: an open leaf would be
		// modelled at whatever angle, and the swing is already legible from the plan.
		FHFMeshOps::AppendBox(Mesh,
			FVector3d(Plan.X, Plan.Y, CentreZ),
			FVector3d(Opening.Width * 0.5 - 0.5, DoorLeafThickness * 0.5, Opening.Height * 0.5 - 0.5),
			Frame.YawDegrees, EHFSurfaceRole::DoorLeaf);
	}
	else
	{
		// Window: a frame around the reveal, with glazing inside it.
		const double HalfWidth = Opening.Width * 0.5;
		const double HalfHeight = Opening.Height * 0.5;

		auto AppendFrameMember = [&](double OffsetAlong, double OffsetUp, double HalfAlong, double HalfUp)
		{
			const FVector2D MemberPlan = Plan + Frame.Direction * OffsetAlong;
			FHFMeshOps::AppendBox(Mesh,
				FVector3d(MemberPlan.X, MemberPlan.Y, CentreZ + OffsetUp),
				FVector3d(HalfAlong, WindowFrameDepth * 0.5, HalfUp),
				Frame.YawDegrees, EHFSurfaceRole::WindowFrame);
		};

		AppendFrameMember(0.0, HalfHeight - WindowFrameWidth * 0.5, HalfWidth, WindowFrameWidth * 0.5);   // head
		AppendFrameMember(0.0, -HalfHeight + WindowFrameWidth * 0.5, HalfWidth, WindowFrameWidth * 0.5);  // sill
		AppendFrameMember(-HalfWidth + WindowFrameWidth * 0.5, 0.0, WindowFrameWidth * 0.5, HalfHeight);  // jamb
		AppendFrameMember(HalfWidth - WindowFrameWidth * 0.5, 0.0, WindowFrameWidth * 0.5, HalfHeight);   // jamb

		// A central mullion, once the opening is wide enough to need one.
		if (Opening.Width > 120.0)
		{
			AppendFrameMember(0.0, 0.0, WindowFrameWidth * 0.5, HalfHeight - WindowFrameWidth);
		}

		FHFMeshOps::AppendBox(Mesh,
			FVector3d(Plan.X, Plan.Y, CentreZ),
			FVector3d(HalfWidth - WindowFrameWidth, GlassThickness * 0.5, HalfHeight - WindowFrameWidth),
			Frame.YawDegrees, EHFSurfaceRole::Glass);
	}

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}
