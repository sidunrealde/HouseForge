// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFHouseActor.h"
#include "Actors/HFOpeningActor.h"
#include "Components/DynamicMeshComponent.h"
#include "Actors/HFElementActors.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "HFEditorSubsystem.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Model/HFSettings.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	/**
	 * Everything HouseForge has left in the shared editor world.
	 *
	 * The automation world is shared between tests, and a settings change is deliberately level-wide:
	 * it rebuilds every element of every house it finds. A house another test left behind therefore
	 * lands in this test's counts, which is exactly how the first run of these two failed - the count
	 * came back 1 for somebody else's door while this test's own door had not moved at all.
	 */
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

	/** A house with one wall and one door in it, alone in the editor world. */
	AHFHouseActor* SpawnOneDoorHouse(UWorld* World)
	{
		ClearHouseForgeActors(World);

		FHFHouseSpec Spec;
		Spec.Name = TEXT("Settings Test");
		Spec.Units = EHFUnits::Centimeters;
		Spec.UnitsSource = TEXT("test");

		FHFRoom& Room = Spec.Rooms.AddDefaulted_GetRef();
		Room.Id = TEXT("R1");
		Room.Type = EHFRoomType::Bedroom;
		Room.CeilingHeight = 300.0;
		Room.Boundary = { FVector2D(0, 0), FVector2D(400, 0), FVector2D(400, 350), FVector2D(0, 350) };

		FHFWall& Wall = Spec.Walls.AddDefaulted_GetRef();
		Wall.Id = TEXT("W1");
		Wall.Start = FVector2D(0.0, 0.0);
		Wall.End = FVector2D(400.0, 0.0);
		Wall.Thickness = 20.0;
		Wall.Height = 300.0;

		FHFOpening& Door = Spec.Openings.AddDefaulted_GetRef();
		Door.Id = TEXT("D1");
		Door.WallId = TEXT("W1");
		Door.Kind = EHFOpeningKind::Door;
		Door.Width = 90.0;
		Door.Height = 210.0;
		Door.OffsetAlongWall = 200.0;
		Door.Swing = EHFSwing::InwardLeft;

		AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
		if (House == nullptr)
		{
			return nullptr;
		}

		House->SetSpec(Spec);
		House->BuildGeometry();
		return House;
	}

	AHFOpeningActor* FindOpening(AHFHouseActor* House)
	{
		for (AActor* Element : House->ElementActors)
		{
			if (AHFOpeningActor* Opening = Cast<AHFOpeningActor>(Element))
			{
				return Opening;
			}
		}
		return nullptr;
	}

	double LeafVolume(AHFOpeningActor* Opening)
	{
		double Total = 0.0;
		for (UDynamicMeshComponent* Part : Opening->GetPartComponents())
		{
			if (Part != nullptr)
			{
				Total += TMeshQueries<FDynamicMesh3>::GetVolumeArea(
					Part->GetDynamicMesh()->GetMeshRef()).X;
			}
		}
		return Total;
	}
}

/**
 * The composing layer seeds an opening from the project's settings, and changing a setting rebuilds
 * what is already in the level.
 *
 * Without this the settings page is decorative: a value changes, the ini is written, and nothing
 * anybody can see moves until somebody happens to rebuild the house.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSettingsRebuildTheLevelTest,
	"HouseForge.Settings.ChangingASettingRebuildsTheLevel", HF_TEST_FLAGS)

bool FHFSettingsRebuildTheLevelTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	UHFEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UHFEditorSubsystem>();
	if (!TestNotNull(TEXT("The HouseForge editor subsystem exists"), Subsystem))
	{
		return false;
	}

	UHFSettings* Settings = GetMutableDefault<UHFSettings>();
	if (!TestNotNull(TEXT("The settings CDO exists"), Settings))
	{
		return false;
	}

	const double SavedLeaf = Settings->Door.LeafThickness;

	AHFHouseActor* House = SpawnOneDoorHouse(World);
	if (!TestNotNull(TEXT("A house builds"), House))
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		Settings->Door.LeafThickness = SavedLeaf;
		if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); }
	};

	AHFOpeningActor* Door = FindOpening(House);
	if (!TestNotNull(TEXT("The house built a door"), Door))
	{
		return false;
	}

	// The composing layer seeded the actor, so its own params carry the project's figures.
	TestEqual(TEXT("The door was seeded from the project's settings"),
		Door->BuildParams.Door.LeafThickness, Settings->Door.LeafThickness);

	const double Before = LeafVolume(Door);
	TestTrue(TEXT("The door leaf has volume"), Before > 0.0);

	// Now the user drags the slider. ApplyProjectSettingsToLevel is what OnSettingChanged calls, and
	// calling it directly is what makes the behaviour testable without a Slate interaction.
	Settings->Door.LeafThickness = SavedLeaf * 2.0;
	const int32 Rebuilt = Subsystem->ApplyProjectSettingsToLevel();

	TestTrue(TEXT("At least one element rebuilt"), Rebuilt >= 1);

	TestEqual(TEXT("The door picked up the new thickness"),
		Door->BuildParams.Door.LeafThickness, SavedLeaf * 2.0);

	const double After = LeafVolume(Door);

	// Measured, not merely different: twice the thickness is twice the material, since the leaf's
	// face size is set by the opening and did not change.
	TestEqual(TEXT("Doubling the leaf thickness doubles the leaf"), After, Before * 2.0, Before * 0.01);

	return true;
}

/**
 * A hand-edited element is never overwritten by a settings change.
 *
 * The same protection a house rebuild gives, at the one place it would be easiest to forget: a
 * project-wide setting is exactly the sort of change that feels like it should apply to everything.
 * Overwriting modelling work is silent and unrecoverable - see .claude/rules/04-conventions.md - so
 * a hand-edited element is skipped entirely, its construction figures included, because re-seeding
 * those alone would still change what a later Revert To Generated produced.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSettingsSpareHandEditedElementsTest,
	"HouseForge.Settings.AHandEditedElementIsNotRegenerated", HF_TEST_FLAGS)

bool FHFSettingsSpareHandEditedElementsTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	UHFEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UHFEditorSubsystem>();
	UHFSettings* Settings = GetMutableDefault<UHFSettings>();
	if (!TestNotNull(TEXT("The subsystem exists"), Subsystem)
		|| !TestNotNull(TEXT("The settings CDO exists"), Settings))
	{
		return false;
	}

	const double SavedLeaf = Settings->Door.LeafThickness;

	AHFHouseActor* House = SpawnOneDoorHouse(World);
	if (!TestNotNull(TEXT("A house builds"), House))
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		Settings->Door.LeafThickness = SavedLeaf;
		if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); }
	};

	AHFOpeningActor* Door = FindOpening(House);
	if (!TestNotNull(TEXT("The house built a door"), Door))
	{
		return false;
	}

	const double SeededThickness = Door->BuildParams.Door.LeafThickness;
	const double EditedVolume = LeafVolume(Door);
	TestTrue(TEXT("The door leaf has volume"), EditedVolume > 0.0);

	// Stand in for an artist taking the Modeling Tools to this door. The actor-level flag is what a
	// house rebuild and a settings change both consult.
	Door->bArtistEdited = true;

	Settings->Door.LeafThickness = SavedLeaf * 3.0;
	const int32 Rebuilt = Subsystem->ApplyProjectSettingsToLevel();

	TestEqual(TEXT("Nothing was rebuilt: the only element in the house is hand-edited"), Rebuilt, 0);

	TestEqual(TEXT("A hand-edited door keeps the figures it was built with"),
		Door->BuildParams.Door.LeafThickness, SeededThickness);

	TestEqual(TEXT("A hand-edited door's geometry is untouched"),
		LeafVolume(Door), EditedVolume, 0.001);

	// And reverting is still the way back: it is the one operation allowed to discard hand work, and
	// after it the element builds with whatever figures it is then given.
	Door->RevertToGenerated();
	TestFalse(TEXT("Reverting clears the hand-edited flag"), Door->bArtistEdited);

	const int32 RebuiltAfterRevert = Subsystem->ApplyProjectSettingsToLevel();
	TestEqual(TEXT("Once reverted, the door rebuilds like any other element"), RebuiltAfterRevert, 1);
	TestEqual(TEXT("And it now carries the new figures"),
		Door->BuildParams.Door.LeafThickness, SavedLeaf * 3.0);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
