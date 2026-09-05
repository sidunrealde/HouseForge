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

SHFClaudePanel::~SHFClaudePanel()
{
	if (CheckHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(CheckHandle);
	}
	FHFClaudeCli::Finish(CheckRun);
}

bool SHFClaudePanel::IsCheckIdle() const
{
	return !CheckRun.IsValid() || CheckRun.bFinished;
}

void SHFClaudePanel::BeginCheck()
{
	if (!IsCheckIdle())
	{
		return;
	}

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
		GEngine->Exec(World, TEXT("ModelContextProtocol.GenerateClientConfig ClaudeCode"));
	}

	// ------------------------------------- 3. free health check: config, server, reachability
	//
	// SPAWNED, NOT WAITED FOR. See the note on the class: this command health-checks the MCP
	// server by connecting to it, and that server is this editor. Waiting here stops the game
	// thread, the HTTP listener stops answering, and the check times out against itself.
	CheckDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	CheckOutput.Reset();

	FString Error;
	if (!FHFClaudeCli::Start(Executable, TEXT("mcp list"), CheckDirectory, CheckRun, Error))
	{
		GStatus.State = EHFClaudeState::Failed;
		GStatus.Message = Error;
		return;
	}

	GStatus.State = EHFClaudeState::Checking;
	GStatus.Message = TEXT("Asking Claude Code what it can see...");

	CheckHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &SHFClaudePanel::PumpCheck), 0.1f);
}

bool SHFClaudePanel::PumpCheck(float DeltaTime)
{
	TArray<FString> Lines;
	FHFClaudeCli::Pump(CheckRun, Lines);

	for (const FString& Line : Lines)
	{
		CheckOutput += Line + TEXT("\n");
	}

	if (!CheckRun.bFinished)
	{
		return true;
	}

	const FString StdErr = CheckRun.StdErr;
	const int32 ReturnCode = CheckRun.ReturnCode;

	FHFClaudeCli::Finish(CheckRun);
	CheckHandle.Reset();

	ConcludeCheck(CheckOutput, StdErr, ReturnCode);
	return false;
}

void SHFClaudePanel::ConcludeCheck(const FString& StdOut, const FString& StdErr, const int32 ReturnCode)
{
	if (ReturnCode != 0 && StdOut.IsEmpty())
	{
		GStatus.State = EHFClaudeState::Failed;
		GStatus.Message = FString::Printf(
			TEXT("Claude Code could not be run. It said: %s"),
			*(StdErr.IsEmpty() ? FString(TEXT("nothing")) : StdErr.TrimStartAndEnd()));
		return;
	}

	// LOGGED, because this is otherwise a black box - and it is what found the deadlock above.
	UE_LOG(LogHouseForgeEditor, Log,
		TEXT("Claude check: 'mcp list' in '%s' returned %d.\n")
		TEXT("--- stdout ---\n%s\n--- stderr ---\n%s\n---"),
		*CheckDirectory, ReturnCode,
		StdOut.IsEmpty() ? TEXT("(nothing)") : *StdOut,
		StdErr.IsEmpty() ? TEXT("(nothing)") : *StdErr);

	// Kept, so the Failed case below can SHOW what it could not read rather than describing it.
	FString ServerLine;
	{
		TArray<FString> Lines;
		StdOut.ParseIntoArrayLines(Lines, true);
		for (const FString& Line : Lines)
		{
			if (Line.TrimStartAndEnd().StartsWith(FString(FHFClaudeCli::ServerName()) + TEXT(":")))
			{
				ServerLine = Line.TrimStartAndEnd();
				break;
			}
		}
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
			"The MCP server is asking for a login. Run 'claude mcp login unreal-mcp' in a terminal "
			"in the project folder.");
		return;

	case EHFClaudeState::PendingApproval:
		GStatus.State = Health;
		GStatus.Message = FString::Printf(
			TEXT("The server is running, but Claude Code will not connect to it until you approve "
				 "it once. Open a terminal in '%s', run 'claude', and approve the 'unreal-mcp' "
				 "server it asks about. You only do this once for this project."),
			*CheckDirectory);
		return;

	case EHFClaudeState::Failed:
		// SHOWS THE LINE IT COULD NOT READ. A status this parse has not been taught is a fact
		// about the CLI, not about the server, and printing it is what turns "it says something
		// odd" into a bug report somebody can act on in one round trip.
		GStatus.State = Health;
		GStatus.Message = ServerLine.IsEmpty()
			? TEXT("Claude Code answered, but said nothing about the HouseForge server.")
			: FString::Printf(
				TEXT("Claude Code reported a state this panel does not recognise:\n\n%s"),
				*ServerLine);
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
	BeginCheck();
	return FReply::Handled();
}

FReply SHFClaudePanel::OnStartServerClicked()
{
	if (GEngine != nullptr)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		GEngine->Exec(World, TEXT("ModelContextProtocol.StartServer"));
		GEngine->Exec(World, TEXT("ModelContextProtocol.GenerateClientConfig ClaudeCode"));
	}

	// Re-checked immediately, so the button either clears the problem or proves it is something
	// else. Leaving the old failure on screen after acting on it is how a fixed thing keeps
	// looking broken.
	BeginCheck();
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
	case EHFClaudeState::Checking:         return LOCTEXT("Checking", "Checking...");
	case EHFClaudeState::Ready:            return LOCTEXT("Ready", "Connected");
	case EHFClaudeState::CliNotFound:      return LOCTEXT("NoCli", "Claude Code not found");
	case EHFClaudeState::ConfigMissing:    return LOCTEXT("NoConfig", "Server not configured");
	case EHFClaudeState::ServerNotRunning: return LOCTEXT("NoServer", "Server not running");
	case EHFClaudeState::PendingApproval:  return LOCTEXT("Pending", "Waiting for you to approve the server");
	case EHFClaudeState::ToolsetMissing:   return LOCTEXT("NoToolset", "HouseForge toolset missing");
	case EHFClaudeState::NotAuthenticated: return LOCTEXT("NoAuth", "Not signed in");
	default:                               return LOCTEXT("Failed", "Could not check");
	}
}

FSlateColor SHFClaudePanel::StatusColour() const
{
	switch (GStatus.State)
	{
	case EHFClaudeState::Unknown:  return FSlateColor::UseSubduedForeground();

	// Subdued rather than the failure colour. A check in flight is not a failure, and painting it
	// red for the seconds it takes teaches an artist to read the red as noise.
	case EHFClaudeState::Checking: return FSlateColor::UseSubduedForeground();
	case EHFClaudeState::Ready:    return FSlateColor(FLinearColor(0.35f, 0.85f, 0.45f));
	default:                       return FSlateColor(FLinearColor(1.0f, 0.45f, 0.35f));
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
				.IsEnabled_Lambda([this]() { return IsCheckIdle(); })
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
