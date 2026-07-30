// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFElementActors.h"
#include "Actors/HFOpeningActor.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Geometry/HFMeshOps.h"
#include "Materials/HFMaterialLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FString RoleName(EHFSurfaceRole Role)
	{
		return StaticEnum<EHFSurfaceRole>()->GetNameStringByValue(static_cast<int64>(Role));
	}

	AHFRoomActor* SpawnTestRoom(UWorld* World)
	{
		AHFRoomActor* Actor = World->SpawnActor<AHFRoomActor>();
		if (Actor == nullptr)
		{
			return nullptr;
		}

		Actor->Room.Id = TEXT("R_Test");
		Actor->Room.Boundary = { FVector2D(0, 0), FVector2D(400, 0), FVector2D(400, 300), FVector2D(0, 300) };
		Actor->Room.FloorZ = 0.0;
		Actor->Room.CeilingHeight = 300.0;
		Actor->Room.SkirtingHeight = 10.0;
		Actor->bGenerateCeilingSlab = true;
		Actor->Regenerate();
		return Actor;
	}

	AHFWallActor* SpawnTestWall(UWorld* World)
	{
		AHFWallActor* Actor = World->SpawnActor<AHFWallActor>();
		if (Actor == nullptr)
		{
			return nullptr;
		}

		Actor->Wall.Id = TEXT("W_Test");
		Actor->Wall.Start = FVector2D(0.0, 0.0);
		Actor->Wall.End = FVector2D(400.0, 0.0);
		Actor->Wall.Thickness = 20.0;
		Actor->Wall.Height = 300.0;
		Actor->Regenerate();
		return Actor;
	}

	/**
	 * A sliding window, because that is the kind with sashes. A fixed window is honestly fixed and
	 * produces no moving parts at all, which would leave the articulated commit path untested.
	 */
	AHFOpeningActor* SpawnTestWindow(UWorld* World)
	{
		AHFOpeningActor* Actor = World->SpawnActor<AHFOpeningActor>();
		if (Actor == nullptr)
		{
			return nullptr;
		}

		Actor->HostWall.Id = TEXT("W1");
		Actor->HostWall.Start = FVector2D(0.0, 0.0);
		Actor->HostWall.End = FVector2D(400.0, 0.0);
		Actor->HostWall.Thickness = 20.0;
		Actor->HostWall.Height = 300.0;

		Actor->Opening.Id = TEXT("WD1");
		Actor->Opening.WallId = TEXT("W1");
		Actor->Opening.OffsetAlongWall = 200.0;
		Actor->Opening.Width = 180.0;
		Actor->Opening.Height = 120.0;
		Actor->Opening.SillHeight = 90.0;
		Actor->Opening.Kind = EHFOpeningKind::SlidingWindow;

		Actor->Regenerate();
		return Actor;
	}

	/** The material ids actually present on a component's mesh. */
	TSet<int32> MaterialIdsOn(const UDynamicMeshComponent* Component)
	{
		TSet<int32> Ids;
		Component->ProcessMesh([&Ids](const FDynamicMesh3& Mesh)
		{
			if (!Mesh.HasAttributes() || !Mesh.Attributes()->HasMaterialID())
			{
				return;
			}
			const FDynamicMeshMaterialAttribute* Attribute = Mesh.Attributes()->GetMaterialID();
			for (const int32 Tid : Mesh.TriangleIndicesItr())
			{
				Ids.Add(Attribute->GetValue(Tid));
			}
		});
		return Ids;
	}

	/** Asserts the slot table is the full role table, in role order. */
	void CheckSlotTable(FAutomationTestBase& Test, const UDynamicMeshComponent* Component, const TCHAR* What)
	{
		const int32 Expected = FHFMeshOps::NumSurfaceRoles();

		if (!Test.TestEqual(*FString::Printf(TEXT("%s has one material slot per surface role"), What),
			Component->GetNumMaterials(), Expected))
		{
			return;
		}

		for (int32 Slot = 0; Slot < Expected; ++Slot)
		{
			const EHFSurfaceRole Role = FHFMeshOps::RoleForMaterialId(Slot);
			Test.TestEqual(
				*FString::Printf(TEXT("%s: slot %d holds the '%s' material"), What, Slot, *RoleName(Role)),
				Component->GetMaterial(Slot), FHFMaterialLibrary::GetPlaceholder(Role));
		}
	}
}

/**
 * The seam this whole slice exists to close: a generated element carrying more than one surface
 * role has to render each of them through its own material slot.
 *
 * A room is the smallest honest case - a floor slab, its skirting and its ceiling soffit are three
 * roles composed onto one component, one of them through AppendPreservingRoles. A wall on its own
 * would pass a slot-count check while proving nothing, because every triangle in it is WallPaint.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFElementMaterialSlotsTest,
	"HouseForge.Materials.GeneratedElementMapsRolesToSlots", HF_TEST_FLAGS)

bool FHFElementMaterialSlotsTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFRoomActor* Room = SpawnTestRoom(World);
	if (!TestNotNull(TEXT("A room actor spawns"), Room))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(Room)) { Room->Destroy(); } };

	UDynamicMeshComponent* Component = Room->GetMeshComponent();
	if (!TestNotNull(TEXT("The room has a dynamic mesh component"), Component))
	{
		return false;
	}

	CheckSlotTable(*this, Component, TEXT("A generated room"));

	const TSet<int32> Ids = MaterialIdsOn(Component);

	// More than one slot in use, and specifically the three the geometry is made of. The scene
	// proxy only splits a mesh into sections at all when the component has more than one material,
	// so a single-slot component renders the lot through slot 0 whatever the mesh says.
	TestTrue(TEXT("A generated room draws through more than one material slot"), Ids.Num() > 1);
	TestEqual(TEXT("A room uses exactly three material slots"), Ids.Num(), 3);

	for (const EHFSurfaceRole Role : { EHFSurfaceRole::FloorFinish, EHFSurfaceRole::Skirting,
		EHFSurfaceRole::CeilingSoffit })
	{
		TestTrue(*FString::Printf(TEXT("The room renders '%s' through its own slot"), *RoleName(Role)),
			Ids.Contains(FHFMeshOps::MaterialIdForRole(Role)));
	}

	// And a wall, which is the case the brief named. One role, so one slot in use out of the full
	// table - but that slot has to be WallPaint's, not slot 0 by coincidence of it being first.
	AHFWallActor* Wall = SpawnTestWall(World);
	if (TestNotNull(TEXT("A wall actor spawns"), Wall))
	{
		ON_SCOPE_EXIT{ if (IsValid(Wall)) { Wall->Destroy(); } };

		CheckSlotTable(*this, Wall->GetMeshComponent(), TEXT("A generated wall"));

		const TSet<int32> WallIds = MaterialIdsOn(Wall->GetMeshComponent());
		TestEqual(TEXT("A plain wall is one surface role"), WallIds.Num(), 1);
		TestTrue(TEXT("A wall renders through the wall paint slot"),
			WallIds.Contains(FHFMeshOps::MaterialIdForRole(EHFSurfaceRole::WallPaint)));
	}

	return true;
}

/**
 * Articulated elements commit their meshes down a completely separate path from
 * AHFElementActor::CommitMesh - one component per moving part, written in a loop that skips any
 * part an artist has edited. A window is the case that matters most here, because its glass is the
 * only translucent role in the set and it is a moving part.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFArticulatedPartMaterialsTest,
	"HouseForge.Materials.MovingPartsAreMaterialledToo", HF_TEST_FLAGS)

bool FHFArticulatedPartMaterialsTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFOpeningActor* Window = SpawnTestWindow(World);
	if (!TestNotNull(TEXT("A window actor spawns"), Window))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(Window)) { Window->Destroy(); } };

	const TArray<TObjectPtr<UDynamicMeshComponent>>& Parts = Window->GetPartComponents();
	if (!TestTrue(TEXT("A sliding window generates its sashes as moving parts"), Parts.Num() > 0))
	{
		return false;
	}

	// The frame is on the actor's own root component and goes down AHFElementActor::CommitMesh; the
	// sashes are on their own components and go down the articulated path. Both have to be dressed,
	// so the root is checked here rather than assumed from the element test above.
	CheckSlotTable(*this, Window->GetMeshComponent(), TEXT("A window's fixed frame"));

	TSet<int32> AcrossAllParts;
	for (const TObjectPtr<UDynamicMeshComponent>& Part : Parts)
	{
		if (!IsValid(Part))
		{
			continue;
		}
		CheckSlotTable(*this, Part, TEXT("A moving part"));
		AcrossAllParts.Append(MaterialIdsOn(Part));
	}

	// The point of the whole exercise, on the one role where getting it wrong is most obvious: a
	// sash whose glass draws through the frame's slot is a boarded-up window.
	TestTrue(TEXT("A window's sashes carry glass on the glass slot"),
		AcrossAllParts.Contains(FHFMeshOps::MaterialIdForRole(EHFSurfaceRole::Glass)));

	TestTrue(TEXT("A window is more than glass"), AcrossAllParts.Num() > 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
