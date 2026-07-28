// Copyright Siddartha G. All Rights Reserved.

#include "HFEditorSubsystem.h"

#include "Actors/HFHouseActor.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "EditorViewportClient.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HouseForgeEditor.h"
#include "ImageUtils.h"
#include "Interfaces/IPluginManager.h"
#include "LevelEditorViewport.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Model/HFSpecSerializer.h"
#include "Model/HFSpecValidator.h"
#include "UnrealEdGlobals.h"

#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "HouseForgeEditor"

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

	const FHFValidationResult Validation = FHFSpecValidator::Validate(Spec);
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

	const FHFValidationResult Validation = FHFSpecValidator::Validate(Spec);
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
	for (TActorIterator<AHFHouseActor> It(World); It; ++It)
	{
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

	const FHFValidationResult Validation = FHFSpecValidator::Validate(Working);
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

FHFOperationResult UHFEditorSubsystem::CaptureTopDown(const FString& FileName, int32 Resolution, FString& OutPath)
{
	OutPath.Reset();

	const AHFHouseActor* House = FindHouseActor();
	if (House == nullptr)
	{
		return FHFOperationResult::Fail(TEXT("No HouseForge house in the current level."));
	}

	FLevelEditorViewportClient* Viewport = nullptr;
	for (FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
	{
		if (Client && Client->IsPerspective() == false && Client->ViewportType == LVT_OrthoXY)
		{
			Viewport = Client;
			break;
		}
	}
	if (Viewport == nullptr)
	{
		// Fall back to the active perspective viewport and point it straight down; a headless run
		// has no ortho viewport at all.
		Viewport = GEditor->GetLevelViewportClients().Num() > 0 ? GEditor->GetLevelViewportClients()[0] : nullptr;
	}
	if (Viewport == nullptr || Viewport->Viewport == nullptr)
	{
		return FHFOperationResult::Fail(TEXT("No editor viewport is available to capture from."));
	}

	// Frame the whole house.
	FBox Bounds(ForceInit);
	for (const FHFWall& Wall : House->Spec.Walls)
	{
		Bounds += FVector(Wall.Start.X, Wall.Start.Y, 0.0);
		Bounds += FVector(Wall.End.X, Wall.End.Y, Wall.BaseZ + Wall.Height);
	}
	if (!Bounds.IsValid)
	{
		return FHFOperationResult::Fail(TEXT("The house has no walls to frame."));
	}

	const FVector Centre = Bounds.GetCenter();
	const double Extent = FMath::Max(Bounds.GetExtent().X, Bounds.GetExtent().Y);

	Viewport->SetViewportType(LVT_OrthoXY);
	Viewport->SetViewLocation(FVector(Centre.X, Centre.Y, Bounds.Max.Z + 1000.0));
	Viewport->SetViewRotation(FRotator(-90.0f, -90.0f, 0.0f));
	Viewport->SetOrthoZoom(FMath::Max(Extent * 2.4, 1000.0));
	Viewport->Invalidate();

	const FString Directory = FPaths::Combine(PluginDir(), TEXT("Saved"), TEXT("Screenshots"));
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Directory);

	FString Name = FileName.IsEmpty() ? TEXT("TopDown") : FileName;
	if (!Name.EndsWith(TEXT(".png")))
	{
		Name += TEXT(".png");
	}
	OutPath = FPaths::Combine(Directory, Name);

	const int32 Size = FMath::Clamp(Resolution <= 0 ? 2048 : Resolution, 256, 8192);

	FViewport* RenderTarget = Viewport->Viewport;
	TArray<FColor> Pixels;
	FIntRect Rect(0, 0, RenderTarget->GetSizeXY().X, RenderTarget->GetSizeXY().Y);
	if (!RenderTarget->ReadPixels(Pixels, FReadSurfaceDataFlags(), Rect))
	{
		return FHFOperationResult::Fail(TEXT("Could not read pixels from the viewport."));
	}

	for (FColor& Pixel : Pixels)
	{
		Pixel.A = 255;
	}

	FImageView Image(Pixels.GetData(), Rect.Width(), Rect.Height(), ERawImageFormat::BGRA8);
	if (!FImageUtils::SaveImageByExtension(*OutPath, Image))
	{
		return FHFOperationResult::Fail(FString::Printf(TEXT("Could not write '%s'."), *OutPath));
	}

	return FHFOperationResult::Ok(FString::Printf(
		TEXT("Captured %dx%d top-down view to %s"), Rect.Width(), Rect.Height(), *OutPath));
}

#undef LOCTEXT_NAMESPACE
