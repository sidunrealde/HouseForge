// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "Claude/HFClaudeCli.h"
#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class SMultiLineEditableTextBox;
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

	/** Kills any generation still running, so closing the panel cannot orphan a CLI process. */
	virtual ~SHFDrawingsPanel();

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

	/**
	 * How many drawing sets there actually are.
	 *
	 * Counted rather than read off SetList->NumSlots(), which is not the same number: when there
	 * are no sets the list still gets a row saying so, so NumSlots() returns one and every "is
	 * there anything to build" test passes on an empty drawings folder.
	 */
	int32 SetCount = 0;

	// -------------------------------------------------------------------------------- generate

	FReply OnGenerateClicked();

	/** Enabled only with a connected Claude AND a set to build from. */
	bool CanGenerate() const;
	FText GenerateTooltip() const;
	FText GenerateLabel() const;

	/** Drains the CLI's pipe and appends to the trace. Returns false to unregister. */
	bool PumpGeneration(float DeltaTime);

	/** Turns one stream-json line into something worth showing a human, or nothing. */
	void AppendTrace(const FString& JsonLine);

	/** The generation in flight, if any. */
	FHFClaudeCli::FRun Run;
	FTSTicker::FDelegateHandle PumpHandle;

	/**
	 * What Claude is doing, as it does it.
	 *
	 * NOT a progress bar. An artist who sees "reading the reflected ceiling plan" understands what
	 * is happening and can tell you what went wrong; a spinner throws that away and makes every
	 * failure look identical.
	 */
	TSharedPtr<SMultiLineEditableTextBox> TraceBox;
	FString Trace;

	/** The set the running generation was started on, for the completion message. */
	FString RunningSet;

	/**
	 * The last thing Claude said, so the result event does not print it a second time.
	 *
	 * On a successful run the CLI's result field is literally the final assistant text block, so
	 * the trace ended with the same paragraph twice.
	 */
	FString LastAssistantText;
};
