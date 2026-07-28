// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFTypes.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The reference drawing set is a deliverable, not a by-product: it is what Claude reads back when
 * rebuilding the house, and therefore the acceptance test for the whole pipeline. The generator is
 * Python and so sits outside this suite - this checks its output is present and plausible, which
 * catches the set being deleted, half-written, or never regenerated after a layout change.
 *
 * Regenerate with Scripts/hf-drawings.ps1.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFDrawingSetPresentTest, "HouseForge.Drawings.SampleSetPresent", HF_TEST_FLAGS)

bool FHFDrawingSetPresentTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("HouseForge"));
	if (!TestTrue(TEXT("HouseForge plugin is discoverable"), Plugin.IsValid()))
	{
		return false;
	}

	const FString Dir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Reference"), TEXT("Drawings"), TEXT("Sample2BHK"));

	IFileManager& Files = IFileManager::Get();
	TArray<FString> Pngs;
	TArray<FString> Svgs;
	Files.FindFiles(Pngs, *FPaths::Combine(Dir, TEXT("*.png")), true, false);
	Files.FindFiles(Svgs, *FPaths::Combine(Dir, TEXT("*.svg")), true, false);

	if (Pngs.IsEmpty())
	{
		AddError(FString::Printf(
			TEXT("No drawings found in '%s'. Regenerate them with Scripts/hf-drawings.ps1"), *Dir));
		return false;
	}

	// Three plans, plus one elevation sheet for each room worth drawing.
	const FHFHouseSpec Spec = FHFSampleHouse::Make2BHK();
	int32 ExpectedElevations = 0;
	for (const FHFRoom& Room : Spec.Rooms)
	{
		switch (Room.Type)
		{
		case EHFRoomType::Living:
		case EHFRoomType::Dining:
		case EHFRoomType::Kitchen:
		case EHFRoomType::Bedroom:
		case EHFRoomType::MasterBedroom:
		case EHFRoomType::Bathroom:
		case EHFRoomType::Foyer:
			++ExpectedElevations;
			break;
		default:
			break;
		}
	}
	const int32 Expected = 3 + ExpectedElevations;

	TestEqual(TEXT("Every sheet is present as PNG"), Pngs.Num(), Expected);
	TestEqual(TEXT("Every sheet is present as SVG"), Svgs.Num(), Expected);

	// The three plans are what the rebuild loop actually reads, so name them explicitly rather
	// than trusting the count alone.
	for (const TCHAR* Required : { TEXT("01-blank-layout"), TEXT("02-furniture-layout"),
								   TEXT("03-reflected-ceiling-plan") })
	{
		const FString Png = FPaths::Combine(Dir, FString(Required) + TEXT(".png"));
		TestTrue(*FString::Printf(TEXT("%s.png exists"), Required), FPaths::FileExists(Png));

		// A truncated or blank render would still be a file; a real A3 line drawing is tens of KB.
		const int64 Size = Files.FileSize(*Png);
		TestTrue(*FString::Printf(TEXT("%s.png has plausible content (%lld bytes)"), Required, Size),
			Size > 20 * 1024);
	}

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
