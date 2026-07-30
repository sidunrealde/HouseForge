// Copyright Siddartha G. All Rights Reserved.

#include "HFEditorSubsystem.h"

#include "Actors/HFHouseActor.h"
#include "Capture/HFPlanSection.h"
#include "Capture/HFSceneCapture.h"
#include "Capture/HFViewingLight.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HouseForgeEditor.h"
#include "ImageUtils.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Model/HFSpecSerializer.h"
#include "Model/HFBuildDefaults.h"
#include "Model/HFSpecValidator.h"
#include "Model/HFSettings.h"
#include "Actors/HFElementActors.h"
#include "Actors/HFOpeningActor.h"
#include "UnrealEdGlobals.h"

#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "HouseForgeEditor"

namespace
{
	/**
	 * What this project judges a spec against.
	 *
	 * Resolved here, in the composing layer, for the same reason the construction figures are: the
	 * validator takes its limits as an argument and never looks them up, so something has to do the
	 * looking up, and the subsystem is the outermost thing that knows a project exists.
	 *
	 * Every entry point that validates has to go through this. A limit only some of them honour is
	 * worse than no limit at all - a spec would be refused by the MCP validate tool and then accepted
	 * by the apply tool that follows it, or the other way round.
	 */
	FHFValidationLimits ProjectValidationLimits()
	{
		return FHFBuildDefaults::FromProjectSettings().Validation;
	}
}

FHFOperationResult FHFOperationResult::Ok(const FString& InMessage)
{
	FHFOperationResult R;
	R.bSuccess = true;
	R.Message = InMessage;
	return R;
}

FHFOperationResult FHFOperationResult::Fail(const FString& InMessage)
{
	FHFOperationResult R;
	R.bSuccess = false;
	R.Message = InMessage;
	return R;
}

FString UHFEditorSubsystem::PluginDir()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("HouseForge"));
	return Plugin.IsValid() ? FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir()) : FString();
}

FString UHFEditorSubsystem::GetDrawingsDirectory() const
{
	const FString Base = PluginDir();
	return Base.IsEmpty() ? FString() : FPaths::Combine(Base, TEXT("Reference"), TEXT("Drawings"));
}

FString UHFEditorSubsystem::GetSpecsDirectory() const
{
	const FString Base = PluginDir();
	return Base.IsEmpty() ? FString() : FPaths::Combine(Base, TEXT("Reference"), TEXT("Specs"));
}

TArray<FString> UHFEditorSubsystem::ListDrawings() const
{
	TArray<FString> Result;

	const FString Root = GetDrawingsDirectory();
	if (Root.IsEmpty())
	{
		return Result;
	}

	IFileManager& Files = IFileManager::Get();
	for (const TCHAR* Extension : { TEXT("*.png"), TEXT("*.jpg"), TEXT("*.jpeg") })
	{
		TArray<FString> Found;
		Files.FindFilesRecursive(Found, *Root, Extension, /*Files*/ true, /*Directories*/ false, /*bClearFileNames*/ false);
		Result.Append(Found);
	}

	// Relative paths read better in a tool result and are stable across machines.
	for (FString& Path : Result)
	{
		FPaths::MakePathRelativeTo(Path, *(Root / TEXT("")));
	}
	Result.Sort();
	return Result;
}

bool UHFEditorSubsystem::RasterisePdf(const FString& PdfPath, const FString& DestinationDir,
	TArray<FString>& OutPngPaths, FString& OutError) const
{
	// Unreal cannot read PDFs, and AutoCAD sheet sets almost always arrive as one. Rasterising is
	// delegated to the same local virtual environment the drawing generator uses, so no system
	// Python is touched and the dependency stays inside the plugin.
	const FString Base = PluginDir();
	if (Base.IsEmpty())
	{
		OutError = TEXT("Could not locate the HouseForge plugin directory.");
		return false;
	}

	const FString Script = FPaths::Combine(Base, TEXT("Scripts"), TEXT("hf_pdf.py"));
	const FString VenvPython = FPaths::Combine(Base, TEXT("Scripts"), TEXT(".venv"), TEXT("Scripts"), TEXT("python.exe"));

	if (!FPaths::FileExists(Script))
	{
		OutError = FString::Printf(TEXT("PDF converter missing at '%s'."), *Script);
		return false;
	}

	const FString Python = FPaths::FileExists(VenvPython) ? VenvPython : FString(TEXT("python"));
	const FString Args = FString::Printf(TEXT("\"%s\" --pdf \"%s\" --out \"%s\""), *Script, *PdfPath, *DestinationDir);

	int32 ReturnCode = -1;
	FString StdOut;
	FString StdErr;
	FPlatformProcess::ExecProcess(*Python, *Args, &ReturnCode, &StdOut, &StdErr);

	if (ReturnCode != 0)
	{
		OutError = FString::Printf(
			TEXT("PDF rasterisation failed (exit %d). Run Scripts/hf-drawings.ps1 once to provision the local Python environment, then retry. %s"),
			ReturnCode, *StdErr.TrimStartAndEnd());
		return false;
	}

	// The script prints one produced path per line.
	TArray<FString> Emitted;
	StdOut.ParseIntoArrayLines(Emitted);
	for (const FString& Line : Emitted)
	{
		const FString Trimmed = Line.TrimStartAndEnd();
		if (!Trimmed.IsEmpty() && FPaths::FileExists(Trimmed))
		{
			OutPngPaths.Add(Trimmed);
		}
	}

	if (OutPngPaths.IsEmpty())
	{
		OutError = FString::Printf(TEXT("PDF '%s' produced no pages."), *FPaths::GetCleanFilename(PdfPath));
		return false;
	}

	return true;
}

FHFOperationResult UHFEditorSubsystem::ImportDrawings(const TArray<FString>& SourcePaths,
	const FString& SetName, TArray<FString>& OutImported)
{
	OutImported.Reset();

	if (SourcePaths.IsEmpty())
	{
		return FHFOperationResult::Fail(TEXT("No files given to import."));
	}

	const FString Root = GetDrawingsDirectory();
	if (Root.IsEmpty())
	{
		return FHFOperationResult::Fail(TEXT("Could not locate the HouseForge plugin directory."));
	}

	const FString Folder = SetName.IsEmpty() ? FPaths::GetBaseFilename(SourcePaths[0]) : SetName;
	const FString Destination = FPaths::Combine(Root, Folder);

	IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();
	if (!Platform.CreateDirectoryTree(*Destination))
	{
		return FHFOperationResult::Fail(FString::Printf(TEXT("Could not create '%s'."), *Destination));
	}

	TArray<FString> Problems;

	for (const FString& Source : SourcePaths)
	{
		if (!FPaths::FileExists(Source))
		{
			Problems.Add(FString::Printf(TEXT("'%s' does not exist"), *Source));
			continue;
		}

		const FString Extension = FPaths::GetExtension(Source).ToLower();

		if (Extension == TEXT("pdf"))
		{
			TArray<FString> Pages;
			FString Error;
			if (RasterisePdf(Source, Destination, Pages, Error))
			{
				OutImported.Append(Pages);
			}
			else
			{
				Problems.Add(Error);
			}
			continue;
		}

		if (Extension != TEXT("png") && Extension != TEXT("jpg") && Extension != TEXT("jpeg"))
		{
			Problems.Add(FString::Printf(TEXT("'%s' is a .%s; only .png, .jpg and .pdf can be read"),
				*FPaths::GetCleanFilename(Source), *Extension));
			continue;
		}

		const FString Target = FPaths::Combine(Destination, FPaths::GetCleanFilename(Source));
		if (Platform.CopyFile(*Target, *Source))
		{
			OutImported.Add(Target);
		}
		else
		{
			Problems.Add(FString::Printf(TEXT("could not copy '%s'"), *FPaths::GetCleanFilename(Source)));
		}
	}

	// Hand back paths relative to the drawings root, which is how ListDrawings reports them.
	for (FString& Path : OutImported)
	{
		FPaths::MakePathRelativeTo(Path, *(Root / TEXT("")));
	}

	if (OutImported.IsEmpty())
	{
		return FHFOperationResult::Fail(FString::Printf(TEXT("Nothing imported. %s"), *FString::Join(Problems, TEXT("; "))));
	}

	FString Message = FString::Printf(TEXT("Imported %d drawing(s) into '%s': %s"),
		OutImported.Num(), *Folder, *FString::Join(OutImported, TEXT(", ")));

	if (!Problems.IsEmpty())
	{
		Message += FString::Printf(TEXT(". Skipped: %s"), *FString::Join(Problems, TEXT("; ")));
	}

	UE_LOG(LogHouseForgeEditor, Log, TEXT("%s"), *Message);
	return FHFOperationResult::Ok(Message);
}

FHFOperationResult UHFEditorSubsystem::ValidateSpecJson(const FString& SpecJson) const
{
	FHFHouseSpec Spec;
	FString Error;
	if (!FHFSpecSerializer::FromJsonString(SpecJson, Spec, Error))
	{
		return FHFOperationResult::Fail(Error);
	}

	const FHFValidationResult Validation = FHFSpecValidator::Validate(Spec, ProjectValidationLimits());
	if (Validation.HasErrors())
	{
		return FHFOperationResult::Fail(Validation.ToString());
	}

	return FHFOperationResult::Ok(Validation.ToString());
}

FHFOperationResult UHFEditorSubsystem::ApplySpecJson(const FString& SpecJson, const FString& LevelName)
{
	FHFHouseSpec Spec;
	FString Error;
	if (!FHFSpecSerializer::FromJsonString(SpecJson, Spec, Error))
	{
		return FHFOperationResult::Fail(Error);
	}

	const FHFValidationResult Validation = FHFSpecValidator::Validate(Spec, ProjectValidationLimits());
	if (Validation.HasErrors())
	{
		// Refusing here matters: a half-built house would screenshot plausibly while the spec
		// behind it is wrong, and the comparison against the drawing would silently pass.
		return FHFOperationResult::Fail(FString::Printf(
			TEXT("Refusing to build - the spec has validation errors.\n%s"), *Validation.ToString()));
	}

	if (!LevelName.IsEmpty())
	{
		if (GEditor == nullptr)
		{
			return FHFOperationResult::Fail(TEXT("No editor is available to create a level."));
		}

		// Do not prompt: this runs from a tool call, where a modal save dialog would hang the
		// caller with nothing able to answer it.
		GEditor->CreateNewMapForEditing(/*bPromptUserToSave*/ false);

		if (GEditor->GetEditorWorldContext().World() == nullptr)
		{
			return FHFOperationResult::Fail(TEXT("Could not create a new level."));
		}
	}

	FHFOperationResult SpawnResult = SpawnHouse(Spec);
	if (!SpawnResult.bSuccess)
	{
		return SpawnResult;
	}

	// A freshly created level has no lights in it at all, so the house that was just built is
	// invisible until something puts one there. Idempotent, so building into an existing level -
	// or building the same spec twice - does not accumulate suns.
	EnsureViewingLight();

	FString Message = FString::Printf(
		TEXT("Built '%s': %d walls, %d openings, %d rooms, %d beams, %d columns, %d false ceilings, %d fixtures."),
		*Spec.Name, Spec.Walls.Num(), Spec.Openings.Num(), Spec.Rooms.Num(),
		Spec.Beams.Num(), Spec.Columns.Num(), Spec.FalseCeilings.Num(), Spec.Fixtures.Num());

	if (Validation.HasWarnings())
	{
		Message += FString::Printf(TEXT("\n%s"), *Validation.ToString());
	}

	return FHFOperationResult::Ok(Message);
}

FHFOperationResult UHFEditorSubsystem::SpawnHouse(const FHFHouseSpec& Spec)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World == nullptr)
	{
		return FHFOperationResult::Fail(TEXT("No editor world is open."));
	}

	// One house per level. Replacing rather than adding keeps GetSpecJson unambiguous.
	//
	// The elements go first, explicitly. AHFHouseActor::Destroyed does this too, but saying it here
	// as well is what makes the central workflow - read drawing, build, screenshot, correct, rebuild
	// - safe to read: without it the level ends up holding the wrong house and the right one,
	// superimposed, while the log line reports the new house's element count and reads correct.
	for (TActorIterator<AHFHouseActor> It(World); It; ++It)
	{
		It->ClearGeometry();
		World->DestroyActor(*It);
	}

	FActorSpawnParameters Params;
	Params.Name = MakeUniqueObjectName(World->PersistentLevel, AHFHouseActor::StaticClass(), TEXT("HouseForge_House"));
	Params.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;

	AHFHouseActor* House = World->SpawnActor<AHFHouseActor>(AHFHouseActor::StaticClass(), FTransform::Identity, Params);
	if (House == nullptr)
	{
		return FHFOperationResult::Fail(TEXT("Could not spawn the house actor."));
	}

	House->SetActorLabel(Spec.Name.IsEmpty() ? TEXT("HouseForge House") : Spec.Name);
	House->SetSpec(Spec);

	return FHFOperationResult::Ok(TEXT("House actor spawned."));
}

AHFHouseActor* UHFEditorSubsystem::FindHouseActor() const
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World == nullptr)
	{
		return nullptr;
	}

	for (TActorIterator<AHFHouseActor> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

FHFOperationResult UHFEditorSubsystem::GetSpecJson(FString& OutSpecJson) const
{
	OutSpecJson.Reset();

	const AHFHouseActor* House = FindHouseActor();
	if (House == nullptr)
	{
		return FHFOperationResult::Fail(TEXT("No HouseForge house in the current level."));
	}

	FString Error;
	if (!FHFSpecSerializer::ToJsonString(House->Spec, OutSpecJson, Error))
	{
		return FHFOperationResult::Fail(Error);
	}

	return FHFOperationResult::Ok(FString::Printf(TEXT("Read back '%s' (%d rooms)."),
		*House->Spec.Name, House->Spec.Rooms.Num()));
}

FHFOperationResult UHFEditorSubsystem::SaveSpecToFile(const FString& FileName)
{
	const AHFHouseActor* House = FindHouseActor();
	if (House == nullptr)
	{
		return FHFOperationResult::Fail(TEXT("No HouseForge house in the current level."));
	}

	const FString Directory = GetSpecsDirectory();
	if (Directory.IsEmpty())
	{
		return FHFOperationResult::Fail(TEXT("Could not locate the HouseForge plugin directory."));
	}

	FString Name = FileName.IsEmpty() ? TEXT("Untitled") : FileName;
	if (!Name.EndsWith(TEXT(".json")))
	{
		Name += TEXT(".json");
	}

	const FString Path = FPaths::Combine(Directory, Name);

	FString Error;
	if (!FHFSpecSerializer::SaveToFile(House->Spec, Path, Error))
	{
		return FHFOperationResult::Fail(Error);
	}

	return FHFOperationResult::Ok(FString::Printf(TEXT("Wrote %s"), *Path));
}

FHFOperationResult UHFEditorSubsystem::ListElements(const FString& CategoryFilter, FString& OutSummary) const
{
	const AHFHouseActor* House = FindHouseActor();
	if (House == nullptr)
	{
		return FHFOperationResult::Fail(TEXT("No HouseForge house in the current level."));
	}

	const FHFHouseSpec& Spec = House->Spec;
	const FString Filter = CategoryFilter.ToLower();
	const bool bAll = Filter.IsEmpty() || Filter == TEXT("all");

	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("%s (units: centimeters)"), *Spec.Name));

	auto Section = [&Lines, &Filter, bAll](const TCHAR* Category, const TArray<FString>& Ids)
	{
		if (!bAll && Filter != FString(Category).ToLower())
		{
			return;
		}
		Lines.Add(FString::Printf(TEXT("%s (%d): %s"), Category, Ids.Num(),
			Ids.IsEmpty() ? TEXT("-") : *FString::Join(Ids, TEXT(", "))));
	};

	auto IdsOf = [](const auto& Container)
	{
		TArray<FString> Ids;
		for (const auto& Element : Container)
		{
			Ids.Add(Element.Id.ToString());
		}
		return Ids;
	};

	Section(TEXT("walls"), IdsOf(Spec.Walls));
	Section(TEXT("openings"), IdsOf(Spec.Openings));
	Section(TEXT("beams"), IdsOf(Spec.Beams));
	Section(TEXT("columns"), IdsOf(Spec.Columns));
	Section(TEXT("rooms"), IdsOf(Spec.Rooms));
	Section(TEXT("falseCeilings"), IdsOf(Spec.FalseCeilings));
	Section(TEXT("fixtures"), IdsOf(Spec.Fixtures));

	OutSummary = FString::Join(Lines, TEXT("\n"));
	return FHFOperationResult::Ok(OutSummary);
}

namespace
{
	/** Applies a JSON patch onto one struct in an array, matched by id. */
	template <typename ElementType>
	bool PatchById(TArray<ElementType>& Container, const FName& Id,
		const TSharedRef<FJsonObject>& Patch, FString& OutError)
	{
		ElementType* Element = Container.FindByPredicate(
			[&Id](const ElementType& E) { return E.Id == Id; });

		if (Element == nullptr)
		{
			OutError = FString::Printf(TEXT("No element with id '%s' in that category."), *Id.ToString());
			return false;
		}

		// Round-trip through JSON so only the named fields change and everything else is preserved
		// exactly - a field-by-field setter would silently reset anything the caller omitted.
		TSharedPtr<FJsonObject> Merged = FJsonObjectConverter::UStructToJsonObject(*Element);
		if (!Merged.IsValid())
		{
			OutError = TEXT("Could not read the existing element.");
			return false;
		}

		for (const auto& Field : Patch->Values)
		{
			Merged->SetField(Field.Key, Field.Value);
		}

		ElementType Updated;
		if (!FJsonObjectConverter::JsonObjectToUStruct(Merged.ToSharedRef(), ElementType::StaticStruct(), &Updated, 0, 0))
		{
			OutError = TEXT("The patched element did not match the schema. Check field names and types.");
			return false;
		}

		*Element = Updated;
		return true;
	}
}

FHFOperationResult UHFEditorSubsystem::ModifyElement(const FString& Category, const FString& ElementId,
	const FString& PropertiesJson)
{
	AHFHouseActor* House = FindHouseActor();
	if (House == nullptr)
	{
		return FHFOperationResult::Fail(TEXT("No HouseForge house in the current level."));
	}

	TSharedPtr<FJsonObject> Patch;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PropertiesJson);
	if (!FJsonSerializer::Deserialize(Reader, Patch) || !Patch.IsValid())
	{
		return FHFOperationResult::Fail(FString::Printf(
			TEXT("Properties must be a JSON object of the fields to change: %s"), *Reader->GetErrorMessage()));
	}

	// Work on a copy so a change that breaks validation can be discarded wholesale.
	FHFHouseSpec Working = House->Spec;
	const FName Id(*ElementId);
	const FString Lower = Category.ToLower();

	FString Error;
	bool bPatched = false;

	if (Lower == TEXT("walls"))				{ bPatched = PatchById(Working.Walls, Id, Patch.ToSharedRef(), Error); }
	else if (Lower == TEXT("openings"))		{ bPatched = PatchById(Working.Openings, Id, Patch.ToSharedRef(), Error); }
	else if (Lower == TEXT("beams"))		{ bPatched = PatchById(Working.Beams, Id, Patch.ToSharedRef(), Error); }
	else if (Lower == TEXT("columns"))		{ bPatched = PatchById(Working.Columns, Id, Patch.ToSharedRef(), Error); }
	else if (Lower == TEXT("rooms"))		{ bPatched = PatchById(Working.Rooms, Id, Patch.ToSharedRef(), Error); }
	else if (Lower == TEXT("falseceilings")) { bPatched = PatchById(Working.FalseCeilings, Id, Patch.ToSharedRef(), Error); }
	else if (Lower == TEXT("fixtures"))		{ bPatched = PatchById(Working.Fixtures, Id, Patch.ToSharedRef(), Error); }
	else
	{
		return FHFOperationResult::Fail(FString::Printf(
			TEXT("Unknown category '%s'. Expected one of: walls, openings, beams, columns, rooms, falseCeilings, fixtures."),
			*Category));
	}

	if (!bPatched)
	{
		return FHFOperationResult::Fail(Error);
	}

	const FHFValidationResult Validation = FHFSpecValidator::Validate(Working, ProjectValidationLimits());
	if (Validation.HasErrors())
	{
		return FHFOperationResult::Fail(FString::Printf(
			TEXT("Change rejected - it would break the spec.\n%s"), *Validation.ToString()));
	}

	House->Modify();
	House->SetSpec(Working);

	return FHFOperationResult::Ok(FString::Printf(TEXT("Updated %s '%s'.%s"),
		*Category, *ElementId,
		Validation.HasWarnings() ? *FString::Printf(TEXT("\n%s"), *Validation.ToString()) : TEXT("")));
}

FHFOperationResult UHFEditorSubsystem::DeleteElement(const FString& Category, const FString& ElementId)
{
	AHFHouseActor* House = FindHouseActor();
	if (House == nullptr)
	{
		return FHFOperationResult::Fail(TEXT("No HouseForge house in the current level."));
	}

	FHFHouseSpec Working = House->Spec;
	const FName Id(*ElementId);
	const FString Lower = Category.ToLower();

	int32 Removed = 0;
	int32 Cascaded = 0;

	auto RemoveById = [&Id](auto& Container)
	{
		return Container.RemoveAll([&Id](const auto& E) { return E.Id == Id; });
	};

	if (Lower == TEXT("walls"))
	{
		Removed = RemoveById(Working.Walls);
		// Openings live in a wall; leaving them behind would dangle a reference the validator
		// would then reject, so they go with it.
		Cascaded = Working.Openings.RemoveAll([&Id](const FHFOpening& O) { return O.WallId == Id; });
		for (FHFFixture& Fixture : Working.Fixtures)
		{
			if (Fixture.AnchorWallId == Id)
			{
				Fixture.AnchorWallId = NAME_None;
			}
		}
	}
	else if (Lower == TEXT("rooms"))
	{
		Removed = RemoveById(Working.Rooms);
		Cascaded = Working.FalseCeilings.RemoveAll([&Id](const FHFFalseCeiling& C) { return C.RoomId == Id; });
		Cascaded += Working.Fixtures.RemoveAll([&Id](const FHFFixture& F) { return F.RoomId == Id; });
	}
	else if (Lower == TEXT("openings"))			{ Removed = RemoveById(Working.Openings); }
	else if (Lower == TEXT("beams"))			{ Removed = RemoveById(Working.Beams); }
	else if (Lower == TEXT("columns"))			{ Removed = RemoveById(Working.Columns); }
	else if (Lower == TEXT("falseceilings"))	{ Removed = RemoveById(Working.FalseCeilings); }
	else if (Lower == TEXT("fixtures"))			{ Removed = RemoveById(Working.Fixtures); }
	else
	{
		return FHFOperationResult::Fail(FString::Printf(TEXT("Unknown category '%s'."), *Category));
	}

	if (Removed == 0)
	{
		return FHFOperationResult::Fail(FString::Printf(
			TEXT("No %s with id '%s'."), *Category, *ElementId));
	}

	House->Modify();
	House->SetSpec(Working);

	return FHFOperationResult::Ok(Cascaded > 0
		? FString::Printf(TEXT("Deleted %s '%s' and %d dependent element(s)."), *Category, *ElementId, Cascaded)
		: FString::Printf(TEXT("Deleted %s '%s'."), *Category, *ElementId));
}

namespace
{
	/** Where captures are written. Under the plugin's Saved folder: user output, not plugin source. */
	FString CaptureDirectory()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("HouseForge"));
		const FString Base = Plugin.IsValid() ? Plugin->GetBaseDir() : FPaths::ProjectDir();
		return FPaths::Combine(Base, TEXT("Saved"), TEXT("Screenshots"));
	}

	FString CapturePath(const FString& FileName, const TCHAR* Fallback)
	{
		FString Name = FileName.IsEmpty() ? FString(Fallback) : FileName;
		if (!Name.EndsWith(TEXT(".png")))
		{
			Name += TEXT(".png");
		}

		// A name, not a path. Otherwise a tool call could write anywhere on the disk.
		Name = FPaths::GetCleanFilename(Name);

		return FPaths::Combine(CaptureDirectory(), Name);
	}

	/**
	 * Image size for a plan of a given world footprint, with the longest edge at Resolution.
	 *
	 * A flat is not square and neither is its drawing. Forcing a square image would put a 12 x 9 m
	 * plan in the middle of a frame that is a third empty, at two thirds of the pixels across the
	 * part that matters.
	 */
	FIntPoint PlanImageSize(double WorldWidth, double WorldHeight, int32 Resolution)
	{
		const double Ratio = (WorldWidth > 0.0) ? (WorldHeight / WorldWidth) : 1.0;

		if (Ratio <= 1.0)
		{
			return FIntPoint(Resolution, FMath::Max(64, FMath::RoundToInt(Resolution * Ratio)));
		}
		return FIntPoint(FMath::Max(64, FMath::RoundToInt(Resolution / Ratio)), Resolution);
	}
}

FHFOperationResult UHFEditorSubsystem::CaptureTopDown(const FString& FileName, int32 Resolution,
	double SectionHeight, FString& OutPath)
{
	OutPath.Reset();

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World == nullptr)
	{
		return FHFOperationResult::Fail(TEXT("There is no editor world to capture."));
	}

	const AHFHouseActor* House = FindHouseActor();
	if (House == nullptr)
	{
		return FHFOperationResult::Fail(TEXT("No HouseForge house in the current level."));
	}

	FString WhyNot;
	if (!FHFSceneCapture::CanRender(WhyNot))
	{
		return FHFOperationResult::Fail(FString::Printf(TEXT("Cannot capture: %s"), *WhyNot));
	}

	const double CutZ = (SectionHeight > 0.0) ? SectionHeight : FHFPlanSection::DefaultCutHeight();

	EnsureViewingLight();

	// The house is READ into a separate sectioned copy, never altered. See FHFPlanSection.
	FBox Bounds(ForceInit);
	TArray<AActor*> Section = FHFPlanSection::Build(World, House, CutZ, Bounds);

	ON_SCOPE_EXIT
	{
		FHFPlanSection::DestroyAll(World, Section);
	};

	if (Section.IsEmpty() || !Bounds.IsValid)
	{
		return FHFOperationResult::Fail(FString::Printf(
			TEXT("Nothing survives a section cut at %.0f cm. Every element of this house is above that ")
			TEXT("height, which usually means the house was built on a raised floor level - pass a section ")
			TEXT("height inside the walls instead."), CutZ));
	}

	// Framing. The padding and the world-span-to-image relationship are the ones worked out for the
	// old viewport capture and they were right; what has gone is the conversion into an editor
	// viewport's OrthoZoom, which existed only because a viewport expresses its framing that way.
	// A scene capture takes the world width directly, so the derivation that used to be needed -
	// units per pixel, ComputeOrthoZoomFactor, the viewport width cancelling - collapses into
	// OrthoWidth being the number itself.
	constexpr double Padding = 1.06;

	const FVector Centre = Bounds.GetCenter();
	const double WorldWidth = FMath::Max(Bounds.GetSize().X, 1.0) * Padding;
	const double WorldHeight = FMath::Max(Bounds.GetSize().Y, 1.0) * Padding;

	const int32 Longest = FMath::Clamp(Resolution <= 0 ? 2048 : Resolution, 256, 8192);
	const FIntPoint ImageSize = PlanImageSize(WorldWidth, WorldHeight, Longest);

	FHFCaptureRequest Request;
	Request.bOrthographic = true;
	Request.OrthoWidth = WorldWidth;

	// Straight down, from above everything in the section. Yaw -90 puts world +X across the image
	// and world +Y up it, which is the orientation the drawing set is laid out in - a plan that
	// disagrees with the drawing it is meant to be compared against is worse than no plan.
	Request.Rotation = FRotator(-90.0f, -90.0f, 0.0f);
	Request.Location = FVector(Centre.X, Centre.Y, Bounds.Max.Z + 500.0);

	Request.Width = ImageSize.X;
	Request.Height = ImageSize.Y;
	Request.ShowOnly = Section;

	// No sky. A plan is compared against a line drawing on white paper; a bright sky wrapped around
	// the flat only makes the comparison harder.
	Request.bShowSky = false;

	Request.OutputPath = CapturePath(FileName, TEXT("Plan"));

	FIntPoint Written = FIntPoint::ZeroValue;
	FString Error;
	if (!FHFSceneCapture::Render(World, Request, Written, Error))
	{
		return FHFOperationResult::Fail(FString::Printf(TEXT("Capture failed: %s"), *Error));
	}

	OutPath = Request.OutputPath;

	return FHFOperationResult::Ok(FString::Printf(
		TEXT("Captured a %dx%d plan, sectioned at %.0f cm, spanning %.0f x %.0f cm, to %s"),
		Written.X, Written.Y, CutZ, WorldWidth, WorldHeight, *OutPath));
}

FHFOperationResult UHFEditorSubsystem::CaptureView(const FString& FileName, int32 Resolution,
	FVector CameraLocation, FVector LookAt, double FieldOfViewDegrees, FString& OutPath)
{
	OutPath.Reset();

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World == nullptr)
	{
		return FHFOperationResult::Fail(TEXT("There is no editor world to capture."));
	}

	FString WhyNot;
	if (!FHFSceneCapture::CanRender(WhyNot))
	{
		return FHFOperationResult::Fail(FString::Printf(TEXT("Cannot capture: %s"), *WhyNot));
	}

	const FVector Direction = LookAt - CameraLocation;
	if (Direction.IsNearlyZero())
	{
		return FHFOperationResult::Fail(
			TEXT("The camera and its target are in the same place, so there is no direction to look in."));
	}

	EnsureViewingLight();

	FHFCaptureRequest Request;
	Request.bOrthographic = false;
	Request.Location = CameraLocation;
	Request.Rotation = Direction.Rotation();
	Request.FieldOfViewDegrees = (FieldOfViewDegrees > 0.0) ? FieldOfViewDegrees : 70.0;

	// 16:9. An interior is judged on what is beside you as much as on what is in front, and a
	// square frame of a 3 m room shows a great deal of floor and ceiling to get there.
	const int32 Longest = FMath::Clamp(Resolution <= 0 ? 1920 : Resolution, 256, 8192);
	Request.Width = Longest;
	Request.Height = FMath::Max(64, FMath::RoundToInt(Longest * 9.0 / 16.0));

	// The whole scene, uncut: this is a view of the flat as built, not a diagnostic drawing of it.
	Request.bShowSky = true;

	Request.OutputPath = CapturePath(FileName, TEXT("View"));

	FIntPoint Written = FIntPoint::ZeroValue;
	FString Error;
	if (!FHFSceneCapture::Render(World, Request, Written, Error))
	{
		return FHFOperationResult::Fail(FString::Printf(TEXT("Capture failed: %s"), *Error));
	}

	OutPath = Request.OutputPath;

	return FHFOperationResult::Ok(FString::Printf(
		TEXT("Captured a %dx%d view from (%.0f, %.0f, %.0f) looking at (%.0f, %.0f, %.0f) at %.0f degrees to %s"),
		Written.X, Written.Y, CameraLocation.X, CameraLocation.Y, CameraLocation.Z,
		LookAt.X, LookAt.Y, LookAt.Z, Request.FieldOfViewDegrees, *OutPath));
}

int32 UHFEditorSubsystem::EnsureViewingLight()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World == nullptr)
	{
		return 0;
	}

	return FHFViewingLight::EnsureIn(World).Num();
}

int32 UHFEditorSubsystem::RemoveViewingLight()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	return (World != nullptr) ? FHFViewingLight::RemoveFrom(World) : 0;
}

// ------------------------------------------------------------------------------- settings

void UHFEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// A settings page nothing listens to is a settings page that appears to do nothing. UDeveloperSettings
	// broadcasts this from its own PostEditChangeProperty, so an edit in Project Settings reaches the
	// open level immediately rather than waiting for somebody to rebuild the house by hand.
	if (UHFSettings* Settings = GetMutableDefault<UHFSettings>())
	{
		SettingsChangedHandle = Settings->OnSettingChanged().AddUObject(
			this, &UHFEditorSubsystem::HandleSettingsChanged);
	}
}

void UHFEditorSubsystem::Deinitialize()
{
	if (SettingsChangedHandle.IsValid())
	{
		if (UHFSettings* Settings = GetMutableDefault<UHFSettings>())
		{
			Settings->OnSettingChanged().Remove(SettingsChangedHandle);
		}
		SettingsChangedHandle.Reset();
	}

	Super::Deinitialize();
}

void UHFEditorSubsystem::HandleSettingsChanged(UObject* Settings, FPropertyChangedEvent& Event)
{
	ApplyProjectSettingsToLevel();
}

int32 UHFEditorSubsystem::ApplyProjectSettingsToLevel()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World == nullptr)
	{
		return 0;
	}

	int32 Rebuilt = 0;
	int32 Preserved = 0;

	// Every house in the level, not just the first. FindHouseActor answers "the house" for the tools,
	// which is the right answer there; a project-wide setting change is different - a level holding
	// two houses would otherwise leave the second one built to the old figures with nothing saying so.
	TArray<AActor*> Elements;
	for (TActorIterator<AHFHouseActor> It(World); It; ++It)
	{
		Elements.Append(It->ElementActors);
	}

	for (AActor* Element : Elements)
	{
		AHFElementActor* Typed = Cast<AHFElementActor>(Element);
		if (!IsValid(Typed))
		{
			continue;
		}

		// Asked before anything is touched. Regenerate would refuse on its own, but re-seeding the
		// construction figures first would still change what a later Revert To Generated produced -
		// so a hand-edited element is left alone completely, parameters included.
		if (Typed->ShouldPreserveOnRebuild())
		{
			++Preserved;
			continue;
		}

		// Only elements whose construction the settings actually feed. Re-seeding is the composing
		// layer's job and is done HERE, not inside any generator.
		if (AHFOpeningActor* Opening = Cast<AHFOpeningActor>(Typed))
		{
			Opening->ApplyProjectDefaults();
			Opening->Regenerate();
			++Rebuilt;
		}
	}

	if (Rebuilt > 0 || Preserved > 0)
	{
		UE_LOG(LogHouseForgeEditor, Log,
			TEXT("HouseForge settings applied: %d element(s) rebuilt, %d preserved as hand-edited."),
			Rebuilt, Preserved);
	}

	return Rebuilt;
}

#undef LOCTEXT_NAMESPACE
