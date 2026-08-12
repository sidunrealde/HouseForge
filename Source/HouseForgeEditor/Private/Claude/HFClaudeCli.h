// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * WHERE THE CHAIN BETWEEN AN ARTIST AND A BUILT HOUSE CAN BREAK.
 *
 * Four links, four different fixes. A single "not connected" tells an artist nothing they can act
 * on, and this is the difference between them filing a useful bug and saying "it doesn't work".
 */
enum class EHFClaudeState : uint8
{
	/** Not checked yet. */
	Unknown,

	/** Everything works: CLI present, server reachable, toolset registered, account authenticated. */
	Ready,

	/** claude is not on PATH. They need to install Claude Code. */
	CliNotFound,

	/** The .mcp.json HouseForge writes is missing, or names no server we recognise. */
	ConfigMissing,

	/** The server is configured but nothing is listening. The plugin can fix this itself. */
	ServerNotRunning,

	/** Server up, but HouseForge never registered with the ToolsetRegistry - a plugin fault. */
	ToolsetMissing,

	/** Everything is up but the CLI has no usable Claude account. They log in once, in a terminal. */
	NotAuthenticated,

	/** Something else went wrong; carry the raw text rather than inventing a diagnosis. */
	Failed,
};

/** The outcome of a connection check: a state, and something worth showing a human. */
struct FHFClaudeStatus
{
	EHFClaudeState State = EHFClaudeState::Unknown;

	/** One sentence, in the artist's terms, saying what to do about it. */
	FString Message;

	bool IsReady() const { return State == EHFClaudeState::Ready; }
};

/**
 * DRIVING THE CLAUDE CODE CLI FROM THE EDITOR.
 *
 * HouseForge does not talk to the Anthropic API. It drives the artist's own Claude Code, which
 * authenticates with their Claude account and already carries the whole agentic loop - read the
 * drawings, write a spec, validate, correct, capture, compare. Re-implementing that loop against
 * the raw API would be a great deal of code to arrive back where the CLI already is.
 *
 *
 * WHY THE CONNECTION CHECK IS A CASCADE AND NOT ONE PROBE
 * -------------------------------------------------------
 * The obvious design - ask the model something and see whether it can reach HouseForge - was
 * measured and rejected. It costs about eight cents a go, and it does not detect the failure it
 * was meant to: with the MCP server dead, `claude -p` still exits 0 with an empty stderr and
 * answers from its own knowledge. A missing MCP server is SILENT to the model.
 *
 * `claude mcp list` health-checks every configured server, costs nothing, calls no model, and
 * names the failure outright. So the cascade runs the free checks first and only reaches the paid
 * one for the single question the free checks cannot answer - whether there is a usable account.
 */
class FHFClaudeCli
{
public:
	/** The server name HouseForge writes into .mcp.json. */
	static const TCHAR* ServerName() { return TEXT("unreal-mcp"); }

	/**
	 * Absolute path to the claude executable, or empty if it is not on PATH.
	 *
	 * Looked up rather than assumed: Claude Code installs per-user on Windows, so there is no
	 * fixed location worth hardcoding.
	 */
	static FString FindExecutable();

	/**
	 * What `claude mcp list` says about one server.
	 *
	 * Pure, so the states it distinguishes can be pinned by a test against real captured output
	 * rather than against a developer's memory of the format. The real thing looks like:
	 *
	 *   unreal-mcp: http://127.0.0.1:8000/mcp (HTTP) - OK Connected
	 *   unreal-mcp: http://127.0.0.1:8000/mcp (HTTP) - X Failed to connect - ConnectionRefused: ...
	 *
	 * with a leading glyph this deliberately does not match on: the glyphs are decorative, they
	 * vary with the terminal's encoding, and a check that turns on them breaks the first time the
	 * CLI is run somewhere that renders them differently.
	 */
	static EHFClaudeState ParseMcpList(const FString& Output, const FString& InServerName);

	/**
	 * Runs a command to completion and returns its exit code, with stdout and stderr separately.
	 *
	 * SEPARATE PIPES ON PURPOSE. The agent's output arrives on stdout as JSON; a CLI that could
	 * not start at all complains on stderr. Merged, "not logged in" is indistinguishable from
	 * something the model said, and the panel cannot tell an artist which of the two happened.
	 *
	 * Blocking, with a timeout. Callers on the game thread run it from a ticker.
	 */
	static int32 RunToCompletion(
		const FString& Executable,
		const FString& Arguments,
		const FString& WorkingDirectory,
		double TimeoutSeconds,
		FString& OutStdOut,
		FString& OutStdErr);

	/**
	 * A neutral directory to run the CLI from.
	 *
	 * NOT the project folder, and the difference is measured: Claude Code auto-discovers a
	 * CLAUDE.md from its working directory, and this project's is large enough to have cost 12,686
	 * cache-creation tokens - about eight cents - on every probe. Running from a scratch directory
	 * with an absolute --mcp-config halves the cost of a check that reads nothing from the cwd.
	 */
	static FString NeutralWorkingDirectory();
};
