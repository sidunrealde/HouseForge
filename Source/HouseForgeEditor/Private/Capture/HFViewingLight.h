// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Scene.h"

class AActor;
class UWorld;

/**
 * Just enough light to see the geometry by. A PLACEHOLDER, and it says so in the outliner.
 *
 * There are no materials and no lighting milestone yet - those are milestones 10 and 11 - so a
 * render of a freshly built flat comes back unlit grey or simply black, and an image nobody can
 * read is no better than no image at all. This is the smallest rig that makes generated geometry
 * legible: a sun, a sky light, a sky atmosphere for the sun to sit in, and an exposure that suits
 * an interior rather than a sunlit exterior.
 *
 * What it deliberately is NOT: the lighting milestone. No cove strips, no fixture lights, no time
 * of day, no IES profiles, nothing per-room. Anything built here would have to be designed around
 * later, and the whole rig is meant to be deleted in one call when the real thing lands - which is
 * what RemoveFrom is for, and why every actor carries the same tag.
 *
 * Two decisions in it are unphysical on purpose, because this is a viewing rig and not a
 * simulation:
 *
 *  - The sky light's lower hemisphere is NOT black. Left at the default, everything facing
 *    downward - soffits, the underside of a loft, the head of every opening - goes to nothing, and
 *    those are surfaces a plan and an interior both need to read.
 *
 *  - The sky light does not cast shadows. Occluded ambient in an interior with no ray tracing
 *    guaranteed leaves rooms at the back of the flat black, and a black room looks exactly like a
 *    room that failed to generate.
 *
 * Idempotent by construction: EnsureIn finds the rig before it spawns one, so calling it on every
 * capture and after every rebuild leaves exactly one of each actor in the level.
 */
class FHFViewingLight
{
public:
	/** The tag every actor of the rig carries. Membership of the rig IS this tag. */
	static const FName& Tag();

	/** Outliner folder the rig is filed under, so it is obvious it is not part of the house. */
	static FName OutlinerFolder() { return FName(TEXT("HouseForge/Placeholder lighting")); }

	/** The rig's actors in a world, in no particular order. Empty when there is no rig. */
	static TArray<AActor*> FindIn(UWorld* World);

	/**
	 * Makes sure the world has exactly one placeholder rig.
	 *
	 * @param bOutSpawned  Optional: true when a rig was created by this call, false when one was
	 *                     already there. Nothing else can tell those apart afterwards.
	 * @return The rig's actors.
	 */
	static TArray<AActor*> EnsureIn(UWorld* World, bool* bOutSpawned = nullptr);

	/** Deletes the rig. Returns how many actors went. For the lighting milestone to call. */
	static int32 RemoveFrom(UWorld* World);

	/**
	 * Exposure the rig is balanced for, as an EV100 figure.
	 *
	 * Published because a one-shot scene capture cannot use eye adaptation - there is no previous
	 * frame for it to adapt from, so the first and only frame comes back black or blown - and so
	 * every capture has to pin its exposure manually to the same value the rig was set up around.
	 * Two places needing the same number is exactly how they drift apart.
	 */
	static float InteriorEV100() { return 8.0f; }

	/**
	 * Pins a capture's own post-process settings to the rig's exposure.
	 *
	 * A scene capture renders one frame and stops. Eye adaptation needs a previous frame to adapt
	 * FROM, so the only frame a capture ever renders is exposed by whatever the adaptation buffer
	 * happened to hold - which for a fresh render target is nothing, and the image comes back
	 * black. Every capture calls this, and it is the same exposure the rig itself is set to.
	 */
	static void ApplyExposureTo(FPostProcessSettings& Settings);
};
