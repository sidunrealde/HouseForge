// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Model/HFArticulation.h"
#include "Model/HFTypes.h"

/**
 * Element generators.
 *
 * Each is a pure function from parameters to a mesh, in world centimetres. No world, no actor, no
 * asset loading - see .claude/rules/04-conventions.md. That is what makes them testable headlessly
 * and what will let the same code run in a commandlet later.
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
	static UE::Geometry::FDynamicMesh3 GenerateOpeningInfill(const FHFOpening& Opening, const FHFWall& Wall);

	/**
	 * The part of an opening's infill that never moves.
	 *
	 * A fixed window's frame and its glazing; a sliding window's outer frame and the tracks its
	 * sashes run on; a ventilator's frame. Where an opening has sashes, the glazing rides in THEM
	 * and is deliberately absent here - a fixed pane behind a closed sash is invisible, doubles the
	 * glass, and would make a closed window and an open one render identically.
	 */
	static UE::Geometry::FDynamicMesh3 GenerateOpeningFixedInfill(const FHFOpening& Opening, const FHFWall& Wall);

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
	static void BuildOpeningParts(const FHFOpening& Opening, const FHFWall& Wall, TArray<FHFMeshPart>& OutParts);

	/**
	 * A door leaf in its own local space: +X from the hinge, +Z from the sill.
	 *
	 * @param SwingSign  sign of the hinge rotation this leaf will be given. The leaf body hangs on
	 *                   the face it swings towards, so its back edge sweeps out of the reveal
	 *                   instead of through the masonry beside the jamb. Zero centres it on the wall.
	 */
	static UE::Geometry::FDynamicMesh3 GenerateDoorLeaf(const FHFOpening& Opening, double SwingSign = 0.0);

	/** Centre point of an opening along its wall, in plan. */
	static FVector2D OpeningCentre(const FHFOpening& Opening, const FHFWall& Wall);
};
