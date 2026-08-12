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

	/**
	 * Configured, running, reachable - and Claude Code will not connect until the artist approves
	 * it once.
	 *
	 * A project-scoped .mcp.json is untrusted input: it arrives with a repository and can point
	 * anywhere, so Claude Code asks before connecting. Entirely reasonable, and invisible until
	 * you hit it - the server is up, the config is right, and nothing works.
	 *
	 * Modelled separately because it is the one failure a running server produces. Folded into
	 * ServerNotRunning, the panel would tell an artist to start something already started and
	 * offer a button that could never help.
	 */
	PendingApproval,

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
	 * The command line that builds a house from a drawing set.
	 *
	 * PURE, AND TESTED, because two of these flags are the only thing confining what Claude may do
	 * inside the artist's editor and machine, and a flag that quietly stops being passed would
	 * widen that with no visible symptom:
	 *
	 *   --strict-mcp-config   Without it, Claude Code ALSO loads whatever MCP servers the artist
	 *                         has configured personally. HouseForge would be one tool among an
	 *                         unknown set, on someone else's machine, reaching things this plugin
	 *                         never sanctioned.
	 *
	 *   --allowedTools        Without it, the run carries Claude Code's own Bash, Edit and Write
	 *                         alongside ours - a generation that can edit files and run commands.
	 *                         With it, the process can build houses and nothing else.
	 *
	 * --permission-mode is set too, because a headless run that stops to ask permission nobody can
	 * answer does not fail: it hangs, which reads as a crash. The allowlist above is what actually
	 * bounds the run; this only stops it blocking.
	 */
	static FString BuildGenerateArguments(const FString& DrawingSet, const FString& ConfigPath);

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
	 * A generation in flight.
	 *
	 * Long-running - minutes, not seconds - so it cannot be a blocking call on the game thread and
	 * it cannot be pumped from SWidget::Tick either: Slate stops ticking a widget whose tab is
	 * hidden, and an artist who docks the panel behind another tab would strand the process with
	 * its pipe unread until it filled and the child blocked. Pumped from a core ticker instead,
	 * which runs whatever the UI is doing.
	 */
	struct FRun
	{
		FProcHandle Process;
		void* OutRead = nullptr;
		void* OutWrite = nullptr;
		void* ErrRead = nullptr;
		void* ErrWrite = nullptr;

		/** Everything read so far that has not yet been split into whole lines. */
		FString PendingOut;

		FString StdErr;
		bool bFinished = false;
		int32 ReturnCode = -1;

		bool IsValid() const { return Process.IsValid(); }
	};

	/** Starts a run. Returns false and fills OutError if the process could not be launched. */
	static bool Start(
		const FString& Executable,
		const FString& Arguments,
		const FString& WorkingDirectory,
		FRun& OutRun,
		FString& OutError);

	/**
	 * Reads whatever is available, hands back any WHOLE lines, and notes whether it has exited.
	 *
	 * Whole lines only, because each line of --output-format stream-json is one JSON object and
	 * half of one parses as nothing. The remainder is held until its newline arrives.
	 */
	static void Pump(FRun& Run, TArray<FString>& OutCompleteLines);

	/** Ends a run, killing the process if it is still going. Safe to call twice. */
	static void Finish(FRun& Run);

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
