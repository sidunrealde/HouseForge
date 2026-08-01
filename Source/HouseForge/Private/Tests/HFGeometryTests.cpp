// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshTransforms.h"
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
 * Shading normals: the one attribute nothing else in the gate can see.
 *
 * A mesh with an empty normal overlay is watertight, correctly wound, the right size, in the right
 * place and correctly unwrapped - every other assertion in this suite passes on it - and renders
 * with the constant normal (0, 1, 0), because FDynamicMesh3::GetVertexNormal returns UnitY when a
 * mesh has neither overlay elements nor per-vertex normals. It only shows up once the flat is lit,
 * which is the milestone's declared end goal, so it is asserted here instead.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFShadingNormalsTest, "HouseForge.Geometry.ShadingNormals", HF_TEST_FLAGS)

namespace
{
	/** The overlay a generated mesh must come back carrying, or nullptr if it has none. */
	const FDynamicMeshNormalOverlay* NormalsOf(const FDynamicMesh3& Mesh)
	{
		return Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryNormals() : nullptr;
	}
}

bool FHFShadingNormalsTest::RunTest(const FString& Parameters)
{
	// ------------------------------------------------------------------- a box shades as six faces

	FDynamicMesh3 Box;
	FHFMeshOps::InitialiseMesh(Box);
	FHFMeshOps::AppendBox(Box, FVector3d(0, 0, 15), FVector3d(10, 20, 15), 0.0, EHFSurfaceRole::JoineryCarcass);
	FHFMeshOps::ApplyWorldScaleUVs(Box);

	const FDynamicMeshNormalOverlay* BoxNormals = NormalsOf(Box);
	if (!TestNotNull(TEXT("A generated box has a normal overlay"), BoxNormals))
	{
		return false;
	}

	// Zero elements is the failure this test exists for: the overlay is created empty by
	// EnableAttributes and every AppendTriangle leaves its three normal slots InvalidID, so a
	// generator that never filled it ships a mesh the renderer shades with a constant.
	TestTrue(TEXT("A generated box carries shading normals rather than an empty overlay"),
		BoxNormals->ElementCount() > 0);

	double WorstDeviation = 0.0;
	TArray<FVector3d> Distinct;

	for (const int32 Tid : Box.TriangleIndicesItr())
	{
		const FIndex3i ElTri = BoxNormals->GetTriangle(Tid);
		if (ElTri.A == INDEX_NONE || ElTri.B == INDEX_NONE || ElTri.C == INDEX_NONE)
		{
			AddError(TEXT("A box triangle was left with no shading normal; it will render as (0, 1, 0)."));
			return false;
		}

		const FVector3d Face = Box.GetTriNormal(Tid);
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FVector3d Shaded(BoxNormals->GetElement(ElTri[Corner]));
			WorstDeviation = FMath::Max(WorstDeviation, (Shaded - Face).Length());
		}

		if (!Distinct.ContainsByPredicate([&Face](const FVector3d& Seen) { return Seen.Dot(Face) > 0.999; }))
		{
			Distinct.Add(Face);
		}
	}

	// Hard arrises: a box's corners are shared between three faces, so anything that welded them
	// would round the box off and this deviation would be large.
	TestTrue(FString::Printf(TEXT("A box's arrises stay hard (worst deviation %.6f)"), WorstDeviation),
		WorstDeviation < 1e-5);
	TestEqual(TEXT("A box shades as six distinct face directions"), Distinct.Num(), 6);

	// ------------------------------------------------------------------- a tube shades as a curve

	// The rail, the knob dome and the cove arc exist precisely to read as curved. Splitting every
	// edge would facet them, which is why the split is by opening angle rather than per triangle.
	constexpr double TubeRadius = 1.25;
	FDynamicMesh3 Tube;
	FHFMeshOps::InitialiseMesh(Tube);
	FHFMeshOps::AppendRevolvedProfile(Tube,
		{ FVector2D(0.0, TubeRadius), FVector2D(40.0, TubeRadius) },
		FVector3d::Zero(), FVector3d::UnitX(), 16, EHFSurfaceRole::MetalHardware);
	FHFMeshOps::ApplyWorldScaleUVs(Tube);

	const FDynamicMeshNormalOverlay* TubeNormals = NormalsOf(Tube);
	if (!TestNotNull(TEXT("A generated tube has a normal overlay"), TubeNormals))
	{
		return false;
	}

	int32 Welded = 0;
	double WorstOffAxis = 0.0;
	double WorstOutward = 1.0;
	double WorstCapDeviation = 0.0;

	for (const int32 Tid : Tube.TriangleIndicesItr())
	{
		const FIndex3i ElTri = TubeNormals->GetTriangle(Tid);
		if (ElTri.A == INDEX_NONE)
		{
			AddError(TEXT("A tube triangle was left with no shading normal."));
			return false;
		}

		const FIndex3i Tri = Tube.GetTriangle(Tid);
		const FVector3d Face = Tube.GetTriNormal(Tid);
		const bool bIsCap = FMath::Abs(Face.X) > 0.9;

		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FVector3d Shaded(TubeNormals->GetElement(ElTri[Corner]));

			if (bIsCap)
			{
				// The caps meet the barrel at 90 degrees and must stay hard, or the tube ends bulge.
				WorstCapDeviation = FMath::Max(WorstCapDeviation, (Shaded - Face).Length());
				continue;
			}

			// Averaging two barrel facets can only ever produce a normal square to the axis, so
			// this is exact rather than approximate - and a constant (0, 1, 0) would satisfy it,
			// which is what the outward check below is for.
			WorstOffAxis = FMath::Max(WorstOffAxis, FMath::Abs(Shaded.X));

			const FVector3d Vertex = Tube.GetVertex(Tri[Corner]);
			const FVector3d Radial = FVector3d(0.0, Vertex.Y, Vertex.Z).GetSafeNormal();
			WorstOutward = FMath::Min(WorstOutward, Shaded.Dot(Radial));

			if ((Shaded - Face).Length() > 1e-3)
			{
				++Welded;
			}
		}
	}

	TestTrue(TEXT("A tube's barrel shades square to its own axis"), WorstOffAxis < 1e-5);
	TestTrue(FString::Printf(TEXT("A tube's barrel shades outward, not with the fallback constant (worst %.4f)"),
			WorstOutward),
		WorstOutward > 0.98);
	TestTrue(TEXT("A tube's barrel is welded smooth rather than faceted"), Welded > 0);
	TestTrue(FString::Printf(TEXT("A tube's end caps stay hard (worst deviation %.6f)"), WorstCapDeviation),
		WorstCapDeviation < 1e-5);

	return true;
}

/**
 * A mesh that has been carried around inside a TArray still owns its own attributes.
 *
 * FDynamicMesh3 is self-referential - its attribute set holds a raw back-pointer to the mesh, and
 * every overlay operation reaches through it - while UE relocates same-type TArray elements with a
 * raw Memmove and no constructor. So growing an array of meshes silently leaves every one of those
 * back-pointers aimed at the freed buffer. Nothing crashes at the time, nothing logs, and the next
 * overlay operation reads whatever now lives at that address.
 *
 * Asserted as a pointer comparison rather than by doing something that would trip over it, because
 * reading freed memory is exactly the failure mode: it usually looks like it worked.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFRelocatedMeshAttributesTest,
	"HouseForge.Geometry.RelocatedMeshKeepsItsAttributes", HF_TEST_FLAGS)

bool FHFRelocatedMeshAttributesTest::RunTest(const FString& Parameters)
{
	// No Reserve, so the appends genuinely reallocate and relocate what is already there.
	TArray<FDynamicMesh3> Meshes;
	for (int32 Index = 0; Index < 32; ++Index)
	{
		FDynamicMesh3& Mesh = Meshes.AddDefaulted_GetRef();
		FHFMeshOps::InitialiseMesh(Mesh);
		FHFMeshOps::AppendBox(Mesh, FVector3d(0, 0, 5), FVector3d(5, 5, 5), 0.0,
			EHFSurfaceRole::JoineryCarcass);
	}

	if (!TestTrue(TEXT("The array reallocated at least once"), Meshes.Num() == 32))
	{
		return false;
	}

	int32 Orphaned = 0;
	for (const FDynamicMesh3& Mesh : Meshes)
	{
		if (Mesh.HasAttributes() && Mesh.Attributes()->GetParentMesh() != &Mesh)
		{
			++Orphaned;
		}
	}
	TestTrue(TEXT("A TArray relocation does orphan attribute back-pointers"), Orphaned > 0);

	// The repair, applied where every generator already goes.
	for (FDynamicMesh3& Mesh : Meshes)
	{
		FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	}

	for (int32 Index = 0; Index < Meshes.Num(); ++Index)
	{
		if (Meshes[Index].Attributes()->GetParentMesh() != &Meshes[Index])
		{
			AddError(FString::Printf(
				TEXT("Mesh %d still points its attribute set at another address; every overlay operation on it reads freed memory."),
				Index));
			return false;
		}
	}

	// And the work that back-pointer is needed for actually happened.
	const FDynamicMeshNormalOverlay* Normals = Meshes[0].Attributes()->PrimaryNormals();
	TestTrue(TEXT("A relocated mesh can still be given shading normals"),
		Normals != nullptr && Normals->ElementCount() > 0);

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

	// The caps, specifically. GetVolumeArea integrates along X alone, so a Z-extruded prism's two flat
	// caps contribute exactly nothing to the figure asserted above - they can be wound inside out and
	// every volume, bounds and watertightness check here still passes. Rotating the solid onto its side
	// is what makes them count: a rigid rotation cannot change a consistently-wound solid's volume, and
	// changes the sign of the part of it the caps carry if they disagree with the walls.
	//
	// This is not hypothetical. PolygonTriangulation::TriangulateSimplePolygon defaults
	// bOrientAsHoleFill to TRUE, which winds its output opposite to the input polygon, and taking that
	// default gave exactly this mesh - correct walls, inverted caps - across every generator that caps
	// a triangulated section.
	FDynamicMesh3 OnItsSide = Mesh;
	MeshTransforms::Rotate(OnItsSide, FRotator(90.0, 0.0, 0.0), FVector3d::Zero());
	TestNearlyEqual(TEXT("Turning the prism on its side does not change its volume"),
		TMeshQueries<FDynamicMesh3>::GetVolumeArea(OnItsSide).X, ExpectedVolume, ExpectedVolume * 0.01);

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

	// A door's infill is its leaf AND the frame that leaf is hung in. The frame is buried a few
	// millimetres in the lintel and in the floor, which is deliberate: a member finishing exactly in
	// the plane of the construction around it is two coplanar surfaces fighting for every pixel.
	const FAxisAlignedBox3d LeafBounds = Leaf.GetBounds();
	TestTrue(TEXT("The infill sits within the opening's height plus the frame's embedment"),
		LeafBounds.Min.Z >= -1.0 && LeafBounds.Max.Z <= 211.0);
	TestTrue(TEXT("The frame is buried in the construction rather than flush with it"),
		LeafBounds.Min.Z < -0.01 && LeafBounds.Max.Z > 210.01);

	FHFOpening Window = MakeDoor(200.0, 150.0, 135.0);
	Window.Id = TEXT("Win1");
	Window.Kind = EHFOpeningKind::Window;
	Window.SillHeight = 90.0;

	const FDynamicMesh3 Glazed = FHFGenerators::GenerateOpeningInfill(Window, Wall);
	TestTrue(TEXT("A window produces a frame and glazing"),
		TMeshQueries<FDynamicMesh3>::GetVolumeArea(Glazed).X > 0.0);

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

/**
 * Joining two generated meshes keeps every triangle's surface role.
 *
 * FDynamicMesh3::AppendWithOffsets shifts the incoming polygroups by the target's MaxGroupID, which
 * is right for meshes whose groups are arbitrary partitions and wrong for every mesh in this plugin,
 * where the group IS the surface role. The result of getting it wrong is not a crash or a hole: the
 * geometry lands exactly where it should and simply stops being reachable from the material panel,
 * so it is worth testing at this level rather than only through whatever composed it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFAppendPreservingRolesTest, "HouseForge.Geometry.AppendPreservingRoles", HF_TEST_FLAGS)

bool FHFAppendPreservingRolesTest::RunTest(const FString& Parameters)
{
	auto CountGroup = [](const FDynamicMesh3& Mesh, EHFSurfaceRole Role)
	{
		const int32 Group = FHFMeshOps::GroupForRole(Role);
		int32 Count = 0;
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (Mesh.GetTriangleGroup(Tid) == Group)
			{
				++Count;
			}
		}
		return Count;
	};

	FDynamicMesh3 Carcass;
	FHFMeshOps::InitialiseMesh(Carcass);
	FHFMeshOps::AppendBox(Carcass, FVector3d(0, 0, 0), FVector3d(50, 30, 100), 0.0,
		EHFSurfaceRole::JoineryCarcass);
	FHFMeshOps::ApplyWorldScaleUVs(Carcass);

	FDynamicMesh3 Rail;
	FHFMeshOps::InitialiseMesh(Rail);
	FHFMeshOps::AppendBox(Rail, FVector3d(0, 0, 120), FVector3d(50, 2, 2), 0.0,
		EHFSurfaceRole::MetalHardware);
	FHFMeshOps::ApplyWorldScaleUVs(Rail);

	const int32 CarcassTris = CountGroup(Carcass, EHFSurfaceRole::JoineryCarcass);
	const int32 RailTris = CountGroup(Rail, EHFSurfaceRole::MetalHardware);
	TestTrue(TEXT("Both meshes start out tagged"), CarcassTris > 0 && RailTris > 0);

	FDynamicMesh3 Joined = Carcass;
	FHFMeshOps::AppendPreservingRoles(Joined, Rail);

	TestEqual(TEXT("The carcass keeps every board triangle"),
		CountGroup(Joined, EHFSurfaceRole::JoineryCarcass), CarcassTris);
	TestEqual(TEXT("The rail is still metal after the join"),
		CountGroup(Joined, EHFSurfaceRole::MetalHardware), RailTris);
	TestEqual(TEXT("Nothing else was tagged on the way in"),
		Joined.TriangleCount(), CarcassTris + RailTris);

	// Three-way, because the offset the raw append applies grows with each join: the second
	// appended mesh is the one that showed the defect in a drawer bank of three.
	FDynamicMesh3 Second;
	FHFMeshOps::InitialiseMesh(Second);
	FHFMeshOps::AppendBox(Second, FVector3d(0, 0, 140), FVector3d(50, 2, 2), 0.0,
		EHFSurfaceRole::MetalHardware);
	FHFMeshOps::ApplyWorldScaleUVs(Second);

	FHFMeshOps::AppendPreservingRoles(Joined, Second);
	TestEqual(TEXT("A second join is still metal"),
		CountGroup(Joined, EHFSurfaceRole::MetalHardware), RailTris * 2);

	// And the geometry itself survived intact.
	TestTrue(TEXT("The joined mesh is watertight"), FHFMeshOps::IsClosed(Joined));
	TestTrue(TEXT("The joined mesh is unwrapped"),
		Joined.Attributes() != nullptr && Joined.Attributes()->PrimaryUV() != nullptr
			&& Joined.Attributes()->PrimaryUV()->ElementCount() > 0);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
