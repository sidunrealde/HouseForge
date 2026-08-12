// Copyright Siddartha G. All Rights Reserved.

#include "Claude/HFClaudeCli.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

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
		if (Trimmed.Contains(TEXT("Connected"), ESearchCase::IgnoreCase))
		{
			return EHFClaudeState::Ready;
		}

		if (Trimmed.Contains(TEXT("Needs authentication"), ESearchCase::IgnoreCase))
		{
			return EHFClaudeState::NotAuthenticated;
		}

		// "Failed to connect", "ConnectionRefused", and anything else this line can say all mean
		// the same thing for a server the plugin itself hosts: nothing is listening yet.
		return EHFClaudeState::ServerNotRunning;
	}

	// Named nowhere in the list. Either .mcp.json was never written or it does not carry our
	// server - both are "the config is not there", not "the server is down".
	return EHFClaudeState::ConfigMissing;
}

FString FHFClaudeCli::NeutralWorkingDirectory()
{
	const FString Directory = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("HouseForge"), TEXT("Cli"));
	IFileManager::Get().MakeDirectory(*Directory, /*Tree*/ true);
	return Directory;
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
