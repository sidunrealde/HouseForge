// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFArticulatedActor.h"
#include "Actors/HFElementActors.h"
#include "Actors/HFHouseActor.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Geometry/HFCoplanarScan.h"
#include "Misc/AutomationTest.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFTypes.h"
#include "UDynamicMesh.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Does anything in the flat flash?
 *
 * Every geometric check this plugin had is a check on ONE mesh - watertight, correctly wound, the
 * declared size, the right roles. Z-fighting is a property of a PAIR, and every mesh in a fighting
 * pair passes all of those individually. So 201 tests were green while a person standing in the
 * living room watched the top of every wall strobe, a stippled band run the whole length of the
 * south elevation, and a striped strip crawl up the wall beside the master bedroom door.
 *
 * The cause was one thing said three ways: RCC and masonry were both modelled as if the other were
 * not there. A beam co-linear with the wall below it occupied the top 450 of that wall's own solid,
 * so beam and wall each drew the same two side faces and the same top face. A column on a wall
 * centreline did the same over its full height. Nothing was the wrong size and nothing was in the
 * wrong place; there were simply two of everything.
 *
 * This measures the class rather than those three instances: for the reference flat as it is
 * actually built, no two surfaces may present co-facing faces in the same plane. The answer is an
 * area in square centimetres, so a regression says how much of the flat flashes and where to stand
 * to see it.
 */
namespace HouseForgeCoplanar
{
	/** Removes every HouseForge actor already standing, so the scan sees one flat and not two. */
	void ClearHouseForgeActors(UWorld* World)
	{
		TArray<AActor*> Doomed;

		for (TActorIterator<AHFHouseActor> It(World); It; ++It)
		{
			It->ClearGeometry();
			Doomed.Add(*It);
		}

		for (TActorIterator<AHFElementActor> It(World); It; ++It)
		{
			Doomed.Add(*It);
		}

		for (AActor* Actor : Doomed)
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}
	}

	/**
	 * Every mesh the flat renders, named and placed.
	 *
	 * Fixed shells and moving parts alike: a wardrobe shutter is its own component and can fight
	 * with the carcass behind it just as readily as a wall can fight with its beam.
	 *
	 * The meshes are COPIED out of their components rather than borrowed. A UDynamicMesh hands back
	 * a reference into an object the garbage collector owns, and the scan holds every surface at
	 * once for the length of a whole-flat comparison.
	 */
	void CollectSurfaces(const AHFHouseActor* House, TArray<UE::Geometry::FDynamicMesh3>& OutMeshes,
		TArray<FHFScanSurface>& OutSurfaces)
	{
		if (House == nullptr)
		{
			return;
		}

		// Reserved up front and never grown past: FHFScanSurface holds a pointer INTO OutMeshes, and
		// a reallocation would leave every surface aimed at freed memory.
		int32 ComponentCount = 0;
		for (const TObjectPtr<AActor>& Actor : House->ElementActors)
		{
			if (const AHFElementActor* Element = Cast<AHFElementActor>(Actor))
			{
				ComponentCount += 1;
				if (const AHFArticulatedActor* Articulated = Cast<AHFArticulatedActor>(Element))
				{
					ComponentCount += Articulated->GetPartComponents().Num();
				}
			}
		}

		OutMeshes.Reserve(ComponentCount);
		OutSurfaces.Reserve(ComponentCount);

		auto Take = [&OutMeshes, &OutSurfaces](UDynamicMeshComponent* Component, const FString& Name)
		{
			if (Component == nullptr || Component->GetDynamicMesh() == nullptr)
			{
				return;
			}

			const UE::Geometry::FDynamicMesh3& Mesh = Component->GetDynamicMesh()->GetMeshRef();
			if (Mesh.TriangleCount() == 0)
			{
				return;
			}

			OutMeshes.Add(Mesh);

			FHFScanSurface Surface;
			Surface.Name = Name;
			Surface.Mesh = &OutMeshes.Last();
			Surface.ToWorld = Component->GetComponentTransform();
			OutSurfaces.Add(MoveTemp(Surface));
		};

		for (const TObjectPtr<AActor>& Actor : House->ElementActors)
		{
			const AHFElementActor* Element = Cast<AHFElementActor>(Actor);
			if (Element == nullptr)
			{
				continue;
			}

			const FString Id = Element->ElementId.ToString();
			Take(Element->GetMeshComponent(), Id);

			if (const AHFArticulatedActor* Articulated = Cast<AHFArticulatedActor>(Element))
			{
				const TArray<TObjectPtr<UDynamicMeshComponent>>& Parts = Articulated->GetPartComponents();
				for (int32 Index = 0; Index < Parts.Num(); ++Index)
				{
					const FName PartId = Articulated->Parts.IsValidIndex(Index)
						? Articulated->Parts[Index].PartId
						: FName(*FString::Printf(TEXT("Part%d"), Index));
					Take(Parts[Index], FString::Printf(TEXT("%s.%s"), *Id, *PartId.ToString()));
				}
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCoplanarFlatTest,
	"HouseForge.SampleHouse.NoTwoSurfacesShareAPlane", HF_TEST_FLAGS)

bool FHFCoplanarFlatTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeCoplanar;

	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world"), World))
	{
		return false;
	}

	ClearHouseForgeActors(World);

	AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
	if (!TestNotNull(TEXT("House actor"), House))
	{
		return false;
	}

	House->SetSpec(FHFSampleHouse::Make2BHK());
	House->BuildGeometry();

	TArray<UE::Geometry::FDynamicMesh3> Meshes;
	TArray<FHFScanSurface> Surfaces;
	CollectSurfaces(House, Meshes, Surfaces);

	if (!TestTrue(TEXT("The reference flat built surfaces to scan"), Surfaces.Num() > 50))
	{
		return false;
	}

	const TArray<FHFCoplanarOverlap> Overlaps = FHFCoplanarScan::Find(Surfaces);
	const double ExposedCm2 = FHFCoplanarScan::ExposedAreaCm2(Overlaps);
	const double SealedCm2 = FHFCoplanarScan::TotalAreaCm2(Overlaps) - ExposedCm2;

	int32 Exposed = 0;
	for (const FHFCoplanarOverlap& Overlap : Overlaps)
	{
		Exposed += Overlap.bSealed ? 0 : 1;
	}

	// Logged whether or not this passes, so the sealed figure is visible in a green run too. It is
	// the number that would quietly grow if a room element started lapping somewhere new.
	AddInfo(FString::Printf(
		TEXT("Reference flat: %d exposed pair(s), %.0f cm2; %d sealed pair(s), %.0f cm2 buried in solid."),
		Exposed, ExposedCm2, Overlaps.Num() - Exposed, SealedCm2));

	if (ExposedCm2 > 0.0)
	{
		for (const FString& Line : FHFCoplanarScan::Describe(Overlaps, 25))
		{
			AddError(Line);
		}

		AddError(FString::Printf(
			TEXT("%d surface pair(s) in the reference flat present %.0f cm2 of co-facing coplanar faces that something can see. That is z-fighting: both faces are drawn, the depth test picks a different winner each frame, and the surface strobes as the camera moves. Decide which element owns the plane - structure displaces masonry, a finish has a real thickness - rather than nudging one by an epsilon."),
			Exposed, ExposedCm2));
	}

	// Zero, to within a square centimetre. Not "small" - a visible plane is owned by exactly one
	// element or it is a defect, and a threshold here is how the next one gets in.
	TestEqual(TEXT("Co-facing coplanar overlap the flat can show, in cm2"), ExposedCm2, 0.0, 1.0);

	ClearHouseForgeActors(World);
	return true;
}

#endif
