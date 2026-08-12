// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#include "Bake/HFBakeService.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "HAL/IConsoleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "HFEditorSubsystem.h"
#include "IDesktopPlatform.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "ToolMenus.h"
#include "Toolset/HFToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "UI/HFPanelIds.h"
#include "UI/SHFHousePanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FHouseForgeEditorModule"

DEFINE_LOG_CATEGORY(LogHouseForgeEditor);

namespace
{
	/**
	 * Whether RegisterToolsetClass was actually reached at startup.
	 *
	 * Recorded rather than re-derived. Asking the registry later would answer a different question
	 * - whether the toolset is registered NOW, by anyone - and the one the panel needs is whether
	 * THIS module got that far, which is what distinguishes a plugin fault from a missing plugin.
	 */
	bool GToolsetRegistered = false;

	/** Opens a file dialog and imports whatever the user picks. */
	void ImportDrawingsInteractive()
	{
		IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
		UHFEditorSubsystem* Editor = GEditor ? GEditor->GetEditorSubsystem<UHFEditorSubsystem>() : nullptr;
		if (Desktop == nullptr || Editor == nullptr)
		{
			return;
		}

		const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

		TArray<FString> Selected;
		const bool bPicked = Desktop->OpenFileDialog(
			ParentWindow,
			TEXT("Import interior drawings"),
			FPaths::ProjectDir(),
			TEXT(""),
			TEXT("Drawings (*.png;*.jpg;*.jpeg;*.pdf)|*.png;*.jpg;*.jpeg;*.pdf|")
			TEXT("Images (*.png;*.jpg;*.jpeg)|*.png;*.jpg;*.jpeg|")
			TEXT("PDF sheet sets (*.pdf)|*.pdf"),
			EFileDialogFlags::Multiple,
			Selected);

		if (!bPicked || Selected.IsEmpty())
		{
			return;
		}

		TArray<FString> Imported;
		const FHFOperationResult Result = Editor->ImportDrawings(Selected, FString(), Imported);

		// A PDF import can fail for an environment reason the user can fix, so say so rather than
		// only logging it.
		FMessageDialog::Open(
			Result.bSuccess ? EAppMsgType::Ok : EAppMsgType::Ok,
			FText::FromString(Result.Message),
			LOCTEXT("ImportTitle", "HouseForge - Import Drawings"));
	}

	/**
	 * Starts the engine's MCP server and writes the Claude Code client config.
	 *
	 * Driven through console commands rather than the plugin's API on purpose: HouseForge
	 * registers with the ToolsetRegistry and never links against ModelContextProtocol, so it stays
	 * buildable whether or not that experimental plugin is present.
	 */
	void StartMcpServer()
	{
		if (GEngine == nullptr)
		{
			return;
		}

		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		GEngine->Exec(World, TEXT("ModelContextProtocol.StartServer"));
		GEngine->Exec(World, TEXT("ModelContextProtocol.GenerateClientConfig"));

		const FText Message = LOCTEXT("McpStarted",
			"Started the Unreal MCP server and wrote .mcp.json into the project folder.\n\n"
			"Claude can now reach HouseForge through list_toolsets / describe_toolset / call_tool.\n\n"
			"To have the server start with the editor, tick Auto Start Server under\n"
			"Editor Preferences > Plugins > Model Context Protocol.");

		FMessageDialog::Open(EAppMsgType::Ok, Message,
			LOCTEXT("McpTitle", "HouseForge - Unreal MCP"));
	}

	/** The panel's tab. One widget, and the tab is a frame around it. */
	TSharedRef<SDockTab> SpawnHousePanelTab(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SHFHousePanel)
			];
	}

	/** Opens the panel, or brings it forward if it is already open somewhere. */
	void OpenHousePanel()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(HFPanelTabIds::HouseForgePanel());
	}

	/**
	 * The same thing from the console, so the panel can be opened without a mouse.
	 *
	 * Worth having beyond convenience: it is how a startup argument or a script gets the panel on
	 * screen, and it is the only way to look at the panel from a headless-launched editor.
	 */
	FAutoConsoleCommand GOpenPanelCommand(
		TEXT("HouseForge.OpenPanel"),
		TEXT("Open the HouseForge panel."),
		FConsoleCommandDelegate::CreateStatic(&OpenHousePanel));

	/**
	 * Whether the flat is in the Lumen scene, from the console.
	 *
	 * ## Where the guard was NOT, and why this had to exist
	 *
	 * FHFSceneCapture::EnsureLumenCoverage guards the one render path that contains no Lumen at all -
	 * measured, three captures with GI on, off and absent came back byte-identical. The paths that DO
	 * converge Lumen are the viewport's HighResShot, PIE and Movie Render Queue, and none of them
	 * passes through any HouseForge code that could refuse.
	 *
	 * A console command is what those have in common: Scripts/hf_lumen.py already drives the viewport
	 * through console commands, so it can now ask this first, and so can anyone taking a screenshot by
	 * hand. The answer goes to the log at Warning when the flat is not covered, which is what makes it
	 * visible in an unattended run's output rather than only to whoever is looking at the screen.
	 */
	void CheckLumenCoverageCommand()
	{
		UHFEditorSubsystem* Editor = GEditor != nullptr
			? GEditor->GetEditorSubsystem<UHFEditorSubsystem>() : nullptr;

		if (Editor == nullptr)
		{
			UE_LOG(LogHouseForgeEditor, Warning, TEXT("HouseForge.CheckLumenCoverage: no editor subsystem."));
			return;
		}

		FString CoverageReport;
		const FHFOperationResult Result = Editor->CheckLumenCoverage(CoverageReport);

		if (Result.bSuccess)
		{
			UE_LOG(LogHouseForgeEditor, Log, TEXT("HouseForge.CheckLumenCoverage: %s"), *CoverageReport);
		}
		else
		{
			UE_LOG(LogHouseForgeEditor, Warning,
				TEXT("HouseForge.CheckLumenCoverage: THIS FLAT IS NOT IN THE LUMEN SCENE. Any render taken now is lit by sky flooding through walls Lumen cannot see, and will look BRIGHTER than the correct result rather than obviously broken.\n%s"),
				*CoverageReport);
		}
	}

	FAutoConsoleCommand GCheckLumenCoverageCommand(
		TEXT("HouseForge.CheckLumenCoverage"),
		TEXT("Report whether the flat is in the Lumen scene. Ask this BEFORE a HighResShot, PIE or MRQ render."),
		FConsoleCommandDelegate::CreateStatic(&CheckLumenCoverageCommand));

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(TEXT("HouseForge"));

		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
		if (Menu == nullptr)
		{
			return;
		}

		FToolMenuSection& Section = Menu->FindOrAddSection(
			TEXT("HouseForge"), LOCTEXT("HouseForgeSection", "HouseForge"));

		// First entry in the section, because it is the door to everything else. The Window menu
		// carries the same tab through the level editor workspace group, so there are two ways in
		// and one tab: TryInvokeTab brings an already-open panel forward rather than opening a
		// second, empty one.
		Section.AddMenuEntry(
			TEXT("HouseForgePanel"),
			LOCTEXT("OpenPanel", "HouseForge Panel"),
			LOCTEXT("OpenPanelTooltip",
				"Open the HouseForge panel: what every surface in the flat is made of, and the "
				"controls to change it."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&OpenHousePanel)));

		Section.AddMenuEntry(
			TEXT("HouseForgeImportDrawings"),
			LOCTEXT("ImportDrawings", "Import Interior Drawings..."),
			LOCTEXT("ImportDrawingsTooltip",
				"Bring AutoCAD interior drawings into HouseForge. PNG and JPG are copied as-is; "
				"PDF sheet sets are rasterised to one image per page so they can be read."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&ImportDrawingsInteractive)));

		Section.AddMenuEntry(
			TEXT("HouseForgeStartMcp"),
			LOCTEXT("StartMcp", "Start Unreal MCP Server"),
			LOCTEXT("StartMcpTooltip",
				"Start the engine's MCP server and write the Claude Code client config, so Claude "
				"can read the imported drawings and build the house."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&StartMcpServer)));
	}
}

void FHouseForgeEditorModule::StartupModule()
{
	// The MCP plugin watches the ToolsetRegistry and surfaces whatever is registered, so HouseForge
	// never references ModelContextProtocol directly.
	if (UToolsetRegistry::IsAvailable())
	{
		UToolsetRegistry::RegisterToolsetClass(UHFToolset::StaticClass());
		GToolsetRegistered = true;
		UE_LOG(LogHouseForgeEditor, Log, TEXT("Registered the HouseForge MCP toolset."));
	}
	else
	{
		UE_LOG(LogHouseForgeEditor, Warning,
			TEXT("ToolsetRegistry is unavailable; HouseForge will not be reachable over MCP."));
	}

	// The runtime element actors reach asset creation through exactly one static delegate, bound
	// here. Package creation is unreachable from a runtime module and making HouseForge depend on
	// UnrealEd to fix that would be far worse. Unbound, nothing can bake - which is the correct
	// behaviour in a cooked build rather than a link error.
	FHFBakeService::Register();

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateStatic(&RegisterMenus));

	// The panel's tab. Registered here rather than inside the ToolMenus callback so that the
	// spawner exists in a headless run too - HouseForge.Editor.Panel.TabSpawnerIsRegistered is the
	// only thing standing between a renamed tab id and a Tools menu entry that opens nothing.
	FGlobalTabmanager::Get()
		->RegisterNomadTabSpawner(HFPanelTabIds::HouseForgePanel(),
			FOnSpawnTab::CreateStatic(&SpawnHousePanelTab))
		.SetDisplayName(LOCTEXT("HouseForgeTab", "HouseForge"))
		.SetTooltipText(LOCTEXT("HouseForgeTabTooltip",
			"What every surface in the flat is made of, and the controls to change it."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory())
		.SetMenuType(ETabSpawnerMenuType::Enabled);

	UE_LOG(LogHouseForgeEditor, Log, TEXT("HouseForge editor module started."));
}

void FHouseForgeEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(TEXT("HouseForge"));

	FHFBakeService::Unregister();

	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(HFPanelTabIds::HouseForgePanel());
	}

	if (UToolsetRegistry::IsAvailable())
	{
		UToolsetRegistry::UnregisterToolsetClass(UHFToolset::StaticClass());
	}

	GToolsetRegistered = false;

	UE_LOG(LogHouseForgeEditor, Log, TEXT("HouseForge editor module shut down."));
}

bool FHouseForgeEditorModule::IsToolsetRegistered()
{
	return GToolsetRegistered;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FHouseForgeEditorModule, HouseForgeEditor)
