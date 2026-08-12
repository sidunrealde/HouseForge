// Copyright Siddartha G. All Rights Reserved.

#include "UI/SHFDrawingsPanel.h"

#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "HFEditorSubsystem.h"
#include "IDesktopPlatform.h"
#include "Input/DragAndDrop.h"
#include "Misc/Paths.h"
#include "SDropTarget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
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
	];

	RefreshSets();
}

#undef LOCTEXT_NAMESPACE
