// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Geometry/HFGenerators.h"
#include "Geometry/HFMeshOps.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFTypes.h"
#include "Tests/HFSpecTestHelpers.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FHFWall MakeWall(double Length = 400.0, double Thickness = 20.0, double Height = 300.0)
	{
		FHFWall Wall;
		Wall.Id = TEXT("W1");
		Wall.Start = FVector2D(0.0, 0.0);
		Wall.End = FVector2D(Length, 0.0);
		Wall.Thickness = Thickness;
		Wall.Height = Height;
		return Wall;
	}

	FHFOpening MakeDoor(double Offset = 200.0, double Width = 90.0, double Height = 210.0)
	{
		FHFOpening Door;
		Door.Id = TEXT("D1");
		Door.WallId = TEXT("W1");
		Door.OffsetAlongWall = Offset;
		Door.Width = Width;
		Door.Height = Height;
		Door.Kind = EHFOpeningKind::Door;
		Door.Swing = EHFSwing::InwardLeft;
		return Door;
	}
}

/** The box primitive everything else is composed from. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFMeshOpsBoxTest, "HouseForge.Geometry.Box", HF_TEST_FLAGS)

bool FHFMeshOpsBoxTest::RunTest(const FString& Parameters)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);
	FHFMeshOps::AppendBox(Mesh, FVector3d(0, 0, 50), FVector3d(100, 50, 50), 0.0, EHFSurfaceRole::WallPaint);

	TestEqual(TEXT("A box has 8 vertices"), Mesh.VertexCount(), 8);
	TestEqual(TEXT("A box has 12 triangles"), Mesh.TriangleCount(), 12);
	TestTrue(TEXT("A box is watertight"), FHFMeshOps::IsClosed(Mesh));

	// Positive volume means the normals face outward. An inverted box still looks solid in the
	// viewport but silently defeats every mesh boolean, so openings would never cut.
	TestTrue(TEXT("A box faces outward (positive volume)"),
		TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X > 0.0);

	const FAxisAlignedBox3d Bounds = Mesh.GetBounds();
	TestTrue(TEXT("Box bounds match the requested extents"),
		Bounds.Min.Equals(FVector3d(-100, -50, 0), 0.01) &&
		Bounds.Max.Equals(FVector3d(100, 50, 100), 0.01));

	// Every triangle must carry a role, or the material panel cannot target it.
	for (const int32 Tid : Mesh.TriangleIndicesItr())
	{
		if (Mesh.GetTriangleGroup(Tid) != FHFMeshOps::GroupForRole(EHFSurfaceRole::WallPaint))
		{
			AddError(TEXT("A triangle was emitted without its surface role polygroup."));
			break;
		}
	}

	// A yaw of 90 degrees swaps the footprint's X and Y extents.
	FDynamicMesh3 Rotated;
	FHFMeshOps::InitialiseMesh(Rotated);
	FHFMeshOps::AppendBox(Rotated, FVector3d(0, 0, 50), FVector3d(100, 50, 50), 90.0, EHFSurfaceRole::WallPaint);
	const FAxisAlignedBox3d RotatedBounds = Rotated.GetBounds();
	TestNearlyEqual(TEXT("Rotation swaps the X extent"), RotatedBounds.Width(), 100.0, 0.01);
	TestNearlyEqual(TEXT("Rotation swaps the Y extent"), RotatedBounds.Height(), 200.0, 0.01);

	return true;
}

/**
 * Prisms must handle concave boundaries. Fanning from a centroid would spill geometry outside an
 * L-shaped room, and these layouts are full of them.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFMeshOpsPrismTest, "HouseForge.Geometry.ConcavePrism", HF_TEST_FLAGS)

bool FHFMeshOpsPrismTest::RunTest(const FString& Parameters)
{
	// A 400x300 rectangle with a 200x150 bite out of the top-right.
	const TArray<FVector2D> LShape = {
		FVector2D(0, 0), FVector2D(400, 0), FVector2D(400, 150),
		FVector2D(200, 150), FVector2D(200, 300), FVector2D(0, 300)
	};

	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);
	TestTrue(TEXT("A concave prism generates"),
		FHFMeshOps::AppendPrism(Mesh, LShape, 0.0, 20.0, EHFSurfaceRole::FloorFinish));

	TestTrue(TEXT("A concave prism is watertight"), FHFMeshOps::IsClosed(Mesh));
	TestTrue(TEXT("A concave prism faces outward (positive volume)"),
		TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X > 0.0);

	// The bite must actually be missing: a fan triangulation would fill it and report the full
	// rectangle's volume.
	const double ExpectedVolume = (400.0 * 300.0 - 200.0 * 150.0) * 20.0;
	TestNearlyEqual(TEXT("The concave bite is genuinely absent"),
		TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X, ExpectedVolume, ExpectedVolume * 0.01);

	const FAxisAlignedBox3d Bounds = Mesh.GetBounds();
	TestTrue(TEXT("Prism bounds match the boundary"),
		Bounds.Min.Equals(FVector3d(0, 0, 0), 0.01) && Bounds.Max.Equals(FVector3d(400, 300, 20), 0.01));

	// Clockwise input must produce the same solid: drawings are read in both directions.
	TArray<FVector2D> Reversed = LShape;
	Algo::Reverse(Reversed);
	FDynamicMesh3 ReversedMesh;
	FHFMeshOps::InitialiseMesh(ReversedMesh);
	FHFMeshOps::AppendPrism(ReversedMesh, Reversed, 0.0, 20.0, EHFSurfaceRole::FloorFinish);

	TestTrue(TEXT("Reversed winding is still watertight"), FHFMeshOps::IsClosed(ReversedMesh));
	TestEqual(TEXT("Reversed winding gives the same triangle count"),
		ReversedMesh.TriangleCount(), Mesh.TriangleCount());

	return true;
}

/** A wall with no openings is a plain box of the declared dimensions. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWallTest, "HouseForge.Geometry.Wall", HF_TEST_FLAGS)

bool FHFWallTest::RunTest(const FString& Parameters)
{
	const FHFWall Wall = MakeWall();
	const FDynamicMesh3 Mesh = FHFGenerators::GenerateWall(Wall, {});

	TestTrue(TEXT("A plain wall is watertight"), FHFMeshOps::IsClosed(Mesh));

	const FAxisAlignedBox3d Bounds = Mesh.GetBounds();
	TestNearlyEqual(TEXT("Wall length matches the centreline"), Bounds.Width(), 400.0, 0.01);
	TestNearlyEqual(TEXT("Wall thickness is centred on the centreline"), Bounds.Height(), 20.0, 0.01);
	TestNearlyEqual(TEXT("Wall height matches"), Bounds.Depth(), 300.0, 0.01);
	TestNearlyEqual(TEXT("The wall sits on its base"), Bounds.Min.Z, 0.0, 0.01);

	// Thickness grows symmetrically about the centreline, which is what keeps junctions with
	// neighbouring walls put when a thickness is edited.
	FHFWall Thick = MakeWall(400.0, 40.0);
	const FAxisAlignedBox3d ThickBounds = FHFGenerators::GenerateWall(Thick, {}).GetBounds();
	TestNearlyEqual(TEXT("Thickening keeps the centreline centred"), ThickBounds.Center().Y, 0.0, 0.01);

	return true;
}

/**
 * The cut is the whole point of the wall generator. If a boolean silently fails, the wall looks
 * perfectly fine in a screenshot and simply has no doorway.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWallOpeningTest, "HouseForge.Geometry.WallOpeningIsCut", HF_TEST_FLAGS)

bool FHFWallOpeningTest::RunTest(const FString& Parameters)
{
	const FHFWall Wall = MakeWall();
	const FDynamicMesh3 Solid = FHFGenerators::GenerateWall(Wall, {});
	const FDynamicMesh3 Cut = FHFGenerators::GenerateWall(Wall, { MakeDoor() });

	TestTrue(TEXT("Cutting an opening adds geometry"), Cut.TriangleCount() > Solid.TriangleCount());
	TestTrue(TEXT("A cut wall is still watertight"), FHFMeshOps::IsClosed(Cut));

	// Overall bounds must not change - the cut removes material from inside, not outside.
	const FAxisAlignedBox3d Bounds = Cut.GetBounds();
	TestNearlyEqual(TEXT("Cutting does not change the wall length"), Bounds.Width(), 400.0, 0.01);
	TestNearlyEqual(TEXT("Cutting does not change the wall height"), Bounds.Depth(), 300.0, 0.01);

	// The doorway must actually be void. Sample the centre of the opening and check no triangle
	// encloses it, by confirming the mesh volume dropped by roughly the opening's volume.
	const double SolidVolume = 400.0 * 20.0 * 300.0;
	const double DoorVolume = 90.0 * 20.0 * 210.0;

	const double SolidVol = TMeshQueries<FDynamicMesh3>::GetVolumeArea(Solid).X;
	const double CutVol = TMeshQueries<FDynamicMesh3>::GetVolumeArea(Cut).X;

	TestNearlyEqual(TEXT("The solid wall's volume matches its dimensions"), SolidVol, SolidVolume, SolidVolume * 0.01);
	TestNearlyEqual(TEXT("The cut removed exactly the opening's volume"),
		SolidVol - CutVol, DoorVolume, DoorVolume * 0.02);

	// Two openings in one wall, to prove booleans chain.
	FHFOpening Second = MakeDoor(50.0, 60.0, 200.0);
	Second.Id = TEXT("D2");
	const FDynamicMesh3 TwoCuts = FHFGenerators::GenerateWall(Wall, { MakeDoor(), Second });
	const double TwoVol = TMeshQueries<FDynamicMesh3>::GetVolumeArea(TwoCuts).X;

	TestTrue(TEXT("A second opening removes more material"), TwoVol < CutVol);
	TestTrue(TEXT("A twice-cut wall is still watertight"), FHFMeshOps::IsClosed(TwoCuts));

	return true;
}

/** Floors, and skirting that stops at doorways rather than running across them. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFloorTest, "HouseForge.Geometry.FloorAndSkirting", HF_TEST_FLAGS)

bool FHFFloorTest::RunTest(const FString& Parameters)
{
	FHFRoom Room;
	Room.Id = TEXT("R1");
	Room.Boundary = { FVector2D(0, 0), FVector2D(400, 0), FVector2D(400, 300), FVector2D(0, 300) };
	Room.CeilingHeight = 300.0;
	Room.SkirtingHeight = 0.0;

	const FDynamicMesh3 Bare = FHFGenerators::GenerateFloor(Room, 15.0, {}, 100.0);
	TestTrue(TEXT("A bare floor slab generates"), Bare.TriangleCount() > 0);
	TestTrue(TEXT("A bare floor slab is watertight"), FHFMeshOps::IsClosed(Bare));

	const FAxisAlignedBox3d Bounds = Bare.GetBounds();
	TestNearlyEqual(TEXT("The slab hangs below the finished floor level"), Bounds.Max.Z, 0.0, 0.01);
	TestNearlyEqual(TEXT("The slab is as thick as requested"), Bounds.Depth(), 15.0, 0.01);

	// Skirting adds geometry.
	Room.SkirtingHeight = 10.0;
	const FDynamicMesh3 Skirted = FHFGenerators::GenerateFloor(Room, 15.0, {}, 100.0);
	TestTrue(TEXT("Skirting adds geometry"), Skirted.TriangleCount() > Bare.TriangleCount());

	// A doorway must interrupt it: running skirting across an opening is the most obvious tell
	// that geometry was generated rather than modelled. Splitting one run into two adds triangles,
	// so the property to check is that less material was emitted, not fewer triangles.
	const FDynamicMesh3 WithGap = FHFGenerators::GenerateFloor(Room, 15.0, { FVector2D(200.0, 0.0) }, 100.0);
	const double SkirtedVolume = TMeshQueries<FDynamicMesh3>::GetVolumeArea(Skirted).X;
	const double GappedVolume = TMeshQueries<FDynamicMesh3>::GetVolumeArea(WithGap).X;
	TestTrue(TEXT("A doorway removes a length of skirting"), GappedVolume < SkirtedVolume);

	// Zero skirting height must emit none at all.
	Room.SkirtingHeight = 0.0;
	TestEqual(TEXT("Zero skirting height emits no skirting"),
		FHFGenerators::GenerateFloor(Room, 15.0, {}, 100.0).TriangleCount(), Bare.TriangleCount());

	return true;
}

/** Beams hang below the slab soffit; that relationship is what false ceilings are built around. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFStructureTest, "HouseForge.Geometry.BeamsAndColumns", HF_TEST_FLAGS)

bool FHFStructureTest::RunTest(const FString& Parameters)
{
	FHFBeam Beam;
	Beam.Id = TEXT("B1");
	Beam.Start = FVector2D(0.0, 0.0);
	Beam.End = FVector2D(600.0, 0.0);
	Beam.Width = 23.0;
	Beam.Depth = 45.0;
	Beam.SoffitZ = 300.0;

	const FDynamicMesh3 BeamMesh = FHFGenerators::GenerateBeam(Beam);
	TestTrue(TEXT("A beam is watertight"), FHFMeshOps::IsClosed(BeamMesh));

	const FAxisAlignedBox3d BeamBounds = BeamMesh.GetBounds();
	TestNearlyEqual(TEXT("A beam's top sits at the slab soffit"), BeamBounds.Max.Z, 300.0, 0.01);
	TestNearlyEqual(TEXT("A beam hangs down by its depth"), BeamBounds.Min.Z, 255.0, 0.01);
	TestNearlyEqual(TEXT("A beam spans its centreline"), BeamBounds.Width(), 600.0, 0.01);

	FHFColumn Column;
	Column.Id = TEXT("C1");
	Column.Position = FVector2D(100.0, 200.0);
	Column.Size = FVector2D(45.0, 23.0);
	Column.Height = 300.0;

	const FDynamicMesh3 ColumnMesh = FHFGenerators::GenerateColumn(Column);
	TestTrue(TEXT("A column is watertight"), FHFMeshOps::IsClosed(ColumnMesh));

	const FAxisAlignedBox3d ColumnBounds = ColumnMesh.GetBounds();
	TestNearlyEqual(TEXT("A column stands on the floor"), ColumnBounds.Min.Z, 0.0, 0.01);
	TestNearlyEqual(TEXT("A column is as tall as declared"), ColumnBounds.Depth(), 300.0, 0.01);
	TestTrue(TEXT("A column is centred on its position"),
		ColumnBounds.Center().Equals(FVector3d(100.0, 200.0, 150.0), 0.01));

	return true;
}

/** Door leaves and window glazing, and the archway that is deliberately nothing. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFOpeningInfillTest, "HouseForge.Geometry.OpeningInfill", HF_TEST_FLAGS)

bool FHFOpeningInfillTest::RunTest(const FString& Parameters)
{
	const FHFWall Wall = MakeWall();

	const FDynamicMesh3 Leaf = FHFGenerators::GenerateOpeningInfill(MakeDoor(), Wall);
	TestTrue(TEXT("A door produces a leaf"), Leaf.TriangleCount() > 0);
	const FAxisAlignedBox3d LeafBounds = Leaf.GetBounds();
	TestTrue(TEXT("The leaf sits within the opening's height"),
		LeafBounds.Min.Z >= -0.01 && LeafBounds.Max.Z <= 210.01);

	FHFOpening Window = MakeDoor(200.0, 150.0, 135.0);
	Window.Id = TEXT("Win1");
	Window.Kind = EHFOpeningKind::Window;
	Window.SillHeight = 90.0;

	const FDynamicMesh3 Glazed = FHFGenerators::GenerateOpeningInfill(Window, Wall);
	TestTrue(TEXT("A window produces a frame and glazing"), Glazed.TriangleCount() > Leaf.TriangleCount());

	const FAxisAlignedBox3d GlazedBounds = Glazed.GetBounds();
	TestNearlyEqual(TEXT("The window sits on its sill"), GlazedBounds.Min.Z, 90.0, 0.5);
	TestNearlyEqual(TEXT("The window reaches its head"), GlazedBounds.Max.Z, 225.0, 0.5);

	// Glass has to be its own role or it cannot be made transparent.
	bool bHasGlass = false;
	for (const int32 Tid : Glazed.TriangleIndicesItr())
	{
		if (FHFMeshOps::RoleForGroup(Glazed.GetTriangleGroup(Tid)) == EHFSurfaceRole::Glass)
		{
			bHasGlass = true;
			break;
		}
	}
	TestTrue(TEXT("Window glazing carries the Glass role"), bHasGlass);

	FHFOpening Arch = MakeDoor();
	Arch.Kind = EHFOpeningKind::Archway;
	TestEqual(TEXT("An archway is a hole and nothing else"),
		FHFGenerators::GenerateOpeningInfill(Arch, Wall).TriangleCount(), 0);

	return true;
}

/** UVs must be real-world scaled, or tiling cannot be expressed in millimetres later. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFUVScaleTest, "HouseForge.Geometry.WorldScaleUVs", HF_TEST_FLAGS)

bool FHFUVScaleTest::RunTest(const FString& Parameters)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);
	// A 200 cm cube, so with a 100 cm texel it should span two tiles.
	FHFMeshOps::AppendBox(Mesh, FVector3d(0, 0, 100), FVector3d(100, 100, 100), 0.0, EHFSurfaceRole::WallPaint);
	FHFMeshOps::ApplyWorldScaleUVs(Mesh, 100.0);

	if (!TestTrue(TEXT("The mesh has attributes"), Mesh.HasAttributes()))
	{
		return false;
	}

	const FDynamicMeshUVOverlay* UVs = Mesh.Attributes()->PrimaryUV();
	if (!TestNotNull(TEXT("The mesh has a UV overlay"), UVs))
	{
		return false;
	}
	TestTrue(TEXT("UVs were generated"), UVs->ElementCount() > 0);

	// The property that matters is the ratio of UV distance to world distance: one tile per
	// TexelSizeCm. Pooling UVs across faces proves nothing, because faces projected along
	// different axes legitimately occupy different regions of UV space.
	int32 Checked = 0;
	for (const int32 Tid : Mesh.TriangleIndicesItr())
	{
		if (!UVs->IsSetTriangle(Tid))
		{
			continue;
		}

		const FIndex3i Tri = Mesh.GetTriangle(Tid);
		const FIndex3i UVTri = UVs->GetTriangle(Tid);

		for (int32 Edge = 0; Edge < 3; ++Edge)
		{
			const int32 Next = (Edge + 1) % 3;
			const double WorldLength = FVector3d::Distance(Mesh.GetVertex(Tri[Edge]), Mesh.GetVertex(Tri[Next]));
			const double UVLength = FVector2f::Distance(UVs->GetElement(UVTri[Edge]), UVs->GetElement(UVTri[Next]));

			// Skip the diagonal of each quad, which is foreshortened in neither axis but is not a
			// clean multiple to reason about; the two axis-aligned edges are the real check.
			if (WorldLength < 1.0)
			{
				continue;
			}

			TestNearlyEqual(
				*FString::Printf(TEXT("A %.0f cm edge is %.2f tiles at a 100 cm texel"), WorldLength, UVLength),
				UVLength, WorldLength / 100.0, 0.01);
			++Checked;
		}

		if (Checked >= 12)
		{
			break;
		}
	}

	TestTrue(TEXT("Some edges were checked"), Checked > 0);

	// And at a finer texel the same edge must span proportionally more tiles.
	FDynamicMesh3 Finer;
	FHFMeshOps::InitialiseMesh(Finer);
	FHFMeshOps::AppendBox(Finer, FVector3d(0, 0, 100), FVector3d(100, 100, 100), 0.0, EHFSurfaceRole::WallPaint);
	FHFMeshOps::ApplyWorldScaleUVs(Finer, 50.0);

	const FDynamicMeshUVOverlay* FineUVs = Finer.Attributes()->PrimaryUV();
	if (FineUVs != nullptr && FineUVs->ElementCount() > 0)
	{
		const FIndex3i Tri = Finer.GetTriangle(0);
		const FIndex3i UVTri = FineUVs->GetTriangle(0);
		const double WorldLength = FVector3d::Distance(Finer.GetVertex(Tri[0]), Finer.GetVertex(Tri[1]));
		const double UVLength = FVector2f::Distance(FineUVs->GetElement(UVTri[0]), FineUVs->GetElement(UVTri[1]));
		TestNearlyEqual(TEXT("Halving the texel size doubles the tile count"),
			UVLength, WorldLength / 50.0, 0.01);
	}

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
