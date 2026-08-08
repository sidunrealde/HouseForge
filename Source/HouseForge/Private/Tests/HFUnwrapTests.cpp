// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/MeshTangents.h"
#include "Geometry/HFJoineryKit.h"
#include "Geometry/HFMeshOps.h"
#include "Geometry/HFRenderFinish.h"
#include "Misc/AutomationTest.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The assertions that would have caught what was wrong with UV0, stated as properties.
 *
 * Every one of these passed - or could not even be expressed - against the projection this replaced,
 * which picked a world axis per triangle by dominant normal component. That is the point of the
 * file: the old defects were invisible not because nobody looked, but because the suite measured
 * the implementation rather than the thing the implementation was for.
 *
 * The properties, and what each one is standing guard over:
 *
 *   1. SHARED EDGES SHARE ELEMENTS - by element id, not by coincident value. Overlays decide
 *      connectivity by identity (TDynamicMeshOverlay::IsSeamEdge compares element indices), so two
 *      elements holding identical numbers are still a seam, and a seam still blocks tangent
 *      averaging. This cannot be checked by comparing UV values, ever.
 *   2. YAW DOES NOT STRETCH - the defect stated as a number.
 *   3. A CHAMFER KEEPS ITS PARENTS' FRAME - no arbitrary third plane down every arris.
 *   4. TANGENTS AGREE ACROSS A SMOOTH EDGE - the failure the whole rewrite exists to prevent,
 *      computed the same way UDynamicMeshComponent computes it.
 *   5. NOTHING IS MIRRORED - the unreported defect, and the one a normal map shows first.
 */
namespace HouseForgeUnwrap
{
	FDynamicMesh3 MakeBox(const FVector3d& Extents, double YawDegrees = 0.0,
		EHFSurfaceRole Role = EHFSurfaceRole::WallPaint)
	{
		FDynamicMesh3 Mesh;
		FHFMeshOps::InitialiseMesh(Mesh);
		FHFMeshOps::AppendBox(Mesh, FVector3d::Zero(), Extents, YawDegrees, Role);
		return Mesh;
	}

	/** A 16-sided barrel: 22.5 degrees a facet, so every seam of it is meant to weld smooth. */
	FDynamicMesh3 MakeBarrel()
	{
		FDynamicMesh3 Mesh;
		FHFMeshOps::InitialiseMesh(Mesh);
		FHFMeshOps::AppendRevolvedProfile(Mesh, { FVector2D(0.0, 1.5), FVector2D(4.0, 1.5) },
			FVector3d::Zero(), FVector3d::UnitZ(), 16, EHFSurfaceRole::MetalHardware);
		return Mesh;
	}

	const FDynamicMeshUVOverlay* UVsOf(const FDynamicMesh3& Mesh)
	{
		return Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryUV() : nullptr;
	}

	/** Worst disagreement between a UV edge and the world edge it came from, in centimetres. */
	double WorstScaleErrorCm(const FDynamicMesh3& Mesh, double TexelSizeCm)
	{
		const FDynamicMeshUVOverlay* UVs = UVsOf(Mesh);
		if (UVs == nullptr)
		{
			return TNumericLimits<double>::Max();
		}

		double Worst = 0.0;
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (!UVs->IsSetTriangle(Tid))
			{
				return TNumericLimits<double>::Max();
			}

			FVector3d P[3];
			Mesh.GetTriVertices(Tid, P[0], P[1], P[2]);

			FVector2f UV[3];
			UVs->GetTriElements(Tid, UV[0], UV[1], UV[2]);

			for (int32 i = 0; i < 3; ++i)
			{
				const int32 j = (i + 1) % 3;
				const double World = (P[j] - P[i]).Length();
				const double InUV = (FVector2d(UV[j].X, UV[j].Y) - FVector2d(UV[i].X, UV[i].Y)).Length();
				Worst = FMath::Max(Worst, FMath::Abs(InUV * TexelSizeCm - World));
			}
		}
		return Worst;
	}

	/** Dihedral angle across an interior edge, in degrees; negative if the edge is on a boundary. */
	double DihedralDegrees(const FDynamicMesh3& Mesh, int32 Eid)
	{
		const FIndex2i Tris = Mesh.GetEdgeT(Eid);
		if (Tris.B == FDynamicMesh3::InvalidID)
		{
			return -1.0;
		}
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			Mesh.GetTriNormal(Tris.A).Dot(Mesh.GetTriNormal(Tris.B)), -1.0, 1.0)));
	}
}

/**
 * Two triangles either side of a smooth edge use the SAME UV element, not two identical ones.
 *
 * The distinction is the whole thing. FDynamicMeshUVOverlay decides whether two triangles are
 * connected by comparing the element INDICES they use at the shared vertices, so a pair of distinct
 * elements holding byte-identical FVector2f values is still a seam - and MikkT, which keys its
 * per-vertex accumulation on (UV element, normal element, orientation), still refuses to average
 * across it. A test written against UV values cannot see any of that, which is how a fully split
 * overlay survived a suite with a world-scale UV test in it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFUnwrapSharesElementsTest,
	"HouseForge.Unwrap.SmoothEdgesShareUVElements", HF_TEST_FLAGS)

bool FHFUnwrapSharesElementsTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeUnwrap;

	auto CheckMesh = [this](const TCHAR* What, FDynamicMesh3& Mesh)
	{
		FHFMeshOps::ApplyWorldScaleUVs(Mesh, 100.0);

		const FDynamicMeshUVOverlay* UVs = UVsOf(Mesh);
		const FDynamicMeshNormalOverlay* Normals =
			Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryNormals() : nullptr;
		if (UVs == nullptr || Normals == nullptr)
		{
			AddError(FString::Printf(TEXT("%s has no overlays to check."), What));
			return;
		}

		int32 Smooth = 0;
		int32 SmoothAndWelded = 0;
		int32 UVSeamsAtSmoothNormals = 0;

		for (const int32 Eid : Mesh.EdgeIndicesItr())
		{
			const double Angle = DihedralDegrees(Mesh, Eid);
			if (Angle < 0.0)
			{
				continue;
			}

			const FIndex2i Tris = Mesh.GetEdgeT(Eid);
			const bool bUVWelded = UVs->AreTrianglesConnected(Tris.A, Tris.B);
			const bool bNormalWelded = Normals->AreTrianglesConnected(Tris.A, Tris.B);

			if (Angle < FHFMeshOps::DefaultHardEdgeAngleDegrees)
			{
				++Smooth;
				SmoothAndWelded += bUVWelded ? 1 : 0;
			}

			// THE INVARIANT. A UV seam where the normals are welded smooth is a tangent crease on a
			// surface that was deliberately made continuous, and it shades faceted under a normal map.
			// Allowed only where a chart closes on itself - see below - which is topology, not a choice.
			if (bNormalWelded && !bUVWelded)
			{
				++UVSeamsAtSmoothNormals;
			}
		}

		if (!TestTrue(FString::Printf(TEXT("%s has smooth interior edges to check"), What), Smooth > 0))
		{
			return;
		}

		// Every smooth edge but the cut a closed loop forces. A barrel is a cylinder: it cannot be
		// laid flat without opening it somewhere, and that somewhere is one column of its facets.
		const int32 Allowed = FMath::Max(2, Smooth / 8);
		TestTrue(*FString::Printf(
			TEXT("%s welds UV0 across %d of its %d smooth edges"), What, SmoothAndWelded, Smooth),
			SmoothAndWelded >= Smooth - Allowed);
		TestTrue(*FString::Printf(
			TEXT("%s leaves at most the topological cut as a UV seam inside welded normals (%d)"),
			What, UVSeamsAtSmoothNormals),
			UVSeamsAtSmoothNormals <= Allowed);
	};

	// A box: the only smooth edges are the six face diagonals, and they must weld.
	FDynamicMesh3 Box = MakeBox(FVector3d(50.0, 30.0, 20.0));
	CheckMesh(TEXT("A box"), Box);

	// A barrel: 32 facet seams that ComputeShadingNormals welds smooth, and UV0 must not undo it.
	FDynamicMesh3 Barrel = MakeBarrel();
	CheckMesh(TEXT("A revolved barrel"), Barrel);

	// And a chamfered box, where the chamfer facets are the geometry the projection used to break.
	FDynamicMesh3 Chamfered = MakeBox(FVector3d(50.0, 30.0, 20.0));
	FHFRenderFinish Finish;
	FHFMeshOps::FinishForRender(Chamfered, Finish);

	const FDynamicMeshUVOverlay* ChamferedUVs = UVsOf(Chamfered);
	if (TestNotNull(TEXT("The chamfered box has UV0"), ChamferedUVs))
	{
		TestTrue(TEXT("A chamfered box is not one UV element per triangle corner"),
			ChamferedUVs->ElementCount() < Chamfered.TriangleCount() * 3);
	}

	return true;
}

/**
 * A wall yawed 45 degrees carries exactly the texture density an axis-aligned one does.
 *
 * THE STRETCH DEFECT, AS A NUMBER. The projection this replaced put a triangle onto the world plane
 * its dominant normal pointed at, which is an orthographic projection, which foreshortens by the
 * cosine of the angle between the surface and that plane. At 45 degrees a metre of wall measured
 * 0.707 of a UV unit instead of 1.0, so the texture rendered 1.41x too big along the wall's length;
 * at 60 degrees, 2x. Tiling stated in millimetres would have been wrong by that factor and there was
 * nothing in the suite that could say so, because the reference flat has 22 walls and all of them
 * are at 0 or 90 degrees.
 *
 * Swept across the whole quadrant rather than checked at 45, because the failure is a smooth curve
 * and one sample of a curve proves nothing about the rest of it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFUnwrapYawTest,
	"HouseForge.Unwrap.YawDoesNotStretchTheTexture", HF_TEST_FLAGS)

bool FHFUnwrapYawTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeUnwrap;

	const FVector3d WallExtents(150.0, 11.5, 140.0);

	for (const double Yaw : { 0.0, 15.0, 30.0, 45.0, 60.0, 75.0, 90.0 })
	{
		FDynamicMesh3 Wall = MakeBox(WallExtents, Yaw);
		FHFMeshOps::ApplyWorldScaleUVs(Wall, 100.0);

		const double Worst = WorstScaleErrorCm(Wall, 100.0);
		TestTrue(*FString::Printf(
			TEXT("A wall yawed %.0f degrees is unwrapped at exactly world scale (worst %.6f cm)"),
			Yaw, Worst),
			Worst <= 0.001);
	}

	// And the other half of gravity alignment: a vertical world edge runs straight up the texture on
	// any wall at any yaw, so a grain, a grout line or a trowel direction follows the building rather
	// than the world axes. Under the old projection the V axis was whichever world axis the dominant
	// plane happened to leave over.
	FDynamicMesh3 Skewed = MakeBox(WallExtents, 37.0);
	FHFMeshOps::ApplyWorldScaleUVs(Skewed, 100.0);

	const FDynamicMeshUVOverlay* UVs = UVsOf(Skewed);
	if (!TestNotNull(TEXT("The skewed wall has UV0"), UVs))
	{
		return false;
	}

	int32 VerticalEdges = 0;
	double WorstDrift = 0.0;
	for (const int32 Tid : Skewed.TriangleIndicesItr())
	{
		// Only the upright faces have a "up" to align to; a wall's top and bottom caps do not.
		if (FMath::Abs(Skewed.GetTriNormal(Tid).Z) > 0.01)
		{
			continue;
		}

		FVector3d P[3];
		Skewed.GetTriVertices(Tid, P[0], P[1], P[2]);

		FVector2f UV[3];
		UVs->GetTriElements(Tid, UV[0], UV[1], UV[2]);

		for (int32 i = 0; i < 3; ++i)
		{
			const int32 j = (i + 1) % 3;
			const FVector3d Delta = P[j] - P[i];
			if (Delta.Length() < 1.0 || FMath::Abs(Delta.X) > 0.001 || FMath::Abs(Delta.Y) > 0.001)
			{
				continue;
			}

			// Purely vertical in the world, so it must be purely vertical in UV: no U component at
			// all, and a V component of exactly its own length over the texel size.
			++VerticalEdges;
			WorstDrift = FMath::Max(WorstDrift, (double)FMath::Abs(UV[j].X - UV[i].X));
			WorstDrift = FMath::Max(WorstDrift,
				FMath::Abs(FMath::Abs((double)(UV[j].Y - UV[i].Y)) - FMath::Abs(Delta.Z) / 100.0));
		}
	}

	TestTrue(TEXT("The skewed wall has vertical edges to check"), VerticalEdges > 0);
	TestTrue(*FString::Printf(
		TEXT("Vertical stays vertical in UV on a wall yawed 37 degrees (worst drift %.6f)"), WorstDrift),
		WorstDrift <= 1e-5);

	return true;
}

/**
 * A chamfer facet is unwrapped as part of the surface it belongs to, not onto a plane of its own.
 *
 * A chamfer sits at 45 degrees to both faces it bridges. Under a per-triangle dominant-axis
 * projection that is a tie, broken by the comparison order, so the facet took whichever axis won -
 * and on a yawed arris that can be an axis unrelated to either parent, foreshortening the facet by
 * up to 29% and putting a texture discontinuity down an edge whose entire purpose is to catch light
 * smoothly.
 *
 * ## Which surface a chamfer belongs to, which is not the one it looks like it belongs to
 *
 * The obvious answer - "the face it runs along" - is wrong, and the mesh says so. A chamfered box's
 * bevel geometry is a single continuous SKIRT: twelve facet strips stitched at eight corner junction
 * polygons, and a junction sits about 35 degrees from each strip it joins, which is under the
 * hard-edge threshold. So ComputeShadingNormals welds the whole skirt smooth, and the invariant this
 * unwrap is built on - a UV seam is always a normal seam - then REQUIRES the skirt to be unwrapped
 * as one piece. It is unfolded rather than projected, which is why the assertion here is isometry
 * and continuity rather than "the facet shares its parent's plane": the facet shares its parent's
 * SCALE exactly, and it shares its own surface's parameterisation continuously, and those two
 * together are what a chamfer needs in order to stop reading as a break in the material.
 *
 * The cost of that choice is stated honestly: the skirt is a closed band, so laying it flat needs a
 * few cuts, and each cut is a tangent crease across a strip one to two millimetres wide.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFUnwrapChamferTest,
	"HouseForge.Unwrap.ChamferJoinsTheSurfaceItBridges", HF_TEST_FLAGS)

bool FHFUnwrapChamferTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeUnwrap;

	// Yawed deliberately: on an axis-aligned box the old tie-break happened to land on a parent, and
	// the defect only shows where no world axis is a parent's normal.
	FDynamicMesh3 Mesh = MakeBox(FVector3d(60.0, 25.0, 90.0), 31.0, EHFSurfaceRole::ShutterLaminate);

	FHFRenderFinish Finish;
	if (!TestTrue(TEXT("The finish chamfers this box"),
		FHFMeshOps::BevelConvexEdges(Mesh, Finish.Bevel)))
	{
		return false;
	}
	FHFMeshOps::ApplyWorldScaleUVs(Mesh, Finish.TexelSizeCm);

	const FDynamicMeshUVOverlay* UVs = UVsOf(Mesh);
	if (!TestNotNull(TEXT("The chamfered box has UV0"), UVs))
	{
		return false;
	}

	// Every triangle, chamfer facets included, at exactly world scale. The old projection could not
	// have passed this: a facet at 45 degrees to the plane it was projected onto loses 29% of its
	// length in the direction of the tilt.
	const double Worst = WorstScaleErrorCm(Mesh, Finish.TexelSizeCm);
	TestTrue(*FString::Printf(
		TEXT("Every triangle of a chamfered, yawed box is unwrapped at world scale (worst %.6f cm)"), Worst),
		Worst <= 0.001);

	// And the skirt is unwrapped as one piece. Every edge inside it is smooth - that is what makes it
	// one surface - so every one of them must weld, bar the handful of cuts a closed band needs
	// before it can lie flat.
	const FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
	if (!TestNotNull(TEXT("The chamfered box has normals"), Normals))
	{
		return false;
	}

	int32 SmoothInsideSkirt = 0;
	int32 WeldedInsideSkirt = 0;

	for (const int32 Eid : Mesh.EdgeIndicesItr())
	{
		const double Angle = DihedralDegrees(Mesh, Eid);
		if (Angle <= 1.0 || Angle >= FHFMeshOps::DefaultHardEdgeAngleDegrees)
		{
			// Coplanar edges are inside a flat face; anything over the threshold is a real arris.
			// What is left is the skirt's own interior: facet meeting junction, at about 35 degrees.
			continue;
		}

		const FIndex2i Tris = Mesh.GetEdgeT(Eid);
		++SmoothInsideSkirt;
		WeldedInsideSkirt += UVs->AreTrianglesConnected(Tris.A, Tris.B) ? 1 : 0;

		TestTrue(TEXT("The skirt's own interior edges are welded smooth in the normals too"),
			Normals->AreTrianglesConnected(Tris.A, Tris.B));
	}

	if (!TestTrue(TEXT("The chamfered box has a skirt with interior edges"), SmoothInsideSkirt > 0))
	{
		return false;
	}

	// A box's skirt is a sphere with six discs cut out of it, so flattening it needs five cuts. The
	// bound is deliberately loose - the exact number depends on where the unfold started - and the
	// point of it is that the skirt is a CONTINUOUS unwrap with a few cuts, not a collection of
	// separately-projected facets, which is what a bound of "most of them" cannot express.
	TestTrue(*FString::Printf(
		TEXT("The chamfer skirt is unwrapped continuously: %d of %d interior edges welded"),
		WeldedInsideSkirt, SmoothInsideSkirt),
		WeldedInsideSkirt >= SmoothInsideSkirt - 8);

	return true;
}

/**
 * Tangents agree across a smooth edge, computed exactly the way the component computes them.
 *
 * THE FAILURE THE UNWRAP EXISTS TO PREVENT. AHFElementActor sets the component to
 * EDynamicMeshComponentTangentsMode::AutoCalculated, which routes to
 * FMeshTangentsf::ComputeTriVertexTangents(PrimaryNormals, PrimaryUV, {}) with bAveraged true, and
 * that accumulates a vertex's tangent per distinct (UV element, normal element, orientation sign)
 * triple. So a UV split blocks tangent averaging EVEN WHERE THE NORMAL IS PERFECTLY WELDED - the
 * carefully-welded normals of a rail tube, a knob dome and a cove arc get a tangent frame that jumps
 * at every seam, and every normal map put on them shades faceted along it.
 *
 * There is nothing to compute separately for this: fixing the sharing IS the tangent fix. Which is
 * exactly why it has to be measured on the real thing rather than argued from the overlay.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFUnwrapTangentsTest,
	"HouseForge.Unwrap.TangentsAgreeAcrossSmoothEdges", HF_TEST_FLAGS)

bool FHFUnwrapTangentsTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeUnwrap;

	/**
	 * Corners across smooth edges whose two triangles disagree about the tangent, over the total.
	 *
	 * Every smooth edge is counted, including ones UV0 did not weld - which is the point. Skipping
	 * those would make the test unfalsifiable: an unwrap that seamed a curved surface everywhere
	 * would be measured on the nothing it had left.
	 */
	auto CountTangentBreaks = [](FDynamicMesh3& Mesh, int32& OutCompared, double& OutWorstDegrees)
	{
		OutCompared = 0;
		OutWorstDegrees = 0.0;

		const FDynamicMeshUVOverlay* UVs = UVsOf(Mesh);
		const FDynamicMeshNormalOverlay* Normals =
			Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryNormals() : nullptr;
		if (UVs == nullptr || Normals == nullptr)
		{
			return -1;
		}

		FMeshTangentsf Tangents(&Mesh);
		Tangents.ComputeTriVertexTangents(Normals, UVs, FComputeTangentsOptions());

		int32 Breaks = 0;
		for (const int32 Eid : Mesh.EdgeIndicesItr())
		{
			const double Angle = DihedralDegrees(Mesh, Eid);
			if (Angle < 0.0 || Angle >= FHFMeshOps::DefaultHardEdgeAngleDegrees)
			{
				continue;
			}

			const FIndex2i Tris = Mesh.GetEdgeT(Eid);
			const FIndex2i EdgeVerts = Mesh.GetEdgeV(Eid);
			const FIndex3i TriA = Mesh.GetTriangle(Tris.A);
			const FIndex3i TriB = Mesh.GetTriangle(Tris.B);

			for (int32 Which = 0; Which < 2; ++Which)
			{
				const int32 Vid = EdgeVerts[Which];

				int32 CornerA = INDEX_NONE, CornerB = INDEX_NONE;
				for (int32 i = 0; i < 3; ++i)
				{
					if (TriA[i] == Vid) { CornerA = i; }
					if (TriB[i] == Vid) { CornerB = i; }
				}
				if (CornerA == INDEX_NONE || CornerB == INDEX_NONE)
				{
					continue;
				}

				FVector3f TangentA, BitangentA, TangentB, BitangentB;
				Tangents.GetPerTriangleTangent(Tris.A, CornerA, TangentA, BitangentA);
				Tangents.GetPerTriangleTangent(Tris.B, CornerB, TangentB, BitangentB);

				if (TangentA.IsNearlyZero() || TangentB.IsNearlyZero())
				{
					continue;
				}

				++OutCompared;
				const double Between = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
					(double)FVector3f::DotProduct(TangentA.GetSafeNormal(), TangentB.GetSafeNormal()), -1.0, 1.0)));
				OutWorstDegrees = FMath::Max(OutWorstDegrees, Between);
				Breaks += Between > 1.0 ? 1 : 0;
			}
		}
		return Breaks;
	};

	// A COVE CORNICE, first, because its curved surface is an open strip - a quarter-circle swept
	// along a run. An open developable strip needs NO cut to lie flat, so the correct answer here is
	// exactly zero, with nothing to allow for, which is what makes it worth measuring even though the
	// old projection also passed it. It passed for a reason worth writing down: a cove swept along X
	// crosses from the -Y bucket into the -Z bucket halfway round, which IS a UV seam in the middle
	// of a welded normal field - but both of those buckets happen to put U along world X, so the
	// tangent came out the same on either side of it by luck of the sweep direction. Turn the run and
	// the luck goes. The barrel below is where the same defect has nowhere to hide.
	FHFCorniceParams Cornice;
	Cornice.Width = 180.0;
	Cornice.Profile = EHFCorniceProfile::Cove;
	Cornice.ProfileSize = 2.0;
	Cornice.CoveSegments = 8;
	Cornice.EdgeBevel = 0.0;

	FDynamicMesh3 Cove = FHFJoineryKit::GenerateCornice(Cornice);
	if (!TestTrue(TEXT("The cove cornice generated geometry"), Cove.TriangleCount() > 0))
	{
		return false;
	}

	int32 CoveCompared = 0;
	double CoveWorst = 0.0;
	const int32 CoveBreaks = CountTangentBreaks(Cove, CoveCompared, CoveWorst);

	TestTrue(TEXT("The cove arc has smooth edges with tangents on both sides"), CoveCompared > 0);
	TestEqual(*FString::Printf(
		TEXT("A cove arc has one tangent frame throughout: %d of %d corners break (worst %.4f degrees)"),
		CoveBreaks, CoveCompared, CoveWorst),
		CoveBreaks, 0);

	// And a barrel, which is closed and therefore does have to be opened somewhere. One cut, on a
	// 16-sided revolve, is one column of facets - so a handful of corners, not a quarter of them.
	FDynamicMesh3 Barrel = MakeBarrel();
	FHFMeshOps::ApplyWorldScaleUVs(Barrel, 100.0);

	int32 BarrelCompared = 0;
	double BarrelWorst = 0.0;
	const int32 BarrelBreaks = CountTangentBreaks(Barrel, BarrelCompared, BarrelWorst);

	if (!TestTrue(TEXT("The barrel has smooth edges with tangents on both sides"), BarrelCompared > 0))
	{
		return false;
	}

	// Zero, measured, not allowed-for. The cut a cylinder needs turns out to cost no tangent at all:
	// the unrolled strip's far end carries the same U direction as its near end, so the two sides of
	// the cut still agree about the frame even though they are different elements. Against the
	// projection this replaced the same barrel broke 4 of these 128 corners, at the four longitudes
	// where the dominant axis changed and U swung from world X to world Y.
	TestEqual(*FString::Printf(
		TEXT("A barrel has one tangent frame all the way round: %d of %d corners break (worst %.4f degrees)"),
		BarrelBreaks, BarrelCompared, BarrelWorst),
		BarrelBreaks, 0);

	return true;
}

/**
 * Nothing is unwrapped mirrored, on any face of anything.
 *
 * THE DEFECT NOBODY REPORTED, and the one a normal map shows first. The old projection switched on
 * the axis and ignored its sign - +X and -X both landed on (Y, Z), +Y and -Y both on (X, Z) - so
 * seen from outside the surface, +U ran one way on the front face of a panel and the opposite way on
 * its back. Every -X and -Y wall face, every ceiling soffit, the underside of every counter and the
 * back of every shutter came out mirrored relative to its front. Handedness is carried per-vertex in
 * TangentZ.W, so the lighting maths stayed correct and nothing looked broken - but wood grain, tile
 * grout, brushed metal and a plaster trowel direction all run backwards on half the flat, and a
 * mirrored normal map is a well-known tell.
 *
 * Stated without reference to any axis: these meshes are wound outward, and the unwrap frame is
 * right-handed, so EVERY triangle's UV winding must come out the same way round. One negative signed
 * UV area is one mirrored triangle.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFUnwrapMirroringTest,
	"HouseForge.Unwrap.NothingIsMirrored", HF_TEST_FLAGS)

bool FHFUnwrapMirroringTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeUnwrap;

	auto CheckMesh = [this](const TCHAR* What, const FDynamicMesh3& Mesh)
	{
		const FDynamicMeshUVOverlay* UVs = UVsOf(Mesh);
		if (UVs == nullptr)
		{
			AddError(FString::Printf(TEXT("%s has no UV0."), What));
			return;
		}

		int32 Checked = 0;
		int32 Mirrored = 0;

		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (!UVs->IsSetTriangle(Tid) || Mesh.GetTriArea(Tid) < 1e-8)
			{
				continue;
			}

			FVector2f UV[3];
			UVs->GetTriElements(Tid, UV[0], UV[1], UV[2]);

			const double Signed = 0.5 * (double)(
				(UV[1].X - UV[0].X) * (UV[2].Y - UV[0].Y) - (UV[1].Y - UV[0].Y) * (UV[2].X - UV[0].X));

			// A sliver's sign is rounding noise, not a frame.
			if (FMath::Abs(Signed) < 1e-12)
			{
				continue;
			}

			// NEGATIVE ON EVERY OUTWARD TRIANGLE, and the sign is not arbitrary: UE is a left-handed
			// coordinate system, so VectorUtil::Normal - which is what GetTriNormal calls - returns
			// Cross(V2 - V0, V1 - V0), the negation of the right-handed cross. Unwrapped into a
			// right-handed (U, V, N) frame, an outward-wound triangle therefore comes out wound the
			// other way in UV. One triangle of the opposite sign is one triangle unwrapped in a
			// mirrored frame, which is the whole failure being tested for.
			++Checked;
			Mirrored += Signed > 0.0 ? 1 : 0;
		}

		TestTrue(FString::Printf(TEXT("%s has triangles to check"), What), Checked > 0);
		TestEqual(FString::Printf(TEXT("%s unwraps every triangle the same way round"), What),
			Mirrored, 0);
	};

	// A box has all six face directions on it at once, which is the whole point: under the old
	// projection three of them came out mirrored and three did not.
	FDynamicMesh3 Box = MakeBox(FVector3d(50.0, 30.0, 20.0));
	FHFMeshOps::ApplyWorldScaleUVs(Box, 100.0);
	CheckMesh(TEXT("A box"), Box);

	FDynamicMesh3 Barrel = MakeBarrel();
	FHFMeshOps::ApplyWorldScaleUVs(Barrel, 100.0);
	CheckMesh(TEXT("A revolved barrel"), Barrel);

	FDynamicMesh3 Chamfered = MakeBox(FVector3d(50.0, 30.0, 20.0), 23.0);
	FHFRenderFinish Finish;
	FHFMeshOps::FinishForRender(Chamfered, Finish);
	CheckMesh(TEXT("A chamfered, yawed box"), Chamfered);

	return true;
}

/**
 * A curved surface is unwrapped at world scale too, which no plane can do.
 *
 * The old projection bucketed a tube's facets into four world axes and projected each bucket flat,
 * so the texture was magnified by 1/cos of however far round the bucket a facet sat - continuously
 * from 1.0 to 1.41, with four-fold symmetry, snapping back at each of the four axis-change
 * longitudes. Those four longitudes were also UV seams sitting in the middle of a normal field that
 * ComputeShadingNormals had deliberately welded smooth, so they were four tangent creases per curved
 * element as well.
 *
 * Unfolding the chart instead keeps every edge length exactly, so a barrel measures true all the way
 * round and pays for it with the one cut a cylinder topologically requires.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFUnwrapCurvedTest,
	"HouseForge.Unwrap.CurvedSurfacesKeepWorldScale", HF_TEST_FLAGS)

bool FHFUnwrapCurvedTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeUnwrap;

	FDynamicMesh3 Barrel = MakeBarrel();
	FHFMeshOps::ApplyWorldScaleUVs(Barrel, 100.0);

	const double Worst = WorstScaleErrorCm(Barrel, 100.0);
	TestTrue(*FString::Printf(
		TEXT("Every facet of a 16-sided barrel is unwrapped at world scale (worst %.6f cm)"), Worst),
		Worst <= 0.001);

	// And the circumference really is the circumference: unrolled, the barrel's girth in UV must be
	// the arc length its facets add up to, not the width of the box that contains it.
	const FDynamicMeshUVOverlay* UVs = UVsOf(Barrel);
	if (!TestNotNull(TEXT("The barrel has UV0"), UVs))
	{
		return false;
	}

	double Girth = 0.0;
	for (const int32 Eid : Barrel.EdgeIndicesItr())
	{
		const FIndex2i Verts = Barrel.GetEdgeV(Eid);
		const FVector3d A = Barrel.GetVertex(Verts.A);
		const FVector3d B = Barrel.GetVertex(Verts.B);

		// The chords of the bottom ring, and only those: the cap's spokes run from the centre out to
		// the same ring at the same height, so height alone does not identify a chord.
		const bool bOnRing = FMath::Abs(A.Z) < 1e-6 && FMath::Abs(B.Z) < 1e-6
			&& FMath::Abs(FVector2d(A.X, A.Y).Length() - 1.5) < 1e-6
			&& FMath::Abs(FVector2d(B.X, B.Y).Length() - 1.5) < 1e-6;
		if (bOnRing)
		{
			Girth += (B - A).Length();
		}
	}

	// 16 chords of a 1.5 cm radius circle: 2 * 16 * r * sin(pi/16).
	const double Expected = 2.0 * 16.0 * 1.5 * FMath::Sin(UE_DOUBLE_PI / 16.0);
	TestNearlyEqual(TEXT("The barrel's own girth is the chord sum it is built from"), Girth, Expected, 1e-6);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
