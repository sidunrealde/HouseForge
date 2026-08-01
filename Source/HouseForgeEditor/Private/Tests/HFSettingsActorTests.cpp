// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFHouseActor.h"
#include "Actors/HFOpeningActor.h"
#include "Actors/HFFanActor.h"
#include "Actors/HFWardrobeActor.h"
#include "Components/DynamicMeshComponent.h"
#include "Actors/HFElementActors.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "HFEditorSubsystem.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Model/HFSettings.h"
#include "Model/HFSpecSerializer.h"
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

	/**
	 * The same house with a wardrobe standing in it, and NO shutter count on the fixture.
	 *
	 * Both halves matter. A house with only a door in it cannot catch a joinery setting that never
	 * reaches a wardrobe, which is exactly why the door test above passed while every joinery
	 * control on the page was inert on anything already in the level. And leaving ShutterCount at
	 * zero is what puts the bay count on the project's module width, which is the figure that used
	 * to freeze at composition time.
	 */
	AHFHouseActor* SpawnOneWardrobeHouse(UWorld* World)
	{
		ClearHouseForgeActors(World);

		FHFHouseSpec Spec;
		Spec.Name = TEXT("Wardrobe Settings Test");
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

		FHFFixture& Fixture = Spec.Fixtures.AddDefaulted_GetRef();
		Fixture.Id = TEXT("F1");
		Fixture.RoomId = TEXT("R1");
		Fixture.Type = EHFFixtureType::Wardrobe;
		Fixture.Label = TEXT("Wardrobe");
		Fixture.Position = FVector2D(200.0, 40.0);
		Fixture.Footprint = FVector2D(240.0, 60.0);
		Fixture.Height = 240.0;
		Fixture.AnchorWallId = TEXT("W1");
		Fixture.Params.ShutterCount = 0;
		Fixture.Params.PlinthHeight = 10.0;

		AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
		if (House == nullptr)
		{
			return nullptr;
		}

		House->SetSpec(Spec);
		House->BuildGeometry();
		return House;
	}

	/**
	 * A room with a templated ceiling over it, a beam round its edge, and a fan hanging in it.
	 *
	 * All three, because the thing under test is that they move TOGETHER. A ceiling on its own
	 * would prove that the drop changed and say nothing about the rotor left inside it; a fan on
	 * its own has nothing over it to be swallowed by.
	 */
	AHFHouseActor* SpawnOneCeilingHouse(UWorld* World, EHFCeilingTemplate Template)
	{
		ClearHouseForgeActors(World);

		FHFHouseSpec Spec;
		Spec.Name = TEXT("Ceiling Settings Test");
		Spec.Units = EHFUnits::Centimeters;
		Spec.UnitsSource = TEXT("test");

		FHFRoom& Room = Spec.Rooms.AddDefaulted_GetRef();
		Room.Id = TEXT("R1");
		Room.Type = EHFRoomType::Bedroom;
		Room.CeilingHeight = 300.0;
		Room.Boundary = { FVector2D(0, 0), FVector2D(500, 0), FVector2D(500, 400), FVector2D(0, 400) };

		FHFWall& Wall = Spec.Walls.AddDefaulted_GetRef();
		Wall.Id = TEXT("W1");
		Wall.Start = FVector2D(0.0, 0.0);
		Wall.End = FVector2D(500.0, 0.0);
		Wall.Thickness = 11.5;
		Wall.Height = 300.0;

		// The reference flat's own section: a 23 beam over an 11.5 partition stands proud of it and
		// therefore shows in the room, which is what puts a ring on the ceiling.
		FHFBeam& Beam = Spec.Beams.AddDefaulted_GetRef();
		Beam.Id = TEXT("BM1");
		Beam.Start = FVector2D(0.0, 0.0);
		Beam.End = FVector2D(500.0, 0.0);
		Beam.Width = 23.0;
		Beam.Depth = 45.0;
		Beam.SoffitZ = 300.0;

		FHFFalseCeiling& Ceiling = Spec.FalseCeilings.AddDefaulted_GetRef();
		Ceiling.Id = TEXT("FC1");
		Ceiling.RoomId = TEXT("R1");
		Ceiling.Template = Template;

		// Dead centre, where a band style leaves the room open to the slab and a full drop does not.
		FHFFixture& Fan = Spec.Fixtures.AddDefaulted_GetRef();
		Fan.Id = TEXT("FAN1");
		Fan.RoomId = TEXT("R1");
		Fan.Type = EHFFixtureType::CeilingFan;
		Fan.Label = TEXT("Ceiling fan");
		Fan.Position = FVector2D(250.0, 200.0);
		Fan.Footprint = FVector2D(120.0, 120.0);
		Fan.Height = 30.0;

		AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
		if (House == nullptr)
		{
			return nullptr;
		}

		House->SetSpec(Spec);
		House->BuildGeometry();
		return House;
	}

	AHFCeilingActor* FindCeiling(AHFHouseActor* House)
	{
		for (AActor* Element : House->ElementActors)
		{
			if (AHFCeilingActor* Ceiling = Cast<AHFCeilingActor>(Element))
			{
				return Ceiling;
			}
		}
		return nullptr;
	}

	AHFFanActor* FindFan(AHFHouseActor* House)
	{
		for (AActor* Element : House->ElementActors)
		{
			if (AHFFanActor* Fan = Cast<AHFFanActor>(Element))
			{
				return Fan;
			}
		}
		return nullptr;
	}

	AHFWardrobeActor* FindWardrobe(AHFHouseActor* House)
	{
		for (AActor* Element : House->ElementActors)
		{
			if (AHFWardrobeActor* Wardrobe = Cast<AHFWardrobeActor>(Element))
			{
				return Wardrobe;
			}
		}
		return nullptr;
	}

	/** Volume of the fixed shell - the carcass, plinth, shelves and cornice, without the leaves. */
	double ShellVolume(AHFElementActor* Element)
	{
		UDynamicMeshComponent* Component = Element->GetMeshComponent();
		return Component != nullptr
			? TMeshQueries<FDynamicMesh3>::GetVolumeArea(Component->GetDynamicMesh()->GetMeshRef()).X
			: 0.0;
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

/**
 * The validation limits on the settings page have to reach the tools that validate.
 *
 * FHFSpecValidator takes its limits as an argument and never looks them up - that is what keeps it
 * testable - so something has to do the looking up, and if nothing does, the Validation section of
 * the settings page is decorative. It was: all three entry points in this subsystem called
 * Validate(Spec) with no limits, so a project could raise its headroom floor to 250 and every spec
 * with 240 of clear height would still be waved through.
 *
 * Asserted through ValidateSpecJson because that is the MCP tool Claude actually calls, rather than
 * through the validator directly - the defect was never in the validator.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSettingsValidationLimitsReachTheToolsTest,
	"HouseForge.Settings.ValidationLimitsReachTheValidateTool", HF_TEST_FLAGS)

bool FHFSettingsValidationLimitsReachTheToolsTest::RunTest(const FString& Parameters)
{
	UHFEditorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UHFEditorSubsystem>() : nullptr;
	UHFSettings* Settings = GetMutableDefault<UHFSettings>();
	if (!TestNotNull(TEXT("The subsystem exists"), Subsystem)
		|| !TestNotNull(TEXT("The settings CDO exists"), Settings))
	{
		return false;
	}

	const double SavedHeadroom = Settings->Validation.MinHeadroomCm;
	ON_SCOPE_EXIT
	{
		Settings->Validation.MinHeadroomCm = SavedHeadroom;
	};

	// A 3 m room with a 60 cm false ceiling in it: 240 of clear height.
	FHFHouseSpec Spec;
	Spec.Name = TEXT("Headroom");
	Spec.Units = EHFUnits::Centimeters;
	Spec.UnitsSource = TEXT("test");

	FHFRoom& Room = Spec.Rooms.AddDefaulted_GetRef();
	Room.Id = TEXT("R1");
	Room.Type = EHFRoomType::Bedroom;
	Room.CeilingHeight = 300.0;
	Room.FloorZ = 0.0;
	Room.Boundary = { FVector2D(0, 0), FVector2D(400, 0), FVector2D(400, 350), FVector2D(0, 350) };

	FHFFalseCeiling& Ceiling = Spec.FalseCeilings.AddDefaulted_GetRef();
	Ceiling.Id = TEXT("FC1");
	Ceiling.RoomId = TEXT("R1");
	Ceiling.Drop = 60.0;

	FString Json;
	FString Error;
	if (!TestTrue(TEXT("The spec serialises"), FHFSpecSerializer::ToJsonString(Spec, Json, Error)))
	{
		return false;
	}

	// At the shipped 210 floor, 240 of clear height is not worth mentioning.
	Settings->Validation.MinHeadroomCm = 210.0;
	TestFalse(TEXT("At the shipped 210 limit the tool says nothing about headroom"),
		Subsystem->ValidateSpecJson(Json).Message.Contains(TEXT("LowHeadroom")));

	// A project building to a taller slab raises the floor, and the tool has to change its answer -
	// which it only can if it read the setting.
	Settings->Validation.MinHeadroomCm = 250.0;
	TestTrue(TEXT("Raising the project's headroom floor to 250 makes the tool report the same spec"),
		Subsystem->ValidateSpecJson(Json).Message.Contains(TEXT("LowHeadroom")));

	return true;
}

/**
 * Dragging a joinery figure moves a wardrobe that is already standing in the level.
 *
 * The door test above cannot catch this and never could: its house holds one wall and one door, so
 * the only element ApplyProjectSettingsToLevel can find is an opening. Every joinery control on the
 * page was therefore inert on anything already built - the flat's doors rebuilt when a figure
 * changed and its wardrobes did not - while the page said in writing that they did.
 *
 * Measured on the BUILT MESH rather than on the parameter struct, which is the only assertion a
 * faithfully-copied-and-then-ignored figure cannot pass. Two figures, because they failed for two
 * different reasons: CarcassBoardThickness never reached the actor at all, and ShutterModuleWidth
 * reached it once and then froze, having been derived into a bay count at composition time.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFJoineryFigureRebuildsAWardrobeTest,
	"HouseForge.Settings.ChangingAJoineryFigureRebuildsAWardrobe", HF_TEST_FLAGS)

bool FHFJoineryFigureRebuildsAWardrobeTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	UHFEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UHFEditorSubsystem>();
	UHFSettings* Settings = GetMutableDefault<UHFSettings>();
	if (!TestNotNull(TEXT("The HouseForge editor subsystem exists"), Subsystem)
		|| !TestNotNull(TEXT("The settings CDO exists"), Settings))
	{
		return false;
	}

	const double SavedBoard = Settings->CarcassBoardThickness;
	const double SavedModule = Settings->ShutterModuleWidth;

	AHFHouseActor* House = SpawnOneWardrobeHouse(World);
	if (!TestNotNull(TEXT("A house builds"), House))
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		Settings->CarcassBoardThickness = SavedBoard;
		Settings->ShutterModuleWidth = SavedModule;
		if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); }
	};

	AHFWardrobeActor* Wardrobe = FindWardrobe(House);
	if (!TestNotNull(TEXT("The house built a wardrobe"), Wardrobe))
	{
		return false;
	}

	TestEqual(TEXT("The wardrobe was seeded from the project's settings"),
		Wardrobe->Wardrobe.Joinery.CarcassBoardThickness, Settings->CarcassBoardThickness);

	// A 240 run at the shipped 45 module is 5.33 bays, which sets out at 5.
	const int32 BaysBefore = Wardrobe->NumParts();
	TestEqual(TEXT("An uncounted run is divided at the project's module width"), BaysBefore, 5);

	const double VolumeBefore = ShellVolume(Wardrobe);
	TestTrue(TEXT("The carcass has volume"), VolumeBefore > 0.0);

	// The user drags the board thickness. Thicker boards, more carcass - a change nothing but the
	// mesh can report, and one that stayed at zero for as long as this branch was missing.
	Settings->CarcassBoardThickness = SavedBoard * 2.0;
	TestTrue(TEXT("Changing a joinery figure rebuilds something"),
		Subsystem->ApplyProjectSettingsToLevel() > 0);

	Wardrobe = FindWardrobe(House);
	if (!TestNotNull(TEXT("The wardrobe survives the rebuild"), Wardrobe))
	{
		return false;
	}

	TestEqual(TEXT("The wardrobe was re-seeded from the changed settings"),
		Wardrobe->Wardrobe.Joinery.CarcassBoardThickness, SavedBoard * 2.0);
	TestTrue(TEXT("Doubling the board thickness puts more material in the carcass"),
		ShellVolume(Wardrobe) > VolumeBefore * 1.05);

	// And the module width, which is the figure the composing layer used to consume and discard. An
	// 80 module divides the same 240 run into 3, so the run comes back with two fewer leaves on it.
	Settings->ShutterModuleWidth = 80.0;
	Subsystem->ApplyProjectSettingsToLevel();

	Wardrobe = FindWardrobe(House);
	if (!TestNotNull(TEXT("The wardrobe survives the second rebuild"), Wardrobe))
	{
		return false;
	}

	TestEqual(TEXT("Widening the module re-divides a wardrobe already in the level"),
		Wardrobe->NumParts(), 3);

	return true;
}

/**
 * The False Ceilings page reaches a ceiling already standing in a level - and takes the fans with it.
 *
 * TWO FAILURES IN ONE, and the second is the one that would not have been noticed. Every other
 * section on this page changes one element in place, so a branch that re-seeds that element is the
 * whole fix. A ceiling figure is different: a ceiling fan hangs from the structural SLAB and its rod
 * is lengthened to reach past whatever the false ceiling puts between it and the room, so deepening
 * a ceiling and rebuilding only the ceiling leaves the fan with the rod it had. That is the rotor
 * built inside the plasterboard - the defect the rod resolution exists to prevent - reached by
 * dragging a slider instead of by writing a spec.
 *
 * Measured on the built mesh wherever a mesh can show it. A figure faithfully copied onto a
 * parameter struct and then ignored is what this whole family of tests exists to catch.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCeilingFigureRebuildsCeilingAndFanTest,
	"HouseForge.Settings.ChangingACeilingFigureMovesTheFanWithIt", HF_TEST_FLAGS)

bool FHFCeilingFigureRebuildsCeilingAndFanTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	UHFEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UHFEditorSubsystem>();
	UHFSettings* Settings = GetMutableDefault<UHFSettings>();
	if (!TestNotNull(TEXT("The HouseForge editor subsystem exists"), Subsystem)
		|| !TestNotNull(TEXT("The settings CDO exists"), Settings))
	{
		return false;
	}

	const FHFCeilingDefaults SavedCeiling = Settings->Ceiling;

	AHFHouseActor* House = SpawnOneCeilingHouse(World, EHFCeilingTemplate::PlainBand);
	if (!TestNotNull(TEXT("A house with a ceiling builds"), House))
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		Settings->Ceiling = SavedCeiling;
		if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); }
	};

	AHFCeilingActor* Ceiling = FindCeiling(House);
	AHFFanActor* Fan = FindFan(House);
	if (!TestNotNull(TEXT("The house built a ceiling"), Ceiling)
		|| !TestNotNull(TEXT("The house built a fan"), Fan))
	{
		return false;
	}

	TestEqual(TEXT("The ceiling was resolved from the project's template figures"),
		Ceiling->Ceiling.Drop, Settings->Ceiling.BandDrop);
	TestEqual(TEXT("...and its band too"),
		Ceiling->Ceiling.BandWidth, Settings->Ceiling.BandWidth);
	TestTrue(TEXT("...and it got a ring, because a beam shows in the room"),
		Ceiling->Ceiling.HasPerimeterBulkhead());

	// THE LOWEST POINT OF THIS MESH IS THE RING, NOT THE BAND, and that caught the first version of
	// this test out. The perimeter bulkhead is sized from the beam it buries, so dragging the band
	// figures leaves the mesh's minimum Z exactly where it was - which is correct, and says nothing
	// at all about whether the band moved. Volume is what the band shows in: a deeper, wider band is
	// more plasterboard, and a figure copied onto the struct without regenerating changes none of it.
	const double VolumeBefore = ShellVolume(Ceiling);
	const double RingSoffitBefore = Ceiling->GetMeshComponent()->GetDynamicMesh()->GetMeshRef().GetBounds().Min.Z;

	TestTrue(TEXT("The ceiling has volume"), VolumeBefore > 0.0);
	TestTrue(TEXT("The ceiling has a run of downlights"), Ceiling->Ceiling.LightPositions.Num() > 0);

	// ------------------------------------------------------------------ the user drags the band
	//
	// Deeper and wider. The drop is measured on the MESH: a band re-seeded onto the parameter struct
	// and never regenerated would satisfy the field comparison and leave the soffit where it was.
	Settings->Ceiling.BandDrop = SavedCeiling.BandDrop * 2.0;
	Settings->Ceiling.BandWidth = SavedCeiling.BandWidth * 1.5;

	TestTrue(TEXT("Changing a ceiling figure rebuilds something"),
		Subsystem->ApplyProjectSettingsToLevel() > 0);

	Ceiling = FindCeiling(House);
	if (!TestNotNull(TEXT("The ceiling survives the rebuild"), Ceiling))
	{
		return false;
	}

	TestEqual(TEXT("The ceiling took the new drop"),
		Ceiling->Ceiling.Drop, Settings->Ceiling.BandDrop);
	TestTrue(*FString::Printf(TEXT("The band was actually rebuilt: volume %.0f then %.0f"),
		VolumeBefore, ShellVolume(Ceiling)),
		ShellVolume(Ceiling) > VolumeBefore * 1.2);

	// And the ring did NOT move, which is the other half of the depth model being right: the ring is
	// sized by the beam it buries, so a band figure has no business changing it.
	TestNearlyEqual(TEXT("The ring stays where the beam put it"),
		Ceiling->GetMeshComponent()->GetDynamicMesh()->GetMeshRef().GetBounds().Min.Z,
		RingSoffitBefore, 0.01);

	// -------------------------------------------- and the fan under a ceiling that covers it
	//
	// Switched to a full drop, which is the only arrangement where the ceiling is between the fan
	// and the room at all - every named template is a band style and leaves the middle open, which
	// is the whole point of them. Done through the spec, because that is how a drawing would say it.
	{
		FHFHouseSpec Deep = House->Spec;
		for (FHFFalseCeiling& Panel : Deep.FalseCeilings)
		{
			Panel.Template = EHFCeilingTemplate::Custom;
			Panel.Style = EHFCeilingStyle::FullDrop;
			Panel.Drop = 20.0;
			Panel.BandWidth = 0.0;
			Panel.PerimeterBulkheadWidth = 0.0;
			Panel.PerimeterBulkheadDrop = 0.0;
		}

		House->SetSpec(Deep);
		House->BuildGeometry();

		Fan = FindFan(House);
		if (!TestNotNull(TEXT("The fan survives a rebuild under a full drop"), Fan))
		{
			return false;
		}

		const double RodUnderShallow = Fan->Fan.DropLength;
		TestTrue(*FString::Printf(TEXT("The rod reaches past a 20 drop: %.2f"), RodUnderShallow),
			RodUnderShallow >= Settings->CeilingFanDropLength + 20.0 - 0.01);

		// The ceiling is deepened and the level asked to catch up. The ceiling moves; the question
		// this test exists for is whether the fan does.
		for (FHFFalseCeiling& Panel : House->Spec.FalseCeilings)
		{
			Panel.Drop = 45.0;
		}

		TestTrue(TEXT("Re-applying settings rebuilds the ceiling and the fan"),
			House->ApplyProjectSettingsToCeilings() >= 2);

		Fan = FindFan(House);
		if (!TestNotNull(TEXT("The fan survives that too"), Fan))
		{
			return false;
		}

		TestTrue(*FString::Printf(
			TEXT("The rod grew with the ceiling rather than leaving the rotor in it: %.2f then %.2f"),
			RodUnderShallow, Fan->Fan.DropLength),
			Fan->Fan.DropLength > RodUnderShallow + 20.0);

		// AND IT IS NOT CUMULATIVE. Re-seeded from the project figure plus the ceiling rather than
		// added to whatever was there, so asking twice gives the same answer instead of hanging the
		// fan a ceiling lower every time somebody opens the settings page.
		const double RodOnce = Fan->Fan.DropLength;
		House->ApplyProjectSettingsToCeilings();

		Fan = FindFan(House);
		if (TestNotNull(TEXT("The fan survives a second re-apply"), Fan))
		{
			TestEqual(TEXT("Re-applying twice does not lengthen the rod again"),
				Fan->Fan.DropLength, RodOnce);
		}
	}

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
