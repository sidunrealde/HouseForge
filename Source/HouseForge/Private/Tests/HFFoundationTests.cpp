// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "Generators/GridBoxMeshGenerator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHFRuntimeModuleLoadedTest,
	"HouseForge.Foundation.RuntimeModuleLoaded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHFRuntimeModuleLoadedTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("HouseForge runtime module is loaded"),
		FModuleManager::Get().IsModuleLoaded(TEXT("HouseForge")));

	return true;
}

/**
 * Proves the geometry stack is genuinely linked and usable from this module, not merely listed in
 * Build.cs. Every generator written from feature/geometry-core onward depends on exactly this.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHFGeometryStackTest,
	"HouseForge.Foundation.GeometryStackLinks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHFGeometryStackTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;

	// A 100cm cube centred on the origin, at the coarsest subdivision.
	constexpr double HalfExtent = 50.0;

	FGridBoxMeshGenerator Generator;
	Generator.Box = FOrientedBox3d(FFrame3d(), FVector3d(HalfExtent, HalfExtent, HalfExtent));
	Generator.EdgeVertices = FIndex3i(2, 2, 2);
	Generator.Generate();

	const FDynamicMesh3 Mesh(&Generator);

	TestEqual(TEXT("Box has 8 vertices"), Mesh.VertexCount(), 8);
	TestEqual(TEXT("Box has 12 triangles (2 per face)"), Mesh.TriangleCount(), 12);

	const FAxisAlignedBox3d Bounds = Mesh.GetBounds();
	TestTrue(TEXT("Box bounds match the requested extents"),
		Bounds.Min.Equals(FVector3d(-HalfExtent, -HalfExtent, -HalfExtent), UE_KINDA_SMALL_NUMBER) &&
		Bounds.Max.Equals(FVector3d(HalfExtent, HalfExtent, HalfExtent), UE_KINDA_SMALL_NUMBER));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
