// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Geometry/HFCoplanarScan.h"

/** One pair of solids caught occupying the same space, with the evidence. */
struct HOUSEFORGE_API FHFClash
{
	FString NameA;
	FString NameB;

	/**
	 * How far the deepest inside point lies from the surface it is inside, in centimetres.
	 *
	 * A real distance to a real surface, not an overlap of bounding boxes: it answers "how much
	 * material is over this point", which is what says whether a clash is a scribe joint or a
	 * cupboard standing in a wall.
	 *
	 * IT IS A LOWER BOUND ON HOW FAR THE THING WOULD HAVE TO MOVE TO COME OUT, and deliberately so.
	 * Distance is to the NEAREST face of the other solid, whichever way that lies, so a fixture
	 * buried 40 cm into a wall but 5 cm from its end reports 5. Both numbers are true and the
	 * smaller one is the safe one to assert against: a scan that overstated depth would fail on
	 * geometry that was fine. Nothing is ever reported as shallower than it is, which is the
	 * property a test needs.
	 */
	double DepthCm = 0.0;

	/** World point where that depth was measured, so the report says where to go and look. */
	FVector3d Sample = FVector3d::Zero();

	/**
	 * Roughly how much of one is inside the other, in cubic centimetres.
	 *
	 * Sampled on a grid, so it is an estimate and reads as one. It exists to separate a corner
	 * clipping a reveal from a fixture standing bodily inside a wall - two clashes at the same depth
	 * that are not the same defect.
	 */
	double VolumeCm3 = 0.0;
};

/** What counts as a clash rather than as contact. */
struct HOUSEFORGE_API FHFClashScanParams
{
	/**
	 * Penetration shallower than this is contact, not a clash, in centimetres.
	 *
	 * THINGS IN A BUILDING TOUCH. A wall unit's back is screwed to the plaster, a railing's base
	 * plate stands on its coping, a skirting is pressed onto the slab; every one of those is a
	 * surface pair at zero separation, and every one is correct. What is never correct is one solid
	 * standing INSIDE another, and the two are separated by a distance rather than by intent.
	 *
	 * 0.5 mm is a twentieth of the thinnest thing the plugin builds and far below anything a camera
	 * resolves, so a genuine contact never trips it. Anything a fixture is deliberately driven into
	 * - a pelmet bedded 3 mm up into its ceiling - is above it, on purpose: a deliberate bite is
	 * declared by the caller that wants it, not hidden under a threshold here.
	 */
	double DepthToleranceCm = 0.05;

	/**
	 * Pitch of the grid used to find crossings and to estimate volume, in centimetres.
	 *
	 * Only the volume figure depends on it. Depth is measured at the vertices and at whichever grid
	 * points land inside, and the deepest point of an interpenetration between two boxes is always a
	 * vertex of one of them.
	 */
	double SampleGridCm = 2.0;

	/** Cap on grid points per pair. The pitch is coarsened to meet it rather than the box clipped. */
	int32 MaxSamplesPerPair = 20000;
};

/**
 * Finds solids standing inside one another.
 *
 * ## Why this is not the coplanar scan
 *
 * FHFCoplanarScan answers "do two surfaces claim the same plane", which is a rendering question. It
 * is deliberately blind to a fixture buried in a wall: a wardrobe 40 mm inside the plaster presents
 * no coplanar faces at all, so nothing flashes, every mesh is watertight, every bound is what it was
 * declared to be, and the flat is wrong. Both geysers in this flat spent a milestone with 107.5 mm
 * of their backs inside a partition and every test was green.
 *
 * Interpenetration is a property of a PAIR of solids and it is measured as a depth, not an area.
 *
 * ## How
 *
 * For each pair of surfaces whose world bounds meet, every vertex of one that lies inside the other
 * is found by winding number and its distance to that other's surface is taken. That distance is the
 * penetration depth, and it is exact for the case that matters: where two solids cross, the deepest
 * inside point is a vertex of one of them.
 *
 * A coarse grid over the shared box runs as well, for the case with no vertex inside either - two
 * thin plates crossing, a rail passing through a post. It also gives the volume estimate.
 *
 * Pure: (surfaces, params) -> report. No world, no actor, no editor - the meshes come in already
 * placed. See .claude/rules/04-conventions.md.
 *
 * ## What is deliberately NOT reported
 *
 * **Surfaces sharing an Owner.** Parts of one fixture are that fixture's own business and its kit's
 * tests are where they belong: a drawer inside its carcass is a drawer in the right place, and a
 * handle is screwed through the shutter it is on. Leave Owner unset and everything is compared.
 *
 * **A surface against itself**, for the same reason the coplanar scan skips it.
 *
 * ## What this scan is NOT for, and it matters
 *
 * ROOM ELEMENTS LAP INTO MASONRY ON PURPOSE. Every floor, ceiling and skirting in HouseForge runs to
 * the wall CENTRELINES, because that is the boundary a plan gives you - the slab bears on the wall,
 * the ceiling dies into it. Those laps are what guarantee there is no hairline crack at the junction,
 * and they are interpenetrations of five to twelve centimetres. Scan structure against structure and
 * the report is nothing but them.
 *
 * So a caller scans what does NOT lap: fixtures against fixtures, and fixtures against the building.
 * A fixture is manufactured and delivered - it stands against the plaster, it never grows into it.
 */
class HOUSEFORGE_API FHFClashScan
{
public:
	/** Every interpenetration between distinct surfaces past the depth tolerance, deepest first. */
	static TArray<FHFClash> Find(TArrayView<const FHFScanSurface> Surfaces,
		const FHFClashScanParams& Params = FHFClashScanParams());

	/**
	 * Every interpenetration between a surface in Probes and one in Against, deepest first.
	 *
	 * The asymmetric form, for "does any of this foul any of that" - a fixture's moving parts against
	 * the flat around them - where comparing Against with itself would report the construction laps
	 * above and drown the answer.
	 */
	static TArray<FHFClash> FindBetween(TArrayView<const FHFScanSurface> Probes,
		TArrayView<const FHFScanSurface> Against,
		const FHFClashScanParams& Params = FHFClashScanParams());

	/** Deepest penetration in a report, in centimetres. Zero when it is empty. */
	static double DeepestCm(TArrayView<const FHFClash> Clashes);

	/** One line per clash, worst first, for a test failure message or a log. */
	static TArray<FString> Describe(TArrayView<const FHFClash> Clashes, int32 MaxLines = 20);
};
