// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Geometry/HFOpeningParams.h"
#include "Model/HFArticulation.h"
#include "Model/HFTypes.h"

/**
 * Element generators.
 *
 * Each is a pure function from parameters to a mesh, in world centimetres. No world, no actor, no
 * asset loading, and no global state either - see .claude/rules/04-conventions.md. That is what
 * makes them testable headlessly and what will let the same code run in a commandlet later.
 *
 * The opening functions take an FHFOpeningBuildParams carrying the construction figures - leaf
 * thickness, sash section, track pitch - that used to be constants in the .cpp. It is DEFAULTED, and
 * the defaults are exactly those constants, so a caller that has nothing to say about construction
 * says nothing. A caller that does - the element actors, which resolve the project's settings into
 * one - passes it in. What must never happen is a generator reaching for the settings object itself:
 * that would put global state behind a pure function and make it untestable headlessly, which is the
 * failure HouseForge.Architecture.GeneratorsDoNotReadSettings exists to catch.
 */
class HOUSEFORGE_API FHFGenerators
{
public:
	/**
	 * A wall, with its openings cut out.
	 *
	 * Built from the centreline outward, so changing thickness grows the wall symmetrically and
	 * leaves its neighbours' junctions alone. Openings are subtracted as boxes that overshoot the
	 * wall faces, because a cutter flush with the surface leaves coplanar faces the boolean has to
	 * resolve and often does badly.
	 */
	static UE::Geometry::FDynamicMesh3 GenerateWall(const FHFWall& Wall, const TArray<FHFOpening>& OpeningsInWall);

	/**
	 * A room's floor slab, plus skirting swept around its boundary.
	 *
	 * Doorways are omitted from the skirting: a continuous skirting across a door opening is one
	 * of the most obvious tells that geometry was generated rather than modelled.
	 */
	static UE::Geometry::FDynamicMesh3 GenerateFloor(const FHFRoom& Room, double SlabThickness,
		const TArray<FVector2D>& SkirtingGaps, double GapWidth);

	/**
	 * A false ceiling.
	 *
	 * All five styles hang below the structural soffit, which is what makes them read as a
	 * suspended ceiling rather than a slab: a peripheral band leaves the centre open to the
	 * structure above, a full drop covers everything, a tray steps between two levels, a cove adds
	 * a shielded channel for an LED strip, and a bulkhead follows its own polygon.
	 *
	 * @param FanDrops Plan positions of ceiling fans, whose drop rods must pass through the
	 *        ceiling. A fan hanging from a soffit it does not penetrate is an obvious tell.
	 */
	static UE::Geometry::FDynamicMesh3 GenerateCeiling(const FHFFalseCeiling& Ceiling, const FHFRoom& Room,
		const TArray<FVector2D>& FanDrops, double FanDropRadius);

	/**
	 * How far below the structural slab this ceiling's panel hangs over a plan point. 0 if none does.
	 *
	 * WHAT A CEILING FAN'S ROD HAS TO GET THROUGH. A fan hangs from the structural slab - that is why
	 * GenerateCeiling cuts a hole for its rod at all - so a panel between the slab and the room is
	 * something the rod passes through and the canopy has to land below. A rod sized from a project
	 * constant instead built the whole rotor INSIDE the plasterboard of any room with a full drop:
	 * the blades end up edge-on slivers lying in the panel, and from underneath the fan reads as a
	 * bladed light fitting glued to the ceiling.
	 *
	 * The answer depends on WHERE, not just on the ceiling: a peripheral or cove ceiling leaves its
	 * centre open to the slab and a fan there needs no extra rod at all, which is exactly why every
	 * fan in the reference flat looked right and the trap stayed shut.
	 *
	 * Pure, and deliberately here rather than on the fan: it mirrors the switch in GenerateCeiling
	 * case for case, and the two have to agree about which parts of a room are covered. The composing
	 * layer asks this and resolves the rod - a generator never reads it.
	 *
	 * @return Distance from the slab down to the UNDERSIDE of whatever covers the point, in
	 *         centimetres. Zero means the point is open to the structure.
	 */
	static double CeilingSoffitDropAt(const FHFFalseCeiling& Ceiling, const FHFRoom& Room,
		const FVector2D& Point);

	/** The structural slab soffit over a room - what you see looking up where nothing conceals it. */
	static UE::Geometry::FDynamicMesh3 GenerateCeilingSlab(const FHFRoom& Room, double SlabThickness);

	/** A downstand beam, hanging below the slab soffit. */
	static UE::Geometry::FDynamicMesh3 GenerateBeam(const FHFBeam& Beam);

	/** A column. */
	static UE::Geometry::FDynamicMesh3 GenerateColumn(const FHFColumn& Column);

	/**
	 * What sits inside an opening: a door leaf, or a window's frame and glazing.
	 *
	 * The whole infill as one mesh, with every moving part in its closed pose. Kept for callers
	 * that want a static snapshot; an actor uses GenerateOpeningFixedInfill plus BuildOpeningParts
	 * instead, so its door can actually open.
	 *
	 * Returns an empty mesh for an archway, which is a hole and nothing else.
	 */
	static UE::Geometry::FDynamicMesh3 GenerateOpeningInfill(const FHFOpening& Opening, const FHFWall& Wall,
		const FHFOpeningBuildParams& Params = FHFOpeningBuildParams());

	/**
	 * The part of an opening's infill that never moves.
	 *
	 * A fixed window's frame and its glazing; a sliding window's outer frame and the tracks its
	 * sashes run on; a ventilator's frame. Where an opening has sashes, the glazing rides in THEM
	 * and is deliberately absent here - a fixed pane behind a closed sash is invisible, doubles the
	 * glass, and would make a closed window and an open one render identically.
	 */
	static UE::Geometry::FDynamicMesh3 GenerateOpeningFixedInfill(const FHFOpening& Opening, const FHFWall& Wall,
		const FHFOpeningBuildParams& Params = FHFOpeningBuildParams());

	/**
	 * The moving parts of an opening.
	 *
	 * A hinged leaf for a door; two panels on two tracks for a sliding door; two sashes on two
	 * tracks for a sliding window, one of them running; a top-hung sash for a ventilator. A fixed
	 * window and an archway have none, which is the honest answer rather than an omission.
	 *
	 * Pure, like every other generator: each part's mesh comes back in the part's own local space
	 * with the origin on its pivot - the hinge jamb for a swing door, the near jamb at the sill for
	 * a sliding unit, the hinge line at the head for a top-hung sash - and the caller places it.
	 * The leaf runs along local +X from the pivot, its thickness on local Y and its height on
	 * local Z from zero at the sill.
	 */
	static void BuildOpeningParts(const FHFOpening& Opening, const FHFWall& Wall, TArray<FHFMeshPart>& OutParts,
		const FHFOpeningBuildParams& Params = FHFOpeningBuildParams());

	/**
	 * A door leaf in its own local space: +X from the hinge, +Z from the sill.
	 *
	 * @param SwingSign  sign of the hinge rotation this leaf will be given. The leaf body hangs on
	 *                   the face it swings towards, so its back edge sweeps out of the reveal
	 *                   instead of through the masonry beside the jamb. Zero centres it on the wall.
	 */
	static UE::Geometry::FDynamicMesh3 GenerateDoorLeaf(const FHFOpening& Opening, double SwingSign = 0.0,
		const FHFDoorParams& Params = FHFDoorParams());

	/** Centre point of an opening along its wall, in plan. */
	static FVector2D OpeningCentre(const FHFOpening& Opening, const FHFWall& Wall);
};
