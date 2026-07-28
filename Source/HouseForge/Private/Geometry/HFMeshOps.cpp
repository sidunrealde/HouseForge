// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFMeshOps.h"

#include "CompGeom/PolygonTriangulation.h"
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
	TArray<FIndex3i> Triangles;
	PolygonTriangulation::TriangulateSimplePolygon(Flat, Triangles);
	if (Triangles.IsEmpty())
	{
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

bool FHFMeshOps::SubtractInPlace(FDynamicMesh3& Target, const FDynamicMesh3& Tool)
{
	if (Tool.TriangleCount() == 0 || Target.TriangleCount() == 0)
	{
		return false;
	}

	FDynamicMesh3 Result;
	Result.EnableTriangleGroups();
	Result.EnableAttributes();

	FMeshBoolean Boolean(&Target, FTransformSRT3d::Identity(),
		&Tool, FTransformSRT3d::Identity(),
		&Result, FMeshBoolean::EBooleanOp::Difference);
	Boolean.bPutResultInInputSpace = true;

	if (!Boolean.Compute() || Result.TriangleCount() == 0)
	{
		// Leaving the target uncut is the safer failure: a half-subtracted wall still looks
		// plausible in a screenshot, which is exactly how a bad cut would go unnoticed.
		return false;
	}

	Target = MoveTemp(Result);
	return true;
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

	FMeshNormals::QuickRecomputeOverlayNormals(Mesh);
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
