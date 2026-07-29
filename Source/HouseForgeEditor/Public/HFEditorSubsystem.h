// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Model/HFTypes.h"
#include "HFEditorSubsystem.generated.h"

class AHFHouseActor;

/** Outcome of an operation, shaped so it reads well when handed back through an MCP tool. */
USTRUCT(BlueprintType)
struct HOUSEFORGEEDITOR_API FHFOperationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HouseForge")
	bool bSuccess = false;

	/** Human and LLM readable. On failure this says what to change, not just that it failed. */
	UPROPERTY(BlueprintReadOnly, Category = "HouseForge")
	FString Message;

	static FHFOperationResult Ok(const FString& InMessage);
	static FHFOperationResult Fail(const FString& InMessage);
};

/**
 * The real HouseForge API.
 *
 * Every editor action and every MCP tool goes through here, so there is exactly one implementation
 * of each operation. UHFToolset is a thin typed wrapper over this class; it holds no logic of its
 * own, which keeps the MCP surface and the UI from drifting apart.
 *
 * Nothing here knows about the sample house. Houses are built from a spec that arrived from a
 * drawing, and there is no shortcut past that - see the SampleIsNotOnTheBuildPath test.
 */
UCLASS()
class HOUSEFORGEEDITOR_API UHFEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	// ------------------------------------------------------------------------ drawing intake

	/** Absolute path of Reference/Drawings, where imported drawings live. */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Drawings")
	FString GetDrawingsDirectory() const;

	/** Absolute path of Reference/Specs. */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Drawings")
	FString GetSpecsDirectory() const;

	/**
	 * Every readable drawing, newest set first, as paths relative to the drawings directory.
	 * This is how Claude discovers what it has been given.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Drawings")
	TArray<FString> ListDrawings() const;

	/**
	 * Copies drawings into Reference/Drawings/<SetName>, rasterising any PDF pages to PNG so they
	 * can be read as images.
	 *
	 * @param SourcePaths  Absolute paths to .png, .jpg, .jpeg or .pdf files.
	 * @param SetName      Subfolder to import into. Blank uses the first file's base name.
	 * @param OutImported  Paths of the readable images produced, relative to the drawings directory.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Drawings")
	FHFOperationResult ImportDrawings(const TArray<FString>& SourcePaths, const FString& SetName,
		TArray<FString>& OutImported);

	// ------------------------------------------------------------------------------- specs

	/** Validates spec JSON without building anything. Returns the full validation report. */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Spec")
	FHFOperationResult ValidateSpecJson(const FString& SpecJson) const;

	/**
	 * Builds a house from spec JSON in a new level.
	 *
	 * Refuses to build a spec with validation errors: half a house is worse than none, because the
	 * screenshot would look plausible while the spec that produced it is wrong.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Spec")
	FHFOperationResult ApplySpecJson(const FString& SpecJson, const FString& LevelName);

	/** The current level's house, serialised back to JSON. The read half of the round trip. */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Spec")
	FHFOperationResult GetSpecJson(FString& OutSpecJson) const;

	/** Writes the current level's house to Reference/Specs/<Name>.json. */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Spec")
	FHFOperationResult SaveSpecToFile(const FString& FileName);

	// ---------------------------------------------------------------------------- elements

	/**
	 * A compact inventory of what is in the level - counts by category and every element id.
	 * Cheaper for Claude to read than the whole spec when it only needs to know what exists.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Elements")
	FHFOperationResult ListElements(const FString& CategoryFilter, FString& OutSummary) const;

	/**
	 * Patches one element in place from a JSON object of the fields to change.
	 *
	 * Re-validates afterwards and rolls the change back if it would break the spec, so a bad edit
	 * cannot leave the level in a state the builder would choke on.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Elements")
	FHFOperationResult ModifyElement(const FString& Category, const FString& ElementId,
		const FString& PropertiesJson);

	/** Removes an element, and anything that depends on it, by id. */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Elements")
	FHFOperationResult DeleteElement(const FString& Category, const FString& ElementId);

	// ------------------------------------------------------------------------------ viewport

	/**
	 * Top-down orthographic capture framed on the house, written under Saved/Screenshots.
	 *
	 * This closes the loop. Without it Claude is building blind and cannot tell whether what it
	 * read matches what it built.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Viewport")
	FHFOperationResult CaptureTopDown(const FString& FileName, int32 Resolution, FString& OutPath);

	/** The house actor in the current level, or nullptr. */
	AHFHouseActor* FindHouseActor() const;

	// ------------------------------------------------------------------------------ settings

	/**
	 * Re-seeds every element in the level from Project Settings and rebuilds it.
	 *
	 * What makes the settings page do something visible: change the leaf thickness and the doors in
	 * the open level get thicker, rather than the change waiting for the next full rebuild.
	 *
	 * Hand-edited elements are skipped entirely - not re-seeded and not rebuilt. An artist who has
	 * modelled a door does not expect a project-wide setting to reach in and overwrite it, and
	 * .claude/rules/04-conventions.md calls that loss silent and unrecoverable. Reverting such an
	 * element picks the new figures up in the ordinary way.
	 *
	 * @return How many elements were rebuilt. Elements preserved as hand-edited are not counted.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "HouseForge|Settings")
	int32 ApplyProjectSettingsToLevel();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	/** Bound to UHFSettings::OnSettingChanged so an edit on the settings page reaches the level. */
	void HandleSettingsChanged(UObject* Settings, struct FPropertyChangedEvent& Event);

	FDelegateHandle SettingsChangedHandle;

	/** Applies a validated spec into the current level, replacing any existing house. */
	FHFOperationResult SpawnHouse(const FHFHouseSpec& Spec);

	/** Rasterises a PDF to one PNG per page via the Scripts virtual environment. */
	bool RasterisePdf(const FString& PdfPath, const FString& DestinationDir,
		TArray<FString>& OutPngPaths, FString& OutError) const;

	static FString PluginDir();
};
