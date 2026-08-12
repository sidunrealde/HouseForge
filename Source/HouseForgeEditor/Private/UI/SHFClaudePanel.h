// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "Claude/HFClaudeCli.h"
#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SButton;
class STextBlock;

/**
 * THE CLAUDE SECTION: whether an artist can actually build a house right now, and what to do if not.
 *
 * HouseForge does not read drawings - Claude does, through the artist's own Claude Code and their
 * own Claude account. This section is where that dependency is made visible, checked, and named
 * when it is missing.
 *
 *
 * WHY A CHECK BUTTON RATHER THAN A LIVE CONNECTION
 * ------------------------------------------------
 * There is no connection to hold. The CLI is a process spawned per request, not a service. What
 * this proves instead is that the chain WOULD work - CLI installed, config written, server up,
 * account usable - so an artist finds out before they have imported a set and pressed Generate,
 * rather than after.
 *
 * The cascade is ordered by cost. Finding the executable and running `claude mcp list` are free
 * and answer three of the four questions; only the fourth, whether there is a usable account,
 * needs a model call, and it is the last thing tried.
 */
class SHFClaudePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHFClaudePanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * What the last check concluded, for anything that needs to gate on it.
	 *
	 * Static because GENERATE lives in a different section and there is exactly one Claude to be
	 * connected to. Drawings and surfaces deliberately do NOT consult this - importing a sheet and
	 * retexturing a wall need no model, and disabling them when Claude is down would strand an
	 * artist who only wanted to rebuild yesterday's spec.
	 */
	static const FHFClaudeStatus& LastStatus();

private:
	FReply OnCheckClicked();
	FReply OnStartServerClicked();

	/** Runs the cascade and updates the panel. Blocking; the free steps are fast. */
	void RunCheck();

	/** Whether the "Start the server" button should be offered at all. */
	EVisibility StartServerVisibility() const;

	FText StatusLine() const;
	FSlateColor StatusColour() const;

	TSharedPtr<SButton> CheckButton;
	TSharedPtr<STextBlock> DetailText;
};
