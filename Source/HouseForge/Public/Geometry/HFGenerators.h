// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
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
	 * Returns an empty mesh for an archway, which is a hole and nothing else.
	 */
	static UE::Geometry::FDynamicMesh3 GenerateOpeningInfill(const FHFOpening& Opening, const FHFWall& Wall);

	/** Centre point of an opening along its wall, in plan. */
	static FVector2D OpeningCentre(const FHFOpening& Opening, const FHFWall& Wall);
};
