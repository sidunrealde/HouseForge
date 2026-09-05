// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "Claude/HFClaudeCli.h"
#include "Containers/Ticker.h"
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
 * The cascade is free the whole way down: find the executable, write the config, read the health
 * check, and ask ourselves whether the toolset registered.
 *
 *
 * WHY THE CHECK CANNOT BLOCK, AND WHY THAT IS NOT A PERFORMANCE POINT
 * -------------------------------------------------------------------
 * `claude mcp list` health-checks the MCP server by CONNECTING TO IT - and that server is this
 * editor. It is served by the engine's HTTP listener, which only answers while the game thread
 * ticks.
 *
 * So a check that waits for the child process on the game thread deadlocks against itself: the
 * editor stops ticking, the listener cannot reply, and `claude` waits its full 30-second timeout
 * before reporting the server unreachable. It was, for the duration of the check, and only
 * because the check was running. Measured, from the panel:
 *
 *   unreal-mcp: ... - Failed to connect - MCP server "unreal-mcp" connection timed out after 30000ms
 *
 * while the identical command by hand, with the editor free to answer, returned Connected in two
 * seconds. The parse was right and the diagnosis was right; the question had been made unanswerable
 * by the act of asking it.
 *
 * Hence a ticker, exactly as a generation uses - the editor keeps ticking, the listener keeps
 * answering, and the check gets a truthful answer.
 */
class SHFClaudePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHFClaudePanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Stops a check still in flight, so closing the panel cannot orphan a CLI process. */
	virtual ~SHFClaudePanel();

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

	/** Spawns `claude mcp list`. The answer arrives later, in PumpCheck. */
	void BeginCheck();

	/** Drains the check process and, once it exits, turns its output into a status. */
	bool PumpCheck(float DeltaTime);

	/** Everything after the output is in hand: parse, toolset, message. */
	void ConcludeCheck(const FString& StdOut, const FString& StdErr, int32 ReturnCode);

	/** The check in flight, if any. */
	FHFClaudeCli::FRun CheckRun;
	FTSTicker::FDelegateHandle CheckHandle;

	/** Where that check is running from, kept for the log line and the messages. */
	FString CheckDirectory;

	/** Whole lines are irrelevant here - the parse wants the lot - so they are just accumulated. */
	FString CheckOutput;

	/**
	 * Whether a check could be started right now.
	 *
	 * Drives the button's enabled state as well as guarding BeginCheck. A check takes seconds, and
	 * the one thing it is waiting on is this editor being free to answer - so a second click during
	 * the first is not merely redundant, it is competing with it.
	 */
	bool IsCheckIdle() const;

	FReply OnStartServerClicked();

	/** Whether the "Start the server" button should be offered at all. */
	EVisibility StartServerVisibility() const;

	FText StatusLine() const;
	FSlateColor StatusColour() const;

	TSharedPtr<SButton> CheckButton;
	TSharedPtr<STextBlock> DetailText;
};
