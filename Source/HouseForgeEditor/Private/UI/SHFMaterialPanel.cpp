// Copyright Siddartha G. All Rights Reserved.

#include "UI/SHFMaterialPanel.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "Geometry/HFMeshOps.h"
#include "IStructureDetailsView.h"
#include "Materials/HFMaterialLibrary.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "HouseForgePanel"

namespace
{
	/** The enum's own display name: "WallPaint" reads as "Wall Paint" without a second table to keep. */
	FText RoleDisplayName(EHFSurfaceRole Role)
	{
		const UEnum* Enum = StaticEnum<EHFSurfaceRole>();
		return Enum ? Enum->GetDisplayNameTextByValue(static_cast<int64>(Role))
					: FText::FromString(TEXT("Surface"));
	}

	/**
	 * How much of the level this role covers, in one short line.
	 *
	 * Area in square metres rather than a triangle count, because area is what says whether a
	 * finish decides a lot of the render or a little: a knob has more triangles than a wall and a
	 * thousandth of its area. Roles covering nothing say so rather than showing a zero, which reads
	 * as a measurement failure rather than as an absence.
	 */
	FText DescribeUsage(const FHFSurfaceUsage& Usage)
	{
		if (Usage.ElementCount == 0)
		{
			return LOCTEXT("RoleNotInLevel", "not in this level");
		}

		FString Text = FString::Printf(TEXT("%d element%s  %.1f m2"),
			Usage.ElementCount, Usage.ElementCount == 1 ? TEXT("") : TEXT("s"),
			Usage.AreaSquareMetres);

		if (Usage.ArtistEditedElementCount > 0)
		{
			Text += FString::Printf(TEXT("  %d hand-edited"), Usage.ArtistEditedElementCount);
		}

		return FText::FromString(Text);
	}
}

SHFMaterialPanel::~SHFMaterialPanel()
{
	if (GEditor != nullptr)
	{
		GEditor->UnregisterForUndo(this);
	}

	if (GEngine != nullptr)
	{
		GEngine->OnLevelActorAdded().RemoveAll(this);
		GEngine->OnLevelActorDeleted().RemoveAll(this);
	}
}

UHFEditorSubsystem* SHFMaterialPanel::Editor() const
{
	return GEditor ? GEditor->GetEditorSubsystem<UHFEditorSubsystem>() : nullptr;
}

TArray<TSharedPtr<FHFSurfaceRoleRow>> SHFMaterialPanel::BuildRoleRows()
{
	TArray<TSharedPtr<FHFSurfaceRoleRow>> Built;

	UHFEditorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UHFEditorSubsystem>() : nullptr;
	if (Subsystem == nullptr)
	{
		return Built;
	}

	// The subsystem decides how many roles there are and what covers what. The panel does not count
	// them itself and does not hold a list of its own - a second list is a second thing to forget to
	// update when a role is added, and the last two were added one milestone apart.
	const TArray<FHFSurfaceUsage> Usage = Subsystem->GetSurfaceUsage();
	Built.Reserve(Usage.Num());

	for (const FHFSurfaceUsage& Row : Usage)
	{
		const TSharedRef<FHFSurfaceRoleRow> Entry = MakeShared<FHFSurfaceRoleRow>();
		Entry->Role = Row.Role;
		Entry->Name = RoleDisplayName(Row.Role);
		Entry->Usage = Row;
		Built.Add(Entry);
	}

	return Built;
}

void SHFMaterialPanel::Construct(const FArguments& InArgs)
{
	if (GEditor != nullptr)
	{
		GEditor->RegisterForUndo(this);
	}

	// The headline figures are about the level, so they go stale the moment a house is built - and
	// a house is normally built by Claude over MCP while this panel is open beside it.
	if (GEngine != nullptr)
	{
		GEngine->OnLevelActorAdded().AddSP(this, &SHFMaterialPanel::HandleLevelChanged);
		GEngine->OnLevelActorDeleted().AddSP(this, &SHFMaterialPanel::HandleLevelChanged);
	}

	FinishStorage = MakeShared<FStructOnScope>(FHFSurfaceFinish::StaticStruct());

	FPropertyEditorModule& PropertyEditor =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	FDetailsViewArgs DetailsArgs;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsArgs.bAllowSearch = true;
	DetailsArgs.bShowOptions = false;
	DetailsArgs.bHideSelectionTip = true;
	// The parameter list is the tall part of the panel and it scrolls itself, in a fill-height slot
	// below. The alternative - an unbounded auto-height view inside an outer scroll box - reads as
	// a details view collapsed to three rows, because SDetailsView slots its tree with FillHeight.
	DetailsArgs.bShowScrollBar = true;
	DetailsArgs.NotifyHook = this;        // See the class comment: this is the interactive tier.

	FStructureDetailsViewArgs StructArgs;
	FinishView = PropertyEditor.CreateStructureDetailView(DetailsArgs, StructArgs, FinishStorage);

	ChildSlot
	[
		SNew(SVerticalBox)

		// ---------------------------------------------------------------- where changes land
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 6.0f, 8.0f, 2.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(this, &SHFMaterialPanel::GetLibraryLine)
			.ColorAndOpacity(this, &SHFMaterialPanel::GetLibraryLineColour)
		]

		// ---------------------------------------------------------------------- the role list
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 4.0f)
		[
			SNew(SBox)
			.HeightOverride(190.0f)
			[
				SAssignNew(RoleList, SListView<TSharedPtr<FHFSurfaceRoleRow>>)
				.ListItemsSource(&Rows)
				.SelectionMode(ESelectionMode::Single)
				.OnGenerateRow(this, &SHFMaterialPanel::MakeRoleRow)
				.OnSelectionChanged(this, &SHFMaterialPanel::OnRoleSelected)
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 2.0f)
		[
			SNew(SSeparator)
		]

		// ------------------------------------------------------------ the selected role's name
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 4.0f, 8.0f, 0.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				.Text(this, &SHFMaterialPanel::GetSelectedRoleName)
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Reset", "Reset"))
				.ToolTipText(LOCTEXT("ResetTooltip",
					"Put this surface back to the finish HouseForge ships. Undoable."))
				.OnClicked(this, &SHFMaterialPanel::OnResetClicked)
			]
		]

		// ---------------------------------------------------------------- the whole parameter set
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4.0f, 4.0f)
		[
			FinishView.IsValid() && FinishView->GetWidget().IsValid()
				? FinishView->GetWidget().ToSharedRef()
				: StaticCastSharedRef<SWidget>(SNew(SSpacer))
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 2.0f)
		[
			SNew(SSeparator)
		]

		// ------------------------------------------------------------------- level-wide actions
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("Save", "Save finishes"))
				.ToolTipText(LOCTEXT("SaveTooltip",
					"Write the finishes to disk so they survive a restart. Until this is pressed the "
					"changes are live in the editor but unsaved."))
				.IsEnabled(this, &SHFMaterialPanel::HasUnsavedChanges)
				.OnClicked(this, &SHFMaterialPanel::OnSaveClicked)
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Reapply", "Re-apply to level"))
				.ToolTipText(LOCTEXT("ReapplyTooltip",
					"Give every element in the level its materials again. Only needed if a surface is "
					"rendering the wrong finish - it changes no geometry and no hand edits."))
				.OnClicked(this, &SHFMaterialPanel::OnReapplyClicked)
			]
		]

		// -------------------------------------------------------------------------- what happened
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 2.0f, 8.0f, 8.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Visibility(this, &SHFMaterialPanel::GetMessageVisibility)
			.Text(this, &SHFMaterialPanel::GetMessage)
			.ColorAndOpacity(this, &SHFMaterialPanel::GetMessageColour)
		]
	];

	RefreshRoles();
	SelectRole(EHFSurfaceRole::WallPaint);
	RememberLibrary();
}

// ==================================================================================== the list

void SHFMaterialPanel::RefreshRoles()
{
	const EHFSurfaceRole Keep = SelectedRole;

	Rows = BuildRoleRows();

	if (RoleList.IsValid())
	{
		RoleList->RequestListRefresh();

		// Selection is restored by role rather than by index. The two agree today and would stop
		// agreeing the moment roles were ever filtered, and a selection that silently moved to a
		// neighbouring surface is the kind of thing somebody discovers after painting the wrong one.
		for (const TSharedPtr<FHFSurfaceRoleRow>& Row : Rows)
		{
			if (Row.IsValid() && Row->Role == Keep)
			{
				RoleList->SetSelection(Row, ESelectInfo::Direct);
				break;
			}
		}
	}
}

TSharedRef<ITableRow> SHFMaterialPanel::MakeRoleRow(TSharedPtr<FHFSurfaceRoleRow> Row,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	const EHFSurfaceRole Role = Row.IsValid() ? Row->Role : EHFSurfaceRole::WallPaint;

	return SNew(STableRow<TSharedPtr<FHFSurfaceRoleRow>>, OwnerTable)
		.Padding(FMargin(2.0f))
		[
			SNew(SHorizontalBox)

			// The swatch reads the library live rather than the row's snapshot, so dragging the
			// colour picker recolours the list entry as it goes. It is also what makes the list
			// scannable without reading eighteen names.
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SBox).WidthOverride(18.0f).HeightOverride(18.0f)
				[
					SNew(SColorBlock)
					.Color(this, &SHFMaterialPanel::GetRoleColour, Role)
					.ShowBackgroundForAlpha(false)
				]
			]

			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Row.IsValid() ? Row->Name : FText::GetEmpty())
			]

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0.0f, 2.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(Row.IsValid() ? DescribeUsage(Row->Usage) : FText::GetEmpty())
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		];
}

FLinearColor SHFMaterialPanel::GetRoleColour(EHFSurfaceRole Role) const
{
	const UHFMaterialLibrary* Library = UHFMaterialLibrary::Get();
	return Library ? Library->FinishForRole(Role).BaseColor : FLinearColor::Gray;
}

void SHFMaterialPanel::OnRoleSelected(TSharedPtr<FHFSurfaceRoleRow> Row, ESelectInfo::Type SelectInfo)
{
	if (Row.IsValid())
	{
		SelectRole(Row->Role);
	}
}

// ================================================================================= the finish

FHFSurfaceFinish* SHFMaterialPanel::EditedFinish() const
{
	return FinishStorage.IsValid()
		? reinterpret_cast<FHFSurfaceFinish*>(FinishStorage->GetStructMemory())
		: nullptr;
}

void SHFMaterialPanel::SelectRole(EHFSurfaceRole Role)
{
	SelectedRole = Role;

	UHFEditorSubsystem* Subsystem = Editor();
	FHFSurfaceFinish* Target = EditedFinish();
	if (Subsystem == nullptr || Target == nullptr)
	{
		return;
	}

	Subsystem->GetSurfaceFinish(Role, *Target);

	// Handed to the view again so every property row re-reads the memory it points at. Without this
	// the struct changes underneath a view still showing the previous role's numbers.
	if (FinishView.IsValid())
	{
		FinishView->SetStructureData(FinishStorage);
	}
}

void SHFMaterialPanel::ApplyEditedFinish(EHFMaterialPush Mode)
{
	UHFEditorSubsystem* Subsystem = Editor();
	const FHFSurfaceFinish* Source = EditedFinish();
	if (Subsystem == nullptr || Source == nullptr)
	{
		return;
	}

	const FHFOperationResult Result = Subsystem->SetSurfaceFinish(SelectedRole, *Source, Mode);

	// A drag says nothing. Only the decision reports, or the panel would spend a gesture flickering
	// a line of text that says the same thing sixty times a second.
	if (Mode == EHFMaterialPush::Commit)
	{
		Say(Result);
		RememberLibrary();
	}
}

void SHFMaterialPanel::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent,
	FProperty* PropertyThatChanged)
{
	ApplyEditedFinish(PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive
		? EHFMaterialPush::Interactive
		: EHFMaterialPush::Commit);
}

void SHFMaterialPanel::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent,
	FEditPropertyChain* PropertyThatChanged)
{
	ApplyEditedFinish(PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive
		? EHFMaterialPush::Interactive
		: EHFMaterialPush::Commit);
}

// ======================================================================================= undo

void SHFMaterialPanel::RememberLibrary()
{
	LastPushed.Reset();

	const UHFMaterialLibrary* Library = UHFMaterialLibrary::Get();
	if (Library == nullptr)
	{
		return;
	}

	for (int32 Index = 0; Index < FHFMeshOps::NumSurfaceRoles(); ++Index)
	{
		const EHFSurfaceRole Role = static_cast<EHFSurfaceRole>(Index);
		LastPushed.Add(Role, Library->FinishForRole(Role));
	}
}

void SHFMaterialPanel::PushRolesChangedSince()
{
	UHFMaterialLibrary* Library = UHFMaterialLibrary::Get();
	if (Library == nullptr)
	{
		return;
	}

	const UScriptStruct* Struct = FHFSurfaceFinish::StaticStruct();

	for (int32 Index = 0; Index < FHFMeshOps::NumSurfaceRoles(); ++Index)
	{
		const EHFSurfaceRole Role = static_cast<EHFSurfaceRole>(Index);
		const FHFSurfaceFinish& Now = Library->FinishForRole(Role);
		const FHFSurfaceFinish* Then = LastPushed.Find(Role);

		// Compared through the UScriptStruct rather than field by field. A hand-written comparison
		// is one more list to forget a field from, and forgetting one here means an undo that
		// silently leaves that value applied.
		if (Then != nullptr && Struct->CompareScriptStruct(&Now, Then, 0))
		{
			continue;
		}

		Library->PushFinish(Role, EHFMaterialPush::Commit);
	}

	RememberLibrary();
}

void SHFMaterialPanel::PostUndo(bool bSuccess)
{
	// PostUndo fires for every transaction in the editor, not only this panel's. Diffing against
	// what was last pushed is what keeps an unrelated undo in the level from costing eighteen
	// material recompiles, and is also what makes Ctrl+Z reach the renderer at all: the transaction
	// restores the data asset, and nothing but this re-pushes the instances it was compiled into.
	PushRolesChangedSince();
	SelectRole(SelectedRole);
	RefreshRoles();
}

void SHFMaterialPanel::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

// ==================================================================================== actions

FReply SHFMaterialPanel::OnResetClicked()
{
	if (UHFEditorSubsystem* Subsystem = Editor())
	{
		Say(Subsystem->ResetSurfaceFinish(SelectedRole));
		SelectRole(SelectedRole);
		RememberLibrary();
	}
	return FReply::Handled();
}

FReply SHFMaterialPanel::OnSaveClicked()
{
	if (UHFEditorSubsystem* Subsystem = Editor())
	{
		int32 Saved = 0;
		Say(Subsystem->SaveSurfaceLibrary(Saved));
	}
	return FReply::Handled();
}

FReply SHFMaterialPanel::OnReapplyClicked()
{
	if (UHFEditorSubsystem* Subsystem = Editor())
	{
		int32 Components = 0;
		Say(Subsystem->ReapplyMaterialsToLevel(Components));
		RefreshRoles();
	}
	return FReply::Handled();
}

bool SHFMaterialPanel::HasUnsavedChanges() const
{
	const UHFEditorSubsystem* Subsystem = Editor();
	return Subsystem != nullptr && Subsystem->HasUnsavedSurfaceChanges();
}

void SHFMaterialPanel::HandleLevelChanged(AActor* Actor)
{
	// Debounced through a one-shot timer rather than refreshed here. Building the reference flat
	// spawns 155 actors, and re-walking every mesh in the level once per spawn would make the panel
	// the slowest thing about generating a house.
	if (bRefreshQueued || !FSlateApplication::IsInitialized())
	{
		return;
	}

	bRefreshQueued = true;
	RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SHFMaterialPanel::RefreshTick));
}

EActiveTimerReturnType SHFMaterialPanel::RefreshTick(double, float)
{
	bRefreshQueued = false;
	RefreshRoles();
	return EActiveTimerReturnType::Stop;
}

// ===================================================================================== saying

void SHFMaterialPanel::Say(const FHFOperationResult& Result)
{
	Message = FText::FromString(Result.Message);
	bMessageIsFailure = !Result.bSuccess;
}

FText SHFMaterialPanel::GetMessage() const
{
	return Message;
}

EVisibility SHFMaterialPanel::GetMessageVisibility() const
{
	return Message.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

FSlateColor SHFMaterialPanel::GetMessageColour() const
{
	return bMessageIsFailure ? FSlateColor(FLinearColor(1.0f, 0.45f, 0.35f))
							 : FSlateColor::UseSubduedForeground();
}

FText SHFMaterialPanel::GetSelectedRoleName() const
{
	return RoleDisplayName(SelectedRole);
}

FText SHFMaterialPanel::GetLibraryLine() const
{
	const UHFEditorSubsystem* Subsystem = Editor();
	if (Subsystem == nullptr)
	{
		return LOCTEXT("NoSubsystem", "The HouseForge editor subsystem is not available.");
	}

	// The failure message from the subsystem verbatim, because it names the missing asset and its
	// path. A panel-local paraphrase would be a second wording of the same fact to keep in step.
	UHFMaterialLibrary* Library = nullptr;
	const FHFOperationResult Editable = Subsystem->GetEditableMaterialLibrary(Library);
	if (!Editable.bSuccess)
	{
		return FText::FromString(Editable.Message);
	}

	return FText::Format(
		LOCTEXT("LibraryLine", "Finishes in {0}. Changes apply to every element using the surface."),
		FText::FromString(Library->GetName()));
}

FSlateColor SHFMaterialPanel::GetLibraryLineColour() const
{
	const UHFEditorSubsystem* Subsystem = Editor();
	if (Subsystem == nullptr)
	{
		return FSlateColor(FLinearColor(1.0f, 0.45f, 0.35f));
	}

	UHFMaterialLibrary* Unused = nullptr;
	return Subsystem->GetEditableMaterialLibrary(Unused).bSuccess
		? FSlateColor::UseSubduedForeground()
		: FSlateColor(FLinearColor(1.0f, 0.45f, 0.35f));
}

#undef LOCTEXT_NAMESPACE
