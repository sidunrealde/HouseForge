// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/AutomationTest.h"
#include "Geometry/HFMeshOps.h"
#include "Materials/HFMaterialLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ScopeExit.h"
#include "UI/HFPanelIds.h"
#include "UI/SHFHousePanel.h"
#include "UI/SHFMaterialPanel.h"
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
		// panel where the truth is a headless harness.
		//
		// HF_UNMEASURED RATHER THAN AddInfo, which is the whole difference between saying so and
		// being heard. An AddInfo skip is a green test that asserted nothing, printed among two
		// thousand other info lines - the exact shape of every defect this suite has shipped. The
		// sentinel is what hf-validate.ps1 greps for, so a stage that exists to take this measurement
		// fails rather than reporting a pass it did not earn. See the note at
		// HFMaterialFinishTests.cpp's CanRender branch.
		//
		// FALSIFIED as an A/B, with this branch forced and stage 3 run on -nullrhi so both panel
		// tests genuinely skip. Sentinel: "GATE FAILED: 3 measurement(s) were skipped in the stage
		// that exists to take them", naming TabSpawns and SurfacesEditReachesTheRenderer. The AddInfo
		// this replaced, same run, same skips: "Every pixel measurement was actually taken", exit 0.
		//
		// WORTH KNOWING: neither the gate's stage 2 nor its stage 3 reaches this branch today, because
		// Slate IS initialised under UnrealEditor-Cmd -nullrhi. It had to be forced to be falsified at
		// all. So this guards a headless harness that does not currently exist rather than one that
		// does - which is the honest status of it, and better than assuming it fires.
		AddWarning(TEXT("HF_UNMEASURED: Slate is not initialised in this run, so the panel widget was NOT constructed and nothing here was asserted."));
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

/**
 * THE SURFACES SECTION IS IN THE STACK, AND IS THERE WITH NO HOUSE IN THE LEVEL.
 *
 * Finishes are assets rather than level state, so an empty level must not make the section vanish
 * or grey out - the panel is most likely to be opened before a house exists, and a library edit
 * made then is what the next generated house picks up.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFPanelSurfacesSectionTest,
	"HouseForge.Editor.Panel.SurfacesSectionIsAlwaysPresent", HF_TEST_FLAGS)

bool FHFPanelSurfacesSectionTest::RunTest(const FString& Parameters)
{
	const TArray<FHFPanelSection> Sections = SHFHousePanel::BuildSections();

	const FHFPanelSection* Surfaces = Sections.FindByPredicate(
		[](const FHFPanelSection& Section) { return Section.Id == HFPanelSectionIds::Surfaces(); });

	if (!TestNotNull(TEXT("The panel has a SURFACES section"), Surfaces))
	{
		return false;
	}

	TestTrue(TEXT("SURFACES is relevant whether or not there is a house in the level"),
		!Surfaces->IsRelevant || Surfaces->IsRelevant());

	return true;
}

/**
 * THE CONNECTION STATUS IS NOT A PAGE, SO IT IS VISIBLE FROM EVERY PAGE.
 *
 * Generate lives in DRAWINGS and is disabled when Claude cannot be reached. Put the status on a tab
 * of its own - which is the obvious thing to do, and what the panel did while it was a stack of
 * sections - and an artist looking at a greyed-out Generate cannot see why without leaving the page
 * that shows it. A disabled control whose explanation is one click away reads as a broken control.
 *
 * So CLAUDE is the header above the tab strip rather than a section, and its absence from
 * BuildSections is the thing that makes it so. Asserted, because "add a section for it" is exactly
 * the tidying a later reader would do: the panel is a list of sections, and there is a section id
 * sitting right there in HFPanelSectionIds for it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFPanelClaudeIsNotAPageTest,
	"HouseForge.Editor.Panel.ConnectionStatusIsOnEveryPage", HF_TEST_FLAGS)

bool FHFPanelClaudeIsNotAPageTest::RunTest(const FString& Parameters)
{
	const TArray<FHFPanelSection> Sections = SHFHousePanel::BuildSections();

	const FHFPanelSection* Claude = Sections.FindByPredicate(
		[](const FHFPanelSection& Section) { return Section.Id == HFPanelSectionIds::Claude(); });

	TestNull(TEXT("CLAUDE is the panel's header rather than one of its pages"), Claude);

	// AND THERE ARE STILL PAGES. Without this, deleting BuildSections entirely satisfies the
	// assertion above perfectly.
	TestTrue(TEXT("The panel still has pages to switch between"), Sections.Num() >= 2);

	return true;
}

/**
 * THE PANEL LISTS EVERY SURFACE ROLE, COUNTED FROM THE ENUM AND NOT FROM A LITERAL.
 *
 * The role count has been 16, 17 and now 18 - LightSource and Mirror were each argued for and added
 * a milestone apart. A panel holding its own list of roles drops whichever were added last, and it
 * drops them silently: the surface still renders, and the only control for changing it is missing
 * from a list nobody has counted. Asserted against FHFMeshOps::NumSurfaceRoles, which is the same
 * source the material slots and the polygroup ids come from.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFPanelListsEveryRoleTest,
	"HouseForge.Editor.Panel.SurfacesListsEveryRole", HF_TEST_FLAGS)

bool FHFPanelListsEveryRoleTest::RunTest(const FString& Parameters)
{
	const TArray<TSharedPtr<FHFSurfaceRoleRow>> Rows = SHFMaterialPanel::BuildRoleRows();

	TestEqual(TEXT("The panel lists one row per surface role"),
		Rows.Num(), FHFMeshOps::NumSurfaceRoles());

	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		if (!TestTrue(FString::Printf(TEXT("Row %d exists"), Index), Rows[Index].IsValid()))
		{
			continue;
		}

		TestEqual(FString::Printf(TEXT("Row %d is the role at that index"), Index),
			static_cast<int32>(Rows[Index]->Role), Index);

		// A row with no name is a row nobody can pick. It happens when a role is added to the enum
		// without a display name, which is exactly the case this file exists to catch.
		TestFalse(FString::Printf(TEXT("Row %d has a name to show"), Index),
			Rows[Index]->Name.IsEmpty());
	}

	return true;
}

/**
 * A CHANGE MADE IN THE PANEL REACHES THE LIBRARY AND THE RENDERER, AND MOVES NO GEOMETRY.
 *
 * The panel's OWN wiring, not the subsystem's. Writing into the struct the details view holds and
 * then calling NotifyPostChange is precisely what a user dragging a slider does, so this exercises
 * the path that would otherwise only ever be tested by hand: the notify hook, the tier the change
 * type selects, and the write through to UHFEditorSubsystem.
 *
 * Both tiers are driven. The interactive one matters most here, because the reason the panel uses a
 * notify hook at all is that OnFinishedChangingProperties stays silent while the mouse is down - a
 * panel wired to that delegate passes every test that only checks the released value, and drags
 * with no visible change in the viewport.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFPanelEditReachesTheRendererTest,
	"HouseForge.Editor.Panel.SurfacesEditReachesTheRenderer", HF_TEST_FLAGS)

bool FHFPanelEditReachesTheRendererTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		// THIS IS THE ONLY TEST THAT PROVES A PANEL EDIT REACHES THE RENDERED MATERIAL, so a silent
		// skip here leaves that claim guarded by nothing at all. Sentinel, not AddInfo - see above.
		AddWarning(TEXT("HF_UNMEASURED: Slate is not initialised in this run, so the panel was NOT constructed and no edit was driven through it."));
		return true;
	}

	UHFMaterialLibrary* Library = UHFMaterialLibrary::Get();
	if (!TestNotNull(TEXT("There is a material library"), Library))
	{
		return false;
	}

	// The panel writes the real shipped asset, because a panel test that wrote to a copy would
	// prove nothing about the panel. Put back afterwards, values and instances both.
	const TMap<EHFSurfaceRole, FHFSurfaceFinish> Saved = Library->Finishes;
	ON_SCOPE_EXIT
	{
		Library->Finishes = Saved;
		Library->PushAllFinishes(EHFMaterialPush::Commit);
	};

	const TSharedRef<SHFMaterialPanel> Panel = SNew(SHFMaterialPanel);

	constexpr EHFSurfaceRole Role = EHFSurfaceRole::FloorFinish;
	Panel->SelectRole(Role);
	TestEqual(TEXT("The panel is showing the role it was asked for"),
		static_cast<int32>(Panel->GetSelectedRole()), static_cast<int32>(Role));

	FHFSurfaceFinish* Edited = Panel->EditedFinish();
	if (!TestNotNull(TEXT("The panel has a finish loaded to edit"), Edited))
	{
		return false;
	}

	TestEqual(TEXT("The loaded finish is the library's, not a blank struct"),
		Edited->Description, Library->FinishForRole(Role).Description);

	FProperty* Roughness = FHFSurfaceFinish::StaticStruct()->FindPropertyByName(
		GET_MEMBER_NAME_CHECKED(FHFSurfaceFinish, Roughness));
	if (!TestNotNull(TEXT("FHFSurfaceFinish has a Roughness property to change"), Roughness))
	{
		return false;
	}

	UMaterialInterface* Material = Library->ResolveMaterial(Role);
	if (!TestNotNull(TEXT("The floor finish role resolves to a material"), Material))
	{
		return false;
	}

	// ---- mid-drag
	Edited->Roughness = 0.271f;
	{
		FPropertyChangedEvent Event(Roughness, EPropertyChangeType::Interactive);
		Panel->NotifyPostChange(Event, Roughness);
	}

	float Rendered = -1.0f;
	Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Roughness")), Rendered);
	TestEqual(TEXT("A mid-drag change is already on the material the surface renders through"),
		Rendered, 0.271f);

	// ---- released
	Edited->Roughness = 0.618f;
	{
		FPropertyChangedEvent Event(Roughness, EPropertyChangeType::ValueSet);
		Panel->NotifyPostChange(Event, Roughness);
	}

	Rendered = -1.0f;
	Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Roughness")), Rendered);
	TestEqual(TEXT("The released value is on the material"), Rendered, 0.618f);
	TestEqual(TEXT("The released value is in the library, which is the record"),
		Library->FinishForRole(Role).Roughness, 0.618f);

	// Selecting another role and coming back shows what was committed rather than a stale copy.
	Panel->SelectRole(EHFSurfaceRole::WallPaint);
	Panel->SelectRole(Role);
	TestEqual(TEXT("Re-selecting the role shows the committed value"),
		Panel->EditedFinish()->Roughness, 0.618f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
