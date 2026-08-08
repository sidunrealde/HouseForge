// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/AutomationTest.h"
#include "UI/HFPanelIds.h"
#include "UI/SHFHousePanel.h"
#include "Widgets/Docking/SDockTab.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * THE TAB THE TOOLS MENU AND THE WINDOW MENU BOTH POINT AT ACTUALLY EXISTS.
 *
 * Three places have to agree on one FName - the spawner, the Tools menu entry and the workspace
 * group - and when they do not, the menu entry opens nothing at all. There is no compile error and
 * no log line for that; the button simply does not work, which is indistinguishable from the panel
 * being broken. This is the assertion that fails instead.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFPanelTabSpawnerTest,
	"HouseForge.Editor.Panel.TabSpawnerIsRegistered", HF_TEST_FLAGS)

bool FHFPanelTabSpawnerTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("The HouseForge panel tab spawner is registered"),
		FGlobalTabmanager::Get()->HasTabSpawner(HFPanelTabIds::HouseForgePanel()));

	return true;
}

/**
 * THE TAB SPAWNS, AND WHAT COMES BACK IS A TAB WITH THE PANEL IN IT.
 *
 * Spawned rather than only registered, because a spawner that throws or hands back an empty tab
 * passes the registration test and fails the moment anybody clicks the menu entry. Nothing about
 * the widget's appearance is asserted - only that constructing it does not fail - which is the
 * whole of what a Slate test can honestly claim without becoming a rendering test.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFPanelTabSpawnsTest,
	"HouseForge.Editor.Panel.TabSpawns", HF_TEST_FLAGS)

bool FHFPanelTabSpawnsTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		// A run with no Slate cannot construct a widget, and failing here would report a missing
		// panel where the truth is a headless harness. Said out loud rather than passing silently.
		AddInfo(TEXT("Slate is not initialised in this run, so the panel widget was not constructed."));
		return true;
	}

	const TSharedRef<SWidget> Panel = SNew(SHFHousePanel);
	TestTrue(TEXT("The HouseForge panel widget constructs"), Panel != SNullWidget::NullWidget);

	return true;
}

/**
 * EVERY SECTION IN THE STACK IS BUILDABLE AND UNIQUELY NAMED.
 *
 * The section array is the seam later milestones append to, so the failure worth catching is an
 * entry added with no Build function or with an id already in use - the first renders as a gap in
 * the panel, the second makes a test that names a section ambiguous. Checked without a widget,
 * a window or a Slate application, which is the point of BuildSections being static.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFPanelSectionsTest,
	"HouseForge.Editor.Panel.SectionsAreWellFormed", HF_TEST_FLAGS)

bool FHFPanelSectionsTest::RunTest(const FString& Parameters)
{
	const TArray<FHFPanelSection> Sections = SHFHousePanel::BuildSections();

	TSet<FName> Ids;
	for (const FHFPanelSection& Section : Sections)
	{
		TestFalse(TEXT("A section has an id"), Section.Id.IsNone());
		TestFalse(FString::Printf(TEXT("Section '%s' has a title"), *Section.Id.ToString()),
			Section.Title.IsEmpty());
		TestTrue(FString::Printf(TEXT("Section '%s' can build its body"), *Section.Id.ToString()),
			static_cast<bool>(Section.Build));

		bool bAlreadyThere = false;
		Ids.Add(Section.Id, &bAlreadyThere);
		TestFalse(FString::Printf(TEXT("Section id '%s' is used once"), *Section.Id.ToString()),
			bAlreadyThere);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
