// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class STextBlock;
class SVerticalBox;

/**
 * THE DRAWINGS SECTION: the drawings this house will be read from, and how they get in.
 *
 * The plugin already imported drawings through a file dialog and through MCP. This is the third
 * route and the one an artist actually reaches for: drop the sheets onto the panel.
 *
 *
 * WHY THIS IS NOT GATED ON CLAUDE
 * -------------------------------
 * Importing a drawing is a file copy and, for PDF, a rasterise. No model is involved. Neither is
 * one involved in building a spec that already exists. So this section works whether or not Claude
 * Code is installed, logged in, or reachable - only GENERATE needs any of that, and only GENERATE
 * is disabled without it.
 *
 * The alternative - one gate in front of the whole panel - is tempting because it gives a single
 * obvious path. It also means an artist cannot re-import yesterday's sheets or rebuild yesterday's
 * spec while Claude is down, which is a worse failure than a slightly wider UI.
 *
 *
 * WHY SDropTarget RATHER THAN OnDragOver/OnDrop ON A BORDER
 * ---------------------------------------------------------
 * SDropTarget already draws the valid/invalid hover state, and its OnAllowDrop runs BEFORE the
 * drop - so a folder of .dwg files gets a "no" cursor rather than a dialog listing what was
 * skipped. Refusing early is the difference between "this doesn't take DWG" and "this is broken".
 */
class SHFDrawingsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHFDrawingsPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Whether this drag carries at least one file HouseForge can actually read.
	 *
	 * ANY readable file, not EVERY one - see the note on the definition. Defers the per-file
	 * question to UHFEditorSubsystem::IsReadableDrawing so the drop and the import cannot disagree.
	 *
	 * Public because it is the whole decision this widget makes before a drop, and it is worth a
	 * test of its own - the any-versus-every rule is exactly the kind of thing a later reader
	 * "corrects" into refusing a real sheet folder.
	 */
	static bool CanAcceptDrag(TSharedPtr<FDragDropOperation> Operation);

private:
	FReply OnDropped(const FGeometry& Geometry, const FDragDropEvent& Event);
	FReply OnBrowseClicked();

	/** Copies the given files in, then refreshes the set list and the status line. */
	void Import(const TArray<FString>& Paths);

	/** Rebuilds the imported-sets list from what is actually on disk. */
	void RefreshSets();

	/** The set name to import into. Blank means "name it after the first file". */
	TSharedPtr<SEditableTextBox> SetNameBox;

	/** What the last import did, in the subsystem's own words. */
	TSharedPtr<STextBlock> StatusText;

	/** One row per set found under Reference/Drawings. */
	TSharedPtr<SVerticalBox> SetList;
};
