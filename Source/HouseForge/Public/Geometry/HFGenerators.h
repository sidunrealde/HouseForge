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
	 * A wall, with its openings cut out and the structure it is built around taken out of it.
	 *
	 * Built from the centreline outward, so changing thickness grows the wall symmetrically and
	 * leaves its neighbours' junctions alone. Openings are subtracted as boxes that overshoot the
	 * wall faces, because a cutter flush with the surface leaves coplanar faces the boolean has to
	 * resolve and often does badly.
	 *
	 * @param Structure Beams and columns passing through this wall. THE RCC FRAME GOES UP FIRST AND
	 *        THE BLOCKWORK INFILLS AROUND IT: masonry is built up to a beam's soffit and butted to a
	 *        column's face, so the wall does not exist where the structure is. Leaving it there gave
	 *        both members the same faces to draw, and the whole flat flashed - see FHFStructuralCut.
	 *        The composing layer works out which members reach this wall; a generator cannot know.
	 */
	static UE::Geometry::FDynamicMesh3 GenerateWall(const FHFWall& Wall, const TArray<FHFOpening>& OpeningsInWall,
		const TArray<FHFStructuralCut>& Structure = TArray<FHFStructuralCut>());

	/**
	 * The volume a beam or a column displaces, ready to be handed to another member as structure.
	 *
	 * Pure conversions, and here rather than on the structs because a beam's orientation comes from
	 * its centreline and only this file knows how that is resolved.
	 */
	static FHFStructuralCut StructuralCutFor(const FHFBeam& Beam);
	static FHFStructuralCut StructuralCutFor(const FHFColumn& Column);

	/**
	 * A wall as a volume, for the wall it is built through.
	 *
	 * Masonry displaces masonry too. Walls are set out on their CENTRELINES, so a balcony parapet
	 * running to the main wall's centreline buries its last 115 in it and the two share a footprint.
	 * On site one run is built through and the other butts to its face; here the one that runs
	 * through is handed to the one that stops, exactly as a beam is.
	 */
	static FHFStructuralCut StructuralCutFor(const FHFWall& Wall);

	/**
	 * A room's floor slab, plus skirting swept around its boundary.
	 *
	 * Doorways are omitted from the skirting: a continuous skirting across a door opening is one
	 * of the most obvious tells that geometry was generated rather than modelled.
	 *
	 * @param WallFaceInsets How far the finished wall face stands in from each boundary EDGE - edge
	 *        i running Boundary[i] to Boundary[i+1] - normally half the thickness of the wall set
	 *        out on that line. A room boundary is a centreline, so without this the skirting is
	 *        laid along the middle of the masonry and buried in it. Empty, or short, means zero for
	 *        the edges it does not cover, which is what a caller with no walls to consult wants.
	 */
	static UE::Geometry::FDynamicMesh3 GenerateFloor(const FHFRoom& Room, double SlabThickness,
		const TArray<FVector2D>& SkirtingGaps, double GapWidth,
		const TArray<double>& WallFaceInsets = TArray<double>());

	/**
	 * A false ceiling.
	 *
	 * All five styles hang below the structural soffit, which is what makes them read as a
	 * suspended ceiling rather than a slab: a peripheral band leaves the centre open to the
	 * structure above, a full drop covers everything, a tray steps between two levels, a cove adds
	 * a shielded channel for an LED strip, and a bulkhead follows its own polygon.
	 *
	 * On top of the style, three things any of them can carry:
	 *
	 *   - a PERIMETER BULKHEAD ring, deeper than the ceiling inside it, boxing in the beams that
	 *     run round the room so the rest of the ceiling does not have to be as deep as they are;
	 *   - a CENTRE PANEL filling the middle higher than the band, which is what turns a band into a
	 *     frame and gives a cove something to wash;
	 *   - RECESSED DOWNLIGHTS at FHFFalseCeiling::LightPositions - a bore through the soffit, a trim
	 *     ring proud of it and an aperture up the can, rather than a dot on a plan.
	 *
	 * @param FanDrops Plan positions of ceiling fans, whose drop rods must pass through the
	 *        ceiling. A fan hanging from a soffit it does not penetrate is an obvious tell.
	 */
	static UE::Geometry::FDynamicMesh3 GenerateCeiling(const FHFFalseCeiling& Ceiling, const FHFRoom& Room,
		const TArray<FVector2D>& FanDrops, double FanDropRadius);

	/**
	 * Where this ceiling's recessed downlights actually are, in three dimensions.
	 *
	 * WHAT THE LIGHTING MILESTONE ATTACHES TO. LightPositions is a list of plan coordinates and says
	 * nothing about height, about which of them fitted, or about how far up the can the emitter
	 * sits - and a spotlight parented at the soffit plane instead of at the lens is shaded by its
	 * own trim. This answers all three from the same layout the geometry was built from, so a
	 * fitting that was skipped for not fitting the band does not come back as a light hanging in
	 * open air.
	 *
	 * Pure, like everything else here. The composing layer asks; nothing is spawned.
	 */
	static TArray<FVector> CeilingDownlights(const FHFFalseCeiling& Ceiling, const FHFRoom& Room);

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

	/**
	 * A downstand beam, hanging below the slab soffit.
	 *
	 * @param Structure Columns this beam lands on, and any beam that runs through it. A column is
	 *        cast before the beams that frame into it, and where two beams cross one is continuous
	 *        and the other stops at its face. Two beam soffits sharing a patch of the same plane is
	 *        a flash directly overhead, in the one surface a room's ceiling is made of.
	 */
	static UE::Geometry::FDynamicMesh3 GenerateBeam(const FHFBeam& Beam,
		const TArray<FHFStructuralCut>& Structure = TArray<FHFStructuralCut>());

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
