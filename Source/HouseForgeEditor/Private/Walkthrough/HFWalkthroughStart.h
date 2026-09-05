// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class APlayerStart;
class UWorld;
struct FHFHouseSpec;
struct FHFRoom;

/**
 * Where a walkthrough begins: a PlayerStart standing in the foyer, on the floor, facing in.
 *
 * ## Why this is not one line of SpawnActor
 *
 * Because a PlayerStart in the wrong place is worse than none at all. Press Play with the start
 * point inside a wall and the character either falls through the world or is ejected sideways
 * through the masonry, and the flat looks broken in a way that has nothing to do with the flat. The
 * one thing this class has to guarantee is that a person actually FITS where it puts them.
 *
 * So the position is not the room's centre. It is the first candidate - centre first, then a grid
 * over the room - at which a character-sized capsule finds nothing solid, which is the same
 * question the character movement component will ask a moment later when somebody presses Play.
 * The foyer of the reference flat is 1.8 m square with a shoe rack in it, a distribution board on
 * one wall and a 1050 mm front door leaf sweeping through it; its geometric centre is not obviously
 * free, and nothing but asking would establish that it is.
 *
 * ## Why the foyer
 *
 * Because it is where a person enters the dwelling, and a walkthrough that starts in the middle of
 * the master bedroom starts with its back to the flat. A spec with no foyer - a studio, or a
 * partial plan - falls back to its largest room, because the alternative is refusing to make the
 * level playable at all.
 *
 * ## Why a tag rather than "the first PlayerStart"
 *
 * A level may legitimately have PlayerStarts this plugin did not put there. Only the tagged ones are
 * ours to move or remove, exactly as with the lighting rig.
 */
class FHFWalkthroughStart
{
public:
	/** The tag the start point carries. Membership IS this tag. */
	static const FName& Tag();

	/** Outliner folder it is filed under. */
	static FName OutlinerFolder() { return FName(TEXT("HouseForge/Walkthrough")); }

	/** The start points this plugin placed. Empty when there are none. */
	static TArray<APlayerStart*> FindIn(UWorld* World);

	/**
	 * Makes sure the world has exactly one HouseForge start point, in the right place.
	 *
	 * Idempotent, and it MOVES an existing one rather than adding a second. A rebuild of a spec
	 * whose foyer has moved has to move the start with it, and a level that gained a start point
	 * per build would be a level nobody could tell had one.
	 *
	 * @param Spec The house, for its rooms. A spec with no rooms places nothing and returns null.
	 * @param bOutSpawned Optional: true when a start point was created rather than moved.
	 * @return The start point, or null when there is nowhere to put one.
	 */
	static APlayerStart* EnsureIn(UWorld* World, const FHFHouseSpec& Spec, bool* bOutSpawned = nullptr);

	/** Deletes the start points this plugin placed. Returns how many went. */
	static int32 RemoveFrom(UWorld* World);

	/**
	 * Works out where a walkthrough should start, without spawning anything.
	 *
	 * Separated from EnsureIn so the decision can be asked about on its own - which is what a test
	 * wanting to know whether the point is inside a room rather than inside a wall needs.
	 *
	 * @param World Optional. With a world, candidates are rejected by a capsule overlap against what
	 *              is actually built; without one, only the room outline is consulted. Passing null
	 *              is legitimate - it answers "where in this SPEC" rather than "where in this LEVEL".
	 * @param OutTransform Where to stand and which way to look.
	 * @param OutRoomId The room chosen, for a message worth reading.
	 * @return False when the spec has no room with any usable floor in it.
	 */
	static bool ResolveStart(const FHFHouseSpec& Spec, UWorld* World,
		FTransform& OutTransform, FName& OutRoomId);

	/**
	 * Which room a walkthrough starts in: the foyer, or the largest room when there is no foyer.
	 *
	 * Public because "it starts in the foyer" is the claim worth asserting, and asserting it should
	 * not require spawning a level.
	 */
	static const FHFRoom* StartRoom(const FHFHouseSpec& Spec);

	/**
	 * Radius of the capsule a candidate position has to be clear for, in centimetres.
	 *
	 * ACharacter's default capsule, because that is what will actually be walking around: 34 by 88.
	 * Deliberately NOT APlayerStart's own marker capsule, which is a different size and is only
	 * there to be clicked on in the viewport - sizing the clearance test against the marker would
	 * be testing whether the icon fits.
	 */
	static double CapsuleRadius() { return 34.0; }

	/** Half-height of that capsule, in centimetres. */
	static double CapsuleHalfHeight() { return 88.0; }

	/**
	 * How far above the slab the capsule stands, in centimetres.
	 *
	 * PUBLISHED BECAUSE TWO PLACES HAVE TO AGREE ABOUT IT, and when they did not the failure was
	 * exactly the one this class exists to prevent. The search tested each candidate with the
	 * capsule lifted by this much - without it every candidate intersects the floor it is standing
	 * on and the search rejects the whole room - and then the start point was placed with the
	 * capsule sitting flat on the slab. So the position that had been proved clear was not the
	 * position anybody was put at, and a capsule overlap at the placed point found the floor.
	 */
	static double FloorClearance() { return 2.0; }

	/** Height of the start ACTOR above the room floor: the capsule centre, clear of the slab. */
	static double StandingHeight() { return CapsuleHalfHeight() + FloorClearance(); }
};
