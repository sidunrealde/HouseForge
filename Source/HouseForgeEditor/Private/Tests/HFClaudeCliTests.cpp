// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Claude/HFClaudeCli.h"
#include "Misc/AutomationTest.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * READING `claude mcp list`, AGAINST OUTPUT ACTUALLY CAPTURED FROM THE CLI.
 *
 * The fixtures below are real lines from Claude Code 2.1.222 on this machine, not a remembered
 * format - including the neighbouring servers, because the parse has to pick ours out of a list
 * that contains whatever else the artist has configured.
 *
 * This is the check the connection cascade rests on, and it is free: no model call, no tokens. The
 * alternative that was measured and rejected - asking the model whether it can reach HouseForge -
 * costs about eight cents AND cannot see the failure, because with the server dead `claude -p`
 * still exits 0 and answers from its own knowledge.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFClaudeMcpListTest,
	"HouseForge.Claude.ReadingTheServerHealthCheck", HF_TEST_FLAGS)

bool FHFClaudeMcpListTest::RunTest(const FString& Parameters)
{
	const FString ServerName = FHFClaudeCli::ServerName();

	// Captured verbatim while the editor was closed - the state an artist is in before they press
	// anything, and the one the panel most needs to name correctly.
	const FString ServerDown = TEXT(
		"Checking MCP server health\n"
		"\n"
		"claude.ai Google Drive: https://drivemcp.googleapis.com/mcp/v1 - Connected\n"
		"rider: http://127.0.0.1:64482/stream (HTTP) - Failed to connect - ConnectionRefused: Unable to connect.\n"
		"unreal-mcp: http://127.0.0.1:8000/mcp (HTTP) - Failed to connect - ConnectionRefused: Unable to connect.\n");

	TestEqual(TEXT("A refused connection is the server not running, not a missing config"),
		FHFClaudeCli::ParseMcpList(ServerDown, ServerName), EHFClaudeState::ServerNotRunning);

	const FString ServerUp = TEXT(
		"Checking MCP server health\n"
		"\n"
		"claude.ai Google Drive: https://drivemcp.googleapis.com/mcp/v1 - Connected\n"
		"unreal-mcp: http://127.0.0.1:8000/mcp (HTTP) - Connected\n");

	TestEqual(TEXT("A connected server is ready"),
		FHFClaudeCli::ParseMcpList(ServerUp, ServerName), EHFClaudeState::Ready);

	// Absent entirely: .mcp.json was never written, or does not carry our server. A different
	// fix from "the server is down" - nothing to start, something to generate.
	const FString NotListed = TEXT(
		"Checking MCP server health\n"
		"\n"
		"claude.ai Google Drive: https://drivemcp.googleapis.com/mcp/v1 - Connected\n");

	TestEqual(TEXT("A server that is not listed at all is a missing config"),
		FHFClaudeCli::ParseMcpList(NotListed, ServerName), EHFClaudeState::ConfigMissing);

	TestEqual(TEXT("Empty output is a missing config rather than a crash"),
		FHFClaudeCli::ParseMcpList(FString(), ServerName), EHFClaudeState::ConfigMissing);

	// The authentication wording, captured from a real server in that state on this machine.
	const FString NeedsAuth = TEXT("unreal-mcp: http://127.0.0.1:8000/mcp (HTTP) - Needs authentication\n");

	TestEqual(TEXT("A server needing authentication is not reported as merely down"),
		FHFClaudeCli::ParseMcpList(NeedsAuth, ServerName), EHFClaudeState::NotAuthenticated);

	// A RUNNING SERVER THAT STILL WILL NOT CONNECT, captured verbatim from a real editor session
	// with the MCP server confirmed listening on 127.0.0.1:8000.
	//
	// This state cost a manual test to find. Claude Code treats a project-scoped .mcp.json as
	// untrusted input - it arrives with a repository and can point anywhere - so it asks before
	// connecting. Every other signal says healthy: the port is open, the config is right, the
	// listener logged itself. Folded into ServerNotRunning, as it was, the panel told the user to
	// start a server that was already started and offered a button that could never help.
	const FString Pending = TEXT(
		"unreal-mcp: http://127.0.0.1:8000/mcp (HTTP) - Pending approval (run `claude` to approve)\n");

	TestEqual(TEXT("An unapproved server is waiting for the user, not down"),
		FHFClaudeCli::ParseMcpList(Pending, ServerName), EHFClaudeState::PendingApproval);

	// THE FAILURE STRING THAT CONTAINS THE SUCCESS WORD.
	//
	// "Connected - tools fetch failed" means the transport opened and tools/list did not, so the
	// run would have NO HouseForge tools. Checked after the plain Connected test, this went green
	// and enabled Generate into a session where every HouseForge call fails - and a missing
	// toolset is silent to the model, which answers from its own knowledge instead of erroring.
	// A false green is the worst answer a connection check can give.
	const FString ToolsFailed = TEXT(
		"unreal-mcp: http://127.0.0.1:8000/mcp (HTTP) - Connected - tools fetch failed");

	TestEqual(TEXT("A server whose tools failed to load is not reported as ready"),
		FHFClaudeCli::ParseMcpList(ToolsFailed, ServerName), EHFClaudeState::ToolsetMissing);

	// ------------------------------------------------------------------ the two ways to mis-parse

	// A DIFFERENT server whose URL happens to contain ours. Matching anywhere in the line rather
	// than on the name prefix would report this as HouseForge being healthy while HouseForge is
	// absent - a false green, which is the worst answer a connection check can give.
	const FString Impostor = TEXT("other-tool: http://127.0.0.1:9000/proxy/unreal-mcp (HTTP) - Connected\n");

	TestEqual(TEXT("Another server merely mentioning ours in its URL is not ours"),
		FHFClaudeCli::ParseMcpList(Impostor, ServerName), EHFClaudeState::ConfigMissing);

	// The decorative glyphs vary with the console encoding, so the parse keys on words. Feeding it
	// a line stripped of them must still read as connected - if this fails, the check would call a
	// healthy server broken the first time it ran somewhere the marks did not survive.
	TestEqual(TEXT("A line whose status glyph did not survive the pipe still reads as connected"),
		FHFClaudeCli::ParseMcpList(
			TEXT("unreal-mcp: http://127.0.0.1:8000/mcp (HTTP) -  Connected\n"), ServerName),
		EHFClaudeState::Ready);

	return true;
}

/**
 * THE TWO FLAGS THAT BOUND WHAT A GENERATION MAY DO.
 *
 * Not a formatting test. Generate spawns Claude Code on the artist's own machine, and exactly two
 * flags decide what that process can touch. Either one silently dropped widens the blast radius
 * with no visible symptom - the generation still works, so nothing looks wrong, and the next
 * person to read the code has no way to tell the omission from a decision.
 *
 *   --strict-mcp-config      keeps the run to HouseForge's server instead of also loading whatever
 *                            MCP servers the artist happens to have configured
 *   --allowedTools           names what may run without being asked about
 *   --permission-mode dontAsk  denies everything that is not on that list - INCLUDING Claude
 *                            Code's own Bash, Edit and Write, which an allow-list alone does not
 *                            remove from the model's tool set
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFClaudeGenerateArgumentsTest,
	"HouseForge.Claude.AGenerationIsConfinedToHouseForge", HF_TEST_FLAGS)

bool FHFClaudeGenerateArgumentsTest::RunTest(const FString& Parameters)
{
	const FString Arguments = FHFClaudeCli::BuildGenerateArguments(
		TEXT("Sample2BHK"), TEXT("D:/Project/.mcp.json"));

	TestTrue(TEXT("The artist's own MCP servers are excluded from the run"),
		Arguments.Contains(TEXT("--strict-mcp-config")));

	TestTrue(TEXT("HouseForge's own tools are the ones allowed without asking"),
		Arguments.Contains(FString::Printf(TEXT("--allowedTools \"mcp__%s__*\""), FHFClaudeCli::ServerName())));

	// THE FLAG THAT ACTUALLY DENIES, and the reason this assertion is written this way.
	//
	// It used to read "Only HouseForge's own tools are allowed - not Bash, Edit or Write" against
	// --allowedTools, which is wrong about the mechanism: an allow-list names what runs WITHOUT
	// being asked, and the built-in tools stay in the model's tool set either way. What denies
	// them is the mode. So the old assertion would have passed with the real guard deleted - a
	// test that could not fail, dressed as a security check.
	//
	// Asserted on the VALUE, not just the flag's presence: --permission-mode acceptEdits would
	// satisfy a Contains("--permission-mode") while permitting exactly what this is meant to stop.
	TestTrue(TEXT("Anything not on the allow-list is denied rather than asked about"),
		Arguments.Contains(TEXT("--permission-mode dontAsk")));

	// Streaming is what makes the panel show the trace as it happens rather than a spinner.
	TestTrue(TEXT("Output streams as it happens"),
		Arguments.Contains(TEXT("--output-format stream-json")));

	TestTrue(TEXT("It runs non-interactively"), Arguments.Contains(TEXT("-p ")));

	TestTrue(TEXT("The config it is pointed at is the one passed in"),
		Arguments.Contains(TEXT("D:/Project/.mcp.json")));

	// The set has to reach the prompt, or every generation would build whatever Claude found first.
	TestTrue(TEXT("The drawing set to build from is named in the prompt"),
		Arguments.Contains(TEXT("Sample2BHK")));

	return true;
}

/**
 * WHAT THE TRACE MUST NEVER DROP AGAIN.
 *
 * The trace is the ONLY window an artist has into a generation, and an audit found it reading
 * three event shapes out of a dozen. Everything that says a run is in TROUBLE fell off the end of
 * the chain: the failed tool_result carrying an MCP error, the api_retry that explains two minutes
 * of silence, the refusal that ends a run without a word, and the init event that reports whether
 * the server connected at all.
 *
 * The consequence was not a missing detail. A generation failing every single MCP call produced
 * exactly the same picture as one succeeding - a list of tool names - so the panel could not tell
 * an artist the one thing they needed to know, and looked busy while doing it.
 *
 * Fixtures are the shapes claude 2.1.x actually emits in --output-format stream-json.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFClaudeTraceTest,
	"HouseForge.Claude.TheTraceShowsWhenARunIsInTrouble", HF_TEST_FLAGS)

bool FHFClaudeTraceTest::RunTest(const FString& Parameters)
{
	FString Last;

	// ------------------------------------------------------------------- a failing MCP call
	//
	// THE ONE THAT MATTERS MOST. Every "user" event in an agentic run is tool_result blocks, and a
	// failed call puts its reason right there. Dropped, the artist sees the tool name that was
	// called and nothing about it having failed.
	const FString Failure = FHFClaudeCli::SummariseTraceLine(
		TEXT(R"({"type":"user","message":{"content":[{"type":"tool_result","is_error":true,)")
		TEXT(R"("content":"ValidationFailed: W_North overlaps W_East"}]}})"), Last);

	TestTrue(TEXT("A failed tool call says so"), Failure.Contains(TEXT("FAILED")));
	TestTrue(TEXT("...and carries the reason the artist needs"),
		Failure.Contains(TEXT("W_North overlaps W_East")));

	// A SUCCESSFUL result is deliberately NOT shown: it is usually a wall of spec JSON that would
	// bury the narration it sits between.
	const FString Success = FHFClaudeCli::SummariseTraceLine(
		TEXT(R"({"type":"user","message":{"content":[{"type":"tool_result","content":"{...spec...}"}]}})"), Last);
	TestTrue(TEXT("A successful tool result does not bury the trace"), Success.IsEmpty());

	// --------------------------------------------------------------- silence that has a reason
	const FString Retry = FHFClaudeCli::SummariseTraceLine(
		TEXT(R"({"type":"system","subtype":"api_retry","attempt":2,"max_retries":5})"), Last);
	TestTrue(TEXT("A retry explains the pause instead of leaving it silent"),
		Retry.Contains(TEXT("2")) && Retry.Contains(TEXT("5")));

	// A refusal ENDS the run. Dropped, the trace simply stops with no reason on screen.
	const FString Refusal = FHFClaudeCli::SummariseTraceLine(
		TEXT(R"({"type":"system","subtype":"model_refusal_no_fallback","content":"Declined the request."})"), Last);
	TestTrue(TEXT("A refusal that ends the run is shown"), Refusal.Contains(TEXT("Declined")));

	// ------------------------------------------------- whether this run could ever have worked
	//
	// The init event is the only report of GROUND TRUTH: what the connection check guesses at
	// beforehand, this states at generation time.
	const FString Init = FHFClaudeCli::SummariseTraceLine(
		TEXT(R"({"type":"system","subtype":"init","mcp_servers":[{"name":"unreal-mcp",)")
		TEXT(R"("status":"failed"}],"tools":["a","b"]})"), Last);

	TestTrue(TEXT("The init event names the server"), Init.Contains(TEXT("unreal-mcp")));
	TestTrue(TEXT("...and its real status, even when that status is failure"),
		Init.Contains(TEXT("failed")));

	// ------------------------------------------------------------------ narration, and no echo
	const FString Said = FHFClaudeCli::SummariseTraceLine(
		TEXT(R"({"type":"assistant","message":{"content":[{"type":"text","text":"Reading the ceiling plan."}]}})"), Last);
	TestTrue(TEXT("Claude's narration reaches the artist"), Said.Contains(TEXT("Reading the ceiling plan")));

	// The result event repeats the final assistant block verbatim, so printing it unconditionally
	// showed the closing summary twice at the bottom of every trace.
	const FString Echo = FHFClaudeCli::SummariseTraceLine(
		TEXT(R"({"type":"result","result":"Reading the ceiling plan."})"), Last);
	TestFalse(TEXT("The result event does not repeat what was just said"),
		Echo.Contains(TEXT("Reading the ceiling plan")));

	// ----------------------------------------------------------------- and never silent on junk
	//
	// A line that is not JSON is the CLI failing outside its own protocol, which is exactly when
	// the panel must not go quiet.
	TestTrue(TEXT("A non-JSON line is shown rather than swallowed"),
		FHFClaudeCli::SummariseTraceLine(TEXT("bash: claude: command not found"), Last)
			.Contains(TEXT("command not found")));

	return true;
}

/**
 * FINDING THE CLI AT ALL.
 *
 * Asserted as a property rather than as a path: whether Claude Code is installed on the machine
 * running the suite is not something the plugin gets to require. What IS required is that the
 * answer is either empty or a file that exists - never a plausible-looking path that does not.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFClaudeFindExecutableTest,
	"HouseForge.Claude.FindingTheExecutable", HF_TEST_FLAGS)

bool FHFClaudeFindExecutableTest::RunTest(const FString& Parameters)
{
	const FString Found = FHFClaudeCli::FindExecutable();

	if (Found.IsEmpty())
	{
		AddInfo(TEXT("Claude Code is not on PATH on this machine; the panel will say so."));
		return true;
	}

	TestTrue(FString::Printf(TEXT("The path it returned exists: '%s'"), *Found),
		FPlatformFileManager::Get().GetPlatformFile().FileExists(*Found));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
