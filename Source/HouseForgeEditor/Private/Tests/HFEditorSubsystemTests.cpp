// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFHouseActor.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HFEditorSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Model/HFSpecSerializer.h"
#include "Model/HFTypes.h"
#include "Toolset/HFToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	UHFEditorSubsystem* Subsystem()
	{
		return GEditor ? GEditor->GetEditorSubsystem<UHFEditorSubsystem>() : nullptr;
	}

	/**
	 * A small house written the way Claude would write one after reading a drawing: JSON, in
	 * millimetres, with no reference to any fixture in the plugin.
	 */
	FString MinimalSpecJson()
	{
		return TEXT(R"JSON(
{
  "schemaVersion": 1,
  "name": "Test Flat",
  "sourceDrawing": "unit-test",
  "units": "Millimeters",
  "defaultWallThickness": 115,
  "defaultWallHeight": 3000,
  "walls": [
    { "id": "W_S", "start": {"x": 0, "y": 0}, "end": {"x": 4000, "y": 0},
      "thickness": 230, "height": 3000, "bIsExternal": true },
    { "id": "W_E", "start": {"x": 4000, "y": 0}, "end": {"x": 4000, "y": 3000},
      "thickness": 230, "height": 3000, "bIsExternal": true },
    { "id": "W_N", "start": {"x": 4000, "y": 3000}, "end": {"x": 0, "y": 3000},
      "thickness": 230, "height": 3000, "bIsExternal": true },
    { "id": "W_W", "start": {"x": 0, "y": 3000}, "end": {"x": 0, "y": 0},
      "thickness": 230, "height": 3000, "bIsExternal": true }
  ],
  "openings": [
    { "id": "D1", "wallId": "W_S", "offsetAlongWall": 2000, "width": 900,
      "height": 2100, "sillHeight": 0, "kind": "Door", "swing": "InwardLeft" }
  ],
  "beams": [
    { "id": "BM_S", "start": {"x": 0, "y": 0}, "end": {"x": 4000, "y": 0},
      "width": 230, "depth": 450, "soffitZ": 3000 }
  ],
  "columns": [
    { "id": "COL_SW", "position": {"x": 0, "y": 0}, "size": {"x": 450, "y": 230},
      "rotationDegrees": 0, "height": 3000, "baseZ": 0 }
  ],
  "rooms": [
    { "id": "R_Main", "name": "Main Room", "type": "Living",
      "boundary": [ {"x":0,"y":0}, {"x":4000,"y":0}, {"x":4000,"y":3000}, {"x":0,"y":3000} ],
      "ceilingHeight": 3000, "skirtingHeight": 100 }
  ],
  "falseCeilings": [
    { "id": "FC1", "roomId": "R_Main", "style": "Peripheral", "drop": 200, "bandWidth": 600 }
  ],
  "fixtures": [
    { "id": "F_Sofa", "roomId": "R_Main", "type": "Sofa", "label": "Sofa",
      "position": {"x": 2000, "y": 1500}, "footprint": {"x": 2100, "y": 900},
      "height": 800, "rotationDegrees": 0, "baseZ": 0, "params": {} }
  ]
}
)JSON");
	}
}

/** Applying a spec then reading it back is the round trip the whole MCP loop rests on. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFApplyAndReadBackTest,
	"HouseForge.Editor.ApplySpecAndReadBack", HF_TEST_FLAGS)

bool FHFApplyAndReadBackTest::RunTest(const FString& Parameters)
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (!TestNotNull(TEXT("Editor subsystem exists"), Editor))
	{
		return false;
	}

	// Build into the level already open: creating one mid-test would disturb the editor session.
	const FHFOperationResult Applied = Editor->ApplySpecJson(MinimalSpecJson(), FString());
	if (!TestTrue(TEXT("Spec applies"), Applied.bSuccess))
	{
		AddError(Applied.Message);
		return false;
	}

	AHFHouseActor* House = Editor->FindHouseActor();
	if (!TestNotNull(TEXT("A house actor is in the level"), House))
	{
		return false;
	}

	// The spec arrived in millimetres and must be stored in centimetres, once.
	TestEqual(TEXT("Stored spec is in centimetres"), House->Spec.Units, EHFUnits::Centimeters);
	TestNearlyEqual(TEXT("A 4000mm wall is 400cm in the level"),
		House->Spec.Walls[0].Length(), 400.0, 0.01);
	TestEqual(TEXT("Beams survive"), House->Spec.Beams.Num(), 1);
	TestEqual(TEXT("Columns survive"), House->Spec.Columns.Num(), 1);

	FString ReadBack;
	const FHFOperationResult Read = Editor->GetSpecJson(ReadBack);
	if (!TestTrue(TEXT("Spec reads back"), Read.bSuccess))
	{
		AddError(Read.Message);
		return false;
	}

	FHFHouseSpec Parsed;
	FString Error;
	TestTrue(TEXT("Read-back JSON parses"), FHFSpecSerializer::FromJsonString(ReadBack, Parsed, Error));
	TestEqual(TEXT("Read-back keeps every wall"), Parsed.Walls.Num(), 4);
	TestEqual(TEXT("Read-back keeps the room"), Parsed.Rooms.Num(), 1);
	TestEqual(TEXT("Read-back keeps the fixture"), Parsed.Fixtures.Num(), 1);
	TestEqual(TEXT("Read-back reports centimetres"), Parsed.Units, EHFUnits::Centimeters);

	// Applying again must replace, not accumulate - otherwise a correction round would stack two
	// houses on top of each other and the screenshot would look right.
	TestTrue(TEXT("Re-applying succeeds"), Editor->ApplySpecJson(MinimalSpecJson(), FString()).bSuccess);

	int32 HouseCount = 0;
	for (TActorIterator<AHFHouseActor> It(GEditor->GetEditorWorldContext().World()); It; ++It)
	{
		++HouseCount;
	}
	TestEqual(TEXT("Re-applying replaces the house rather than adding one"), HouseCount, 1);

	return true;
}

/** A spec with errors must be refused outright, not built half-way. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFRefusesInvalidSpecTest,
	"HouseForge.Editor.RefusesInvalidSpec", HF_TEST_FLAGS)

bool FHFRefusesInvalidSpecTest::RunTest(const FString& Parameters)
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (!TestNotNull(TEXT("Editor subsystem exists"), Editor))
	{
		return false;
	}

	// A door wider than the wall it sits in.
	FString Broken = MinimalSpecJson();
	Broken = Broken.Replace(TEXT("\"offsetAlongWall\": 2000"), TEXT("\"offsetAlongWall\": 3950"));

	const FHFOperationResult Validated = Editor->ValidateSpecJson(Broken);
	TestFalse(TEXT("Validation rejects the broken spec"), Validated.bSuccess);
	TestTrue(TEXT("The report names the rule"), Validated.Message.Contains(TEXT("OpeningExceedsWall")));

	const FHFOperationResult Applied = Editor->ApplySpecJson(Broken, FString());
	TestFalse(TEXT("Applying the broken spec is refused"), Applied.bSuccess);
	TestTrue(TEXT("The refusal explains why"), Applied.Message.Contains(TEXT("OpeningExceedsWall")));

	// Malformed JSON is a different failure and must read differently.
	const FHFOperationResult Garbage = Editor->ValidateSpecJson(TEXT("{ not json"));
	TestFalse(TEXT("Malformed JSON is rejected"), Garbage.bSuccess);
	TestFalse(TEXT("The malformed-JSON message is not empty"), Garbage.Message.IsEmpty());

	return true;
}

/** Editing through the tools must never leave the level in a state the builder would choke on. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFModifyAndDeleteTest,
	"HouseForge.Editor.ModifyAndDeleteElements", HF_TEST_FLAGS)

bool FHFModifyAndDeleteTest::RunTest(const FString& Parameters)
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (!TestNotNull(TEXT("Editor subsystem exists"), Editor))
	{
		return false;
	}

	if (!TestTrue(TEXT("Baseline spec applies"), Editor->ApplySpecJson(MinimalSpecJson(), FString()).bSuccess))
	{
		return false;
	}

	// A patch changes only the named field and preserves everything else.
	const FHFOperationResult Thickened = Editor->ModifyElement(
		TEXT("walls"), TEXT("W_S"), TEXT("{\"thickness\": 30}"));
	TestTrue(TEXT("A valid change is accepted"), Thickened.bSuccess);

	const AHFHouseActor* House = Editor->FindHouseActor();
	if (TestNotNull(TEXT("House still present"), House))
	{
		const FHFWall* Wall = House->Spec.FindWall(TEXT("W_S"));
		if (TestNotNull(TEXT("The wall survives the patch"), Wall))
		{
			TestNearlyEqual(TEXT("Thickness changed"), Wall->Thickness, 30.0, 0.01);
			TestNearlyEqual(TEXT("Untouched fields are preserved"), Wall->Length(), 400.0, 0.01);
			TestTrue(TEXT("Flags are preserved"), Wall->bIsExternal);
		}
	}

	// A change that would break the spec is rolled back rather than applied.
	const FHFOperationResult Rejected = Editor->ModifyElement(
		TEXT("walls"), TEXT("W_S"), TEXT("{\"height\": 50}"));
	TestFalse(TEXT("A change that breaks the spec is rejected"), Rejected.bSuccess);
	TestTrue(TEXT("The rejection explains why"), Rejected.Message.Contains(TEXT("OpeningExceedsWallHeight")));

	if (const AHFHouseActor* After = Editor->FindHouseActor())
	{
		const FHFWall* Wall = After->Spec.FindWall(TEXT("W_S"));
		if (TestNotNull(TEXT("The wall is still there after a rejected change"), Wall))
		{
			TestNearlyEqual(TEXT("The rejected change did not take effect"), Wall->Height, 300.0, 0.01);
		}
	}

	TestFalse(TEXT("An unknown category is rejected"),
		Editor->ModifyElement(TEXT("doors"), TEXT("W_S"), TEXT("{}")).bSuccess);
	TestFalse(TEXT("An unknown id is rejected"),
		Editor->ModifyElement(TEXT("walls"), TEXT("W_Nope"), TEXT("{\"thickness\": 20}")).bSuccess);

	// Deleting a wall must take its openings with it, or the spec would hold a dangling reference.
	const FHFOperationResult Deleted = Editor->DeleteElement(TEXT("walls"), TEXT("W_S"));
	TestTrue(TEXT("Deleting a wall succeeds"), Deleted.bSuccess);

	if (const AHFHouseActor* Final = Editor->FindHouseActor())
	{
		TestEqual(TEXT("The wall is gone"), Final->Spec.Walls.Num(), 3);
		TestEqual(TEXT("Its door went with it"), Final->Spec.Openings.Num(), 0);
		TestNull(TEXT("The wall cannot be found"), Final->Spec.FindWall(TEXT("W_S")));
	}

	return true;
}

/** The element inventory is how Claude orients itself without pulling the whole spec. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFListElementsTest,
	"HouseForge.Editor.ListElements", HF_TEST_FLAGS)

bool FHFListElementsTest::RunTest(const FString& Parameters)
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (!TestNotNull(TEXT("Editor subsystem exists"), Editor))
	{
		return false;
	}

	if (!TestTrue(TEXT("Baseline spec applies"), Editor->ApplySpecJson(MinimalSpecJson(), FString()).bSuccess))
	{
		return false;
	}

	FString Summary;
	TestTrue(TEXT("Listing succeeds"), Editor->ListElements(FString(), Summary).bSuccess);
	TestTrue(TEXT("Walls are listed"), Summary.Contains(TEXT("W_S")));
	TestTrue(TEXT("Rooms are listed"), Summary.Contains(TEXT("R_Main")));
	TestTrue(TEXT("Beams are listed"), Summary.Contains(TEXT("BM_S")));
	TestTrue(TEXT("Columns are listed"), Summary.Contains(TEXT("COL_SW")));

	FString WallsOnly;
	TestTrue(TEXT("Filtered listing succeeds"), Editor->ListElements(TEXT("walls"), WallsOnly).bSuccess);
	TestTrue(TEXT("The filter keeps walls"), WallsOnly.Contains(TEXT("W_S")));
	TestFalse(TEXT("The filter excludes rooms"), WallsOnly.Contains(TEXT("R_Main")));

	return true;
}

/**
 * The toolset has to be registered for Claude to reach HouseForge at all. If this fails the whole
 * MCP path is dead, however well the rest works.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFToolsetRegisteredTest,
	"HouseForge.Editor.ToolsetRegistered", HF_TEST_FLAGS)

bool FHFToolsetRegisteredTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("ToolsetRegistry is available"), UToolsetRegistry::IsAvailable()))
	{
		return false;
	}

	TestTrue(TEXT("The HouseForge toolset is registered"),
		UToolsetRegistry::IsToolsetClassRegistered(UHFToolset::StaticClass()));

	// The tools must survive being called with no house present, since that is the state Claude
	// finds the editor in before it has built anything.
	const FString Elements = UHFToolset::ListElements(TEXT(""));
	TestFalse(TEXT("ListElements returns something"), Elements.IsEmpty());

	const FString Drawings = UHFToolset::ListDrawings();
	TestFalse(TEXT("ListDrawings returns something"), Drawings.IsEmpty());
	TestTrue(TEXT("The reference drawings are discoverable"),
		Drawings.Contains(TEXT("01-blank-layout.png")));

	return true;
}

/** Import must accept the formats drawings actually arrive in and reject the rest clearly. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFImportRejectsUnsupportedTest,
	"HouseForge.Editor.ImportRejectsUnsupportedFormats", HF_TEST_FLAGS)

bool FHFImportRejectsUnsupportedTest::RunTest(const FString& Parameters)
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (!TestNotNull(TEXT("Editor subsystem exists"), Editor))
	{
		return false;
	}

	TArray<FString> Imported;

	const FHFOperationResult Empty = Editor->ImportDrawings({}, TEXT("Test"), Imported);
	TestFalse(TEXT("Importing nothing fails"), Empty.bSuccess);

	// A .dwg cannot be read as an image, and the message has to say so rather than fail silently.
	const FString Fake = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("hf-import-test.dwg"));
	FFileHelper::SaveStringToFile(TEXT("not a drawing"), *Fake);

	const FHFOperationResult Unsupported = Editor->ImportDrawings({ Fake }, TEXT("HFImportTest"), Imported);
	TestFalse(TEXT("An unsupported format is rejected"), Unsupported.bSuccess);
	TestTrue(TEXT("The rejection names the accepted formats"), Unsupported.Message.Contains(TEXT(".png")));

	IFileManager::Get().Delete(*Fake);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
