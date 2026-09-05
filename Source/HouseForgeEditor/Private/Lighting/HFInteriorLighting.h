// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;
struct FPostProcessSettings;

/**
 * The sky the flat sits under, and the exposure it is read at.
 *
 * THE THING FHFViewingLight SAID IT WAS NOT. That class is scaffolding and its own header says so
 * in the first paragraph: "What it deliberately is NOT: the lighting milestone... the whole rig is
 * meant to be deleted in one call when the real thing lands - which is what RemoveFrom is for."
 * This is the real thing, and EnsureIn does exactly that deletion on its way in.
 *
 * ## What is different from the placeholder, and why
 *
 * Three things, and all three are consequences of the flat now having lights of its own. Before
 * this milestone the only illumination anywhere was the sun through the openings, so the
 * placeholder had to cheat to stop the back of the flat going black. It no longer has to.
 *
 *  1. NO AMBIENT CUBEMAP. The placeholder lights its interiors with one, because a
 *     USceneCaptureComponent2D runs no global illumination and a sky light delivers its diffuse
 *     THROUGH global illumination - measured, in that file: driving the sky light across five
 *     orders of magnitude changed a captured interior by not one pixel value. That is a fact about
 *     scene captures, not about the flat, so it stays where it belongs - FHFSceneCapture applies
 *     the placeholder's settings to its own capture components regardless of which rig is in the
 *     level, and is unaffected by anything here. A viewport and a PIE session do have Lumen, and
 *     an ambient cubemap on top of Lumen is a second, flat, unoccluded ambient term that washes
 *     out precisely the contact shadowing the fittings are there to produce.
 *
 *  2. THE SKY LIGHT CASTS SHADOWS. The placeholder switches that off, and says why: with no ray
 *     tracing guaranteed, occluded ambient left the rooms at the back of the flat black, and a
 *     black room is indistinguishable from a room that failed to generate. A room at the back of
 *     the flat now has a light fitting in it, so the failure that argument was avoiding cannot
 *     happen, and an unoccluded sky light indoors is simply wrong: it puts skylight on the inside
 *     face of every wall in the dwelling.
 *
 *  3. LUMEN IS ASKED FOR EXPLICITLY, on the volume, rather than left to whatever the project
 *     scalability settings happen to be. An interior lit by fittings is almost entirely lit by
 *     bounce; with GI off it is lit by four bright pools and black everywhere else.
 *
 * ## Exposure
 *
 * Manual, pinned through the physical camera, at InteriorEV100 - not eye adaptation. Deterministic
 * exposure is not a preference here: this repository captures the flat and MEASURES the images, and
 * a histogram that moves because the camera adapted is a histogram that says nothing. The dynamic
 * range problem that auto exposure would have solved - a window six stops brighter than the wall
 * beside it - is given to local exposure instead, which is the tool for it and which does not
 * change what a fixed camera reports.
 *
 * ## Idempotent by construction
 *
 * EnsureIn finds the rig before it spawns one, so calling it on every build leaves exactly one of
 * each actor in the level. Every actor carries Tag(), and membership of the rig IS that tag.
 */
class FHFInteriorLighting
{
public:
	/** The tag every actor of the rig carries. Membership of the rig IS this tag. */
	static const FName& Tag();

	/** Outliner folder the rig is filed under. */
	static FName OutlinerFolder() { return FName(TEXT("HouseForge/Lighting")); }

	/** The rig's actors in a world, in no particular order. Empty when there is no rig. */
	static TArray<AActor*> FindIn(UWorld* World);

	/**
	 * Makes sure the world has exactly one lighting rig, and no placeholder rig.
	 *
	 * @param bOutSpawned Optional: true when a rig was created by this call, false when one was
	 *                    already there. Nothing else can tell those apart afterwards.
	 * @return The rig's actors.
	 */
	static TArray<AActor*> EnsureIn(UWorld* World, bool* bOutSpawned = nullptr);

	/** Deletes the rig. Returns how many actors went. */
	static int32 RemoveFrom(UWorld* World);

	/**
	 * Illuminance of the sun, in lux. A bright overcast day rather than direct noon.
	 *
	 * The same figure the placeholder rig arrived at, kept deliberately rather than re-chosen: it
	 * was reasoned about once, in FHFViewingLight, against renders of this flat. Direct noon sun is
	 * ten times this and blows every room with a window in it while leaving the rest black.
	 */
	static float SunLux() { return 10000.0f; }

	/**
	 * The exposure the interior is balanced for, as an EV100 figure.
	 *
	 * ## Where 7 comes from
	 *
	 * DERIVED, AND NOT YET MEASURED - which is stated plainly because the placeholder's own comment
	 * is a warning about exactly this: its ambient fill was chosen against dark placeholder
	 * surfaces, the material milestone raised every albedo, nobody re-measured, and 73% of the
	 * living room ended up above sRGB 225 with a whole room living inside six levels.
	 *
	 * The reasoning: a domestic room lit to something like 250 lux, with the material library's wall
	 * paint at 0.79 albedo, has a wall luminance of E * rho / pi = 250 * 0.79 / pi, about 63 cd/m2.
	 * The EV100 that puts a given luminance at MIDDLE GREY is log2(L * 100 / 12.5), which for 63 is
	 * about 9. A lit wall should not read as middle grey - it should read as a light surface, some
	 * two stops up - so the exposure wants to be about two stops more open than that: EV100 7.
	 *
	 * That it lands one stop brighter than the placeholder's 8 is a consistency check rather than a
	 * coincidence: this rig has removed the ambient cubemap that was carrying the placeholder's
	 * interiors, so it needs to open up to compensate.
	 *
	 * ## What would actually settle it
	 *
	 * A histogram of one interior render at this exposure, which is a minute of work with a GPU and
	 * is what FHFViewingLight's own comment says the equivalent question cost. Until somebody does
	 * that, this is arithmetic, and it is written here rather than inlined so that when it is
	 * re-measured there is one number to change.
	 */
	static float InteriorEV100() { return 7.0f; }

	/**
	 * Applies the rig's exposure, local exposure and Lumen choices to a set of post-process
	 * settings.
	 *
	 * Public so a camera can be given the same treatment as the level's own volume when somebody
	 * has overridden or deleted that volume.
	 */
	static void ApplyInteriorSettingsTo(FPostProcessSettings& Settings);
};
