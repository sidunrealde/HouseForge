// Copyright Siddartha G. All Rights Reserved.

#include "Toolset/HFToolset.h"

#include "Editor.h"
#include "HFEditorSubsystem.h"
#include "Model/HFTypes.h"

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

	/**
	 * Parses an enum value from the name a caller typed, listing the real ones when it does not match.
	 *
	 * A tool that answers "invalid fixture type" and stops is a tool a model retries at random. The
	 * valid names are in the reflection data already, so saying them costs nothing and turns a dead
	 * end into one more round trip.
	 */
	template <typename TEnum>
	bool ParseEnumName(const FString& Name, const TCHAR* What, TEnum& OutValue, FString& OutError)
	{
		const UEnum* Enum = StaticEnum<TEnum>();
		if (Enum == nullptr)
		{
			OutError = FString::Printf(TEXT("FAILED: %s is not a reflected enum."), What);
			return false;
		}

		// No flags: the lookup goes through FName, whose comparison is already case-insensitive, so
		// "wardrobe" and "Wardrobe" both resolve. A model should not have to guess our casing.
		const int64 Value = Enum->GetValueByNameString(Name);
		if (Value != INDEX_NONE)
		{
			OutValue = static_cast<TEnum>(Value);
			return true;
		}

		TArray<FString> Names;
		// NumEnums includes the generated _MAX entry, which is not a value anybody can pass.
		for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
		{
			Names.Add(Enum->GetNameStringByIndex(Index));
		}

		OutError = FString::Printf(TEXT("FAILED: '%s' is not a %s. Valid values: %s"),
			*Name, What, *FString::Join(Names, TEXT(", ")));
		return false;
	}

	/** Builds an override from what a tool was given, or explains what was wrong with it. */
	bool MakeOverrideFromArgs(const FString& AssetPath, const FString& FitMode, float YawDegrees,
		FHFAssetOverride& OutOverride, FString& OutError)
	{
		if (AssetPath.IsEmpty())
		{
			OutError = TEXT("FAILED: no asset path given.");
			return false;
		}

		// Defaulted rather than refused when blank: UniformFit keeps the asset's proportions and is
		// the answer that cannot distort anything, so it is the safe thing to assume.
		EHFAssetFitMode Mode = EHFAssetFitMode::UniformFit;
		if (!FitMode.IsEmpty() && !ParseEnumName(FitMode, TEXT("fit mode"), Mode, OutError))
		{
			return false;
		}

		OutOverride.OverrideMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(AssetPath));
		OutOverride.FitMode = Mode;
		OutOverride.AssetRotationOffset = FRotator(0.0, YawDegrees, 0.0);

		// NEVER STAMPED WITH A TABLE NAME. Anything a caller asked for by name is a decision about
		// those instances, and a later mapping-table pass must not take it back.
		OutOverride.SourceTable = NAME_None;

		return true;
	}
}

FString UHFToolset::ListFixtureTypes()
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (Editor == nullptr)
	{
		return NoEditor();
	}

	const TArray<FHFFixtureGroup> Groups = Editor->GetFixtureGroups();
	if (Groups.IsEmpty())
	{
		return TEXT("No generated fixtures in the level. Build a house first.");
	}

	TArray<FString> Lines;
	for (const FHFFixtureGroup& Group : Groups)
	{
		FString Line = FString::Printf(TEXT("%s  x%d"), *Group.TypeName, Group.InstanceCount);

		if (Group.OverriddenCount > 0)
		{
			Line += FString::Printf(TEXT("  (%d replaced"), Group.OverriddenCount);
			if (Group.HandPickedCount > 0)
			{
				// Said explicitly, because it is the number that decides what a batch pass will skip.
				Line += FString::Printf(TEXT(", %d chosen individually and left alone by batch passes"),
					Group.HandPickedCount);
			}
			Line += TEXT(")");
		}

		Line += FString::Printf(TEXT("  [%s]"), *FString::JoinBy(Group.ElementIds, TEXT(" "),
			[](const FName& Id) { return Id.ToString(); }));

		Lines.Add(Line);
	}

	return FString::Printf(TEXT("%d fixture type(s) in the level:\n%s"),
		Groups.Num(), *FString::Join(Lines, TEXT("\n")));
}

FString UHFToolset::ReplaceFixtureType(const FString& FixtureType, const FString& AssetPath,
	const FString& FitMode, float YawDegrees)
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (Editor == nullptr)
	{
		return NoEditor();
	}

	FString Error;

	EHFFixtureType Type = EHFFixtureType::Unknown;
	if (!ParseEnumName(FixtureType, TEXT("fixture type"), Type, Error))
	{
		return Error;
	}

	FHFAssetOverride Override;
	if (!MakeOverrideFromArgs(AssetPath, FitMode, YawDegrees, Override, Error))
	{
		return Error;
	}

	FString ReportText;
	const FHFOperationResult Result = Editor->ApplyAssetToType(Type, Override, ReportText);

	// The report goes back on success too: how many were replaced, which came out stretched and which
	// have no collision are exactly what the caller has to act on, and a bare "Ok" throws them away.
	return Report(Result);
}

FString UHFToolset::PreviewFixtureAsset(const FString& ElementId, const FString& AssetPath,
	const FString& FitMode)
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (Editor == nullptr)
	{
		return NoEditor();
	}

	FString Error;
	FHFAssetOverride Override;
	if (!MakeOverrideFromArgs(AssetPath, FitMode, 0.0f, Override, Error))
	{
		return Error;
	}

	FHFAssetFitResult Fit;
	return Report(Editor->PreviewAssetOverride(ElementId, Override, Fit));
}

FString UHFToolset::RevertFixturesToGenerated(const FString& ElementIds)
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (Editor == nullptr)
	{
		return NoEditor();
	}

	TArray<FString> Ids;
	ElementIds.ParseIntoArray(Ids, TEXT(","), /*InCullEmpty*/ true);
	for (FString& Id : Ids)
	{
		Id.TrimStartAndEndInline();
	}

	FString ReportText;
	return Report(Editor->ClearAssetOverrides(Ids, ReportText));
}

FString UHFToolset::ApplyAssetMappingTable()
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (Editor == nullptr)
	{
		return NoEditor();
	}

	FString ReportText;
	return Report(Editor->ApplyAssetMappingTable(ReportText));
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

FString UHFToolset::ConvertLength(const FString& Text, const FString& DefaultUnits)
{
	const FString Lower = DefaultUnits.TrimStartAndEnd().ToLower();

	EHFUnits Units = EHFUnits::Millimeters;
	if (Lower == TEXT("centimeters") || Lower == TEXT("centimetres") || Lower == TEXT("cm")) { Units = EHFUnits::Centimeters; }
	else if (Lower == TEXT("meters") || Lower == TEXT("metres") || Lower == TEXT("m"))       { Units = EHFUnits::Meters; }
	else if (Lower == TEXT("feet") || Lower == TEXT("ft"))                                   { Units = EHFUnits::Feet; }
	else if (Lower == TEXT("inches") || Lower == TEXT("in"))                                 { Units = EHFUnits::Inches; }

	double Centimeters = 0.0;
	if (!FHFUnits::ParseLengthToCentimeters(Text, Units, Centimeters))
	{
		return FString::Printf(
			TEXT("FAILED: could not read '%s' as a dimension. Accepted forms: 12'-6\", 12' 6\", 78\", 3600, 3600mm, 360cm, 3.6m."),
			*Text);
	}

	// Report every unit at once: whichever the spec is written in, the number is right there.
	return FString::Printf(TEXT("%s = %.2f cm = %.1f mm = %.4f m = %.4f ft = %.3f in"),
		*Text, Centimeters, Centimeters * 10.0, Centimeters / 100.0,
		Centimeters / 30.48, Centimeters / 2.54);
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

FString UHFToolset::CaptureTopDown(const FString& FileName, int32 Resolution, float SectionHeightCm)
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (Editor == nullptr)
	{
		return NoEditor();
	}

	FString Path;
	const FHFOperationResult Result = Editor->CaptureTopDown(FileName, Resolution, SectionHeightCm, Path);
	return Report(Result);
}

FString UHFToolset::CaptureView(const FString& FileName, int32 Resolution,
	float CameraX, float CameraY, float CameraZ,
	float TargetX, float TargetY, float TargetZ,
	float FieldOfViewDegrees)
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (Editor == nullptr)
	{
		return NoEditor();
	}

	FString Path;
	const FHFOperationResult Result = Editor->CaptureView(FileName, Resolution,
		FVector(CameraX, CameraY, CameraZ), FVector(TargetX, TargetY, TargetZ),
		FieldOfViewDegrees, Path);
	return Report(Result);
}

FString UHFToolset::SetHouseRenderMode(bool Baked)
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (Editor == nullptr)
	{
		return NoEditor();
	}

	FString BakeReport;
	const FHFOperationResult Result = Editor->SetHouseRenderMode(Baked, BakeReport);

	// The report goes back on success as well as on failure. A bake's interesting numbers - how many
	// parts, how many skipped, and what share of the flat is now radiant - are exactly what the
	// caller needs in order to decide whether to render, and a bare "Ok" would throw them away.
	return Report(Result);
}

FString UHFToolset::CheckLumenCoverage()
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (Editor == nullptr)
	{
		return NoEditor();
	}

	FString Coverage;
	const FHFOperationResult Result = Editor->CheckLumenCoverage(Coverage);
	return Report(Result);
}

FString UHFToolset::FindBakedOrphans()
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (Editor == nullptr)
	{
		return NoEditor();
	}

	FString ReportText;
	return Report(Editor->FindBakedOrphans(ReportText));
}

FString UHFToolset::DeleteBakedOrphans()
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (Editor == nullptr)
	{
		return NoEditor();
	}

	FString ReportText;
	return Report(Editor->DeleteBakedOrphans(ReportText));
}

FString UHFToolset::AdoptBakedAssetEdits(const FString& ElementIds)
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (Editor == nullptr)
	{
		return NoEditor();
	}

	TArray<FString> Ids;
	ElementIds.ParseIntoArray(Ids, TEXT(","), /*InCullEmpty*/ true);
	for (FString& Id : Ids)
	{
		Id.TrimStartAndEndInline();
	}

	FString ReportText;
	return Report(Editor->AdoptBakedAssetEdits(Ids, ReportText));
}
