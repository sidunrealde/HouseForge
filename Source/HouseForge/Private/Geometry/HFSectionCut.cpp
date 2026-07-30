// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFSectionCut.h"

#include "Geometry/HFMeshOps.h"
#include "HouseForge.h"

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Operations/MeshPlaneCut.h"

using namespace UE::Geometry;

FDynamicMesh3 FHFSectionCut::CutBelow(const FDynamicMesh3& Source, const FHFSectionCutParams& Params,
	bool* bOutClosed)
{
	if (bOutClosed != nullptr)
	{
		*bOutClosed = false;
	}

	FDynamicMesh3 Mesh(Source);

	// The copy constructor reparents, but say so anyway: this mesh is about to be handed to an
	// operation that reaches through the attribute set on every edge split it performs, and a
	// caller that assembled Source into a TArray has already had its back-pointer relocated out
	// from under it. See FHFMeshOps::AdoptAttributes.
	FHFMeshOps::AdoptAttributes(Mesh);

	if (Mesh.TriangleCount() == 0)
	{
		return Mesh;
	}

	// Nothing to do when the plane misses the mesh entirely. Worth short-circuiting rather than
	// letting the cut no-op: a cut above everything still runs the whole edge-split and
	// boundary-loop machinery, and a house is a few hundred of these.
	const FAxisAlignedBox3d Bounds = Mesh.GetBounds();
	if (Params.CutZ >= Bounds.Max.Z)
	{
		if (bOutClosed != nullptr)
		{
			*bOutClosed = FHFMeshOps::IsClosed(Mesh);
		}
		return Mesh;
	}
	if (Params.CutZ <= Bounds.Min.Z)
	{
		// Everything is above the plane. Return empty rather than a mesh of zero-area slivers.
		FDynamicMesh3 Empty;
		FHFMeshOps::InitialiseMesh(Empty);
		return Empty;
	}

	FMeshPlaneCut Cut(&Mesh, FVector3d(0.0, 0.0, Params.CutZ), FVector3d::UnitZ());

	// Left off deliberately. Simplification merges the new cut edges into their neighbours, which
	// on a wall whose doorway boundary runs right up to the cut plane can weld the doorway's edge
	// into the wall face - closing, in the plan, the gap the doorway is there to show.
	Cut.bSimplifyAlongNewEdges = false;

	Cut.UVScaleFactor = (Params.TexelSizeCm > 0.0) ? static_cast<float>(1.0 / Params.TexelSizeCm) : 1.0f;

	if (!Cut.Cut())
	{
		// Cut() does the deletion before it extracts loops, so a failure here means the geometry
		// below the plane is right and only the boundary is unknown. Say so and carry on with an
		// uncapped result rather than throwing away a correct half of a house.
		UE_LOG(LogHouseForge, Warning,
			TEXT("Section cut at z=%.1f could not extract its cut boundary; the result will not be capped."),
			Params.CutZ);
	}
	else if (Params.bCap)
	{
		// One constant group for every cap face, so the whole cut carries the role the caller
		// named. SimpleHoleFill fans each loop from its centroid, which is exact on the planar
		// loops a horizontal cut through prismatic geometry produces.
		Cut.SimpleHoleFill(FHFMeshOps::GroupForRole(Params.CapRole));
	}

	FHFMeshOps::AdoptAttributes(Mesh);

	if (Mesh.TriangleCount() == 0)
	{
		return Mesh;
	}

	// The cap arrives with no shading normals and with hole-fill UVs that are not on the plugin's
	// world scale. Both are re-derived over the whole result rather than patched onto the new
	// faces: the projection is per polygroup, and the cap is a polygroup.
	FHFMeshOps::ApplyWorldScaleUVs(Mesh, Params.TexelSizeCm);
	FHFMeshOps::AssignMaterialIdsFromRoles(Mesh);

	if (bOutClosed != nullptr)
	{
		*bOutClosed = FHFMeshOps::IsClosed(Mesh);
	}

	return Mesh;
}
