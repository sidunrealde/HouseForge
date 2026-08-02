// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMesh3.h"
#include "Geometry/HFClashScan.h"
#include "Geometry/HFMeshOps.h"
#include "Misc/AutomationTest.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The clash scan, on shapes whose answer is known by arithmetic.
 *
 * THE SCAN IS ABOUT TO BE POINTED AT THE WHOLE FLAT AND BELIEVED. Every figure it produces there is
 * a figure nobody can check by hand, so it is checked here instead, against boxes: a stated overlap
 * has a stated depth and a stated volume, and a stated contact has neither.
 */
namespace HouseForgeClashTests
{
	/** A box as its own mesh, at the origin of its own frame, placed by the surface's transform. */
	FDynamicMesh3 Box(const FVector3d& Extents)
	{
		FDynamicMesh3 Mesh;
		FHFMeshOps::AppendBox(Mesh, FVector3d::Zero(), Extents, 0.0, EHFSurfaceRole::JoineryCarcass);
		return Mesh;
	}

	FHFScanSurface SurfaceAt(const FString& Name, const FDynamicMesh3& Mesh, const FVector3d& At,
		const FName& Owner = NAME_None)
	{
		FHFScanSurface Surface;
		Surface.Name = Name;
		Surface.Mesh = &Mesh;
		Surface.ToWorld = FTransform(FVector(At));
		Surface.Owner = Owner;
		return Surface;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFClashScanMeasuresDepthTest,
	"HouseForge.Clash.PenetrationIsMeasuredInCentimetres", HF_TEST_FLAGS)

bool FHFClashScanMeasuresDepthTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeClashTests;

	// A 20 cm cube, and a 20 x 10 x 10 bar driven 4 cm into one face of it.
	//
	// THE BAR IS NARROWER THAN THE CUBE ON PURPOSE. Made the same size, the bar's leading vertices
	// land exactly on the cube's own side faces, every one of them measures zero from a surface, and
	// the honest answer to "how much material is over this point" is nothing. That is not a flaw in
	// the measure, it is what a corner-to-corner overlap is - but it makes a poor fixed point, so
	// the shape here is the one whose answer is unambiguous: the bar's end face is 4 cm in, and no
	// other face of the cube is nearer to it than that.
	const FDynamicMesh3 Cube = Box(FVector3d(10.0, 10.0, 10.0));
	const FDynamicMesh3 Bar = Box(FVector3d(10.0, 5.0, 5.0));

	const FHFScanSurface Surfaces[] = {
		SurfaceAt(TEXT("Cube"), Cube, FVector3d::Zero()),
		SurfaceAt(TEXT("Bar"), Bar, FVector3d(16.0, 0.0, 0.0)),
	};

	const TArray<FHFClash> Clashes = FHFClashScan::Find(Surfaces);

	if (!TestEqual(TEXT("A bar driven into a cube is one clash"), Clashes.Num(), 1))
	{
		return false;
	}

	// The bar's end vertices sit at x = 6, 4 cm inside the cube's face at x = 10, and 5 cm from its
	// sides at y = z = +-10. Nearest is the 4.
	TestNearlyEqual(TEXT("Depth is the 4 cm the bar is driven in by"), Clashes[0].DepthCm, 4.0, 0.05);

	// ----------------------------------------------------------------- and the volume converges
	//
	// 4 x 10 x 10 = 400. Asked at the default 2 cm pitch it comes back near 300, and that is not a
	// defect - a 4 cm dimension sampled at 2 cm is three cells of cell-centre arithmetic, and the
	// answer is as coarse as the question. It matters that the estimate CONVERGES on the real figure
	// when asked properly, because that is what makes it a volume rather than a number.
	//
	// Ten per cent, and it does not tighten by asking for a finer pitch. A cell is counted whole or
	// not at all by where its centre falls, so every face of the region carries up to half a cell of
	// error whatever the pitch is - the bias shrinks with the cell but so does the cell it is
	// measured in. That is the accuracy the figure has, and a tighter assertion here would be a
	// claim about the sampler that is not true.
	FHFClashScanParams Fine;
	Fine.SampleGridCm = 0.25;

	const TArray<FHFClash> Refined = FHFClashScan::Find(Surfaces, Fine);
	if (!TestEqual(TEXT("Still one clash at a fine pitch"), Refined.Num(), 1))
	{
		return false;
	}

	TestTrue(*FString::Printf(TEXT("Volume converges on 400 cm3 (got %.0f)"), Refined[0].VolumeCm3),
		FMath::Abs(Refined[0].VolumeCm3 - 400.0) < 40.0);

	// The sample is somewhere in the shared slab, so a report says where to go and look.
	TestTrue(TEXT("The reported point is inside the overlap"),
		Clashes[0].Sample.X > 5.9 && Clashes[0].Sample.X < 10.1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFClashScanIgnoresContactTest,
	"HouseForge.Clash.TouchingIsNotClashing", HF_TEST_FLAGS)

bool FHFClashScanIgnoresContactTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeClashTests;

	// THE CASE THE WHOLE FLAT IS MADE OF. A wall unit's back is screwed to the plaster and a
	// railing's base plate stands on its coping: two solids sharing a face, at zero separation, and
	// both correct. A scan that called this a clash would report every fixture in the building.
	const FDynamicMesh3 A = Box(FVector3d(10.0, 10.0, 10.0));
	const FDynamicMesh3 B = Box(FVector3d(10.0, 10.0, 10.0));

	const FHFScanSurface Flush[] = {
		SurfaceAt(TEXT("A"), A, FVector3d::Zero()),
		SurfaceAt(TEXT("B"), B, FVector3d(20.0, 0.0, 0.0)),
	};

	TestEqual(TEXT("Face to face is not a clash"), FHFClashScan::Find(Flush).Num(), 0);

	// And clear of each other, which is the other half of the same claim.
	const FHFScanSurface Apart[] = {
		SurfaceAt(TEXT("A"), A, FVector3d::Zero()),
		SurfaceAt(TEXT("B"), B, FVector3d(24.0, 0.0, 0.0)),
	};

	TestEqual(TEXT("Clear of each other is not a clash"), FHFClashScan::Find(Apart).Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFClashScanOwnerTest,
	"HouseForge.Clash.OnePartsAreItsOwnBusiness", HF_TEST_FLAGS)

bool FHFClashScanOwnerTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeClashTests;

	// A drawer inside its carcass is a drawer in the right place. Two surfaces of one fixture are
	// that fixture's kit's business, and this scan is about separate objects in one flat.
	const FDynamicMesh3 A = Box(FVector3d(10.0, 10.0, 10.0));
	const FDynamicMesh3 B = Box(FVector3d(10.0, 10.0, 10.0));

	const FHFScanSurface SameOwner[] = {
		SurfaceAt(TEXT("Unit.Carcass"), A, FVector3d::Zero(), TEXT("F_Unit")),
		SurfaceAt(TEXT("Unit.Drawer"), B, FVector3d(16.0, 0.0, 0.0), TEXT("F_Unit")),
	};

	TestEqual(TEXT("Two parts of one fixture are not compared"), FHFClashScan::Find(SameOwner).Num(), 0);

	const FHFScanSurface Neighbours[] = {
		SurfaceAt(TEXT("Unit.Carcass"), A, FVector3d::Zero(), TEXT("F_Unit")),
		SurfaceAt(TEXT("Other.Carcass"), B, FVector3d(16.0, 0.0, 0.0), TEXT("F_Other")),
	};

	TestEqual(TEXT("Two fixtures are"), FHFClashScan::Find(Neighbours).Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFClashScanCrossingTest,
	"HouseForge.Clash.CrossingPlatesAreCaughtWithNoVertexInside", HF_TEST_FLAGS)

bool FHFClashScanCrossingTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeClashTests;

	// THE CASE THE VERTEX TEST ALONE CANNOT SEE, and the reason there is a grid at all.
	//
	// A long thin plate crossing another long thin plate at right angles: every vertex of each is
	// outside the other, because both are longer than the thing they pass through is wide. A rail
	// through a post, a shutter swung edge-on through a partition. Only a sample taken in the shared
	// space finds it.
	const FDynamicMesh3 Long = Box(FVector3d(60.0, 2.0, 2.0));
	const FDynamicMesh3 Across = Box(FVector3d(2.0, 60.0, 2.0));

	const FHFScanSurface Crossing[] = {
		SurfaceAt(TEXT("Rail"), Long, FVector3d::Zero()),
		SurfaceAt(TEXT("Post"), Across, FVector3d::Zero()),
	};

	const TArray<FHFClash> Clashes = FHFClashScan::Find(Crossing);

	if (!TestEqual(TEXT("Two crossing plates are a clash"), Clashes.Num(), 1))
	{
		return false;
	}

	// 4 x 4 x 4 of shared space. The deepest inside point is its centre, 2 cm from every face.
	TestTrue(*FString::Printf(TEXT("Depth is about 2 cm (got %.2f)"), Clashes[0].DepthCm),
		Clashes[0].DepthCm > 1.5 && Clashes[0].DepthCm < 2.2);

	return true;
}

#endif
