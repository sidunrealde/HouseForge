// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFMeshOps.h"

#include "HouseForge.h"

#include "CompGeom/PolygonTriangulation.h"
#include "ConstrainedDelaunay2.h"
#include "Curve/GeneralPolygon2.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "Curve/PolygonOffsetUtils.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "Operations/MeshBevel.h"
#include "Operations/MeshBoolean.h"
#include "Parameterization/DynamicMeshUVEditor.h"
#include "Parameterization/MeshUVPacking.h"

using namespace UE::Geometry;

namespace
{
	/**
	 * Turns a triangulated cap the right way up, as a whole.
	 *
	 * THE TWO TRIANGULATORS THIS FILE USES DO NOT AGREE, and both prism builders cap their solids
	 * with the same formula on the assumption that they do. PolygonTriangulation::TriangulateSimplePolygon
	 * returns triangles wound like its input; FConstrainedDelaunay2d returns them wound the other
	 * way. So every prism with a hole in it came out with BOTH caps inverted - the top facing down
	 * into the solid, the bottom facing up out of it.
	 *
	 * Nothing in the suite could see it, and AppendPrism's own comment records why: GetVolumeArea
	 * integrates along X alone, so a Z-extruded prism's caps contribute exactly zero to the volume
	 * it reports. Bounds, watertightness, role tagging and triangle count all come out exactly
	 * right. In a room it is not subtle at all - a peripheral ceiling band showed its soffit only
	 * from ABOVE, so looking up from underneath you saw straight through the band into the plenum,
	 * and the band's top face fought the slab soffit it was pressed against.
	 *
	 * ## Why the whole set, and never triangle by triangle
	 *
	 * Rewinding each triangle to counter-clockwise looks equivalent and is not. Given a CONCAVE
	 * boundary, TriangulateSimplePolygon returns a set that DELIBERATELY CONTAINS BOTH ORIENTATIONS:
	 * the reversed triangles cover the concave bite and cancel the forward ones that overran it, so
	 * the set sums to the true area. Forcing them all one way fills the bite in - an L-shaped room's
	 * floor grows to its bounding rectangle, which HouseForge.Geometry.ConcavePrism measures.
	 *
	 * Judging the set by its TOTAL signed area and flipping all of it or none preserves those
	 * relative windings exactly, and needs no promise from either library about which way round it
	 * likes to work. A triangulator swapped in later cannot turn the ceilings inside out again.
	 */
	void OrientCapCounterClockwise(TArray<FIndex3i>& Triangles, const TArray<FVector2d>& Positions)
	{
		double Signed = 0.0;

		for (const FIndex3i& Tri : Triangles)
		{
			if (!Positions.IsValidIndex(Tri.A) || !Positions.IsValidIndex(Tri.B) || !Positions.IsValidIndex(Tri.C))
			{
				return;
			}

			const FVector2d& A = Positions[Tri.A];
			const FVector2d& B = Positions[Tri.B];
			const FVector2d& C = Positions[Tri.C];
			Signed += (B.X - A.X) * (C.Y - A.Y) - (C.X - A.X) * (B.Y - A.Y);
		}

		if (Signed >= 0.0)
		{
			return;
		}

		for (FIndex3i& Tri : Triangles)
		{
			Swap(Tri.B, Tri.C);
		}
	}

	/**
	 * Whether a closed 2D loop turns the same way at every vertex.
	 *
	 * The test for whether a cap may be fanned from its own centre rather than ear-clipped - see the
	 * Cap lambda in AppendLoft, where the difference decides how the flat top of every cushion in the
	 * flat is shaded. Collinear runs are tolerated, because a rounded rectangle's straight sides are
	 * exactly that; only a genuine reversal disqualifies the loop.
	 */
	bool IsConvexLoop(const TArray<FVector2d>& Loop)
	{
		const int32 Count = Loop.Num();
		if (Count < 3)
		{
			return false;
		}

		// Scaled to the loop so the tolerance means the same thing on a 5 cm knob and a 2 m worktop.
		double Extent = 0.0;
		for (int32 i = 0; i < Count; ++i)
		{
			Extent = FMath::Max(Extent, (Loop[i] - Loop[0]).Length());
		}
		const double Tolerance = FMath::Max(Extent * Extent * 1e-9, UE_DOUBLE_KINDA_SMALL_NUMBER);

		int32 Sign = 0;
		for (int32 i = 0; i < Count; ++i)
		{
			const FVector2d& A = Loop[i];
			const FVector2d& B = Loop[(i + 1) % Count];
			const FVector2d& C = Loop[(i + 2) % Count];

			const double Cross = (B.X - A.X) * (C.Y - B.Y) - (B.Y - A.Y) * (C.X - B.X);
			if (FMath::Abs(Cross) <= Tolerance)
			{
				continue;
			}

			const int32 ThisSign = Cross > 0.0 ? 1 : -1;
			if (Sign == 0)
			{
				Sign = ThisSign;
			}
			else if (Sign != ThisSign)
			{
				return false;
			}
		}

		return Sign != 0;
	}

	/**
	 * Which of the six axis directions a triangle is projected along.
	 *
	 * SIGNED, and that is what makes the projection safe to weld along. The plane a triangle
	 * projects onto follows only the axis - +Z and -Z both give (X, Y) - so an unsigned answer
	 * would let the two faces of a thin fin share UV elements, which is fine for the UV VALUES
	 * (identical either way, since they are a function of position) and wrong for everything
	 * downstream that reads the resulting connectivity as an island. A fin welded into one island
	 * is an island folded back on top of itself, and a lightmap packed from it overlaps.
	 *
	 * The comparison order matches what the per-triangle projection has always used, so nothing
	 * lands on a different plane than it did before.
	 */
	int32 SignedProjectionAxis(const FVector3d& Normal)
	{
		const double AbsX = FMath::Abs(Normal.X);
		const double AbsY = FMath::Abs(Normal.Y);
		const double AbsZ = FMath::Abs(Normal.Z);

		if (AbsZ >= AbsX && AbsZ >= AbsY)	{ return Normal.Z >= 0.0 ? 2 : 5; }
		if (AbsX >= AbsY)					{ return Normal.X >= 0.0 ? 0 : 3; }
		return Normal.Y >= 0.0 ? 1 : 4;
	}

	/** World position over texel size, on the plane that axis projects onto. */
	FVector2f ProjectOntoAxisPlane(const FVector3d& P, int32 SignedAxis, double InvTexel)
	{
		switch (SignedAxis % 3)
		{
		case 2:		return FVector2f(static_cast<float>(P.X * InvTexel), static_cast<float>(P.Y * InvTexel));
		case 0:		return FVector2f(static_cast<float>(P.Y * InvTexel), static_cast<float>(P.Z * InvTexel));
		default:	return FVector2f(static_cast<float>(P.X * InvTexel), static_cast<float>(P.Z * InvTexel));
		}
	}

	/**
	 * Fills a UV overlay with the world-scale planar projection, welded per vertex and axis.
	 *
	 * WELDED, where this used to append three fresh elements for every triangle corner. The values
	 * are identical either way - a projection is a pure function of position and axis - so nothing
	 * about the world-scale relationship the material panel depends on changes. What changes is the
	 * connectivity, and two things read it:
	 *
	 *   - TANGENTS. UDynamicMeshComponent computes them with EDynamicMeshComponentTangentsMode::
	 *     AutoCalculated, from the normal overlay AND the primary UV overlay. A UV overlay with no
	 *     shared elements anywhere splits the tangent frame at every single triangle edge, so a
	 *     surface that was carefully welded smooth by ComputeShadingNormals still gets a
	 *     discontinuous tangent basis - invisible against today's flat colours and a faceted seam on
	 *     every triangle of every curved surface the moment a normal map goes on. That is exactly
	 *     the failure AHFElementActor's constructor comment predicted for tangents mode, one layer
	 *     further down.
	 *   - ISLANDS. The lightmap unwrap packs UV islands, and islands are connected components of
	 *     this overlay. Fully split elements mean one island per triangle, which packs every
	 *     triangle of the flat as its own chart with its own gutter.
	 */
	void FillWorldScaleProjection(FDynamicMesh3& Mesh, FDynamicMeshUVOverlay& UVs, double TexelSizeCm)
	{
		UVs.ClearElements();

		const double InvTexel = 1.0 / TexelSizeCm;

		// Keyed by vertex and signed axis, so a face's interior edges weld and its arrises do not.
		TMap<TPair<int32, int32>, int32> ElementForVertexAndAxis;
		ElementForVertexAndAxis.Reserve(Mesh.TriangleCount() * 2);

		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			const FIndex3i Tri = Mesh.GetTriangle(Tid);
			const int32 Axis = SignedProjectionAxis(Mesh.GetTriNormal(Tid));

			int32 Elements[3];
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const TPair<int32, int32> Key(Tri[Corner], Axis);
				if (const int32* Existing = ElementForVertexAndAxis.Find(Key))
				{
					Elements[Corner] = *Existing;
					continue;
				}

				const int32 New = UVs.AppendElement(
					ProjectOntoAxisPlane(Mesh.GetVertex(Tri[Corner]), Axis, InvTexel));
				ElementForVertexAndAxis.Add(Key, New);
				Elements[Corner] = New;
			}

			UVs.SetTriangle(Tid, FIndex3i(Elements[0], Elements[1], Elements[2]));
		}
	}

	/**
	 * True if the two triangles either side of an edge fold AWAY from the solid.
	 *
	 * Face normals cannot answer this on their own: an arris and an internal corner of the same
	 * angle give the same dot product, and FMeshNormals' own opening-angle predicate only dots the
	 * normals - which is right for deciding whether to weld a normal and wrong for deciding whether
	 * to cut material away. So the far vertex of the second triangle is tested against the first
	 * triangle's plane. Behind it means the pair closes over the solid, which is a convex arris; in
	 * front of it means they open into a corner, and a real internal corner is FILLED, not chamfered.
	 */
	bool IsConvexEdge(const FDynamicMesh3& Mesh, int32 Eid)
	{
		const FIndex2i EdgeTris = Mesh.GetEdgeT(Eid);
		if (EdgeTris.B == FDynamicMesh3::InvalidID)
		{
			return false;
		}

		const FIndex2i EdgeVerts = Mesh.GetEdgeV(Eid);
		const FIndex3i Far = Mesh.GetTriangle(EdgeTris.B);

		int32 FarVertex = FDynamicMesh3::InvalidID;
		for (int32 i = 0; i < 3; ++i)
		{
			if (Far[i] != EdgeVerts.A && Far[i] != EdgeVerts.B)
			{
				FarVertex = Far[i];
				break;
			}
		}
		if (FarVertex == FDynamicMesh3::InvalidID)
		{
			return false;
		}

		const FVector3d Normal = Mesh.GetTriNormal(EdgeTris.A);
		return Normal.Dot(Mesh.GetVertex(FarVertex) - Mesh.GetVertex(EdgeVerts.A)) < -UE_KINDA_SMALL_NUMBER;
	}

	/**
	 * How wide the faces either side of an edge are, measured across it.
	 *
	 * The triangle's altitude over the candidate edge. For the two-triangle faces AppendBox and
	 * AppendPrism emit that is exactly the face's other dimension, which is the number that decides
	 * whether a chamfer fits on it - a 3 mm shadow gap between two shutters answers 0.3, and
	 * chamfering both its arrises at 1.5 mm would consume it and weld the shutters into one slab.
	 */
	double NarrowestFaceAcross(const FDynamicMesh3& Mesh, int32 Eid)
	{
		const FIndex2i EdgeVerts = Mesh.GetEdgeV(Eid);
		const FVector3d A = Mesh.GetVertex(EdgeVerts.A);
		const FVector3d B = Mesh.GetVertex(EdgeVerts.B);

		FVector3d Along = B - A;
		if (!Along.Normalize())
		{
			return 0.0;
		}

		double Narrowest = TNumericLimits<double>::Max();

		const FIndex2i EdgeTris = Mesh.GetEdgeT(Eid);
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const int32 Tid = EdgeTris[Side];
			if (Tid == FDynamicMesh3::InvalidID)
			{
				continue;
			}

			const FIndex3i Tri = Mesh.GetTriangle(Tid);
			for (int32 i = 0; i < 3; ++i)
			{
				if (Tri[i] == EdgeVerts.A || Tri[i] == EdgeVerts.B)
				{
					continue;
				}

				const FVector3d ToFar = Mesh.GetVertex(Tri[i]) - A;
				Narrowest = FMath::Min(Narrowest, (ToFar - Along * ToFar.Dot(Along)).Length());
			}
		}

		return Narrowest == TNumericLimits<double>::Max() ? 0.0 : Narrowest;
	}

	/**
	 * Puts a surface role back on every triangle the bevel invented.
	 *
	 * FMeshBevel allocates a fresh polygroup for each strip and each junction polygon, so on a mesh
	 * where the group IS the role - which is every mesh here - the chamfers come out tagged with
	 * nothing the material panel can reach. The role floods in from the original faces each new
	 * triangle touches, largest neighbouring area winning, and ties broken by the lower role index
	 * so the answer does not depend on triangle iteration order.
	 *
	 * WHICH TRIANGLES ARE NEW IS TAKEN FROM THE BEVEL, never inferred from the group ids, and the
	 * difference is not academic. AllocateTriangleGroup hands back MaxGroupID and steps it, and on a
	 * mesh built entirely from one role - a wall, a slab, a beam - MaxGroupID is 2. So the groups the
	 * bevel invents are 2, 3, 4: perfectly valid role ids meaning FloorFinish, CeilingSoffit,
	 * CoveInterior. A wall's chamfers would come back tagged as floor and ceiling, every check for
	 * "is this a role" would pass, and the material panel would paint the arrises of every wall in
	 * the flat with a different finish from the wall.
	 *
	 * @return false if any new triangle was left untagged, which means the flood could not reach it.
	 */
	bool RetagNewTrianglesWithRoles(FDynamicMesh3& Mesh, const TArray<int32>& NewTriangles)
	{
		TSet<int32> Untagged;
		Untagged.Reserve(NewTriangles.Num());
		for (const int32 Tid : NewTriangles)
		{
			if (Mesh.IsTriangle(Tid))
			{
				Untagged.Add(Tid);
			}
		}

		while (!Untagged.IsEmpty())
		{
			TSet<int32> StillUntagged;
			TArray<TPair<int32, int32>> Assignments;

			for (const int32 Tid : Untagged)
			{
				// Area per neighbouring role, so a chamfer belongs to the face it mostly came off.
				TMap<int32, double> AreaByGroup;
				const FIndex3i Neighbours = Mesh.GetTriNeighbourTris(Tid);
				for (int32 i = 0; i < 3; ++i)
				{
					const int32 Neighbour = Neighbours[i];
					if (Neighbour == FDynamicMesh3::InvalidID || Untagged.Contains(Neighbour))
					{
						continue;
					}

					AreaByGroup.FindOrAdd(Mesh.GetTriangleGroup(Neighbour)) += Mesh.GetTriArea(Neighbour);
				}

				if (AreaByGroup.IsEmpty())
				{
					StillUntagged.Add(Tid);
					continue;
				}

				int32 Best = INT32_MAX;
				double BestArea = -1.0;
				for (const TPair<int32, double>& Candidate : AreaByGroup)
				{
					if (Candidate.Value > BestArea + UE_KINDA_SMALL_NUMBER
						|| (Candidate.Value > BestArea - UE_KINDA_SMALL_NUMBER && Candidate.Key < Best))
					{
						Best = Candidate.Key;
						BestArea = Candidate.Value;
					}
				}

				Assignments.Emplace(Tid, Best);
			}

			if (Assignments.IsEmpty())
			{
				// Nothing reachable this round and nothing will be reachable next round either.
				return false;
			}

			// Applied after the whole round rather than during it, so one new triangle can never
			// take its role from another new triangle that was assigned earlier in the same sweep.
			for (const TPair<int32, int32>& Assignment : Assignments)
			{
				Mesh.SetTriangleGroup(Assignment.Key, Assignment.Value);
			}

			Untagged = MoveTemp(StillUntagged);
		}

		return true;
	}
}

EHFSurfaceRole FHFMeshOps::RoleForGroup(int32 GroupId)
{
	const int32 Index = GroupId - 1;
	// THE LAST ENUMERATOR, whichever that is, and not a role named by hand. Naming one is how the
	// bound came to be Structure, and the moment a role was added after it every triangle and every
	// slot beyond Structure fell back to WallPaint - a surface silently rendering the wrong material,
	// which is the one failure this table exists to prevent.
	const int32 Max = NumSurfaceRoles() - 1;
	return (Index >= 0 && Index <= Max) ? static_cast<EHFSurfaceRole>(Index) : EHFSurfaceRole::WallPaint;
}

EHFSurfaceRole FHFMeshOps::RoleForMaterialId(int32 MaterialId)
{
	// THE LAST ENUMERATOR, whichever that is, and not a role named by hand. Naming one is how the
	// bound came to be Structure, and the moment a role was added after it every triangle and every
	// slot beyond Structure fell back to WallPaint - a surface silently rendering the wrong material,
	// which is the one failure this table exists to prevent.
	const int32 Max = NumSurfaceRoles() - 1;
	return (MaterialId >= 0 && MaterialId <= Max) ? static_cast<EHFSurfaceRole>(MaterialId) : EHFSurfaceRole::WallPaint;
}

void FHFMeshOps::InitialiseMesh(FDynamicMesh3& Mesh)
{
	Mesh.Clear();
	Mesh.EnableTriangleGroups();
	Mesh.EnableAttributes();
}

void FHFMeshOps::AssignMaterialIdsFromRoles(FDynamicMesh3& Mesh)
{
	if (!Mesh.HasAttributes())
	{
		Mesh.EnableAttributes();
	}

	// Reparent before touching an overlay - see AdoptAttributes. A mesh that has been carried in a
	// TArray has an attribute set pointing at a freed buffer, and EnableMaterialID sizes the new
	// attribute from ParentMesh->MaxTriangleID().
	AdoptAttributes(Mesh);

	FDynamicMeshAttributeSet* Attributes = Mesh.Attributes();
	if (Attributes == nullptr)
	{
		return;
	}

	if (!Attributes->HasMaterialID())
	{
		Attributes->EnableMaterialID();
	}

	FDynamicMeshMaterialAttribute* MaterialIds = Attributes->GetMaterialID();
	if (MaterialIds == nullptr)
	{
		return;
	}

	// Triangle groups are read, never written. If a mesh somehow arrived without them every
	// triangle falls to WallPaint, which is the same fallback RoleForGroup already applies.
	for (const int32 Tid : Mesh.TriangleIndicesItr())
	{
		const int32 Group = Mesh.HasTriangleGroups() ? Mesh.GetTriangleGroup(Tid) : 0;
		MaterialIds->SetValue(Tid, MaterialIdForRole(RoleForGroup(Group)));
	}
}

TSet<EHFSurfaceRole> FHFMeshOps::RolesPresent(const FDynamicMesh3& Mesh)
{
	TSet<EHFSurfaceRole> Roles;
	if (!Mesh.HasTriangleGroups())
	{
		return Roles;
	}

	for (const int32 Tid : Mesh.TriangleIndicesItr())
	{
		Roles.Add(RoleForGroup(Mesh.GetTriangleGroup(Tid)));
	}
	return Roles;
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
	//
	// The cap is turned the right way up rather than trusted, for the reason set out on
	// OrientCapCounterClockwise. It changes nothing here - this triangulator already agrees - and it
	// is what makes the identical formula in AppendPrismWithHoles correct rather than lucky.
	OrientCapCounterClockwise(Triangles, Flat);

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

	// Turned the right way up, not trusted. FConstrainedDelaunay2d hands back triangles wound
	// OPPOSITE to the ones AppendPrism's triangulator returns, and this identical formula therefore
	// built every holed prism in the plugin inside out. See OrientCapCounterClockwise.
	OrientCapCounterClockwise(Triangulator.Triangles, Triangulator.Vertices);

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

TArray<FVector2D> FHFMeshOps::RoundedRectangle(const FVector2D& Centre, const FVector2D& HalfExtents,
	double CornerRadius, int32 CornerSteps)
{
	const int32 Steps = FMath::Max(CornerSteps, 0);
	const FVector2D Half(FMath::Max(HalfExtents.X, 0.0), FMath::Max(HalfExtents.Y, 0.0));
	const double R = FMath::Clamp(CornerRadius, 0.0, FMath::Min(Half.X, Half.Y));

	TArray<FVector2D> Out;
	Out.Reserve(4 * (Steps + 1));

	// Corner centres, counter-clockwise from the front-left. At a zero radius all four collapse onto
	// the rectangle's own corners and every step of the arc lands on the same point - which is a
	// degenerate edge and NOT a degenerate face, so a triangulator drops it and a loft skinning a
	// square ring to a rounded one keeps its point correspondence. That is the whole reason the count
	// is fixed rather than reduced when there is no radius to draw.
	const FVector2D Corners[4] = {
		Centre + FVector2D(-(Half.X - R), -(Half.Y - R)),
		Centre + FVector2D(Half.X - R, -(Half.Y - R)),
		Centre + FVector2D(Half.X - R, Half.Y - R),
		Centre + FVector2D(-(Half.X - R), Half.Y - R)
	};

	// ------------------------------------------------------------------ WHICH QUADRANT EACH ARC IS IN
	//
	// The front-left corner's arc runs from pointing LEFT to pointing DOWN - 180 degrees to 270 - and
	// each corner after it is a quarter turn on. Started a quarter turn earlier, as this was, every
	// arc is drawn in the quadrant belonging to the NEXT corner: the front-left one sweeps from below
	// the corner centre across to its right, into the middle of the shape.
	//
	// At a small radius that is a shallow notch at each corner and reads as a slightly odd rounding.
	// At the radius a piece of sanitaryware needs - four fifths of the half-extent, because a cast
	// bowl is nearly an ellipse - the four notches meet in the middle and the outline becomes a
	// FOUR-LOBED CLOVER. Every WC pan, every basin and every one of the sink's bowls was one.
	//
	// Nothing measured it: a clover spans exactly the same bounding box as the rounded rectangle it
	// should have been, it is closed, its volume is plausible, and its corner radius is right where it
	// is drawn at all. It took rendering the bathroom and looking at the seat.
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		const double Start = PI + Corner * HALF_PI;

		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const double Angle = Steps > 0
				? Start + HALF_PI * static_cast<double>(Step) / static_cast<double>(Steps)
				: Start;
			Out.Add(Corners[Corner] + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * R);
		}
	}

	return Out;
}

bool FHFMeshOps::AppendLoft(FDynamicMesh3& Mesh, const TArray<TArray<FVector2D>>& Sections,
	const TArray<double>& SectionZ, bool bCapBottom, bool bCapTop, EHFSurfaceRole Role)
{
	if (Sections.Num() < 2 || Sections.Num() != SectionZ.Num())
	{
		UE_LOG(LogHouseForge, Warning,
			TEXT("Loft not built: %d sections against %d heights; no geometry emitted."),
			Sections.Num(), SectionZ.Num());
		return false;
	}

	const int32 Ring = Sections[0].Num();
	if (Ring < 3)
	{
		return false;
	}

	for (int32 Index = 0; Index < Sections.Num(); ++Index)
	{
		if (Sections[Index].Num() != Ring)
		{
			// Refused rather than resampled. A caller whose rings disagree has not asked for a loft
			// with a bad section, it has asked for two different outlines and has no correspondence in
			// mind - and any correspondence invented here would be wrong in a way that still produces a
			// closed solid with a plausible silhouette.
			UE_LOG(LogHouseForge, Warning,
				TEXT("Loft not built: section %d has %d points against the first section's %d."),
				Index, Sections[Index].Num(), Ring);
			return false;
		}

		if (Index > 0 && SectionZ[Index] < SectionZ[Index - 1] - UE_KINDA_SMALL_NUMBER)
		{
			UE_LOG(LogHouseForge, Warning,
				TEXT("Loft not built: section %d is below section %d."), Index, Index - 1);
			return false;
		}
	}

	// ONE reversal decision, applied to every ring. Normalising each section on its own area would
	// flip only the ones authored the other way, and a loft between a counter-clockwise ring and a
	// clockwise one is skinned into a twisted band - closed, positive in volume, and correct in
	// silhouette from whichever side the twist is not on.
	TArray<TArray<FVector2D>> Rings = Sections;
	if (SignedArea(Rings[0]) < 0.0)
	{
		for (TArray<FVector2D>& Loop : Rings)
		{
			Algo::Reverse(Loop);
		}
	}

	const int32 Group = GroupForRole(Role);

	TArray<TArray<int32>> Verts;
	Verts.Reserve(Rings.Num());

	for (int32 Index = 0; Index < Rings.Num(); ++Index)
	{
		TArray<int32> Row;
		Row.Reserve(Ring);
		for (const FVector2D& Point : Rings[Index])
		{
			Row.Add(Mesh.AppendVertex(FVector3d(Point.X, Point.Y, SectionZ[Index])));
		}
		Verts.Add(MoveTemp(Row));
	}

	// The skin, wound exactly as AppendPrism winds its sides so that a lofted solid and an extruded
	// one face the same way and can be joined or subtracted from each other.
	for (int32 Level = 0; Level + 1 < Rings.Num(); ++Level)
	{
		const TArray<int32>& Lower = Verts[Level];
		const TArray<int32>& Upper = Verts[Level + 1];

		for (int32 i = 0; i < Ring; ++i)
		{
			const int32 Next = (i + 1) % Ring;
			Mesh.AppendTriangle(Lower[i], Upper[Next], Lower[Next], Group);
			Mesh.AppendTriangle(Lower[i], Upper[i], Upper[Next], Group);
		}
	}

	auto Cap = [&Mesh, Group](const TArray<FVector2D>& Loop, const TArray<int32>& Row, double CapZ,
		bool bFacingDown)
	{
		TArray<FVector2d> Flat;
		Flat.Reserve(Loop.Num());
		for (const FVector2D& Point : Loop)
		{
			Flat.Add(FVector2d(Point.X, Point.Y));
		}

		// A CONVEX CAP IS FANNED FROM ITS OWN CENTRE, AND THAT IS A SHADING DECISION, NOT A TIDINESS
		// ONE. It cost a fortnight of the sofa reading as damaged furniture.
		//
		// Shading normals are area-weighted per vertex, so what a boundary vertex's normal comes out as
		// depends on how much CAP area happens to touch it against how much ROLL area does. Ear
		// clipping distributes that area wildly unevenly round a ring: along the straight sides it
		// hands a vertex most of a large ear, and at a tight corner - where a rounded rectangle packs
		// CornerSteps + 1 points into a few millimetres of arc - it hands it nothing but slivers. So
		// the cap of a cushion shaded flat along its edges and swung outward at its four corners, and
		// the ear triangulation radiating out of each corner painted that discontinuity as a dark
		// arrowhead with a hard crease down both sides. Every cushion, both sofa arms, both mattresses,
		// all four chair seats. Read from four metres as a tear in the upholstery.
		//
		// A centroid fan gives every boundary vertex the same share of the cap - its own edge length
		// times half the cap's span - which is enormous against a roll band a few millimetres wide, so
		// the whole ring resolves to the cap's own normal and the flat face shades as a flat face. The
		// roll below it is untouched and still welds smooth.
		//
		// Convex only. Ear clipping stays for everything else: a fan across a concave ring puts
		// triangles outside the polygon, which is a hole in the solid rather than a mark on it.
		if (IsConvexLoop(Flat))
		{
			FVector2d Centre(0.0, 0.0);
			for (const FVector2d& Point : Flat)
			{
				Centre += Point;
			}
			Centre /= static_cast<double>(Flat.Num());

			const int32 Hub = Mesh.AppendVertex(FVector3d(Centre.X, Centre.Y, CapZ));
			const int32 Count = Row.Num();

			// The fan is put through the SAME orientation step the ear-clipped path uses rather than
			// wound by hand, and that is not caution. Emitted by eye it came out inverted: half the
			// caps in the flat faced into their own solids, which reads as a 173-degree crease on the
			// bevel check, as sixty centimetres of concavity, and as fourteen surfaces in the flat
			// suddenly sharing a plane with the floor. Deriving the winding from the loop's own signed
			// area means a ring authored either way round lands the same way up.
			TArray<FIndex3i> Triangles;
			Triangles.Reserve(Count);

			const int32 HubIndex = Flat.Add(Centre);
			for (int32 i = 0; i < Count; ++i)
			{
				Triangles.Add(FIndex3i(HubIndex, i, (i + 1) % Count));
			}

			OrientCapCounterClockwise(Triangles, Flat);

			auto VertexFor = [&Row, Hub, Count](int32 Index) { return Index == Count ? Hub : Row[Index]; };

			for (const FIndex3i& Tri : Triangles)
			{
				if (bFacingDown)
				{
					Mesh.AppendTriangle(VertexFor(Tri.A), VertexFor(Tri.B), VertexFor(Tri.C), Group);
				}
				else
				{
					Mesh.AppendTriangle(VertexFor(Tri.C), VertexFor(Tri.B), VertexFor(Tri.A), Group);
				}
			}
			return true;
		}

		TArray<FIndex3i> Triangles;
		PolygonTriangulation::TriangulateSimplePolygon(Flat, Triangles, /*bOrientAsHoleFill*/ false);
		if (Triangles.IsEmpty())
		{
			return false;
		}

		OrientCapCounterClockwise(Triangles, Flat);

		for (const FIndex3i& Tri : Triangles)
		{
			if (bFacingDown)
			{
				Mesh.AppendTriangle(Row[Tri.A], Row[Tri.B], Row[Tri.C], Group);
			}
			else
			{
				Mesh.AppendTriangle(Row[Tri.C], Row[Tri.B], Row[Tri.A], Group);
			}
		}
		return true;
	};

	if (bCapBottom)
	{
		Cap(Rings[0], Verts[0], SectionZ[0], /*bFacingDown*/ true);
	}
	if (bCapTop)
	{
		Cap(Rings.Last(), Verts.Last(), SectionZ.Last(), /*bFacingDown*/ false);
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

bool FHFMeshOps::AppendSoftBox(FDynamicMesh3& Mesh, const FVector3d& Min, const FVector3d& Max,
	const FHFSoftBoxParams& Params, EHFSurfaceRole Role)
{
	const FVector3d Size = Max - Min;
	if (Size.X <= UE_KINDA_SMALL_NUMBER || Size.Y <= UE_KINDA_SMALL_NUMBER || Size.Z <= UE_KINDA_SMALL_NUMBER)
	{
		// Nothing, rather than a sliver. A zero-thickness cushion is a pair of coincident faces that
		// carries through every volume and closedness measurement taken afterwards.
		return false;
	}

	const int32 CornerSteps = FMath::Clamp(Params.CornerSteps, 0, 16);
	const int32 RollSteps = FMath::Clamp(Params.RollSteps, 1, 12);

	// THE LEAN COSTS DEPTH RATHER THAN ADDING IT. A ring is SlabY deep wherever it sits, and the box's
	// declared depth is the envelope the whole lean sweeps - so a raked cushion stays inside the
	// footprint it was drawn in instead of growing out of the back of the sofa.
	const double Rake = FMath::Clamp(Params.RakeY, 0.0, Size.Y * 0.9);
	const double SlabY = Size.Y - Rake;

	// The two rolls share the height, and neither may eat more than half the plan section it is
	// turning round - past that the "roll" has consumed the face it was rolling off and the solid
	// pinches to a ridge.
	double TopR = FMath::Max(Params.TopRadius, 0.0);
	double BottomR = FMath::Max(Params.BottomRadius, 0.0);

	if (TopR + BottomR > Size.Z)
	{
		const double Scale = Size.Z / (TopR + BottomR);
		TopR *= Scale;
		BottomR *= Scale;
	}

	const double MaxInset = FMath::Min(Size.X, SlabY) * 0.45;
	TopR = FMath::Min(TopR, MaxInset);
	BottomR = FMath::Min(BottomR, MaxInset);

	// THE CORNER ARC CENTRE MUST NOT MOVE BETWEEN RINGS, and everything below turns on why.
	//
	// RoundedRectangle puts its corner arc centres at HalfExtents - Radius. A ring drawn in by Inset
	// has HalfExtents = Size/2 - Inset, so as long as its Radius is CornerRadius - Inset the centre
	// sits at Size/2 - CornerRadius no matter which ring it is: the four vertical arc centres are one
	// fixed line, every ring is the same inner box offset outward by one distance, and the corner is
	// the sphere octant a fillet actually is.
	//
	// The moment Radius is floored at anything other than CornerRadius - Inset, that centre becomes
	// Size/2 - Inset - Floor and TRAVELS INWARD as the roll turns. At the top ring, where Inset is the
	// whole of TopRadius, the centre has moved in by TopRadius - Floor diagonally and the ring's
	// corner is pulled INSIDE the surface below it. The skin folds back on itself: a re-entrant
	// faceted wedge with a hard crease down each side of it, on all four corners of every cushion and
	// both ends of both arms of the sofa. Six millimetres of fold on a 70 mm roll, plainly visible
	// from four metres, and invisible to every measurement in the suite - the solid is still closed,
	// still the right volume, and still exactly inside its declared box.
	//
	// The floor existed for a real reason: at CornerRadius == TopRadius the top ring's true radius is
	// zero, RoundedRectangle emits that corner point CornerSteps + 1 times over, and the band above it
	// is a fan of zero-area triangles whose absent normals get averaged into the real ones. The answer
	// is not to move the centre, it is to KEEP THE RADIUS OFF ZERO IN THE FIRST PLACE: hold the plan
	// radius strictly above both rolls, so CornerRadius - Inset is always positive and the centre
	// never has to be compromised. A cushion whose top face has 4 mm of round on its plan corners is
	// what a cushion has anyway.
	double CornerR = FMath::Clamp(Params.CornerRadius, 0.0, FMath::Min(Size.X, SlabY) * 0.5);
	if (CornerR > 0.0)
	{
		// Enough to keep four distinct points in the tightest arc, and never so much that it eats the
		// section: a quarter of what is left over the roll, capped at a tenth of the half-section.
		const double MinPlan = FMath::Min(FMath::Max(CornerR * 0.15, 0.2), FMath::Min(Size.X, SlabY) * 0.05);
		CornerR = FMath::Min(FMath::Max(CornerR, FMath::Max(TopR, BottomR) + MinPlan),
			FMath::Min(Size.X, SlabY) * 0.5);
	}

	// One ring per level, bottom-up: height and how far that level is drawn in from the declared box.
	TArray<double> RingZ;
	TArray<double> RingInset;

	auto PushRing = [&RingZ, &RingInset](double Z, double Inset)
	{
		if (RingZ.Num() > 0 && Z <= RingZ.Last() + UE_KINDA_SMALL_NUMBER)
		{
			// Two rings at one height would loft into a zero-height band of degenerate triangles.
			// Merged rather than dropped, and to the LARGER inset: a roll of zero radius produces
			// RollSteps identical rings, and the level that survives has to be the drawn-in one or
			// the roll would step back out at its own start.
			RingInset.Last() = FMath::Max(RingInset.Last(), Inset);
			return;
		}
		RingZ.Add(Z);
		RingInset.Add(Inset);
	};

	// THE ROLL IS STEPPED FINELY AT ITS TWO ENDS AND COARSELY IN THE MIDDLE, and that is not a
	// refinement - it is what stops the flat top of a cushion reading as a chevron.
	//
	// A roll spaced evenly in angle meets the flat cap above it at a real angle. At five steps the
	// topmost band lies 9 degrees off horizontal, which is far below the 40 that ComputeShadingNormals
	// splits at, so the cap and the band weld: every vertex on the cap's boundary gets a normal tilted
	// outward by half that, while the cap has no interior vertices of its own to hold the true
	// vertical. A flat polygon shaded entirely from tilted boundary normals renders its own ear
	// triangulation - a dark arrowhead across each corner of every cushion, every mattress and both
	// sofa arms, with a hard crease down each side of it. It is the same picture as a geometric fold
	// and it was read as one.
	//
	// Smoothstep in the parameter makes the discretisation tangent-matched at BOTH ends: horizontal
	// where it meets the cap, vertical where it meets the side. The top band drops to under 5 degrees
	// and the shading runs continuously off the flat face into the roll, which is what the surface
	// actually does. The middle bands take up the slack at about 27 degrees - still comfortably
	// welded, and on the part of the roll that is turning fastest anyway, where it is invisible.
	//
	// Cheaper and more honest than the alternative of splitting the seam hard: a rounded box IS
	// tangent-continuous there, so a clean crease would be a line that is not on the object.
	auto RollTheta = [RollSteps](int32 Step) -> double
	{
		const double U = static_cast<double>(Step) / RollSteps;
		return UE_DOUBLE_HALF_PI * (U * U * (3.0 - 2.0 * U));
	};

	for (int32 Step = 0; Step <= RollSteps; ++Step)
	{
		const double Theta = RollTheta(Step);
		PushRing(Min.Z + BottomR * (1.0 - FMath::Cos(Theta)), BottomR * (1.0 - FMath::Sin(Theta)));
	}

	for (int32 Step = RollSteps; Step >= 0; --Step)
	{
		const double Theta = RollTheta(Step);
		PushRing(Max.Z - TopR * (1.0 - FMath::Cos(Theta)), TopR * (1.0 - FMath::Sin(Theta)));
	}

	if (RingZ.Num() < 2)
	{
		return false;
	}

	TArray<TArray<FVector2D>> Sections;
	Sections.Reserve(RingZ.Num());

	const double MidX = (Min.X + Max.X) * 0.5;

	for (int32 Index = 0; Index < RingZ.Num(); ++Index)
	{
		const double U = FMath::Clamp((RingZ[Index] - Min.Z) / Size.Z, 0.0, 1.0);
		const double Inset = RingInset[Index];

		const FVector2D Centre(MidX, Min.Y + Rake * U + SlabY * 0.5);
		const FVector2D Half(
			FMath::Max(Size.X * 0.5 - Inset, UE_KINDA_SMALL_NUMBER),
			FMath::Max(SlabY * 0.5 - Inset, UE_KINDA_SMALL_NUMBER));

		// THE PLAN RADIUS CLOSES AS THE ROLL TURNS, and getting this wrong is visible from across the
		// room. Held constant while the ring drew in, the corner of a cushion came out as a flat
		// lozenge where the vertical radius and the plan radius failed to meet - a facet with its own
		// highlight, on all four corners of every cushion and both ends of both arms, in a kit whose
		// entire purpose is that nothing on it reads as machined.
		//
		// Taking the inset off the radius makes the surface the offset of an inner box by one distance
		// in every direction, which is what a fillet actually is: the corner becomes a sphere octant
		// and the three radii meet in one continuous surface. It also means a plan radius smaller than
		// the roll cannot blend, so the kits above keep CornerRadius at or above both rolls.
		//
		// NOT FLOORED, AND THAT IS THE WHOLE POINT - see the CornerR note above. CornerR has already
		// been held above both rolls, so this stays positive on every ring and the arc centre it
		// implies, HalfExtents - Radius, is the same fixed line for all of them.
		const double Radius = FMath::Clamp(CornerR - Inset, 0.0,
			FMath::Max(FMath::Min(Half.X, Half.Y), 0.0));

		Sections.Add(RoundedRectangle(Centre, Half, Radius, CornerSteps));
	}

	return AppendLoft(Mesh, Sections, RingZ, /*bCapBottom*/ true, /*bCapTop*/ true, Role);
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
		// Unwelded edges are reported alongside, because they say WHICH failure this is: a result
		// with triangles and open seams is a cut that worked and did not close, which is a
		// tolerance problem, not a geometry one.
		UE_LOG(LogHouseForge, Warning,
			TEXT("Mesh subtraction produced unusable geometry (computed=%d, result tris=%d, closed=%d, unwelded edges=%d); target left uncut."),
			bComputed ? 1 : 0, Result.TriangleCount(), IsClosed(Result) ? 1 : 0,
			Boolean.CreatedBoundaryEdges.Num());
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
	// Project each triangle along its dominant axis rather than using the UV editor's box
	// projection: that one atlases the six faces into a cube-cross layout, so UVs no longer
	// correspond to world distance. Here UV is simply world position over texel size, which means
	// one tile really is TexelSizeCm across - the property the material panel needs in order to
	// express tiling in millimetres.
	FillWorldScaleProjection(Mesh, *UVs, TexelSizeCm);

	ComputeShadingNormals(Mesh);
}

bool FHFMeshOps::BevelConvexEdges(FDynamicMesh3& Mesh, const FHFBevelParams& Params,
	const TArray<FHFStructuralCut>& FlushVolumes)
{
	if (!Params.bEnabled || Mesh.TriangleCount() == 0 || !Mesh.HasTriangleGroups())
	{
		return false;
	}

	AdoptAttributes(Mesh);

	// WHERE THIS ELEMENT'S MATERIAL DIES INTO SOMETHING ELSE'S, there is no arris to chamfer. See
	// the header. A tolerance rather than an exact surface test: the boundary is a lap of a
	// millimetre or two by construction, and an edge that close to a neighbour's masonry is one no
	// camera can see round.
	constexpr double FlushToleranceCm = 0.2;

	auto InsideAnyFlushVolume = [&FlushVolumes](const FVector3d& Point)
	{
		for (const FHFStructuralCut& Volume : FlushVolumes)
		{
			if (!Volume.IsValid())
			{
				continue;
			}

			// Into the volume's own frame, so a cut turned in plan is tested as the box it is
			// rather than as its axis-aligned bounds - which for a 450 x 230 column at 90 degrees
			// would reach 110 mm further into the wall than the concrete does.
			const FVector Local = FRotator(0.0, -Volume.YawDegrees, 0.0)
				.RotateVector(FVector(Point) - Volume.Centre);

			if (FMath::Abs(Local.X) <= Volume.Extents.X + FlushToleranceCm
				&& FMath::Abs(Local.Y) <= Volume.Extents.Y + FlushToleranceCm
				&& FMath::Abs(Local.Z) <= Volume.Extents.Z + FlushToleranceCm)
			{
				return true;
			}
		}
		return false;
	};

	// One pass per distinct chamfer width the roles present actually ask for, widest first. An edge
	// between two roles takes the SMALLER of the two widths, so a chamfer never eats past what the
	// tighter of the two materials would accept, and an edge touching a role that must stay sharp -
	// fabric, or anything the user has zeroed - is not a candidate at all.
	TArray<double> Widths;
	for (const EHFSurfaceRole Role : RolesPresent(Mesh))
	{
		const double Width = Params.WidthFor(Role);
		if (Width > UE_KINDA_SMALL_NUMBER && !Widths.ContainsByPredicate(
			[Width](double Existing) { return FMath::IsNearlyEqual(Existing, Width, 1e-6); }))
		{
			Widths.Add(Width);
		}
	}
	if (Widths.IsEmpty())
	{
		return false;
	}
	Widths.Sort([](double A, double B) { return A > B; });

	const double CosThreshold = FMath::Cos(FMath::DegreesToRadians(
		FMath::Clamp(Params.MinAngleDegrees, 1.0, 179.0)));

	// Triangles earlier passes created, so a later pass cannot chamfer a chamfer. A fresh facet
	// meets its parent faces at half the original angle - 45 degrees off a box arris - which is
	// above any sensible threshold, so without this the second pass would bevel the first pass's
	// work and the third would bevel the second's.
	TSet<int32> BevelCreated;
	bool bAnyApplied = false;

	for (const double Width : Widths)
	{
		const double MinFace = Width * FMath::Max(Params.MinFeatureFactor, 1.0);

		TArray<int32> Candidates;
		for (const int32 Eid : Mesh.EdgeIndicesItr())
		{
			const FIndex2i EdgeTris = Mesh.GetEdgeT(Eid);
			if (EdgeTris.B == FDynamicMesh3::InvalidID)
			{
				continue;
			}
			if (BevelCreated.Contains(EdgeTris.A) || BevelCreated.Contains(EdgeTris.B))
			{
				continue;
			}

			const double WidthA = Params.WidthFor(RoleForGroup(Mesh.GetTriangleGroup(EdgeTris.A)));
			const double WidthB = Params.WidthFor(RoleForGroup(Mesh.GetTriangleGroup(EdgeTris.B)));
			if (!FMath::IsNearlyEqual(FMath::Min(WidthA, WidthB), Width, 1e-6))
			{
				continue;
			}

			if (Mesh.GetTriNormal(EdgeTris.A).Dot(Mesh.GetTriNormal(EdgeTris.B)) > CosThreshold)
			{
				continue;
			}
			if (!IsConvexEdge(Mesh, Eid))
			{
				continue;
			}

			const FIndex2i EdgeVerts = Mesh.GetEdgeV(Eid);
			const FVector3d VertexA = Mesh.GetVertex(EdgeVerts.A);
			const FVector3d VertexB = Mesh.GetVertex(EdgeVerts.B);

			if (FVector3d::Distance(VertexA, VertexB) < MinFace)
			{
				continue;
			}

			// Both ends, so a long arris that merely passes through a neighbour's footprint - a
			// wall's own corner running past a column further along it - keeps its chamfer.
			if (!FlushVolumes.IsEmpty()
				&& InsideAnyFlushVolume(VertexA) && InsideAnyFlushVolume(VertexB))
			{
				continue;
			}
			if (NarrowestFaceAcross(Mesh, Eid) < MinFace)
			{
				continue;
			}

			Candidates.Add(Eid);
		}

		if (Candidates.IsEmpty())
		{
			continue;
		}

		// A vertex where exactly two chamfered edges meet is a corner only if they turn there.
		// Straight through - a long arris that happens to be split by a vertex, which every boolean
		// leaves behind - has to stay one span, or the bevel terminates and restarts mid-edge and
		// leaves a notch in the middle of a chamfer.
		TMap<int32, TArray<FVector3d>> DirectionsAtVertex;
		for (const int32 Eid : Candidates)
		{
			const FIndex2i EdgeVerts = Mesh.GetEdgeV(Eid);
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const FVector3d From = Mesh.GetVertex(EdgeVerts[Side]);
				FVector3d Direction = Mesh.GetVertex(EdgeVerts[1 - Side]) - From;
				if (Direction.Normalize())
				{
					DirectionsAtVertex.FindOrAdd(EdgeVerts[Side]).Add(Direction);
				}
			}
		}

		auto IsCornerVertex = [&DirectionsAtVertex](int32 Vid)
		{
			const TArray<FVector3d>* Directions = DirectionsAtVertex.Find(Vid);
			if (Directions == nullptr || Directions->Num() != 2)
			{
				return false;
			}
			// Opposite directions mean the span runs straight through. Anything else is a turn.
			return (*Directions)[0].Dot((*Directions)[1]) > -0.94;
		};

		// Worked on a copy. FMeshBevel unlinks the mesh before it re-stitches it, so a failure
		// part-way leaves geometry that is neither the input nor a bevel of it, and a half-beveled
		// wall is worse than a sharp one because it looks plausible in every screenshot.
		FDynamicMesh3 Beveled(Mesh);
		AdoptAttributes(Beveled);

		FMeshBevel Bevel;
		Bevel.InsetDistance = Width;
		Bevel.NumSubdivisions = 0;
		Bevel.InitializeFromTriangleEdges(Beveled, Candidates, IsCornerVertex);

		const bool bWasClosed = IsClosed(Mesh);
		const TSet<EHFSurfaceRole> RolesBefore = RolesPresent(Mesh);

		if (!Bevel.Apply(Beveled, nullptr))
		{
			UE_LOG(LogHouseForge, Warning,
				TEXT("Bevel of %d edge(s) at %.3f cm failed; those arrises are left sharp."),
				Candidates.Num(), Width);
			continue;
		}

		if (!RetagNewTrianglesWithRoles(Beveled, Bevel.NewTriangles)
			|| !RolesPresent(Beveled).Includes(RolesBefore)
			|| RolesPresent(Beveled).Num() != RolesBefore.Num()
			|| (bWasClosed && !IsClosed(Beveled)))
		{
			// Roles lost or the solid opened up. Both are invisible in a capture and fatal later -
			// an untagged chamfer can never be re-materialled and an open solid defeats every
			// boolean and every collision cook it meets - so the sharp mesh stands.
			UE_LOG(LogHouseForge, Warning,
				TEXT("Bevel of %d edge(s) at %.3f cm produced geometry that lost a surface role or opened the solid; those arrises are left sharp."),
				Candidates.Num(), Width);
			continue;
		}

		for (const int32 Tid : Bevel.NewTriangles)
		{
			BevelCreated.Add(Tid);
		}

		Mesh = MoveTemp(Beveled);
		AdoptAttributes(Mesh);
		bAnyApplied = true;
	}

	return bAnyApplied;
}

bool FHFMeshOps::BuildLightmapUVs(FDynamicMesh3& Mesh, const FHFLightmapParams& Params)
{
	if (!Params.bEnabled || Mesh.TriangleCount() == 0)
	{
		return false;
	}

	if (!Mesh.HasAttributes())
	{
		Mesh.EnableAttributes();
	}
	AdoptAttributes(Mesh);

	FDynamicMeshAttributeSet* Attributes = Mesh.Attributes();
	if (Attributes == nullptr)
	{
		return false;
	}

	if (Attributes->NumUVLayers() < 2)
	{
		Attributes->SetNumUVLayers(2);
	}

	FDynamicMeshUVOverlay* Lightmap = Attributes->GetUVLayer(1);
	if (Lightmap == nullptr)
	{
		return false;
	}

	// Seeded with the same world-scale projection UV0 uses, at a texel size of 1 cm. The absolute
	// scale is irrelevant - the packer normalises it - but the RELATIVE scale between islands is
	// not, and starting from a world-scale unwrap is what makes a 3 m wall arrive at the packer
	// with proportionally more area than a door handle rather than each being fitted to its own
	// slot. Islands fall out of the welding: one per connected same-facing planar region.
	FillWorldScaleProjection(Mesh, *Lightmap, 1.0);

	FDynamicMeshUVPacker Packer(Lightmap);
	Packer.TextureResolution = FMath::Max(Params.TextureResolution, 16);
	Packer.GutterSize = static_cast<float>(FMath::Max(Params.GutterPixels, 1));
	Packer.bAllowFlips = false;

	if (!Packer.StandardPack())
	{
		// Leaving a projected-but-unpacked second layer behind would be the worst outcome: it looks
		// like a lightmap unwrap, and every island sits on top of every other one.
		Lightmap->ClearElements();
		UE_LOG(LogHouseForge, Warning,
			TEXT("Lightmap UV packing failed for a %d-triangle mesh; no second UV channel was written."),
			Mesh.TriangleCount());
		return false;
	}

	// THE PACKER DOES NOT GUARANTEE THE UNIT SQUARE, and on the reference flat it overruns it by up
	// to 4.6 on a fifth of the meshes - the ones carrying many small islands. A lightmap UV outside
	// 0..1 is not a cosmetic problem: the bake allocates one texture per mesh and addresses it with
	// these coordinates, so anything past the edge wraps back onto another island and lights it with
	// somebody else's bounce.
	//
	// Fixed by one UNIFORM scale and translate over the whole sheet, never per island. That preserves
	// every relative island size and every gap between them exactly as the packer laid them out; the
	// only cost is that the gutter ends up the same fraction smaller, which is why GutterPixels is
	// asked for at the target resolution rather than assumed to survive. Refitting islands
	// individually would be the wrong fix - it would destroy the consistent texel density that
	// starting from a world-scale projection exists to produce.
	FAxisAlignedBox2f Packed = FAxisAlignedBox2f::Empty();
	for (const int32 Eid : Lightmap->ElementIndicesItr())
	{
		Packed.Contain(Lightmap->GetElement(Eid));
	}

	const float Border = 0.5f / static_cast<float>(Packer.TextureResolution);
	const float Span = FMath::Max(Packed.Width(), Packed.Height());
	if (Span > UE_KINDA_SMALL_NUMBER
		&& (Packed.Min.X < 0.0f || Packed.Min.Y < 0.0f || Packed.Max.X > 1.0f || Packed.Max.Y > 1.0f))
	{
		const float Scale = (1.0f - 2.0f * Border) / Span;
		for (const int32 Eid : Lightmap->ElementIndicesItr())
		{
			const FVector2f UV = Lightmap->GetElement(Eid);
			Lightmap->SetElement(Eid, FVector2f(
				Border + (UV.X - Packed.Min.X) * Scale,
				Border + (UV.Y - Packed.Min.Y) * Scale));
		}
	}

	return true;
}

void FHFMeshOps::FinishForRender(FDynamicMesh3& Mesh, const FHFRenderFinish& Finish,
	const TArray<FHFStructuralCut>& FlushVolumes)
{
	if (Mesh.TriangleCount() == 0)
	{
		return;
	}

	BevelConvexEdges(Mesh, Finish.Bevel, FlushVolumes);

	// Re-projected after the bevel rather than trusted from the generator. The chamfer facets are
	// new triangles with no UV elements and no normal elements of their own, and a triangle with no
	// normal elements shades off FDynamicMesh3's constant (0, 1, 0) - so an unfinished chamfer is a
	// band of geometry lit as though it faced +Y, on every arris in the flat.
	ApplyWorldScaleUVs(Mesh, Finish.TexelSizeCm);

	BuildLightmapUVs(Mesh, Finish.Lightmap);
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

namespace
{
	/** A closed loop as Clipper wants it: counter-clockwise, so "inside" is unambiguous. */
	bool ToGeneralPolygon(const TArray<FVector2D>& Loop, FGeneralPolygon2d& Out)
	{
		if (Loop.Num() < 3)
		{
			return false;
		}

		TArray<FVector2d> Points;
		Points.Reserve(Loop.Num());
		for (const FVector2D& Point : Loop)
		{
			Points.Add(FVector2d(Point.X, Point.Y));
		}

		FPolygon2d Outer(Points);
		if (Outer.SignedArea() < 0.0)
		{
			Outer.Reverse();
		}

		Out = FGeneralPolygon2d(Outer);
		return true;
	}

	TArray<TArray<FVector2D>> FromGeneralPolygons(const TArray<FGeneralPolygon2d>& In)
	{
		TArray<TArray<FVector2D>> Out;
		for (const FGeneralPolygon2d& Polygon : In)
		{
			const TArray<FVector2d>& Vertices = Polygon.GetOuter().GetVertices();
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

	/** Both booleans differ only in which one they call, so they share everything else. */
	TArray<TArray<FVector2D>> PolygonBoolean(const TArray<FVector2D>& Subject,
		const TArray<TArray<FVector2D>>& Others, bool bSubtract)
	{
		FGeneralPolygon2d SubjectPolygon;
		if (!ToGeneralPolygon(Subject, SubjectPolygon))
		{
			return {};
		}

		TArray<FGeneralPolygon2d> OtherPolygons;
		for (const TArray<FVector2D>& Loop : Others)
		{
			FGeneralPolygon2d Polygon;
			if (ToGeneralPolygon(Loop, Polygon))
			{
				OtherPolygons.Add(MoveTemp(Polygon));
			}
		}

		if (OtherPolygons.IsEmpty())
		{
			// Nothing to cut with. Subtracting nothing leaves the subject; intersecting with
			// nothing leaves nothing, and both are the honest answers rather than failures.
			TArray<TArray<FVector2D>> Untouched;
			if (bSubtract)
			{
				Untouched.Add(Subject);
			}
			return Untouched;
		}

		const TArray<FGeneralPolygon2d> SubjectArray = { SubjectPolygon };

		TArray<FGeneralPolygon2d> Result;
		const bool bOk = bSubtract
			? PolygonsDifference(SubjectArray, OtherPolygons, Result)
			: PolygonsIntersection(SubjectArray, OtherPolygons, Result);

		if (!bOk)
		{
			return {};
		}

		return FromGeneralPolygons(Result);
	}
}

TArray<TArray<FVector2D>> FHFMeshOps::SubtractPolygons(const TArray<FVector2D>& Subject,
	const TArray<TArray<FVector2D>>& Cutters)
{
	return PolygonBoolean(Subject, Cutters, /*bSubtract*/ true);
}

TArray<TArray<FVector2D>> FHFMeshOps::IntersectPolygons(const TArray<FVector2D>& Subject,
	const TArray<TArray<FVector2D>>& Clips)
{
	return PolygonBoolean(Subject, Clips, /*bSubtract*/ false);
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
