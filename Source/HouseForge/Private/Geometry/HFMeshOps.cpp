// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFMeshOps.h"

#include "HouseForge.h"

#include "CompGeom/PolygonTriangulation.h"
#include "ConstrainedDelaunay2.h"
#include "Curve/GeneralPolygon2.h"
#include "Curve/PolygonOffsetUtils.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "Operations/MeshBoolean.h"
#include "Parameterization/DynamicMeshUVEditor.h"

using namespace UE::Geometry;

EHFSurfaceRole FHFMeshOps::RoleForGroup(int32 GroupId)
{
	const int32 Index = GroupId - 1;
	const int32 Max = static_cast<int32>(EHFSurfaceRole::Structure);
	return (Index >= 0 && Index <= Max) ? static_cast<EHFSurfaceRole>(Index) : EHFSurfaceRole::WallPaint;
}

void FHFMeshOps::InitialiseMesh(FDynamicMesh3& Mesh)
{
	Mesh.Clear();
	Mesh.EnableTriangleGroups();
	Mesh.EnableAttributes();
}

double FHFMeshOps::SignedArea(const TArray<FVector2D>& Polygon)
{
	const int32 Count = Polygon.Num();
	if (Count < 3)
	{
		return 0.0;
	}

	double Twice = 0.0;
	for (int32 i = 0, j = Count - 1; i < Count; j = i++)
	{
		Twice += (Polygon[j].X * Polygon[i].Y) - (Polygon[i].X * Polygon[j].Y);
	}
	return Twice * 0.5;
}

void FHFMeshOps::AppendBox(FDynamicMesh3& Mesh, const FVector3d& Centre,
	const FVector3d& Extents, double YawDegrees, EHFSurfaceRole Role)
{
	if (Extents.X <= 0.0 || Extents.Y <= 0.0 || Extents.Z <= 0.0)
	{
		return;
	}

	const double Radians = FMath::DegreesToRadians(YawDegrees);
	const double CosYaw = FMath::Cos(Radians);
	const double SinYaw = FMath::Sin(Radians);

	auto ToWorld = [&](double X, double Y, double Z)
	{
		return FVector3d(
			Centre.X + X * CosYaw - Y * SinYaw,
			Centre.Y + X * SinYaw + Y * CosYaw,
			Centre.Z + Z);
	};

	// Corners: 0-3 bottom counter-clockwise, 4-7 top.
	int32 V[8];
	int32 Index = 0;
	for (int32 ZSide = -1; ZSide <= 1; ZSide += 2)
	{
		const double Z = Extents.Z * ZSide;
		const double Corners[4][2] = {
			{ -Extents.X, -Extents.Y },
			{  Extents.X, -Extents.Y },
			{  Extents.X,  Extents.Y },
			{ -Extents.X,  Extents.Y }
		};
		for (const auto& Corner : Corners)
		{
			V[Index++] = Mesh.AppendVertex(ToWorld(Corner[0], Corner[1], Z));
		}
	}

	const int32 Group = GroupForRole(Role);

	// Wound so normals face outward under FDynamicMesh3's convention. Getting this backwards is
	// quietly disastrous: the mesh still looks solid in the viewport, but its signed volume is
	// negative and mesh booleans subtract nothing at all, so every opening silently fails to cut.
	auto Quad = [&](int32 A, int32 B, int32 C, int32 D)
	{
		Mesh.AppendTriangle(V[A], V[C], V[B], Group);
		Mesh.AppendTriangle(V[A], V[D], V[C], Group);
	};

	Quad(0, 3, 2, 1);	// bottom, wound so its normal points down
	Quad(4, 5, 6, 7);	// top
	Quad(0, 1, 5, 4);	// -Y
	Quad(1, 2, 6, 5);	// +X
	Quad(2, 3, 7, 6);	// +Y
	Quad(3, 0, 4, 7);	// -X
}

bool FHFMeshOps::AppendPrism(FDynamicMesh3& Mesh, const TArray<FVector2D>& Polygon,
	double BottomZ, double TopZ, EHFSurfaceRole Role)
{
	if (Polygon.Num() < 3 || FMath::IsNearlyEqual(BottomZ, TopZ))
	{
		// Logged, because a silent no-op is indistinguishable from geometry that was never asked
		// for: the caller gets an empty mesh, the actor still exists, and the element count in the
		// build log is right. See the triangulation failure below for how ordinary input gets here.
		UE_LOG(LogHouseForge, Warning,
			TEXT("Prism not built: %d boundary points over a height of %.4f; no geometry emitted."),
			Polygon.Num(), TopZ - BottomZ);
		return false;
	}

	// Normalise winding to counter-clockwise so the cap normals come out predictably regardless of
	// how the boundary was authored - drawings are read in both directions.
	TArray<FVector2D> Boundary = Polygon;
	if (SignedArea(Boundary) < 0.0)
	{
		Algo::Reverse(Boundary);
	}

	TArray<FVector2d> Flat;
	Flat.Reserve(Boundary.Num());
	for (const FVector2D& Point : Boundary)
	{
		Flat.Add(FVector2d(Point.X, Point.Y));
	}

	// Handles concave boundaries, which is the whole reason for not fanning from a centroid.
	//
	// bOrientAsHoleFill=false, and it is not optional: the default is TRUE, which winds the output
	// triangles OPPOSITE to the input polygon because the usual caller is patching a hole and wants
	// the patch facing back the other way. Taking that default gives caps facing into the solid while
	// the side walls below face out of it, and nothing catches it - GetVolumeArea integrates along X
	// only, so a Z-extruded prism's caps contribute exactly zero to the volume it reports.
	TArray<FIndex3i> Triangles;
	PolygonTriangulation::TriangulateSimplePolygon(Flat, Triangles, /*bOrientAsHoleFill*/ false);
	if (Triangles.IsEmpty())
	{
		// The reachable case, and it is reachable from ordinary user input rather than from abuse:
		// the triangulator produces nothing for a polygon that is not simple, and a bow-tie room
		// boundary is an everyday mis-read of a plan. The floor then comes back empty while the
		// walls around it generate perfectly, so a top-down view still shows the room outline and
		// the hole reads as an unfinished floor rather than as a failure.
		UE_LOG(LogHouseForge, Warning,
			TEXT("Prism not built: a %d-point boundary could not be triangulated - it is probably self-intersecting."),
			Boundary.Num());
		return false;
	}

	const int32 Count = Boundary.Num();
	const int32 Group = GroupForRole(Role);

	TArray<int32> BottomVerts;
	TArray<int32> TopVerts;
	BottomVerts.Reserve(Count);
	TopVerts.Reserve(Count);
	for (const FVector2D& Point : Boundary)
	{
		BottomVerts.Add(Mesh.AppendVertex(FVector3d(Point.X, Point.Y, BottomZ)));
		TopVerts.Add(Mesh.AppendVertex(FVector3d(Point.X, Point.Y, TopZ)));
	}

	// Same outward-facing convention as AppendBox: caps face away from the solid, sides wound to
	// match. An inverted prism has negative volume and silently defeats mesh booleans.
	for (const FIndex3i& Tri : Triangles)
	{
		Mesh.AppendTriangle(BottomVerts[Tri.A], BottomVerts[Tri.B], BottomVerts[Tri.C], Group);
		Mesh.AppendTriangle(TopVerts[Tri.C], TopVerts[Tri.B], TopVerts[Tri.A], Group);
	}

	for (int32 i = 0; i < Count; ++i)
	{
		const int32 Next = (i + 1) % Count;
		Mesh.AppendTriangle(BottomVerts[i], TopVerts[Next], BottomVerts[Next], Group);
		Mesh.AppendTriangle(BottomVerts[i], TopVerts[i], TopVerts[Next], Group);
	}

	return true;
}

bool FHFMeshOps::AppendPrismWithHoles(FDynamicMesh3& Mesh, const TArray<FVector2D>& Outer,
	const TArray<TArray<FVector2D>>& Holes, double BottomZ, double TopZ, EHFSurfaceRole Role)
{
	if (Outer.Num() < 3 || FMath::IsNearlyEqual(BottomZ, TopZ))
	{
		UE_LOG(LogHouseForge, Warning,
			TEXT("Prism with holes not built: %d outer points over a height of %.4f; no geometry emitted."),
			Outer.Num(), TopZ - BottomZ);
		return false;
	}

	if (Holes.IsEmpty())
	{
		return AppendPrism(Mesh, Outer, BottomZ, TopZ, Role);
	}

	// Clipper and the triangulator both want the outer loop counter-clockwise and holes clockwise.
	auto ToOriented = [](const TArray<FVector2D>& Loop, bool bWantCounterClockwise)
	{
		TArray<FVector2d> Points;
		Points.Reserve(Loop.Num());
		for (const FVector2D& Point : Loop)
		{
			Points.Add(FVector2d(Point.X, Point.Y));
		}

		FPolygon2d Polygon(Points);
		const bool bIsCounterClockwise = Polygon.SignedArea() > 0.0;
		if (bIsCounterClockwise != bWantCounterClockwise)
		{
			Polygon.Reverse();
		}
		return Polygon;
	};

	FGeneralPolygon2d General(ToOriented(Outer, true));
	TArray<TArray<FVector2D>> UsedHoles;

	for (const TArray<FVector2D>& Hole : Holes)
	{
		if (Hole.Num() < 3)
		{
			continue;
		}

		FPolygon2d HolePolygon = ToOriented(Hole, false);
		// AddHole validates containment and non-overlap; a hole it rejects would corrupt the
		// triangulation, so it is dropped rather than forced.
		if (General.AddHole(HolePolygon, /*bCheckContainment*/ true, /*bCheckOrientation*/ true))
		{
			TArray<FVector2D>& Kept = UsedHoles.AddDefaulted_GetRef();
			for (const FVector2d& Point : HolePolygon.GetVertices())
			{
				Kept.Add(FVector2D(Point.X, Point.Y));
			}
		}
	}

	if (UsedHoles.IsEmpty())
	{
		return AppendPrism(Mesh, Outer, BottomZ, TopZ, Role);
	}

	FConstrainedDelaunay2d Triangulator;
	// Odd rather than Positive: it fills by nesting depth rather than winding direction, so the
	// hole is excluded regardless of which way round the loops ended up. Relying on winding gave a
	// band whose volume was outer plus inner instead of outer minus inner.
	Triangulator.FillRule = FConstrainedDelaunay2d::EFillRule::Odd;
	Triangulator.Add(General);

	if (!Triangulator.Triangulate() || Triangulator.Triangles.IsEmpty())
	{
		UE_LOG(LogHouseForge, Warning,
			TEXT("Prism with holes not built: a %d-point outer boundary with %d holes could not be triangulated."),
			Outer.Num(), UsedHoles.Num());
		return false;
	}

	const int32 Group = GroupForRole(Role);

	// Caps share the triangulated vertex set; the side walls are built from the loops directly and
	// reuse those same vertices, which is what keeps the prism watertight.
	TArray<int32> BottomVerts;
	TArray<int32> TopVerts;
	BottomVerts.Reserve(Triangulator.Vertices.Num());
	TopVerts.Reserve(Triangulator.Vertices.Num());

	for (const FVector2d& Vertex : Triangulator.Vertices)
	{
		BottomVerts.Add(Mesh.AppendVertex(FVector3d(Vertex.X, Vertex.Y, BottomZ)));
		TopVerts.Add(Mesh.AppendVertex(FVector3d(Vertex.X, Vertex.Y, TopZ)));
	}

	for (const FIndex3i& Tri : Triangulator.Triangles)
	{
		Mesh.AppendTriangle(BottomVerts[Tri.A], BottomVerts[Tri.B], BottomVerts[Tri.C], Group);
		Mesh.AppendTriangle(TopVerts[Tri.C], TopVerts[Tri.B], TopVerts[Tri.A], Group);
	}

	// The triangulator keeps input points at the front of its vertex list, in the order added:
	// the outer loop first, then each accepted hole.
	// One winding rule for every loop. Holes are already stored wound opposite to the outer
	// boundary, so walking them in order reverses the wall direction on its own - mirroring the
	// formula as well flips them back, which made the hole's walls face outward and its volume
	// count as solid instead of void.
	auto AppendWall = [&](int32 StartIndex, int32 Count)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 A = StartIndex + i;
			const int32 B = StartIndex + ((i + 1) % Count);

			Mesh.AppendTriangle(BottomVerts[A], TopVerts[B], BottomVerts[B], Group);
			Mesh.AppendTriangle(BottomVerts[A], TopVerts[A], TopVerts[B], Group);
		}
	};

	const int32 OuterCount = General.GetOuter().VertexCount();
	AppendWall(0, OuterCount);

	int32 Cursor = OuterCount;
	for (const FPolygon2d& Hole : General.GetHoles())
	{
		AppendWall(Cursor, Hole.VertexCount());
		Cursor += Hole.VertexCount();
	}

	return true;
}

bool FHFMeshOps::AppendExtrudedSection(FDynamicMesh3& Mesh, const TArray<FVector2D>& Section,
	const FVector3d& Origin, const FVector3d& SectionU, const FVector3d& SweepDir,
	double SweepLength, EHFSurfaceRole Role)
{
	if (Section.Num() < 3 || FMath::Abs(SweepLength) <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	FVector3d W = SweepDir;
	double Length = SweepLength;
	if (Length < 0.0)
	{
		// Sweeping backwards is a legitimate request - a profile run along a panel edge picks its
		// direction from the panel's frame, not from the caller's convenience.
		W = -W;
		Length = -Length;
	}
	if (!W.Normalize())
	{
		return false;
	}

	// (U, V, W) right-handed by construction: V = W x U gives U x V = W for any U perpendicular to
	// W. Deriving V rather than taking it is what makes an inside-out sweep impossible to ask for.
	FVector3d U = SectionU - W * SectionU.Dot(W);
	if (!U.Normalize())
	{
		return false;
	}
	const FVector3d V = W.Cross(U);

	// Same winding normalisation as AppendPrism: sections are authored in whichever direction reads
	// naturally, and the caps have to come out facing the same way regardless.
	TArray<FVector2D> Boundary = Section;
	if (SignedArea(Boundary) < 0.0)
	{
		Algo::Reverse(Boundary);
	}

	TArray<FVector2d> Flat;
	Flat.Reserve(Boundary.Num());
	for (const FVector2D& Point : Boundary)
	{
		Flat.Add(FVector2d(Point.X, Point.Y));
	}

	// Concave sections are the norm here - a J-profile cutter is an L with a chamfer taken off it.
	// bOrientAsHoleFill=false for the reason spelled out in AppendPrism: the default reverses the
	// winding. A swept section is where that bites hardest, because its caps are the faces that carry
	// the whole of the volume integral.
	TArray<FIndex3i> Triangles;
	PolygonTriangulation::TriangulateSimplePolygon(Flat, Triangles, /*bOrientAsHoleFill*/ false);
	if (Triangles.IsEmpty())
	{
		return false;
	}

	const int32 Count = Boundary.Num();
	const int32 Group = GroupForRole(Role);

	TArray<int32> StartVerts;
	TArray<int32> EndVerts;
	StartVerts.Reserve(Count);
	EndVerts.Reserve(Count);
	for (const FVector2D& Point : Boundary)
	{
		const FVector3d InPlane = Origin + U * Point.X + V * Point.Y;
		StartVerts.Add(Mesh.AppendVertex(InPlane));
		EndVerts.Add(Mesh.AppendVertex(InPlane + W * Length));
	}

	// Identical winding to AppendPrism, with (U, V, W) standing in for (X, Y, Z).
	for (const FIndex3i& Tri : Triangles)
	{
		Mesh.AppendTriangle(StartVerts[Tri.A], StartVerts[Tri.B], StartVerts[Tri.C], Group);
		Mesh.AppendTriangle(EndVerts[Tri.C], EndVerts[Tri.B], EndVerts[Tri.A], Group);
	}

	for (int32 i = 0; i < Count; ++i)
	{
		const int32 Next = (i + 1) % Count;
		Mesh.AppendTriangle(StartVerts[i], EndVerts[Next], StartVerts[Next], Group);
		Mesh.AppendTriangle(StartVerts[i], EndVerts[i], EndVerts[Next], Group);
	}

	return true;
}

bool FHFMeshOps::AppendRevolvedProfile(FDynamicMesh3& Mesh, const TArray<FVector2D>& Profile,
	const FVector3d& Origin, const FVector3d& Axis, int32 SideCount, EHFSurfaceRole Role)
{
	if (Profile.Num() < 2)
	{
		return false;
	}

	// Rounded up rather than rejected: the caller asked for a smoothness, not for a vertex count,
	// and a multiple of four is what puts vertices on both in-plane axes so the bounds come out at
	// the full diameter.
	const int32 Sides = FMath::Max(4, ((FMath::Max(SideCount, 3) + 3) / 4) * 4);

	FVector3d W = Axis;
	if (!W.Normalize())
	{
		return false;
	}

	// Any perpendicular does; a surface of revolution does not care where its seam falls. Picked
	// deterministically so the same profile always produces the same mesh.
	FVector3d U = (FMath::Abs(W.Z) < 0.9) ? FVector3d::UnitZ().Cross(W) : FVector3d::UnitX().Cross(W);
	if (!U.Normalize())
	{
		return false;
	}
	const FVector3d V = W.Cross(U);

	// Consecutive duplicates would emit a band of zero-area triangles, which is not a closed solid
	// so much as a closed solid with rubbish welded into it.
	TArray<FVector2D> Points;
	Points.Reserve(Profile.Num());
	for (const FVector2D& Point : Profile)
	{
		const FVector2D Clamped(Point.X, FMath::Max(Point.Y, 0.0));
		if (Points.IsEmpty() || !Points.Last().Equals(Clamped, UE_KINDA_SMALL_NUMBER))
		{
			Points.Add(Clamped);
		}
	}
	if (Points.Num() < 2)
	{
		return false;
	}

	for (int32 i = 1; i < Points.Num() - 1; ++i)
	{
		if (Points[i].Y <= UE_KINDA_SMALL_NUMBER)
		{
			// A zero radius in the middle pinches the solid into two lobes joined at a point. That
			// is non-manifold, so refuse it rather than emit something IsClosed would call fine.
			UE_LOG(LogHouseForge, Warning,
				TEXT("Revolved profile has a zero radius at interior point %d; refusing to pinch the solid."), i);
			return false;
		}
	}

	const int32 Group = GroupForRole(Role);

	TArray<TArray<int32>> Rings;
	Rings.SetNum(Points.Num());
	for (int32 r = 0; r < Points.Num(); ++r)
	{
		const FVector3d Centre = Origin + W * Points[r].X;
		const double Radius = Points[r].Y;

		if (Radius <= UE_KINDA_SMALL_NUMBER)
		{
			Rings[r].Add(Mesh.AppendVertex(Centre));
			continue;
		}

		Rings[r].Reserve(Sides);
		for (int32 s = 0; s < Sides; ++s)
		{
			const double Theta = (2.0 * UE_DOUBLE_PI * s) / Sides;
			Rings[r].Add(Mesh.AppendVertex(Centre + (U * FMath::Cos(Theta) + V * FMath::Sin(Theta)) * Radius));
		}
	}

	// Outward-facing under the same convention AppendBox uses: Quad(A, B, C, D) winds as (A, C, B)
	// and (A, D, C).
	auto Quad = [&](int32 A, int32 B, int32 C, int32 D)
	{
		Mesh.AppendTriangle(A, C, B, Group);
		Mesh.AppendTriangle(A, D, C, Group);
	};

	for (int32 r = 0; r + 1 < Points.Num(); ++r)
	{
		const TArray<int32>& Lower = Rings[r];
		const TArray<int32>& Upper = Rings[r + 1];

		for (int32 s = 0; s < Sides; ++s)
		{
			const int32 Next = (s + 1) % Sides;

			if (Lower.Num() == 1)
			{
				Mesh.AppendTriangle(Lower[0], Upper[s], Upper[Next], Group);
			}
			else if (Upper.Num() == 1)
			{
				Mesh.AppendTriangle(Lower[s], Upper[0], Lower[Next], Group);
			}
			else
			{
				Quad(Lower[s], Lower[Next], Upper[Next], Upper[s]);
			}
		}
	}

	// Cap whichever ends are discs. An apex needs no cap; a disc left open is a hole, and a mesh
	// with a hole silently defeats every boolean it is ever handed to.
	if (Rings[0].Num() > 1)
	{
		const int32 Centre = Mesh.AppendVertex(Origin + W * Points[0].X);
		for (int32 s = 0; s < Sides; ++s)
		{
			Mesh.AppendTriangle(Centre, Rings[0][s], Rings[0][(s + 1) % Sides], Group);
		}
	}
	if (Rings.Last().Num() > 1)
	{
		const TArray<int32>& Ring = Rings.Last();
		const int32 Centre = Mesh.AppendVertex(Origin + W * Points.Last().X);
		for (int32 s = 0; s < Sides; ++s)
		{
			Mesh.AppendTriangle(Centre, Ring[(s + 1) % Sides], Ring[s], Group);
		}
	}

	return true;
}

void FHFMeshOps::AppendPreservingRoles(FDynamicMesh3& Target, const FDynamicMesh3& Source)
{
	if (Source.TriangleCount() == 0)
	{
		return;
	}

	// Without these the append drops the source's groups and UVs on the floor rather than failing,
	// and a caller handed a bare mesh would get untagged, unwrapped geometry back.
	if (!Target.HasTriangleGroups())
	{
		Target.EnableTriangleGroups();
	}
	if (!Target.HasAttributes())
	{
		Target.EnableAttributes();
	}

	// Target is somebody else's mesh and may have been relocated by the array holding it.
	AdoptAttributes(Target);

	Target.EnableMatchingAttributes(Source, /*bClearExisting*/ false, /*bDiscardExtraAttributes*/ false);

	FDynamicMesh3::FAppendInfo Info;
	Target.AppendWithOffsets(Source, &Info);

	if (Info.GroupOffset == 0)
	{
		return;
	}

	// Put the roles back. The offset the append applied is exactly what has to come off again; the
	// group counter is left where the append moved it, which costs nothing and keeps any later
	// AllocateTriangleGroup from colliding with a role id.
	for (int32 Tid = Info.TriangleOffset; Tid < Target.MaxTriangleID(); ++Tid)
	{
		if (Target.IsTriangle(Tid))
		{
			Target.SetTriangleGroup(Tid, Target.GetTriangleGroup(Tid) - Info.GroupOffset);
		}
	}
}

bool FHFMeshOps::SubtractInPlace(FDynamicMesh3& Target, const FDynamicMesh3& Tool)
{
	if (Tool.TriangleCount() == 0 || Target.TriangleCount() == 0)
	{
		return false;
	}

	// Same reason as AppendPreservingRoles: Target arrives from the caller, and a part mesh living
	// in a TArray has had its attribute back-pointer left behind by the array's last reallocation.
	AdoptAttributes(Target);

	const bool bInputsClosed = IsClosed(Target) && IsClosed(Tool);

	FDynamicMesh3 Result;
	Result.EnableTriangleGroups();
	Result.EnableAttributes();

	FMeshBoolean Boolean(&Target, FTransformSRT3d::Identity(),
		&Tool, FTransformSRT3d::Identity(),
		&Result, FMeshBoolean::EBooleanOp::Difference);
	Boolean.bPutResultInInputSpace = true;

	// The one thing standing between a cut face and the material panel. The tool's triangles are
	// appended into the result with freshly allocated group ids, so ask for the map back and undo
	// the renumbering below - the group IS the surface role here, not an arbitrary partition.
	Boolean.bPopulateSecondMeshGroupMap = true;

	// Compute() returns false whenever it could not resolve every intersection perfectly, which it
	// reports even for cuts that came out clean - a through-hole in a box comes back "invalid"
	// while being exactly right. Judging the result is therefore more reliable than trusting the
	// return value, which was silently leaving ceilings and fan drops uncut.
	const bool bComputed = Boolean.Compute();

	const bool bResultUsable = Result.TriangleCount() > 0 && (!bInputsClosed || IsClosed(Result));

	if (!bResultUsable)
	{
		// Leaving the target uncut is the safer failure: a half-subtracted wall still looks
		// plausible in a screenshot, which is exactly how a bad cut would go unnoticed. Log it,
		// because a silent no-op is indistinguishable from a cut that was never requested.
		UE_LOG(LogHouseForge, Warning,
			TEXT("Mesh subtraction produced unusable geometry (computed=%d, result tris=%d, closed=%d); target left uncut."),
			bComputed ? 1 : 0, Result.TriangleCount(), IsClosed(Result) ? 1 : 0);
		return false;
	}

	// Put the tool's own groups back on the faces it left behind. The map runs tool group -> result
	// group, and every face the cut exposed has to come back the other way; anything that came from
	// the target kept its group and is not in the map at all.
	const TMap<int32, int32>& ToolToResult = Boolean.SecondMeshGroupMap.GetForwardMap();
	if (Result.HasTriangleGroups() && ToolToResult.Num() > 0)
	{
		TMap<int32, int32> ResultToTool;
		ResultToTool.Reserve(ToolToResult.Num());
		for (const TPair<int32, int32>& Renumbered : ToolToResult)
		{
			ResultToTool.Add(Renumbered.Value, Renumbered.Key);
		}

		for (const int32 Tid : Result.TriangleIndicesItr())
		{
			if (const int32* ToolGroup = ResultToTool.Find(Result.GetTriangleGroup(Tid)))
			{
				Result.SetTriangleGroup(Tid, *ToolGroup);
			}
		}
	}

	Target = MoveTemp(Result);
	return true;
}

void FHFMeshOps::AdoptAttributes(FDynamicMesh3& Mesh)
{
	if (!Mesh.HasAttributes() || Mesh.Attributes()->GetParentMesh() == &Mesh)
	{
		return;
	}

	// FDynamicMeshAttributeSet::Reparent is private to FDynamicMesh3, and the two places it runs are
	// the move constructor and the move assignment - neither of which dereferences the stale pointer
	// on the way past, which is what makes this round trip safe on a mesh that has been relocated.
	// It is a handful of container moves, and it only runs when the back-pointer is actually wrong.
	FDynamicMesh3 Rehomed(MoveTemp(Mesh));
	Mesh = MoveTemp(Rehomed);
}

void FHFMeshOps::ApplyWorldScaleUVs(FDynamicMesh3& Mesh, double TexelSizeCm)
{
	if (Mesh.TriangleCount() == 0 || TexelSizeCm <= 0.0)
	{
		return;
	}

	if (!Mesh.HasAttributes())
	{
		Mesh.EnableAttributes();
	}

	// Every generator funnels through here, which is the only reason this is cheap enough to be a
	// guarantee rather than a rule somebody has to remember. See AdoptAttributes for what it repairs.
	AdoptAttributes(Mesh);

	FDynamicMeshUVOverlay* UVs = Mesh.Attributes()->PrimaryUV();
	if (UVs == nullptr)
	{
		return;
	}
	UVs->ClearElements();

	// Project each triangle along its dominant axis rather than using the UV editor's box
	// projection: that one atlases the six faces into a cube-cross layout, so UVs no longer
	// correspond to world distance. Here UV is simply world position over texel size, which means
	// one tile really is TexelSizeCm across - the property the material panel needs in order to
	// express tiling in millimetres.
	const double InvTexel = 1.0 / TexelSizeCm;

	for (const int32 Tid : Mesh.TriangleIndicesItr())
	{
		const FIndex3i Tri = Mesh.GetTriangle(Tid);
		const FVector3d Normal = Mesh.GetTriNormal(Tid);

		const double AbsX = FMath::Abs(Normal.X);
		const double AbsY = FMath::Abs(Normal.Y);
		const double AbsZ = FMath::Abs(Normal.Z);

		int32 Elements[3];
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FVector3d P = Mesh.GetVertex(Tri[Corner]);

			FVector2f UV;
			if (AbsZ >= AbsX && AbsZ >= AbsY)		{ UV = FVector2f(P.X * InvTexel, P.Y * InvTexel); }
			else if (AbsX >= AbsY)					{ UV = FVector2f(P.Y * InvTexel, P.Z * InvTexel); }
			else									{ UV = FVector2f(P.X * InvTexel, P.Z * InvTexel); }

			Elements[Corner] = UVs->AppendElement(UV);
		}

		UVs->SetTriangle(Tid, FIndex3i(Elements[0], Elements[1], Elements[2]));
	}

	ComputeShadingNormals(Mesh);
}

void FHFMeshOps::ComputeShadingNormals(FDynamicMesh3& Mesh, double HardEdgeAngleDegrees)
{
	if (Mesh.TriangleCount() == 0)
	{
		return;
	}

	if (!Mesh.HasAttributes())
	{
		Mesh.EnableAttributes();
	}

	FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
	if (Normals == nullptr)
	{
		return;
	}

	// The split has to happen FIRST, and calling the recompute on its own is a silent no-op.
	//
	// EnableAttributes creates the normal overlay empty, and every AppendTriangle routes through
	// TDynamicMeshOverlay::InitializeNewTriangle, which writes InvalidID into all three of that
	// triangle's normal slots. So on a mesh built purely from AppendVertex/AppendTriangle - which is
	// every mesh this plugin generates - the overlay has zero elements. QuickRecomputeOverlayNormals
	// only RE-computes elements that already exist: it sizes its accumulator from MaxElementID and
	// skips any triangle whose first element is InvalidID, so it touches nothing and returns true.
	//
	// The cost is total and invisible. FMeshRenderBufferSetConversionUtil falls back to
	// FDynamicMesh3::GetVertexNormal when a triangle has no normal elements, and that returns the
	// constant FVector3f::UnitY() when per-vertex normals are not enabled - which they never are.
	// Every wall, floor, shutter, drawer and handle then shades as though it faced +Y. Nothing in the
	// gate can see it: volume, bounds, watertightness, roles and UVs are all exactly right, and an
	// unlit or wireframe capture is pixel-identical to correct output.
	//
	// Split by opening angle rather than by polygroup, and the choice is deliberate both ways:
	//
	//   - by polygroup would smooth a box, whose six faces all share one surface role, into a blob,
	//     and would smooth straight across a cornice's springing arris, where the arc and the front
	//     face are both ShutterLaminate;
	//   - per-triangle everywhere would facet the rail tube, the knob dome and the cove arc, which
	//     exist precisely to read as curved.
	//
	// At 40 degrees the kit's real geometry lands the right way round: box arrises (90) and chamfer
	// facets (45) stay hard, while a 12-facet rail tube (30), a 16-sided knob (22.5) and a cove at
	// its default 8 segments (11.25) all weld smooth. See .claude/rules/04-conventions.md, which asks
	// for hard and soft edges chosen deliberately rather than left to a blanket recompute.
	FMeshNormals::InitializeOverlayTopologyFromOpeningAngle(&Mesh, Normals, HardEdgeAngleDegrees);
	FMeshNormals::QuickRecomputeOverlayNormals(Mesh);
}

TArray<TArray<FVector2D>> FHFMeshOps::InsetPolygon(const TArray<FVector2D>& Polygon, double Amount)
{
	TArray<TArray<FVector2D>> Out;
	if (Polygon.Num() < 3 || Amount <= 0.0)
	{
		return Out;
	}

	TArray<FVector2d> Points;
	Points.Reserve(Polygon.Num());
	for (const FVector2D& Point : Polygon)
	{
		Points.Add(FVector2d(Point.X, Point.Y));
	}

	// Clipper expects a consistent winding; normalise to counter-clockwise so a negative offset
	// reliably means "inward" rather than depending on how the boundary was authored.
	FPolygon2d Outer(Points);
	if (Outer.SignedArea() < 0.0)
	{
		Outer.Reverse();
	}

	TArray<FGeneralPolygon2d> Input;
	Input.Add(FGeneralPolygon2d(Outer));

	TArray<FGeneralPolygon2d> Offsetted;
	// Miter joins keep the square corners a room actually has; rounding them would put a fillet on
	// every wall junction.
	if (!PolygonsOffset(-Amount, Input, Offsetted, /*bCopyInputOnFailure*/ false, /*MiterLimit*/ 2.0,
		EPolygonOffsetJoinType::Miter, EPolygonOffsetEndType::Polygon))
	{
		return Out;
	}

	for (const FGeneralPolygon2d& Result : Offsetted)
	{
		const TArray<FVector2d>& Vertices = Result.GetOuter().GetVertices();
		if (Vertices.Num() < 3)
		{
			continue;
		}

		TArray<FVector2D>& Loop = Out.AddDefaulted_GetRef();
		Loop.Reserve(Vertices.Num());
		for (const FVector2d& Vertex : Vertices)
		{
			Loop.Add(FVector2D(Vertex.X, Vertex.Y));
		}
	}

	return Out;
}

bool FHFMeshOps::IsClosed(const FDynamicMesh3& Mesh)
{
	if (Mesh.TriangleCount() == 0)
	{
		return false;
	}

	for (const int32 Eid : Mesh.EdgeIndicesItr())
	{
		if (Mesh.IsBoundaryEdge(Eid))
		{
			return false;
		}
	}
	return true;
}
