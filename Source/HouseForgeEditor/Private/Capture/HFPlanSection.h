// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class AHFHouseActor;
class UWorld;

/**
 * The throwaway geometry a plan is rendered from.
 *
 * FHFSectionCut does the cut; this is the composing layer that resolves a house into meshes, hands
 * each one to that pure generator, and puts the results somewhere a camera can see them. Nothing
 * here decides what a section is - it only decides which meshes to cut, at what height, and with
 * what role on the cut faces.
 *
 * The section is built into SEPARATE, TRANSIENT actors rather than by modifying the house.
 * Sectioning the real geometry, capturing, and putting it back would be a mesh rewrite on every
 * element in the flat, twice per screenshot - and any failure part way through would leave a
 * decapitated house saved into the user's level. It also breaks the artist-edit rule from the far
 * side: rewriting a hand-edited shutter's mesh to draw a picture is exactly the silent,
 * unrecoverable loss .claude/rules/04-conventions.md forbids. The house is never touched; it is
 * read.
 *
 * The section actors are shown to the capture through its show-only list, so the uncut house does
 * not have to be hidden and the two are never both in frame.
 */
class FHFPlanSection
{
public:
	/**
	 * The height a plan is cut at, in centimetres above the house's floor level.
	 *
	 * 1.2 m is the convention, and it is a convention for a reason: it is above every windowsill
	 * and counter and below every door head, so a plan cut there shows window openings as openings
	 * and doorways as gaps.
	 */
	static double DefaultCutHeight() { return 120.0; }

	/**
	 * Camera rotation for a plan: straight down, with world +X running across the image.
	 *
	 * Which leaves world +Y running DOWN the image, and that is not a choice - it is forced. A
	 * camera's screen axes satisfy Forward x Right = Up, so a camera looking down (-Z) with its
	 * right vector on +X has an up vector of -Y. The only alternative a rotation can reach is +Y up
	 * with +X running LEFT, which is a horizontal mirror instead of a vertical one.
	 *
	 * The drawing sheets in Reference/Drawings are laid out the other way: gen_sample_drawings.py
	 * maps plan millimetres to the sheet "flipping Y so north is up". So a captured plan and a
	 * drawing sheet of the same flat are mirror images of each other, and comparing them means
	 * knowing that. The capture says so in its own result message rather than quietly flipping the
	 * image - flipping would make the plan agree with the sheet while disagreeing with every
	 * perspective view of the same level, which is a worse lie than an honest mirror.
	 *
	 * Pinned as a named function and asserted in HouseForge.Capture.APlanIsOrientedTheWayItSays,
	 * because it is exactly the sort of fact that gets "fixed" by someone who has not measured it.
	 */
	static FRotator PlanCameraRotation() { return FRotator(-90.0f, -90.0f, 0.0f); }

	/**
	 * Builds the sectioned copy of a house.
	 *
	 * @param CutZ       World height of the cut plane, in centimetres.
	 * @param OutBounds  World bounds of the geometry produced - what a plan camera should frame.
	 *                   Taken from the SECTION rather than from the spec, so the frame is around
	 *                   what will actually be in the picture.
	 * @return The transient actors holding the section. The caller must destroy them; DestroyAll
	 *         is there for that and is safe to call on a partly built result.
	 */
	static TArray<AActor*> Build(UWorld* World, const AHFHouseActor* House, double CutZ, FBox& OutBounds);

	/** Destroys actors returned by Build. */
	static void DestroyAll(UWorld* World, TArray<AActor*>& Actors);
};
