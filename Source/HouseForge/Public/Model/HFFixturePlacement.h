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
 *   OnWallFace    the CENTRE of the footprint, slid along its own back direction until its BACK
 *                 PLANE lies on the finished face of the wall. A bought fitting screwed to plaster.
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

	/**
	 * Back plane ON the finished face of the anchor wall, origin at the CENTRE of the footprint.
	 *
	 * ## What this is for, and why AgainstWall is not it
	 *
	 * AgainstWall puts the actor's origin where the drawing put the footprint's corner and leaves it
	 * there. That is exactly right for a fitted run - a wardrobe is MADE to the gap it is going into,
	 * so the drawn footprint is the object and the carpenter scribes it to whatever the plaster
	 * actually does. It is wrong for something BOUGHT and screwed to the wall, because a plan marks a
	 * geyser or a mirror by roughly where it goes, at a precision of "on that wall", and the fitting
	 * itself is a fixed size that has to end up against the finished face.
	 *
	 * In the reference flat that difference is not academic and it is not small:
	 *
	 *   - both geysers are drawn 400 deep, centred 150 off W_Mid_Upper's centreline. The wall is 115
	 *     thick, so its bathroom face is at 57.5 and the geyser's back lands 107.5 mm INSIDE the
	 *     masonry - a 45 kg pressure vessel a quarter buried in a partition, in both bathrooms.
	 *   - both mirrors are drawn 30 deep, 120 off W_Mid_Lower's centreline: 47.5 mm of daylight
	 *     BEHIND a mirror hanging on nothing.
	 *
	 * Neither is visible in the spec, both are obvious the moment the room is rendered, and they are
	 * the same defect the bedroom group found in two TV units and a shoe rack - a fixture set out from
	 * a wall's centreline instead of its face. So the answer is made a placement rule rather than a
	 * number written into each fixture: the fitting keeps its position ALONG the wall and its height
	 * exactly as drawn, and slides only along its own back direction until it touches the plaster.
	 *
	 * Z is the room floor plus the fixture's own BaseZ, so a geyser at 2100 and a WC at 0 place
	 * identically - which is what lets one rule serve the whole group.
	 *
	 * With no anchor wall there is no face to find and this is FreeStanding: the drawn position is
	 * all there is to go on, and inventing a wall would be worse than honouring the drawing.
	 *
	 * @param FloorZ Finished floor level of the room the fixture stands in.
	 * @param AnchorWall The wall it is screwed to, or null.
	 */
	static FTransform OnWallFace(const FHFFixture& Fixture, double FloorZ, const FHFWall* AnchorWall);

	/**
	 * Centred ON the wall's own centreline in plan, standing at the fixture's BaseZ. A balustrade.
	 *
	 * ## Why neither of the two rules above is this one
	 *
	 * A balcony railing does not stand in front of a parapet and it is not made to fit a gap: it
	 * stands ON TOP of one, on base plates bolted through the coping, and the coping's centre is where
	 * the plates go. AgainstWall leaves the drawn position alone, and OnWallFace lands the fixture's
	 * BACK on a face - both of which put a 60 mm railing entirely on the room side of a 115 mm coping,
	 * with nothing under any of its posts.
	 *
	 * In the reference flat that is exactly what the drawing does. All three railings are drawn 60 mm
	 * off their parapet's centreline, so 32.5 of a 60 wide railing overlaps the coping and 27.5 hangs
	 * over the balcony. Corrected here rather than in the spec, for the reason OnWallFace's correction
	 * is: it is a property of how a railing is fixed, not a number three fixtures should each carry a
	 * copy of.
	 *
	 * The position ALONG the wall and the height are untouched; only the offset across it is resolved.
	 * With no anchor wall there is no coping to find and this is FreeStanding.
	 *
	 * @param FloorZ Finished floor level of the balcony the railing stands on.
	 * @param AnchorWall The parapet it stands on, or null.
	 */
	static FTransform OnWallTop(const FHFFixture& Fixture, double FloorZ, const FHFWall* AnchorWall);

	/** How far OnWallTop has to move a fixture to centre it on the coping. Signed, along the back. */
	static double WallCentrelineCorrection(const FHFFixture& Fixture, const FHFWall* AnchorWall);

	/**
	 * Back plane on the plaster, and the TOP of the fixture at a soffit the caller resolved.
	 *
	 * ## Why a pelmet is not placed like a geyser
	 *
	 * OnWallFace puts a fitting at the height the drawing gave it, which is right for everything
	 * measured up from the floor - a socket at 300, a geyser at 2100, a mirror over a basin. A curtain
	 * pelmet is not measured up from anything. It is fixed to the ceiling, and its drawn BaseZ is the
	 * ceiling's height minus its own depth, worked out by whoever drew it against whatever the false
	 * ceiling was that week.
	 *
	 * In this flat that figure is 2350, and it is stale by 300 mm: the ceilings became shallow bands,
	 * the living room's soffit came up from 2550 to 2850, and the pelmets stayed where they were
	 * drawn. Built at 2350 they hang with 300 of bare wall above them - a box screwed near a ceiling
	 * rather than a step in it - and nothing in the spec says so.
	 *
	 * So the ceiling is asked instead of the drawing, exactly as a sink's rim height is asked of the
	 * counter it is set into rather than taken from its drawn 690. See OnSurface, which is the same
	 * argument the other way up.
	 *
	 * ## The embedment is deliberate
	 *
	 * The top board is pushed UP past the soffit by Embedment rather than stopped on it. A board that
	 * stops exactly on a plane leaves two coplanar faces flashing against each other, and a board that
	 * stops short of it leaves a slot into the plenum. It is screwed to the ceiling, so it goes into
	 * it - the same joint rule the frame kit's members follow.
	 *
	 * @param SoffitZ World Z of the finished ceiling over the fixture, resolved by the composing layer.
	 * @param AnchorWall The wall it is fixed to, or null - in which case only the height is resolved.
	 * @param Embedment How far the top is driven up into the ceiling. Small; see above.
	 */
	static FTransform UnderSoffit(const FHFFixture& Fixture, double SoffitZ, const FHFWall* AnchorWall,
		double Embedment = 0.3);

	/**
	 * How far OnWallFace has to move a fixture to bring its back onto the plaster. Signed.
	 *
	 * Positive when the fixture is standing off the wall and has to move towards it, negative when it
	 * is buried in the masonry and has to come out. Zero with no anchor wall.
	 *
	 * Public because it is the thing worth asserting: "the geyser is not in the wall" is a statement
	 * about this number, and it can be checked against a spec without building anything.
	 */
	static double WallFaceCorrection(const FHFFixture& Fixture, const FHFWall* AnchorWall);

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
