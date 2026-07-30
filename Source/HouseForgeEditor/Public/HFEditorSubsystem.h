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

	// ------------------------------------------------------------------------------- capture

	/**
	 * A PLAN of the house: an orthographic view of a horizontal section, written under
	 * Saved/Screenshots.
	 *
	 * This closes the loop. Without it Claude is building blind and cannot tell whether what it
	 * read matches what it built - and for a long time it could not tell, because the top-down
	 * view this replaces showed the top of the ceilings. A featureless slab is not a plan, and
	 * comparing one against a drawing tells you nothing about the house underneath it.
	 *
	 * So the house is cut through at SectionHeight the way a real plan is cut, and the section is
	 * what gets rendered. The house itself is never modified - a sectioned copy is built, captured
	 * and thrown away.
	 *
	 * Renders offscreen. No editor viewport is used, borrowed or read from, so this works with the
	 * editor window minimised, covered, or on another desktop.
	 *
	 * @param FileName       Name of the PNG. Any path on it is ignored.
	 * @param Resolution     Longest edge in pixels. The other edge follows the plan's proportions.
	 * @param SectionHeight  Height of the cut in centimetres. Zero or less takes the 120 cm a plan
	 *                       is conventionally cut at.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Capture")
	FHFOperationResult CaptureTopDown(const FString& FileName, int32 Resolution, double SectionHeight,
		FString& OutPath);

	/**
	 * A perspective view from anywhere, looking at anything - an interior of one room, rather than
	 * a drawing of the whole flat.
	 *
	 * The other half of being able to see the flat. A plan says whether the layout is right; only
	 * a view from inside a room says whether the room is. Renders offscreen, like the plan, and
	 * against the same placeholder lighting.
	 *
	 * @param CameraLocation      Where the camera is, in world centimetres. Eye height is about 160.
	 * @param LookAt              What it points at, in world centimetres.
	 * @param FieldOfViewDegrees  Horizontal field of view. Zero or less takes 70, which is close to
	 *                            a 24 mm lens and is what interiors are normally shot on.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Capture")
	FHFOperationResult CaptureView(const FString& FileName, int32 Resolution, FVector CameraLocation,
		FVector LookAt, double FieldOfViewDegrees, FString& OutPath);

	/**
	 * Makes sure the level has the placeholder viewing light, and returns how many actors it has.
	 *
	 * Idempotent, and called by both captures - there are no materials and no lighting milestone
	 * yet, so without it a render comes back black and says nothing about the geometry in it. The
	 * rig is scaffolding and is labelled as such in the outliner; milestone 11 replaces it.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "HouseForge|Capture")
	int32 EnsureViewingLight();

	/** Deletes the placeholder viewing light. Returns how many actors went. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "HouseForge|Capture")
	int32 RemoveViewingLight();

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
