// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Geometry/HFGenerators.h"
#include "Geometry/HFMeshOps.h"
#include "Geometry/HFSectionCut.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ---------------------------------------------------------------------------------------------
//
// The section cut - the thing that turns a top-down render into a plan.
//
// Everything here is measured on the mesh, never on a picture. That is not squeamishness about
// screenshots; it is the only way this can be tested at all. The gate runs -nullrhi, so the
// renderer the cut feeds cannot be asked a single question during a test run. So the cut is held
// to the properties a plan actually depends on - what survives, what disappears, whether the
// opening it leaves is closed again, and whether a doorway is still a hole - all of which are
// properties of geometry.
//
// No triangle counts. A cap is asserted by area and by closure, not by how many triangles the hole
// filler happened to fan it into.
//
// ---------------------------------------------------------------------------------------------

namespace
{
	constexpr double CutHeight = 120.0;

	FDynamicMesh3 SolidBox(const FVector3d& Centre, const FVector3d& Extents, EHFSurfaceRole Role)
	{
		FDynamicMesh3 Mesh;
		FHFMeshOps::InitialiseMesh(Mesh);
		FHFMeshOps::AppendBox(Mesh, Centre, Extents, 0.0, Role);
		return Mesh;
	}

	double Volume(const FDynamicMesh3& Mesh)
	{
		return (Mesh.TriangleCount() == 0) ? 0.0 : TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X;
	}

	/**
	 * Area of the surface facing straight up at a given height.
	 *
	 * This is the one measurement that says whether a camera overhead will see anything at all.
	 * A wall cut open at 1.2 m has its side faces (edge-on from above, no projected area) and its
	 * own underside (pointing away, backface-culled) and nothing else - so an uncapped section
	 * measures zero here, and a capped one measures the wall's plan footprint.
	 */
	double UpwardAreaAtHeight(const FDynamicMesh3& Mesh, double Height, double Tolerance = 0.5)
	{
		double Area = 0.0;
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			const FVector3d Normal = Mesh.GetTriNormal(Tid);
			if (Normal.Z < 0.9)
			{
				continue;
			}

			FVector3d A, B, C;
			Mesh.GetTriVertices(Tid, A, B, C);
			const double MeanZ = (A.Z + B.Z + C.Z) / 3.0;
			if (FMath::Abs(MeanZ - Height) > Tolerance)
			{
				continue;
			}

			Area += 0.5 * ((B - A).Cross(C - A)).Length();
		}
		return Area;
	}

	/** Area of every triangle carrying a given surface role. */
	double AreaOfRole(const FDynamicMesh3& Mesh, EHFSurfaceRole Role)
	{
		const int32 Group = FHFMeshOps::GroupForRole(Role);
		double Area = 0.0;
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (Mesh.GetTriangleGroup(Tid) != Group)
			{
				continue;
			}
			FVector3d A, B, C;
			Mesh.GetTriVertices(Tid, A, B, C);
			Area += 0.5 * ((B - A).Cross(C - A)).Length();
		}
		return Area;
	}

	/** A 4 m run of 230 mm wall, 3 m high, on the X axis from the origin. */
	FHFWall ReferenceWall()
	{
		FHFWall Wall;
		Wall.Id = TEXT("W1");
		Wall.Start = FVector2D(0.0, 0.0);
		Wall.End = FVector2D(400.0, 0.0);
		Wall.Thickness = 23.0;
		Wall.Height = 300.0;
		Wall.BaseZ = 0.0;
		return Wall;
	}

	FHFOpening ReferenceDoor()
	{
		FHFOpening Door;
		Door.Id = TEXT("D1");
		Door.WallId = TEXT("W1");
		Door.OffsetAlongWall = 200.0;
		Door.Width = 90.0;
		Door.Height = 210.0;
		Door.SillHeight = 0.0;
		Door.Kind = EHFOpeningKind::Door;
		return Door;
	}
}

/**
 * The cut removes what is above it and keeps what is below, exactly.
 *
 * Asserted as a volume rather than as a bounding box, because a bounding box would be satisfied by
 * a mesh that had merely been squashed. The remaining solid must be the same prism, 120 cm tall.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSectionRemovesWhatIsAboveTest,
	"HouseForge.Section.CutRemovesWhatIsAboveIt", HF_TEST_FLAGS)

bool FHFSectionRemovesWhatIsAboveTest::RunTest(const FString& Parameters)
{
	const FDynamicMesh3 Box = SolidBox(FVector3d(0.0, 0.0, 150.0), FVector3d(200.0, 11.5, 150.0),
		EHFSurfaceRole::WallPaint);

	FHFSectionCutParams Params;
	Params.CutZ = CutHeight;
	Params.CapRole = EHFSurfaceRole::Structure;

	bool bClosed = false;
	const FDynamicMesh3 Section = FHFSectionCut::CutBelow(Box, Params, &bClosed);

	if (!TestTrue(TEXT("The section has geometry"), Section.TriangleCount() > 0))
	{
		return false;
	}

	const double Expected = 400.0 * 23.0 * CutHeight;
	TestNearlyEqual(TEXT("The section holds exactly the solid below the cut plane"),
		Volume(Section), Expected, Expected * 0.005);

	TestNearlyEqual(TEXT("Nothing survives above the cut plane"),
		Section.GetBounds().Max.Z, CutHeight, 0.01);

	TestNearlyEqual(TEXT("The bottom is where it always was"),
		Section.GetBounds().Min.Z, 0.0, 0.01);

	return true;
}

/**
 * The cut is closed again afterwards, and that closure is the whole point.
 *
 * The failing case is not a hole in a mesh - nobody would notice that in a plan. It is that a
 * camera overhead sees NOTHING where a wall should be: the only faces left are edge-on sides and
 * an underside pointing away from the camera, which is backface-culled. So this measures the area
 * facing up at the cut height, capped and uncapped, and the uncapped figure has to be zero. That
 * comparison is the reason FHFSectionCutParams::bCap defaults to true.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSectionIsCappedTest,
	"HouseForge.Section.TheCutIsCappedOrTheWallIsInvisible", HF_TEST_FLAGS)

bool FHFSectionIsCappedTest::RunTest(const FString& Parameters)
{
	const FDynamicMesh3 Box = SolidBox(FVector3d(0.0, 0.0, 150.0), FVector3d(200.0, 11.5, 150.0),
		EHFSurfaceRole::WallPaint);

	FHFSectionCutParams Capped;
	Capped.CutZ = CutHeight;
	Capped.bCap = true;
	Capped.CapRole = EHFSurfaceRole::Structure;

	bool bClosed = false;
	const FDynamicMesh3 WithCap = FHFSectionCut::CutBelow(Box, Capped, &bClosed);

	FHFSectionCutParams Uncapped = Capped;
	Uncapped.bCap = false;
	bool bUncappedClosed = true;
	const FDynamicMesh3 WithoutCap = FHFSectionCut::CutBelow(Box, Uncapped, &bUncappedClosed);

	TestTrue(TEXT("A capped section is a closed solid"), bClosed);
	TestFalse(TEXT("An uncapped section is left open"), bUncappedClosed);

	const double Footprint = 400.0 * 23.0;

	TestNearlyEqual(TEXT("A capped section presents its whole plan footprint to a camera overhead"),
		UpwardAreaAtHeight(WithCap, CutHeight), Footprint, Footprint * 0.01);

	TestNearlyEqual(TEXT("An uncapped section presents nothing at all, which is why it is not an option"),
		UpwardAreaAtHeight(WithoutCap, CutHeight), 0.0, 0.001);

	// The cap is reachable by the material panel. An untagged face is one nothing can ever
	// re-material, and the whole cut face of every wall in the plan would be untagged.
	TestNearlyEqual(TEXT("The cap carries the role the caller named"),
		AreaOfRole(WithCap, EHFSurfaceRole::Structure), Footprint, Footprint * 0.01);

	return true;
}

/**
 * Roles below the cut are carried through untouched.
 *
 * A wall's skirting sits at 10 cm and a wall cut at 120 keeps all of it. If the cut renumbered
 * polygroups the way a raw mesh append does, the skirting would come out tagged with a group no
 * role maps to - the failure AppendPreservingRoles exists to prevent, arriving by another door.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSectionKeepsRolesBelowTest,
	"HouseForge.Section.RolesBelowTheCutSurvive", HF_TEST_FLAGS)

bool FHFSectionKeepsRolesBelowTest::RunTest(const FString& Parameters)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	// A wall with a band of skirting stuck to its face, both well below the cut.
	FHFMeshOps::AppendBox(Mesh, FVector3d(0.0, 0.0, 150.0), FVector3d(200.0, 11.5, 150.0),
		0.0, EHFSurfaceRole::WallPaint);
	FHFMeshOps::AppendBox(Mesh, FVector3d(0.0, 12.5, 5.0), FVector3d(200.0, 1.0, 5.0),
		0.0, EHFSurfaceRole::Skirting);

	const double SkirtingAreaBefore = AreaOfRole(Mesh, EHFSurfaceRole::Skirting);

	FHFSectionCutParams Params;
	Params.CutZ = CutHeight;
	Params.CapRole = EHFSurfaceRole::Structure;

	const FDynamicMesh3 Section = FHFSectionCut::CutBelow(Mesh, Params);

	const TSet<EHFSurfaceRole> Roles = FHFMeshOps::RolesPresent(Section);
	TestTrue(TEXT("The wall's own role survives the cut"), Roles.Contains(EHFSurfaceRole::WallPaint));
	TestTrue(TEXT("Skirting below the cut survives the cut"), Roles.Contains(EHFSurfaceRole::Skirting));

	TestNearlyEqual(TEXT("Every square centimetre of skirting is still tagged as skirting"),
		AreaOfRole(Section, EHFSurfaceRole::Skirting), SkirtingAreaBefore, SkirtingAreaBefore * 0.01);

	return true;
}

/**
 * A doorway is still a gap after the cut, which is the single thing a plan is read for.
 *
 * Measured as the volume the doorway removes: a 900 mm door in a 230 mm wall takes exactly
 * 90 x 23 x 120 cm out of the section, because the opening runs from the floor past the cut. If
 * the cut welded the doorway's edges shut - which is what bSimplifyAlongNewEdges would risk - the
 * two volumes would agree and the plan would show an unbroken wall across every door.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSectionDoorwayStaysAGapTest,
	"HouseForge.Section.ADoorwayStaysAGap", HF_TEST_FLAGS)

bool FHFSectionDoorwayStaysAGapTest::RunTest(const FString& Parameters)
{
	const FHFWall Wall = ReferenceWall();

	const FDynamicMesh3 Solid = FHFGenerators::GenerateWall(Wall, {});
	const FDynamicMesh3 Doored = FHFGenerators::GenerateWall(Wall, { ReferenceDoor() });

	FHFSectionCutParams Params;
	Params.CutZ = CutHeight;
	Params.CapRole = EHFSurfaceRole::WallPaint;

	const FDynamicMesh3 SolidSection = FHFSectionCut::CutBelow(Solid, Params);
	const FDynamicMesh3 DooredSection = FHFSectionCut::CutBelow(Doored, Params);

	if (!TestTrue(TEXT("Both sections have geometry"),
		SolidSection.TriangleCount() > 0 && DooredSection.TriangleCount() > 0))
	{
		return false;
	}

	const double DoorwayVolume = 90.0 * Wall.Thickness * CutHeight;
	const double Removed = Volume(SolidSection) - Volume(DooredSection);

	TestNearlyEqual(TEXT("The section is missing exactly the doorway"),
		Removed, DoorwayVolume, DoorwayVolume * 0.02);

	// And the gap goes all the way through, so it reads as a way out of the room rather than as a
	// recess: the cap area drops by the doorway's plan footprint too.
	const double GapFootprint = 90.0 * Wall.Thickness;
	TestNearlyEqual(TEXT("The doorway is a gap through the wall, not a niche in it"),
		UpwardAreaAtHeight(SolidSection, CutHeight) - UpwardAreaAtHeight(DooredSection, CutHeight),
		GapFootprint, GapFootprint * 0.05);

	return true;
}

/**
 * A room's ceiling slab disappears and its floor stays.
 *
 * This is the case that rules out the obvious shortcut. Hiding ceiling actors would be a one-line
 * alternative to cutting geometry, and it does not work: AHFRoomActor appends the structural
 * ceiling slab into the SAME mesh as its floor slab and skirting, so hiding the ceiling hides the
 * floor and the plan comes back empty. A cut separates them because it is a cut.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSectionDropsTheCeilingKeepsTheFloorTest,
	"HouseForge.Section.TheCeilingGoesAndTheFloorStays", HF_TEST_FLAGS)

bool FHFSectionDropsTheCeilingKeepsTheFloorTest::RunTest(const FString& Parameters)
{
	FHFRoom Room;
	Room.Id = TEXT("R1");
	Room.Name = TEXT("Living");
	Room.Boundary = { FVector2D(0, 0), FVector2D(400, 0), FVector2D(400, 300), FVector2D(0, 300) };
	Room.CeilingHeight = 300.0;
	Room.SkirtingHeight = 10.0;

	constexpr double Slab = 15.0;

	// The room actor's mesh: floor slab and skirting, plus the structural ceiling slab overhead.
	FDynamicMesh3 RoomMesh = FHFGenerators::GenerateFloor(Room, Slab, FHFSkirting::For(Room, {}, {}, {}));
	const FDynamicMesh3 CeilingSlab = FHFGenerators::GenerateCeilingSlab(Room, Slab);
	FHFMeshOps::AppendPreservingRoles(RoomMesh, CeilingSlab);

	const double FloorAndSkirting = Volume(RoomMesh) - Volume(CeilingSlab);

	FHFSectionCutParams Params;
	Params.CutZ = CutHeight;
	Params.CapRole = EHFSurfaceRole::FloorFinish;

	const FDynamicMesh3 Section = FHFSectionCut::CutBelow(RoomMesh, Params);

	if (!TestTrue(TEXT("The sectioned room still has geometry"), Section.TriangleCount() > 0))
	{
		return false;
	}

	TestTrue(TEXT("The ceiling slab is gone"), Section.GetBounds().Max.Z <= CutHeight + 0.01);

	TestNearlyEqual(TEXT("The floor slab and its skirting are all still there"),
		Volume(Section), FloorAndSkirting, FMath::Max(FloorAndSkirting * 0.01, 1.0));

	TestTrue(TEXT("The floor finish is still a floor finish"),
		FHFMeshOps::RolesPresent(Section).Contains(EHFSurfaceRole::FloorFinish));

	return true;
}

/**
 * A cut above everything changes nothing, and a cut below everything leaves nothing.
 *
 * The two ends of the range. The first matters because a house holds elements shorter than the cut
 * - a kitchen counter, a windowsill - and every one of them must come through whole. The second is
 * what makes a loft, a bulkhead or a high-level cabinet vanish from a plan, as it should.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSectionEndsOfTheRangeTest,
	"HouseForge.Section.AboveEverythingAndBelowEverything", HF_TEST_FLAGS)

bool FHFSectionEndsOfTheRangeTest::RunTest(const FString& Parameters)
{
	// A counter: 90 cm high, entirely below a 120 cm cut.
	const FDynamicMesh3 Counter = SolidBox(FVector3d(0.0, 0.0, 45.0), FVector3d(100.0, 30.0, 45.0),
		EHFSurfaceRole::CounterStone);

	FHFSectionCutParams Params;
	Params.CutZ = CutHeight;

	const FDynamicMesh3 Untouched = FHFSectionCut::CutBelow(Counter, Params);
	TestNearlyEqual(TEXT("Something entirely below the cut comes through whole"),
		Volume(Untouched), Volume(Counter), FMath::Max(Volume(Counter) * 0.001, 0.5));
	TestTrue(TEXT("And is still closed"), FHFMeshOps::IsClosed(Untouched));

	// A loft: 210 to 240 cm, entirely above the cut.
	const FDynamicMesh3 Loft = SolidBox(FVector3d(0.0, 0.0, 225.0), FVector3d(100.0, 30.0, 15.0),
		EHFSurfaceRole::JoineryCarcass);

	const FDynamicMesh3 Vanished = FHFSectionCut::CutBelow(Loft, Params);
	TestEqual(TEXT("Something entirely above the cut leaves nothing behind"),
		Vanished.TriangleCount(), 0);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
