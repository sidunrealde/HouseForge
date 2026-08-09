// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Materials/HFMaterialLibrary.h"
#include "Materials/HFSurfaceFinish.h"
#include "Model/HFTypes.h"
#include "HFEditorSubsystem.generated.h"

class AHFHouseActor;
class UHFMaterialLibrary;

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
 * HOW MUCH OF THE OPEN LEVEL ONE SURFACE ROLE ACTUALLY COVERS.
 *
 * The answer to "will changing this do anything I can see", which is the first question anyone
 * has in front of a list of eighteen finishes and the one a list of eighteen names cannot answer.
 * Measured off the built meshes rather than off the spec, because the material slot a triangle
 * renders through is a property of the mesh: a hand-edited element that had its polygroups
 * re-cut counts for what it now is, not for what it was generated as.
 *
 * Every role is reported, including the ones covering nothing. A role absent from the level is a
 * fact worth showing - it is why a finish edit looks inert - and dropping the row would make the
 * list change length as houses come and go.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGEEDITOR_API FHFSurfaceUsage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HouseForge")
	EHFSurfaceRole Role = EHFSurfaceRole::WallPaint;

	/** Element actors with at least one triangle rendering through this role. */
	UPROPERTY(BlueprintReadOnly, Category = "HouseForge")
	int32 ElementCount = 0;

	/** Triangles rendering through this role, across every element. */
	UPROPERTY(BlueprintReadOnly, Category = "HouseForge")
	int32 TriangleCount = 0;

	/**
	 * Surface area carrying this role, in square metres.
	 *
	 * The figure that actually says how much of a render a finish decides. A triangle count says a
	 * knob has more of them than a wall does; 244 m2 of wall paint against 0.4 m2 of knob chrome
	 * says which one is worth ten minutes.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "HouseForge")
	double AreaSquareMetres = 0.0;

	/**
	 * How many of those elements are hand-edited.
	 *
	 * Shown rather than warned about, because the honest answer is reassuring: re-materialling is
	 * entirely component-side and asset-side, so a hand-edited element takes a finish change like
	 * any other and keeps its modelling. The count is there so nobody has to take that on trust.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "HouseForge")
	int32 ArtistEditedElementCount = 0;
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

	// ---------------------------------------------------------------------------------- bake
	//
	// The bake exists as a switch on every element actor, but until this section there was no way to
	// reach it from anywhere except C++ and the details panel - and HouseForge is driven over MCP by
	// a model, which has neither. A feature that cannot be invoked from the route the plugin is
	// actually used through is not a feature.

	/**
	 * Bakes or unbakes every element of every house in the level.
	 *
	 * BAKING IS A RENDERING CHOICE AND IT IS REVERSIBLE - the dynamic meshes are kept, hidden, and
	 * restored exactly by the unbake. What it changes is whether Lumen can see the flat at all: a
	 * UDynamicMeshComponent gets no mesh cards and so no surface cache, and an unbaked interior is
	 * lit by unoccluded sky rather than by its own fixtures.
	 *
	 * @param bBaked    True to bake, false to switch every element back to its live mesh.
	 * @param OutReport What was baked, skipped or failed, element by element.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Bake")
	FHFOperationResult SetHouseRenderMode(bool bBaked, FString& OutReport);

	/**
	 * Re-bakes every baked element whose geometry has changed since it was baked.
	 *
	 * The one case bAutoRebakeOnRegenerate does not cover: an element baked, then edited with
	 * auto-rebake switched off, is drawing geometry the spec no longer describes.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Bake")
	FHFOperationResult RebakeStale(FString& OutReport);

	/**
	 * Whether the flat is in the Lumen scene, and what is missing if it is not.
	 *
	 * The same check the capture path refuses on, exposed so it can be asked BEFORE spending the
	 * time on a render - and so that "is this render trustworthy" has an answer that is not a
	 * judgement of the image.
	 *
	 * @param OutReport The full account: counts, area share, the absent elements and the remedy.
	 * @return          Ok when the flat is covered; Fail, with the report, when it is not.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Bake")
	FHFOperationResult CheckLumenCoverage(FString& OutReport) const;

	// ------------------------------------------------------------------------------ surfaces
	//
	// The read and write halves of "what every surface role is made of". The material panel is a
	// view onto these and holds no logic of its own, so the panel and the MCP surface cannot come
	// to different conclusions about what changing a finish means - which is the whole reason the
	// panel was not allowed to reach into UHFMaterialLibrary directly.
	//
	// NOTHING HERE CAN REACH A VERTEX. Every write below lands on a material instance or on the
	// library asset. No mesh is read, no Regenerate is called, and bArtistEdited is never
	// consulted or set, so re-materialling cannot destroy a hand edit and cannot renumber the
	// surface-role polygroups the entire assignment mechanism is built on. Asserted by
	// HouseForge.Editor.Surfaces.SettingAFinishDoesNotTouchGeometry.

	/**
	 * The library this project is building with. Never null - see UHFMaterialLibrary::Get.
	 *
	 * May be the class default object when the plugin's content is missing. That one is readable
	 * and renders correctly but cannot be edited or saved, which is why the write path asks for
	 * an editable library separately rather than letting an edit land on the CDO and vanish.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Surfaces")
	UHFMaterialLibrary* GetMaterialLibrary() const;

	/**
	 * The library, refused when it is the compiled-in defaults rather than an asset.
	 *
	 * A separate call because "you can look at this" and "you can change this" are different
	 * answers here, and the failure message names the asset that is missing. Editing the CDO
	 * would appear to work, render correctly for the session, and be gone on restart with
	 * nothing having said so.
	 */
	FHFOperationResult GetEditableMaterialLibrary(UHFMaterialLibrary*& OutLibrary) const;

	/** The finish for one role: this library's entry, or the shipped default where it has none. */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Surfaces")
	FHFOperationResult GetSurfaceFinish(EHFSurfaceRole Role, FHFSurfaceFinish& OutFinish) const;

	/**
	 * Writes one role's finish into the library and pushes it to the renderer.
	 *
	 * Interactive is the mid-gesture tier: numeric values only, straight to the render thread, no
	 * transaction and no dirty package. Commit is the decision: the whole finish including texture
	 * slots and static switches, inside a transaction so it undoes, with the library asset marked
	 * dirty so it is savable. Both tiers write the same finish into the library, so a drag
	 * abandoned by clicking elsewhere still leaves the library saying what the viewport shows.
	 *
	 * Every element using the role updates at once and none of them is visited: the material
	 * instance is a shared asset, so one write re-renders all 155 elements of the reference flat.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Surfaces")
	FHFOperationResult SetSurfaceFinish(EHFSurfaceRole Role, const FHFSurfaceFinish& Finish,
		EHFMaterialPush Mode);

	/** Puts one role back to the finish the plugin ships, and pushes it. Undoable. */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Surfaces")
	FHFOperationResult ResetSurfaceFinish(EHFSurfaceRole Role);

	/**
	 * How much of the open level each role covers, one entry per role, in enum order.
	 *
	 * Empty of counts rather than empty of rows when there is no house: a level with nothing in it
	 * still has eighteen roles and every finish is still editable, because finishes are assets and
	 * not level state.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Surfaces")
	TArray<FHFSurfaceUsage> GetSurfaceUsage() const;

	/**
	 * Re-fills every HouseForge component's material slots from the library.
	 *
	 * The recovery path, not part of an edit. Slot assignment already happens at generation, so
	 * this is for the cases where it could not have: a library swapped under a level that was
	 * built with another one, or an element hand-edited into carrying a role it did not before.
	 * Component-side only - ConfigureMaterialSet sets slots and never touches FDynamicMesh3.
	 *
	 * @param OutComponents  How many components were re-filled.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Surfaces")
	FHFOperationResult ReapplyMaterialsToLevel(int32& OutComponents);

	/**
	 * Writes the library and the material instances it has pushed to, so the change survives a restart.
	 *
	 * TWO SETS OF PACKAGES, not one. The library asset is the record and the MI_HF_* instances are
	 * that record compiled for the renderer, and they are separate assets - saving only the library
	 * would come back from a restart looking correct in the details panel and rendering the old
	 * finish, which is the most confusing possible half-failure.
	 *
	 * @param OutPackagesSaved  How many packages were written. Zero when nothing was unsaved.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Surfaces")
	FHFOperationResult SaveSurfaceLibrary(int32& OutPackagesSaved);

	/** Whether anything in the library or its instances is unsaved. Drives the panel's Save button. */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Surfaces")
	bool HasUnsavedSurfaceChanges() const;

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
	/**
	 * What each role's finish was before the drag currently in progress on it started.
	 *
	 * THE UNDO BUFFER CANNOT SEE THE START OF A GESTURE WITHOUT THIS. Interactive pushes deliberately
	 * open no transaction - one per mouse-move would fill the buffer with a hundred steps nobody
	 * wants to walk back through - but they DO write the library, so by the time the commit on mouse-
	 * up calls Modify(), the object it snapshots already holds the dragged value. Undo then restored
	 * the value the user had just dragged to, which is to say it did nothing at all. Typing a number
	 * undid correctly and dragging one did not, which is a worse bug than neither working.
	 *
	 * Filled on the FIRST interactive push of a gesture and consumed by the commit that ends it,
	 * which puts the pre-drag value back into the library for the instant Modify() looks at it.
	 */
	TMap<EHFSurfaceRole, FHFSurfaceFinish> PreGestureFinishes;

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
