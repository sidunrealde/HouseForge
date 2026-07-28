// Copyright Siddartha G. All Rights Reserved.

#include "Toolset/HFToolset.h"

#include "Editor.h"
#include "HFEditorSubsystem.h"

namespace
{
	UHFEditorSubsystem* Subsystem()
	{
		return GEditor ? GEditor->GetEditorSubsystem<UHFEditorSubsystem>() : nullptr;
	}

	/**
	 * Tool results are plain strings, so failures have to be legible rather than a bare error
	 * code - the message is the only thing the caller gets to act on.
	 */
	FString Report(const FHFOperationResult& Result)
	{
		return Result.bSuccess ? Result.Message : FString::Printf(TEXT("FAILED: %s"), *Result.Message);
	}

	FString NoEditor()
	{
		return TEXT("FAILED: the HouseForge editor subsystem is unavailable.");
	}
}

FString UHFToolset::ListDrawings()
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (Editor == nullptr)
	{
		return NoEditor();
	}

	const TArray<FString> Drawings = Editor->ListDrawings();
	if (Drawings.IsEmpty())
	{
		return FString::Printf(
			TEXT("No drawings found. Import some with ImportDrawings, or place .png/.jpg files under %s"),
			*Editor->GetDrawingsDirectory());
	}

	return FString::Printf(TEXT("%d drawing(s) under %s:\n%s"),
		Drawings.Num(), *Editor->GetDrawingsDirectory(), *FString::Join(Drawings, TEXT("\n")));
}

FString UHFToolset::GetDrawingsPath()
{
	UHFEditorSubsystem* Editor = Subsystem();
	return Editor ? Editor->GetDrawingsDirectory() : NoEditor();
}

FString UHFToolset::ImportDrawings(const FString& SourcePaths, const FString& SetName)
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (Editor == nullptr)
	{
		return NoEditor();
	}

	TArray<FString> Paths;
	SourcePaths.ParseIntoArray(Paths, TEXT(";"), /*InCullEmpty*/ true);
	for (FString& Path : Paths)
	{
		Path.TrimStartAndEndInline();
		Path = Path.TrimQuotes();
	}

	TArray<FString> Imported;
	return Report(Editor->ImportDrawings(Paths, SetName, Imported));
}

FString UHFToolset::ValidateSpec(const FString& SpecJson)
{
	UHFEditorSubsystem* Editor = Subsystem();
	return Editor ? Report(Editor->ValidateSpecJson(SpecJson)) : NoEditor();
}

FString UHFToolset::ApplySpec(const FString& SpecJson, const FString& LevelName)
{
	UHFEditorSubsystem* Editor = Subsystem();
	return Editor ? Report(Editor->ApplySpecJson(SpecJson, LevelName)) : NoEditor();
}

FString UHFToolset::GetSpec()
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (Editor == nullptr)
	{
		return NoEditor();
	}

	FString Json;
	const FHFOperationResult Result = Editor->GetSpecJson(Json);
	return Result.bSuccess ? Json : Report(Result);
}

FString UHFToolset::ListElements(const FString& Category)
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (Editor == nullptr)
	{
		return NoEditor();
	}

	FString Summary;
	const FHFOperationResult Result = Editor->ListElements(Category, Summary);
	return Result.bSuccess ? Summary : Report(Result);
}

FString UHFToolset::ModifyElement(const FString& Category, const FString& ElementId, const FString& PropertiesJson)
{
	UHFEditorSubsystem* Editor = Subsystem();
	return Editor ? Report(Editor->ModifyElement(Category, ElementId, PropertiesJson)) : NoEditor();
}

FString UHFToolset::DeleteElement(const FString& Category, const FString& ElementId)
{
	UHFEditorSubsystem* Editor = Subsystem();
	return Editor ? Report(Editor->DeleteElement(Category, ElementId)) : NoEditor();
}

FString UHFToolset::SaveSpec(const FString& FileName)
{
	UHFEditorSubsystem* Editor = Subsystem();
	return Editor ? Report(Editor->SaveSpecToFile(FileName)) : NoEditor();
}

FString UHFToolset::CaptureTopDown(const FString& FileName, int32 Resolution)
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (Editor == nullptr)
	{
		return NoEditor();
	}

	FString Path;
	const FHFOperationResult Result = Editor->CaptureTopDown(FileName, Resolution, Path);
	return Report(Result);
}
