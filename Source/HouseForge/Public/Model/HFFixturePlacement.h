// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Model/HFTypes.h"

/**
 * Where a fixture stands, and which way it faces.
 *
 * LIFTED OUT OF AHFWardrobeActor::PlacementFor, unchanged, because thirteen more fixture types in
 * this milestone need exactly the same answer. A drawing states a yaw and a wall, and neither on its
 * own says which way a run faces: a run lies the same way whichever of its two faces is against the
 * wall, so a wardrobe, a base unit and a TV console placed off the rotation alone are each a
 * one-in-two chance of a wall of shutters facing a wall. Copied thirteen times, it would be thirteen
 * chances to copy it slightly wrong, and each one looks right in plan.
 *
 * ## Frames
 *
 * Every function returns an actor transform in world centimetres. The origin each one puts on the
 * actor differs, and that difference is the whole content of the type:
 *
 *   AgainstWall   the FRONT-LEFT CORNER of the footprint, on the floor. The corner a fitted run is
 *                 actually set out from on site, and the datum FHFCasedGoodsParams,
 *                 FHFCounterParams and FHFPlinthParams are all specified against.
 *   FreeStanding  the CENTRE of the footprint. Loose furniture has no back and no set-out corner;
 *                 a drawing places a dining table by where it sits, not by a corner of it.
 *   OnSurface     the CENTRE of the footprint, at a Z the caller resolved from the host. A sink and
 *                 a hob are set INTO a counter, and their cutouts are symmetric about their middle.
 *
 * ## Purity
 *
 * Pure functions of their arguments: no world, no actor, no settings - see
 * .claude/rules/04-conventions.md. Whatever units the caller works in are the units returned;
 * AHFHouseActor works in centimetres because SetSpec converts exactly once at ingest.
 */
class HOUSEFORGE_API FHFFixturePlacement
{
public:
	/**
	 * The yaw that puts the fixture's back against the wall it is anchored to.
	 *
	 * The drawing's own angle where there is no anchor wall, which is all there is to go on. Local
	 * +Y runs BACK into the unit, so the test is whether that direction already points away from the
	 * wall - and if it does, the whole run is turned through half a turn.
	 */
	static double FacingYaw(const FHFFixture& Fixture, const FHFWall* AnchorWall);

	/**
	 * Back to the wall, origin at the front-left corner of the footprint.
	 *
	 * Z is the room's floor plus the fixture's own BaseZ, so this places a floor-standing run and a
	 * wall-hung one identically - a wall cabinet is a carcass whose bottom happens to be at 140.
	 *
	 * @param FloorZ Finished floor level of the room the fixture stands in.
	 * @param AnchorWall The wall it backs onto, or null.
	 */
	static FTransform AgainstWall(const FHFFixture& Fixture, double FloorZ, const FHFWall* AnchorWall);

	/** Origin at the centre of the footprint, on the floor at the fixture's own BaseZ. */
	static FTransform FreeStanding(const FHFFixture& Fixture, double FloorZ);

	/**
	 * Origin at the centre of the footprint, at a Z the caller has resolved from the host.
	 *
	 * NOT AT THE FIXTURE'S OWN BaseZ, and that is the point of it being a separate function. A sink
	 * drawn at 690 is drawn there because somebody added up a 720 carcass and a 40 counter on a 100
	 * plinth; the counter's thickness comes from the project's settings, so the drawn figure is
	 * stale the moment anybody changes them. The host's built top is asked for instead, exactly as a
	 * ceiling fan's rod length is resolved rather than declared.
	 *
	 * @param SurfaceZ World Z of the finished surface the fixture is set into.
	 * @param YawDegrees The HOST's resolved yaw, not the fixture's own. A hob set into a counter
	 *        turns with the counter; a cutout square to the drawing in a run turned through a right
	 *        angle is a cutout across the run.
	 */
	static FTransform OnSurface(const FHFFixture& Fixture, double SurfaceZ, double YawDegrees);

	/** True when a point in plan falls inside the fixture's oriented footprint, grown by Margin. */
	static bool FootprintContains(const FHFFixture& Fixture, const FVector2D& Point, double Margin = 0.0);

	/**
	 * The world Z range a fixture's drawn box occupies in its room.
	 *
	 * Ceiling-mounted fixtures are measured DOWN from the ceiling, so this is not simply BaseZ and
	 * BaseZ + Height - see FHFFixture::IsCeilingMounted. Used to decide whether two fixtures are
	 * even at the same level before asking whether they collide in plan.
	 */
	static void WorldZRange(const FHFFixture& Fixture, const FHFRoom* Room, double& OutBottomZ,
		double& OutTopZ);
};
