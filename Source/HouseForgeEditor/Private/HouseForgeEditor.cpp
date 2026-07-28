// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "HFEditorSubsystem.h"
#include "IDesktopPlatform.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "ToolMenus.h"
#include "Toolset/HFToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

#define LOCTEXT_NAMESPACE "FHouseForgeEditorModule"

DEFINE_LOG_CATEGORY(LogHouseForgeEditor);

namespace
{
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
		UE_LOG(LogHouseForgeEditor, Log, TEXT("Registered the HouseForge MCP toolset."));
	}
	else
	{
		UE_LOG(LogHouseForgeEditor, Warning,
			TEXT("ToolsetRegistry is unavailable; HouseForge will not be reachable over MCP."));
	}

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateStatic(&RegisterMenus));

	UE_LOG(LogHouseForgeEditor, Log, TEXT("HouseForge editor module started."));
}

void FHouseForgeEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(TEXT("HouseForge"));

	if (UToolsetRegistry::IsAvailable())
	{
		UToolsetRegistry::UnregisterToolsetClass(UHFToolset::StaticClass());
	}

	UE_LOG(LogHouseForgeEditor, Log, TEXT("HouseForge editor module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FHouseForgeEditorModule, HouseForgeEditor)
