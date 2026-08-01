// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "Geometry/HFGenerators.h"
#include "Geometry/HFJoineryKit.h"
#include "Geometry/HFMeshOps.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace HouseForgeRenderFinish
{
	FDynamicMesh3 MakeBox(const FVector3d& Extents = FVector3d(50.0, 30.0, 20.0),
		EHFSurfaceRole Role = EHFSurfaceRole::WallPaint)
	{
		FDynamicMesh3 Mesh;
		FHFMeshOps::InitialiseMesh(Mesh);
		FHFMeshOps::AppendBox(Mesh, FVector3d::Zero(), Extents, 0.0, Role);
		return Mesh;
	}

	/** The sharpest convex arris left in a mesh, in degrees. Zero when everything is smooth. */
	double SharpestConvexArris(const FDynamicMesh3& Mesh)
	{
		double Sharpest = 0.0;

		for (const int32 Eid : Mesh.EdgeIndicesItr())
		{
			const FIndex2i Tris = Mesh.GetEdgeT(Eid);
			if (Tris.B == FDynamicMesh3::InvalidID)
			{
				continue;
			}

			// Convexity the same way FHFMeshOps does it: which side of one triangle's plane the
			// other triangle's far vertex falls on. Dotting the normals cannot distinguish an
			// arris from an internal corner, and an internal corner is meant to stay sharp.
			const FIndex2i EdgeVerts = Mesh.GetEdgeV(Eid);
			const FIndex3i Far = Mesh.GetTriangle(Tris.B);

			int32 FarVertex = FDynamicMesh3::InvalidID;
			for (int32 i = 0; i < 3; ++i)
			{
				if (Far[i] != EdgeVerts.A && Far[i] != EdgeVerts.B)
				{
					FarVertex = Far[i];
				}
			}
			if (FarVertex == FDynamicMesh3::InvalidID)
			{
				continue;
			}

			const FVector3d NormalA = Mesh.GetTriNormal(Tris.A);
			if (NormalA.Dot(Mesh.GetVertex(FarVertex) - Mesh.GetVertex(EdgeVerts.A)) >= 0.0)
			{
				continue;
			}

			const double Angle = FMath::RadiansToDegrees(
				FMath::Acos(FMath::Clamp(NormalA.Dot(Mesh.GetTriNormal(Tris.B)), -1.0, 1.0)));
			Sharpest = FMath::Max(Sharpest, Angle);
		}

		return Sharpest;
	}

	/** True if two 2D triangles share any interior area. */
	bool TrianglesOverlap(const FVector2f A[3], const FVector2f B[3])
	{
		// Separating axis over both triangles' edge normals. Exact for convex shapes, and a
		// triangle is convex.
		auto SeparatedBy = [](const FVector2f P[3], const FVector2f Q[3])
		{
			for (int32 i = 0; i < 3; ++i)
			{
				const FVector2f Edge = P[(i + 1) % 3] - P[i];
				const FVector2f Axis(-Edge.Y, Edge.X);
				if (Axis.IsNearlyZero())
				{
					continue;
				}

				float MinP = TNumericLimits<float>::Max(), MaxP = -TNumericLimits<float>::Max();
				float MinQ = TNumericLimits<float>::Max(), MaxQ = -TNumericLimits<float>::Max();
				for (int32 k = 0; k < 3; ++k)
				{
					const float ProjP = FVector2f::DotProduct(P[k], Axis);
					const float ProjQ = FVector2f::DotProduct(Q[k], Axis);
					MinP = FMath::Min(MinP, ProjP); MaxP = FMath::Max(MaxP, ProjP);
					MinQ = FMath::Min(MinQ, ProjQ); MaxQ = FMath::Max(MaxQ, ProjQ);
				}

				// Touching along a shared border is not overlapping. Islands are packed with a
				// gutter, so a real overlap is an interval genuinely inside the other.
				const float Slack = 1e-5f * FMath::Max(1.0f, Axis.Size());
				if (MinP >= MaxQ - Slack || MinQ >= MaxP - Slack)
				{
					return true;
				}
			}
			return false;
		};

		return !SeparatedBy(A, B) && !SeparatedBy(B, A);
	}

	/** Every pair of UV1 triangles tested for overlap. Exhaustive, so only for small meshes. */
	int32 CountLightmapOverlaps(const FDynamicMesh3& Mesh)
	{
		if (!Mesh.HasAttributes() || Mesh.Attributes()->NumUVLayers() < 2)
		{
			return -1;
		}

		const FDynamicMeshUVOverlay* Lightmap = Mesh.Attributes()->GetUVLayer(1);
		if (Lightmap == nullptr)
		{
			return -1;
		}

		TArray<int32> Tids;
		TArray<TStaticArray<FVector2f, 3>> Triangles;
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (!Lightmap->IsSetTriangle(Tid))
			{
				continue;
			}

			const FIndex3i UVTri = Lightmap->GetTriangle(Tid);
			TStaticArray<FVector2f, 3> Corners;
			for (int32 i = 0; i < 3; ++i)
			{
				Corners[i] = Lightmap->GetElement(UVTri[i]);
			}

			// Degenerate slivers cannot overlap anything and confuse the separating axis.
			const FVector2f E0 = Corners[1] - Corners[0];
			const FVector2f E1 = Corners[2] - Corners[0];
			if (FMath::Abs(E0.X * E1.Y - E0.Y * E1.X) < 1e-9f)
			{
				continue;
			}

			Tids.Add(Tid);
			Triangles.Add(Corners);
		}

		int32 Overlaps = 0;
		for (int32 i = 0; i < Triangles.Num(); ++i)
		{
			for (int32 j = i + 1; j < Triangles.Num(); ++j)
			{
				if (TrianglesOverlap(&Triangles[i][0], &Triangles[j][0]))
				{
					++Overlaps;
				}
			}
		}
		return Overlaps;
	}
}

/**
 * A chamfered box has no razor arris left on it.
 *
 * The requirement in .claude/rules/04-conventions.md, measured rather than assumed: "No perfectly
 * sharp edges. A real edge has a small chamfer that catches light; a mathematically sharp one reads
 * as CG under any lighting." A box is the primitive everything in this flat is composed from, so if
 * a box still has 90 degree arrises after the finish then so does the whole flat.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBevelledBoxTest,
	"HouseForge.Photoreal.BevelledBoxHasNoSharpArris", HF_TEST_FLAGS)

bool FHFBevelledBoxTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeRenderFinish;

	FDynamicMesh3 Sharp = MakeBox();
	TestNearlyEqual(TEXT("A raw box has 90 degree arrises"), SharpestConvexArris(Sharp), 90.0, 0.01);

	FHFBevelParams Params;
	FDynamicMesh3 Chamfered = MakeBox();
	if (!TestTrue(TEXT("A box can be chamfered"), FHFMeshOps::BevelConvexEdges(Chamfered, Params)))
	{
		return false;
	}

	// Half of 90, because a chamfer splits the arris in two - and half of 90 is 45, which is still
	// above the threshold. That is exactly why the operation refuses to bevel its own output: a
	// second pass would see 45 and cut again.
	TestTrue(TEXT("No arris is sharper than a single chamfer facet"),
		SharpestConvexArris(Chamfered) < Params.MinAngleDegrees + 10.0);
	TestTrue(TEXT("The chamfer facets are real facets, not a re-cut arris"),
		SharpestConvexArris(Chamfered) > 40.0);

	TestTrue(TEXT("A chamfered box is still watertight"), FHFMeshOps::IsClosed(Chamfered));
	TestTrue(TEXT("A chamfered box has more triangles than a sharp one"),
		Chamfered.TriangleCount() > Sharp.TriangleCount());

	// Volume is the check that the chamfer is small. A 1.5 mm cut off a 100 x 60 x 40 box takes a
	// few cubic centimetres out of 240,000; anything more means the inset ran away.
	const double SharpVolume = TMeshQueries<FDynamicMesh3>::GetVolumeArea(Sharp).X;
	const double ChamferedVolume = TMeshQueries<FDynamicMesh3>::GetVolumeArea(Chamfered).X;
	TestTrue(TEXT("A chamfer takes material away"), ChamferedVolume < SharpVolume);
	TestTrue(TEXT("A chamfer takes almost nothing away"), ChamferedVolume > SharpVolume * 0.999);

	// Running it a second time must not chamfer the chamfers. It is not idempotent, so this is the
	// property that keeps composition safe rather than a nicety.
	FDynamicMesh3 Twice = Chamfered;
	FHFMeshOps::BevelConvexEdges(Twice, Params);
	TestTrue(TEXT("Chamfering a chamfered box does not keep cutting"),
		TMeshQueries<FDynamicMesh3>::GetVolumeArea(Twice).X > ChamferedVolume * 0.999);

	return true;
}

/**
 * The chamfers carry the same surface role as the faces they came off.
 *
 * FMeshBevel allocates a fresh polygroup for every strip and every junction polygon, and here the
 * polygroup IS the surface role - so untagged chamfers are chamfers the material panel can never
 * reach. It is invisible in any screenshot, which is why it is asserted rather than looked at.
 *
 * The single-role case is the one that bites hardest and is checked first: on a mesh built entirely
 * from WallPaint, MaxGroupID is 2, so the ids the bevel invents are 2, 3, 4 - valid role ids meaning
 * FloorFinish, CeilingSoffit and CoveInterior. Anything that decided newness by looking at the id
 * would tag a wall's arrises as floor and ceiling and report success.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBevelRolesTest,
	"HouseForge.Photoreal.BevelPreservesRoles", HF_TEST_FLAGS)

bool FHFBevelRolesTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeRenderFinish;

	FHFBevelParams Params;

	FDynamicMesh3 OneRole = MakeBox(FVector3d(50.0, 30.0, 20.0), EHFSurfaceRole::WallPaint);
	FHFMeshOps::BevelConvexEdges(OneRole, Params);

	TSet<EHFSurfaceRole> Roles = FHFMeshOps::RolesPresent(OneRole);
	TestEqual(TEXT("A single-role box is still a single role after chamfering"), Roles.Num(), 1);
	TestTrue(TEXT("And it is still the role it was built with"), Roles.Contains(EHFSurfaceRole::WallPaint));

	// Two solids in one mesh, so the chamfers have to come out on the right side of the divide.
	FDynamicMesh3 TwoRoles;
	FHFMeshOps::InitialiseMesh(TwoRoles);
	FHFMeshOps::AppendBox(TwoRoles, FVector3d(0.0, 0.0, 0.0), FVector3d(50.0, 30.0, 5.0),
		0.0, EHFSurfaceRole::CounterStone);
	FHFMeshOps::AppendBox(TwoRoles, FVector3d(0.0, 0.0, -40.0), FVector3d(45.0, 28.0, 35.0),
		0.0, EHFSurfaceRole::JoineryCarcass);

	auto CountRole = [&TwoRoles](EHFSurfaceRole Role)
	{
		int32 Count = 0;
		for (const int32 Tid : TwoRoles.TriangleIndicesItr())
		{
			Count += FHFMeshOps::RoleForGroup(TwoRoles.GetTriangleGroup(Tid)) == Role ? 1 : 0;
		}
		return Count;
	};

	const int32 StoneBefore = CountRole(EHFSurfaceRole::CounterStone);
	const int32 CarcassBefore = CountRole(EHFSurfaceRole::JoineryCarcass);

	FHFMeshOps::BevelConvexEdges(TwoRoles, Params);

	Roles = FHFMeshOps::RolesPresent(TwoRoles);
	TestEqual(TEXT("Two roles in, two roles out"), Roles.Num(), 2);
	TestTrue(TEXT("The counter is still stone"), Roles.Contains(EHFSurfaceRole::CounterStone));
	TestTrue(TEXT("The carcass is still carcass"), Roles.Contains(EHFSurfaceRole::JoineryCarcass));

	const int32 StoneAfter = CountRole(EHFSurfaceRole::CounterStone);
	const int32 CarcassAfter = CountRole(EHFSurfaceRole::JoineryCarcass);

	// Each solid keeps its own chamfers. The two are separate closed boxes with no shared edge, so
	// there is no ambiguity to resolve here and nothing should have crossed the divide - and the
	// total has to account for every triangle, or something came back untagged and fell to WallPaint.
	TestTrue(TEXT("The counter's own chamfers came out as stone"), StoneAfter > StoneBefore);
	TestTrue(TEXT("The carcass's own chamfers came out as carcass"), CarcassAfter > CarcassBefore);
	TestEqual(TEXT("Every triangle is accounted for by one of the two roles"),
		StoneAfter + CarcassAfter, TwoRoles.TriangleCount());

	return true;
}

/**
 * The chamfer stops where a real one would: a shadow gap is not wide enough to take one.
 *
 * .claude/rules/04-conventions.md makes the 3 mm reveal between one shutter and the next the most
 * load-bearing figure in the joinery kit - "without it a run of shutters renders as one unbroken
 * slab, which is the clearest tell there is that joinery was generated rather than built". A
 * 1.5 mm chamfer on both arrises of a 3 mm reveal consumes the whole reveal, so the guard that
 * refuses to bevel a face narrower than a few chamfers is what stops photorealism undoing joinery.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBevelKeepsShadowGapsTest,
	"HouseForge.Photoreal.BevelLeavesShadowGapsAlone", HF_TEST_FLAGS)

bool FHFBevelKeepsShadowGapsTest::RunTest(const FString& Parameters)
{
	FHFBevelParams Params;

	// A 3 mm-wide face - the return of a reveal - between two broad ones.
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);
	FHFMeshOps::AppendBox(Mesh, FVector3d::Zero(), FVector3d(40.0, 0.15, 60.0),
		0.0, EHFSurfaceRole::ShutterLaminate);

	const FAxisAlignedBox3d Before = Mesh.GetBounds();
	FHFMeshOps::BevelConvexEdges(Mesh, Params);
	const FAxisAlignedBox3d After = Mesh.GetBounds();

	// The 3 mm dimension survives untouched; the broad ones may lose a chamfer off each end.
	TestNearlyEqual(TEXT("A 3 mm reveal keeps its full width"),
		After.Depth(), Before.Depth(), 0.001);
	TestTrue(TEXT("A 3 mm reveal is still a 3 mm reveal"), After.Depth() > 0.29);

	return true;
}

/**
 * UV0 is world position over texel size, and stays that way through everything above.
 *
 * The material panel expresses tiling in millimetres by leaning on exactly this. It is re-derived
 * here from the vertex positions rather than compared against a stored copy, so the assertion is
 * the relationship itself and not a snapshot of it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFRenderFinishKeepsWorldScaleUVsTest,
	"HouseForge.Photoreal.FinishKeepsWorldScaleUV0", HF_TEST_FLAGS)

bool FHFRenderFinishKeepsWorldScaleUVsTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeRenderFinish;

	FDynamicMesh3 Mesh = MakeBox(FVector3d(100.0, 100.0, 100.0));

	FHFRenderFinish Finish;
	FHFMeshOps::FinishForRender(Mesh, Finish);

	if (!TestTrue(TEXT("The finished mesh has attributes"), Mesh.HasAttributes()))
	{
		return false;
	}

	const FDynamicMeshUVOverlay* UVs = Mesh.Attributes()->PrimaryUV();
	if (!TestNotNull(TEXT("The finished mesh has a primary UV overlay"), UVs))
	{
		return false;
	}

	const double InvTexel = 1.0 / Finish.TexelSizeCm;
	int32 Checked = 0;

	for (const int32 Tid : Mesh.TriangleIndicesItr())
	{
		if (!UVs->IsSetTriangle(Tid))
		{
			AddError(TEXT("A triangle came out of the finish with no UV0 - including, and especially, a chamfer facet."));
			return false;
		}

		const FIndex3i Tri = Mesh.GetTriangle(Tid);
		const FIndex3i UVTri = UVs->GetTriangle(Tid);
		const FVector3d Normal = Mesh.GetTriNormal(Tid);

		const double AbsX = FMath::Abs(Normal.X);
		const double AbsY = FMath::Abs(Normal.Y);
		const double AbsZ = FMath::Abs(Normal.Z);

		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FVector3d P = Mesh.GetVertex(Tri[Corner]);

			FVector2f Expected;
			if (AbsZ >= AbsX && AbsZ >= AbsY)	{ Expected = FVector2f(P.X * InvTexel, P.Y * InvTexel); }
			else if (AbsX >= AbsY)				{ Expected = FVector2f(P.Y * InvTexel, P.Z * InvTexel); }
			else								{ Expected = FVector2f(P.X * InvTexel, P.Z * InvTexel); }

			if (!UVs->GetElement(UVTri[Corner]).Equals(Expected, 1e-4f))
			{
				AddError(FString::Printf(
					TEXT("UV0 on triangle %d corner %d is no longer world position over texel size."), Tid, Corner));
				return false;
			}
			++Checked;
		}
	}

	TestTrue(TEXT("Something was actually checked"), Checked > 0);

	// And the welding that makes tangents continuous. Before this, every triangle corner got its own
	// element, so the tangent basis the component derives from UV0 broke at every triangle edge -
	// invisible against flat colours and a seam per triangle once a normal map goes on.
	TestTrue(TEXT("UV0 elements are shared between triangles of the same face"),
		UVs->ElementCount() < Mesh.TriangleCount() * 3);

	return true;
}

/**
 * A second UV channel exists, and its islands do not sit on top of each other.
 *
 * .claude/rules/04-conventions.md asks for "a second UV channel for lightmaps, so baked lighting
 * stays an option alongside Lumen". UV0 cannot serve: it is world position, so two identical walls
 * in different rooms occupy the same UV space by design, and a bake would light one from the other.
 *
 * Overlap is tested exhaustively, by separating axis over every pair of UV1 triangles. That is
 * quadratic and therefore only usable on small meshes, which is why the flat-wide version samples
 * instead - but on a box it is exact, and exact is what "does not overlap" has to mean.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFLightmapUVTest,
	"HouseForge.Photoreal.LightmapUVsDoNotOverlap", HF_TEST_FLAGS)

bool FHFLightmapUVTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeRenderFinish;

	FDynamicMesh3 Mesh = MakeBox(FVector3d(120.0, 45.0, 210.0), EHFSurfaceRole::DoorLeaf);

	FHFRenderFinish Finish;
	FHFMeshOps::FinishForRender(Mesh, Finish);

	if (!TestTrue(TEXT("The finished mesh has a second UV layer"),
		Mesh.HasAttributes() && Mesh.Attributes()->NumUVLayers() >= 2))
	{
		return false;
	}

	const FDynamicMeshUVOverlay* Lightmap = Mesh.Attributes()->GetUVLayer(1);
	if (!TestNotNull(TEXT("The second layer has an overlay"), Lightmap))
	{
		return false;
	}

	FAxisAlignedBox2f Bounds = FAxisAlignedBox2f::Empty();
	int32 Unset = 0;
	for (const int32 Tid : Mesh.TriangleIndicesItr())
	{
		if (!Lightmap->IsSetTriangle(Tid))
		{
			++Unset;
			continue;
		}

		const FIndex3i UVTri = Lightmap->GetTriangle(Tid);
		for (int32 i = 0; i < 3; ++i)
		{
			Bounds.Contain(Lightmap->GetElement(UVTri[i]));
		}
	}

	TestEqual(TEXT("Every triangle carries a lightmap UV"), Unset, 0);
	TestTrue(TEXT("The unwrap is inside the unit square"),
		Bounds.Min.X >= -0.001f && Bounds.Min.Y >= -0.001f
		&& Bounds.Max.X <= 1.001f && Bounds.Max.Y <= 1.001f);
	TestTrue(TEXT("The unwrap fills a useful part of the unit square"),
		Bounds.Width() > 0.5f && Bounds.Height() > 0.5f);

	TestEqual(TEXT("No two lightmap triangles overlap"), CountLightmapOverlaps(Mesh), 0);

	// UV0 must not have been disturbed by any of it - the material panel reads that one.
	const FDynamicMeshUVOverlay* Primary = Mesh.Attributes()->PrimaryUV();
	if (TestNotNull(TEXT("UV0 survives the second channel"), Primary))
	{
		FAxisAlignedBox2f PrimaryBounds = FAxisAlignedBox2f::Empty();
		for (const int32 Eid : Primary->ElementIndicesItr())
		{
			PrimaryBounds.Contain(Primary->GetElement(Eid));
		}
		// 210 cm tall at a 100 cm texel spans about 2.1 tiles, which the unit square could not hold.
		TestTrue(TEXT("UV0 is still in world scale, not normalised into the unit square"),
			PrimaryBounds.Height() > 1.5f || PrimaryBounds.Width() > 1.5f);
	}

	return true;
}

/**
 * A pane of glass is a solid, everywhere one is generated.
 *
 * .claude/rules/04-conventions.md: "Glass needs thickness, not a plane, or refraction and reflection
 * look wrong." A zero-thickness pane is not merely thin - it has no back surface for a ray to
 * refract out of, so Lumen and any path tracer read it as a coloured film rather than as glass, and
 * the double reflection that makes a window read as a window never appears.
 *
 * Checked on the real generators rather than on a hand-built pane, because the failure would arrive
 * as somebody centring a pane in a rebate and getting the arithmetic to zero.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFGlassIsSolidTest,
	"HouseForge.Photoreal.GlassIsASolid", HF_TEST_FLAGS)

bool FHFGlassIsSolidTest::RunTest(const FString& Parameters)
{
	auto GlassBounds = [](const FDynamicMesh3& Mesh)
	{
		FAxisAlignedBox3d Bounds = FAxisAlignedBox3d::Empty();
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (FHFMeshOps::RoleForGroup(Mesh.GetTriangleGroup(Tid)) != EHFSurfaceRole::Glass)
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
	};

	// The thinnest glass the plugin specifies anywhere is 4 mm, so anything under 3 mm of measured
	// thickness is a pane that has been flattened by arithmetic rather than one that is meant to be
	// thin.
	const double MinimumThicknessCm = 0.3;

	auto CheckPane = [&](const TCHAR* What, const FDynamicMesh3& Mesh)
	{
		const FAxisAlignedBox3d Bounds = GlassBounds(Mesh);
		if (!TestTrue(FString::Printf(TEXT("%s has glass at all"), What), Bounds.Volume() >= 0.0 && Bounds.MaxDim() > 0.0))
		{
			return;
		}

		const double Thinnest = FMath::Min3(Bounds.Width(), Bounds.Depth(), Bounds.Height());
		TestTrue(FString::Printf(TEXT("%s glazing has real thickness (%.3f cm)"), What, Thinnest),
			Thinnest >= MinimumThicknessCm);
	};

	FHFWall Wall;
	Wall.Id = TEXT("W1");
	Wall.Start = FVector2D(0.0, 0.0);
	Wall.End = FVector2D(500.0, 0.0);
	Wall.Thickness = 23.0;
	Wall.Height = 300.0;

	FHFOpening Window;
	Window.Id = TEXT("WIN1");
	Window.WallId = Wall.Id;
	Window.OffsetAlongWall = 250.0;
	Window.Width = 150.0;
	Window.Height = 120.0;
	Window.SillHeight = 90.0;
	Window.Kind = EHFOpeningKind::Window;
	CheckPane(TEXT("A fixed window's"), FHFGenerators::GenerateOpeningInfill(Window, Wall));

	auto CheckEveryGlazedPart = [&](const TCHAR* What, const FHFOpening& Opening)
	{
		TArray<FHFMeshPart> Parts;
		FHFGenerators::BuildOpeningParts(Opening, Wall, Parts);
		int32 Glazed = 0;
		for (const FHFMeshPart& Part : Parts)
		{
			if (FHFMeshOps::RolesPresent(Part.Mesh).Contains(EHFSurfaceRole::Glass))
			{
				CheckPane(What, Part.Mesh);
				++Glazed;
			}
		}
		TestTrue(FString::Printf(TEXT("%s has a glazed part at all"), What), Glazed > 0);
	};

	FHFOpening Slider = Window;
	Slider.Id = TEXT("WIN2");
	Slider.Kind = EHFOpeningKind::SlidingWindow;
	CheckEveryGlazedPart(TEXT("A sliding window sash's"), Slider);

	FHFOpening Balcony;
	Balcony.Id = TEXT("D1");
	Balcony.WallId = Wall.Id;
	Balcony.OffsetAlongWall = 250.0;
	Balcony.Width = 210.0;
	Balcony.Height = 210.0;
	Balcony.Kind = EHFOpeningKind::SlidingDoor;
	CheckEveryGlazedPart(TEXT("A balcony slider's"), Balcony);

	FHFShutterParams Glazed;
	Glazed.ModuleWidth = 45.0;
	Glazed.ModuleHeight = 90.0;
	Glazed.Thickness = 1.9;
	Glazed.bGlassInsert = true;
	CheckPane(TEXT("A glazed shutter's"), FHFJoineryKit::GenerateShutter(Glazed));

	return true;
}

/**
 * A chamfer does not survive the finish without normals or UVs of its own.
 *
 * The whole point of running the bevel before the projection. A triangle with no normal elements
 * shades off FDynamicMesh3's constant (0, 1, 0), so an unfinished chamfer would be a band of
 * geometry lit as though it faced +Y - on every arris in the flat, and pixel-identical to correct
 * output in any unlit or wireframe capture.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFChamferIsShadedTest,
	"HouseForge.Photoreal.ChamfersAreShadedAndUnwrapped", HF_TEST_FLAGS)

bool FHFChamferIsShadedTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeRenderFinish;

	FDynamicMesh3 Mesh = MakeBox(FVector3d(50.0, 30.0, 20.0), EHFSurfaceRole::CounterStone);
	const int32 Before = Mesh.TriangleCount();

	FHFRenderFinish Finish;
	FHFMeshOps::FinishForRender(Mesh, Finish);

	if (!TestTrue(TEXT("Chamfers were added"), Mesh.TriangleCount() > Before))
	{
		return false;
	}

	const FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
	if (!TestNotNull(TEXT("The finished mesh has a normal overlay"), Normals))
	{
		return false;
	}

	for (const int32 Tid : Mesh.TriangleIndicesItr())
	{
		if (!Normals->IsSetTriangle(Tid))
		{
			AddError(FString::Printf(TEXT("Triangle %d has no shading normal after the finish."), Tid));
			return false;
		}
	}

	// And the chamfer facets stay hard against the faces either side. A 90 degree arris chamfered
	// once leaves two 45 degree edges, which is above the 40 degree split, so the chamfer reads as
	// its own plane catching its own highlight rather than as a smudge between two faces. That is
	// the deliberate hard-edge choice .claude/rules/04-conventions.md asks for.
	int32 SplitNormals = 0;
	for (const int32 Eid : Mesh.EdgeIndicesItr())
	{
		const FIndex2i Tris = Mesh.GetEdgeT(Eid);
		if (Tris.B != FDynamicMesh3::InvalidID && !Normals->AreTrianglesConnected(Tris.A, Tris.B))
		{
			++SplitNormals;
		}
	}
	TestTrue(TEXT("Chamfer facets keep hard edges against their parent faces"), SplitNormals > 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
