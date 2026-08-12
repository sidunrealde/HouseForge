// Copyright Siddartha G. All Rights Reserved.

#include "UI/SHFClaudePanel.h"

#include "Claude/HFClaudeCli.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "HouseForgeEditor.h"
#include "Misc/Paths.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HouseForgeClaude"

namespace
{
	/** One status for the editor, because there is one Claude and one MCP server. */
	FHFClaudeStatus GStatus;

	/** Where HouseForge writes the client config the CLI reads. */
	FString ClientConfigPath()
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".mcp.json")));
	}
}

const FHFClaudeStatus& SHFClaudePanel::LastStatus()
{
	return GStatus;
}

void SHFClaudePanel::RunCheck()
{
	// ------------------------------------------------------------------- 1. is the CLI there
	const FString Executable = FHFClaudeCli::FindExecutable();
	if (Executable.IsEmpty())
	{
		GStatus.State = EHFClaudeState::CliNotFound;
		GStatus.Message = TEXT(
			"Claude Code is not installed, or not on this machine's PATH. Install it, then open a "
			"terminal and run 'claude' once to sign in with your Claude account.");
		return;
	}

	// -------------------------------------------- 2. make sure the config it reads is current
	//
	// Written before asking rather than after failing. The server and the config are both ours to
	// produce, so a check that reported "no config" when it could simply have written one would be
	// making the artist do the plugin's job.
	if (GEngine != nullptr)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		GEngine->Exec(World, TEXT("ModelContextProtocol.GenerateClientConfig"));
	}

	// ------------------------------------- 3. free health check: config, server, reachability
	FString StdOut;
	FString StdErr;
	const int32 ReturnCode = FHFClaudeCli::RunToCompletion(
		Executable,
		TEXT("mcp list"),
		FHFClaudeCli::NeutralWorkingDirectory(),
		/*TimeoutSeconds*/ 60.0,
		StdOut,
		StdErr);

	if (ReturnCode != 0 && StdOut.IsEmpty())
	{
		GStatus.State = EHFClaudeState::Failed;
		GStatus.Message = FString::Printf(
			TEXT("Claude Code could not be run. It said: %s"),
			*(StdErr.IsEmpty() ? FString(TEXT("nothing")) : StdErr.TrimStartAndEnd()));
		return;
	}

	const EHFClaudeState Health = FHFClaudeCli::ParseMcpList(StdOut, FHFClaudeCli::ServerName());

	switch (Health)
	{
	case EHFClaudeState::ServerNotRunning:
		GStatus.State = Health;
		GStatus.Message = TEXT(
			"The HouseForge MCP server is not running, so Claude has nothing to build into. "
			"Start it and check again.");
		return;

	case EHFClaudeState::ConfigMissing:
		GStatus.State = Health;
		GStatus.Message = FString::Printf(
			TEXT("Claude Code cannot see the HouseForge server. Check that '%s' exists and that "
				 "the Model Context Protocol plugin is enabled."),
			*ClientConfigPath());
		return;

	case EHFClaudeState::NotAuthenticated:
		GStatus.State = Health;
		GStatus.Message = TEXT(
			"Claude Code needs signing in. Open a terminal, run 'claude', and log in with your "
			"Claude account - you only do this once.");
		return;

	default:
		break;
	}

	// ------------------------------------------------------------ 4. is the toolset registered
	//
	// Asked of ourselves rather than of Claude. HouseForge registers its own toolset at startup,
	// so the plugin already knows the answer - and a missing toolset is a plugin fault, which is a
	// different sentence from anything the artist can fix.
	if (!FHouseForgeEditorModule::IsToolsetRegistered())
	{
		GStatus.State = EHFClaudeState::ToolsetMissing;
		GStatus.Message = TEXT(
			"Claude can reach the server, but HouseForge did not register its toolset. This is a "
			"plugin fault rather than anything to fix here - check the log for LogHouseForgeEditor.");
		return;
	}

	GStatus.State = EHFClaudeState::Ready;
	GStatus.Message = TEXT("Claude Code is connected and can see HouseForge. Drop drawings and press Generate.");
}

FReply SHFClaudePanel::OnCheckClicked()
{
	RunCheck();
	return FReply::Handled();
}

FReply SHFClaudePanel::OnStartServerClicked()
{
	if (GEngine != nullptr)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		GEngine->Exec(World, TEXT("ModelContextProtocol.StartServer"));
		GEngine->Exec(World, TEXT("ModelContextProtocol.GenerateClientConfig"));
	}

	// Re-checked immediately, so the button either clears the problem or proves it is something
	// else. Leaving the old failure on screen after acting on it is how a fixed thing keeps
	// looking broken.
	RunCheck();
	return FReply::Handled();
}

EVisibility SHFClaudePanel::StartServerVisibility() const
{
	// Offered only for the one state it actually fixes. A button that is present but useless
	// teaches an artist to distrust every button beside it.
	return GStatus.State == EHFClaudeState::ServerNotRunning
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

FText SHFClaudePanel::StatusLine() const
{
	switch (GStatus.State)
	{
	case EHFClaudeState::Unknown:          return LOCTEXT("Unknown", "Not checked yet");
	case EHFClaudeState::Ready:            return LOCTEXT("Ready", "Connected");
	case EHFClaudeState::CliNotFound:      return LOCTEXT("NoCli", "Claude Code not found");
	case EHFClaudeState::ConfigMissing:    return LOCTEXT("NoConfig", "Server not configured");
	case EHFClaudeState::ServerNotRunning: return LOCTEXT("NoServer", "Server not running");
	case EHFClaudeState::ToolsetMissing:   return LOCTEXT("NoToolset", "HouseForge toolset missing");
	case EHFClaudeState::NotAuthenticated: return LOCTEXT("NoAuth", "Not signed in");
	default:                               return LOCTEXT("Failed", "Could not check");
	}
}

FSlateColor SHFClaudePanel::StatusColour() const
{
	switch (GStatus.State)
	{
	case EHFClaudeState::Unknown: return FSlateColor::UseSubduedForeground();
	case EHFClaudeState::Ready:   return FSlateColor(FLinearColor(0.35f, 0.85f, 0.45f));
	default:                      return FSlateColor(FLinearColor(1.0f, 0.45f, 0.35f));
	}
}

void SHFClaudePanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SHFClaudePanel::StatusLine)
				.ColorAndOpacity(this, &SHFClaudePanel::StatusColour)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SAssignNew(CheckButton, SButton)
				.Text(LOCTEXT("Check", "Check connection"))
				.ToolTipText(LOCTEXT("CheckTip",
					"Confirms Claude Code is installed, signed in, and can see HouseForge - before "
					"you spend time on a drawing set."))
				.OnClicked(FOnClicked::CreateSP(this, &SHFClaudePanel::OnCheckClicked))
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 0.0f, 4.0f, 4.0f)
		[
			SAssignNew(DetailText, STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text_Lambda([]() { return FText::FromString(GStatus.Message); })
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 0.0f, 4.0f, 6.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("StartServer", "Start the server"))
			.Visibility(this, &SHFClaudePanel::StartServerVisibility)
			.OnClicked(FOnClicked::CreateSP(this, &SHFClaudePanel::OnStartServerClicked))
		]
	];
}

#undef LOCTEXT_NAMESPACE
