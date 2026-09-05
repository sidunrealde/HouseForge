// Copyright Siddartha G. All Rights Reserved.

#include "UI/SHFDrawingsPanel.h"

#include "Claude/HFClaudeCli.h"
#include "DesktopPlatformModule.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "HFEditorSubsystem.h"
#include "IDesktopPlatform.h"
#include "Input/DragAndDrop.h"
#include "Misc/Paths.h"
#include "SDropTarget.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/SHFClaudePanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HouseForgeDrawings"

namespace
{
	/** The one filter string, so the dialog and the drop test cannot drift apart. */
	const TCHAR* const DrawingDialogFilter =
		TEXT("Drawings (*.png;*.jpg;*.jpeg;*.pdf)|*.png;*.jpg;*.jpeg;*.pdf|")
		TEXT("Images (*.png;*.jpg;*.jpeg)|*.png;*.jpg;*.jpeg|")
		TEXT("PDF sheet sets (*.pdf)|*.pdf");

	UHFEditorSubsystem* Subsystem()
	{
		return GEditor ? GEditor->GetEditorSubsystem<UHFEditorSubsystem>() : nullptr;
	}
}

bool SHFDrawingsPanel::CanAcceptDrag(TSharedPtr<FDragDropOperation> Operation)
{
	// ANY readable file is enough to accept the drag, not EVERY file. Dragging a whole sheet folder
	// that happens to carry a stray .txt should still work, and ImportDrawings reports what it
	// skipped. Requiring all of them would refuse the most common real drop there is.
	if (!Operation.IsValid() || !Operation->IsOfType<FExternalDragOperation>())
	{
		return false;
	}

	const TSharedPtr<FExternalDragOperation> Files =
		StaticCastSharedPtr<FExternalDragOperation>(Operation);

	if (!Files->HasFiles())
	{
		return false;
	}

	for (const FString& Path : Files->GetFiles())
	{
		if (UHFEditorSubsystem::IsReadableDrawing(Path))
		{
			return true;
		}
	}
	return false;
}

FReply SHFDrawingsPanel::OnDropped(const FGeometry& Geometry, const FDragDropEvent& Event)
{
	const TSharedPtr<FExternalDragOperation> Files =
		Event.GetOperationAs<FExternalDragOperation>();

	if (!Files.IsValid() || !Files->HasFiles())
	{
		return FReply::Unhandled();
	}

	Import(Files->GetFiles());
	return FReply::Handled();
}

FReply SHFDrawingsPanel::OnBrowseClicked()
{
	IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
	if (Desktop == nullptr)
	{
		return FReply::Handled();
	}

	TArray<FString> Selected;
	const bool bPicked = Desktop->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Import interior drawings"),
		FPaths::ProjectDir(),
		TEXT(""),
		DrawingDialogFilter,
		EFileDialogFlags::Multiple,
		Selected);

	if (bPicked && !Selected.IsEmpty())
	{
		Import(Selected);
	}
	return FReply::Handled();
}

void SHFDrawingsPanel::Import(const TArray<FString>& Paths)
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (Editor == nullptr)
	{
		return;
	}

	const FString SetName = SetNameBox.IsValid() ? SetNameBox->GetText().ToString().TrimStartAndEnd()
		: FString();

	TArray<FString> Imported;
	const FHFOperationResult Result = Editor->ImportDrawings(Paths, SetName, Imported);

	// The subsystem's own message, in the panel rather than in a dialog. It already names what it
	// skipped and why - a PDF that could not be rasterised says so - and a status line an artist
	// can re-read beats a modal they dismissed before finishing the sentence.
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::FromString(Result.Message));
		StatusText->SetColorAndOpacity(Result.bSuccess
			? FSlateColor::UseForeground()
			: FSlateColor(FLinearColor(1.0f, 0.45f, 0.35f)));
	}

	RefreshSets();
}

void SHFDrawingsPanel::RefreshSets()
{
	if (!SetList.IsValid())
	{
		return;
	}

	SetList->ClearChildren();
	SetCount = 0;

	UHFEditorSubsystem* Editor = Subsystem();
	if (Editor == nullptr)
	{
		return;
	}

	// ListDrawings reports sheets, relative to the drawings root. The panel shows SETS, because a
	// set is what a house is read from and what GENERATE will name - so fold the sheets into their
	// first path component and count them.
	TMap<FString, int32> SheetsPerSet;
	for (const FString& Sheet : Editor->ListDrawings())
	{
		FString Set;
		FString Remainder;
		if (!Sheet.Split(TEXT("/"), &Set, &Remainder))
		{
			// A sheet sitting loose in the drawings root rather than in a set folder.
			Set = TEXT("(loose sheets)");
		}
		SheetsPerSet.FindOrAdd(Set)++;
	}

	if (SheetsPerSet.IsEmpty())
	{
		SetList->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoSets", "No drawings imported yet."))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
		return;
	}

	SetCount = SheetsPerSet.Num();
	SheetsPerSet.KeySort([](const FString& A, const FString& B) { return A < B; });

	for (const TPair<FString, int32>& Entry : SheetsPerSet)
	{
		SetList->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(FText::Format(
					LOCTEXT("SetRow", "{0}  -  {1} sheet(s)"),
					FText::FromString(Entry.Key),
					FText::AsNumber(Entry.Value)))
			];
	}
}

// =========================================================================== generating a house

SHFDrawingsPanel::~SHFDrawingsPanel()
{
	// A panel closed mid-generation must not leave the CLI running: it would hold an MCP session
	// open against an editor that has stopped listening, and nothing would ever read its pipe.
	if (PumpHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PumpHandle);
	}
	FHFClaudeCli::Finish(Run);
}

bool SHFDrawingsPanel::CanGenerate() const
{
	if (Run.IsValid() && !Run.bFinished)
	{
		return false;
	}

	// THE GATE, and the only one in this panel. Importing and listing above are deliberately live
	// whatever Claude is doing - it is only building a house from a drawing that needs a model.
	// SetCount, NOT SetList->NumSlots(). An empty drawings folder still puts one row in the list -
	// the row that says it is empty - so NumSlots() answers "is anything drawn here", which is a
	// different question and is always yes. Generate would have been enabled with no drawings, and
	// the run would have asked Claude to build a set that does not exist.
	return SHFClaudePanel::LastStatus().IsReady() && SetCount > 0;
}

FText SHFDrawingsPanel::GenerateLabel() const
{
	return (Run.IsValid() && !Run.bFinished)
		? LOCTEXT("Generating", "Building...")
		: LOCTEXT("Generate", "Generate");
}

FText SHFDrawingsPanel::GenerateTooltip() const
{
	// Says WHICH precondition is missing. "Disabled" with no reason is the most common way a UI
	// wastes somebody's afternoon.
	if (Run.IsValid() && !Run.bFinished)
	{
		return LOCTEXT("BusyTip", "A house is being built. Wait for it to finish.");
	}

	if (!SHFClaudePanel::LastStatus().IsReady())
	{
		return LOCTEXT("NoClaudeTip",
			"Claude Code is not connected. Check the connection in the CLAUDE section above - "
			"reading a drawing needs it, though importing drawings does not.");
	}

	if (SetCount == 0)
	{
		return LOCTEXT("NoSetTip", "Import a drawing set first - drop the sheets above.");
	}

	return LOCTEXT("GenerateTip",
		"Hands the drawings to Claude Code, which reads them and builds the flat in this editor.");
}

FReply SHFDrawingsPanel::OnGenerateClicked()
{
	const FString Executable = FHFClaudeCli::FindExecutable();
	if (Executable.IsEmpty())
	{
		Trace = TEXT("Claude Code is not installed, or not on PATH.");
		return FReply::Handled();
	}

	RunningSet = SetNameBox.IsValid() ? SetNameBox->GetText().ToString().TrimStartAndEnd() : FString();
	if (RunningSet.IsEmpty())
	{
		// Nothing typed: build the set that is actually there. Naming it explicitly beats letting
		// Claude choose, which on a folder with several sets would be a coin toss.
		UHFEditorSubsystem* Editor = Subsystem();
		const TArray<FString> Sheets = Editor ? Editor->ListDrawings() : TArray<FString>();
		if (Sheets.Num() > 0)
		{
			FString Remainder;
			if (!Sheets[0].Split(TEXT("/"), &RunningSet, &Remainder))
			{
				RunningSet = Sheets[0];
			}
		}
	}

	const FString ConfigPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".mcp.json")));

	Trace = FString::Printf(TEXT("Building '%s'...\n"), *RunningSet);

	FString Error;
	if (!FHFClaudeCli::Start(
		Executable,
		FHFClaudeCli::BuildGenerateArguments(RunningSet, ConfigPath),
		// The PROJECT directory this time, not the neutral one the cheap checks use: a generation
		// wants the project's CLAUDE.md, which is what tells Claude the rules it is building under.
		FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()),
		Run,
		Error))
	{
		Trace += Error;
		return FReply::Handled();
	}

	PumpHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &SHFDrawingsPanel::PumpGeneration), 0.1f);

	return FReply::Handled();
}

bool SHFDrawingsPanel::PumpGeneration(float DeltaTime)
{
	TArray<FString> Lines;
	FHFClaudeCli::Pump(Run, Lines);

	for (const FString& Line : Lines)
	{
		AppendTrace(Line);
	}

	if (!Run.bFinished)
	{
		return true;
	}

	// stderr is reported only on failure. On a good run it carries progress chatter nobody needs,
	// but on a bad one it is the only place the real reason appears.
	if (Run.ReturnCode != 0)
	{
		Trace += FString::Printf(TEXT("\nClaude exited with code %d.\n"), Run.ReturnCode);
		if (!Run.StdErr.IsEmpty())
		{
			Trace += Run.StdErr.TrimStartAndEnd();
		}
	}

	if (TraceBox.IsValid())
	{
		TraceBox->SetText(FText::FromString(Trace));
	}

	FHFClaudeCli::Finish(Run);
	PumpHandle.Reset();
	RefreshSets();
	return false;
}

void SHFDrawingsPanel::AppendTrace(const FString& JsonLine)
{
	TSharedPtr<FJsonObject> Object;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonLine);

	if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
	{
		// Not JSON. Shown rather than swallowed - if the CLI printed a plain-text error, that line
		// is the most useful thing on screen, and dropping it would leave the panel silent about
		// the one thing that went wrong.
		Trace += JsonLine + TEXT("\n");
	}
	else
	{
		const FString Type = Object->GetStringField(TEXT("type"));

		if (Type == TEXT("assistant") || Type == TEXT("user"))
		{
			// The message text, which is Claude narrating what it is doing.
			const TSharedPtr<FJsonObject>* Message = nullptr;
			if (Object->TryGetObjectField(TEXT("message"), Message) && Message != nullptr)
			{
				const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
				if ((*Message)->TryGetArrayField(TEXT("content"), Content) && Content != nullptr)
				{
					for (const TSharedPtr<FJsonValue>& Block : *Content)
					{
						const TSharedPtr<FJsonObject> BlockObject = Block->AsObject();
						if (!BlockObject.IsValid())
						{
							continue;
						}

						const FString BlockType = BlockObject->GetStringField(TEXT("type"));
						if (BlockType == TEXT("text"))
						{
							Trace += BlockObject->GetStringField(TEXT("text")) + TEXT("\n");
						}
						else if (BlockType == TEXT("tool_use"))
						{
							Trace += FString::Printf(TEXT("  [%s]\n"),
								*BlockObject->GetStringField(TEXT("name")));
						}
					}
				}
			}
		}
		else if (Type == TEXT("result"))
		{
			const FString Result = Object->GetStringField(TEXT("result"));
			if (!Result.IsEmpty())
			{
				Trace += TEXT("\n") + Result + TEXT("\n");
			}

			// NOT "money taken from your account", which is what this used to say.
			//
			// total_cost_usd is a computed API-equivalent price derived from token usage, and
			// the CLI emits it identically whether it is authenticated by an API key or by a
			// Pro/Max subscription. On a subscription nothing is charged at all - so telling an
			// artist a build "used $0.87 of your Claude account" is false, and false in the
			// direction that makes a reasonable person stop using the tool.
			double Cost = 0.0;
			if (Object->TryGetNumberField(TEXT("total_cost_usd"), Cost) && Cost > 0.0)
			{
				Trace += FString::Printf(
					TEXT("\nDone - about $%.2f of tokens at API rates. On a Claude ")
					TEXT("subscription that is what it would have cost, not a charge.\n"), Cost);
			}
		}
	}

	if (TraceBox.IsValid())
	{
		TraceBox->SetText(FText::FromString(Trace));
	}
}

void SHFDrawingsPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		// ------------------------------------------------------------------ the drop target
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 4.0f)
		[
			SNew(SBox)
			.HeightOverride(84.0f)
			[
				SNew(SDropTarget)
				.OnAllowDrop(SDropTarget::FVerifyDrag::CreateStatic(&SHFDrawingsPanel::CanAcceptDrag))
				.OnIsRecognized(SDropTarget::FVerifyDrag::CreateStatic(&SHFDrawingsPanel::CanAcceptDrag))
				.OnDropped(FOnDrop::CreateSP(this, &SHFDrawingsPanel::OnDropped))
				[
					SNew(SBox)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("DropHere", "Drop interior drawings here"))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(0.0f, 2.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("DropTypes", "PNG, JPG, or PDF sheet sets"))
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						]
					]
				]
			]
		]

		// ------------------------------------------------- set name, and the non-drag way in
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SetLabel", "Set"))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(SetNameBox, SEditableTextBox)
				.HintText(LOCTEXT("SetHint", "named after the first file"))
				.ToolTipText(LOCTEXT("SetTip",
					"The folder under Reference/Drawings to import into. Leave blank to name it "
					"after the first file dropped."))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Browse", "Browse..."))
				.ToolTipText(LOCTEXT("BrowseTip", "Pick drawings with a file dialog instead of dragging them."))
				.OnClicked(FOnClicked::CreateSP(this, &SHFDrawingsPanel::OnBrowseClicked))
			]
		]

		// ----------------------------------------------------------- what the last import did
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 4.0f)
		[
			SAssignNew(StatusText, STextBlock)
			.AutoWrapText(true)
			.Text(FText::GetEmpty())
		]

		// ------------------------------------------------------------------ what is already in
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 4.0f, 4.0f, 6.0f)
		[
			SAssignNew(SetList, SVerticalBox)
		]

		// ---------------------------------------------------------------------------- generate
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 2.0f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(this, &SHFDrawingsPanel::GenerateLabel)
			.ToolTipText(this, &SHFDrawingsPanel::GenerateTooltip)
			.IsEnabled(this, &SHFDrawingsPanel::CanGenerate)
			.OnClicked(FOnClicked::CreateSP(this, &SHFDrawingsPanel::OnGenerateClicked))
		]

		// The trace. Read-only, but a text box rather than a label so an artist can select a line
		// and paste it into a bug report - which is most of what a trace is for.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 4.0f, 4.0f, 6.0f)
		[
			SNew(SBox)
			.MaxDesiredHeight(220.0f)
			[
				SAssignNew(TraceBox, SMultiLineEditableTextBox)
				.IsReadOnly(true)
				.AlwaysShowScrollbars(false)
				.AutoWrapText(true)
				.Text_Lambda([this]() { return FText::FromString(Trace); })
			]
		]
	];

	RefreshSets();
}

#undef LOCTEXT_NAMESPACE
