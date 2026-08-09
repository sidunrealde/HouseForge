// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "HFEditorSubsystem.h"
#include "Materials/HFSurfaceFinish.h"
#include "Misc/NotifyHook.h"
#include "Model/HFTypes.h"
#include "UObject/StructOnScope.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class IStructureDetailsView;
class STextBlock;
class UHFMaterialLibrary;

/** One row of the role list: what the surface is called, and how much of the level it covers. */
struct FHFSurfaceRoleRow
{
	EHFSurfaceRole Role = EHFSurfaceRole::WallPaint;

	/** The enum's own display name, so a role renamed in one place is renamed everywhere. */
	FText Name;

	FHFSurfaceUsage Usage;
};

/**
 * THE SURFACES SECTION: what every surface in the flat is made of, and the controls to change it.
 *
 * The first thing the user asked for, in their first brief - "a separate UI panel where all the
 * material textures and uvs and whatever else might be necessary can be changed". Eighteen surface
 * roles down the top, the selected role's full finish underneath, and every change landing live on
 * every element using that role without any of them being visited.
 *
 *
 * WHY THE PARAMETERS ARE A DETAILS VIEW AND NOT TWENTY-FIVE HAND-BUILT CONTROLS
 * -----------------------------------------------------------------------------
 * FHFSurfaceFinish already carries, on every field, the clamp, the unit, the slider range and a
 * sentence of documentation - and the texture slots are TSoftObjectPtr<UTexture>, which a details
 * view renders as a real Content Browser picker with drag-and-drop, a thumbnail and a browse-to
 * arrow. Hand-building that would be several hundred lines whose only distinguishing feature is
 * the ways it could fall out of step with the struct. This is the same argument
 * Docs/PanelAndBakeDesign.md makes for not reimplementing FHFWall's fields, applied to the struct
 * it was written about.
 *
 * The panel's own work is the part a details view cannot do: which role, how much of the level it
 * covers, what it is in a sentence, and where the change lands.
 *
 *
 * WHY FNotifyHook RATHER THAN OnFinishedChangingProperties
 * --------------------------------------------------------
 * The two-tier update is the whole reason a drag is smooth, and it needs to hear about a change
 * WHILE the mouse is down. IDetailsView's OnFinishedChangingProperties does not fire then -
 * FPropertyValueImpl::ImportText only calls NotifyFinishedChangingProperties when
 * !bInteractiveChangeInProgress (PropertyHandleImpl.cpp:697) - so a panel built on it shows nothing
 * until release, which reads as a broken slider. The notify hook is called from
 * FPropertyNode::NotifyPostChange on every change, interactive ones included, and the
 * FPropertyChangedEvent carries the ChangeType that says which tier this is.
 *
 *
 * NOTHING HERE CAN REACH A VERTEX
 * -------------------------------
 * Every write goes through UHFEditorSubsystem::SetSurfaceFinish, which writes a material instance
 * and a data asset. No mesh is touched, no Regenerate is called, and bArtistEdited is neither read
 * nor written - so changing a colour cannot destroy a hand edit.
 */
class SHFMaterialPanel : public SCompoundWidget, public FNotifyHook, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SHFMaterialPanel) {}
	SLATE_END_ARGS()

	SHFMaterialPanel() = default;
	virtual ~SHFMaterialPanel() override;

	void Construct(const FArguments& InArgs);

	/**
	 * Every role, in enum order, with its usage. The panel's list source.
	 *
	 * Static, and free of any widget, so "the panel lists every role" is testable without a tab.
	 * Counted from the subsystem rather than from a literal: the role count has been 16, 17 and now
	 * 18, and a panel hard-coded to sixteen silently drops whichever roles were added last.
	 */
	static TArray<TSharedPtr<FHFSurfaceRoleRow>> BuildRoleRows();

	// FNotifyHook. Both overloads land on the same handler: which one the property editor picks
	// depends on how deep the changed property sits, and the tier is decided by the event either way.
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent,
		FProperty* PropertyThatChanged) override;
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent,
		FEditPropertyChain* PropertyThatChanged) override;

	// FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	/**
	 * Loads a role's finish into the parameter view. What clicking a row in the list does.
	 *
	 * Public because selecting a surface is the panel's own verb rather than an implementation
	 * detail of its list - a later "edit the surface of the selected actor" would call this - and
	 * because it is half of what makes the panel's edit path testable without a mouse.
	 */
	void SelectRole(EHFSurfaceRole Role);

	/** The role the parameter view is showing. */
	EHFSurfaceRole GetSelectedRole() const { return SelectedRole; }

	/**
	 * The finish being edited, as the struct it really is.
	 *
	 * This is the memory the details view writes into, so writing to it and then calling
	 * NotifyPostChange is exactly what a user dragging a slider does - which is how the panel's own
	 * wiring gets tested rather than only the subsystem underneath it.
	 */
	FHFSurfaceFinish* EditedFinish() const;

private:
	UHFEditorSubsystem* Editor() const;

	/** Rebuilds the role list, keeping the current selection. Called on construct and after a commit. */
	void RefreshRoles();

	/** One push, at the tier the change type asks for. */
	void ApplyEditedFinish(EHFMaterialPush Mode);

	TSharedRef<ITableRow> MakeRoleRow(TSharedPtr<FHFSurfaceRoleRow> Row,
		const TSharedRef<STableViewBase>& OwnerTable);
	void OnRoleSelected(TSharedPtr<FHFSurfaceRoleRow> Row, ESelectInfo::Type SelectInfo);

	FReply OnResetClicked();
	FReply OnSaveClicked();
	FReply OnReapplyClicked();
	bool HasUnsavedChanges() const;

	/** Live from the library, not from the row's snapshot, so a colour drag recolours the list. */
	FLinearColor GetRoleColour(EHFSurfaceRole Role) const;

	FText GetSelectedRoleName() const;
	FText GetLibraryLine() const;
	FSlateColor GetLibraryLineColour() const;

	/** Queues a refresh when the level gains or loses actors. Debounced - a house build spawns 155. */
	void HandleLevelChanged(AActor* Actor);
	EActiveTimerReturnType RefreshTick(double InCurrentTime, float InDeltaTime);
	bool bRefreshQueued = false;

	/** Puts a message under the controls. The panel never opens a dialog for an ordinary action. */
	void Say(const FHFOperationResult& Result);
	FText GetMessage() const;
	EVisibility GetMessageVisibility() const;
	FSlateColor GetMessageColour() const;

	TSharedPtr<SListView<TSharedPtr<FHFSurfaceRoleRow>>> RoleList;
	TArray<TSharedPtr<FHFSurfaceRoleRow>> Rows;

	TSharedPtr<IStructureDetailsView> FinishView;
	TSharedPtr<FStructOnScope> FinishStorage;

	EHFSurfaceRole SelectedRole = EHFSurfaceRole::WallPaint;

	/**
	 * What the library said last time the panel wrote or read it.
	 *
	 * Undo is why this exists. PostUndo fires for every transaction in the editor, not only this
	 * panel's, and pushing all eighteen instances on each one would put a hitch on every unrelated
	 * undo in the level. Comparing against this says which roles a transaction actually moved, so
	 * only those are pushed - and it is what makes Ctrl+Z reach the renderer at all rather than
	 * restoring the asset while the viewport goes on showing the undone finish.
	 */
	TMap<EHFSurfaceRole, FHFSurfaceFinish> LastPushed;

	void RememberLibrary();
	void PushRolesChangedSince();

	FText Message;
	bool bMessageIsFailure = false;
};
