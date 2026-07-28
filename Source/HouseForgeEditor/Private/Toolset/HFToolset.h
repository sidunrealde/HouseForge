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
	 * Captures a top-down orthographic view of the house and returns the image path.
	 * Compare this against the source drawing to check what was built matches what was read.
	 * @param FileName Output file name.
	 * @param Resolution Longest edge in pixels.
	 */
	UFUNCTION(meta = (AICallable), Category = "HouseForge", meta = (ClampMin = "256", ClampMax = "8192"))
	static FString CaptureTopDown(const FString& FileName, int32 Resolution);
};
