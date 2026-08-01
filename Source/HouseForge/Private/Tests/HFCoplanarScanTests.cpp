// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Geometry/HFCoplanarScan.h"
#include "Geometry/HFMeshOps.h"
#include "Misc/AutomationTest.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The z-fighting scan, and the cap winding it depends on being able to trust.
 *
 * An instrument that answers "nothing wrong" is indistinguishable from an instrument that is
 * broken, and this one is the only thing standing between the flat and the defect a person had to
 * see for themselves. So it is calibrated against geometry whose answer is known by hand: a beam
 * sitting in the top of a wall, which is the defect itself; a butt joint, which is how the building
 * goes together and must NOT be reported; and the same beam given a real 5 mm of clearance, which
 * is the fix.
 */
namespace HouseForgeCoplanarScanTests
{
	FDynamicMesh3 MakeBox(const FVector3d& Centre, const FVector3d& Extents)
	{
		FDynamicMesh3 Mesh;
		FHFMeshOps::InitialiseMesh(Mesh);
		FHFMeshOps::AppendBox(Mesh, Centre, Extents, 0.0, EHFSurfaceRole::Structure);
		return Mesh;
	}

	/** How much of a mesh's area lies in the plane Z=Height facing the given way, in cm2. */
	double AreaFacing(const FDynamicMesh3& Mesh, double Height, double NormalZ)
	{
		double Area = 0.0;

		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Mesh.GetTriVertices(Tid, A, B, C);

			if (!FMath::IsNearlyEqual(A.Z, Height, 0.01)
				|| !FMath::IsNearlyEqual(B.Z, Height, 0.01)
				|| !FMath::IsNearlyEqual(C.Z, Height, 0.01))
			{
				continue;
			}

			// The engine's own winding normal, not a cross product written out here - a textbook
			// one comes out exactly opposed to it, and this test exists to settle a sign.
			if (Mesh.GetTriNormal(Tid).Z * NormalZ > 0.5)
			{
				Area += VectorUtil::Area(A, B, C);
			}
		}

		return Area;
	}
}

/**
 * A prism's caps face out of the solid - whether or not it has holes in it.
 *
 * AppendPrism and AppendPrismWithHoles apply the SAME cap-winding formula to the output of two
 * DIFFERENT triangulators, and the formula is only right if both hand back triangles wound the same
 * way. Nothing checked that they do, and nothing could have: AppendPrism's own comment records that
 * GetVolumeArea integrates along X only, so a Z-extruded prism's caps contribute exactly zero to
 * the volume it reports. An inverted cap is invisible to every volume, bounds and watertightness
 * assertion in the suite.
 *
 * It is very visible in a room. A peripheral ceiling band with inverted caps shows its soffit only
 * from ABOVE; from underneath, where the room is, the band is a hole through to the plenum.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFPrismCapWindingTest,
	"HouseForge.Geometry.PrismCapsFaceOutOfTheSolidWithHolesOrWithout", HF_TEST_FLAGS)

bool FHFPrismCapWindingTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeCoplanarScanTests;

	const TArray<FVector2D> Outer = {
		FVector2D(0.0, 0.0), FVector2D(200.0, 0.0), FVector2D(200.0, 100.0), FVector2D(0.0, 100.0)
	};
	const TArray<TArray<FVector2D>> Holes = { {
		FVector2D(50.0, 25.0), FVector2D(150.0, 25.0), FVector2D(150.0, 75.0), FVector2D(50.0, 75.0)
	} };

	// ------------------------------------------------------------------------------- no holes
	FDynamicMesh3 Solid;
	FHFMeshOps::InitialiseMesh(Solid);
	TestTrue(TEXT("A plain prism builds"), FHFMeshOps::AppendPrism(Solid, Outer, 10.0, 40.0,
		EHFSurfaceRole::CeilingSoffit));

	TestEqual(TEXT("A plain prism's top cap faces up, in cm2"), AreaFacing(Solid, 40.0, 1.0), 20000.0, 1.0);
	TestEqual(TEXT("A plain prism's top cap has nothing facing down"), AreaFacing(Solid, 40.0, -1.0), 0.0, 1.0);
	TestEqual(TEXT("A plain prism's bottom cap faces down, in cm2"), AreaFacing(Solid, 10.0, -1.0), 20000.0, 1.0);
	TestEqual(TEXT("A plain prism's bottom cap has nothing facing up"), AreaFacing(Solid, 10.0, 1.0), 0.0, 1.0);

	// ------------------------------------------------------------------------------ with a hole
	//
	// Same outline, same heights, one hole through it. The cap area drops by the hole and NOTHING
	// ELSE about the answer may change: this is a ceiling band, and which way its soffit faces is
	// which way a person standing under it sees it.
	FDynamicMesh3 Annulus;
	FHFMeshOps::InitialiseMesh(Annulus);
	TestTrue(TEXT("A prism with a hole builds"),
		FHFMeshOps::AppendPrismWithHoles(Annulus, Outer, Holes, 10.0, 40.0, EHFSurfaceRole::CeilingSoffit));

	const double Expected = 20000.0 - 5000.0;
	TestEqual(TEXT("A holed prism's top cap faces up, in cm2"), AreaFacing(Annulus, 40.0, 1.0), Expected, 1.0);
	TestEqual(TEXT("A holed prism's top cap has nothing facing down"), AreaFacing(Annulus, 40.0, -1.0), 0.0, 1.0);
	TestEqual(TEXT("A holed prism's bottom cap faces down, in cm2"), AreaFacing(Annulus, 10.0, -1.0), Expected, 1.0);
	TestEqual(TEXT("A holed prism's bottom cap has nothing facing up"), AreaFacing(Annulus, 10.0, 1.0), 0.0, 1.0);

	TestTrue(TEXT("A prism with a hole is watertight"), FHFMeshOps::IsClosed(Annulus));

	return true;
}

/** A beam sitting in the top of a wall: two side faces and a top face, twice over. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCoplanarScanFindsItTest,
	"HouseForge.Geometry.CoplanarScanMeasuresACoincidentFace", HF_TEST_FLAGS)

bool FHFCoplanarScanFindsItTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeCoplanarScanTests;

	// Wall: 200 long, 20 thick, 100 tall, standing on Z=0.
	const FDynamicMesh3 Wall = MakeBox(FVector3d(0.0, 0.0, 50.0), FVector3d(100.0, 10.0, 50.0));

	// Beam: the same section, occupying the top 30 of it. Exactly the defect.
	const FDynamicMesh3 Beam = MakeBox(FVector3d(0.0, 0.0, 85.0), FVector3d(100.0, 10.0, 15.0));

	TArray<FHFScanSurface> Surfaces;
	Surfaces.Add({ TEXT("Wall"), &Wall, FTransform::Identity });
	Surfaces.Add({ TEXT("Beam"), &Beam, FTransform::Identity });

	const TArray<FHFCoplanarOverlap> Overlaps = FHFCoplanarScan::Find(Surfaces);

	// Every face of the beam except its soffit, which is buried in the wall: two sides of 200 x 30,
	// a top of 200 x 20, and both 20 x 30 ends. The ends count - the beam runs the whole length of
	// this wall, so its end faces lie in the wall's own end faces rather than inside the masonry.
	TestEqual(TEXT("The beam and the wall are one reported pair"), Overlaps.Num(), 1);
	TestEqual(TEXT("Co-facing overlap between a beam and the wall it sits in, in cm2"),
		FHFCoplanarScan::TotalAreaCm2(Overlaps),
		2.0 * 200.0 * 30.0 + 200.0 * 20.0 + 2.0 * 20.0 * 30.0, 1.0);

	if (Overlaps.Num() == 1)
	{
		TestEqual(TEXT("Two coincident faces are reported as touching"), Overlaps[0].SeparationCm, 0.0, 1e-6);
	}

	return true;
}

/**
 * A butt joint is not a defect, and must never be reported as one.
 *
 * Two solids meeting face to face are coincident and correct - a wall built up to the beam soffit
 * above it, a plinth standing on the floor. Exactly one of the pair faces any given camera and the
 * other is culled, so nothing flashes. A scan that cannot tell this from a fight would fail on
 * every correctly-built junction in the flat, and the only way to make it pass would be to pull the
 * building apart.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCoplanarScanIgnoresButtJointTest,
	"HouseForge.Geometry.CoplanarScanIgnoresAButtJoint", HF_TEST_FLAGS)

bool FHFCoplanarScanIgnoresButtJointTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeCoplanarScanTests;

	// Wall to Z=70, beam from Z=70 to Z=100. They share the plane at Z=70, facing each other.
	const FDynamicMesh3 Wall = MakeBox(FVector3d(0.0, 0.0, 35.0), FVector3d(100.0, 10.0, 35.0));
	const FDynamicMesh3 Beam = MakeBox(FVector3d(0.0, 0.0, 85.0), FVector3d(100.0, 10.0, 15.0));

	TArray<FHFScanSurface> Surfaces;
	Surfaces.Add({ TEXT("Wall"), &Wall, FTransform::Identity });
	Surfaces.Add({ TEXT("Beam"), &Beam, FTransform::Identity });

	const TArray<FHFCoplanarOverlap> Overlaps = FHFCoplanarScan::Find(Surfaces);

	TestEqual(TEXT("A butt joint is not z-fighting"), FHFCoplanarScan::TotalAreaCm2(Overlaps), 0.0, 1e-6);
	return true;
}

/** A real separation is not a fight, and half a millimetre is not a real separation. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCoplanarScanSeparationTest,
	"HouseForge.Geometry.CoplanarScanKnowsAThicknessFromAnEpsilon", HF_TEST_FLAGS)

bool FHFCoplanarScanSeparationTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeCoplanarScanTests;

	const FDynamicMesh3 Wall = MakeBox(FVector3d(0.0, 0.0, 50.0), FVector3d(100.0, 10.0, 50.0));

	// Set back 5 mm on every face - a real, buildable clearance a joiner would work to. Every face:
	// leaving the ends coincident leaves 1140 cm2 of fight, which is the answer this originally got.
	const FDynamicMesh3 Clear = MakeBox(FVector3d(0.0, 0.0, 84.5), FVector3d(99.5, 9.5, 15.0));

	TArray<FHFScanSurface> Cleared;
	Cleared.Add({ TEXT("Wall"), &Wall, FTransform::Identity });
	Cleared.Add({ TEXT("Beam"), &Clear, FTransform::Identity });
	TestEqual(TEXT("5 mm of clearance is not z-fighting"),
		FHFCoplanarScan::TotalAreaCm2(FHFCoplanarScan::Find(Cleared)), 0.0, 1e-6);

	// A hundredth of a millimetre is not a clearance, it is a nudge, and the depth buffer cannot
	// tell it from nothing. It must still be caught.
	const FDynamicMesh3 Nudged = MakeBox(FVector3d(0.0, 0.0, 85.0 - 0.001),
		FVector3d(100.0 - 0.001, 10.0 - 0.001, 15.0));

	TArray<FHFScanSurface> Fudged;
	Fudged.Add({ TEXT("Wall"), &Wall, FTransform::Identity });
	Fudged.Add({ TEXT("Beam"), &Nudged, FTransform::Identity });
	TestTrue(TEXT("An epsilon nudge is still z-fighting"),
		FHFCoplanarScan::TotalAreaCm2(FHFCoplanarScan::Find(Fudged)) > 1000.0);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
