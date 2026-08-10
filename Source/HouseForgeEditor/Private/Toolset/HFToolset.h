// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "HFToolset.generated.h"

/**
 * HouseForge tools for Claude.
 *
 * Turns AutoCAD interior drawings of 2BHK and 3BHK flats into editable Unreal levels. The workflow
 * is: list the drawings, read the images, write a House Spec describing what they show, validate
 * it, apply it, then capture a top-down view and compare it against the drawing.
 *
 * Never invent a spec. Every wall, room and fixture must come from something visible in a drawing.
 * The schema is documented in the plugin's Docs/HouseSpecSchema.md.
 *
 * Registered with the ToolsetRegistry, which the engine's Model Context Protocol plugin surfaces
 * automatically. Every function here is a thin wrapper over UHFEditorSubsystem, which holds the
 * actual implementation - the editor UI calls exactly the same code.
 */
UCLASS(BlueprintType, Hidden)
class UHFToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Lists the drawings available to read, as paths relative to the drawings folder.
	 * Start here: these are the images to open before writing any spec.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge")
	static FString ListDrawings();

	/**
	 * Returns the absolute path of the drawings folder, so the images can be opened and read.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge")
	static FString GetDrawingsPath();

	/**
	 * Imports drawings into the plugin so they can be read. PDFs are rasterised to one PNG per page.
	 * @param SourcePaths Semicolon separated absolute paths to .png, .jpg or .pdf files.
	 * @param SetName Folder to import into. Leave blank to name it after the first file.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge")
	static FString ImportDrawings(const FString& SourcePaths, const FString& SetName);

	/**
	 * Converts a dimension written the way it appears on a drawing into centimetres.
	 *
	 * Handles imperial forms that are easy to get wrong by hand - 12'-6", 12' 6", 78" - and metric
	 * with or without a suffix: 3600, 3600mm, 3.6m. Use it rather than converting mentally.
	 * @param Text The dimension exactly as written on the drawing.
	 * @param DefaultUnits Units for a bare number with no suffix: Millimeters, Centimeters, Meters, Feet or Inches.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge")
	static FString ConvertLength(const FString& Text, const FString& DefaultUnits);

	/**
	 * Checks a House Spec without building anything, returning every problem in one pass.
	 * Always validate before applying: the report names the exact rule and the offending numbers.
	 * @param SpecJson The House Spec as JSON.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge")
	static FString ValidateSpec(const FString& SpecJson);

	/**
	 * Builds a house from a House Spec. Refuses specs with validation errors.
	 * @param SpecJson The House Spec as JSON.
	 * @param LevelName Name for a new level. Leave blank to build into the level already open.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge")
	static FString ApplySpec(const FString& SpecJson, const FString& LevelName);

	/**
	 * Returns the House Spec of the level's current house as JSON. Use this to read back what was
	 * built before changing it.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge")
	static FString GetSpec();

	/**
	 * Lists what is in the level: counts and element ids per category. Cheaper to read than the
	 * whole spec when you only need to know what exists.
	 * @param Category One of walls, openings, beams, columns, rooms, falseCeilings, fixtures. Blank for all.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge")
	static FString ListElements(const FString& Category);

	/**
	 * Changes fields on one element. Rejected and rolled back if the change would break the spec.
	 * @param Category One of walls, openings, beams, columns, rooms, falseCeilings, fixtures.
	 * @param ElementId The element's id.
	 * @param PropertiesJson JSON object holding only the fields to change.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge")
	static FString ModifyElement(const FString& Category, const FString& ElementId, const FString& PropertiesJson);

	/**
	 * Removes an element, along with anything that depended on it.
	 * @param Category One of walls, openings, beams, columns, rooms, falseCeilings, fixtures.
	 * @param ElementId The element's id.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge")
	static FString DeleteElement(const FString& Category, const FString& ElementId);

	/**
	 * Saves the level's house to a spec file under the plugin's Reference/Specs folder.
	 * @param FileName File name without a path.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge")
	static FString SaveSpec(const FString& FileName);

	/**
	 * Draws a PLAN of the house and returns the image path: the flat cut through horizontally and
	 * viewed orthographically from above, the way an architectural plan is drawn.
	 *
	 * This is the tool to compare against the source drawing. It is a section, not a view of the
	 * roof, so walls read as walls, doorways as gaps and rooms as rooms. The house is not modified.
	 * Works with the editor window minimised or covered.
	 *
	 * ORIENTATION: world +X runs right across the image and world +Y runs DOWN it. The drawing
	 * sheets under Reference/Drawings are laid out with +Y UP, so a captured plan and a drawing
	 * sheet of the same flat are mirrored vertically. Compare accordingly - a room that is
	 * top-left on the sheet is bottom-left here.
	 * @param FileName Output file name.
	 * @param Resolution Longest edge in pixels. The short edge follows the plan's proportions.
	 * @param SectionHeightCm Height of the cut. Leave at 0 for 120 cm, which is where plans are cut.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge", meta = (ClampMin = "256", ClampMax = "8192"))
	static FString CaptureTopDown(const FString& FileName, int32 Resolution, float SectionHeightCm);

	/**
	 * Renders a perspective view from a point, looking at another point - an interior of one room.
	 *
	 * Use this to judge a room rather than the layout: whether a wardrobe is the right height, what
	 * a doorway lines up with, whether a ceiling reads. Coordinates are world centimetres, the same
	 * ones the House Spec uses after ingest. Standing eye height is about 160.
	 * @param FileName Output file name.
	 * @param Resolution Longest edge in pixels. The image is 16:9.
	 * @param CameraX Camera position X, in centimetres.
	 * @param CameraY Camera position Y, in centimetres.
	 * @param CameraZ Camera position Z, in centimetres.
	 * @param TargetX What to look at, X.
	 * @param TargetY What to look at, Y.
	 * @param TargetZ What to look at, Z.
	 * @param FieldOfViewDegrees Horizontal field of view. Leave at 0 for 70, a wide interior lens.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge", meta = (ClampMin = "256", ClampMax = "8192"))
	static FString CaptureView(const FString& FileName, int32 Resolution,
		float CameraX, float CameraY, float CameraZ,
		float TargetX, float TargetY, float TargetZ,
		float FieldOfViewDegrees);

	/**
	 * Bakes the house to static meshes, or switches it back. REQUIRED BEFORE ANY LIT RENDER.
	 *
	 * Lumen cannot see the dynamic meshes HouseForge generates: they get no mesh cards, so no
	 * surface cache, so no bounce light - on software AND hardware tracing. An unbaked interior is
	 * lit by sky flooding straight through its own walls, which renders BRIGHTER than the correct
	 * result and looks perfectly cheerful. CaptureView refuses to draw an unbaked flat for that
	 * reason.
	 *
	 * The bake is reversible and non-destructive: the live meshes are kept and hidden, articulation
	 * is preserved part by part, and unbaking restores them exactly. It writes one static mesh asset
	 * per part into the project's Content folder, so it is not free - bake when you are ready to
	 * look at the flat, not after every edit.
	 * @param Baked True to bake, false to switch back to the live editable meshes.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge")
	static FString SetHouseRenderMode(bool Baked);

	/**
	 * Reports whether the flat is in the Lumen scene, so a render can be trusted before it is taken.
	 *
	 * Ask this rather than judging an image: the failure mode is a BRIGHTER, more attractive picture,
	 * not a dark or obviously broken one. Names the elements that are absent and why.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge")
	static FString CheckLumenCoverage();

	/**
	 * Lists baked static meshes in this level's folder that no element claims any more.
	 *
	 * The bake deliberately never deletes an asset - a regeneration that drops a wardrobe drawer, an
	 * element the revised spec removed, a level rebuilt - so they accumulate, and this is how they are
	 * seen. Nothing is deleted; DeleteBakedOrphans is a separate, deliberate second call.
	 *
	 * Scoped to the OPEN level by a provenance stamp. Assets belonging to another level, or to nobody,
	 * are never listed however unreferenced they look from here.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge")
	static FString FindBakedOrphans();

	/** Deletes what FindBakedOrphans lists. NOT UNDOABLE - list first and read the list. */
	UFUNCTION(meta = (AICallable), Category = "HouseForge")
	static FString DeleteBakedOrphans();

	/**
	 * Takes edits made to a BAKED ASSET back into the element's live mesh.
	 *
	 * While an element is baked, Unreal's Modeling Tools edit the BAKED STATIC MESH rather than the
	 * live one - that is measured, and it is the correct thing for the engine to do, because the live
	 * mesh is deliberately not editable while it is hidden. A re-bake therefore REFUSES to overwrite an
	 * asset that has changed since it was baked, and reports which element.
	 *
	 * This is the way out of that: the asset's geometry becomes the element's live mesh, the element is
	 * marked hand-edited and stops regenerating, and baking again writes the sculpted form back.
	 *
	 * @param ElementIds Comma-separated element ids. LEAVE EMPTY for every affected element.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge")
	static FString AdoptBakedAssetEdits(const FString& ElementIds);

	// ------------------------------------------------------------------ content browser assets
	//
	// The replacement pass, wrapped so Claude reaches exactly the code the ASSETS panel section
	// reaches. The panel is a view onto UHFEditorSubsystem and holds no logic of its own; if these
	// wrappers did not exist, the panel's flagship feature would be the one thing Claude could not
	// do, and the two surfaces would drift from the day the panel was written.

	/**
	 * Lists what is in the level by fixture type, with how many of each and how many are swapped.
	 *
	 * Ask this first. The type names are the ones ReplaceFixtureType takes, and the counts are what
	 * says whether a swap did anything.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge")
	static FString ListFixtureTypes();

	/**
	 * Replaces every generated fixture of one type with a Content Browser static mesh.
	 *
	 * NON-DESTRUCTIVE AND REVERSIBLE. The generated mesh is kept and hidden, its parameters are
	 * untouched, and RevertFixturesToGenerated restores it exactly. The asset is fitted into the box
	 * the generated fixture occupied, so it lands where the drawing put it whatever pivot its author
	 * used.
	 *
	 * Fixtures somebody swapped individually are deliberately left alone.
	 *
	 * @param FixtureType One of the names ListFixtureTypes reports, e.g. Wardrobe, Sofa, WC.
	 * @param AssetPath   Full object path of a UStaticMesh, e.g. /Game/Furniture/SM_Wardrobe.SM_Wardrobe.
	 * @param FitMode     KeepAssetSize, UniformFit, StretchToFootprint or FitPlanKeepHeight.
	 *                    UniformFit keeps the asset's proportions; StretchToFootprint fills the drawn
	 *                    box exactly and is right for built-in joinery.
	 * @param YawDegrees  Correction for an asset authored facing another way. Usually 0, 90, 180, 270.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge")
	static FString ReplaceFixtureType(const FString& FixtureType, const FString& AssetPath,
		const FString& FitMode, float YawDegrees);

	/**
	 * What an asset would do to one fixture, without changing anything.
	 *
	 * An asset will never match the drawing exactly. This reports the generated size, the size the
	 * asset would land at, how far it is being stretched and how much slack is left - so a bad fit is
	 * something to see rather than something to discover in a render.
	 * @param ElementId The fixture's element id, from ListElements or ListFixtureTypes.
	 * @param AssetPath Full object path of a UStaticMesh.
	 * @param FitMode   KeepAssetSize, UniformFit, StretchToFootprint or FitPlanKeepHeight.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge")
	static FString PreviewFixtureAsset(const FString& ElementId, const FString& AssetPath,
		const FString& FitMode);

	/**
	 * THE WAY BACK. Puts generated geometry back, exactly as it was.
	 *
	 * The parameter structs were never discarded, so this is a switch rather than a regeneration.
	 * @param ElementIds Comma-separated element ids. LEAVE EMPTY to revert the whole level.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge")
	static FString RevertFixturesToGenerated(const FString& ElementIds);

	/**
	 * Applies the project's asset mapping table across the level now.
	 *
	 * Every build already ends with this, so it is only needed when the table has been edited while a
	 * house is standing.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge")
	static FString ApplyAssetMappingTable();
};
