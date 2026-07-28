// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFElementActors.h"
#include "Actors/HFHouseActor.h"
#include "Components/DynamicMeshComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "HFEditorSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	/** A wall actor standing alone in the editor world, for testing regeneration behaviour. */
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
}

/**
 * Generated geometry has to stay editable by an artist afterwards, and staying editable is
 * worthless if a rebuild silently overwrites the work. These are UDynamicMeshComponents so the
 * Modeling Tools can be taken to them; this proves an edit survives.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFArtistEditSurvivesTest,
	"HouseForge.Editor.ArtistEditsSurviveRegeneration", HF_TEST_FLAGS)

bool FHFArtistEditSurvivesTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFWallActor* Wall = SpawnTestWall(World);
	if (!TestNotNull(TEXT("A wall actor spawns"), Wall))
	{
		return false;
	}

	ON_SCOPE_EXIT{ if (IsValid(Wall)) { Wall->Destroy(); } };

	UDynamicMeshComponent* Component = Wall->GetMeshComponent();
	if (!TestNotNull(TEXT("The wall has a dynamic mesh component"), Component))
	{
		return false;
	}

	const int32 GeneratedTriangles = Component->GetDynamicMesh()->GetMeshRef().TriangleCount();
	TestTrue(TEXT("The wall generated geometry"), GeneratedTriangles > 0);
	TestFalse(TEXT("A freshly generated wall is not marked as hand-edited"), Wall->bArtistEdited);

	// Stand in for the Modeling Tools: change the mesh through the component, exactly as any
	// editor tool would, and let the component's change notification fire.
	Component->GetDynamicMesh()->EditMesh([](FDynamicMesh3& EditMesh)
	{
		const int32 A = EditMesh.AppendVertex(FVector3d(0, 0, 1000));
		const int32 B = EditMesh.AppendVertex(FVector3d(50, 0, 1000));
		const int32 C = EditMesh.AppendVertex(FVector3d(0, 50, 1000));
		EditMesh.AppendTriangle(A, B, C, 1);
	});
	Component->NotifyMeshUpdated();

	TestTrue(TEXT("Editing the mesh marks the element as hand-edited"), Wall->bArtistEdited);

	const int32 EditedTriangles = Component->GetDynamicMesh()->GetMeshRef().TriangleCount();
	TestEqual(TEXT("The edit added a triangle"), EditedTriangles, GeneratedTriangles + 1);

	// The load-bearing assertion: regenerating must leave the edit alone.
	Wall->Regenerate();
	TestEqual(TEXT("Regeneration does not overwrite hand-edited geometry"),
		Component->GetDynamicMesh()->GetMeshRef().TriangleCount(), EditedTriangles);

	// Changing a parameter must not overwrite it either - that path goes through the same guard.
	Wall->Wall.Thickness = 40.0;
	Wall->Regenerate();
	TestEqual(TEXT("A parameter change does not overwrite hand-edited geometry"),
		Component->GetDynamicMesh()->GetMeshRef().TriangleCount(), EditedTriangles);

	// Reverting is the explicit way back, and it must honour the new parameter.
	Wall->RevertToGenerated();
	TestFalse(TEXT("Reverting clears the hand-edited flag"), Wall->bArtistEdited);
	TestEqual(TEXT("Reverting restores generated geometry"),
		Component->GetDynamicMesh()->GetMeshRef().TriangleCount(), GeneratedTriangles);

	// And regeneration works normally again afterwards.
	Wall->Wall.Height = 250.0;
	Wall->Regenerate();
	const FAxisAlignedBox3d Bounds = Component->GetDynamicMesh()->GetMeshRef().GetBounds();
	TestNearlyEqual(TEXT("Regeneration resumes after a revert"), Bounds.Depth(), 250.0, 0.01);

	return true;
}

/** A house-level rebuild must respect the per-element flag, or the flag protects nothing. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFHouseRebuildPreservesEditsTest,
	"HouseForge.Editor.HouseRebuildPreservesArtistEdits", HF_TEST_FLAGS)

bool FHFHouseRebuildPreservesEditsTest::RunTest(const FString& Parameters)
{
	UHFEditorSubsystem* Editor = GEditor ? GEditor->GetEditorSubsystem<UHFEditorSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("Editor subsystem exists"), Editor))
	{
		return false;
	}

	const FString SpecJson = TEXT(R"JSON(
{
  "schemaVersion": 1, "name": "Rebuild Test", "units": "Centimeters",
  "unitsSource": "test fixture",
  "walls": [
    { "id": "W_S", "start": {"x":0,"y":0}, "end": {"x":400,"y":0}, "thickness": 20, "height": 300 },
    { "id": "W_E", "start": {"x":400,"y":0}, "end": {"x":400,"y":300}, "thickness": 20, "height": 300 },
    { "id": "W_N", "start": {"x":400,"y":300}, "end": {"x":0,"y":300}, "thickness": 20, "height": 300 },
    { "id": "W_W", "start": {"x":0,"y":300}, "end": {"x":0,"y":0}, "thickness": 20, "height": 300 }
  ],
  "rooms": [
    { "id": "R1", "name": "Room", "type": "Living",
      "boundary": [{"x":0,"y":0},{"x":400,"y":0},{"x":400,"y":300},{"x":0,"y":300}],
      "ceilingHeight": 300, "skirtingHeight": 10 }
  ]
}
)JSON");

	if (!TestTrue(TEXT("The spec applies"), Editor->ApplySpecJson(SpecJson, FString()).bSuccess))
	{
		return false;
	}

	AHFHouseActor* House = Editor->FindHouseActor();
	if (!TestNotNull(TEXT("A house was built"), House))
	{
		return false;
	}

	// Find a generated wall and edit it.
	AHFWallActor* EditedWall = nullptr;
	for (AActor* Element : House->ElementActors)
	{
		if (AHFWallActor* Candidate = Cast<AHFWallActor>(Element))
		{
			EditedWall = Candidate;
			break;
		}
	}
	if (!TestNotNull(TEXT("The house contains wall actors"), EditedWall))
	{
		return false;
	}

	UDynamicMeshComponent* Component = EditedWall->GetMeshComponent();
	Component->GetDynamicMesh()->EditMesh([](FDynamicMesh3& EditMesh)
	{
		const int32 A = EditMesh.AppendVertex(FVector3d(0, 0, 2000));
		const int32 B = EditMesh.AppendVertex(FVector3d(50, 0, 2000));
		const int32 C = EditMesh.AppendVertex(FVector3d(0, 50, 2000));
		EditMesh.AppendTriangle(A, B, C, 1);
	});
	Component->NotifyMeshUpdated();

	TestTrue(TEXT("The wall is marked as hand-edited"), EditedWall->bArtistEdited);
	const int32 EditedTriangles = Component->GetDynamicMesh()->GetMeshRef().TriangleCount();

	// Rebuilding the whole house must not destroy and respawn the edited wall.
	House->BuildGeometry();

	TestTrue(TEXT("The edited wall actor survives a house rebuild"), IsValid(EditedWall));
	if (IsValid(EditedWall))
	{
		TestTrue(TEXT("It is still marked as hand-edited"), EditedWall->bArtistEdited);
		TestEqual(TEXT("Its geometry is untouched"),
			EditedWall->GetMeshComponent()->GetDynamicMesh()->GetMeshRef().TriangleCount(), EditedTriangles);
		TestTrue(TEXT("It is still owned by the house"), House->ElementActors.Contains(EditedWall));
	}

	// Everything else was rebuilt as normal.
	int32 WallCount = 0;
	for (AActor* Element : House->ElementActors)
	{
		WallCount += Cast<AHFWallActor>(Element) != nullptr ? 1 : 0;
	}
	TestEqual(TEXT("The rebuild produced one actor per wall, with no duplicates"), WallCount, 4);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
