// Copyright Siddartha G. All Rights Reserved.

#include "Claude/HFClaudeCli.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Dom/JsonObject.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FString FHFClaudeCli::FindExecutable()
{
	IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();

#if PLATFORM_WINDOWS
	const TCHAR* const Names[] = { TEXT("claude.exe"), TEXT("claude.cmd"), TEXT("claude.bat") };
#else
	const TCHAR* const Names[] = { TEXT("claude") };
#endif

	// PATH first, because that is where an install puts it and where the artist's own terminal
	// finds it. Matching what their terminal does matters: "it works when I type claude" is the
	// first thing they will say if this disagrees.
	const FString PathVariable = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));

	TArray<FString> Directories;
#if PLATFORM_WINDOWS
	PathVariable.ParseIntoArray(Directories, TEXT(";"), /*bCullEmpty*/ true);
#else
	PathVariable.ParseIntoArray(Directories, TEXT(":"), /*bCullEmpty*/ true);
#endif

	// The per-user install location, appended rather than assumed. Claude Code installs here on
	// Windows and the entry is normally on PATH already - but a shell that has not been restarted
	// since the install has a stale PATH, and failing then would be a confusing first experience.
	const FString HomeDirectory = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
	if (!HomeDirectory.IsEmpty())
	{
		Directories.Add(FPaths::Combine(HomeDirectory, TEXT(".local"), TEXT("bin")));
	}

	for (const FString& Directory : Directories)
	{
		for (const TCHAR* Name : Names)
		{
			const FString Candidate = FPaths::Combine(Directory.TrimQuotes(), Name);
			if (Platform.FileExists(*Candidate))
			{
				return Candidate;
			}
		}
	}

	return FString();
}

EHFClaudeState FHFClaudeCli::ParseMcpList(const FString& Output, const FString& InServerName)
{
	if (InServerName.IsEmpty())
	{
		return EHFClaudeState::ConfigMissing;
	}

	TArray<FString> Lines;
	Output.ParseIntoArrayLines(Lines, /*bCullEmpty*/ true);

	for (const FString& Line : Lines)
	{
		// "<name>: <url> (HTTP) - <state>". Anchored on "<name>:" at the start of the trimmed line
		// so a server whose URL merely mentions ours is not mistaken for it.
		const FString Trimmed = Line.TrimStartAndEnd();
		const FString Prefix = InServerName + TEXT(":");

		if (!Trimmed.StartsWith(Prefix, ESearchCase::CaseSensitive))
		{
			continue;
		}

		// On the WORDS, never the glyphs. The check marks and crosses are decorative and depend on
		// the console's encoding; keying on them would break the first time this ran through a
		// pipe that mangled them - and it would break by reporting a healthy server as broken.
		// BEFORE the Connected test, because the failure string CONTAINS the success word:
		// "! Connected - tools fetch failed" means the transport opened and tools/list did not,
		// so the run would have no HouseForge tools at all. Tested first, this went green and
		// enabled Generate into a session where every HouseForge call would fail - and a missing
		// toolset is silent to the model, which answers from its own knowledge instead.
		if (Trimmed.Contains(TEXT("tools fetch failed"), ESearchCase::IgnoreCase))
		{
			return EHFClaudeState::ToolsetMissing;
		}

		if (Trimmed.Contains(TEXT("Connected"), ESearchCase::IgnoreCase))
		{
			return EHFClaudeState::Ready;
		}

		if (Trimmed.Contains(TEXT("Needs authentication"), ESearchCase::IgnoreCase))
		{
			return EHFClaudeState::NotAuthenticated;
		}

		// Checked BEFORE the fall-through, because a pending server is running and reachable - it
		// is only unapproved. Reading it as "not running" sends an artist to start something that
		// is already started, which is the exact wrong instruction.
		if (Trimmed.Contains(TEXT("Pending approval"), ESearchCase::IgnoreCase))
		{
			return EHFClaudeState::PendingApproval;
		}

		// ONLY ON POSITIVE EVIDENCE OF FAILURE.
		//
		// This used to be a fall-through: anything not recognised became "the server is not
		// running". That is a confident wrong answer, and it is the same shape of mistake as the
		// Pending-approval state it already cost us - a status this parse has not been taught
		// becomes an instruction to go and start something that may well be running.
		//
		// A line whose status has not resolved yet, or a wording added by a later CLI, now says so
		// instead of guessing.
		if (Trimmed.Contains(TEXT("Failed to connect"), ESearchCase::IgnoreCase)
			|| Trimmed.Contains(TEXT("ConnectionRefused"), ESearchCase::IgnoreCase)
			|| Trimmed.Contains(TEXT("ECONNREFUSED"), ESearchCase::IgnoreCase))
		{
			return EHFClaudeState::ServerNotRunning;
		}

		return EHFClaudeState::Failed;
	}

	// Named nowhere in the list. Either .mcp.json was never written or it does not carry our
	// server - both are "the config is not there", not "the server is down".
	return EHFClaudeState::ConfigMissing;
}

FString FHFClaudeCli::BuildGenerateArguments(const FString& DrawingSet, const FString& ConfigPath)
{
	// The task, in the terms the plugin's own workflow doc uses. Deliberately says what to do and
	// not how - the CLI already carries the read/validate/correct/capture loop, and prescribing
	// the steps here would fight it rather than help.
	const FString Prompt = FString::Printf(
		TEXT("Read the interior drawings in the drawing set named %s and build the flat in ")
		TEXT("Unreal. ")
		TEXT("Use the HouseForge tools: list the drawings, read them, write a House Spec, ")
		TEXT("validate it, and apply it. If validation reports problems, correct the spec and ")
		TEXT("validate again rather than building a spec with errors. When the level is built, ")
		TEXT("capture a plan and compare it against the source drawing. ")
		// SAID OUT LOUD, because the first real generation did not think to. A sheet is downscaled
		// to be read, which costs about a fifth of every dimension string on it; the run said "let
		// me crop the plan sheets so I can read the detail properly", found no way to, and fell
		// back to inferring the furniture layout from the elevations. Every misplaced piece in that
		// flat came from those two lines, and the tool that fixes it is no use unread.
		TEXT("The sheets are large and are downscaled when you read them, so fine dimensions and ")
		TEXT("labels can be unreadable at full-sheet size. Use CropDrawing to read any region at ")
		TEXT("full resolution rather than guessing at it or working around it from another sheet."),
		*DrawingSet);

	FString Arguments;

	// -p, so it runs and exits rather than waiting for a terminal nobody is watching.
	Arguments += TEXT("-p ");
	Arguments += FString::Printf(TEXT("\"%s\" "), *Prompt.ReplaceCharWithEscapedChar());

	// One JSON object per line as it happens, which is what lets the panel show the trace live
	// rather than a spinner and then a wall of text.
	//
	// WITHOUT --include-partial-messages, which was here and did nothing. It adds one shape,
	// {type:"stream_event", event:<raw SSE>}, which the trace does not read - so it bought no
	// extra granularity and cost roughly ten times the byte volume on a pipe drained every 100 ms,
	// multiplying the mid-character read boundaries the line splitter has to survive.
	Arguments += TEXT("--output-format stream-json --verbose ");

	Arguments += FString::Printf(TEXT("--mcp-config \"%s\" "), *ConfigPath);
	Arguments += TEXT("--strict-mcp-config ");
	// READ AND GLOB ARE PART OF THE JOB, not a relaxation of the confinement.
	//
	// The MCP tools hand back PATHS, not content: ListDrawings returns the names of the sheets and
	// CaptureTopDown returns where it wrote the plan PNG. So "read the drawings" and "compare the
	// capture against the source" are both, mechanically, reading a file off disk. Without Read in
	// the allow-list a generation cannot look at a single drawing - it is not confined, it is
	// unable, and it went looking for a shell to get around it:
	//
	//   [PowerShell] FAILED: Permission to use PowerShell has been denied ... don't ask mode
	//   [Bash]       FAILED: Permission to use Bash has been denied ... don't ask mode
	//
	// which is what a well-behaved agent does when the sanctioned route is missing, and exactly
	// what --permission-mode is there to stop. The denials were right; the allow-list was wrong.
	//
	// Read is already confined to the run's working directory - Claude Code will not read outside
	// its cwd tree - and that is the project folder, so this grants the drawings, the captures and
	// the project, not the machine.
	//
	// WRITE IS STILL NOT HERE, and does not need to be: ValidateSpec and ApplySpec take the spec
	// JSON inline over MCP, and SaveSpec writes it from inside the editor. The spec never touches
	// the filesystem through the model.
	Arguments += FString::Printf(TEXT("--allowedTools \"mcp__%s__*,Read,Glob\" "), ServerName());
	Arguments += TEXT("--permission-mode dontAsk ");
	Arguments += TEXT("--model opus");

	return Arguments;
}

FString FHFClaudeCli::NeutralWorkingDirectory()
{
	const FString Directory = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("HouseForge"), TEXT("Cli"));
	IFileManager::Get().MakeDirectory(*Directory, /*Tree*/ true);
	return Directory;
}

FString FHFClaudeCli::SummariseTraceLine(const FString& JsonLine, FString& InOutLastAssistant)
{
	FString Out;
	TSharedPtr<FJsonObject> Object;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonLine);

	if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
	{
		// Not JSON. Shown rather than swallowed - if the CLI printed a plain-text error, that line
		// is the most useful thing on screen, and dropping it would leave the panel silent about
		// the one thing that went wrong.
		Out += JsonLine + TEXT("\n");
	}
	else
	{
		const FString Type = Object->GetStringField(TEXT("type"));

		// ------------------------------------------------------------------- what went wrong
		//
		// FIRST, because these are the only events that explain a run in trouble, and the panel
		// used to drop every one of them. An artist watching a generation that was failing every
		// single MCP call saw the same picture as one that was succeeding: a list of tool names.
		if (Type == TEXT("system"))
		{
			const FString Subtype = Object->GetStringField(TEXT("subtype"));

			if (Subtype == TEXT("init"))
			{
				// The one event that says whether this run can work AT ALL: which MCP servers
				// connected, and which tools survived --allowedTools. Everything the connection
				// check guesses at beforehand, this reports as fact at generation time.
				const TArray<TSharedPtr<FJsonValue>>* Servers = nullptr;
				if (Object->TryGetArrayField(TEXT("mcp_servers"), Servers) && Servers != nullptr)
				{
					for (const TSharedPtr<FJsonValue>& Entry : *Servers)
					{
						const TSharedPtr<FJsonObject> Server = Entry->AsObject();
						if (Server.IsValid())
						{
							Out += FString::Printf(TEXT("  MCP %s: %s\n"),
								*Server->GetStringField(TEXT("name")),
								*Server->GetStringField(TEXT("status")));
						}
					}
				}

				const TArray<TSharedPtr<FJsonValue>>* Tools = nullptr;
				if (Object->TryGetArrayField(TEXT("tools"), Tools) && Tools != nullptr)
				{
					Out += FString::Printf(TEXT("  %d tool(s) available\n"), Tools->Num());
				}
			}
			else if (Subtype == TEXT("api_error"))
			{
				Out += TEXT("  The API returned an error.\n");
			}
			else if (Subtype == TEXT("api_retry"))
			{
				// Without this, an overloaded API is minutes of silence under a "Building..."
				// button, which is indistinguishable from a hang.
				int32 Attempt = 0;
				int32 MaxRetries = 0;
				Object->TryGetNumberField(TEXT("attempt"), Attempt);
				Object->TryGetNumberField(TEXT("max_retries"), MaxRetries);
				Out += FString::Printf(TEXT("  Retrying (%d of %d)...\n"), Attempt, MaxRetries);
			}
			else if (Subtype == TEXT("notification"))
			{
				// Text the CLI wrote specifically to be shown to a human.
				Out += TEXT("  ") + Object->GetStringField(TEXT("text")) + TEXT("\n");
			}
			else if (Subtype.StartsWith(TEXT("model_refusal")) || Subtype.StartsWith(TEXT("model_fallback"))
				|| Subtype == TEXT("model_consent_fallback"))
			{
				// A refusal ends the run. Dropped, the trace simply stops with no reason given.
				Out += TEXT("  ") + Object->GetStringField(TEXT("content")) + TEXT("\n");
			}
		}
		else if (Type == TEXT("assistant") || Type == TEXT("user"))
		{
			const TSharedPtr<FJsonObject>* Message = nullptr;
			if (Object->TryGetObjectField(TEXT("message"), Message) && Message != nullptr)
			{
				const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
				if ((*Message)->TryGetArrayField(TEXT("content"), Content) && Content != nullptr)
				{
					for (const TSharedPtr<FJsonValue>& Block : *Content)
					{
						const TSharedPtr<FJsonObject> BlockObject = Block->AsObject();
						if (!BlockObject.IsValid())
						{
							continue;
						}

						const FString BlockType = BlockObject->GetStringField(TEXT("type"));
						if (BlockType == TEXT("text"))
						{
							InOutLastAssistant = BlockObject->GetStringField(TEXT("text"));
							Out += InOutLastAssistant + TEXT("\n");
						}
						else if (BlockType == TEXT("tool_use"))
						{
							Out += FString::Printf(TEXT("  [%s]\n"),
								*BlockObject->GetStringField(TEXT("name")));
						}
						else if (BlockType == TEXT("tool_result"))
						{
							// THE PAYLOAD OF EVERY user EVENT IN AN AGENTIC RUN, and where a
							// failed MCP call puts its error text. Only errors are shown: a
							// successful tool result is usually a wall of spec JSON that would
							// bury the narration, but a failure is the whole story.
							bool bIsError = false;
							if (BlockObject->TryGetBoolField(TEXT("is_error"), bIsError) && bIsError)
							{
								FString ErrorText;
								if (!BlockObject->TryGetStringField(TEXT("content"), ErrorText))
								{
									ErrorText = TEXT("(no detail given)");
								}
								Out += TEXT("  FAILED: ") + ErrorText.Left(600) + TEXT("\n");
							}
						}
					}
				}
				else
				{
					// A user event whose content is a plain string rather than an array. Silently
					// skipped before, which is a whole event kind vanishing.
					FString Plain;
					if ((*Message)->TryGetStringField(TEXT("content"), Plain) && !Plain.IsEmpty())
					{
						Out += Plain + TEXT("\n");
					}
				}
			}
		}
		else if (Type == TEXT("result"))
		{
			// ONLY IF IT IS NOT A REPEAT. On a successful run the result field IS the text of
			// the last assistant block - the same string printed a moment earlier when that
			// event came through - so appending it unconditionally showed Claude's closing
			// summary twice at the bottom of every trace.
			const FString Result = Object->GetStringField(TEXT("result"));
			if (!Result.IsEmpty() && Result != InOutLastAssistant)
			{
				Out += TEXT("\n") + Result + TEXT("\n");
			}

			// NOT "money taken from your account", which is what this used to say.
			//
			// total_cost_usd is a computed API-equivalent price derived from token usage, and
			// the CLI emits it identically whether it is authenticated by an API key or by a
			// Pro/Max subscription. On a subscription nothing is charged at all - so telling an
			// artist a build "used $0.87 of your Claude account" is false, and false in the
			// direction that makes a reasonable person stop using the tool.
			double Cost = 0.0;
			if (Object->TryGetNumberField(TEXT("total_cost_usd"), Cost) && Cost > 0.0)
			{
				Out += FString::Printf(
					TEXT("\nDone - about $%.2f of tokens at API rates. On a Claude ")
					TEXT("subscription that is what it would have cost, not a charge.\n"), Cost);
			}
		}
	}

	return Out;
}

bool FHFClaudeCli::Start(
	const FString& Executable,
	const FString& Arguments,
	const FString& WorkingDirectory,
	FRun& OutRun,
	FString& OutError)
{
	// RESET FIRST, and this is not tidiness.
	//
	// FRun is a long-lived member of the panel, so a second Generate re-uses the struct a finished
	// run left behind. Finish() sets bFinished, and Start() used only to fill in the handles - so
	// on every run after the first, Pump() returned immediately on the stale flag, PumpGeneration
	// saw a finished run on its first tick and called Finish(), which TerminateProc'd the Claude
	// process launched a hundred milliseconds earlier. The first generation in a session worked
	// and every one after it killed itself, which reads as intermittent rather than as broken.
	//
	// PendingOut and StdErr carried over too, so the next run's trace opened with the last one's
	// tail.
	OutRun = FRun();

	if (!FPlatformProcess::CreatePipe(OutRun.OutRead, OutRun.OutWrite)
		|| !FPlatformProcess::CreatePipe(OutRun.ErrRead, OutRun.ErrWrite))
	{
		OutError = TEXT("Could not create a pipe to read Claude's output.");
		Finish(OutRun);
		return false;
	}

	OutRun.Process = FPlatformProcess::CreateProc(
		*Executable,
		*Arguments,
		/*bLaunchDetached*/ false,
		/*bLaunchHidden*/ true,
		/*bLaunchReallyHidden*/ true,
		/*OutProcessID*/ nullptr,
		/*PriorityModifier*/ 0,
		WorkingDirectory.IsEmpty() ? nullptr : *WorkingDirectory,
		OutRun.OutWrite,
		/*PipeReadChild*/ nullptr,
		OutRun.ErrWrite);

	if (!OutRun.Process.IsValid())
	{
		OutError = FString::Printf(TEXT("Could not start '%s'."), *Executable);
		Finish(OutRun);
		return false;
	}

	return true;
}

void FHFClaudeCli::Pump(FRun& Run, TArray<FString>& OutCompleteLines)
{
	OutCompleteLines.Reset();

	if (!Run.IsValid() || Run.bFinished)
	{
		return;
	}

	Run.PendingOut += FPlatformProcess::ReadPipe(Run.OutRead);
	Run.StdErr += FPlatformProcess::ReadPipe(Run.ErrRead);

	// Split on newlines and keep the tail. A read can land mid-object, and handing half a line to
	// a JSON parse would drop the event rather than merely delaying it.
	int32 Newline = INDEX_NONE;
	while (Run.PendingOut.FindChar(TEXT('\n'), Newline))
	{
		FString Line = Run.PendingOut.Left(Newline);
		Run.PendingOut.RightChopInline(Newline + 1, EAllowShrinking::No);

		Line.TrimEndInline();
		if (!Line.IsEmpty())
		{
			OutCompleteLines.Add(MoveTemp(Line));
		}
	}

	if (!FPlatformProcess::IsProcRunning(Run.Process))
	{
		// One last read: anything written between the read above and the process exiting would
		// otherwise be lost, and on a short run that can be the entire result.
		Run.PendingOut += FPlatformProcess::ReadPipe(Run.OutRead);
		Run.StdErr += FPlatformProcess::ReadPipe(Run.ErrRead);

		Run.PendingOut.TrimEndInline();
		if (!Run.PendingOut.IsEmpty())
		{
			OutCompleteLines.Add(Run.PendingOut);
			Run.PendingOut.Reset();
		}

		FPlatformProcess::GetProcReturnCode(Run.Process, &Run.ReturnCode);
		Run.bFinished = true;
	}
}

void FHFClaudeCli::Finish(FRun& Run)
{
	if (Run.Process.IsValid())
	{
		if (FPlatformProcess::IsProcRunning(Run.Process))
		{
			// KillTree, because the CLI spawns its own children and leaving them behind would keep
			// the MCP session open against an editor that has moved on.
			FPlatformProcess::TerminateProc(Run.Process, /*KillTree*/ true);
		}
		FPlatformProcess::CloseProc(Run.Process);
		Run.Process.Reset();
	}

	if (Run.OutRead != nullptr || Run.OutWrite != nullptr)
	{
		FPlatformProcess::ClosePipe(Run.OutRead, Run.OutWrite);
		Run.OutRead = nullptr;
		Run.OutWrite = nullptr;
	}

	if (Run.ErrRead != nullptr || Run.ErrWrite != nullptr)
	{
		FPlatformProcess::ClosePipe(Run.ErrRead, Run.ErrWrite);
		Run.ErrRead = nullptr;
		Run.ErrWrite = nullptr;
	}

	Run.bFinished = true;
}

int32 FHFClaudeCli::RunToCompletion(
	const FString& Executable,
	const FString& Arguments,
	const FString& WorkingDirectory,
	const double TimeoutSeconds,
	FString& OutStdOut,
	FString& OutStdErr)
{
	OutStdOut.Reset();
	OutStdErr.Reset();

	void* OutReadPipe = nullptr;
	void* OutWritePipe = nullptr;
	void* ErrReadPipe = nullptr;
	void* ErrWritePipe = nullptr;

	if (!FPlatformProcess::CreatePipe(OutReadPipe, OutWritePipe)
		|| !FPlatformProcess::CreatePipe(ErrReadPipe, ErrWritePipe))
	{
		OutStdErr = TEXT("Could not create a pipe to read the CLI's output.");
		return -1;
	}

	ON_SCOPE_EXIT
	{
		FPlatformProcess::ClosePipe(OutReadPipe, OutWritePipe);
		FPlatformProcess::ClosePipe(ErrReadPipe, ErrWritePipe);
	};

	FProcHandle Process = FPlatformProcess::CreateProc(
		*Executable,
		*Arguments,
		/*bLaunchDetached*/ false,
		/*bLaunchHidden*/ true,
		/*bLaunchReallyHidden*/ true,
		/*OutProcessID*/ nullptr,
		/*PriorityModifier*/ 0,
		WorkingDirectory.IsEmpty() ? nullptr : *WorkingDirectory,
		OutWritePipe,
		/*PipeReadChild*/ nullptr,
		ErrWritePipe);

	if (!Process.IsValid())
	{
		OutStdErr = FString::Printf(TEXT("Could not start '%s'."), *Executable);
		return -1;
	}

	const double StartedAt = FPlatformTime::Seconds();

	// Drained WHILE it runs, not after. A pipe that fills blocks the child, so a process producing
	// more output than the buffer holds would deadlock against a reader waiting for it to exit.
	while (FPlatformProcess::IsProcRunning(Process))
	{
		OutStdOut += FPlatformProcess::ReadPipe(OutReadPipe);
		OutStdErr += FPlatformProcess::ReadPipe(ErrReadPipe);

		if (TimeoutSeconds > 0.0 && FPlatformTime::Seconds() - StartedAt > TimeoutSeconds)
		{
			FPlatformProcess::TerminateProc(Process, /*KillTree*/ true);
			FPlatformProcess::CloseProc(Process);

			OutStdErr += FString::Printf(
				TEXT("\nTimed out after %.0f seconds."), TimeoutSeconds);
			return -1;
		}

		FPlatformProcess::Sleep(0.02f);
	}

	// Whatever was written between the last read and the process exiting.
	OutStdOut += FPlatformProcess::ReadPipe(OutReadPipe);
	OutStdErr += FPlatformProcess::ReadPipe(ErrReadPipe);

	int32 ReturnCode = -1;
	FPlatformProcess::GetProcReturnCode(Process, &ReturnCode);
	FPlatformProcess::CloseProc(Process);

	return ReturnCode;
}
