// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"

/**
 * One surface handed to the scan: a mesh, where it sits, and what to call it in the report.
 *
 * The mesh is borrowed, not copied - the caller keeps it alive for the duration of the scan. A
 * house is a few hundred of these and several of them are wardrobes.
 */
struct HOUSEFORGE_API FHFScanSurface
{
	/** What the report calls this surface. An element id, or an id and a part name. */
	FString Name;

	/** Borrowed. Must outlive the scan. */
	const UE::Geometry::FDynamicMesh3* Mesh = nullptr;

	/** Mesh space to world. */
	FTransform ToWorld = FTransform::Identity;
};

/** How close two faces have to be before they count as fighting over the same plane. */
struct HOUSEFORGE_API FHFCoplanarScanParams
{
	/**
	 * How far apart two planes may be and still be the same plane, in centimetres.
	 *
	 * This is a Z-FIGHTING tolerance, not a modelling one. Depth precision at architectural range
	 * is far finer than a millimetre, so two surfaces 1 mm apart resolve cleanly and read as a
	 * lap, not a fight. The default is deliberately tighter than the smallest real thickness the
	 * plugin builds - the thinnest is 5 mm of glass - so a genuine finish never trips it, and far
	 * too tight for a nudge-by-epsilon "fix" to pass it.
	 */
	double PlaneToleranceCm = 0.05;

	/**
	 * How nearly parallel two faces must be, in degrees.
	 *
	 * Faces pointing the SAME way only. Two surfaces facing each other across a butt joint - a
	 * wall's cut end against the column it stops at - are coincident and correct: exactly one of
	 * them faces any given camera and the other is culled. It is co-FACING pairs that flash.
	 */
	double NormalToleranceDegrees = 1.5;

	/**
	 * Overlap below this is not reported, in square centimetres.
	 *
	 * Triangulation of two independently-built solids meeting along an edge produces slivers of a
	 * few hundredths of a square centimetre, which are arithmetic rather than geometry. One square
	 * centimetre is far below anything a camera can resolve and far above that noise.
	 */
	double MinAreaCm2 = 1.0;

	/**
	 * How far off the shared plane the visibility probe is taken, in centimetres.
	 *
	 * Small enough to stay inside the thinnest thing that could be sealing the pair - a 15 cm floor
	 * slab, a 11.5 cm partition - and large enough to be clear of the plane itself.
	 */
	double SealProbeCm = 0.1;

	/**
	 * Cell size of the broad-phase grid, in centimetres.
	 *
	 * Performance only - the result does not depend on it. Coplanar faces must be spatially
	 * coincident, so bucketing by world cell turns an all-pairs triangle comparison over a whole
	 * flat into a local one.
	 */
	double BroadPhaseCellCm = 100.0;
};

/** One pair of surfaces caught claiming the same plane, with the evidence. */
struct HOUSEFORGE_API FHFCoplanarOverlap
{
	FString NameA;
	FString NameB;

	/** The direction both faces point. */
	FVector3d Normal = FVector3d::UnitZ();

	/** Total co-facing overlap area between the two, in square centimetres. */
	double AreaCm2 = 0.0;

	/** Largest gap found anywhere in that overlap, in centimetres. Near zero on a true fight. */
	double SeparationCm = 0.0;

	/** A world point inside the overlap, so the report says WHERE to go and look. */
	FVector3d Sample = FVector3d::Zero();

	/**
	 * True when the side both faces look at is solid, so neither is ever drawn.
	 *
	 * A coincidence buried in material cannot flash. See the note on sealed pairs below.
	 */
	bool bSealed = false;
};

/**
 * Finds surfaces that will z-fight.
 *
 * Z-fighting is two faces claiming the same plane and both being drawn: the depth test picks
 * whichever won this frame's arithmetic, and it picks differently as the camera moves, so the
 * surface strobes. It is invisible to every check this plugin had - both meshes are watertight,
 * both are wound correctly, both are the right size, and the triangle counts are exactly what they
 * should be. It took a person standing in the flat to see it.
 *
 * So it is measured directly. For each pair of surfaces whose bounds meet, every pair of triangles
 * that lie in the same plane within tolerance AND point the same way has its overlap area computed
 * exactly, by clipping one against the other in the shared plane. The answer is an area in square
 * centimetres, which is a number a test can assert on and a number that says how bad it is.
 *
 * Pure: (surfaces, params) -> report. No world, no actor, no editor - the meshes come in already
 * placed. See .claude/rules/04-conventions.md.
 *
 * ## What is deliberately NOT reported
 *
 * **Opposed faces.** A wall built up to a column has a cut end coincident with the column's face,
 * pointing into it. That is a butt joint, it is how the building goes together, and it cannot
 * flash: back-face culling draws exactly one of the two from any camera.
 *
 * **A surface against itself.** A single mesh's own faces are the generator's business, and a
 * boolean routinely leaves coincident triangles inside a solid where nothing can see them.
 *
 * ## Sealed pairs, which are reported but do not count
 *
 * Z-fighting needs a camera. Two co-facing surfaces buried inside material - a skirting's underside
 * and the wall's base, both pressed onto the floor slab; a ceiling's top cap and a beam's top, both
 * under the structural slab - are drawn by nothing, ever, and no amount of moving the camera will
 * make them strobe.
 *
 * This is not a loophole, it is the shape of the building. Every room element in HouseForge runs to
 * the WALL CENTRELINES, because that is the boundary a plan gives you: the floor slab laps into the
 * masonry it bears on, the skirting stops in it, the ceiling dies into it. Those laps are what
 * guarantee there is no hairline gap at the junction, and abandoning the convention to satisfy an
 * area figure would trade an invisible coincidence for a visible crack.
 *
 * So each pair is probed: one step along the shared normal, into the side both faces look at. If
 * that point is inside any element's solid the pair is marked sealed and left out of the exposed
 * total. It is still reported, because a pair that becomes sealed by accident is worth seeing.
 *
 * The probe is taken at the centre of the pair's LARGEST patch, so a pair that is sealed over part
 * of its area and open over the rest is judged by the bigger part. Every case in this plugin is
 * uniform - a whole face is either buried or it is not - but it is an approximation and it is one.
 */
class HOUSEFORGE_API FHFCoplanarScan
{
public:
	/** Every co-facing coplanar overlap between distinct surfaces, largest area first. */
	static TArray<FHFCoplanarOverlap> Find(TArrayView<const FHFScanSurface> Surfaces,
		const FHFCoplanarScanParams& Params = FHFCoplanarScanParams());

	/** Sum of every reported overlap, in square centimetres, sealed or not. */
	static double TotalAreaCm2(TArrayView<const FHFCoplanarOverlap> Overlaps);

	/** Sum of the overlaps something can actually see - the figure that means "this flashes". */
	static double ExposedAreaCm2(TArrayView<const FHFCoplanarOverlap> Overlaps);

	/** One line per overlap, worst first, for a test failure message or a log. */
	static TArray<FString> Describe(TArrayView<const FHFCoplanarOverlap> Overlaps, int32 MaxLines = 20);
};
